#include "InNaviValidateLayoutCommandlet.h"

#include "LayoutValidation.h"
#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "NavigationRoutePlanner.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

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
    auto Report = LayoutValidation::ValidateFile(Input);
    if (!Report.HasErrors())
    {
        // Geometry is generated only in a disposable world. No current editor/project state is used or saved.
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (!World) return 2;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        auto* Buildings = World->SpawnActor<ABuildingGenerator>();
        auto* Interior = World->SpawnActor<AInteriorGenerator>();
        Interior->DispatchBeginPlay();
        Buildings->LoadBuildingsFromFile(Input);
        FNavigationRoutePlanner::ValidateLayoutConnectivity(*Buildings, *Interior, FString(), Report);
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
