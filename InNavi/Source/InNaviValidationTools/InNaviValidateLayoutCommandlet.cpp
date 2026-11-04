#include "InNaviValidateLayoutCommandlet.h"

#include "LayoutValidation.h"
#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "NavigationRoutePlanner.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

UInNaviValidateLayoutCommandlet::UInNaviValidateLayoutCommandlet()
{
    IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}

int32 UInNaviValidateLayoutCommandlet::Main(const FString& Params)
{
    FString Input, Output;
    if (!FParse::Value(*Params, TEXT("Input="), Input))
    {
        UE_LOG(LogTemp, Error, TEXT("Use -run=InNaviValidateLayout -Input=<project.json> [-Report=<report.json>] [-FailOnWarnings].")); return 2;
    }
    Input = FPaths::ConvertRelativePathToFull(Input); FPaths::CollapseRelativeDirectories(Input);
    const bool bExport = FParse::Value(*Params, TEXT("Report="), Output);
    if (bExport)
    {
        Output = FPaths::ConvertRelativePathToFull(Output); FPaths::CollapseRelativeDirectories(Output);
        if (Input.Equals(Output, ESearchCase::IgnoreCase)) { UE_LOG(LogTemp, Error, TEXT("Report output must differ from the input file.")); return 2; }
    }
    FString Text;
    TSharedPtr<FJsonObject> Project;
    LayoutValidation::FReport Report;
    if (FFileHelper::LoadFileToString(Text, *Input)
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Project) && Project)
        Report = LayoutValidation::ValidateProject(Project);
    else
    {
        LayoutValidation::FIssue Issue;
        Issue.Rule = TEXT("FILE_READ");
        Issue.JsonPath = TEXT("$");
        Issue.Message = TEXT("The file could not be read as a JSON project.");
        Issue.Suggestion = TEXT("Check the path and JSON syntax.");
        Report.Issues.Add(MoveTemp(Issue));
    }
    Report.Source = Input;
    if (!Report.HasErrors())
    {
        // Geometry is generated only in a disposable world. No current editor/project state is used or saved.
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (!World) return 2;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        auto* Buildings = World->SpawnActor<ABuildingGenerator>();
        auto* Interior = World->SpawnActor<AInteriorGenerator>();
        if (Buildings && Interior && Buildings->LoadBuildingsFromJson(Project) == Project->GetArrayField(TEXT("buildings")).Num())
        {
            Interior->DispatchBeginPlay();
            FNavigationRoutePlanner::ValidateLayoutConnectivity(*Buildings, *Interior, FString(), Report);
        }
        else
        {
            LayoutValidation::FIssue Issue;
            Issue.Rule = TEXT("NAVIGATION_LOAD_FAILED");
            Issue.Message = TEXT("The project could not be fully loaded for navigation checks.");
            Report.Issues.Add(MoveTemp(Issue));
        }
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    }
    else
    {
        LayoutValidation::FIssue Issue; Issue.Rule = TEXT("NAVIGATION_SKIPPED"); Issue.Severity = LayoutValidation::ESeverity::Warning;
        Issue.Message = TEXT("Navigation was not checked because the input contains data or geometry errors.");
        Report.Issues.Add(MoveTemp(Issue));
    }
    Report.Sort();
    for (const auto& Issue : Report.Issues)
        UE_LOG(LogTemp, Display, TEXT("[%s] %s %s L%d %s: %s"), LayoutValidation::SeverityName(Issue.Severity), *Issue.Rule, *Issue.BuildingId, Issue.Floor, *Issue.ObjectId, *Issue.Message);
    UE_LOG(LogTemp, Display, TEXT("Layout validation: %d errors, %d warnings; %d rooms checked, %d skipped."),
        Report.Count(LayoutValidation::ESeverity::Error), Report.Count(LayoutValidation::ESeverity::Warning), Report.RoomsChecked, Report.RoomsSkipped);
    if (bExport)
    {
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
        if (!LayoutValidation::ExportReport(Report, Output)) { UE_LOG(LogTemp, Error, TEXT("Could not write report: %s"), *Output); return 2; }
    }
    return Report.HasErrors() || (FParse::Param(*Params, TEXT("FailOnWarnings")) && Report.Count(LayoutValidation::ESeverity::Warning) > 0) ? 1 : 0;
}
