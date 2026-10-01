#if WITH_DEV_AUTOMATION_TESTS

#include "NavigationRoutePlanner.h"
#include "NavigationPanelWidget.h"
#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "PolyBuilding.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace
{
	constexpr double TestGeometryEpsilon = 0.0001;

	double Cross2D(const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		return static_cast<double>(B.X - A.X) * (C.Y - A.Y)
			- static_cast<double>(B.Y - A.Y) * (C.X - A.X);
	}

	bool IsOnSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		return FMath::Abs(Cross2D(A, B, Point)) <= TestGeometryEpsilon
			&& Point.X >= FMath::Min(A.X, B.X) - TestGeometryEpsilon
			&& Point.X <= FMath::Max(A.X, B.X) + TestGeometryEpsilon
			&& Point.Y >= FMath::Min(A.Y, B.Y) - TestGeometryEpsilon
			&& Point.Y <= FMath::Max(A.Y, B.Y) + TestGeometryEpsilon;
	}

	bool SegmentsIntersect(const FVector2D& A, const FVector2D& B,
		const FVector2D& C, const FVector2D& D)
	{
		const double C1 = Cross2D(A, B, C);
		const double C2 = Cross2D(A, B, D);
		const double C3 = Cross2D(C, D, A);
		const double C4 = Cross2D(C, D, B);
		if (((C1 > TestGeometryEpsilon && C2 < -TestGeometryEpsilon)
			|| (C1 < -TestGeometryEpsilon && C2 > TestGeometryEpsilon))
			&& ((C3 > TestGeometryEpsilon && C4 < -TestGeometryEpsilon)
				|| (C3 < -TestGeometryEpsilon && C4 > TestGeometryEpsilon)))
		{
			return true;
		}
		return IsOnSegment(C, A, B) || IsOnSegment(D, A, B)
			|| IsOnSegment(A, C, D) || IsOnSegment(B, C, D);
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
		const FVector2D DoorMidpoint(231.8666f, 111.8725f);
		const bool bUsesDoor = Route.Points.ContainsByPredicate([&DoorMidpoint](const FNavigationRoutePoint& Point)
			{ return Point.Floor == 1 && Point.Position.Equals(DoorMidpoint, 0.02f); });
		TestTrue(TEXT("Door route uses the authored opening"), bUsesDoor);
		// E2's sample data has no exterior exit marked yet.
        // These checks verify outdoor-point validation and that a route is not invented without an exit.
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

		TestFalse(TEXT("Indoor-to-outdoor route requires a marked exterior exit"),
			FNavigationRoutePlanner::FindRoute(*Building, *Interior, Generator->GetScale(),
				1, FVector2D(120.f, 70.f),
				OutsideFloor, OutsidePoint, Route));
	}
	World->DestroyWorld(false);
	return true;
}

#endif
