#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "InteriorScreenWidget.h"
#include "PolyBuilding.h"
#include "NavigationRoutePlanner.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "Misc/AutomationTest.h"

namespace
{
	// The rooms of E2 that can be used as a point of a route.
	TArray<UNavigationPanelWidget::FNavigationRoomOption> ReachableRooms(const FNavigationSearchFixture& Fixture)
	{
		const APolyBuilding* E2 = Fixture.Generator->FindBuildingById(TEXT("E2"));
		TArray<UNavigationPanelWidget::FNavigationRoomOption> Rooms;
		for (const auto& Option : Fixture.Nav->GetSelectableRooms())
		{
			FString Error;
			if (E2 && FNavigationRoutePlanner::IsValidPoint(*E2, *Fixture.Interior, Fixture.Generator->GetScale(), Option.Floor, Option.Centroid, Error)) Rooms.Add(Option);
		}
		return Rooms;
	}
}

// The route is one list of places: the first is the Start, the last the Destination, any between are stops. Two places are enough.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationPlacesListTest, "InNavi.Navigation.Places.List",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationPlacesListTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	UNavigationPanelWidget* Nav = Fixture.Nav;
	const auto Rooms = ReachableRooms(Fixture);
	if (!TestTrue(TEXT("E2 has three reachable rooms"), Rooms.Num() >= 3)) return false;
	const FString A = Rooms[0].GetDisplayLabel();
	const FString B = Rooms[2].GetDisplayLabel();
	const FString C = Rooms[1].GetDisplayLabel();

	TestEqual(TEXT("An empty route has the two ends as rows"), Nav->GetPlaceCount(), 2);
	TestEqual(TEXT("none of them set"), Nav->GetSetPlaceCount(), 0);
	TestEqual(TEXT("the first is the Start"), Nav->GetPlaceRole(0), FString(TEXT("Start")));
	TestEqual(TEXT("the last the Destination"), Nav->GetPlaceRole(1), FString(TEXT("Destination")));
	TestFalse(TEXT("nothing to look for a route with"), Nav->CanFindRoute());

	// A Start and a stop are a route: the destination is not mandatory.
	TestTrue(TEXT("the Start is set from the list"), Nav->SetPlaceToRoom(0, A));
	TestFalse(TEXT("one place is not a route"), Nav->CanFindRoute());
	TestTrue(TEXT("a stop is added"), Nav->AddStopToRoom(B));
	TestEqual(TEXT("the Start, the stop and the empty Destination"), Nav->GetPlaceCount(), 3);
	TestEqual(TEXT("the middle one is a stop"), Nav->GetPlaceRole(1), FString(TEXT("Stop 1")));
	TestEqual(TEXT("two are set"), Nav->GetSetPlaceCount(), 2);
	TestFalse(TEXT("the Destination is not set"), Nav->IsPlaceSet(2));
	TestTrue(TEXT("a Start and a stop are enough to look for a route"), Nav->CanFindRoute());
	Nav->RequestFindRoute();
	TestFalse(*FString::Printf(TEXT("and the route is found (%s)"), *Nav->GetDisplayStatusMessage()), Nav->DidLastFindRouteFail());
	TestFalse(TEXT("with points"), Nav->GetRoutePoints().IsEmpty());
	TestEqual(TEXT("the stop became the destination"), Nav->GetPlaceCount(), 2);
	TestEqual(TEXT("which shows the place"), Nav->GetPlaceText(1), B);

	// A third place goes between the ends.
	Nav->RequestClearRoute();
	TestEqual(TEXT("Clear Route empties the list"), Nav->GetSetPlaceCount(), 0);
	TestEqual(TEXT("to the two ends"), Nav->GetPlaceCount(), 2);
	Nav->SetPlaceToRoom(0, A);
	Nav->SetPlaceToRoom(1, C);
	TestTrue(TEXT("a stop"), Nav->AddStopToRoom(B));
	TestEqual(TEXT("goes before the Destination"), Nav->GetPlaceCount(), 3);
	TestEqual(TEXT("the Start"), Nav->GetPlaceText(0), A);
	TestEqual(TEXT("the stop"), Nav->GetPlaceText(1), B);
	TestEqual(TEXT("the Destination"), Nav->GetPlaceText(2), C);

	// Up and down.
	TestEqual(TEXT("the stop moves down"), Nav->MovePlace(1, 1), 2);
	TestEqual(TEXT("and is the Destination"), Nav->GetPlaceText(2), B);
	TestEqual(TEXT("the one it passed is now the stop"), Nav->GetPlaceText(1), C);
	TestEqual(TEXT("the last cannot go further down"), Nav->MovePlace(2, 1), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("nor the first further up"), Nav->MovePlace(0, -1), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("the Start moves down"), Nav->MovePlace(0, 1), 1);
	TestEqual(TEXT("and the stop takes its place"), Nav->GetPlaceText(0), C);

	// Reverse.
	Nav->ReversePlaces();
	TestEqual(TEXT("Reverse turns the list round: the Start"), Nav->GetPlaceText(0), B);
	TestEqual(TEXT("the stop"), Nav->GetPlaceText(1), A);
	TestEqual(TEXT("the Destination"), Nav->GetPlaceText(2), C);

	// Replace, remove.
	TestTrue(TEXT("a stop is replaced from the list"), Nav->SetPlaceToRoom(1, Rooms.Num() > 3 ? Rooms[3].GetDisplayLabel() : A));
	TestEqual(TEXT("without adding a place"), Nav->GetPlaceCount(), 3);
	TestTrue(TEXT("a stop is removed"), Nav->ClearPlace(1));
	TestEqual(TEXT("and the list is the two ends"), Nav->GetPlaceCount(), 2);
	TestTrue(TEXT("an end is emptied, not removed"), Nav->ClearPlace(0));
	TestEqual(TEXT("so the list keeps its length"), Nav->GetPlaceCount(), 2);
	TestFalse(TEXT("the Start is empty"), Nav->IsPlaceSet(0));
	TestEqual(TEXT("and the Destination is still there"), Nav->GetPlaceText(1), C);
	return true;
}

// The Navigation panel: the places list and its buttons, and the two views.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationPlacesPanelTest, "InNavi.Navigation.Places.Panel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationPlacesPanelTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>();
	Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(Controller);
	FScriptDelegate Content;
	Content.BindUFunction(Screen, TEXT("HandleInteriorContentChanged"));
	Controller->OnInteriorContentChanged.AddUnique(Content);
	FScriptDelegate Repaint;
	Repaint.BindUFunction(Screen, TEXT("HandleNavigationStateChanged"));
	Fixture.Nav->OnStateChanged.AddUnique(Repaint);
	Controller->OnInteriorContentChanged.Broadcast();

	auto Find = [&](const TCHAR* Name) { return Screen->FindScreenWidget(Name); };
	auto Visible = [&](const TCHAR* Name)
	{
		const UWidget* Widget = Cast<UWidget>(Find(Name));
		return Widget && Widget->GetVisibility() != ESlateVisibility::Collapsed && Widget->GetVisibility() != ESlateVisibility::Hidden;
	};
	auto Click = [&](const TCHAR* Name) -> bool
	{
		UButton* Button = Cast<UButton>(Find(Name));
		if (Button) Button->OnClicked.Broadcast();
		return Button != nullptr;
	};
	auto Pick = [&](int32 Slot, const FString& Label)
	{
		UComboBoxString* Combo = Cast<UComboBoxString>(Find(*FString::Printf(TEXT("Cbx_NavPlace%d"), Slot)));
		if (Combo)
		{
			Combo->SetSelectedOption(Label);
			Combo->OnSelectionChanged.Broadcast(Label, ESelectInfo::OnMouseClick);
		}
		return Combo != nullptr;
	};
	auto ShownIn = [&](int32 Slot) -> FString
	{
		const UComboBoxString* Combo = Cast<UComboBoxString>(Find(*FString::Printf(TEXT("Cbx_NavPlace%d"), Slot)));
		return Combo ? Combo->GetSelectedOption() : FString();
	};
	const auto Rooms = ReachableRooms(Fixture);
	if (!TestTrue(TEXT("E2 has three reachable rooms"), Rooms.Num() >= 3)) return false;
	const FString A = Rooms[0].GetDisplayLabel();
	const FString B = Rooms[1].GetDisplayLabel();
	const FString C = Rooms[2].GetDisplayLabel();

	// Plan, with the two ends.
	TestTrue(TEXT("Plan is shown"), Visible(TEXT("Box_NavPlan")));
	TestFalse(TEXT("Go is not"), Visible(TEXT("Box_NavGo")));
	TestTrue(TEXT("the Start row is shown"), Visible(TEXT("Row_NavPlaceSize0")));
	TestTrue(TEXT("the Destination row"), Visible(TEXT("Row_NavPlaceSize1")));
	TestFalse(TEXT("and no more"), Visible(TEXT("Row_NavPlaceSize2")));
	UButton* Find_ = Cast<UButton>(Find(TEXT("Btn_NavFindRoute")));
	if (!TestNotNull(TEXT("Find Route exists"), Find_)) return false;
	TestFalse(TEXT("Find Route waits for two places"), Find_->GetIsEnabled());

	// A Start and a stop, no Destination.
	TestTrue(TEXT("the Start is picked"), Pick(0, A));
	TestFalse(TEXT("one place is not enough"), Find_->GetIsEnabled());
	TestTrue(TEXT("Add stop is clicked"), Click(TEXT("Btn_NavAddStop")));
	TestTrue(TEXT("a row appears before the Destination"), Visible(TEXT("Row_NavPlaceSize2")));
	TestTrue(TEXT("the stop is picked in it"), Pick(1, B));
	TestEqual(TEXT("the stop is a place"), Fixture.Nav->GetStops().Num(), 1);
	TestTrue(TEXT("Find Route is enabled with a Start and a stop"), Find_->GetIsEnabled());
	TestEqual(TEXT("the Start shows"), ShownIn(0), A);
	TestEqual(TEXT("the stop shows"), ShownIn(1), B);
	TestTrue(TEXT("Done closes the row for adding"), Click(TEXT("Btn_NavAddStop")));
	TestFalse(TEXT("which goes"), Visible(TEXT("Row_NavPlaceSize3")));

	// The number selects the row; Down moves it.
	TestFalse(TEXT("no Up, Down, Remove before a row is chosen"), Visible(TEXT("Row_NavPlaceActionsSize")));
	TestTrue(TEXT("a number is clicked"), Click(TEXT("Btn_NavPlaceBadge0")));
	TestTrue(TEXT("then they are there"), Visible(TEXT("Row_NavPlaceActionsSize")));
	TestFalse(TEXT("the first cannot go up"), Cast<UButton>(Find(TEXT("Btn_NavPlaceUp")))->GetIsEnabled());
	TestTrue(TEXT("Down is clicked"), Click(TEXT("Btn_NavPlaceDown")));
	TestEqual(TEXT("the places changed order: now the stop is first"), ShownIn(0), B);
	TestEqual(TEXT("and the Start is next"), ShownIn(1), A);
	TestTrue(TEXT("Reverse is clicked"), Click(TEXT("Btn_NavReverse")));
	TestTrue(TEXT("Reverse turns the list round: the empty end goes to the Start"), ShownIn(0).IsEmpty());
	TestEqual(TEXT("the other end is the Destination"), ShownIn(2), B);
	TestEqual(TEXT("and the stop stays between"), ShownIn(1), A);
	TestTrue(TEXT("Remove: the number is chosen"), Click(TEXT("Btn_NavPlaceBadge1")));
	TestTrue(TEXT("and Remove is clicked"), Click(TEXT("Btn_NavPlaceRemove")));
	TestEqual(TEXT("the stop is gone"), Fixture.Nav->GetStops().Num(), 0);
	TestFalse(TEXT("with nothing chosen again"), Visible(TEXT("Row_NavPlaceActionsSize")));

	// A route found goes to Go; Edit Route goes back.
	TestTrue(TEXT("a Start"), Pick(0, A));
	TestTrue(TEXT("a Destination"), Pick(1, C));
	Fixture.Nav->RequestFindRoute();
	TestFalse(TEXT("found"), Fixture.Nav->DidLastFindRouteFail());
	TestTrue(TEXT("Go is shown for the route"), Visible(TEXT("Box_NavGo")));
	TestFalse(TEXT("Plan is not"), Visible(TEXT("Box_NavPlan")));
	TestTrue(TEXT("Edit Route is clicked"), Click(TEXT("Btn_NavEditRoute")));
	TestTrue(TEXT("Plan is back"), Visible(TEXT("Box_NavPlan")));
	TestTrue(TEXT("and the Go tab shows Go again"), Click(TEXT("Btn_NavTabGo")) && Visible(TEXT("Box_NavGo")));

	// Clear Route empties the rows.
	TestTrue(TEXT("Clear Route is clicked"), Click(TEXT("Btn_NavClearRoute")));
	TestTrue(TEXT("Plan"), Visible(TEXT("Box_NavPlan")));
	TestTrue(TEXT("the Start is empty"), ShownIn(0).IsEmpty());
	TestTrue(TEXT("the Destination is empty"), ShownIn(1).IsEmpty());
	TestFalse(TEXT("and there are two rows again"), Visible(TEXT("Row_NavPlaceSize2")));
	Fixture.Nav->OnStateChanged.Remove(Repaint);
	Controller->OnInteriorContentChanged.Remove(Content);
	return true;
}

#endif
