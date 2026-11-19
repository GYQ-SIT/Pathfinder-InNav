#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileArrivedPageWidget.h"
#include "MobileMapWidget.h"
#include "MobileMockSource.h"
#include "MobileNavigatePageWidget.h"
#include "MobilePlacePageWidget.h"
#include "MobileReorderHandle.h"
#include "MobileRoutePageWidget.h"
#include "MobileSearchOverlayWidget.h"
#include "MobileSearchPageWidget.h"
#include "MobileSelectWidget.h"
#include "MobileSheetFlow.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// Several destinations ("Add stop" of the prototype's second iteration): what the route page does to the list, how the screen plans a leg for each,
// walks them one after the other, and keeps the trip while a place is looked for to add to it.

namespace
{
	UMobileAppWidget* MakeJourneyScreen(UWorld* World)
	{
		UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
		if (Screen)
		{
			Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
			Screen->TakeWidget();
			Screen->ApplyViewportSize(375.0, 812.0);
		}
		return Screen;
	}

	FString JoinIds(const TArray<FString>& Ids) { return FString::Join(Ids, TEXT(",")); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileJourneyRequestTest, "InNavi.Mobile.Journey.Request", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileJourneyRequestTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileRoutePageWidget* Page = CreateWidget<UMobileRoutePageWidget>(World, UMobileRoutePageWidget::StaticClass());
	if (!TestNotNull(TEXT("the page is created"), Page)) return false;
	Page->TakeWidget();
	TArray<FMobileRouteRequest> Log;
	Page->SetOnRequestChanged([&Log](const FMobileRouteRequest& Request) { Log.Add(Request); });
	FMobileRouteRequest Start;
	Start.ToPlaceId = TEXT("a");
	Page->SetRequest(Start, FMobileRoute());
	TestEqual(TEXT("one destination"), Page->GetDestinationCount(), 1);

	Page->AddStop(TEXT("b"));
	Page->AddStop(TEXT("c"));
	TestEqual(TEXT("three destinations"), Page->GetDestinationCount(), 3);
	TestEqual(TEXT("the first is To and the rest are stops"), Page->GetRequest().ToPlaceId + TEXT("|") + JoinIds(Page->GetRequest().StopPlaceIds), FString(TEXT("a|b,c")));
	TestEqual(TEXT("each is a change of the request"), Log.Num(), 2);
	Page->AddStop(TEXT("b"));
	Page->AddStop(TEXT("a"));
	Page->AddStop(FString());
	TestEqual(TEXT("a place that is already a destination is not added again, nor is nothing"), Page->GetDestinationCount(), 3);

	Page->MoveDestination(2, 0);
	TestEqual(TEXT("moving the last to the first"), Page->GetRequest().ToPlaceId + TEXT("|") + JoinIds(Page->GetRequest().StopPlaceIds), FString(TEXT("c|a,b")));
	Page->MoveDestination(0, 1);
	TestEqual(TEXT("and the first one place down"), Page->GetRequest().ToPlaceId + TEXT("|") + JoinIds(Page->GetRequest().StopPlaceIds), FString(TEXT("a|c,b")));
	const int32 Before = Log.Num();
	Page->MoveDestination(1, 1);
	Page->MoveDestination(5, 0);
	Page->MoveDestination(0, -1);
	TestEqual(TEXT("a move to the same place or out of the list changes nothing"), Log.Num(), Before);

	Page->RemoveDestination(0);
	TestEqual(TEXT("taking the first off makes the next the first"), Page->GetRequest().ToPlaceId + TEXT("|") + JoinIds(Page->GetRequest().StopPlaceIds), FString(TEXT("c|b")));
	Page->ChooseDestination(1, TEXT("d"));
	TestEqual(TEXT("choosing another place for a stop"), JoinIds(Page->GetRequest().StopPlaceIds), FString(TEXT("d")));
	Page->ChooseDestination(0, TEXT("e"));
	TestEqual(TEXT("and for the first destination"), Page->GetRequest().ToPlaceId, FString(TEXT("e")));
	Page->RemoveDestination(1);
	Page->RemoveDestination(0);
	TestEqual(TEXT("the last destination cannot be taken off"), Page->GetDestinationCount(), 1);

	// The total of the trip is the sum of its legs.
	FMobileRoute One, Two;
	One.bFound = Two.bFound = true;
	One.Minutes = 3; One.Metres = 100.0;
	Two.Minutes = 4; Two.Metres = 50.0;
	Page->SetRequest(Start, One, false, { One, Two });
	TestTrue(TEXT("minutes and metres add up"), Page->GetTotalMinutes() == 7 && FMath::IsNearlyEqual(Page->GetTotalMetres(), 150.0));
	Page->SetRequest(Start, One);
	TestTrue(TEXT("one leg is the route itself"), Page->GetTotalMinutes() == 3 && FMath::IsNearlyEqual(Page->GetTotalMetres(), 100.0));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileJourneyWalkTest, "InNavi.Mobile.Journey.Walk", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileJourneyWalkTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = MakeJourneyScreen(World);
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;

	// A route to one place has one leg.
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-101"));
	Screen->SendSheetEvent(EMobileSheetEvent::DirectionsRequested);
	TestEqual(TEXT("the route page"), Screen->GetFlow().GetState(), EMobileSheetState::Route);
	TestTrue(TEXT("one destination, one leg"), Screen->GetDestinationCount() == 1 && Screen->GetLegs().Num() == 1 && Screen->GetCurrentRoute().bFound);
	const FMobileRoute FirstLeg = Screen->GetCurrentRoute();

	// Add stops: a leg for each, the first leg is the route the page shows, and the page knows the total.
	UMobileRoutePageWidget* Page = Screen->GetRoutePage();
	Page->AddStop(TEXT("E2-02-101"));
	Page->AddStop(TEXT("E6-2M-102"));
	TestEqual(TEXT("the stops are the screen's"), JoinIds(Screen->GetStopPlaceIds()), FString(TEXT("E2-02-101,E6-2M-102")));
	TestEqual(TEXT("three destinations"), Screen->GetDestinationCount(), 3);
	TestEqual(TEXT("three legs"), Screen->GetLegs().Num(), 3);
	TestTrue(TEXT("every leg found"), !Screen->GetLegs().ContainsByPredicate([](const FMobileRoute& Leg) { return !Leg.bFound; }));
	TestEqual(TEXT("the first leg is the route shown"), Screen->GetCurrentRoute().Steps.Num(), FirstLeg.Steps.Num());
	int32 Minutes = 0;
	for (const FMobileRoute& Leg : Screen->GetLegs()) Minutes += Leg.Minutes;
	TestEqual(TEXT("the page's total is the legs' sum"), Page->GetTotalMinutes(), Minutes);
	TestEqual(TEXT("the trip is remembered for Add stop"), JoinIds(Screen->GetTripDraft()), FString(TEXT("E2-01-101,E2-02-101,E6-2M-102")));
	TestEqual(TEXT("the second leg starts where the first ends"), Screen->GetLegs()[1].Points[0].BuildingId, Screen->GetLegs()[0].Points.Last().BuildingId);

	// The map shows the whole trip with a numbered circle where each stop on the way is (the last destination is the route's end).
	TestEqual(TEXT("two stops on the way"), Screen->GetMapView()->GetStopCount(), 2);
	int32 PointsOfAllLegs = 0;
	for (const FMobileRoute& Leg : Screen->GetLegs()) PointsOfAllLegs += Leg.Points.Num();
	TestTrue(TEXT("the route is every leg's points"), PointsOfAllLegs > Screen->GetLegs()[0].Points.Num());

	// Reordering and removing plan the legs again.
	Page->MoveDestination(2, 0);
	TestEqual(TEXT("the last became the first (the selected place)"), Screen->GetSelectedPlace().Id, FString(TEXT("E6-2M-102")));
	TestEqual(TEXT("and the others follow"), JoinIds(Screen->GetStopPlaceIds()), FString(TEXT("E2-01-101,E2-02-101")));
	Page->MoveDestination(0, 2);
	Page->RemoveDestination(1);
	TestEqual(TEXT("a stop taken off"), JoinIds(Screen->GetStopPlaceIds()), FString(TEXT("E6-2M-102")));
	TestEqual(TEXT("two legs"), Screen->GetLegs().Num(), 2);
	Page->AddStop(TEXT("E2-02-101"));

	// Walking: the first leg, then the next ones one after the other, then the arrival at the last destination.
	Screen->SendSheetEvent(EMobileSheetEvent::NavigationStarted);
	TestEqual(TEXT("walking"), Screen->GetFlow().GetState(), EMobileSheetState::Navigating);
	TestEqual(TEXT("the first leg"), Screen->GetLegIndex(), 0);
	TestEqual(TEXT("while walking the map shows the leg, with no numbered stops"), Screen->GetMapView()->GetStopCount(), 0);
	TestEqual(TEXT("the page says which destination"), Screen->GetNavigatePage()->GetStopString(), FString(TEXT("Destination 1 of 3")));
	TArray<int32> LegsSeen;
	int32 StopsReached = 0;
	FString LastLabel, LastStop;
	for (int32 Guard = 0; Guard < 200 && Screen->GetFlow().GetState() == EMobileSheetState::Navigating; ++Guard)
	{
		if (LegsSeen.IsEmpty() || LegsSeen.Last() != Screen->GetLegIndex()) LegsSeen.Add(Screen->GetLegIndex());
		LastLabel = Screen->GetNavigatePage()->GetNextLabelString();
		LastStop = Screen->GetNavigatePage()->GetStopString();
		if (LastLabel.StartsWith(TEXT("Continue to ")))
		{
			// A stop on the way: it says so, and where the walk goes on.
			++StopsReached;
			TestTrue(TEXT("a stop reached says so"), Screen->GetNavigatePage()->GetInstructionString().StartsWith(TEXT("Stop reached: ")));
		}
		Screen->AdvanceNavigation();
	}
	TestEqual(TEXT("two stops were reached on the way"), StopsReached, 2);
	TestEqual(TEXT("the last button says the walk is over"), LastLabel, FString(TEXT("I have arrived")));
	TestEqual(TEXT("at the last destination"), LastStop, FString(TEXT("Destination 3 of 3")));
	TestEqual(TEXT("every leg was walked, in order"), LegsSeen, TArray<int32>({ 0, 1, 2 }));
	TestEqual(TEXT("then the arrival"), Screen->GetFlow().GetState(), EMobileSheetState::Arrived);
	TestEqual(TEXT("at the last destination"), Screen->GetSelectedPlace().Id, FString(TEXT("E2-02-101")));

	// Finding another place ends the trip.
	Screen->SendSheetEvent(EMobileSheetEvent::FindAnotherPlace);
	TestTrue(TEXT("no stops and nothing remembered"), Screen->GetStopPlaceIds().IsEmpty() && Screen->GetTripDraft().IsEmpty() && Screen->GetLegs().IsEmpty());
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapTapInterruptsTest, "InNavi.Mobile.Journey.MapTapInterrupts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapTapInterruptsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = MakeJourneyScreen(World);
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;

	// Arrived: a tap on another room opens it, with no cross to press first, and the trip is over.
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-101"));
	Screen->SendSheetEvent(EMobileSheetEvent::DirectionsRequested);
	Screen->SendSheetEvent(EMobileSheetEvent::NavigationStarted);
	Screen->SendSheetEvent(EMobileSheetEvent::ArrivedAtDestination);
	TestEqual(TEXT("arrived"), Screen->GetFlow().GetState(), EMobileSheetState::Arrived);
	Screen->HandleMapPlaceTapped(TEXT("E2-02-101"));
	TestEqual(TEXT("a tap after arriving opens the new place"), Screen->GetFlow().GetState(), EMobileSheetState::Place);
	TestEqual(TEXT("it is the room tapped"), Screen->GetSelectedPlace().Id, FString(TEXT("E2-02-101")));
	TestTrue(TEXT("and the old trip is gone"), Screen->GetTripDraft().IsEmpty() && Screen->GetLegs().IsEmpty());

	// Walking: the walk is interrupted for the new place.
	Screen->SendSheetEvent(EMobileSheetEvent::DirectionsRequested);
	Screen->SendSheetEvent(EMobileSheetEvent::NavigationStarted);
	TestEqual(TEXT("walking"), Screen->GetFlow().GetState(), EMobileSheetState::Navigating);
	Screen->HandleMapPlaceTapped(TEXT("E6-2M-102"));
	TestEqual(TEXT("a tap while walking opens the new place"), Screen->GetFlow().GetState(), EMobileSheetState::Place);
	TestEqual(TEXT("it is the room tapped (walking)"), Screen->GetSelectedPlace().Id, FString(TEXT("E6-2M-102")));
	TestTrue(TEXT("the walk is gone"), Screen->GetTripDraft().IsEmpty() && Screen->GetLegs().IsEmpty());

	// A route only being looked at: the new place opens, and the trip stays so it can be added as a stop.
	Screen->SendSheetEvent(EMobileSheetEvent::DirectionsRequested);
	TestEqual(TEXT("the route page"), Screen->GetFlow().GetState(), EMobileSheetState::Route);
	Screen->HandleMapPlaceTapped(TEXT("E2-02-101"));
	TestEqual(TEXT("a tap on the route page opens the new place"), Screen->GetFlow().GetState(), EMobileSheetState::Place);
	TestEqual(TEXT("it is the room tapped (route)"), Screen->GetSelectedPlace().Id, FString(TEXT("E2-02-101")));
	TestEqual(TEXT("the trip is kept for Add stop"), JoinIds(Screen->GetTripDraft()), FString(TEXT("E6-2M-102")));

	// A tap on nothing known changes nothing.
	Screen->HandleMapPlaceTapped(TEXT("no-such-room"));
	TestEqual(TEXT("an unknown room is ignored"), Screen->GetSelectedPlace().Id, FString(TEXT("E2-02-101")));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileJourneyTripTest, "InNavi.Mobile.Journey.Trip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileJourneyTripTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = MakeJourneyScreen(World);
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	UMobileSearchOverlayWidget* Overlay = Screen->GetSearchOverlay();

	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-101"));
	TestFalse(TEXT("with no trip open a place has Get directions"), Screen->CanAddStopFromPlace());
	Screen->RequestDirections();
	TestEqual(TEXT("the route page"), Screen->GetFlow().GetState(), EMobileSheetState::Route);
	TestEqual(TEXT("a trip of one place"), JoinIds(Screen->GetTripDraft()), FString(TEXT("E2-01-101")));

	// The route page is left by the map's search (the field stays there): the trip is kept and the new place can be added to it.
	Screen->OpenMapSearch();
	Overlay->ChooseRow(TEXT("place:E2-01-103"));
	TestEqual(TEXT("the place page of the new place"), Screen->GetFlow().GetState(), EMobileSheetState::Place);
	TestEqual(TEXT("which is the selected place"), Screen->GetSelectedPlace().Id, FString(TEXT("E2-01-103")));
	TestTrue(TEXT("the button says Add stop"), Screen->CanAddStopFromPlace());
	TestTrue(TEXT("on the page too"), Screen->GetPlacePage()->IsAddStopMode() && Screen->GetPlacePage()->GetDirectionsLabelString() == TEXT("Add stop"));
	Screen->RequestDirections();
	TestEqual(TEXT("back on the route page"), Screen->GetFlow().GetState(), EMobileSheetState::Route);
	TestEqual(TEXT("the first destination is the first again"), Screen->GetSelectedPlace().Id, FString(TEXT("E2-01-101")));
	TestEqual(TEXT("and the new place is its stop"), JoinIds(Screen->GetStopPlaceIds()), FString(TEXT("E2-01-103")));
	TestEqual(TEXT("with two legs"), Screen->GetLegs().Num(), 2);

	// A place that is already in the trip cannot be added.
	Screen->OpenMapSearch();
	Overlay->ChooseRow(TEXT("place:E2-01-103"));
	TestFalse(TEXT("it is in the trip"), Screen->CanAddStopFromPlace());

	// Closing the route page ends the trip; the next directions are a new trip.
	Screen->OpenMapSearch();
	Overlay->ChooseRow(TEXT("place:E2-01-104"));
	Screen->RequestDirections();
	TestEqual(TEXT("a trip of three"), Screen->GetDestinationCount(), 3);
	Screen->SendSheetEvent(EMobileSheetEvent::Back);
	TestEqual(TEXT("back on the place page"), Screen->GetFlow().GetState(), EMobileSheetState::Place);
	TestTrue(TEXT("no stops, nothing remembered"), Screen->GetStopPlaceIds().IsEmpty() && Screen->GetTripDraft().IsEmpty());
	TestFalse(TEXT("so the button is Get directions"), Screen->CanAddStopFromPlace());
	TestTrue(TEXT("on the page too"), !Screen->GetPlacePage()->IsAddStopMode() && Screen->GetPlacePage()->GetDirectionsLabelString() == TEXT("Get directions"));
	Screen->RequestDirections();
	TestEqual(TEXT("a trip of one"), Screen->GetDestinationCount(), 1);

	// Walking, then Exit: the route page again, at the first leg, with the stops.
	Screen->OpenMapSearch();
	Overlay->ChooseRow(TEXT("place:E2-01-102"));
	Screen->RequestDirections();
	Screen->SendSheetEvent(EMobileSheetEvent::NavigationStarted);
	Screen->AdvanceNavigation();
	Screen->SendSheetEvent(EMobileSheetEvent::ExitNavigation);
	TestEqual(TEXT("the route page"), Screen->GetFlow().GetState(), EMobileSheetState::Route);
	TestEqual(TEXT("with the stops"), Screen->GetDestinationCount(), 2);
	TestEqual(TEXT("at the first leg"), Screen->GetLegIndex(), 0);

	// Opening another site starts everything again.
	Screen->OpenSite(TEXT("paya-lebar"));
	TestTrue(TEXT("no trip"), Screen->GetStopPlaceIds().IsEmpty() && Screen->GetTripDraft().IsEmpty() && Screen->GetLegs().IsEmpty());
	World->DestroyWorld(false);
	return true;
}

// What the route page shows and does for the stops: a drop-down for each destination, Add stop, a handle to move a stop, the line over the numbers, the
// ways between levels, and Route options and Route steps that open and close.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRouteStopsUiTest, "InNavi.Mobile.Journey.RouteUi", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRouteStopsUiTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobileRoutePageWidget* Page = CreateWidget<UMobileRoutePageWidget>(World, UMobileRoutePageWidget::StaticClass());
	if (!TestNotNull(TEXT("the page is created"), Page)) return false;
	Page->TakeWidget();
	const TArray<FMobilePlace> All = Source.SearchPlaces(TEXT("sit-punggol"), FString(), FMobilePlaceFilter());
	Page->SetChoices(All, Source.ListBuildings(TEXT("sit-punggol")));
	for (const TCHAR* Name : { TEXT("Btn_RouteBack"), TEXT("Txt_RouteTitle"), TEXT("RouteStopsCard"), TEXT("RouteStops"), TEXT("RouteTransport"), TEXT("RouteOptionsHeader"), TEXT("RouteStepsHeader"), TEXT("RouteOptions"), TEXT("RouteSteps"), TEXT("Btn_StartNavigation"), TEXT("Txt_RouteVia") })
	{
		TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Page->FindNamedWidget(Name));
	}
	TestEqual(TEXT("the title"), Cast<UTextBlock>(Page->FindNamedWidget(TEXT("Txt_RouteTitle")))->GetText().ToString(), FString(TEXT("Your route")));

	FMobileRoute Found;
	Found.bFound = true;
	Found.Minutes = 3;
	Found.Metres = 120.0;
	FMobileRouteRequest Request;
	Request.ToPlaceId = TEXT("E2-01-101");
	Page->SetRequest(Request, Found);

	// One destination: a drop-down that shows the place's name (the list has the place with its building and level), no handle, and Add stop.
	if (!TestEqual(TEXT("one destination row"), Page->GetDestinationCount(), 1)) return false;
	UMobileSelectWidget* First = Page->GetDestinationSelect(0);
	if (!TestNotNull(TEXT("with its drop-down"), First)) return false;
	TestEqual(TEXT("the field shows the name"), First->GetSelectedShortLabel(), FString(TEXT("Study lounge")));
	TestTrue(TEXT("the list shows the place with its building and level"), First->GetSelectedLabel().StartsWith(TEXT("Study lounge")) && First->GetSelectedLabel() != First->GetSelectedShortLabel());
	TestEqual(TEXT("every place is a choice"), First->GetOptionCount(), All.Num());
	TestNull(TEXT("one destination has no handle to move"), Page->GetReorderHandle(0));
	TestFalse(TEXT("not adding"), Page->IsAddingStop());
	TestNull(TEXT("and no drop-down for it"), Page->GetAddSelect());
	TestEqual(TEXT("the ways are all on"), Page->GetViaString(), FString(TEXT("via Escalator, Lift, Stairs")));
	TestTrue(TEXT("no line over the numbers for one destination"), Page->GetJourneyString().IsEmpty());
	TestEqual(TEXT("the minutes"), Page->GetMinutesString(), FString(TEXT("3 min")));
	TestEqual(TEXT("the metres"), Page->GetMetresString(), FString(TEXT("120 m")));

	// Add stop turns into a drop-down of the places that are not in the trip; choosing one adds it and goes back to the words.
	Page->BeginAddStop();
	TestTrue(TEXT("adding"), Page->IsAddingStop());
	UMobileSelectWidget* Adding = Page->GetAddSelect();
	if (!TestNotNull(TEXT("the drop-down"), Adding)) return false;
	TestEqual(TEXT("'Choose a destination' and every place but the destination"), Adding->GetOptionCount(), All.Num());
	TestTrue(TEXT("it is open"), Adding->IsOpen());
	TestEqual(TEXT("the first option says what to do"), Adding->GetOptions()[0].Label, FString(TEXT("Choose a destination")));
	Page->CancelAddStop();
	TestFalse(TEXT("cancelled"), Page->IsAddingStop());
	Page->BeginAddStop();
	TArray<FMobileRouteRequest> Log;
	Page->SetOnRequestChanged([&Log](const FMobileRouteRequest& Changed) { Log.Add(Changed); });
	Page->GetAddSelect()->Choose(TEXT("E2-01-102"));
	TestFalse(TEXT("the choice ends the adding"), Page->IsAddingStop());
	TestTrue(TEXT("and is a stop"), Log.Num() == 1 && JoinIds(Log[0].StopPlaceIds) == TEXT("E2-01-102"));
	TestEqual(TEXT("two destination rows"), Page->GetDestinationCount(), 2);
	TestNotNull(TEXT("the second has its drop-down"), Page->GetDestinationSelect(1));
	TestTrue(TEXT("and each has a handle now"), Page->GetReorderHandle(0) && Page->GetReorderHandle(1));
	TestEqual(TEXT("the line says the first destination"), Page->GetViaString(), FString(TEXT("First destination \u00B7 via Escalator, Lift, Stairs")));

	// The total over the numbers.
	FMobileRoute Second = Found;
	Second.Minutes = 4;
	Second.Metres = 30.0;
	Page->SetRequest(Page->GetRequest(), Found, false, { Found, Second });
	TestEqual(TEXT("the line over the numbers"), Page->GetJourneyString(), FString(TEXT("2 destinations \u00B7 7 min \u00B7 150 m total")));
	TestEqual(TEXT("the numbers are the total"), Page->GetMinutesString(), FString(TEXT("7 min")));

	// A drag of a handle by two rows moves the stop two places (and no further than the list).
	Page->AddStop(TEXT("E2-01-103"));
	TestEqual(TEXT("three destinations"), Page->GetDestinationCount(), 3);
	Page->GetReorderHandle(0)->SimulateDrag(2.f * UMobileRoutePageWidget::StopRowHeight + 5.f);
	TestEqual(TEXT("the first went down two places"), Page->GetRequest().ToPlaceId + TEXT("|") + JoinIds(Page->GetRequest().StopPlaceIds), FString(TEXT("E2-01-102|E2-01-103,E2-01-101")));
	Page->GetReorderHandle(2)->SimulateDrag(-5.f * UMobileRoutePageWidget::StopRowHeight);
	TestEqual(TEXT("the last went up to the top, not beyond"), Page->GetRequest().ToPlaceId, FString(TEXT("E2-01-101")));
	const int32 Before = Log.Num();
	Page->GetReorderHandle(1)->SimulateDrag(10.f);
	TestEqual(TEXT("a drag of less than a row changes nothing"), Log.Num(), Before);

	// The ways between levels: three pills; Wheelchair accessible is the lift alone.
	Page->TapOption(TEXT("liftonly"));
	TestEqual(TEXT("wheelchair accessible"), Page->GetViaString(), FString(TEXT("First destination \u00B7 via Wheelchair accessible")));
	Page->TapOption(TEXT("lift"));
	TestTrue(TEXT("with no way at all it says so"), Page->GetViaString().EndsWith(TEXT("no way between levels")));

	// Route options and Route steps are closed until their line is tapped.
	TestFalse(TEXT("options closed"), Page->AreOptionsOpen());
	TestFalse(TEXT("steps closed"), Page->AreStepsOpen());
	TestEqual(TEXT("so their content is hidden"), Page->FindNamedWidget(TEXT("RouteOptions"))->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("and the steps"), Page->FindNamedWidget(TEXT("RouteSteps"))->GetVisibility(), ESlateVisibility::Collapsed);
	Page->SetOptionsOpen(true);
	Page->SetStepsOpen(true);
	TestTrue(TEXT("open"), Page->AreOptionsOpen() && Page->AreStepsOpen());
	TestEqual(TEXT("the options show"), Page->FindNamedWidget(TEXT("RouteOptions"))->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("and the steps"), Page->FindNamedWidget(TEXT("RouteSteps"))->GetVisibility(), ESlateVisibility::Visible);
	Page->SetOptionsOpen(false);
	TestEqual(TEXT("closed again"), Page->FindNamedWidget(TEXT("RouteOptions"))->GetVisibility(), ESlateVisibility::Collapsed);

	// A handle is not dragging until it is pressed.
	TestFalse(TEXT("not dragging"), Page->GetReorderHandle(0)->IsDragging());
	World->DestroyWorld(false);
	return true;
}

// The tab bar is there for the list of places and for the other tabs; the place, the route, the walk and the arrival take its room (as in the prototype).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileTabsHiddenTest, "InNavi.Mobile.Journey.TabsHidden", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileTabsHiddenTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = MakeJourneyScreen(World);
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	TestTrue(TEXT("the list of places has the tab bar"), Screen->AreTabsShown() && Screen->GetLayoutSnapshot().TabBarHeight > 0.0);

	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-101"));
	TestFalse(TEXT("the place page has not"), Screen->AreTabsShown());
	Screen->SendSheetEvent(EMobileSheetEvent::DirectionsRequested);
	TestFalse(TEXT("nor the route page"), Screen->AreTabsShown());
	Screen->SnapSheetNow();
	const FMobileLayoutSnapshot Layout = Screen->GetLayoutSnapshot();
	TestTrue(TEXT("the sheet goes down to the bottom of the screen"), FMath::IsNearlyEqual(Layout.TabBarHeight, 0.0) && FMath::IsNearlyEqual(Layout.SheetTop + Layout.SheetHeight, Layout.DesignHeight, 0.01));
	Screen->SendSheetEvent(EMobileSheetEvent::NavigationStarted);
	TestFalse(TEXT("nor the walk"), Screen->AreTabsShown());
	Screen->SnapSheetNow();
	TestTrue(TEXT("the sheet is tall enough for what the walk shows"), Screen->GetLayoutSnapshot().SheetHeight >= UMobileAppWidget::NavigationSheetHeight - 0.01);
	TestNotNull(TEXT("and its button is pinned under what scrolls"), Screen->GetNavigatePage()->FindNamedWidget(TEXT("NavigateScroll")));
	Screen->PreviewState(EMobileSheetState::Arrived);
	TestTrue(TEXT("the arrival has them again, as in the prototype"), Screen->AreTabsShown());
	UButton* ArrivedClose = Cast<UButton>(Screen->GetArrivedPage()->FindNamedWidget(TEXT("Btn_ArrivedClose")));
	TestNotNull(TEXT("and a cross to close it"), ArrivedClose);

	// Another tab has it whatever the sheet is on; so has the list when the sheet is back at it.
	Screen->SetActiveTab(UMobileAppWidget::ETab::Explore);
	TestTrue(TEXT("Explore has the tab bar"), Screen->AreTabsShown());
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	Screen->SendSheetEvent(EMobileSheetEvent::FindAnotherPlace);
	TestTrue(TEXT("and the list again"), Screen->AreTabsShown());
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
