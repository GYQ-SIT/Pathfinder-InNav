#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileMapWidget.h"
#include "MobileMockSource.h"
#include "MobileArrivedPageWidget.h"
#include "MobileNavigatePageWidget.h"
#include "MobilePlacePageWidget.h"
#include "MobileRoutePageWidget.h"
#include "MobileSearchPageWidget.h"
#include "MobileSheetFlow.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// The phone app's map: the floor plan of a level and the overview of the site, what is drawn on them (route, destination, the person), how they
// fit the uncovered part of the screen, taps on rooms and buildings, and how the sheet's state, the level button and Overview drive them.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapWidgetTest, "InNavi.Mobile.Map", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapWidgetTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobileMapWidget* Map = CreateWidget<UMobileMapWidget>(World, UMobileMapWidget::StaticClass());
	if (!TestNotNull(TEXT("the map is created"), Map)) return false;
	Map->TakeWidget();
	Map->SetSource(&Source, TEXT("sit-punggol"));
	TestFalse(TEXT("nothing is shown before a level is chosen"), Map->HasLevelMap());
	// A user widget is not hit-testable by itself; without this a tap on a room never reaches the map (found by clicking in the running game).
	TestEqual(TEXT("the map takes clicks"), Map->GetVisibility(), ESlateVisibility::Visible);

	Map->ShowLevel(TEXT("E2"), 1);
	TestTrue(TEXT("a level is shown"), Map->HasLevelMap() && Map->GetBuildingId() == TEXT("E2") && Map->GetLevel() == 1);
	Map->ShowLevel(TEXT("E2"), 99);
	TestFalse(TEXT("a level the building does not have shows nothing"), Map->HasLevelMap());
	Map->ShowLevel(TEXT("E6"), 3);
	TestTrue(TEXT("E6's mezzanine"), Map->HasLevelMap() && Map->GetLevel() == 3);

	// The plan fits the part of the widget that is not covered, on small, ordinary and tall phones, flat and tilted.
	struct FPhone { double Width, Height, SheetCovers; };
	for (const FPhone& Phone : { FPhone{ 360.0, 640.0, 300.0 }, FPhone{ 375.0, 812.0, 437.0 }, FPhone{ 412.0, 915.0, 480.0 } })
	{
		const double Top = 64.0;
		Map->SetFitInsets(Top, Phone.SheetCovers);
		for (const bool bTilted : { false, true })
		{
			Map->Set3D(bTilted);
			const FBox2D Extent = Map->GetPlanExtentInWidget(Phone.Width, Phone.Height);
			const FString Where = FString::Printf(TEXT("%.0fx%.0f %s"), Phone.Width, Phone.Height, bTilted ? TEXT("tilted") : TEXT("flat"));
			TestTrue(*(Where + TEXT(": the plan is on the widget")), Extent.bIsValid && Extent.Min.X >= 0.0 && Extent.Max.X <= Phone.Width);
			TestTrue(*(Where + TEXT(": below the top bar")), Extent.Min.Y >= Top - 0.5);
			TestTrue(*(Where + TEXT(": above the sheet")), Extent.Max.Y <= Phone.Height - Phone.SheetCovers + 0.5);
			TestTrue(*(Where + TEXT(": and not tiny")), Extent.GetSize().X > 120.0);
		}
	}
	Map->Set3D(false);
	Map->SetFitInsets(64.0, 437.0);

	// A tap on a room finds its place; elsewhere finds none.
	FMobileLevelMap Level;
	Source.GetLevelMap(TEXT("E2"), 1, Level);
	Map->ShowLevel(TEXT("E2"), 1);
	const FMobileMapRoom& Lounge = Level.Rooms[0];
	const FVector2D Inside = Map->ToWidget(FVector2D(Lounge.Polygon[0] + Lounge.Polygon[2]) * 0.5, 375.0, 812.0);
	TestEqual(TEXT("a tap in a room finds the place"), Map->PlaceAt(Inside, 375.0, 812.0), Lounge.PlaceId);
	TestTrue(TEXT("a tap in the corridor finds none"), Map->PlaceAt(Map->ToWidget(FVector2D(50.0, 50.0), 375.0, 812.0), 375.0, 812.0).IsEmpty());
	TestTrue(TEXT("a tap off the plan finds none"), Map->PlaceAt(FVector2D(2.0, 700.0), 375.0, 812.0).IsEmpty());
	FString Tapped;
	Map->SetOnPlaceTapped([&Tapped](const FString& Id) { Tapped = Id; });
	Map->Tap(Inside, 375.0, 812.0);
	TestEqual(TEXT("and a tap reports it"), Tapped, Lounge.PlaceId);
	Map->Set3D(true);
	TestEqual(TEXT("also in the tilted view"), Map->PlaceAt(Map->ToWidget(FVector2D(Lounge.Polygon[0] + Lounge.Polygon[2]) * 0.5, 375.0, 812.0), 375.0, 812.0), Lounge.PlaceId);
	Map->Set3D(false);

	// The route is drawn where it is on the level shown.
	FMobileRouteRequest Request;
	Request.ToPlaceId = TEXT("E2-02-101");
	const FMobileRoute Route = Source.FindRoute(Request);
	Map->SetRoute(Route);
	const int32 OnFirst = Map->GetRoutePointsOnLevel();
	Map->ShowLevel(TEXT("E2"), 2);
	const int32 OnSecond = Map->GetRoutePointsOnLevel();
	TestTrue(TEXT("a route up a level has points on both levels"), OnFirst >= 2 && OnSecond >= 2);
	TestEqual(TEXT("and no more than it has"), OnFirst + OnSecond, Route.Points.Num());
	Map->ShowLevel(TEXT("E2"), 3);
	TestEqual(TEXT("none on a level it does not go to"), Map->GetRoutePointsOnLevel(), 0);
	Map->ClearRoute();
	Map->ShowLevel(TEXT("E2"), 1);
	TestEqual(TEXT("clearing it removes them"), Map->GetRoutePointsOnLevel(), 0);

	// The highlighted place and the person.
	Map->SetHighlightPlace(TEXT("E2-01-101"));
	TestEqual(TEXT("a place is highlighted"), Map->GetHighlightPlace(), FString(TEXT("E2-01-101")));
	FMobileRoutePoint Here;
	Here.BuildingId = TEXT("E2");
	Here.Level = 1;
	Map->SetPosition(Here);
	TestTrue(TEXT("the person has a position"), Map->HasPosition());
	Map->ClearPosition();
	TestFalse(TEXT("which can be taken away"), Map->HasPosition());

	// The overview: buildings, tapped to open.
	Map->ShowOverview();
	TestTrue(TEXT("the overview"), Map->IsOverview());
	const FMobileCampusMap Campus = Source.GetCampusMap(TEXT("sit-punggol"));
	TestEqual(TEXT("three buildings on it"), Campus.Buildings.Num(), 3);
	TArray<FVector2D> Outline = Campus.Buildings[1].Outline;
	const FVector2D OnE6 = Map->ToWidget((Outline[0] + Outline[2]) * 0.5, 375.0, 812.0);
	TestEqual(TEXT("a tap on a building finds it"), Map->BuildingAt(OnE6, 375.0, 812.0), Campus.Buildings[1].BuildingId);
	TestTrue(TEXT("rooms are not tapped in the overview"), Map->PlaceAt(OnE6, 375.0, 812.0).IsEmpty());
	FString Opened;
	Map->SetOnBuildingTapped([&Opened](const FString& Id) { Opened = Id; });
	Map->Tap(OnE6, 375.0, 812.0);
	TestEqual(TEXT("and a tap reports it"), Opened, FString(TEXT("E6")));
	const FBox2D OverviewExtent = Map->GetPlanExtentInWidget(375.0, 812.0);
	TestTrue(TEXT("the overview fits too"), OverviewExtent.Min.X >= 0.0 && OverviewExtent.Max.X <= 375.0 && OverviewExtent.Max.Y <= 812.0 - 437.0 + 0.5);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapFlowTest, "InNavi.Mobile.MapFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapFlowTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (Screen) Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	UMobileMapWidget* Map = Screen->GetMapView();
	if (!TestNotNull(TEXT("the screen has the map"), Map)) return false;
	auto Click = [](UWidget* Widget) { Cast<UButton>(Widget)->OnClicked.Broadcast(); };

	// At first: the first building's first level, with the person at its door.
	TestTrue(TEXT("E2's first level"), Map->HasLevelMap() && Screen->GetMapBuildingId() == TEXT("E2") && Screen->GetMapLevel() == 1);
	TestTrue(TEXT("with the person on it (My location)"), Map->HasPosition());

	// The level button: the list, a choice.
	Screen->SetLevelPopupOpen(true);
	TestTrue(TEXT("the level list opens"), Screen->IsLevelPopupOpen());
	Screen->ChooseMapLevel(3);
	TestFalse(TEXT("a choice closes it"), Screen->IsLevelPopupOpen());
	TestEqual(TEXT("and shows that level"), Map->GetLevel(), 3);
	TestEqual(TEXT("with the button saying so"), Cast<UTextBlock>(Screen->FindNamedWidget(TEXT("Btn_Level_Label")))->GetText().ToString(), FString(TEXT("L3")));
	Screen->LocateMe();
	TestEqual(TEXT("Locate goes back to where the person is"), Map->GetLevel(), 1);

	// Overview and a building.
	Screen->ToggleOverview();
	TestTrue(TEXT("Overview shows the site"), Map->IsOverview() && Screen->IsMapOverview());
	TestEqual(TEXT("the level button says Map"), Cast<UTextBlock>(Screen->FindNamedWidget(TEXT("Btn_Level_Label")))->GetText().ToString(), FString(TEXT("Map")));
	Screen->SetLevelPopupOpen(true);
	TestFalse(TEXT("no level list on the overview"), Screen->IsLevelPopupOpen());
	Screen->OpenBuilding(TEXT("E6"));
	TestTrue(TEXT("a building opens its first level"), !Map->IsOverview() && Map->GetBuildingId() == TEXT("E6") && Map->GetLevel() == 1);
	Screen->ToggleOverview();
	Screen->ToggleOverview();
	TestFalse(TEXT("Overview toggles"), Map->IsOverview());

	// A tilted view and back.
	Screen->SetMap3D(true);
	TestTrue(TEXT("3D tilts the map"), Map->IsShowing3D() && Screen->IsShowing3D());
	Screen->SetMap3D(false);
	TestFalse(TEXT("2D flattens it"), Map->IsShowing3D());

	// Choosing a place: the map goes to its level and marks it. A tap on a room does the same.
	Screen->OpenBuilding(TEXT("E2"));
	Screen->GetSearchPage()->ChoosePlace(TEXT("E6-2M-102"));
	TestEqual(TEXT("the place page"), Screen->GetFlow().GetState(), EMobileSheetState::Place);
	TestTrue(TEXT("the map shows the place's level"), Map->GetBuildingId() == TEXT("E6") && Map->GetLevel() == 3);
	TestEqual(TEXT("and marks the place"), Map->GetHighlightPlace(), FString(TEXT("E6-2M-102")));
	FMobileLevelMap Level;
	Screen->GetSource()->GetLevelMap(TEXT("E6"), 3, Level);
	const FMobileMapRoom& OtherRoom = Level.Rooms[2];
	Map->SetFitInsets(64.0, 437.0);
	Map->Tap(Map->ToWidget((OtherRoom.Polygon[0] + OtherRoom.Polygon[2]) * 0.5, 375.0, 812.0), 375.0, 812.0);
	TestEqual(TEXT("a tap on another room opens that place"), Screen->GetSelectedPlace().Id, OtherRoom.PlaceId);
	TestEqual(TEXT("and the map marks it"), Map->GetHighlightPlace(), OtherRoom.PlaceId);

	// The route: the map shows it from its start; walking follows the person up a level.
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")));
	TestTrue(TEXT("the route is on the map"), Map->GetRoutePointsOnLevel() >= 1);
	const FMobileRoute Route = Screen->GetCurrentRoute();
	TestTrue(TEXT("a route from E2 to E6 leaves a building"), Route.Steps.ContainsByPredicate([](const FMobileStep& S) { return S.Kind == EMobileStepKind::ExitBuilding; }));
	TestEqual(TEXT("it starts on the level it starts on"), Map->GetBuildingId(), Route.Points[0].BuildingId);
	Click(Screen->GetRoutePage()->FindNamedWidget(TEXT("Btn_StartNavigation")));
	TestTrue(TEXT("walking: the person is on the map"), Map->HasPosition());
	bool bFollowed = true;
	for (int32 Guard = 0; Guard < 30 && Screen->GetFlow().GetState() == EMobileSheetState::Navigating; ++Guard)
	{
		const FMobileStep& Step = Route.Steps[Screen->GetNavStepIndex()];
		bFollowed = bFollowed && Map->GetBuildingId() == Step.BuildingId && Map->GetLevel() == Step.Level;
		Click(Screen->GetNavigatePage()->FindNamedWidget(TEXT("Btn_NextInstruction")));
	}
	TestTrue(TEXT("each step shows the building and level the person is on"), bFollowed);
	TestEqual(TEXT("the arrival"), Screen->GetFlow().GetState(), EMobileSheetState::Arrived);
	TestTrue(TEXT("on the destination's level, with the place marked"), Map->GetBuildingId() == TEXT("E6") && Map->GetLevel() == 3 && Map->GetHighlightPlace() == OtherRoom.PlaceId);
	TestEqual(TEXT("no route is left on the map"), Map->GetRoutePointsOnLevel(), 0);

	// Back to searching: nothing marked.
	Click(Screen->GetArrivedPage()->FindNamedWidget(TEXT("Btn_FindAnother")));
	TestTrue(TEXT("nothing marked while searching"), Map->GetHighlightPlace().IsEmpty());
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
