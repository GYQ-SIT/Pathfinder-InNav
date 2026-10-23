#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "InteriorScreenWidget.h"
#include "PolyBuilding.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Misc/AutomationTest.h"

// A Start or Destination picked from a room dropdown in one building must still show in the dropdown after another building is opened.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationOtherBuildingDropdownTest, "InNavi.Navigation.Search.OtherBuildingDropdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationOtherBuildingDropdownTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>();
	Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(Controller);
	// What the open screen is wired to: the lists refill when the interior changes, and the panel repaints on every state change.
	FScriptDelegate Content;
	Content.BindUFunction(Screen, TEXT("HandleInteriorContentChanged"));
	Controller->OnInteriorContentChanged.AddUnique(Content);
	FScriptDelegate Repaint;
	Repaint.BindUFunction(Screen, TEXT("HandleNavigationStateChanged"));
	Fixture.Nav->OnStateChanged.AddUnique(Repaint);
	Controller->OnInteriorContentChanged.Broadcast();
	UComboBoxString* Starts = Cast<UComboBoxString>(Screen->FindScreenWidget(TEXT("Cbx_NavStartRoom")));
	UComboBoxString* Destinations = Cast<UComboBoxString>(Screen->FindScreenWidget(TEXT("Cbx_NavDestinationRoom")));
	UComboBoxString* Switcher = Cast<UComboBoxString>(Screen->FindScreenWidget(TEXT("Cbx_BuildingSwitcher")));
	if (!TestNotNull(TEXT("Start dropdown exists"), Starts) || !TestNotNull(TEXT("Destination dropdown exists"), Destinations)
		|| !TestNotNull(TEXT("Building switcher exists"), Switcher)) return false;

	const APolyBuilding* E2 = Fixture.Generator->FindBuildingById(TEXT("E2"));
	const APolyBuilding* E3 = Fixture.Generator->FindBuildingById(TEXT("E3"));
	if (!TestNotNull(TEXT("E2 exists"), E2) || !TestNotNull(TEXT("E3 exists"), E3)) return false;

	// Two rooms in E2 that can be started from.
	TArray<UNavigationPanelWidget::FNavigationRoomOption> Rooms;
	for (const auto& Option : Fixture.Nav->GetSelectableRooms())
	{
		FString Error;
		if (FNavigationRoutePlanner::IsValidPoint(*E2, *Fixture.Interior, Fixture.Generator->GetScale(), Option.Floor, Option.Centroid, Error)) Rooms.Add(Option);
	}
	if (!TestTrue(TEXT("E2 has two reachable rooms"), Rooms.Num() >= 2)) return false;
	const FString StartLabel = Rooms[0].GetDisplayLabel();
	const FString DestinationLabel = Rooms[1].GetDisplayLabel();
	// The entry for a room in another building has no id: building, name, floor.
	const FString StartShownElsewhere = FString::Printf(TEXT("%s (Floor %d)"), *Rooms[0].RoomName, Rooms[0].Floor);
	const FString DestinationShownElsewhere = FString::Printf(TEXT("%s (Floor %d)"), *Rooms[1].RoomName, Rooms[1].Floor);

	Starts->SetSelectedOption(StartLabel);
	Starts->OnSelectionChanged.Broadcast(StartLabel, ESelectInfo::OnMouseClick);
	Destinations->SetSelectedOption(DestinationLabel);
	Destinations->OnSelectionChanged.Broadcast(DestinationLabel, ESelectInfo::OnMouseClick);
	TestTrue(TEXT("Start and Destination are set from the dropdowns"), Fixture.Nav->WasStartSetViaDropdown() && Fixture.Nav->WasDestinationSetViaDropdown());
	const int32 StartOptionsInE2 = Starts->GetOptionCount();
	TestEqual(TEXT("Inside its own building the dropdown shows just the room"), Starts->GetSelectedOption(), StartLabel);

	// The user opens another building.
	Switcher->SetSelectedOption(E3->BuildingName);
	Switcher->OnSelectionChanged.Broadcast(E3->BuildingName, ESelectInfo::OnMouseClick);
	TestEqual(TEXT("E3 is open"), Fixture.Interior->GetLoadedBuildingId(), FString(TEXT("E3")));
	TestEqual(TEXT("Start is kept"), Fixture.Nav->GetStart().BuildingId, FString(TEXT("E2")));
	TestTrue(TEXT("Start still counts as set from the dropdown"), Fixture.Nav->WasStartSetViaDropdown());
	const FString E2Name = E2->BuildingName.IsEmpty() ? FString(TEXT("E2")) : E2->BuildingName;
	TestEqual(TEXT("The Start dropdown keeps showing the Start, with its building"), Starts->GetSelectedOption(), E2Name + TEXT(" · ") + StartShownElsewhere);
	TestEqual(TEXT("The Destination dropdown keeps showing the Destination, with its building"), Destinations->GetSelectedOption(), E2Name + TEXT(" · ") + DestinationShownElsewhere);

	// Picking that entry again is not a new Start.
	const FString Shown = Starts->GetSelectedOption();
	Starts->OnSelectionChanged.Broadcast(Shown, ESelectInfo::OnMouseClick);
	TestEqual(TEXT("Picking the shown entry changes nothing"), Fixture.Nav->GetStart().BuildingId, FString(TEXT("E2")));
	TestTrue(TEXT("and gives no error"), !Fixture.Nav->GetDisplayStatusMessage().Contains(TEXT("Could not find")));

	// Back in its own building the extra entry is gone.
	const FString E2Id = TEXT("E2");
	const FString E2SwitcherName = E2->BuildingName;
	Switcher->SetSelectedOption(E2SwitcherName);
	Switcher->OnSelectionChanged.Broadcast(E2SwitcherName, ESelectInfo::OnMouseClick);
	TestEqual(TEXT("E2 is open again"), Fixture.Interior->GetLoadedBuildingId(), E2Id);
	TestEqual(TEXT("The dropdown shows the plain room again"), Starts->GetSelectedOption(), StartLabel);
	TestEqual(TEXT("and the extra entry is gone"), Starts->GetOptionCount(), StartOptionsInE2);

	// Clearing the route clears both dropdowns, in any building.
	Switcher->SetSelectedOption(E3->BuildingName);
	Switcher->OnSelectionChanged.Broadcast(E3->BuildingName, ESelectInfo::OnMouseClick);
	UButton* ClearRoute = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_NavClearRoute")));
	if (!TestNotNull(TEXT("Clear Route button exists"), ClearRoute)) return false;
	ClearRoute->OnClicked.Broadcast();
	TestTrue(TEXT("Clear Route empties the Start dropdown"), Starts->GetSelectedOption().IsEmpty());
	TestTrue(TEXT("and the Destination dropdown"), Destinations->GetSelectedOption().IsEmpty());
	return true;
}

#endif
