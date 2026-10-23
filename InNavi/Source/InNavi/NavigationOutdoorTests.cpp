#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "PolyBuilding.h"
#include "NavigationRoutePlanner.h"
#include "NavigationRouteVisualization.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

// Navigation on the exterior screen (outdoor mode): the dropdowns hold each building's main door, a click on the ground sets a
// point, and the points and route stay when a building is opened. Uses docs/test-data/entrance-test-2-with-doors.json: buildings A
// and B stand 1 m apart and C is far away; A, B and C each have a main door.

namespace NavigationOutdoorTests
{
	// Replaces the fixture's project with the test project. Returns false if it cannot be loaded.
	bool LoadTestProject(FNavigationSearchFixture& Fixture)
	{
		Fixture.Interior->ClearLoadedInterior();
		Fixture.Generator->ClearAllBuildings();
		return Fixture.Generator->LoadBuildingsFromFile(FPaths::ProjectDir() / TEXT("../docs/test-data/entrance-test-2-with-doors.json")) == 3;
	}

	// The route starts outside at one building and ends outside at another (it may go through buildings on the way: that can be
	// the shortest way).
	bool RunsBetween(const TArray<FNavigationRoutePoint>& Route, const FString& FromBuilding, const FString& ToBuilding)
	{
		return Route.Num() >= 2 && Route[0].Floor == UNavigationPanelWidget::OutdoorFloor && Route[0].BuildingId == FromBuilding
			&& Route.Last().Floor == UNavigationPanelWidget::OutdoorFloor && Route.Last().BuildingId == ToBuilding;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationOutdoorMainDoorsTest, "InNavi.Navigation.Outdoor.MainDoors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationOutdoorMainDoorsTest::RunTest(const FString& Parameters)
{
	using namespace NavigationOutdoorTests;
	FNavigationSearchFixture Fixture;
	if (!TestTrue(TEXT("test project loads"), LoadTestProject(Fixture))) return false;
	UNavigationPanelWidget* Nav = Fixture.Nav;
	Nav->SetOutdoorMode(true);

	const TArray<UNavigationPanelWidget::FNavigationRoomOption>& Options = Nav->GetSelectableRooms();
	if (!TestEqual(TEXT("one entry per building's main door"), Options.Num(), 3)) return false;
	TestEqual(TEXT("named after the building"), Options[0].GetDisplayLabel(), FString(TEXT("Building A · Main door")));
	TestEqual(TEXT("in order"), Options[2].GetDisplayLabel(), FString(TEXT("Building C · Main door")));
	const float Scale = Fixture.Generator->GetScale();
	for (const auto& Option : Options)
	{
		const APolyBuilding* Building = Fixture.Generator->FindBuildingById(Option.BuildingId);
		if (!TestNotNull(TEXT("the entry names a building"), Building)) return false;
		TestEqual(TEXT("an outdoor point"), Option.Floor, UNavigationPanelWidget::OutdoorFloor);
		FString Error;
		TestTrue(TEXT("outside its building and in its outside area"), FNavigationRoutePlanner::IsValidPoint(*Building, *Fixture.Interior, Scale, UNavigationPanelWidget::OutdoorFloor, Option.Centroid, Error));
	}
	// A's door is in its west wall (x = 0), a metre out.
	TestTrue(TEXT("A's door point is west of A, about a metre out"), FMath::IsNearlyEqual(Options[0].Centroid.X, -100.f / Scale, 0.5f) && FMath::IsNearlyEqual(Options[0].Centroid.Y, 100.f, 1.f));

	TestTrue(TEXT("search finds nothing for a word that is not there"), Nav->SearchDestinations(TEXT("zzz")).IsEmpty());
	TestEqual(TEXT("search finds all by 'main door'"), Nav->SearchDestinations(TEXT("main door")).Num(), 3);

	// Picking doors sets the points and a route can be found between buildings, all outside.
	TestTrue(TEXT("Start from A's door"), Nav->SetStartToRoom(Options[0].GetDisplayLabel()));
	TestTrue(TEXT("Destination at C's door"), Nav->SetDestinationToRoom(Options[2].GetDisplayLabel()));
	TestTrue(TEXT("both set from the dropdown"), Nav->WasStartSetViaDropdown() && Nav->WasDestinationSetViaDropdown());
	TestTrue(TEXT("both are outside"), Nav->IsStartOutside() && Nav->IsDestinationOutside());
	TestEqual(TEXT("the dropdown shows the Start as its own entry"), Nav->GetStartRoomLabel(), Options[0].GetDisplayLabel());
	TestTrue(TEXT("so no extra entry is needed"), Nav->GetStartOtherBuildingLabel().IsEmpty());
	TestTrue(TEXT("a route can be asked for"), Nav->CanFindRoute());
	Nav->RequestFindRoute();
	TestTrue(TEXT("a route is found"), !Nav->GetRoutePoints().IsEmpty());
	TestTrue(TEXT("the route runs from A's door to C's door, outside at both ends"), RunsBetween(Nav->GetRoutePoints(), TEXT("A"), TEXT("C")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationOutdoorGroundClickTest, "InNavi.Navigation.Outdoor.GroundClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationOutdoorGroundClickTest::RunTest(const FString& Parameters)
{
	using namespace NavigationOutdoorTests;
	FNavigationSearchFixture Fixture;
	if (!TestTrue(TEXT("test project loads"), LoadTestProject(Fixture))) return false;
	UNavigationPanelWidget* Nav = Fixture.Nav;
	Nav->SetOutdoorMode(true);

	// Nothing happens unless a point is being picked.
	Nav->SelectOutdoorGroundPoint(FVector2D(-10, 100), false);
	TestFalse(TEXT("no point is set without picking"), Nav->HasStart());

	Nav->ToggleSelectStart();
	Nav->SelectOutdoorGroundPoint(FVector2D(-10, 100), false);
	if (!TestTrue(TEXT("a spot next to A sets the Start"), Nav->HasStart())) return false;
	TestTrue(TEXT("outside"), Nav->IsStartOutside());
	TestEqual(TEXT("belonging to A"), Nav->GetStart().BuildingId, FString(TEXT("A")));
	TestFalse(TEXT("not from the dropdown"), Nav->WasStartSetViaDropdown());
	TestTrue(TEXT("the point has a world position"), Nav->GetStart().bHasWorldPosition);

	// Spots that are not allowed change nothing and say why.
	Nav->ToggleSelectDestination();
	Nav->SelectOutdoorGroundPoint(FVector2D(150, 100), false);
	TestFalse(TEXT("inside A is refused"), Nav->HasDestination());
	TestTrue(TEXT("and says so"), Nav->GetDisplayStatusMessage().Contains(TEXT("inside a building")));
	Nav->SelectOutdoorGroundPoint(FVector2D(5000, 5000), false);
	TestFalse(TEXT("far from every building is refused"), Nav->HasDestination());
	TestTrue(TEXT("and says so"), Nav->GetDisplayStatusMessage().Contains(TEXT("Too far")));
	Nav->SelectOutdoorGroundPoint(FVector2D(-10, 100), true);
	TestFalse(TEXT("a click on a building is refused"), Nav->HasDestination());
	TestTrue(TEXT("and says so"), Nav->GetDisplayStatusMessage().Contains(TEXT("is a building")));
	TestEqual(TEXT("still picking the Destination"), Nav->GetSelectionMode(), ENavigationSelectionMode::Destination);

	// A spot next to C finishes the pair, and a route is found.
	Nav->SelectOutdoorGroundPoint(FVector2D(1100, 100), false);
	TestTrue(TEXT("a spot next to C sets the Destination"), Nav->HasDestination());
	TestEqual(TEXT("belonging to C"), Nav->GetDestination().BuildingId, FString(TEXT("C")));
	Nav->RequestFindRoute();
	TestTrue(TEXT("a route is found"), !Nav->GetRoutePoints().IsEmpty());
	TestTrue(TEXT("the route runs from A to C, outside at both ends"), RunsBetween(Nav->GetRoutePoints(), TEXT("A"), TEXT("C")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationOutdoorKeepsStateTest, "InNavi.Navigation.Outdoor.KeepsState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationOutdoorKeepsStateTest::RunTest(const FString& Parameters)
{
	using namespace NavigationOutdoorTests;
	FNavigationSearchFixture Fixture;
	if (!TestTrue(TEXT("test project loads"), LoadTestProject(Fixture))) return false;
	UNavigationPanelWidget* Nav = Fixture.Nav;
	ANavPlayerController* Controller = Fixture.MakeController();

	// A Start picked from a room in A, then the campus screen: the point is kept and the dropdown shows it as an extra entry.
	Fixture.Interior->LoadInteriorForBuilding(TEXT("A"));
	Fixture.Interior->SetActiveFloor(1);
	const auto& Rooms = Nav->GetSelectableRooms();
	if (!TestFalse(TEXT("A has rooms"), Rooms.IsEmpty())) return false;
	const FString RoomLabel = Rooms[0].GetDisplayLabel();
	if (!TestTrue(TEXT("Start from a room in A"), Nav->SetStartToRoom(RoomLabel))) return false;

	Controller->RestoreExteriorCameraView();
	TestTrue(TEXT("leaving the interior keeps the Start"), Nav->HasStart());
	Nav->SetOutdoorMode(true);
	TestTrue(TEXT("the Start is still there outside"), Nav->HasStart());
	const FString Shown = Nav->GetStartOtherBuildingLabel();
	TestTrue(TEXT("outside, the room shows as an extra entry naming its building"), Shown.StartsWith(TEXT("Building A · ")) && Shown.Contains(TEXT("(Floor 1)")));
	TestTrue(TEXT("without its id"), !Shown.Contains(TEXT("[")));
	TestTrue(TEXT("the dropdown has no entry of its own for it"), Nav->GetStartRoomLabel().IsEmpty());

	// A main door picked outside, then a building opened: it shows as an extra entry that already names its building.
	Nav->SetOutdoorMode(true);
	const FString DoorLabel = Nav->GetSelectableRooms()[2].GetDisplayLabel();
	TestTrue(TEXT("Destination at C's door"), Nav->SetDestinationToRoom(DoorLabel));
	Nav->SetOutdoorMode(false);
	Fixture.Interior->LoadInteriorForBuilding(TEXT("A"));
	TestEqual(TEXT("inside a building the door is an extra entry, named as it was"), Nav->GetDestinationOtherBuildingLabel(), DoorLabel);
	TestTrue(TEXT("and Clear removes both"), (Nav->RequestClearRoute(), !Nav->HasStart() && !Nav->HasDestination()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationOutdoorRouteViewTest, "InNavi.Navigation.Outdoor.RouteView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationOutdoorRouteViewTest::RunTest(const FString& Parameters)
{
	using namespace NavigationOutdoorTests;
	FNavigationSearchFixture Fixture;
	if (!TestTrue(TEXT("test project loads"), LoadTestProject(Fixture))) return false;
	UNavigationPanelWidget* Nav = Fixture.Nav;
	Nav->SetOutdoorMode(true);
	const TArray<UNavigationPanelWidget::FNavigationRoomOption> Doors = Nav->GetSelectableRooms();
	if (!TestEqual(TEXT("three main doors"), Doors.Num(), 3)) return false;
	TestTrue(TEXT("Start at A's door"), Nav->SetStartToRoom(Doors[0].GetDisplayLabel()));
	TestTrue(TEXT("Destination at C's door"), Nav->SetDestinationToRoom(Doors[2].GetDisplayLabel()));
	Nav->RequestFindRoute();
	if (!TestFalse(TEXT("a route is found"), Nav->GetRoutePoints().IsEmpty())) return false;

	// Drawn on the campus: the outdoor stretches and both markers.
	const FNavigation3DView View = UNavigationRouteVisualization::BuildCampusView(Nav->GetRoutePoints(), &Nav->GetStart(), &Nav->GetDestination());
	TestFalse(TEXT("the route has lines"), View.Segments.IsEmpty());
	TestTrue(TEXT("both markers are there"), View.Start.IsSet() && View.Destination.IsSet());
	TestTrue(TEXT("labelled Start and Destination"), View.Labels.ContainsByPredicate([](const FNavigationDisplayLabel& L) { return L.Text == TEXT("Start"); })
		&& View.Labels.ContainsByPredicate([](const FNavigationDisplayLabel& L) { return L.Text == TEXT("Destination"); }));
	TestTrue(TEXT("nothing is drawn when the route is hidden"), UNavigationRouteVisualization::BuildCampusView(Nav->GetRoutePoints(), &Nav->GetStart(), &Nav->GetDestination(), false).Segments.IsEmpty());

	// A Start inside a building (picked while it was open) is marked at its spot in the building.
	Fixture.Interior->LoadInteriorForBuilding(TEXT("A"));
	Fixture.Interior->SetActiveFloor(2);
	Nav->SetOutdoorMode(false);
	const UNavigationPanelWidget::FNavigationRoomOption* SecondFloorRoom = Nav->GetSelectableRooms().FindByPredicate(
		[](const UNavigationPanelWidget::FNavigationRoomOption& Option) { return Option.Floor == 2; });
	if (!TestNotNull(TEXT("A has a room on floor 2"), SecondFloorRoom)) return false;
	TestTrue(TEXT("Start in a room"), Nav->SetStartToRoom(SecondFloorRoom->GetDisplayLabel()));
	Nav->SetOutdoorMode(true);
	const FNavigation3DView IndoorStart = UNavigationRouteVisualization::BuildCampusView(TArray<FNavigationRoutePoint>(), &Nav->GetStart(), nullptr);
	TestTrue(TEXT("an indoor Start is marked"), IndoorStart.Start.IsSet());
	TestTrue(TEXT("at its height (the second floor is above the ground)"), IndoorStart.Start.IsSet() && IndoorStart.Start->Z > 100.f);
	return true;
}

#endif
