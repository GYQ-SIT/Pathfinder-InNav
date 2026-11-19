#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "InteriorGenerator.h"
#include "PolyBuilding.h"
#include "NavigationRoutePlanner.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"

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

// The graph can be made before anyone asks for a route (FNavigationGraphWarmup, on a worker thread): the first route then finds it ready and builds nothing,
// and a route asked for while it works waits for it instead of making the graph again. The routes are the same as without it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationGraphWarmupTest, "InNavi.Navigation.GraphCache.WarmedBeforeTheFirstRoute", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationGraphWarmupTest::RunTest(const FString& Parameters)
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
	const auto WaitFor = [](const FNavigationGraphWarmup& Warmup)
	{
		for (int32 Waited = 0; Waited < 12000 && !Warmup.IsDone(); ++Waited) FPlatformProcess::Sleep(0.005f);
		return Warmup.IsDone();
	};

	// The route without any warm-up, as the reference.
	FNavigationRoutePlanner::ClearGraphCache();
	FNavigationRouteResult Reference;
	TestTrue(TEXT("a route is found without a warm-up"), FNavigationRoutePlanner::FindCampusRoute(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[1]), Reference));

	// Warmed first: the graph is built by the warm-up, and the first route builds nothing.
	FNavigationRoutePlanner::ClearGraphCache();
	{
		FNavigationGraphWarmup Warmup(*B.Fixture.Generator, *B.Fixture.Interior);
		TestTrue(TEXT("the warm-up finishes"), WaitFor(Warmup));
		TestTrue(TEXT("and kept the graph"), Warmup.WasKept());
	}
	// The warm-up makes the graphs a route can ask for: this building alone (it has no link to another) and the campus.
	const int32 MadeByWarmup = FNavigationRoutePlanner::GetGraphCacheMisses();
	TestTrue(TEXT("the graphs were built by the warm-up"), MadeByWarmup >= 1);
	FNavigationRouteResult Warmed;
	TestTrue(TEXT("the first route is found"), FNavigationRoutePlanner::FindCampusRoute(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[1]), Warmed));
	TestEqual(TEXT("and built no graph"), FNavigationRoutePlanner::GetGraphCacheMisses(), MadeByWarmup);
	TestTrue(TEXT("it found the one the warm-up kept"), FNavigationRoutePlanner::GetGraphCacheHits() >= 1);
	TestTrue(TEXT("and it is the same route as without a warm-up"), SameRoute(Reference, Warmed));

	// A route asked for while the warm-up works waits for it (a little at a time, as the screens run it) and does not join the graph a second time.
	FNavigationRoutePlanner::ClearGraphCache();
	FNavigationGraphWarmup Working(*B.Fixture.Generator, *B.Fixture.Interior);
	FNavigationRouteJob Job(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[1]));
	bool bDone = false;
	for (int32 Slices = 0; Slices < 20000 && !bDone; ++Slices)
	{
		bDone = Job.Step(0.002);
		if (!bDone) FPlatformProcess::Sleep(0.002f);
	}
	TestTrue(TEXT("the route that was asked for meanwhile is found"), bDone && Job.Succeeded());
	TestTrue(TEXT("it is the same route"), SameRoute(Reference, Job.GetResult()));
	TestEqual(TEXT("and the graphs were joined once, not twice"), FNavigationRoutePlanner::GetGraphCacheMisses(), MadeByWarmup);

	// A warm-up that is dropped at once stops, and does not leave a route waiting for it.
	FNavigationRoutePlanner::ClearGraphCache();
	{
		FNavigationGraphWarmup Dropped(*B.Fixture.Generator, *B.Fixture.Interior);
	}
	FNavigationRouteResult After;
	TestTrue(TEXT("a route is found after a warm-up was dropped"), FNavigationRoutePlanner::FindCampusRoute(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[1]), After));
	TestTrue(TEXT("and it is the same route"), SameRoute(Reference, After));
	return true;
}

// The kept graph also lives in files: a graph that is built is written, the next start reads it instead of joining it, a file that is damaged or made for
// other data is ignored, and the route is the same every way.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationGraphFilesTest, "InNavi.Navigation.GraphCache.FilesKeepTheGraph", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationGraphFilesTest::RunTest(const FString& Parameters)
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
	const auto Route = [&](FNavigationRouteResult& Result)
	{
		return FNavigationRoutePlanner::FindCampusRoute(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[1]), Result);
	};
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const auto Files = [&Directory]()
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(Directory / TEXT("*.bin")), true, false);
		return Found;
	};
	ON_SCOPE_EXIT
	{
		FNavigationRoutePlanner::SetGraphFiles(FString(), FString());
		IFileManager::Get().DeleteDirectory(*Directory, false, true);
	};

	// Files are off until they are asked for: nothing is written.
	FNavigationRoutePlanner::SetGraphFiles(FString(), FString());
	FNavigationRoutePlanner::ClearGraphCache();
	FNavigationRouteResult Reference;
	TestTrue(TEXT("a route is found without files"), Route(Reference));
	FNavigationRoutePlanner::WaitForGraphFiles();
	TestTrue(TEXT("with files off nothing is written"), Files().IsEmpty());

	// On: the graph that is built is written.
	FNavigationRoutePlanner::SetGraphFiles(Directory, FString());
	FNavigationRoutePlanner::ClearGraphCache();
	FNavigationRouteResult Built;
	TestTrue(TEXT("a route is found and its graph built"), Route(Built));
	FNavigationRoutePlanner::WaitForGraphFiles();
	TestTrue(TEXT("the graph was written to a file"), !Files().IsEmpty());
	TestTrue(TEXT("and the route is the same as without files"), SameRoute(Reference, Built));

	// A start that has nothing in memory reads the file and builds nothing.
	FNavigationRoutePlanner::ClearGraphCache();
	FNavigationRouteResult FromFile;
	TestTrue(TEXT("a route is found from the file"), Route(FromFile));
	TestEqual(TEXT("it built no graph"), FNavigationRoutePlanner::GetGraphCacheMisses(), 0);
	TestTrue(TEXT("it read the file"), FNavigationRoutePlanner::GetGraphFileReads() >= 1);
	TestTrue(TEXT("and it is the same route"), SameRoute(Reference, FromFile));

	// Files that come with the app are read, and never written to.
	FNavigationRoutePlanner::SetGraphFiles(FString(), Directory);
	FNavigationRoutePlanner::ClearGraphCache();
	FNavigationRouteResult Shipped;
	TestTrue(TEXT("a route is found from a file that came with the app"), Route(Shipped));
	TestTrue(TEXT("it read it"), FNavigationRoutePlanner::GetGraphFileReads() >= 1 && FNavigationRoutePlanner::GetGraphCacheMisses() == 0);
	TestTrue(TEXT("with the same route"), SameRoute(Reference, Shipped));

	// A file that is cut off, or damaged, is ignored: the graph is built, and the route is the same.
	FNavigationRoutePlanner::SetGraphFiles(Directory, FString());
	for (const FString& Name : Files())
	{
		TArray<uint8> Bytes;
		FFileHelper::LoadFileToArray(Bytes, *(Directory / Name));
		Bytes.SetNum(Bytes.Num() / 2);
		FFileHelper::SaveArrayToFile(Bytes, *(Directory / Name));
	}
	FNavigationRoutePlanner::ClearGraphCache();
	FNavigationRouteResult AfterCut;
	TestTrue(TEXT("a route is found when the file is cut off"), Route(AfterCut));
	TestTrue(TEXT("the graph was built, not read"), FNavigationRoutePlanner::GetGraphFileReads() == 0 && FNavigationRoutePlanner::GetGraphCacheMisses() >= 1);
	TestTrue(TEXT("and the route is the same"), SameRoute(Reference, AfterCut));
	FNavigationRoutePlanner::WaitForGraphFiles();

	// Another building is another key: the old file is not found, the graph is built, and a file is written for it.
	FString Error;
	B.Fixture.Interior->SetActiveFloor(B.Rooms.Last().Floor);
	TestTrue(TEXT("a room is taken out of the building"), B.Fixture.Interior->DeleteRoom(B.Rooms.Last().RoomId));
	FNavigationRoutePlanner::ClearGraphCache();
	FNavigationRouteResult Changed;
	TestTrue(TEXT("a route is found in the changed building"), Route(Changed));
	TestTrue(TEXT("its graph was built, not read from the file of the other data"), FNavigationRoutePlanner::GetGraphFileReads() == 0 && FNavigationRoutePlanner::GetGraphCacheMisses() >= 1);
	FNavigationRoutePlanner::WaitForGraphFiles();
	TestTrue(TEXT("and kept in a file of its own"), Files().Num() >= 2);
	return true;
}

// A route job that may use a worker thread (the phone's, which reads its project into a world of its own) joins the graph there, at full speed instead of a
// few milliseconds a frame, and finds the same route.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationRouteJobWorkerTest, "InNavi.Navigation.GraphCache.JoinOnAWorkerThread", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationRouteJobWorkerTest::RunTest(const FString& Parameters)
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
	FNavigationRoutePlanner::SetGraphFiles(FString(), FString());
	FNavigationRoutePlanner::ClearGraphCache();
	FNavigationRouteResult Reference;
	TestTrue(TEXT("a route is found a little at a time, as before"), FNavigationRoutePlanner::FindCampusRoute(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[1]), Reference));

	FNavigationRoutePlanner::ClearGraphCache();
	{
		FNavigationRouteJob Job(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[1]));
		Job.AllowWorkerThread();
		bool bDone = false;
		for (int32 Slices = 0; Slices < 20000 && !bDone; ++Slices)
		{
			bDone = Job.Step(0.002);
			if (!bDone) FPlatformProcess::Sleep(0.002f);
		}
		TestTrue(TEXT("the job that may use a worker finishes"), bDone && Job.Succeeded());
		TestTrue(TEXT("and finds the same route"), SameRoute(Reference, Job.GetResult()));
		TestEqual(TEXT("its graph was joined once"), FNavigationRoutePlanner::GetGraphCacheMisses(), 1);
		const int32 Hits = FNavigationRoutePlanner::GetGraphCacheHits();
		// The next one finds the graph the worker joined, and has no worker to wait for.
		FNavigationRouteJob Second(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[2]));
		Second.AllowWorkerThread();
		bool bSecondDone = false;
		for (int32 Slices = 0; Slices < 20000 && !bSecondDone; ++Slices) bSecondDone = Second.Step(0.002);
		TestTrue(TEXT("the next route is found"), bSecondDone && Second.Succeeded());
		TestTrue(TEXT("with the graph the worker joined"), FNavigationRoutePlanner::GetGraphCacheHits() > Hits && FNavigationRoutePlanner::GetGraphCacheMisses() == 1);
	}

	// A job that is dropped while its worker is busy stops it and does not crash.
	FNavigationRoutePlanner::ClearGraphCache();
	{
		FNavigationRouteJob Dropped(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[1]));
		Dropped.AllowWorkerThread();
		Dropped.Step(0.002);
		Dropped.Step(0.002);
	}
	FNavigationRouteResult After;
	TestTrue(TEXT("a route is found after a job was dropped"), FNavigationRoutePlanner::FindCampusRoute(*B.Fixture.Generator, *B.Fixture.Interior, PointOf(B.Rooms[0]), PointOf(B.Rooms[1]), After));
	TestTrue(TEXT("and it is the same route"), SameRoute(Reference, After));
	return true;
}

#endif
