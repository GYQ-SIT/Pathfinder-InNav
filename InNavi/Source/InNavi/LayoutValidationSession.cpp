#include "LayoutValidationSession.h"
#include "Async/Async.h"

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "NavPlayerController.h"
#include "NavigationRoutePlanner.h"
#include "PolyBuilding.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

ULayoutValidationSession* ANavPlayerController::GetLayoutValidationSession()
{
    if (!LayoutValidationSession) LayoutValidationSession = NewObject<ULayoutValidationSession>(this);
    LayoutValidationSession->SetContext(Generator, InteriorGen, GetCurrentProjectFilePath());
    return LayoutValidationSession;
}

void ULayoutValidationSession::SetContext(ABuildingGenerator* InBuildings, AInteriorGenerator* InInterior, const FString& InSource)
{
    if (bRunning && (Buildings != InBuildings || Interior != InInterior || Source != InSource)) CancelValidate();
    if (bHasReport && (Buildings != InBuildings || Interior != InInterior || Source != InSource)) bStale = true;
    Buildings = InBuildings; Interior = InInterior; Source = InSource;
}

TSharedRef<FJsonObject> ULayoutValidationSession::Capture(const ABuildingGenerator& Generator, const AInteriorGenerator& Live)
{
    auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("version"), Generator.GetLoadedFileVersion());
    Root->SetNumberField(TEXT("scale"), Generator.GetScale());
    Root->SetStringField(TEXT("projectName"), Generator.GetProjectName());
    Root->SetStringField(TEXT("description"), Generator.GetProjectDescription());
    auto All = Generator.GetAllBuildings();
    All.Sort([](const APolyBuilding& A, const APolyBuilding& B) { return A.BuildingId < B.BuildingId; });
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const auto* Building : All)
    {
        if (!IsValid(Building)) continue;
        auto Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("id"), Building->BuildingId);
        Object->SetStringField(TEXT("name"), Generator.GetBuildingName(Building->BuildingId));
        Object->SetNumberField(TEXT("floorCount"), Building->GetFloorCount());
        Object->SetNumberField(TEXT("floorHeight"), Building->GetFloorHeight());
        Object->SetArrayField(TEXT("offset"), { MakeShared<FJsonValueNumber>(Building->BuildingOffset.X), MakeShared<FJsonValueNumber>(Building->BuildingOffset.Y) });
        TArray<TSharedPtr<FJsonValue>> Points;
        for (auto Point : Building->GetFootprint()) Points.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(Point.X), MakeShared<FJsonValueNumber>(Point.Y) }));
        Object->SetArrayField(TEXT("footprint"), Points);
        const auto Data = Live.GetLoadedBuildingId() == Building->BuildingId ? Live.CaptureInteriorSnapshot() : Generator.GetInteriorDataForBuilding(Building->BuildingId);
        if (Data) Object->SetObjectField(TEXT("interior"), Data);
        Values.Add(MakeShared<FJsonValueObject>(Object));
    }
    Root->SetArrayField(TEXT("buildings"), Values);
    // Stored JSON can be shared with the editor. Detach the entire snapshot before handing it to consumers.
    FString Text; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
    TSharedPtr<FJsonObject> Copy; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Copy);
    return Copy.IsValid() ? Copy.ToSharedRef() : Root;
}

void ULayoutValidationSession::Validate(const FString& ScopeBuildingId)
{
    BeginValidate(ScopeBuildingId, false);
    while (!StepValidate(TNumericLimits<double>::Max())) {}
}

void ULayoutValidationSession::StartValidate(const FString& ScopeBuildingId)
{
    BeginValidate(ScopeBuildingId, true);
    if (bRunning) TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ULayoutValidationSession::TickValidate));
}

void ULayoutValidationSession::BeginValidate(const FString& ScopeBuildingId, bool bAsync)
{
    CancelValidate();
    ++Generation;
    Report = {}; FocusedIssue = INDEX_NONE; bHasReport = false; bStale = false;
    RunScope = ScopeBuildingId;
    if (!IsValid(Buildings) || !IsValid(Interior))
    {
        LayoutValidation::FIssue Issue; Issue.Rule = TEXT("CONTEXT_MISSING"); Issue.Message = TEXT("Open a project before validating its layout.");
        Report.Issues.Add(MoveTemp(Issue)); bHasReport = true; return;
    }
    BuildingRevision = Buildings->GetNavigationRevision();
    InteriorRevision = Interior->GetNavigationRevision();
    const auto Snapshot = Capture(*Buildings, *Interior);
    if (bAsync)
    {
        // The worker sees only detached JSON. Building actors and route checks stay on the game thread.
        DataCheckCancelled = MakeShared<std::atomic<bool>>(false);
        DataCheck = Async(EAsyncExecution::ThreadPool, [Snapshot, Cancelled = DataCheckCancelled]()
        {
            return LayoutValidation::ValidateProject(Snapshot, [&Cancelled] { return Cancelled->load(); });
        });
        bRunning = true;
        return;
    }
    Report = LayoutValidation::ValidateProject(Snapshot);
    BeginRouteChecks();
}

void ULayoutValidationSession::BeginRouteChecks()
{
    Report.Source = Source;
    bDataHadErrors = Report.HasErrors();
    DataIssueCount = Report.Issues.Num();
    if (!bDataHadErrors)
    {
        ++RouteCheckRuns;
        Check = MakeShared<FNavigationLayoutCheck>(*Buildings, *Interior, RunScope);
        bRunning = true;
        return;
    }
    LayoutValidation::FIssue Issue; Issue.Rule = TEXT("NAVIGATION_SKIPPED"); Issue.Severity = LayoutValidation::ESeverity::Warning;
    Issue.Message = TEXT("Navigation was not checked because the project contains data or geometry errors.");
    Issue.Suggestion = TEXT("Resolve data errors and validate again. The All buildings scope shows errors in other buildings.");
    Report.Issues.Add(MoveTemp(Issue));
    FinishValidate();
}

bool ULayoutValidationSession::StepValidate(double BudgetSeconds)
{
    if (!bRunning) return true;
    if (!IsValid(Buildings) || !IsValid(Interior)
        || Buildings->GetNavigationRevision() != BuildingRevision || Interior->GetNavigationRevision() != InteriorRevision)
    {
        LayoutValidation::FIssue Issue; Issue.Rule = TEXT("NAVIGATION_SKIPPED"); Issue.Severity = LayoutValidation::ESeverity::Warning;
        Issue.Message = TEXT("The project changed or closed while validating. Validate it again.");
        Report.Issues.Add(MoveTemp(Issue));
        FinishValidate();
        bStale = true;
        return true;
    }
    if (DataCheck.IsValid())
    {
        if (!DataCheck.IsReady() && BudgetSeconds != TNumericLimits<double>::Max()) return false;
        Report = DataCheck.Get();
        DataCheck = {};
        DataCheckCancelled.Reset();
        BeginRouteChecks();
        return !bRunning;
    }
    if (!Check->Step(BudgetSeconds, Report)) return false;
    FinishValidate();
    return true;
}

void ULayoutValidationSession::FinishValidate()
{
    StopTicker();
    CancelDataCheck();
    Check.Reset(); bRunning = false;
    RouteIssues.Reset();
    for (int32 I = DataIssueCount; I < Report.Issues.Num(); ++I) RouteIssues.Add(Report.Issues[I]);
    Report.FilterToBuilding(RunScope); Report.Sort(); bHasReport = true; bStale = false;
    ++Generation;
}

void ULayoutValidationSession::CancelValidate()
{
    StopTicker();
    if (!bRunning) return;
    CancelDataCheck();
    Check.Reset(); bRunning = false;
    Report = {}; bHasReport = false; ++Generation;
}

void ULayoutValidationSession::CancelDataCheck()
{
    if (DataCheckCancelled) DataCheckCancelled->store(true);
    DataCheckCancelled.Reset();
    DataCheck = {};
}

float ULayoutValidationSession::GetProgress() const { return bRunning && Check ? Check->GetProgress() : bHasReport ? 1.f : 0.f; }
FString ULayoutValidationSession::GetProgressText() const
{
    if (DataCheck.IsValid()) return TEXT("Checking layout data");
    return bRunning && Check ? Check->GetPhase() : FString();
}

void ULayoutValidationSession::StopTicker()
{
    if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    TickHandle.Reset();
}

bool ULayoutValidationSession::TickValidate(float DeltaTime)
{
    // About half a frame at 60 fps, so the editor keeps answering while this works.
    return !StepValidate(0.008);
}

void ULayoutValidationSession::BeginDestroy()
{
    StopTicker();
    CancelDataCheck();
    Super::BeginDestroy();
}

void ULayoutValidationSession::RefreshStaleness()
{
    if (!bHasReport || bStale) return;
    bStale = !IsValid(Buildings) || !IsValid(Interior) || LayoutValidation::Fingerprint(Capture(*Buildings, *Interior)) != Report.Fingerprint;
    if (bStale) FocusedIssue = INDEX_NONE;
}

bool ULayoutValidationSession::ShowIssue(ANavPlayerController& Controller, int32 Index)
{
    RefreshStaleness();
    if (bRunning || bStale || !Report.Issues.IsValidIndex(Index) || !IsValid(Buildings) || !IsValid(Interior)) return false;
    const auto Issue = Report.Issues[Index];
    if (Issue.BuildingId.IsEmpty() || !Buildings->FindBuildingById(Issue.BuildingId)) return false;
    // This explicit view action follows the controller's normal building-switch path, which preserves unsaved edits.
    if (Controller.GetLoadedBuildingId() != Issue.BuildingId && Controller.LoadInteriorForBuilding(Issue.BuildingId) < 0) return false;
    Controller.SetIn3DPreviewMode(false);
    if (Issue.Floor > 0) Controller.SetActiveFloor(Issue.Floor);
    switch (Issue.Kind)
    {
    case LayoutValidation::EObjectKind::Room: Controller.SelectRoom(Issue.ObjectId); break;
    case LayoutValidation::EObjectKind::Opening: Controller.SelectDoor(Issue.OwnerId, Issue.ObjectId); break;
    case LayoutValidation::EObjectKind::Connector: Controller.SelectConnector(Issue.ObjectId); break;
    case LayoutValidation::EObjectKind::Entrance: Controller.SelectEntrance(Issue.ObjectId); break;
    default: Controller.SelectRoom(FString()); break;
    }
    // Opening older JSON materialises defaults in the live model. Refresh read-only diagnostics after
    // that explicit view action, rather than mislabelling a representation change as an author edit.
    if (LayoutValidation::Fingerprint(Capture(*Buildings, *Interior)) != Report.Fingerprint)
    {
        RefreshAfterViewing();
        FocusedIssue = Report.Issues.IndexOfByPredicate([&](const LayoutValidation::FIssue& Current)
        {
            return Current.Rule == Issue.Rule && Current.BuildingId == Issue.BuildingId && Current.Floor == Issue.Floor
                && Current.Kind == Issue.Kind && Current.ObjectId == Issue.ObjectId && Current.OwnerId == Issue.OwnerId;
        });
    }
    else FocusedIssue = Index;
    return true;
}

void ULayoutValidationSession::RefreshAfterViewing()
{
    // Opening a building can fill in what its file left out (empty floors, say). That changes how the layout is written, not how
    // it routes, and the report was current just before the building was opened. So only the quick data checks are redone, and
    // what the route checks found is kept: the whole check would take as long as the first run, with the editor waiting.
    const FString Scope = Report.ScopeBuildingId;
    LayoutValidation::FReport Fresh = LayoutValidation::ValidateProject(Capture(*Buildings, *Interior));
    if (Fresh.HasErrors() != bDataHadErrors)
    {
        // The route checks would now be run, or left out, differently: do it all again.
        Validate(Scope);
        return;
    }
    Fresh.Source = Source;
    Fresh.bNavigationChecked = Report.bNavigationChecked; Fresh.RoomsChecked = Report.RoomsChecked; Fresh.RoomsSkipped = Report.RoomsSkipped;
    for (const auto& Issue : RouteIssues) Fresh.Issues.Add(Issue);
    Fresh.FilterToBuilding(Scope); Fresh.Sort();
    auto Describe = [](const LayoutValidation::FReport& Of)
    {
        TArray<FString> Lines;
        for (const auto& I : Of.Issues) Lines.Add(FString::Printf(TEXT("%s|%s|%d|%d|%s|%s|%s"), *I.Rule, *I.BuildingId, I.Floor, static_cast<int32>(I.Kind), *I.ObjectId, *I.OwnerId, *I.Message));
        return Lines;
    };
    // The list on the panel is rebuilt (and scrolls back to the top) only when what it lists has changed.
    const bool bSameIssues = Describe(Fresh) == Describe(Report);
    Report = MoveTemp(Fresh); bStale = false;
    if (!bSameIssues) ++Generation;
}

const LayoutValidation::FIssue* ULayoutValidationSession::GetFocusedIssue() const
{
    return !bStale && Report.Issues.IsValidIndex(FocusedIssue) ? &Report.Issues[FocusedIssue] : nullptr;
}

bool ULayoutValidationSession::Export(const FString& Path)
{
    RefreshStaleness();
    if (!bHasReport || bStale) return false;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    return LayoutValidation::ExportReport(Report, Path);
}
