#if WITH_DEV_AUTOMATION_TESTS

#include "NavigationSearchUtils.h"
#include "NavigationPanelWidget.h"
#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "PolyBuilding.h"
#include "NavPlayerController.h"
#include "InteriorScreenWidget.h"
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

namespace
{
	struct FNavigationSearchFixture
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		ABuildingGenerator* Generator = World->SpawnActor<ABuildingGenerator>();
		AInteriorGenerator* Interior = World->SpawnActor<AInteriorGenerator>();
		UNavigationPanelWidget* Nav = NewObject<UNavigationPanelWidget>();
		FNavigationSearchFixture()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Interior->DispatchBeginPlay();
			Generator->LoadBuildingsFromFile(FPaths::ProjectDir() / TEXT("Data/buildings.json"));
			Interior->LoadInteriorForBuilding(TEXT("E2"));
			Nav->SetNavigationContext(Generator, Interior);
		}
		~FNavigationSearchFixture()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		ANavPlayerController* MakeController()
		{
			ANavPlayerController* Controller = World->SpawnActor<ANavPlayerController>();
			ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
			Controller->Player = Player;
			Player->PlayerController = Controller;
			Controller->SetAsLocalPlayerController();
			World->AddController(Controller);
			Controller->SetAllUiSoundsEnabled(false);
			FindFProperty<FObjectPropertyBase>(Controller->GetClass(), TEXT("Generator"))->SetObjectPropertyValue_InContainer(Controller, Generator);
			FindFProperty<FObjectPropertyBase>(Controller->GetClass(), TEXT("InteriorGen"))->SetObjectPropertyValue_InContainer(Controller, Interior);
			FindFProperty<FObjectPropertyBase>(Controller->GetClass(), TEXT("NavigationPanel"))->SetObjectPropertyValue_InContainer(Controller, Nav);
			return Controller;
		}
	};
}

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
	UEditableTextBox* TagsBox = Cast<UEditableTextBox>(Screen->GetWidgetFromName(TEXT("Txt_RoomTags")));
	UButton* ApplyTags = Cast<UButton>(Screen->GetWidgetFromName(TEXT("Btn_ApplyRoomTags")));
	UEditableTextBox* Search = Cast<UEditableTextBox>(Screen->GetWidgetFromName(TEXT("Txt_DestinationSearch")));
	UComboBoxString* Destinations = Cast<UComboBoxString>(Screen->GetWidgetFromName(TEXT("Cbx_NavDestinationRoom")));
	if (!TestNotNull(TEXT("Tags field exists"), TagsBox) || !TestNotNull(TEXT("Apply tags exists"), ApplyTags)
		|| !TestNotNull(TEXT("Destination search exists"), Search) || !TestNotNull(TEXT("Destination results exist"), Destinations)) return false;
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
	TypeText(Search, TEXT("PRESENTATION-TAG"));
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
	UEditableTextBox* BuildingSearch = Cast<UEditableTextBox>(Editor->GetWidgetFromName(TEXT("Txt_BuildingSearch")));
	UComboBoxString* BuildingResults = Cast<UComboBoxString>(Editor->GetWidgetFromName(TEXT("Cbx_FilteredBuildings")));
	UButton* Reset = Cast<UButton>(Editor->GetWidgetFromName(TEXT("Btn_ResetBuildingFilters")));
	if (!TestNotNull(TEXT("Building search exists"), BuildingSearch) || !TestNotNull(TEXT("Building results exist"), BuildingResults)
		|| !TestNotNull(TEXT("Reset filters exists"), Reset)) return false;
	TypeText(BuildingSearch, TEXT("E1"));
	TestTrue(TEXT("Building search populates results"), BuildingResults->GetOptionCount() > 0);
	for (const auto& Pair : Controller->GetFilteredBuildingOptions())
	{
		if (Pair.Value == TEXT("E1")) BuildingResults->OnSelectionChanged.Broadcast(Pair.Key, ESelectInfo::OnMouseClick);
	}
	TestEqual(TEXT("Result selection selects its stable building ID"), Controller->GetSelectedBuildingId(), FString(TEXT("E1")));
	TypeText(BuildingSearch, TEXT("missing-building-756"));
	TestTrue(TEXT("No results disables choices and clears hidden selection"), !BuildingResults->GetIsEnabled() && Controller->GetSelectedBuildingId().IsEmpty());
	Reset->OnClicked.Broadcast();
	TestEqual(TEXT("Reset restores all editable buildings"), BuildingResults->GetOptionCount(), Controller->GetEditableBuildingCount());
	TypeText(BuildingSearch, TEXT("E2"));
	Controller->RestoreExteriorCameraView();
	TestTrue(TEXT("Returning to exterior retains filters and excluded-building visibility"),
		Controller->GetBuildingFilterText() == TEXT("E2") && Fixture.Generator->FindBuildingById(TEXT("E1"))->IsHidden());
	return true;
}

#endif
