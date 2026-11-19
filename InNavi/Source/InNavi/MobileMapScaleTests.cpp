#if WITH_DEV_AUTOMATION_TESTS

#include "MobileMapScale.h"
#include "MobileMapWidget.h"
#include "MobileMockSource.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// The phone map's scale: how big the lift and stairs circles and the room names are at a scale, where the map starts on a building too big for the screen,
// and how the view behaves there (MobileMapScale.h and the map widget).

namespace
{
	constexpr double ViewW = 375.0;
	constexpr double ViewH = 812.0;
	constexpr double InsetTop = 64.0;
	constexpr double InsetBottom = 300.0;
	// The middle of the part of the map that is not covered.
	const FVector2D ViewMiddle(ViewW * 0.5, InsetTop + (ViewH - InsetTop - InsetBottom) * 0.5);

	TArray<FVector2D> Rectangle(double X0, double Y0, double X1, double Y1)
	{
		return { FVector2D(X0, Y0), FVector2D(X1, Y0), FVector2D(X1, Y1), FVector2D(X0, Y1) };
	}

	// A long building like E2/3 (35 by 153 metres, the plan in metres), with a neighbour beside it.
	FMobileLevelMap LongBuilding(double Width = 35.0, double Length = 153.0)
	{
		FMobileLevelMap Map;
		Map.BuildingId = TEXT("E2");
		Map.Level = 1;
		Map.Outline = Rectangle(0.0, 0.0, Width, Length);
		Map.Entrance = FVector2D(Width * 0.5, 4.0);
		Map.bHasEntrance = true;
		auto AddRoom = [&](const TCHAR* Id, const TCHAR* Name, double Y0, double Y1)
		{
			FMobileMapRoom Room;
			Room.PlaceId = Id;
			Room.Name = Name;
			Room.Category = TEXT("Learning");
			Room.Polygon = Rectangle(2.0, Y0, Width - 2.0, Y1);
			Map.Rooms.Add(Room);
		};
		AddRoom(TEXT("near"), TEXT("Lectorial 6, E2-01-01"), 10.0, 30.0);
		AddRoom(TEXT("far"), TEXT("Lectorial 9, E2-01-09"), 120.0, 140.0);
		return Map;
	}

	FMobileCampusMap Campus(const FMobileLevelMap& Level)
	{
		FMobileCampusMap Map;
		FMobileCampusBuilding Here;
		Here.BuildingId = Level.BuildingId;
		Here.Name = TEXT("E2/3");
		Here.Outline = Level.Outline;
		FMobileCampusBuilding Next;
		Next.BuildingId = TEXT("E5");
		Next.Name = TEXT("E5");
		Next.Outline = Rectangle(60.0, 20.0, 100.0, 90.0);
		Map.Buildings = { Here, Next };
		return Map;
	}

	UMobileMapWidget* MapFor(UWorld* World, const FMobileLevelMap& Level)
	{
		UMobileMapWidget* Map = CreateWidget<UMobileMapWidget>(World, UMobileMapWidget::StaticClass());
		if (Map)
		{
			Map->TakeWidget();
			Map->SetFitInsets(InsetTop, InsetBottom);
			Map->SetViewSize(ViewW, ViewH);
			Map->SetMapsForTest(Level, Campus(Level));
		}
		return Map;
	}

	FBox2D PlanExtent(const UMobileMapWidget* Map) { return Map->GetPlanExtentInWidget(ViewW, ViewH); }
}

// What the arithmetic says, by itself.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapScaleArithmeticTest, "InNavi.Mobile.MapScale.Arithmetic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapScaleArithmeticTest::RunTest(const FString& Parameters)
{
	using namespace MobileMapScale;
	// The circle of a lift or staircase follows the scale, within limits.
	TestEqual(TEXT("far out the circle is the smallest"), MarkerDiameter(1.0), MinMarkerDiameter);
	TestEqual(TEXT("a staircase is three metres"), MarkerDiameter(5.0), 15.0);
	TestEqual(TEXT("far in the circle is the largest"), MarkerDiameter(40.0), MaxMarkerDiameter);
	TestTrue(TEXT("and only grows with the scale"), MarkerDiameter(2.0) <= MarkerDiameter(3.0) && MarkerDiameter(3.0) <= MarkerDiameter(6.0));
	TestFalse(TEXT("a small circle is a plain dot"), ShowsGlyph(MarkerDiameter(3.4)));
	TestTrue(TEXT("a big one carries its picture"), ShowsGlyph(MarkerDiameter(5.0)));

	// Where the map starts: the whole plan, or 100 metres across the screen, whichever is the bigger scale.
	TestTrue(TEXT("a plan too big to fit 100 metres starts at 100 metres across"), FMath::IsNearlyEqual(StartScale(2.0, 343.0, 1.0), 3.43, 1e-6));
	TestEqual(TEXT("a small plan is shown whole"), StartScale(8.0, 343.0, 1.0), 8.0);
	TestTrue(TEXT("a plan in other units is counted in metres"), FMath::IsNearlyEqual(StartScale(0.5, 343.0, 4.0), 0.8575, 1e-6));
	TestEqual(TEXT("with the whole plan on the start the zoom-out limit is as it was"), MinZoomFor(3.0, 3.0, 0.7), 0.7);
	TestTrue(TEXT("a big building can be zoomed out to the whole plan, and a little beyond"), FMath::IsNearlyEqual(MinZoomFor(2.0, 4.0, 0.7), 0.35, 1e-9));

	// The names.
	TestEqual(TEXT("the code after the comma is left off"), ShortName(TEXT("Lectorial 6, E2-01-01")), FString(TEXT("Lectorial 6")));
	TestEqual(TEXT("a name without one is as it is"), ShortName(TEXT("Male Toilet 1-0")), FString(TEXT("Male Toilet 1-0")));
	TestTrue(TEXT("a project id is not a name"), LooksLikeRawId(TEXT("SQL-1161271361-01-09")) && LooksLikeRawId(TEXT("room 123456789")));
	TestFalse(TEXT("a room code is"), LooksLikeRawId(TEXT("E2-01-101")) || LooksLikeRawId(TEXT("Lectorial 7")));
	TestTrue(TEXT("a name that is no wider than the room goes on it, a little wider too"), LabelFits(50.0, 50.0) && LabelFits(54.0, 50.0));
	TestFalse(TEXT("one much wider does not"), LabelFits(100.0, 50.0));
	return true;
}

// A building too big for the screen starts around its focus at about 100 metres across, and the whole plan is a pinch away.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapScaleStartTest, "InNavi.Mobile.MapScale.Start", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapScaleStartTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	const FMobileLevelMap Level = LongBuilding();
	UMobileMapWidget* Map = MapFor(World, Level);
	if (!TestNotNull(TEXT("the map is created"), Map)) return false;

	// 343 pixels for 100 metres: 3.43 pixels a metre, so the 153 metres are taller than the part that is not covered (448) and 35 metres are 120 wide.
	const FBox2D Start = PlanExtent(Map);
	TestTrue(TEXT("the map starts at about 100 metres across"), FMath::IsNearlyEqual(Start.GetSize().X, 35.0 * 3.43, 1.0));
	TestTrue(TEXT("so a long building is longer than the screen"), Start.GetSize().Y > ViewH - InsetTop - InsetBottom);
	TestTrue(TEXT("and the view is built around the way in (the person has no position here)"), (Map->ToWidget(Level.Entrance, ViewW, ViewH) - ViewMiddle).Size() < 0.5);

	// The person's own position wins over the way in once the view starts again.
	FMobileRoutePoint Here;
	Here.BuildingId = Level.BuildingId;
	Here.Level = 1;
	Here.Position = FVector2D(10.0, 100.0);
	Map->SetPosition(Here);
	Map->ResetView();
	TestTrue(TEXT("with a position the view is built around it"), (Map->ToWidget(Here.Position, ViewW, ViewH) - ViewMiddle).Size() < 0.5);

	// Zooming out reaches the whole plan (and a little beyond); zooming in stops at the largest.
	TestTrue(TEXT("the least zoom is below 0.7 for a building that does not fit"), Map->GetMinZoom() < UMobileMapWidget::MinZoom);
	Map->ZoomBy(-20.0);
	TestTrue(TEXT("zooming out stops at the least"), FMath::IsNearlyEqual(Map->GetZoom(), Map->GetMinZoom()));
	TestTrue(TEXT("and then the whole building is in the part that is not covered"), PlanExtent(Map).GetSize().Y <= ViewH - InsetTop - InsetBottom);
	Map->ZoomBy(40.0);
	TestTrue(TEXT("zooming in stops at the largest"), FMath::IsNearlyEqual(Map->GetZoom(), UMobileMapWidget::MaxZoom));

	// A small building is shown whole, as before.
	UMobileMapWidget* Small = MapFor(World, LongBuilding(40.0, 30.0));
	TestTrue(TEXT("a small building fits the part that is not covered"), PlanExtent(Small).Min.X >= 0.0 && PlanExtent(Small).Max.X <= ViewW && PlanExtent(Small).GetSize().Y <= ViewH - InsetTop - InsetBottom + 0.5);
	TestEqual(TEXT("and zooms out to 0.7 as before"), Small->GetMinZoom(), UMobileMapWidget::MinZoom);
	World->DestroyWorld(false);
	return true;
}

// The view round the focus: turning keeps the focus where it is, the compass turns about the middle, a place chosen from the list is brought into view,
// a tap on a place in view is not, and a route is shown whole when it fits.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapScaleFocusTest, "InNavi.Mobile.MapScale.Focus", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapScaleFocusTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	const FMobileLevelMap Level = LongBuilding();
	UMobileMapWidget* Map = MapFor(World, Level);
	if (!TestNotNull(TEXT("the map is created"), Map)) return false;

	// Turning (the plan turns about its own middle, far from the focus here) leaves the focus in the middle of the view.
	const FVector2D Focus = Map->FocusPoint();
	Map->SetRotation(0.9);
	TestTrue(TEXT("turned, the focus is still in the middle"), (Map->ToWidget(Focus, ViewW, ViewH) - ViewMiddle).Size() < 0.5);
	Map->ResetRotation();
	TestTrue(TEXT("the compass turns it back about the middle, the focus stays"), FMath::IsNearlyZero(Map->GetRotation()) && (Map->ToWidget(Focus, ViewW, ViewH) - ViewMiddle).Size() < 0.5);

	// A place chosen from the list that is in view does not move the view; one that is out of sight is brought to the middle.
	const FBox2D BeforeNear = PlanExtent(Map);
	Map->SetHighlightPlace(TEXT("near"));
	TestTrue(TEXT("a place in view leaves the view where it is"), (PlanExtent(Map).Min - BeforeNear.Min).Size() < 0.01);
	Map->SetHighlightPlace(TEXT("far"));
	const FVector2D FarAt = Map->ToWidget(FVector2D(Level.Outline[2].X * 0.5, 130.0), ViewW, ViewH);
	TestTrue(TEXT("a place out of sight is brought to the middle"), (FarAt - ViewMiddle).Size() < 0.5);

	// A route on the level is shown whole when it fits 100 metres (and its own middle is the focus).
	UMobileMapWidget* Routed = MapFor(World, Level);
	FMobileRoute Route;
	Route.bFound = true;
	for (const double Y : { 40.0, 80.0, 120.0 })
	{
		FMobileRoutePoint Point;
		Point.BuildingId = Level.BuildingId;
		Point.Level = 1;
		Point.Position = FVector2D(17.5, Y);
		Route.Points.Add(Point);
	}
	Routed->SetRoute(Route);
	const FVector2D Start = Routed->ToWidget(Route.Points[0].Position, ViewW, ViewH);
	const FVector2D End = Routed->ToWidget(Route.Points.Last().Position, ViewW, ViewH);
	TestTrue(TEXT("both ends of the route are in the part that is not covered"), Start.Y > InsetTop && End.Y > InsetTop && Start.Y < ViewH - InsetBottom && End.Y < ViewH - InsetBottom);
	TestTrue(TEXT("the same route set again does not move the view"), [&]()
	{
		Routed->PanBy(FVector2D(0.0, 20.0));
		const FVector2D Moved = Routed->ToWidget(Route.Points[0].Position, ViewW, ViewH);
		Routed->SetRoute(Route);
		return (Routed->ToWidget(Route.Points[0].Position, ViewW, ViewH) - Moved).Size() < 0.01;
	}());

	// The rest of the site round the building is drawn: the other buildings of the campus map.
	TestEqual(TEXT("one other building is drawn round the level"), Map->GetContextBuildingCount(), 1);
	World->DestroyWorld(false);
	return true;
}

// Another level of the same building keeps the view; another building starts again.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapScaleLevelTest, "InNavi.Mobile.MapScale.Level", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapScaleLevelTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobileMapWidget* Map = CreateWidget<UMobileMapWidget>(World, UMobileMapWidget::StaticClass());
	if (!TestNotNull(TEXT("the map is created"), Map)) return false;
	Map->TakeWidget();
	Map->SetSource(&Source, TEXT("sit-punggol"));
	Map->SetFitInsets(InsetTop, InsetBottom);
	Map->SetViewSize(ViewW, ViewH);
	Map->ShowLevel(TEXT("E2"), 1);
	Map->ZoomBy(1.0);
	Map->PanBy(FVector2D(10.0, 15.0));
	const double Zoom = Map->GetZoom();
	const FVector2D Pan = Map->GetPan();
	Map->ShowLevel(TEXT("E2"), 2);
	TestTrue(TEXT("changing floor keeps the zoom and where the person has moved to"), FMath::IsNearlyEqual(Map->GetZoom(), Zoom) && (Map->GetPan() - Pan).Size() < 0.01);
	Map->ShowLevel(TEXT("E6"), 1);
	TestTrue(TEXT("another building starts again"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0) && Map->GetPan().IsNearlyZero());
	World->DestroyWorld(false);
	return true;
}

#endif
