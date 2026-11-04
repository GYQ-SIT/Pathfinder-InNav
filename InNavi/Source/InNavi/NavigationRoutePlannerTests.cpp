#if WITH_DEV_AUTOMATION_TESTS

#include "NavigationRoutePlanner.h"
#include "NavigationPanelWidget.h"
#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "PolyBuilding.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace
{
	constexpr double TestGeometryEpsilon = 0.0001;

	// Named distinctly (Test... prefix) rather than the more obvious Cross2D/IsOnSegment -
	// NavigationPanelWidget.cpp already has its own same-named, same-signature pair in its own
	// anonymous namespace. Both being anonymous-namespace/internal-linkage normally keeps them
	// apart, but Unreal's adaptive-unity build only excludes files with local uncommitted changes
	// from unity batching - a from-clean CI checkout has nothing to exclude, so both land in the
	// same merged translation unit and collide (first caught by CI, not any local build, since a
	// local build's own working set is rarely "completely clean").
	double TestCross2D(const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		return static_cast<double>(B.X - A.X) * (C.Y - A.Y)
			- static_cast<double>(B.Y - A.Y) * (C.X - A.X);
	}

	bool TestIsOnSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		return FMath::Abs(TestCross2D(A, B, Point)) <= TestGeometryEpsilon
			&& Point.X >= FMath::Min(A.X, B.X) - TestGeometryEpsilon
			&& Point.X <= FMath::Max(A.X, B.X) + TestGeometryEpsilon
			&& Point.Y >= FMath::Min(A.Y, B.Y) - TestGeometryEpsilon
			&& Point.Y <= FMath::Max(A.Y, B.Y) + TestGeometryEpsilon;
	}

	bool SegmentsIntersect(const FVector2D& A, const FVector2D& B,
		const FVector2D& C, const FVector2D& D)
	{
		const double C1 = TestCross2D(A, B, C);
		const double C2 = TestCross2D(A, B, D);
		const double C3 = TestCross2D(C, D, A);
		const double C4 = TestCross2D(C, D, B);
		if (((C1 > TestGeometryEpsilon && C2 < -TestGeometryEpsilon)
			|| (C1 < -TestGeometryEpsilon && C2 > TestGeometryEpsilon))
			&& ((C3 > TestGeometryEpsilon && C4 < -TestGeometryEpsilon)
				|| (C3 < -TestGeometryEpsilon && C4 > TestGeometryEpsilon)))
		{
			return true;
		}
		return TestIsOnSegment(C, A, B) || TestIsOnSegment(D, A, B)
			|| TestIsOnSegment(A, C, D) || TestIsOnSegment(B, C, D);
	}

	double SegmentDistance(const FVector2D& A, const FVector2D& B,
		const FVector2D& C, const FVector2D& D)
	{
		if (SegmentsIntersect(A, B, C, D))
		{
			return 0.0;
		}
		auto PointDistance = [](const FVector2D& Point, const FVector2D& S0, const FVector2D& S1)
		{
			return FVector2D::Distance(Point, FMath::ClosestPointOnSegment2D(Point, S0, S1));
		};
		return FMath::Min(
			FMath::Min(PointDistance(A, C, D), PointDistance(B, C, D)),
			FMath::Min(PointDistance(C, A, B), PointDistance(D, A, B)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationRoutePlannerIntegrationTest,
	"InNavi.Navigation.RoutePlanner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavigationRoutePlannerIntegrationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ABuildingGenerator* Generator = World->SpawnActor<ABuildingGenerator>();
	AInteriorGenerator* Interior = World->SpawnActor<AInteriorGenerator>();
	Interior->DispatchBeginPlay();

	const FString DataPath = FPaths::ProjectDir() / TEXT("Data") / TEXT("buildings.json");
	TestTrue(TEXT("Example building data loads"), Generator->LoadBuildingsFromFile(DataPath) > 0);
	TestTrue(TEXT("E2 interior loads"), Interior->LoadInteriorForBuilding(TEXT("E2")) >= 0);
	APolyBuilding* Building = Generator->FindBuildingById(TEXT("E2"));
	TestNotNull(TEXT("E2 exists"), Building);
	if (Building)
	{
		FNavigationRouteResult Route;
		TestTrue(TEXT("Unobstructed same-floor route"), FNavigationRoutePlanner::FindRoute(*Building, *Interior,
			Generator->GetScale(), 1, FVector2D(120.f, 70.f), 1, FVector2D(150.f, 70.f), Route));
		TestEqual(TEXT("Unobstructed route is direct"), Route.Points.Num(), 2);

		const FVector2D Elevator(348.4473f, 99.6574f);
		TestTrue(TEXT("Cross-floor route uses the E2 elevator"), FNavigationRoutePlanner::FindRoute(
			*Building, *Interior, Generator->GetScale(), 1, Elevator, 2, Elevator, Route));
		bool bChangedFloor = false;
		for (int32 I = 1; I < Route.Points.Num(); ++I)
		{
			bChangedFloor |= Route.Points[I - 1].Floor != Route.Points[I].Floor;
		}
		TestTrue(TEXT("Cross-floor route contains a floor transition"), bChangedFloor);

		// E2-01-02 is a sealed rectangular room. A straight route from left to right would cross it,
		// so the visibility graph must use the expanded wall boundary and travel around it.
		TestTrue(TEXT("Route around sealed room"), FNavigationRoutePlanner::FindRoute(*Building, *Interior,
			Generator->GetScale(), 1, FVector2D(250.f, 125.f), 1, FVector2D(350.f, 125.f), Route));
		TestTrue(TEXT("Blocked route contains safe turns"), Route.Points.Num() > 2);

		const FVector2D RoomCorners[] =
		{
			FVector2D(274.1677f, 106.1657f), FVector2D(334.1677f, 106.1657f),
			FVector2D(334.1677f, 151.1657f), FVector2D(274.1677f, 151.1657f)
		};
		constexpr double ExpectedClearance = 2.99; // (20 cm / 2 wall + 20 cm gap) / scale 10
		for (int32 RouteIndex = 1; RouteIndex < Route.Points.Num(); ++RouteIndex)
		{
			for (int32 WallIndex = 0; WallIndex < 4; ++WallIndex)
			{
				const double Distance = SegmentDistance(Route.Points[RouteIndex - 1].Position,
					Route.Points[RouteIndex].Position, RoomCorners[WallIndex], RoomCorners[(WallIndex + 1) % 4]);
				TestTrue(FString::Printf(TEXT("Route segment %d clears sealed-room wall %d"),
					RouteIndex - 1, WallIndex), Distance >= ExpectedClearance);
			}
		}

		FString ValidationError;
		TestFalse(TEXT("Point inside expanded wall boundary is rejected"),
			FNavigationRoutePlanner::IsValidPoint(*Building, *Interior, Generator->GetScale(),
				1, FVector2D(272.f, 125.f), ValidationError));
		TestTrue(TEXT("Wall-boundary validation explains the problem"),
			ValidationError.Contains(TEXT("safety boundary")));

		// E2-01-01 has a 90 cm door in its right wall. Inflating solid wall pieces must leave the
		// centre of that opening traversable.
		TestTrue(TEXT("Room exit still passes through its door"), FNavigationRoutePlanner::FindRoute(
			*Building, *Interior, Generator->GetScale(), 1, FVector2D(200.f, 110.f),
			1, FVector2D(250.f, 110.f), Route));
		// The route may cross anywhere in the opening with sufficient clearance; it need not
		// include the exact midpoint as a vertex when a shorter valid segment exists.
		bool bUsesDoor = false;
		for (int32 I = 1; I < Route.Points.Num(); ++I)
		{
			bUsesDoor |= Route.Points[I - 1].Floor == 1 && Route.Points[I].Floor == 1
				&& SegmentsIntersect(Route.Points[I - 1].Position, Route.Points[I].Position,
					FVector2D(231.8666, 107.3725), FVector2D(231.8666, 116.3725));
		}
		TestTrue(TEXT("Door route uses the authored opening"), bUsesDoor);
		// The legacy outside exit must work as loaded, without reauthoring its destination.
		// Verify outdoor-point validation and that the indoor/outdoor route crosses through that exit.
		const int32 OutsideFloor = UNavigationPanelWidget::OutdoorFloor;
		const FVector2D OutsidePoint(80.f, 100.f);

		TestTrue(TEXT("Point in the outdoor selection area is valid"),
			FNavigationRoutePlanner::IsValidPoint(*Building, *Interior, Generator->GetScale(),
				OutsideFloor, OutsidePoint, ValidationError));

		TestFalse(TEXT("Point inside the building is not valid as an outdoor point"),
			FNavigationRoutePlanner::IsValidPoint(*Building, *Interior, Generator->GetScale(),
				OutsideFloor, FVector2D(120.f, 70.f), ValidationError));

		TestTrue(TEXT("Invalid outdoor point explains the problem"),
			ValidationError.Contains(TEXT("outside area")));

		TestTrue(TEXT("Indoor-to-outdoor route uses the marked exterior exit"),
			FNavigationRoutePlanner::FindRoute(*Building, *Interior, Generator->GetScale(),
				1, FVector2D(120.f, 70.f),
				OutsideFloor, OutsidePoint, Route));
		const bool bCrossesExteriorExit = Route.Points.ContainsByPredicate(
			[OutsideFloor](const FNavigationRoutePoint& Point)
			{
				return Point.Floor == OutsideFloor
					&& Point.ConnectorId.StartsWith(TEXT("OutsideExit:"));
			});
		TestTrue(TEXT("Outdoor route contains the authored exit connector"), bCrossesExteriorExit);

		// Two doors facing the same corridor must be able to exit normally and turn beside the
		// rooms, instead of making a long detour to an unrelated building corner.
		Interior->SetActiveFloor(3);
		FString CreationError;
		const float Scale = Generator->GetScale();
		TArray<AInteriorRoom*> TestRooms;
		for (int32 RoomIndex = 0; RoomIndex < 2; ++RoomIndex)
		{
			const double X = 200.0 + RoomIndex * 100.0;
			AInteriorRoom* Room = Interior->CreateRoomFromFootprint(
				{ FVector2D(X, 80), FVector2D(X + 20, 80),
					FVector2D(X + 20, 100), FVector2D(X, 100) },
				FString::Printf(TEXT("DoorApproachTest%d"), RoomIndex), TEXT("Test room"),
				true, false, false, CreationError);
			TestNotNull(TEXT("Door approach test room created"), Room);
			if (Room)
			{
				FInteriorOpening Opening;
				Opening.Id = TEXT("TestDoor");
				Opening.Type = EInteriorOpeningType::Door;
				Opening.EdgeIndex = 2;
				Opening.Offset = 5.5f * Scale;
				Opening.Width = 9.f * Scale;
				Room->AddOpening(Opening);
				TestRooms.Add(Room);
			}
		}
		if (TestRooms.Num() == 2)
		{
			TestTrue(TEXT("Rooms with parallel doorways connect"),
				FNavigationRoutePlanner::FindRoute(*Building, *Interior, Scale,
					3, FVector2D(210, 90), 3, FVector2D(310, 90), Route));
			double RouteLength = 0.0;
			for (int32 I = 1; I < Route.Points.Num(); ++I)
			{
				RouteLength += FVector2D::Distance(Route.Points[I - 1].Position, Route.Points[I].Position);
				TestEqual(TEXT("Parallel doorway route stays on its floor"), Route.Points[I].Floor, 3);
				for (const AInteriorRoom* Room : TestRooms)
				{
					const TArray<FVector2D>& Corners = Room->GetFootprint();
					for (int32 Edge = 0; Edge < 4; ++Edge)
					{
						const FVector2D A = Corners[Edge];
						const FVector2D B = Corners[(Edge + 1) % 4];
						const FVector2D Direction = (B - A).GetSafeNormal();
						TArray<TPair<FVector2D, FVector2D>> SolidPieces;
						if (Edge == 2)
						{
							SolidPieces.Add({ A, A + Direction * 5.5 });
							SolidPieces.Add({ A + Direction * 14.5, B });
						}
						else
						{
							SolidPieces.Add({ A, B });
						}
						for (const auto& Piece : SolidPieces)
						{
							TestTrue(TEXT("Door approach route preserves clearance from solid walls"),
								SegmentDistance(Route.Points[I - 1].Position, Route.Points[I].Position,
									Piece.Key, Piece.Value) >= ExpectedClearance);
						}
					}
				}
			}
			TestTrue(TEXT("Room-to-room route stays near the doors rather than the building boundary"),
				RouteLength > 100.0 && RouteLength < 145.0);
			bool bCrossesOpenCorridor = false;
			for (int32 I = 1; I < Route.Points.Num(); ++I)
			{
				const FVector2D A = Route.Points[I - 1].Position;
				const FVector2D B = Route.Points[I].Position;
				if (A.X <= 260.0 && B.X >= 260.0 && B.X - A.X > TestGeometryEpsilon)
				{
					const FVector2D Crossing = FMath::Lerp(A, B, (260.0 - A.X) / (B.X - A.X));
					bCrossesOpenCorridor = true;
					TestTrue(TEXT("Open corridor route favours the comfortable wall gap"), Crossing.Y >= 106.9);
				}
			}
			TestTrue(TEXT("Room route crosses the open corridor"), bCrossesOpenCorridor);
		}
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationRestrictedRoomTest, "InNavi.Navigation.RestrictedRooms", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationRestrictedRoomTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto* Generator = World->SpawnActor<ABuildingGenerator>();
	auto* Interior = World->SpawnActor<AInteriorGenerator>();
	Interior->DispatchBeginPlay();
	auto* Building = Generator->SpawnBuildingFromData(TEXT("A"), TEXT("Building A"),
		{{0, 0}, {1600, 0}, {1600, 1000}, {0, 1000}}, 1, 300.f, FVector2D::ZeroVector);
	if (!TestNotNull(TEXT("the test building exists"), Building))
	{
		World->DestroyWorld(false);
		return false;
	}
	Interior->LoadInteriorForBuilding(TEXT("A"));
	const TArray<FVector2D> Footprint{{500, 300}, {1000, 300}, {1000, 700}, {500, 700}};
	FString Error;
	AInteriorRoom* Room = Interior->CreateRoomFromFootprint(Footprint, TEXT("R"), TEXT("Room"), true, false, false, Error);
	if (!TestNotNull(TEXT("the test room exists"), Room))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestFalse(TEXT("the right door exists"), Interior->AddDoorToRoom(TEXT("R"), 1, 155.f).IsEmpty());
	TestFalse(TEXT("the left door exists"), Interior->AddDoorToRoom(TEXT("R"), 3, 155.f).IsEmpty());

	FNavigationRoutePoint Start;
	Start.BuildingId = TEXT("A"); Start.Floor = 1; Start.Position = {250, 500};
	FNavigationRoutePoint End = Start;
	End.Position = {1250, 500};
	FNavigationRoutePoint Inside = Start;
	Inside.Position = {750, 500};
	FNavigationRouteResult Route;
	TestTrue(TEXT("a public room with the same geometry is reachable"), FNavigationRoutePlanner::FindCampusRoute(*Generator, *Interior, Start, Inside, Route));

	auto CheckRestrictedRoutes = [&]()
	{
		TestFalse(TEXT("a restricted room cannot be the destination"), FNavigationRoutePlanner::FindCampusRoute(*Generator, *Interior, Start, Inside, Route));
		TestFalse(TEXT("a restricted room cannot be the start"), FNavigationRoutePlanner::FindCampusRoute(*Generator, *Interior, Inside, End, Route));
		if (TestTrue(TEXT("a public route can detour around the room"), FNavigationRoutePlanner::FindCampusRoute(*Generator, *Interior, Start, End, Route)))
		{
			for (int32 I = 1; I < Route.Points.Num(); ++I)
			{
				for (int32 Edge = 0; Edge < Footprint.Num(); ++Edge)
				{
					TestTrue(TEXT("the route clears each restricted wall, including its doors"),
						SegmentDistance(Route.Points[I - 1].Position, Route.Points[I].Position,
							Footprint[Edge], Footprint[(Edge + 1) % Footprint.Num()]) >= 29.99);
				}
			}
		}
	};
	for (const bool bPrivate : {true, false})
	{
		Room = Interior->GetRoomById(TEXT("R"));
		if (!TestNotNull(TEXT("the room is loaded for this access case"), Room)) break;
		Room->bIsPublic = !bPrivate;
		Room->bRequiresKey = !bPrivate;
		TestFalse(TEXT("point picking rejects a restricted room"), FNavigationRoutePlanner::IsValidPoint(*Building, *Interior, 1.f, 1, Inside.Position, Error));
		CheckRestrictedRoutes();
		Interior->SyncInteriorToBuildingGenerator();
		Interior->ClearLoadedInterior();
		CheckRestrictedRoutes();
		Interior->LoadInteriorForBuilding(TEXT("A"));
	}
	World->DestroyWorld(false);
	return true;
}

#endif
