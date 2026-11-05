#if WITH_DEV_AUTOMATION_TESTS

#include "LayoutValidation.h"
#include "LayoutValidationSession.h"
#include "LayoutValidationPanelWidget.h"
#include "InteriorScreenWidget.h"
#include "EditScreenWidget.h"
#include "InteriorDockLayout.h"
#include "ExteriorDockLayout.h"
#include "DockHostUMG.h"
#include "UiTestFixture.h"
#include "InteriorRoom.h"
#include "PolyBuilding.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

namespace InNaviValidationTests
{
    using namespace LayoutValidation;
    TSharedPtr<FJsonObject> Project()
    {
        TSharedPtr<FJsonObject> Root;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT(R"JSON({
          "version":1,"scale":1,"buildings":[{"id":"A","name":"Building A","floorCount":2,"floorHeight":300,
          "footprint":[[0,0],[1000,0],[1000,1000],[0,1000]],"interior":{
          "entrances":[{"id":"Main","kind":"main","level":1,"edge":0,"offset":500,"width":120}],
          "floors":[{"level":1,"rooms":[{"id":"R1","name":"Room 1","footprint":[[100,100],[400,100],[400,400],[100,400]],
          "openings":[{"id":"D1","type":"door","edgeIndex":0,"offset":100,"width":120,"connectsTo":"corridor"}]}],
          "navNodes":[{"id":"Lift1","type":"elevator","connectorGroupId":"Lift","position":[800,800],"connectsFloors":[1,2]}]},
          {"level":2,"rooms":[{"id":"R2","name":"Room 2","footprint":[[100,100],[400,100],[400,400],[100,400]],
          "openings":[{"id":"D1","type":"door","edgeIndex":0,"offset":100,"width":120}]}],
          "navNodes":[{"id":"Lift2","type":"elevator","connectorGroupId":"Lift","position":[800,800],"connectsFloors":[1,2]}]}]}}]})JSON"))), Root);
        return Root;
    }
    TSharedPtr<FJsonObject> Building(const TSharedPtr<FJsonObject>& Root) { return Root->GetArrayField(TEXT("buildings"))[0]->AsObject(); }
    TSharedPtr<FJsonObject> Interior(const TSharedPtr<FJsonObject>& Root) { return Building(Root)->GetObjectField(TEXT("interior")); }
    TSharedPtr<FJsonObject> Floor(const TSharedPtr<FJsonObject>& Root, int32 Index = 0) { return Interior(Root)->GetArrayField(TEXT("floors"))[Index]->AsObject(); }
    TSharedPtr<FJsonObject> Room(const TSharedPtr<FJsonObject>& Root, int32 Index = 0) { return Floor(Root, Index)->GetArrayField(TEXT("rooms"))[0]->AsObject(); }
    bool Has(const FReport& Report, const TCHAR* Rule) { return Report.Issues.ContainsByPredicate([&](const FIssue& I) { return I.Rule == Rule; }); }
    FString Json(const TSharedPtr<FJsonObject>& Root) { FString Text; FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text; }

    struct FFixture : FNavigationSearchFixture
    {
        ANavPlayerController* Controller;
        explicit FFixture(const TSharedPtr<FJsonObject>& Root = Project())
        {
            Interior->ClearLoadedInterior(); Generator->ClearAllBuildings();
            Generator->SpawnBuildingFromData(TEXT("A"), TEXT("Building A"), { {0,0}, {1000,0}, {1000,1000}, {0,1000} }, 2, 300, FVector2D::ZeroVector);
            Generator->SetInteriorDataForBuilding(TEXT("A"), InNaviValidationTests::Interior(Root));
            Interior->LoadInteriorForBuilding(TEXT("A"));
            Controller = MakeController();
        }
    };

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDataRules, "InNavi.LayoutValidation.DataRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FDataRules::RunTest(const FString& Parameters)
    {
        auto Root = Project(); TestFalse(TEXT("valid authored project has no data errors"), ValidateProject(Root).HasErrors());
        const auto Before = Json(Root); ValidateProject(Root); TestEqual(TEXT("validation never rewrites input"), Json(Root), Before);
        auto Buildings = Root->GetArrayField(TEXT("buildings")); const auto Duplicate = Buildings[0]; Buildings.Add(Duplicate); Root->SetArrayField(TEXT("buildings"), Buildings);
        TestTrue(TEXT("duplicate buildings survive raw validation"), Has(ValidateProject(Root), TEXT("ID_DUPLICATE")));
        Root = Project(); Building(Root)->SetStringField(TEXT("floorCount"), TEXT("two"));
        TestTrue(TEXT("bad types produce diagnostics"), Has(ValidateProject(Root), TEXT("DATA_TYPE")));
        Root = Project(); Floor(Root)->SetArrayField(TEXT("rooms"), { MakeShared<FJsonValueNull>() });
        TestTrue(TEXT("null entries are safe to inspect"), Has(ValidateProject(Root), TEXT("DATA_TYPE")));
        Root = Project(); Floor(Root, 1)->SetNumberField(TEXT("level"), 1);
        TestTrue(TEXT("duplicate floor levels"), Has(ValidateProject(Root), TEXT("FLOOR_DUPLICATE")));
        Root = Project(); Root->RemoveField(TEXT("version"));
        TestFalse(TEXT("legacy files without versions are accepted"), ValidateProject(Root).HasErrors());
        Root->SetNumberField(TEXT("scale"), 0.000001);
        TestTrue(TEXT("a scale that the editor cannot load fails validation"), ValidateProject(Root).HasErrors());
        int32 FullCheckpoints = 0;
        ValidateProject(Project(), [&FullCheckpoints] { ++FullCheckpoints; return false; });
        int32 Checkpoints = 0;
        const auto Cancelled = ValidateProject(Project(), [&Checkpoints] { return ++Checkpoints >= 12; });
        TestTrue(TEXT("the data scan stops before completing"), Checkpoints >= 12 && Checkpoints < FullCheckpoints);
        TestTrue(TEXT("a cancelled scan does not publish a report"), Cancelled.Fingerprint.IsEmpty() && Cancelled.Issues.IsEmpty());
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryRules, "InNavi.LayoutValidation.GeometryAndOpenings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FGeometryRules::RunTest(const FString& Parameters)
    {
        auto Root = Project(); auto Door = Room(Root)->GetArrayField(TEXT("openings"))[0]->AsObject();
        Door->SetNumberField(TEXT("offset"), 290);
        TestTrue(TEXT("opening is checked in world units"), Has(ValidateProject(Root), TEXT("OPENING_BOUNDS")));
        Root->SetNumberField(TEXT("scale"), 10);
        TestFalse(TEXT("scaling increases the physical edge length"), Has(ValidateProject(Root), TEXT("OPENING_BOUNDS")));
        Root = Project(); Room(Root)->SetArrayField(TEXT("footprint"), { MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(100),MakeShared<FJsonValueNumber>(100)}), MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(400),MakeShared<FJsonValueNumber>(400)}), MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(100),MakeShared<FJsonValueNumber>(400)}), MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(400),MakeShared<FJsonValueNumber>(100)}) });
        TestTrue(TEXT("self-crossing polygon"), Has(ValidateProject(Root), TEXT("GEOMETRY_SELF_INTERSECTION")));
        Root = Project(); Room(Root)->SetBoolField(TEXT("closed"), false); Room(Root)->SetArrayField(TEXT("openings"), {});
        auto Points = Room(Root)->GetArrayField(TEXT("footprint")); Points.SetNum(2); Room(Root)->SetArrayField(TEXT("footprint"), Points);
        TestFalse(TEXT("a two-point open wall is valid"), ValidateProject(Root).HasErrors());
        Root = Project(); auto Openings = Room(Root)->GetArrayField(TEXT("openings"));
        auto Other = MakeShared<FJsonObject>(); Other->SetStringField(TEXT("id"), TEXT("D2")); Other->SetNumberField(TEXT("edgeIndex"), 0); Other->SetNumberField(TEXT("offset"), 110); Other->SetNumberField(TEXT("width"), 100);
        Openings.Add(MakeShared<FJsonValueObject>(Other)); Room(Root)->SetArrayField(TEXT("openings"), Openings);
        TestTrue(TEXT("overlapping openings are reported"), Has(ValidateProject(Root), TEXT("OPENING_OVERLAP")));
        TestFalse(TEXT("overlap is advisory"), ValidateProject(Root).HasErrors());

        Root = Project();
        auto Rooms = Floor(Root)->GetArrayField(TEXT("rooms"));
        auto OtherRoom = MakeShared<FJsonObject>();
        OtherRoom->SetStringField(TEXT("id"), TEXT("Adjacent"));
        Rooms.Add(MakeShared<FJsonValueObject>(OtherRoom));
        Floor(Root)->SetArrayField(TEXT("rooms"), Rooms);
        auto SetOutline = [&](std::initializer_list<FVector2D> Outline)
        {
            TArray<TSharedPtr<FJsonValue>> Points;
            for (const FVector2D& Point : Outline)
            {
                Points.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
                    MakeShared<FJsonValueNumber>(Point.X), MakeShared<FJsonValueNumber>(Point.Y)}));
            }
            OtherRoom->SetArrayField(TEXT("footprint"), Points);
        };
        SetOutline({{400, 400}, {400, 100}, {100, 100}, {100, 400}});
        TestTrue(TEXT("identical area is found with different winding and start vertex"), Has(ValidateProject(Root), TEXT("ROOM_OVERLAP")));
        SetOutline({{200, 100}, {500, 100}, {500, 400}, {200, 400}});
        TestTrue(TEXT("overlap is found along collinear edges"), Has(ValidateProject(Root), TEXT("ROOM_OVERLAP")));
        SetOutline({{400, 100}, {500, 100}, {500, 400}, {400, 400}});
        TestFalse(TEXT("a shared edge alone is allowed"), Has(ValidateProject(Root), TEXT("ROOM_OVERLAP")));
        SetOutline({{400, 400}, {500, 400}, {500, 500}, {400, 500}});
        TestFalse(TEXT("a shared corner alone is allowed"), Has(ValidateProject(Root), TEXT("ROOM_OVERLAP")));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReferenceRules, "InNavi.LayoutValidation.ReferencesAndLegacy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FReferenceRules::RunTest(const FString& Parameters)
    {
        auto Root = Project(); auto Door = Room(Root)->GetArrayField(TEXT("openings"))[0]->AsObject(); Door->SetStringField(TEXT("connectsTo"), TEXT("Deleted room"));
        TestTrue(TEXT("missing metadata reference"), Has(ValidateProject(Root), TEXT("DOOR_REFERENCE")));
        TestFalse(TEXT("metadata is not a physical routing failure"), ValidateProject(Root).HasErrors());
        Root = Project(); Floor(Root, 1)->SetArrayField(TEXT("navNodes"), {});
        TestTrue(TEXT("missing connector instance"), Has(ValidateProject(Root), TEXT("CONNECTOR_INSTANCE_MISSING")));
        Root = Project(); for (int32 I=0; I<2; ++I) { auto Node=Floor(Root,I)->GetArrayField(TEXT("navNodes"))[0]->AsObject(); Node->RemoveField(TEXT("connectorGroupId")); Node->RemoveField(TEXT("connectsFloors")); }
        TestFalse(TEXT("legacy ungrouped connectors retain planner fallback"), ValidateProject(Root).HasErrors());
        Root = Project(); Interior(Root)->RemoveField(TEXT("floors")); Interior(Root)->RemoveField(TEXT("entrances"));
        TestFalse(TEXT("unauthored interior is informational"), ValidateProject(Root).HasErrors());
        TestTrue(TEXT("unauthored interior is explicit"), Has(ValidateProject(Root), TEXT("INTERIOR_UNAUTHORED")));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReachability, "InNavi.LayoutValidation.Reachability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FReachability::RunTest(const FString& Parameters)
    {
        FFixture Fixture; auto* Session = Fixture.Controller->GetLayoutValidationSession(); Session->Validate();
        for (const auto& Issue : Session->GetReport().Issues) AddInfo(Issue.Rule + TEXT(": ") + Issue.Message);
        TestTrue(TEXT("navigation checked"), Session->GetReport().bNavigationChecked);
        TestEqual(TEXT("both floors' destinations checked"), Session->GetReport().RoomsChecked, 2);
        TestFalse(TEXT("doors and lift make both rooms reachable"), Has(Session->GetReport(), TEXT("ROOM_UNREACHABLE")));
        auto Root = Project(); Room(Root,1)->SetArrayField(TEXT("openings"), {});
        Fixture.Interior->ClearLoadedInterior(); Fixture.Generator->SetInteriorDataForBuilding(TEXT("A"), InNaviValidationTests::Interior(Root)); Fixture.Interior->LoadInteriorForBuilding(TEXT("A"));
        Session->Validate();
        TestTrue(TEXT("sealed room is unreachable"), Has(Session->GetReport(), TEXT("ROOM_UNREACHABLE")));
        const auto* Bad = Session->GetReport().Issues.FindByPredicate([](const FIssue& I){ return I.Rule==TEXT("ROOM_UNREACHABLE"); });
        TestTrue(TEXT("issue locates the sealed room"), Bad && Bad->ObjectId == TEXT("R2") && Bad->Floor == 2);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoEntrances, "InNavi.LayoutValidation.NoEntrances", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FNoEntrances::RunTest(const FString& Parameters)
    {
        auto Root=Project(); Interior(Root)->SetArrayField(TEXT("entrances"), {}); FFixture Fixture(Root);
        auto* Session=Fixture.Controller->GetLayoutValidationSession(); Session->Validate();
        TestTrue(TEXT("missing entry points reported"), Has(Session->GetReport(),TEXT("NO_ENTRY_POINTS")));
        TestFalse(TEXT("does not claim completed reachability"),Session->GetReport().bNavigationChecked);
        TestFalse(TEXT("does not report all rooms as broken"),Has(Session->GetReport(),TEXT("ROOM_UNREACHABLE")));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReadOnlyAndStale, "InNavi.LayoutValidation.ReadOnlyAndStaleness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FReadOnlyAndStale::RunTest(const FString& Parameters)
    {
        FFixture Fixture;
        auto* PC=Fixture.Controller; PC->SelectRoom(TEXT("R1")); PC->SetActiveFloor(1);
        auto* Session=PC->GetLayoutValidationSession();
        const FString Before=Fingerprint(ULayoutValidationSession::Capture(*Fixture.Generator,*Fixture.Interior));
        const bool bDirty=PC->HasUnsavedChanges(); Session->Validate();
        TestEqual(TEXT("no layout mutation"),Fingerprint(ULayoutValidationSession::Capture(*Fixture.Generator,*Fixture.Interior)),Before);
        TestEqual(TEXT("no selection mutation"),PC->GetSelectedRoomId(),FString(TEXT("R1")));
        TestEqual(TEXT("no floor change"),PC->GetActiveFloor(),1);
        TestEqual(TEXT("no dirty flag change"),PC->HasUnsavedChanges(),bDirty);
        TestFalse(TEXT("new report is current"),Session->IsStale());
        Fixture.Interior->SetRoomName(TEXT("R1"),TEXT("Unsaved name"));
        Session->RefreshStaleness(); TestTrue(TEXT("unsaved live edit invalidates report"),Session->IsStale());
        TestFalse(TEXT("stale issue actions rejected"),Session->ShowIssue(*PC,0));
        Session->Validate(); TestFalse(TEXT("rerun clears stale state"),Session->IsStale());
        TestTrue(TEXT("undo remains usable after validating"),Fixture.Interior->Undo());
        TestEqual(TEXT("validation introduced no history step"),Fixture.Interior->GetRoomById(TEXT("R1"))->RoomName,FString(TEXT("Room 1")));
        Session->RefreshStaleness(); TestTrue(TEXT("undo invalidates the report"),Session->IsStale());
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPanelIntegration, "InNavi.LayoutValidation.DockPanel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FPanelIntegration::RunTest(const FString& Parameters)
    {
        FFixture Fixture; auto* PC=Fixture.Controller;
        auto* Screen=NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(PC);
        auto* Panel=Screen->GetValidationPanelForTests(); if (!TestNotNull(TEXT("interior validation panel"),Panel)) return false;
        TestTrue(TEXT("registered with dock host"),Screen->GetDockHost()->IsPanelRegistered(InteriorDock::Validation));
        TestNotNull(TEXT("window entry"),Screen->FindScreenWidget(TEXT("Chk_WindowValidation")));
        auto* Run=Cast<UButton>(Panel->FindContentWidget(TEXT("Btn_ValidateLayout")));
        auto* Filter=Cast<UComboBoxString>(Panel->FindContentWidget(TEXT("Cbx_ValidationSeverity")));
        auto* Export=Cast<UButton>(Panel->FindContentWidget(TEXT("Btn_ExportValidation")));
        if (!TestTrue(TEXT("controls exist"),Run&&Filter&&Export)) return false;
        Panel->RefreshDisplay(); TestFalse(TEXT("export disabled before first run"),Export->GetIsEnabled());
        Run->OnClicked.Broadcast(); auto* Session=PC->GetLayoutValidationSession();
        TestTrue(TEXT("button starts the shared validation session"),Session->IsRunning());
        Panel->RefreshDisplay(); TestFalse(TEXT("a run cannot be started twice"),Run->GetIsEnabled());
        auto* Cancel=Cast<UButton>(Panel->FindContentWidget(TEXT("Btn_CancelValidation")));
        TestTrue(TEXT("cancel is offered while running"),Cancel&&Cancel->GetVisibility()==ESlateVisibility::Visible);
        TestFalse(TEXT("export waits for the report"),Export->GetIsEnabled());
        while (!Session->StepValidate(0.0)) {}
        Panel->RefreshDisplay(); TestTrue(TEXT("the session finishes with a report"),Session->HasReport());
        TestTrue(TEXT("cancel is hidden again"),Cancel&&Cancel->GetVisibility()==ESlateVisibility::Collapsed);
        TestTrue(TEXT("and validating is possible again"),Run->GetIsEnabled());
        TestTrue(TEXT("current report can be exported"),Export->GetIsEnabled());
        Fixture.Interior->GetRoomById(TEXT("R1"))->RoomName=TEXT("Edited"); Panel->RefreshDisplay();
        TestFalse(TEXT("stale report cannot be exported"),Export->GetIsEnabled());
        auto* Status=Cast<UTextBlock>(Panel->FindContentWidget(TEXT("Txt_ValidationStatus")));
        TestTrue(TEXT("stale warning visible"),Status&&Status->GetText().ToString().Contains(TEXT("Layout changed")));
        auto* Exterior=NewObject<UEditScreenWidget>(); Exterior->Initialize(); Exterior->TakeWidget(); Exterior->SetOwningPlayer(PC);
        TestNotNull(TEXT("interior File menu exposes Save As"), Screen->FindScreenWidget(TEXT("Btn_FileSaveAs")));
        TestNotNull(TEXT("exterior File menu exposes Save As"), Exterior->FindScreenWidget(TEXT("Btn_FileSaveAs")));
        TestNotNull(TEXT("exterior validation panel"),Exterior->GetValidationPanelForTests());
        TestTrue(TEXT("exterior registered"),Exterior->GetDockHost()->IsPanelRegistered(ExteriorDock::Validation));
        FDockLayout Old; Old.DockPanelToEdge(InteriorDock::Toolbar, EDockSide::Left);
        TArray<FName> Missing; FDockLayout Restored; TestTrue(TEXT("old layout still loads"),Restored.FromJson(Old.ToJson({1920,1080}), {1920,1080},InteriorDock::KnownPanels(),Missing));
        InteriorDock::PlaceMissingPanels(Restored,Missing); TestFalse(TEXT("new panel does not open itself in old layouts"),Restored.ContainsPanel(InteriorDock::Validation));
        TestTrue(TEXT("new panel can be shown"),Restored.ShowPanel(InteriorDock::Validation,InteriorDock::DefaultSideFor(InteriorDock::Validation)));
        return true;
    }

    // Validate in slices finds exactly what Validate all at once finds, can be cancelled, and survives the project closing under it.
    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlicedRun, "InNavi.LayoutValidation.SlicedRun", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FSlicedRun::RunTest(const FString& Parameters)
    {
        auto Root=Project(); Room(Root,1)->SetArrayField(TEXT("openings"), {});
        FFixture Fixture(Root); auto* Session=Fixture.Controller->GetLayoutValidationSession();
        auto Summary=[](const FReport& Report)
        {
            TArray<FString> Lines;
            for (const auto& I : Report.Issues) Lines.Add(FString::Printf(TEXT("%s|%s|%d|%s"),*I.Rule,*I.BuildingId,I.Floor,*I.ObjectId));
            Lines.Sort(); return Lines;
        };
        Session->Validate();
        const TArray<FString> AllAtOnce=Summary(Session->GetReport()); const int32 Checked=Session->GetReport().RoomsChecked;
        TestTrue(TEXT("the sealed room is found"),AllAtOnce.Contains(TEXT("ROOM_UNREACHABLE|A|2|R2")));

        Session->StartValidate();
        TestTrue(TEXT("running"),Session->IsRunning()); TestFalse(TEXT("no report until it is done"),Session->HasReport());
        int32 Steps=0; float Last=0.f;
        while (!Session->StepValidate(0.0) && Steps<100000)
        {
            ++Steps; const float Now=Session->GetProgress();
            TestTrue(TEXT("progress only goes forward, up to 1"),Now>=Last&&Now<=1.f); Last=Now;
            TestFalse(TEXT("says what it is doing"),Session->GetProgressText().IsEmpty());
        }
        TestTrue(TEXT("it took more than one slice"),Steps>1);
        TestFalse(TEXT("done"),Session->IsRunning()); TestTrue(TEXT("report"),Session->HasReport());
        TestEqual(TEXT("the same findings as all at once"),Summary(Session->GetReport()),AllAtOnce);
        TestEqual(TEXT("the same rooms checked"),Session->GetReport().RoomsChecked,Checked);

        Session->StartValidate(); Session->StepValidate(0.0); Session->CancelValidate();
        TestFalse(TEXT("cancelled run is over"),Session->IsRunning()); TestFalse(TEXT("and leaves no report"),Session->HasReport());

        Session->StartValidate(); Session->StepValidate(0.0);
        Fixture.Interior->ClearLoadedInterior(); Fixture.Generator->ClearAllBuildings();
        while (!Session->StepValidate(0.0)) {}
        TestTrue(TEXT("a project closed during the run is reported, not read"),Has(Session->GetReport(),TEXT("NAVIGATION_SKIPPED")));
        return true;
    }

    // Opening a building from the list fills in what its file left out, which must not make the route checks run all over again.
    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FViewKeepsRouteResults, "InNavi.LayoutValidation.ViewKeepsRouteResults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FViewKeepsRouteResults::RunTest(const FString& Parameters)
    {
        auto Root=Project(); Room(Root,1)->SetArrayField(TEXT("openings"), {});
        FFixture Fixture(Root); auto* PC=Fixture.Controller; auto* Session=PC->GetLayoutValidationSession();
        Fixture.Generator->SpawnBuildingFromData(TEXT("B"),TEXT("Building B"),{ {2000,0}, {3000,0}, {3000,1000}, {2000,1000} },2,300,FVector2D::ZeroVector);
        Session->Validate();
        const uint32 Runs=Session->GetRouteCheckRuns();
        const int32 Unauthored=Session->GetReport().Issues.IndexOfByPredicate([](const FIssue& I){return I.Rule==TEXT("INTERIOR_UNAUTHORED")&&I.BuildingId==TEXT("B");});
        const int32 Sealed=Session->GetReport().Issues.IndexOfByPredicate([](const FIssue& I){return I.Rule==TEXT("ROOM_UNREACHABLE");});
        if (!TestTrue(TEXT("the unauthored building and the sealed room are reported"),Unauthored!=INDEX_NONE&&Sealed!=INDEX_NONE)) return false;
        const FString Before=Fingerprint(ULayoutValidationSession::Capture(*Fixture.Generator,*Fixture.Interior));
        TestTrue(TEXT("showing the unauthored building"),Session->ShowIssue(*PC,Unauthored));
        TestEqual(TEXT("it is open"),PC->GetLoadedBuildingId(),FString(TEXT("B")));
        TestNotEqual(TEXT("opening it changed how the layout is written"),Fingerprint(ULayoutValidationSession::Capture(*Fixture.Generator,*Fixture.Interior)),Before);
        TestEqual(TEXT("without running the route checks again"),Session->GetRouteCheckRuns(),Runs);
        TestFalse(TEXT("the report is still current"),Session->IsStale());
        TestTrue(TEXT("the sealed room is still reported"),Has(Session->GetReport(),TEXT("ROOM_UNREACHABLE")));
        const int32 Again=Session->GetReport().Issues.IndexOfByPredicate([](const FIssue& I){return I.Rule==TEXT("ROOM_UNREACHABLE");});
        TestTrue(TEXT("a route issue can still be shown"),Session->ShowIssue(*PC,Again));
        TestEqual(TEXT("in its building"),PC->GetLoadedBuildingId(),FString(TEXT("A")));
        TestEqual(TEXT("still no second route check"),Session->GetRouteCheckRuns(),Runs);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFileAndExport,"InNavi.LayoutValidation.FileAndExport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FFileAndExport::RunTest(const FString& Parameters)
    {
        const FString Dir=FPaths::ProjectSavedDir()/TEXT("AutomationTemp"); IFileManager::Get().MakeDirectory(*Dir,true);
        const FString Input=Dir/(FGuid::NewGuid().ToString()+TEXT(".json")), Output=Input+TEXT(".report.json");
        auto Root=Project(); auto Values=Root->GetArrayField(TEXT("buildings")); const auto Duplicate=Values[0]; Values.Add(Duplicate); Root->SetArrayField(TEXT("buildings"),Values);
        const FString Before=Json(Root); FFileHelper::SaveStringToFile(Before,*Input);
        const auto Report=ValidateFile(Input); TestTrue(TEXT("raw duplicate checked before loader drops it"),Has(Report,TEXT("ID_DUPLICATE")));
        TestTrue(TEXT("JSON report exported"),ExportReport(Report,Output));
        FString After; FFileHelper::LoadFileToString(After,*Input); TestEqual(TEXT("input file stays unchanged"),After,Before);
        FString Text; FFileHelper::LoadFileToString(Text,*Output); TSharedPtr<FJsonObject> Exported;
        TestTrue(TEXT("report parses"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Exported));
        TestTrue(TEXT("reports distinguish data from navigation checks"),Exported&&!Exported->GetBoolField(TEXT("navigationChecked")));
        FFileHelper::SaveStringToFile(TEXT("{bad JSON"),*Input); TestTrue(TEXT("malformed file gets an issue"),Has(ValidateFile(Input),TEXT("FILE_READ")));
        IFileManager::Get().Delete(*Input); IFileManager::Get().Delete(*Output); return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIssueFocusAndSave, "InNavi.LayoutValidation.IssueFocusAndSaving", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FIssueFocusAndSave::RunTest(const FString& Parameters)
    {
        auto Root=Project(); Room(Root,1)->SetArrayField(TEXT("openings"), {}); FFixture Fixture(Root);
        auto* PC=Fixture.Controller; auto* Session=PC->GetLayoutValidationSession(); Session->Validate();
        const int32 Index=Session->GetReport().Issues.IndexOfByPredicate([](const FIssue& I){return I.Rule==TEXT("ROOM_UNREACHABLE");});
        if (!TestTrue(TEXT("sealed room finding exists"),Index!=INDEX_NONE)) return false;
        const FString Before=Fingerprint(ULayoutValidationSession::Capture(*Fixture.Generator,*Fixture.Interior));
        TestTrue(TEXT("show issue succeeds"),Session->ShowIssue(*PC,Index));
        TestEqual(TEXT("shows the right floor"),PC->GetActiveFloor(),2);
        TestEqual(TEXT("selects the room"),PC->GetSelectedRoomId(),FString(TEXT("R2")));
        TestEqual(TEXT("view action preserves layout"),Fingerprint(ULayoutValidationSession::Capture(*Fixture.Generator,*Fixture.Interior)),Before);
        const FString Path=FPaths::ProjectSavedDir()/TEXT("AutomationTemp")/(FGuid::NewGuid().ToString()+TEXT(".json"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true); FFileHelper::SaveStringToFile(Json(Root),*Path);
        PC->LoadBuildingDataFromPath(Path); PC->LoadInteriorForBuilding(TEXT("A")); Session=PC->GetLayoutValidationSession(); Session->Validate();
        TestTrue(TEXT("report contains errors before saving"),Session->GetReport().HasErrors());
        TestTrue(TEXT("unresolved validation errors do not block normal Save"),PC->SaveCurrentProject());
        TestTrue(TEXT("validation never fixes the sealed room"),Fixture.Interior->GetRoomById(TEXT("R2"))->GetOpenings().IsEmpty());
        IFileManager::Get().Delete(*Path); return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirroredLinks, "InNavi.LayoutValidation.MirroredLinksAndElevation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FMirroredLinks::RunTest(const FString& Parameters)
    {
        auto Root=Project(); auto Link=MakeShared<FJsonObject>();
        Link->SetStringField(TEXT("id"),TEXT("AtoB")); Link->SetStringField(TEXT("kind"),TEXT("link")); Link->SetStringField(TEXT("connectsTo"),TEXT("B"));
        Link->SetNumberField(TEXT("level"),2); Link->SetNumberField(TEXT("edge"),1); Link->SetNumberField(TEXT("offset"),500); Link->SetNumberField(TEXT("width"),120);
        auto Doors=Interior(Root)->GetArrayField(TEXT("entrances")); Doors.Add(MakeShared<FJsonValueObject>(Link)); Interior(Root)->SetArrayField(TEXT("entrances"),Doors);
        FFixture Fixture(Root);
        auto* B=Fixture.Generator->SpawnBuildingFromData(TEXT("B"),TEXT("Building B"),{{0,0},{1000,0},{1000,1000},{0,1000}},2,300,{1030,0});
        Fixture.Generator->SetInteriorDataForBuilding(TEXT("B"),Interior(Project()));
        auto* Session=Fixture.Controller->GetLayoutValidationSession(); Session->Validate();
        TestFalse(TEXT("single saved link creates a valid derived mirror"),Has(Session->GetReport(),TEXT("LINK_UNUSABLE")));
        TestEqual(TEXT("validation leaves A open rather than spawning B's interior"),Fixture.Interior->GetLoadedBuildingId(),FString(TEXT("A")));
        TestEqual(TEXT("B stores only its own main entrance"),Fixture.Generator->GetStoredEntrances(TEXT("B")).Num(),1);
        // A different floor height makes the level-2 doorway physically incompatible.
        const auto Outline=B->GetFootprint(); B->BuildFromFootprint(Outline,Fixture.Generator->GetScale(),B->BuildingOffset,2,400);
        Session->Validate(); TestTrue(TEXT("matching floor numbers cannot bridge different elevations"),Has(Session->GetReport(),TEXT("LINK_UNUSABLE")));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConcaveProbe, "InNavi.LayoutValidation.ConcaveRoomProbe", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FConcaveProbe::RunTest(const FString& Parameters)
    {
        auto Root=Project(); TArray<TSharedPtr<FJsonValue>> Points;
        for (FVector2D P : TArray<FVector2D>{{100,100},{500,100},{500,200},{200,200},{200,500},{100,500}})
            Points.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y)}));
        Room(Root)->SetArrayField(TEXT("footprint"),Points); FFixture Fixture(Root);
        auto* Session=Fixture.Controller->GetLayoutValidationSession(); Session->Validate();
        TestTrue(TEXT("concave room can be checked"),Session->GetReport().bNavigationChecked);
        TestFalse(TEXT("probe is not a vertex average in the outside notch"),Has(Session->GetReport(),TEXT("ROOM_PROBE_UNAVAILABLE")));
        TestFalse(TEXT("concave room with an opening is reachable"),Has(Session->GetReport(),TEXT("ROOM_UNREACHABLE")));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FViewSwitchFreshness, "InNavi.LayoutValidation.ViewSwitchFreshness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FViewSwitchFreshness::RunTest(const FString& Parameters)
    {
        FFixture Fixture;
        Fixture.Interior->SetRoomName(TEXT("R1"),TEXT("Unsaved room name"));
        auto Root=Project(); Room(Root,1)->SetArrayField(TEXT("openings"),{});
        Fixture.Generator->SpawnBuildingFromData(TEXT("B"),TEXT("Building B"),{{0,0},{1000,0},{1000,1000},{0,1000}},2,300,{2000,0});
        Fixture.Generator->SetInteriorDataForBuilding(TEXT("B"),Interior(Root));
        auto* Session=Fixture.Controller->GetLayoutValidationSession(); Session->Validate();
        const int32 Index=Session->GetReport().Issues.IndexOfByPredicate([](const FIssue& I){return I.Rule==TEXT("ROOM_UNREACHABLE")&&I.BuildingId==TEXT("B");});
        if (!TestTrue(TEXT("unopened B's sealed room is diagnosed"),Index!=INDEX_NONE)) return false;
        TestTrue(TEXT("issue opens B"),Session->ShowIssue(*Fixture.Controller,Index));
        TestEqual(TEXT("B is now open"),Fixture.Interior->GetLoadedBuildingId(),FString(TEXT("B")));
        Session->RefreshStaleness(); TestFalse(TEXT("materialised defaults do not leave a falsely stale report"),Session->IsStale());
        TestNotNull(TEXT("the focused navigation finding remains highlighted"),Session->GetFocusedIssue());
        const auto StoredA=Fixture.Generator->GetInteriorDataForBuilding(TEXT("A"));
        const auto FirstRoom=StoredA->GetArrayField(TEXT("floors"))[0]->AsObject()->GetArrayField(TEXT("rooms"))[0]->AsObject();
        TestEqual(TEXT("unsaved edits in A are preserved by normal switching"),FirstRoom->GetStringField(TEXT("name")),FString(TEXT("Unsaved room name")));
        return true;
    }
}
#endif
