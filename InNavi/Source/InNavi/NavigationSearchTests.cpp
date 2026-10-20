#if WITH_DEV_AUTOMATION_TESTS

#include "NavigationSearchUtils.h"
#include "MapBuildingRowWidget.h"
#include "MapPanelWidget.h"
#include "UiTestFixture.h"
#include "NavigationPanelWidget.h"
#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "PolyBuilding.h"
#include "NavPlayerController.h"
#include "InteriorScreenWidget.h"
#include "GameFramework/DefaultPawn.h"
#include "Components/ScrollBox.h"
#include "LayerRowWidget.h"
#include "EditScreenWidget.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/CheckBox.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationSearchTextTest, "InNavi.Navigation.Search.TextMatching",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationSearchTextTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Normalized = NavigationSearch::ParseTags(TEXT(" Lab, lab, , Teaching Room ,LAB, research "));
	TestEqual(TEXT("Blank and duplicate tags removed"), Normalized.Num(), 3);
	TestEqual(TEXT("Multi-word tags retained"), FString::Join(Normalized, TEXT(", ")), FString(TEXT("Lab, Teaching Room, research")));
	TestTrue(TEXT("Empty query matches"), NavigationSearch::Matches(TEXT(" \t "), TEXT("Room")));
	TestTrue(TEXT("Query ignores case and matches all words"), NavigationSearch::Matches(TEXT("LAB  floor\t2"), TEXT("Laboratory E1 Floor 2")));
	TestFalse(TEXT("Every word must match"), NavigationSearch::Matches(TEXT("lab library"), TEXT("Laboratory")));
	TestTrue(TEXT("Room ID is searchable"), NavigationSearch::Matches(TEXT("e1-02"), TEXT("Teaching [E1-02-01]")));
	TestTrue(TEXT("Empty tags clear metadata"), NavigationSearch::ParseTags(TEXT(" , , ")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationRoomTagsTest, "InNavi.Navigation.Search.RoomTagsPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationRoomTagsTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	const auto Options = Fixture.Nav->GetSelectableRooms();
	if (!TestFalse(TEXT("Fixture has rooms"), Options.IsEmpty())) return false;
	const FString Id = Options[0].RoomId;
	TestTrue(TEXT("Legacy dataset without tags loads"), Fixture.Interior->GetRoomTags(Id).IsEmpty());
	TestTrue(TEXT("Tags can be assigned"), Fixture.Interior->SetRoomTags(Id, { TEXT(" Research "), TEXT("research"), TEXT("Teaching room") }));
	TestEqual(TEXT("Tags normalized on assignment"), Fixture.Interior->GetRoomTags(Id).Num(), 2);
	TestTrue(TEXT("Tags find the destination"), Fixture.Nav->SearchDestinations(TEXT("RESEARCH teaching")).ContainsByPredicate(
		[&Id](const auto& Option) { return Option.RoomId == Id; }));
	TestTrue(TEXT("ID finds the destination"), Fixture.Nav->SearchDestinations(Id).ContainsByPredicate(
		[&Id](const auto& Option) { return Option.RoomId == Id; }));
	TestTrue(TEXT("Unknown destination returns no matches"), Fixture.Nav->SearchDestinations(TEXT("no-such-destination-731985")).IsEmpty());
	TestTrue(TEXT("Unknown room cannot be tagged"), !Fixture.Interior->SetRoomTags(TEXT("missing-room"), { TEXT("lab") }));
	TestTrue(TEXT("Tag edit can be undone"), Fixture.Interior->Undo());
	TestTrue(TEXT("Undo restores original tags"), Fixture.Interior->GetRoomTags(Id).IsEmpty());
	TestTrue(TEXT("Tag edit can be redone"), Fixture.Interior->Redo());
	TestEqual(TEXT("Redo restores tags"), Fixture.Interior->GetRoomTags(Id).Num(), 2);
	Fixture.Interior->SyncInteriorToBuildingGenerator();
	const FString SavePath = FPaths::ProjectSavedDir() / TEXT("RoomTags-") + FGuid::NewGuid().ToString() + TEXT(".json");
	TestTrue(TEXT("Tagged project saves"), Fixture.Generator->SaveBuildingsToFile(SavePath));
	Fixture.Interior->ClearLoadedInterior();
	Fixture.Generator->ClearAllBuildings();
	TestTrue(TEXT("Saved project reloads"), Fixture.Generator->LoadBuildingsFromFile(SavePath) > 0);
	Fixture.Interior->LoadInteriorForBuilding(TEXT("E2"));
	TestEqual(TEXT("Saved tags survive reload"), FString::Join(Fixture.Interior->GetRoomTags(Id), TEXT(", ")),
		FString(TEXT("Research, Teaching room")));
	TestTrue(TEXT("Reloaded tags remain searchable"), Fixture.Nav->SearchDestinations(TEXT("teaching room")).ContainsByPredicate(
		[&Id](const auto& Option) { return Option.RoomId == Id; }));
	IFileManager::Get().Delete(*SavePath);
	TestTrue(TEXT("Tags can be removed"), Fixture.Interior->SetRoomTags(Id, {}));
	TestTrue(TEXT("Cleared tags are empty"), Fixture.Interior->GetRoomTags(Id).IsEmpty());
	const TArray<FVector2D> CopyFootprint = Fixture.Interior->GetRoomById(Id)->GetFootprint();
	Fixture.Interior->SetActiveFloor(Options[0].Floor);
	FString CreationError;
	AInteriorRoom* Copy = Fixture.Interior->CreateRoomFromFootprintWithTags(CopyFootprint, TEXT("Tags-Copy-Test"),
		TEXT("Tags copy"), true, false, true, CreationError, { TEXT("copied-tag") });
	if (TestNotNull(TEXT("Tagged room can be created in one operation"), Copy))
	{
		TestTrue(TEXT("Copied metadata participates in its creation snapshot"), Copy->RoomTags.Contains(TEXT("copied-tag")));
		TestTrue(TEXT("Undo tagged creation succeeds"), Fixture.Interior->Undo());
		TestNull(TEXT("One undo removes the newly created room"), Fixture.Interior->GetRoomById(TEXT("Tags-Copy-Test")));
		TestTrue(TEXT("Redo tagged creation succeeds"), Fixture.Interior->Redo());
		TestTrue(TEXT("Redo restores copied tags"), Fixture.Interior->GetRoomTags(TEXT("Tags-Copy-Test")).Contains(TEXT("copied-tag")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationBuildingFilterTest, "InNavi.Navigation.Search.BuildingFilters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationBuildingFilterTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	const TArray<FVector2D> Shape = { {0, 0}, {20, 0}, {20, 20}, {0, 20} };
	APolyBuilding* Low = Fixture.Generator->SpawnBuildingFromData(TEXT("Search-Low"), TEXT("Research Annex"), Shape, 1, 300, FVector2D(1000, 0));
	APolyBuilding* High = Fixture.Generator->SpawnBuildingFromData(TEXT("Search-High"), TEXT("Research Tower"), Shape, 8, 300, FVector2D(1100, 0));
	if (!TestNotNull(TEXT("Low fixture exists"), Low) || !TestNotNull(TEXT("High fixture exists"), High)) return false;
	Fixture.Generator->SetBuildingFilter(TEXT("RESEARCH"), 6, 0);
	TestEqual(TEXT("Text and floor criteria combine"), Fixture.Generator->GetFilteredBuildings().Num(), 1);
	TestFalse(TEXT("Matching building is visible"), High->IsHidden());
	TestTrue(TEXT("Nonmatching building is hidden and cannot intercept clicks"), Low->IsHidden() && !Low->GetActorEnableCollision());
	Fixture.Generator->SetCampusBuildingsVisible(false);
	TestTrue(TEXT("Campus visibility composes with search"), High->IsHidden());
	Fixture.Generator->SetCampusBuildingsVisible(true);
	TestTrue(TEXT("Restoring campus preserves search criteria"), !High->IsHidden() && Low->IsHidden());
	Fixture.Generator->SetBuildingFilter(TEXT("search-low"), 0, 0);
	TestTrue(TEXT("Stable building IDs match case-insensitively"), !Low->IsHidden() && High->IsHidden());
	Fixture.Generator->SetBuildingFilter(TEXT(""), 2, 5);
	TestTrue(TEXT("Floor range excludes both low and high buildings"), Low->IsHidden() && High->IsHidden());
	Fixture.Generator->SetBuildingFilter(TEXT(""), 0, 0);
	TestTrue(TEXT("Reset restores visibility and collision"), !Low->IsHidden() && !High->IsHidden()
		&& Low->GetActorEnableCollision() && High->GetActorEnableCollision());
	Fixture.Generator->SetBuildingFilter(TEXT("new-library"), 0, 0);
	APolyBuilding* New = Fixture.Generator->SpawnBuildingFromData(TEXT("New-Library"), TEXT("Library"), Shape, 2, 300, FVector2D(1200, 0));
	TestTrue(TEXT("Newly created buildings respect active filters"), New && !New->IsHidden() && Low->IsHidden());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationSearchControlsTest, "InNavi.Navigation.Search.Controls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationSearchControlsTest::RunTest(const FString& Parameters)
{
	// Programmatic SetText does not simulate typing in a collapsed/headless Slate field.
	auto TypeText = [](UEditableTextBox* Box, const FString& Text)
	{
		Box->SetText(FText::FromString(Text));
		Box->OnTextChanged.Broadcast(Box->GetText());
	};
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>();
	Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(Controller);
	FScriptDelegate Content;
	Content.BindUFunction(Screen, TEXT("HandleInteriorContentChanged"));
	Controller->OnInteriorContentChanged.AddUnique(Content);
	FScriptDelegate Selection;
	Selection.BindUFunction(Screen, TEXT("HandleSelectedRoomChanged"));
	Controller->OnSelectedRoomChanged.AddUnique(Selection);
	UEditableTextBox* TagsBox = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Txt_RoomTags")));
	UButton* ApplyTags = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_ApplyRoom"))); // Save changes: the name and the tags
	UEditableTextBox* Search = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Txt_DestinationSearch")));
	UComboBoxString* Destinations = Cast<UComboBoxString>(Screen->FindScreenWidget(TEXT("Cbx_NavDestinationRoom")));
	UComboBoxString* Starts = Cast<UComboBoxString>(Screen->FindScreenWidget(TEXT("Cbx_NavStartRoom")));
	if (!TestNotNull(TEXT("Tags field exists"), TagsBox) || !TestNotNull(TEXT("Apply tags exists"), ApplyTags)
		|| !TestNotNull(TEXT("Destination search exists"), Search) || !TestNotNull(TEXT("Destination results exist"), Destinations)
		|| !TestNotNull(TEXT("Start dropdown exists"), Starts)) return false;
	auto Options = Fixture.Nav->GetSelectableRooms();
	APolyBuilding* Building = Fixture.Generator->FindBuildingById(TEXT("E2"));
	Options.RemoveAll([&](const auto& Option)
	{
		FString Error;
		return !Building || !FNavigationRoutePlanner::IsValidPoint(*Building, *Fixture.Interior, Fixture.Generator->GetScale(), Option.Floor, Option.Centroid, Error);
	});
	if (!TestFalse(TEXT("A reachable destination exists"), Options.IsEmpty())) return false;
	const auto Room = Options[0];
	Controller->SelectRoom(Room.RoomId);
	TagsBox->SetText(FText::FromString(TEXT("presentation-tag, Presentation-TAG, teaching")));
	ApplyTags->OnClicked.Broadcast();
	TestEqual(TEXT("Actual Apply Tags updates room metadata"), Fixture.Interior->GetRoomTags(Room.RoomId).Num(), 2);
	TestTrue(TEXT("Tags mark the project dirty"), Controller->HasUnsavedChanges());
	Starts->SetSelectedOption(Room.GetDisplayLabel());
	Starts->OnSelectionChanged.Broadcast(Room.GetDisplayLabel(), ESelectInfo::OnMouseClick);
	const int32 StartOptionCount = Starts->GetOptionCount();
	TypeText(Search, TEXT("PRESENTATION-TAG"));
	TestEqual(TEXT("Destination typing preserves Start options"), Starts->GetOptionCount(), StartOptionCount);
	TestEqual(TEXT("Destination typing preserves Start selection"), Starts->GetSelectedOption(), Room.GetDisplayLabel());
	TestTrue(TEXT("Destination typing preserves actual Start endpoint"), Fixture.Nav->HasStart());
	TestEqual(TEXT("Typing filters the actual destination dropdown"), Destinations->GetOptionCount(), 1);
	Destinations->OnSelectionChanged.Broadcast(Room.GetDisplayLabel(), ESelectInfo::OnMouseClick);
	TestTrue(TEXT("Picking a result sets a real destination"), Fixture.Nav->HasDestination() && Fixture.Nav->WasDestinationSetViaDropdown());
	TypeText(Search, TEXT("no-match-736"));
	TestTrue(TEXT("No-results state disables dropdown without losing destination"), !Destinations->GetIsEnabled() && Fixture.Nav->HasDestination());
	TypeText(Search, TEXT(""));
	TestEqual(TEXT("Clearing search restores rooms"), Destinations->GetOptionCount(), Fixture.Nav->GetSelectableRooms().Num());
	Controller->OnSelectedRoomChanged.Remove(Selection);
	Controller->OnInteriorContentChanged.Remove(Content);

	UEditScreenWidget* Editor = NewObject<UEditScreenWidget>();
	Editor->Initialize(); Editor->TakeWidget(); Editor->SetOwningPlayer(Controller);
	UEditableTextBox* BuildingSearch = Cast<UEditableTextBox>(Editor->FindScreenWidget(TEXT("Txt_BuildingSearch")));
	UMapPanelWidget* MapPanel = Editor->GetMapPanelForTests();
	UButton* Reset = Cast<UButton>(Editor->FindScreenWidget(TEXT("Btn_ResetBuildingFilters")));
	if (!TestNotNull(TEXT("Building search exists"), BuildingSearch) || !TestNotNull(TEXT("Map panel exists"), MapPanel)
		|| !TestNotNull(TEXT("Reset filters exists"), Reset)) return false;
	TypeText(BuildingSearch, TEXT("E1"));
	TestTrue(TEXT("Building search populates the list"), MapPanel->GetBuildingRowCountForTests() > 0);
	for (UMapBuildingRowWidget* Row : MapPanel->GetBuildingRowsForTests())
	{
		if (Row->GetBuildingId() == TEXT("E1")) { Row->ClickForTests(); break; }
	}
	TestEqual(TEXT("Clicking a row selects its stable building ID"), Controller->GetSelectedBuildingId(), FString(TEXT("E1")));
	TypeText(BuildingSearch, TEXT("missing-building-756"));
	TestTrue(TEXT("No results empties the list and clears hidden selection"), MapPanel->GetBuildingRowCountForTests() == 0 && Controller->GetSelectedBuildingId().IsEmpty());
	Reset->OnClicked.Broadcast();
	TestEqual(TEXT("Reset restores all editable buildings"), MapPanel->GetBuildingRowCountForTests(), Controller->GetEditableBuildingCount());
	TypeText(BuildingSearch, TEXT("E2"));
	Controller->RestoreExteriorCameraView();
	TestTrue(TEXT("Returning to exterior retains filters and excluded-building visibility"),
		Controller->GetBuildingFilterText() == TEXT("E2") && Fixture.Generator->FindBuildingById(TEXT("E1"))->IsHidden());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationBuildingRefreshTest, "InNavi.Navigation.Search.BuildingRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationBuildingRefreshTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	Controller->SetBuildingFilter(TEXT("index refresh"), 6, 0);
	TestTrue(TEXT("Initial cached results are empty"), Controller->GetFilteredBuildingOptions().IsEmpty());
	const TArray<FVector2D> Shape = { {0, 0}, {100, 0}, {100, 100}, {0, 100} };
	APolyBuilding* Building = Fixture.Generator->SpawnBuildingFromData(TEXT("Index-Test"), TEXT("Index Refresh"), Shape, 8, 300, FVector2D(2000, 2000));
	if (!TestNotNull(TEXT("Refresh fixture exists"), Building)) return false;
	TestEqual(TEXT("Creation updates cached results"), Controller->GetFilteredBuildingOptions().Num(), 1);
	const uint64 Revision = Controller->GetBuildingResultsRevision();
	Controller->SetBuildingFilter(TEXT(" index refresh "), 6, 0);
	Controller->GetFilteredBuildingOptions();
	Controller->GetFilteredBuildingOptions();
	TestEqual(TEXT("Unchanged filters and reads do not request another refresh"), Controller->GetBuildingResultsRevision(), Revision);
	Building->SetFloorCount(2);
	TestTrue(TEXT("Floor edits remove nonmatching results"), Controller->GetFilteredBuildingOptions().IsEmpty());
	TestFalse(TEXT("Metadata edits preserve preview visibility until exterior filters are reapplied"), Building->IsHidden());
	Fixture.Generator->ApplyBuildingFilter();
	TestTrue(TEXT("Returning to exterior excludes the changed building and collision"), Building->IsHidden() && !Building->GetActorEnableCollision());
	Building->SetFloorCount(7);
	Fixture.Generator->ApplyBuildingFilter();
	TestTrue(TEXT("Floor edits restore matching results and update labels"),
		Controller->GetFilteredBuildingOptions().Contains(TEXT("Index Refresh [Index-Test] - 7 floors")) && !Building->IsHidden());
	TestTrue(TEXT("Rename succeeds"), Fixture.Generator->SetBuildingName(TEXT("Index-Test"), TEXT("Library")));
	TestTrue(TEXT("Rename invalidates cached text filtering"), Controller->GetFilteredBuildingOptions().IsEmpty());
	Controller->SetBuildingFilter(TEXT("library"), 0, 0);
	TestEqual(TEXT("Updated name can be found"), Controller->GetFilteredBuildingOptions().Num(), 1);
	Controller->SetCampusBuildingsVisible(false);
	TestTrue(TEXT("Campus visibility invalidates results"), Controller->GetFilteredBuildingOptions().IsEmpty());
	Controller->SetCampusBuildingsVisible(true);
	TestEqual(TEXT("Campus visibility restores filtered results"), Controller->GetFilteredBuildingOptions().Num(), 1);
	const int32 CountBeforeDestroy = Controller->GetEditableBuildingCount();
	{
		// This transient test world has not initialized actors for play. Allow its native
		// UFUNCTION destruction delegate to execute through AActor::ProcessEvent.
		FEditorScriptExecutionGuard ScriptGuard;
		TestTrue(TEXT("Building destruction succeeds"), Building->Destroy());
	}
	TestTrue(TEXT("Destruction removes cached results"), Controller->GetFilteredBuildingOptions().IsEmpty());
	TestEqual(TEXT("Destruction updates total count"), Controller->GetEditableBuildingCount(), CountBeforeDestroy - 1);
	Fixture.Interior->ClearLoadedInterior();
	Fixture.Generator->ClearAllBuildings();
	TestTrue(TEXT("Bulk clear empties results"), Controller->GetFilteredBuildingOptions().IsEmpty());
	TestEqual(TEXT("Bulk clear empties count"), Controller->GetEditableBuildingCount(), 0);
	Controller->SetBuildingFilter(TEXT("E2"), 0, 0);
	Fixture.Generator->LoadBuildingsFromFile(FPaths::ProjectDir() / TEXT("Data/buildings.json"));
	TestTrue(TEXT("Project reload populates fresh results"), Controller->GetFilteredBuildingOptions().FindKey(TEXT("E2")) != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationRoomSearchIndexTest, "InNavi.Navigation.Search.RoomIndexInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationRoomSearchIndexTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	const TArray<FVector2D> BuildingShape = { {0, 0}, {100, 0}, {100, 100}, {0, 100} };
	Fixture.Generator->SpawnBuildingFromData(TEXT("Room-Index"), TEXT("Room Index"), BuildingShape, 2, 300, FVector2D(2000, 2000));
	Fixture.Interior->LoadInteriorForBuilding(TEXT("Room-Index"));
	TestTrue(TEXT("Switching buildings removes old rooms from index"), Fixture.Nav->GetSelectableRooms().IsEmpty());
	FString Error;
	const TArray<FVector2D> Triangle = { {10, 10}, {30, 10}, {10, 30} };
	const TArray<FVector2D> Square = { {60, 60}, {80, 60}, {80, 80}, {60, 80} };
	if (!TestNotNull(TEXT("Room creation succeeds"), Fixture.Interior->CreateRoomFromFootprintWithTags(
		Triangle, TEXT("Index-A"), TEXT("Zulu"), true, false, true, Error, { TEXT("teaching lab") }))) return false;
	TestEqual(TEXT("Creation invalidates an empty index"), Fixture.Nav->SearchDestinations(TEXT("TEACHING lab")).Num(), 1);
	Fixture.Interior->CreateRoomFromFootprintWithTags(Square, TEXT("Index-B"), TEXT("Alpha"), true, false, true, Error, {});
	auto Options = Fixture.Nav->SearchDestinations(TEXT(""));
	if (!TestEqual(TEXT("Both rooms indexed"), Options.Num(), 2)) return false;
	TestEqual(TEXT("Destinations retain alphabetical ordering"), Options[0].RoomId, FString(TEXT("Index-B")));
	TestEqual(TEXT("Start retains existing room order"), Fixture.Nav->GetSelectableRooms()[0].RoomId, FString(TEXT("Index-A")));
	const uint64 Revision = Fixture.Interior->GetRoomSearchRevision();
	Fixture.Nav->SearchDestinations(TEXT("Alpha"));
	Fixture.Nav->SearchDestinations(TEXT("missing"));
	TestEqual(TEXT("Searching does not dirty room data"), Fixture.Interior->GetRoomSearchRevision(), Revision);
	Fixture.Interior->SetRoomName(TEXT("Index-A"), TEXT("Beta"));
	TestTrue(TEXT("Old name disappears from cached search text"), Fixture.Nav->SearchDestinations(TEXT("Zulu")).IsEmpty());
	TestEqual(TEXT("New name appears immediately"), Fixture.Nav->SearchDestinations(TEXT("Beta")).Num(), 1);
	Fixture.Interior->SetRoomTags(TEXT("Index-A"), { TEXT("meeting") });
	TestTrue(TEXT("Old tags disappear"), Fixture.Nav->SearchDestinations(TEXT("teaching")).IsEmpty());
	TestEqual(TEXT("New tags appear"), Fixture.Nav->SearchDestinations(TEXT("meeting")).Num(), 1);
	TestTrue(TEXT("Undo tag edit succeeds"), Fixture.Interior->Undo());
	TestEqual(TEXT("Undo rebuilds tag search text"), Fixture.Nav->SearchDestinations(TEXT("teaching")).Num(), 1);
	TestTrue(TEXT("Redo tag edit succeeds"), Fixture.Interior->Redo());
	TestEqual(TEXT("Redo rebuilds tag search text"), Fixture.Nav->SearchDestinations(TEXT("meeting")).Num(), 1);
	const FVector2D BeforeMove = Fixture.Nav->SearchDestinations(TEXT("Index-A"))[0].Centroid;
	TArray<FVector2D> Moved = Triangle;
	for (FVector2D& Point : Moved) Point.X += 5;
	TestTrue(TEXT("Live room move succeeds"), Fixture.Interior->MoveRoom(TEXT("Index-A"), Moved));
	TestTrue(TEXT("Live move invalidates cached centroid before commit"), Fixture.Nav->SearchDestinations(TEXT("Index-A"))[0].Centroid.Equals(BeforeMove + FVector2D(5, 0), 0.001));
	TestTrue(TEXT("Move commits"), Fixture.Interior->CommitRoomMove(TEXT("Index-A"), Triangle));
	const FVector2D BeforeRotation = Fixture.Nav->SearchDestinations(TEXT("Index-A"))[0].Centroid;
	TestTrue(TEXT("Live rotation succeeds"), Fixture.Interior->RotateRoom(TEXT("Index-A"), 90));
	TestFalse(TEXT("Rotation invalidates asymmetric room centroid before commit"), Fixture.Nav->SearchDestinations(TEXT("Index-A"))[0].Centroid.Equals(BeforeRotation, 0.001));
	Fixture.Interior->CommitRoomRotation(TEXT("Index-A"), Moved);
	const FVector2D BeforeResize = Fixture.Nav->SearchDestinations(TEXT("Index-A"))[0].Centroid;
	TestTrue(TEXT("Room resize succeeds"), Fixture.Interior->ResizeRoom(TEXT("Index-A"), 40, 20));
	const FVector2D ResizedCentroid = Fixture.Nav->SearchDestinations(TEXT("Index-A"))[0].Centroid;
	TestFalse(TEXT("Resize updates cached centroid"), ResizedCentroid.Equals(BeforeResize, 0.001));
	const TArray<FVector2D> BeforeRejectedMove = Fixture.Interior->GetRoomById(TEXT("Index-A"))->GetFootprint();
	TArray<FVector2D> OverlappingSquare = Square;
	for (FVector2D& Point : OverlappingSquare) Point += FVector2D(5, 5);
	TestTrue(TEXT("Move preview allows overlap"), Fixture.Interior->MoveRoom(TEXT("Index-A"), OverlappingSquare));
	TestTrue(TEXT("Overlapping preview has updated centroid"), Fixture.Nav->SearchDestinations(TEXT("Index-A"))[0].Centroid.Equals(FVector2D(75, 75), 0.001));
	TestFalse(TEXT("Commit rejects overlapping room"), Fixture.Interior->CommitRoomMove(TEXT("Index-A"), BeforeRejectedMove));
	TestTrue(TEXT("Reverted move restores cached centroid"), Fixture.Nav->SearchDestinations(TEXT("Index-A"))[0].Centroid.Equals(ResizedCentroid, 0.001));
	TestTrue(TEXT("Room deletion succeeds"), Fixture.Interior->DeleteRoom(TEXT("Index-A")));
	TestTrue(TEXT("Deleted room disappears from index"), Fixture.Nav->SearchDestinations(TEXT("Index-A")).IsEmpty());
	TestTrue(TEXT("Undo deletion succeeds"), Fixture.Interior->Undo());
	TestEqual(TEXT("Undo deletion restores search entry"), Fixture.Nav->SearchDestinations(TEXT("Index-A")).Num(), 1);
	Fixture.Interior->SetActiveFloor(2);
	if (!TestNotNull(TEXT("Upstairs room creation succeeds"), Fixture.Interior->CreateRoomFromFootprintWithTags(
		Triangle, TEXT("Index-Upstairs"), TEXT("Upstairs"), true, false, true, Error, {}))) return false;
	Options = Fixture.Nav->SearchDestinations(TEXT("Upstairs"));
	if (!TestEqual(TEXT("New floor room is indexed"), Options.Num(), 1)) return false;
	TestEqual(TEXT("New room uses the upstairs floor"), Options[0].Floor, 2);
	TestTrue(TEXT("Floor removal succeeds"), Fixture.Interior->RemoveFloor(1));
	Options = Fixture.Nav->SearchDestinations(TEXT("Upstairs"));
	if (TestEqual(TEXT("Room survives floor renumbering"), Options.Num(), 1))
	{
		TestEqual(TEXT("Room floor and label are refreshed"), Options[0].Floor, 1);
		TestTrue(TEXT("New floor appears in cached label"), Options[0].GetDisplayLabel().Contains(TEXT("Floor 1")));
	}
	Fixture.Interior->ClearLoadedInterior();
	TestTrue(TEXT("Clearing interior clears index"), Fixture.Nav->GetSelectableRooms().IsEmpty());
	Fixture.Interior->LoadInteriorForBuilding(TEXT("E2"));
	TestFalse(TEXT("Loading another building populates index"), Fixture.Nav->GetSelectableRooms().IsEmpty());
	TestTrue(TEXT("No entries leak across buildings"), Fixture.Nav->SearchDestinations(TEXT("Index-Upstairs")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationLayersSearchTest, "InNavi.Navigation.Search.LayersResults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationLayersSearchTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>();
	Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(Controller);
	UEditableTextBox* Search = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Txt_LayersSearch")));
	UScrollBox* List = Cast<UScrollBox>(Screen->FindScreenWidget(TEXT("LayersListBox")));
	if (!TestNotNull(TEXT("Layers search exists"), Search) || !TestNotNull(TEXT("Layers list exists"), List)) return false;

	// A room on a floor other than the active one, tagged so it can be found by tag.
	const auto Options = Fixture.Nav->GetSelectableRooms();
	const auto* Target = Options.FindByPredicate([](const auto& Option) { return Option.Floor != 1; });
	if (!Target) Target = &Options[0];
	const FString RoomId = Target->RoomId;
	const int32 RoomFloor = Target->Floor;
	TestTrue(TEXT("tag the room"), Fixture.Interior->SetRoomTags(RoomId, { TEXT("zz-unique-tag") }));
	Controller->SetActiveFloor(1);

	auto CountResultRows = [&]()
	{
		int32 Count = 0;
		for (int32 i = 0; i < List->GetChildrenCount(); ++i)
		{
			if (Cast<ULayerRowWidget>(List->GetChildAt(i))) ++Count;
		}
		return Count;
	};
	// The list is only built on a refresh; do one with an empty search to get the plain-list baseline.
	Search->SetText(FText());
	Search->OnTextChanged.Broadcast(Search->GetText());
	const int32 RowsWithoutSearch = CountResultRows();
	Search->SetText(FText::FromString(TEXT("ZZ-UNIQUE")));
	Search->OnTextChanged.Broadcast(Search->GetText());
	TestTrue(TEXT("a tag search adds a results header and the matching room"), CountResultRows() >= 2);

	// Find the result row and click it the way the row widget does.
	ULayerRowWidget* ResultRow = nullptr;
	for (int32 i = 0; i < List->GetChildrenCount(); ++i)
	{
		if (ULayerRowWidget* Row = Cast<ULayerRowWidget>(List->GetChildAt(i)))
		{
			if (Row->GetRowKeyForTests().StartsWith(TEXT("result:"))) { ResultRow = Row; }
		}
	}
	if (!TestNotNull(TEXT("a result row exists"), ResultRow)) return false;
	TestEqual(TEXT("result key names floor and room"), ResultRow->GetRowKeyForTests(), FString::Printf(TEXT("result:%d:%s"), RoomFloor, *RoomId));
	ResultRow->OnLabelClicked.Broadcast(ResultRow->GetRowKeyForTests());
	TestEqual(TEXT("clicking the result switches to the room's floor"), Controller->GetActiveFloor(), RoomFloor);
	TestEqual(TEXT("and selects the room"), Controller->GetSelectedRoomId(), RoomId);

	// A search that matches nothing lists no rooms.
	Search->SetText(FText::FromString(TEXT("no-such-room-93751")));
	Search->OnTextChanged.Broadcast(Search->GetText());
	bool bAnyResultRow = false;
	for (int32 i = 0; i < List->GetChildrenCount(); ++i)
	{
		if (ULayerRowWidget* Row = Cast<ULayerRowWidget>(List->GetChildAt(i)))
		{
			bAnyResultRow |= Row->GetRowKeyForTests().StartsWith(TEXT("result:"));
		}
	}
	TestFalse(TEXT("no match, no result rows"), bAnyResultRow);

	// Clearing the search removes the results again.
	Search->SetText(FText());
	Search->OnTextChanged.Broadcast(Search->GetText());
	TestEqual(TEXT("clearing the search restores the plain list"), CountResultRows(), RowsWithoutSearch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationSelectFromListTest, "InNavi.Navigation.Search.SelectFromList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationSelectFromListTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	APawn* Pawn = Fixture.World->SpawnActor<ADefaultPawn>();
	Controller->Possess(Pawn);
	if (!TestTrue(TEXT("the controller has a pawn to move"), Controller->GetPawn() == Pawn)) return false;

	APolyBuilding* First = Fixture.Generator->FindBuildingById(TEXT("E1"));
	APolyBuilding* Second = Fixture.Generator->FindBuildingById(TEXT("E2"));
	if (!TestNotNull(TEXT("two buildings"), First) || !TestNotNull(TEXT("two buildings"), Second)) return false;

	Pawn->SetActorLocation(FVector(-123456.f, 0.f, 50.f));
	TestTrue(TEXT("picking E1 from the list works"), Controller->SelectBuildingById(TEXT("E1")));
	TestTrue(TEXT("the camera moved"), !FMath::IsNearlyEqual(Pawn->GetActorLocation().X, -123456.f, 1.f));
	TestTrue(TEXT("and looks down at the building"), Controller->GetControlRotation().Pitch < -1.f);
	TestEqual(TEXT("the building is not cut open"), First->GetVisibleInteriorLevel(), 0);

	TestTrue(TEXT("picking E2 next works"), Controller->SelectBuildingById(TEXT("E2")));
	TestEqual(TEXT("the second building is not cut open"), Second->GetVisibleInteriorLevel(), 0);
	TestEqual(TEXT("and the first one still is not"), First->GetVisibleInteriorLevel(), 0);
	TestEqual(TEXT("the pick is selected"), Controller->GetSelectedBuildingId(), FString(TEXT("E2")));
	return true;
}

#endif
