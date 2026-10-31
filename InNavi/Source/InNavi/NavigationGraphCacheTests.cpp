#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "InteriorGenerator.h"
#include "PolyBuilding.h"
#include "NavigationRoutePlanner.h"
#include "Misc/AutomationTest.h"

// The part of the route graph that does not depend on the route is kept between routes (PAT-142): the next route in the same building joins
// only its start and destination to it. These tests check that it is found when nothing has changed, made again when something has, and that
// a route comes out the same either way.
namespace NavigationGraphCacheTests
{
	// A building of two floors with a generated layout on each, open in the interior generator.
	struct FGeneratedBuilding
	{
		FNavigationSearchFixture Fixture;
		TArray<UNavigationPanelWidget::FNavigationRoomOption> Rooms;
		bool bReady = false;

		FGeneratedBuilding()
		{
			const double Units = 100.0 / Fixture.Generator->GetScale();
			const TArray<FVector2D> Outline = { FVector2D(0, 0), FVector2D(60 * Units, 0), FVector2D(60 * Units, 30 * Units), FVector2D(0, 30 * Units) };
			Fixture.Generator->SpawnBuildingFromData(TEXT("CACHE"), TEXT("Cache test"), Outline, 2, 300.f, FVector2D(9000.0, 9000.0));
			Fixture.Interior->LoadInteriorForBuilding(TEXT("CACHE"));
			if (Fixture.Interior->GetActiveFloor() < 1) Fixture.Interior->AddFloor();
			FString Error;
			Fixture.Interior->SetActiveFloor(1);
			Fixture.Interior->AddEntranceAtPoint(FVector2D(30 * Units, 0.0), 15.f, Error);
			bReady = true;
			for (int32 Level = 1; Level <= 2; ++Level)
			{
				Fixture.Interior->SetActiveFloor(Level);
				FFloorGenerationResult Generated;
				bReady &= Fixture.Interior->GenerateFloor(Level, Generated);
			}
			Fixture.Nav->SetNavigationContext(Fixture.Generator, Fixture.Interior);
			const APolyBuilding* Building = Fixture.Generator->FindBuildingById(TEXT("CACHE"));
			for (const auto& Option : Fixture.Nav->GetSelectableRooms())
			{
				FString Why;
				if (Building && FNavigationRoutePlanner::IsValidPoint(*Building, *Fixture.Interior, Fixture.Generator->GetScale(), Option.Floor, Option.Centroid, Why)) Rooms.Add(Option);
			}
		}

		bool Route(const UNavigationPanelWidget::FNavigationRoomOption& From, const UNavigationPanelWidget::FNavigationRoomOption& To, FNavigationRouteResult& Result) const
		{
			const APolyBuilding* Building = Fixture.Generator->FindBuildingById(TEXT("CACHE"));
			return FNavigationRoutePlanner::FindRoute(*Building, *Fixture.Interior, Fixture.Generator->GetScale(), From.Floor, From.Centroid, To.Floor, To.Centroid, Result);
		}
	};

	bool SameRoute(const FNavigationRouteResult& A, const FNavigationRouteResult& B)
	{
		if (A.Points.Num() != B.Points.Num()) return false;
		for (int32 Index = 0; Index < A.Points.Num(); ++Index)
		{
			if (A.Points[Index].Floor != B.Points[Index].Floor || !A.Points[Index].Position.Equals(B.Points[Index].Position, 0.001)) return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationGraphCacheReuseTest, "InNavi.Navigation.GraphCache.ReusedUntilTheBuildingChanges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationGraphCacheReuseTest::RunTest(const FString& Parameters)
{
	using namespace NavigationGraphCacheTests;
	FGeneratedBuilding B;
	if (!TestTrue(TEXT("a generated building with rooms to route between"), B.bReady && B.Rooms.Num() >= 6)) return false;
	FNavigationRoutePlanner::ClearGraphCache();

	FNavigationRouteResult First, Second, Again;
	TestTrue(TEXT("a route is found"), B.Route(B.Rooms[0], B.Rooms[1], First));
	TestEqual(TEXT("the graph of the building had to be built"), FNavigationRoutePlanner::GetGraphCacheMisses(), 1);
	TestEqual(TEXT("and was not there to be found"), FNavigationRoutePlanner::GetGraphCacheHits(), 0);
	TestTrue(TEXT("a route between other rooms is found"), B.Route(B.Rooms[0], B.Rooms[2], Second));
	TestEqual(TEXT("the second route found the graph"), FNavigationRoutePlanner::GetGraphCacheHits(), 1);
	TestEqual(TEXT("and built nothing"), FNavigationRoutePlanner::GetGraphCacheMisses(), 1);
	TestTrue(TEXT("the first route again is found"), B.Route(B.Rooms[0], B.Rooms[1], Again));
	TestEqual(TEXT("with the graph that was kept"), FNavigationRoutePlanner::GetGraphCacheHits(), 2);
	TestTrue(TEXT("and it is the same route as when the graph was built for it"), SameRoute(First, Again));
	TestFalse(TEXT("(a route has places to go through)"), First.Points.IsEmpty());

	// A change of the building changes what the graph is made from, so it is built again; a route is still found.
	FString Error;
	B.Fixture.Interior->SetActiveFloor(B.Rooms.Last().Floor);
	TestTrue(TEXT("a room is taken out of the building"), B.Fixture.Interior->DeleteRoom(B.Rooms.Last().RoomId));
	FNavigationRouteResult Changed;
	TestTrue(TEXT("a route is found in the changed building"), B.Route(B.Rooms[0], B.Rooms[1], Changed));
	TestEqual(TEXT("the graph was made again for it"), FNavigationRoutePlanner::GetGraphCacheMisses(), 2);
	TestEqual(TEXT("and the kept one was not used"), FNavigationRoutePlanner::GetGraphCacheHits(), 2);
	return true;
}

// The search the Navigation panel runs a little each frame, and the one that works at once, share the kept graph, and preferences for the
// ways between floors are part of what it is made from.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationGraphCacheCampusTest, "InNavi.Navigation.GraphCache.CampusRoutesAndPreferences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationGraphCacheCampusTest::RunTest(const FString& Parameters)
{
	using namespace NavigationGraphCacheTests;
	FGeneratedBuilding B;
	if (!TestTrue(TEXT("a generated building with rooms to route between"), B.bReady && B.Rooms.Num() >= 3)) return false;
	auto PointOf = [](const UNavigationPanelWidget::FNavigationRoomOption& Room)
	{
		FNavigationRoutePoint Point;
		Point.BuildingId = TEXT("CACHE"); Point.Floor = Room.Floor; Point.Position = Room.Centroid;
		return Point;
	};
	FNavigationRoutePlanner::ClearGraphCache();
	FNavigationRouteResult First, Second;
	TestTrue(TEXT("a route is found"), FNavigationRoutePlanner::FindCampusRoute(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[1]), First));
	const int32 MissesAfterFirst = FNavigationRoutePlanner::GetGraphCacheMisses();
	TestTrue(TEXT("its graph was built"), MissesAfterFirst >= 1);
	TestTrue(TEXT("and the same trip again is found"), FNavigationRoutePlanner::FindCampusRoute(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[1]), Second));
	TestEqual(TEXT("without building it again"), FNavigationRoutePlanner::GetGraphCacheMisses(), MissesAfterFirst);
	TestTrue(TEXT("by finding the one that was kept"), FNavigationRoutePlanner::GetGraphCacheHits() >= 1);
	TestTrue(TEXT("with the same route"), SameRoute(First, Second));

	// The same job, a little at a time, as the panel runs it.
	const int32 HitsBeforeJob = FNavigationRoutePlanner::GetGraphCacheHits();
	FNavigationRouteJob Job(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[1]));
	int32 Slices = 0;
	while (!Job.Step(0.0) && Slices < 100000) ++Slices;
	TestTrue(TEXT("the sliced search finds a route"), Job.Succeeded());
	TestTrue(TEXT("with the kept graph"), FNavigationRoutePlanner::GetGraphCacheHits() > HitsBeforeJob);
	TestTrue(TEXT("and the same route"), SameRoute(First, Job.GetResult()));

	// No lifts: different nodes, so a different graph (built, not found).
	FNavigationRoutePreferences NoLifts;
	NoLifts.bAllowLift = false;
	const int32 MissesBefore = FNavigationRoutePlanner::GetGraphCacheMisses();
	FNavigationRouteResult Stairs;
	FNavigationRoutePlanner::FindCampusRoute(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[1]), Stairs, NoLifts);
	TestTrue(TEXT("another preference for the ways between floors makes a graph of its own"), FNavigationRoutePlanner::GetGraphCacheMisses() > MissesBefore);
	return true;
}

// The graph of a campus does not depend on where a leg of a trip starts and ends: a trip that goes out of one building and to another does not
// make a graph for every pair of buildings (it took about ten seconds each in a project with many floors).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationGraphCacheLegsTest, "InNavi.Navigation.GraphCache.LegsOfATripShareTheGraph", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationGraphCacheLegsTest::RunTest(const FString& Parameters)
{
	using namespace NavigationGraphCacheTests;
	FGeneratedBuilding B;
	if (!TestTrue(TEXT("a generated building with rooms to route between"), B.bReady && B.Rooms.Num() >= 3)) return false;
	const double Units = 100.0 / B.Fixture.Generator->GetScale();
	const TArray<FVector2D> Outline = { FVector2D(0, 0), FVector2D(20 * Units, 0), FVector2D(20 * Units, 20 * Units), FVector2D(0, 20 * Units) };
	B.Fixture.Generator->SpawnBuildingFromData(TEXT("OUT1"), TEXT("Outside 1"), Outline, 1, 300.f, FVector2D(30000.0, 9000.0));
	B.Fixture.Generator->SpawnBuildingFromData(TEXT("OUT2"), TEXT("Outside 2"), Outline, 1, 300.f, FVector2D(9000.0, 30000.0));

	auto OutsideOf = [&](const TCHAR* BuildingId)
	{
		FNavigationRoutePoint Point;
		Point.BuildingId = BuildingId; Point.Floor = UNavigationPanelWidget::OutdoorFloor; Point.Position = FVector2D(-10 * Units, -10 * Units);
		return Point;
	};
	FNavigationRoutePoint Room;
	Room.BuildingId = TEXT("CACHE"); Room.Floor = B.Rooms[0].Floor; Room.Position = B.Rooms[0].Centroid;

	FNavigationRoutePlanner::ClearGraphCache();
	FNavigationRouteResult FromFirst, FromSecond;
	FNavigationRoutePlanner::FindCampusRoute(*B.Fixture.Generator, *B.Fixture.Interior, OutsideOf(TEXT("OUT1")), Room, FromFirst);
	const int32 Misses = FNavigationRoutePlanner::GetGraphCacheMisses();
	const int32 Hits = FNavigationRoutePlanner::GetGraphCacheHits();
	TestTrue(TEXT("the first leg built the graph of the campus"), Misses >= 1);
	FNavigationRoutePlanner::FindCampusRoute(*B.Fixture.Generator, *B.Fixture.Interior, OutsideOf(TEXT("OUT2")), Room, FromSecond);
	TestEqual(TEXT("a leg that starts at another building builds nothing"), FNavigationRoutePlanner::GetGraphCacheMisses(), Misses);
	TestTrue(TEXT("it finds the graph that was kept"), FNavigationRoutePlanner::GetGraphCacheHits() > Hits);
	return true;
}

#endif
