#include "NavigationRoutePlanner.h"
#include "NavigationPanelWidget.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "PolyBuilding.h"

namespace
{
	constexpr double GeometryEpsilon = 0.0001;
	// Extra free space between the physical wall surface and the centre of the route. Unreal world
	// units are centimetres; the wall's own half thickness is added separately per floor.
	constexpr double RouteWallGapWorldUnits = 20.0;
	// Prefer comfortable spacing in open areas without making narrow doors impassable.
	constexpr double PreferredWallGapWorldUnits = 60.0;
	constexpr double WallProximityCostWeight = 4.0;
	constexpr double TurnPointMargin = 1.05;
	// A turn point at the minimum clearance can itself be covered by a nearby wall. Walk out along
	// the same ray in small steps and keep the first clear point, rather than dropping that direction
	// from the visibility graph and forcing Dijkstra to use an unrelated distant corner/connector.
	constexpr double TurnPointSearchStep = 0.5;
	constexpr int32 TurnPointSearchAttempts = 16;

	struct FSegment
	{
		FVector2D A;
		FVector2D B;
	};

	struct FExteriorExit
	{
		FString Id;
		FVector2D Position = FVector2D::ZeroVector;
	};

	struct FFloorGeometry
	{
		TArray<FSegment> Walls;
		TArray<FVector2D> DoorPoints;
		TArray<FVector2D> TurnPoints;
		TArray<FExteriorExit> ExteriorExits;
		double WallClearance = 0.0;
		double PreferredWallClearance = 0.0;
	};

	struct FVertex
	{
		FNavigationRoutePoint Point;
		TArray<int32> ConnectsFloors;
	};

	struct FRouteGraphEdge
	{
		int32 To = INDEX_NONE;
		float Cost = 0.f;
	};

	double Cross(const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		return static_cast<double>(B.X - A.X) * (C.Y - A.Y)
			- static_cast<double>(B.Y - A.Y) * (C.X - A.X);
	}

	bool OnSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		return FMath::Abs(Cross(A, B, P)) <= GeometryEpsilon
			&& P.X >= FMath::Min(A.X, B.X) - GeometryEpsilon
			&& P.X <= FMath::Max(A.X, B.X) + GeometryEpsilon
			&& P.Y >= FMath::Min(A.Y, B.Y) - GeometryEpsilon
			&& P.Y <= FMath::Max(A.Y, B.Y) + GeometryEpsilon;
	}

	bool InsideOrOnBoundary(const FVector2D& Point, const TArray<FVector2D>& Polygon)
	{
		if (Polygon.Num() < 3)
		{
			return false;
		}

		bool bInside = false;
		for (int32 I = 0, J = Polygon.Num() - 1; I < Polygon.Num(); J = I++)
		{
			const FVector2D& A = Polygon[J];
			const FVector2D& B = Polygon[I];
			if (OnSegment(Point, A, B))
			{
				return true;
			}
			if ((A.Y > Point.Y) != (B.Y > Point.Y)
				&& Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	bool SegmentsIntersectOrTouch(const FVector2D& A, const FVector2D& B,
		const FVector2D& C, const FVector2D& D)
	{
		const double C1 = Cross(A, B, C);
		const double C2 = Cross(A, B, D);
		const double C3 = Cross(C, D, A);
		const double C4 = Cross(C, D, B);
		if ((C1 > GeometryEpsilon && C2 < -GeometryEpsilon
			|| C1 < -GeometryEpsilon && C2 > GeometryEpsilon)
			&& (C3 > GeometryEpsilon && C4 < -GeometryEpsilon
				|| C3 < -GeometryEpsilon && C4 > GeometryEpsilon))
		{
			return true;
		}

		return OnSegment(C, A, B) || OnSegment(D, A, B)
			|| OnSegment(A, C, D) || OnSegment(B, C, D);
	}

	double PointToSegmentDistance(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		return FVector2D::Distance(Point, FMath::ClosestPointOnSegment2D(Point, A, B));
	}

	int32 FindNearestBoundaryEdge(
		const FVector2D& Point,
		const TArray<FVector2D>& Boundary,
		double& OutDistance)
	{
		OutDistance = TNumericLimits<double>::Max();

		if (Boundary.Num() < 2)
		{
			return INDEX_NONE;
		}

		int32 NearestEdge = INDEX_NONE;

		for (int32 I = 0; I < Boundary.Num(); ++I)
		{
			const double Distance = PointToSegmentDistance(
				Point,
				Boundary[I],
				Boundary[(I + 1) % Boundary.Num()]);

			if (Distance < OutDistance)
			{
				OutDistance = Distance;
				NearestEdge = I;
			}
		}

		return NearestEdge;
	}

	bool IsExteriorExitVertex(const FNavigationRoutePoint& Point)
	{
		return Point.ConnectorId.StartsWith(TEXT("OutsideExit:"));
	}

	bool CanTravelOutside(
		const FVector2D& A,
		const FVector2D& B,
		const TArray<FVector2D>& BuildingBoundary,
		bool bAIsExit,
		bool bBIsExit)
	{
		for (int32 I = 0; I < BuildingBoundary.Num(); ++I)
		{
			const FVector2D& EdgeA = BuildingBoundary[I];
			const FVector2D& EdgeB =
				BuildingBoundary[(I + 1) % BuildingBoundary.Num()];

			if (!SegmentsIntersectOrTouch(A, B, EdgeA, EdgeB))
			{
				continue;
			}

			const bool bTouchesAtAllowedExit =
				(bAIsExit && PointToSegmentDistance(A, EdgeA, EdgeB) <= GeometryEpsilon)
				|| (bBIsExit && PointToSegmentDistance(B, EdgeA, EdgeB) <= GeometryEpsilon);

			if (!bTouchesAtAllowedExit)
			{
				return false;
			}
		}

		return true;
	}

	double SegmentToSegmentDistance(const FVector2D& A, const FVector2D& B,
		const FVector2D& C, const FVector2D& D)
	{
		if (SegmentsIntersectOrTouch(A, B, C, D))
		{
			return 0.0;
		}
		return FMath::Min(
			FMath::Min(PointToSegmentDistance(A, C, D), PointToSegmentDistance(B, C, D)),
			FMath::Min(PointToSegmentDistance(C, A, B), PointToSegmentDistance(D, A, B)));
	}

	void AddWall(FFloorGeometry& Geometry, FVector2D A, FVector2D B)
	{
		if (!A.Equals(B, GeometryEpsilon))
		{
			Geometry.Walls.Add({ A, B });
		}
	}

	bool IsPointClearOfWalls(const FVector2D& Point, const TArray<FVector2D>& BuildingBoundary,
		const FFloorGeometry& Geometry)
	{
		if (!InsideOrOnBoundary(Point, BuildingBoundary))
		{
			return false;
		}
		const double MinimumDistance = Geometry.WallClearance - GeometryEpsilon;
		for (int32 I = 0; I < BuildingBoundary.Num(); ++I)
		{
			if (PointToSegmentDistance(Point, BuildingBoundary[I],
				BuildingBoundary[(I + 1) % BuildingBoundary.Num()]) < MinimumDistance)
			{
				return false;
			}
		}
		for (const FSegment& Wall : Geometry.Walls)
		{
			if (PointToSegmentDistance(Point, Wall.A, Wall.B) < MinimumDistance)
			{
				return false;
			}
		}
		return true;
	}

	bool TryAddTurnPoint(FFloorGeometry& Geometry, const TArray<FVector2D>& BuildingBoundary,
		const FVector2D& Candidate)
	{
		if (!IsPointClearOfWalls(Candidate, BuildingBoundary, Geometry))
		{
			return false;
		}
		if (!Geometry.TurnPoints.ContainsByPredicate([&Candidate](const FVector2D& Existing)
			{ return Existing.Equals(Candidate, 0.01f); }))
		{
			Geometry.TurnPoints.Add(Candidate);
		}
		return true;
	}

	void AddNearestTurnPointAlongRay(FFloorGeometry& Geometry,
		const TArray<FVector2D>& BuildingBoundary, const FVector2D& Origin,
		const FVector2D& Direction, double MinimumOffset)
	{
		if (Direction.IsNearlyZero() || MinimumOffset <= GeometryEpsilon)
		{
			return;
		}

		const double SearchStep = Geometry.WallClearance * TurnPointSearchStep;
		for (int32 Attempt = 0; Attempt < TurnPointSearchAttempts; ++Attempt)
		{
			const double Offset = MinimumOffset + SearchStep * Attempt;
			if (TryAddTurnPoint(Geometry, BuildingBoundary, Origin + Direction * Offset))
			{
				// The first valid point is the nearest one on this ray. Adding more distant points would
				// create unnecessary graph choices and can make a route appear to wander away.
				return;
			}
		}
	}

	void AddCornerTurnPoints(FFloorGeometry& Geometry, const TArray<FVector2D>& BuildingBoundary,
		const FVector2D& Previous, const FVector2D& Corner, const FVector2D& Next)
	{
		const FVector2D ToPrevious = (Previous - Corner).GetSafeNormal();
		const FVector2D ToNext = (Next - Corner).GetSafeNormal();
		const FVector2D Bisector = (ToPrevious + ToNext).GetSafeNormal();
		if (Bisector.IsNearlyZero())
		{
			return;
		}

		const double PerpendicularFactor = FMath::Min(
			FMath::Abs(Cross(FVector2D::ZeroVector, Bisector, ToPrevious)),
			FMath::Abs(Cross(FVector2D::ZeroVector, Bisector, ToNext)));
		if (PerpendicularFactor <= GeometryEpsilon)
		{
			return;
		}

		// Offset far enough along the angle bisector that the point is beyond both wall capsules.
		// Trying both directions supports travel inside and outside a closed room; the building and
		// other wall boundaries filter whichever candidate is not actually usable.
		const double Offset = Geometry.WallClearance * TurnPointMargin / PerpendicularFactor;
		AddNearestTurnPointAlongRay(Geometry, BuildingBoundary, Corner, Bisector, Offset);
		AddNearestTurnPointAlongRay(Geometry, BuildingBoundary, Corner, -Bisector, Offset);
		const double PreferredOffset = Geometry.PreferredWallClearance * TurnPointMargin / PerpendicularFactor;
		AddNearestTurnPointAlongRay(Geometry, BuildingBoundary, Corner, Bisector, PreferredOffset);
		AddNearestTurnPointAlongRay(Geometry, BuildingBoundary, Corner, -Bisector, PreferredOffset);
	}

	void AddOpenEndTurnPoints(FFloorGeometry& Geometry, const TArray<FVector2D>& BuildingBoundary,
		const FVector2D& End, const FVector2D& Neighbour)
	{
		const FVector2D Away = (End - Neighbour).GetSafeNormal();
		if (Away.IsNearlyZero())
		{
			return;
		}
		const FVector2D Perpendicular(-Away.Y, Away.X);
		const double Radius = Geometry.WallClearance * TurnPointMargin;
		for (int32 Step = -4; Step <= 4; ++Step)
		{
			const double Angle = Step * PI / 8.0;
			const FVector2D Direction = Away * FMath::Cos(Angle) + Perpendicular * FMath::Sin(Angle);
			AddNearestTurnPointAlongRay(Geometry, BuildingBoundary, End, Direction, Radius);
			AddNearestTurnPointAlongRay(Geometry, BuildingBoundary, End, Direction,
				Geometry.PreferredWallClearance * TurnPointMargin);
		}
	}

	FFloorGeometry MakeFloorGeometry(const AInteriorGenerator& Interior, float Scale, int32 Floor,
		const TArray<FVector2D>& BuildingBoundary)
	{
		FFloorGeometry Geometry;
		TArray<FSegment> DoorApproaches;
		Geometry.WallClearance =
			(Interior.GetWallThicknessForFloor(Floor) * 0.5 + RouteWallGapWorldUnits)
			/ FMath::Max(static_cast<double>(Scale), static_cast<double>(KINDA_SMALL_NUMBER));
		Geometry.PreferredWallClearance =
			(Interior.GetWallThicknessForFloor(Floor) * 0.5 + PreferredWallGapWorldUnits)
			/ FMath::Max(static_cast<double>(Scale), static_cast<double>(KINDA_SMALL_NUMBER));

		const TArray<AInteriorRoom*>& Rooms = Interior.GetRoomsOnFloor(Floor);
		for (const AInteriorRoom* Room : Rooms)
		{
			if (!Room)
			{
				continue;
			}
			const TArray<FVector2D>& Footprint = Room->GetFootprint();
			const int32 NumEdges = Room->IsClosedLoop() ? Footprint.Num() : Footprint.Num() - 1;
			for (int32 EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				const FVector2D A = Footprint[EdgeIndex];
				const FVector2D B = Footprint[(EdgeIndex + 1) % Footprint.Num()];
				const FVector2D Direction = (B - A).GetSafeNormal();
				const double Length = FVector2D::Distance(A, B);
				if (Length <= GeometryEpsilon)
				{
					continue;
				}

				TArray<TPair<double, double>> Gaps;
				for (const FInteriorOpening& Opening : Room->GetOpenings())
				{
					if (Opening.EdgeIndex != EdgeIndex
						|| (Opening.Type != EInteriorOpeningType::Door && Opening.Type != EInteriorOpeningType::Void))
					{
						continue;
					}
					const double Begin = FMath::Clamp(static_cast<double>(Opening.Offset) / Scale, 0.0, Length);
					const double End = FMath::Clamp(static_cast<double>(Opening.Offset + Opening.Width) / Scale, Begin, Length);
					if (End > Begin + GeometryEpsilon)
					{
						Gaps.Add(TPair<double, double>(Begin, End));
						const FVector2D DoorPosition = A + Direction * ((Begin + End) * 0.5);
						Geometry.DoorPoints.Add(DoorPosition);
						// Leaving a doorway sideways can intersect the clearance capsules of its jambs.
						// Supply a point on each side where the route can turn after crossing normally.
						const FVector2D Normal(-Direction.Y, Direction.X);
						const FVector2D ApproachOffset = Normal * Geometry.WallClearance * TurnPointMargin;
						DoorApproaches.Add({ DoorPosition - ApproachOffset, DoorPosition + ApproachOffset });
						const FVector2D PreferredOffset = Normal * Geometry.PreferredWallClearance * TurnPointMargin;
						DoorApproaches.Add({ DoorPosition - PreferredOffset, DoorPosition + PreferredOffset });

						if (Floor == 1
							&& Opening.Type == EInteriorOpeningType::Door
							&& Opening.ConnectsTo.Equals(TEXT("outside"), ESearchCase::IgnoreCase))
						{
							FExteriorExit Exit;
							Exit.Id = Room->RoomId + TEXT(":") + Opening.Id;
							Exit.Position = DoorPosition;
							Geometry.ExteriorExits.Add(Exit);
						}
					}
				}
				Gaps.Sort([](const TPair<double, double>& Left, const TPair<double, double>& Right)
					{ return Left.Key < Right.Key; });

				double Cursor = 0.0;
				for (const TPair<double, double>& Gap : Gaps)
				{
					if (Gap.Key > Cursor + GeometryEpsilon)
					{
						AddWall(Geometry, A + Direction * Cursor, A + Direction * Gap.Key);
					}
					Cursor = FMath::Max(Cursor, Gap.Value);
				}
				if (Cursor < Length - GeometryEpsilon)
				{
					AddWall(Geometry, A + Direction * Cursor, B);
				}
			}
		}

		// Validate approaches against every room, so a nearby wall cannot be bypassed by an
		// unchecked doorway vertex. All graph edges still pass the full wall-clearance test.
		for (const FSegment& Approach : DoorApproaches)
		{
			TryAddTurnPoint(Geometry, BuildingBoundary, Approach.A);
			TryAddTurnPoint(Geometry, BuildingBoundary, Approach.B);
		}

		// Generate graph vertices on the clearance boundary, after every wall has been collected so
		// candidates can be rejected against neighbouring rooms as well as their own wall.
		for (const AInteriorRoom* Room : Rooms)
		{
			if (!Room)
			{
				continue;
			}
			const TArray<FVector2D>& Footprint = Room->GetFootprint();
			if (Room->IsClosedLoop())
			{
				for (int32 I = 0; I < Footprint.Num(); ++I)
				{
					AddCornerTurnPoints(Geometry, BuildingBoundary,
						Footprint[(I + Footprint.Num() - 1) % Footprint.Num()], Footprint[I],
						Footprint[(I + 1) % Footprint.Num()]);
				}
			}
			else if (Footprint.Num() >= 2)
			{
				AddOpenEndTurnPoints(Geometry, BuildingBoundary, Footprint[0], Footprint[1]);
				for (int32 I = 1; I + 1 < Footprint.Num(); ++I)
				{
					AddCornerTurnPoints(Geometry, BuildingBoundary, Footprint[I - 1],
						Footprint[I], Footprint[I + 1]);
				}
				AddOpenEndTurnPoints(Geometry, BuildingBoundary, Footprint.Last(),
					Footprint[Footprint.Num() - 2]);
			}
		}
		for (int32 I = 0; I < BuildingBoundary.Num(); ++I)
		{
			AddCornerTurnPoints(Geometry, BuildingBoundary,
				BuildingBoundary[(I + BuildingBoundary.Num() - 1) % BuildingBoundary.Num()],
				BuildingBoundary[I], BuildingBoundary[(I + 1) % BuildingBoundary.Num()]);
		}
		return Geometry;
	}

	bool CanTravel(const FVector2D& A, const FVector2D& B,
		const TArray<FVector2D>& BuildingBoundary, const FFloorGeometry& Geometry,
		const TArray<int32>& AllowedBoundaryEdges)
	{
		for (double T : { 0.25, 0.5, 0.75 })
		{
			if (!InsideOrOnBoundary(FMath::Lerp(A, B, T), BuildingBoundary))
			{
				return false;
			}
		}

		const double MinimumDistance = Geometry.WallClearance - GeometryEpsilon;

		for (int32 I = 0; I < BuildingBoundary.Num(); ++I)
		{
			if (AllowedBoundaryEdges.Contains(I))
			{
				continue;
			}

			if (SegmentToSegmentDistance(A, B, BuildingBoundary[I],
				BuildingBoundary[(I + 1) % BuildingBoundary.Num()]) < MinimumDistance)
			{
				return false;
			}
		}

		for (const FSegment& Wall : Geometry.Walls)
		{
			if (SegmentToSegmentDistance(A, B, Wall.A, Wall.B) < MinimumDistance)
			{
				return false;
			}
		}

		return true;
	}

	float IndoorTravelCost(const FVector2D& A, const FVector2D& B,
		const TArray<FVector2D>& Boundary, const FFloorGeometry& Geometry)
	{
		const double Length = FVector2D::Distance(A, B);
		const int32 Samples = FMath::Clamp(FMath::CeilToInt(
			Length / (Geometry.PreferredWallClearance * 0.5)), 1, 256);
		double Penalty = 0.0;
		for (int32 I = 0; I < Samples; ++I)
		{
			const FVector2D Point = FMath::Lerp(A, B, (I + 0.5) / Samples);
			double Distance = Geometry.PreferredWallClearance;
			for (int32 Edge = 0; Edge < Boundary.Num(); ++Edge)
			{
				Distance = FMath::Min(Distance, PointToSegmentDistance(Point,
					Boundary[Edge], Boundary[(Edge + 1) % Boundary.Num()]));
			}
			for (const FSegment& Wall : Geometry.Walls)
			{
				Distance = FMath::Min(Distance, PointToSegmentDistance(Point, Wall.A, Wall.B));
			}
			const double Deficit = 1.0 - Distance / Geometry.PreferredWallClearance;
			Penalty += Deficit * Deficit;
		}
		// Charge for sustained travel beside walls, while allowing short doorway crossings.
		return static_cast<float>(Length * (1.0 + WallProximityCostWeight * Penalty / Samples));
	}

	bool CanUseConnectorBetween(const FVertex& A, const FVertex& B)
	{

		const bool bOnePointIsOutside =
			A.Point.Floor == UNavigationPanelWidget::OutdoorFloor
			|| B.Point.Floor == UNavigationPanelWidget::OutdoorFloor;

		if (bOnePointIsOutside)
		{
			const bool bCorrectFloorPair =
				(A.Point.Floor == UNavigationPanelWidget::OutdoorFloor && B.Point.Floor == 1)
				|| (B.Point.Floor == UNavigationPanelWidget::OutdoorFloor && A.Point.Floor == 1);

			return bCorrectFloorPair
				&& IsExteriorExitVertex(A.Point)
				&& IsExteriorExitVertex(B.Point)
				&& A.Point.ConnectorId == B.Point.ConnectorId;
		}

		// Both points must be connector points, not normal path points.
		if (A.Point.ConnectorType == EInteriorNavNodeType::PathNode ||
			B.Point.ConnectorType == EInteriorNavNodeType::PathNode)
		{
			return false;
		}

		// The two points must be on different floors.
		if (A.Point.Floor == B.Point.Floor)
		{
			return false;
		}

		// Check that both points represent the same physical connector.
		bool bSameConnector = false;

		if (!A.Point.ConnectorId.IsEmpty() &&
			A.Point.ConnectorId == B.Point.ConnectorId)
		{
			// Newer data uses ConnectorGroupId.
			bSameConnector = true;
		}
		else if (A.Point.ConnectorId.IsEmpty() &&
			B.Point.ConnectorId.IsEmpty() &&
			A.Point.ConnectorType == B.Point.ConnectorType &&
			A.Point.Position.Equals(B.Point.Position, 0.01f))
		{
			// Older JSON may not have ConnectorGroupId.
			// In that case, use the same position and connector type.
			bSameConnector = true;
		}

		if (!bSameConnector)
		{
			return false;
		}

		// Check whether connector A can reach connector B's floor.
		const auto CanReachFloor =
			[](const FVertex& Connector, int32 TargetFloor)
			{
				if (Connector.ConnectsFloors.Num() > 0)
				{
					// Normal case: use the saved floor list.
					return Connector.ConnectsFloors.Contains(TargetFloor);
				}

				// Fallback for older data without a floor list.
				if (Connector.Point.ConnectorType ==
					EInteriorNavNodeType::Elevator)
				{
					return true;
				}

				// Stairs and escalators normally connect adjacent floors.
				return FMath::Abs(
					Connector.Point.Floor - TargetFloor) == 1;
			};

		// Both connector copies must agree that the floors are connected.
		return CanReachFloor(A, B.Point.Floor) &&
			CanReachFloor(B, A.Point.Floor);
	}
}

bool FNavigationRoutePlanner::IsValidPoint(const APolyBuilding& Building,
	const AInteriorGenerator& Interior, float Scale, int32 Floor, FVector2D Point, FString& OutError)
{
	OutError.Empty();
	if (Scale <= KINDA_SMALL_NUMBER)
	{
		OutError = TEXT("The building scale is invalid.");
		return false;
	}

	if (Floor == UNavigationPanelWidget::OutdoorFloor)
	{
		if (!UNavigationPanelWidget::IsOutdoorPoint(Building, Point))
		{
			OutError = TEXT("Select a point in the highlighted outside area.");
			return false;
		}

		return true;
	}

	if (Floor < 1 || Floor > Building.GetFloorCount())
	{
		OutError = TEXT("Choose a floor in the loaded building.");
		return false;
	}

	if (!InsideOrOnBoundary(Point, Building.GetFootprint()))
	{
		OutError = TEXT("Select a point inside the building.");
		return false;
	}
	const TArray<FVector2D>& Boundary = Building.GetFootprint();
	const FFloorGeometry Geometry = MakeFloorGeometry(Interior, Scale, Floor, Boundary);

	for (int32 Index = 0; Index < Boundary.Num(); ++Index)
	{
		const FVector2D& A = Boundary[Index];
		const FVector2D& B = Boundary[(Index + 1) % Boundary.Num()];

		const FVector2D Closest =
			FMath::ClosestPointOnSegment2D(Point, A, B);

		if (FVector2D::Distance(Point, Closest) < Geometry.WallClearance - GeometryEpsilon)
		{
			OutError = TEXT("Select a point outside the building wall's safety boundary.");
			return false;
		}
	}

	for (const FSegment& Wall : Geometry.Walls)
	{
		const FVector2D Closest = FMath::ClosestPointOnSegment2D(Point, Wall.A, Wall.B);
		if (FVector2D::Distance(Point, Closest) < Geometry.WallClearance - GeometryEpsilon)
		{
			OutError = TEXT("Select a point outside the wall's safety boundary.");
			return false;
		}
	}
	return true;
}

bool FNavigationRoutePlanner::FindRoute(
	const APolyBuilding& Building,
	const AInteriorGenerator& Interior,
	float Scale,
	int32 StartFloor,
	FVector2D Start,
	int32 DestinationFloor,
	FVector2D Destination,
	FNavigationRouteResult& OutResult)
{
	// Clear any old route before calculating a new one.
	OutResult = FNavigationRouteResult();

	// Check that both selected points are valid.
	if (!IsValidPoint(
		Building,
		Interior,
		Scale,
		StartFloor,
		Start,
		OutResult.Error))
	{
		return false;
	}

	if (!IsValidPoint(
		Building,
		Interior,
		Scale,
		DestinationFloor,
		Destination,
		OutResult.Error))
	{
		return false;
	}

	/*
		The route graph contains:

		- Start point
		- Destination point
		- Door points
		- Room corner turning points
		- Building boundary corners
		- Elevators, stairs, and escalators
	*/
	TMap<int32, FFloorGeometry> Floors;
	TArray<FVertex> Vertices;

	// Vertex 0 is always the start point.
	Vertices.Add(
		{
			{
				Start,
				StartFloor,
				FString(),
				EInteriorNavNodeType::PathNode
			},
			{}
		});

	// Vertex 1 is always the destination point.
	Vertices.Add(
		{
			{
				Destination,
				DestinationFloor,
				FString(),
				EInteriorNavNodeType::PathNode
			},
			{}
		});

	// Build the geometry information for every floor.
	for (int32 Floor = 1;
		Floor <= Building.GetFloorCount();
		++Floor)
	{
		// Get walls, doors, and possible turning points on this floor.
		FFloorGeometry Geometry =
			MakeFloorGeometry(Interior, Scale, Floor, Building.GetFootprint());

		// Add every door as a possible route point.
		for (const FVector2D& Door : Geometry.DoorPoints)
		{
			Vertices.Add(
				{
					{
						Door,
						Floor,
						FString(),
						EInteriorNavNodeType::PathNode
					},
					{}
				});
		}

		// Add room corners as possible route turning points.
		for (const FVector2D& Turn : Geometry.TurnPoints)
		{
			Vertices.Add(
				{
					{
						Turn,
						Floor,
						FString(),
						EInteriorNavNodeType::PathNode
					},
					{}
				});
		}

		for (const FExteriorExit& Exit : Geometry.ExteriorExits)
		{
			const FString ExitConnectorId =
				FString::Printf(TEXT("OutsideExit:%s"), *Exit.Id);

			// Floor 1 side of the marked exit.
			Vertices.Add(
				{
					{
						Exit.Position,
						Floor,
						ExitConnectorId,
						EInteriorNavNodeType::PathNode
					},
					{}
				});

			// Outside side of the same exit.
			Vertices.Add(
				{
					{
						Exit.Position,
						UNavigationPanelWidget::OutdoorFloor,
						ExitConnectorId,
						EInteriorNavNodeType::PathNode
					},
					{}
				});
		}
		// Save the geometry so it can be used later
		// when checking whether two points can see each other.
		Floors.Add(Floor, MoveTemp(Geometry));
	}

	// Add elevator, stairs, escalator, and path nodes.
	for (const FInteriorNavNode& Node :
		Interior.GetNavNodes())
	{
		// Ignore invalid nodes outside the building's floor range.
		if (Node.Level < 1 ||
			Node.Level > Building.GetFloorCount())
		{
			continue;
		}

		Vertices.Add(
			{
				{
					Node.Position,
					Node.Level,
					Node.ConnectorGroupId,
					Node.Type
				},
				Node.ConnectsFloors
			});
	}

	/*
		Create the graph connections.

		Every pair of points is checked:

		- Same floor:
		  connect them only if the direct line does not cross a wall.

		- Different floors:
		  connect them only if they belong to the same connector.
	*/
	TArray<TArray<FRouteGraphEdge>> Graph;
	Graph.SetNum(Vertices.Num());

	for (int32 I = 0;
		I < Vertices.Num();
		++I)
	{
		for (int32 J = I + 1;
			J < Vertices.Num();
			++J)
		{
			const FVertex& A = Vertices[I];
			const FVertex& B = Vertices[J];

			float Cost = 0.0f;

			if (A.Point.Floor == B.Point.Floor)
			{
				if (A.Point.Floor == UNavigationPanelWidget::OutdoorFloor)
				{
					if (!CanTravelOutside(
						A.Point.Position,
						B.Point.Position,
						Building.GetFootprint(),
						IsExteriorExitVertex(A.Point),
						IsExteriorExitVertex(B.Point)))
					{
						continue;
					}

					Cost = FVector2D::Distance(
						A.Point.Position,
						B.Point.Position);
				}
				else
				{
					// Find the geometry for this floor.
					const FFloorGeometry* FloorGeometry =
						Floors.Find(A.Point.Floor);

					if (!FloorGeometry)
					{
						continue;
					}

					TArray<int32> AllowedBoundaryEdges;

					auto AddExitBoundaryEdge = [&](const FNavigationRoutePoint& Point) -> bool
						{
							if (!IsExteriorExitVertex(Point))
							{
								return true;
							}

							double DistanceToBoundary = 0.0;
							const int32 BoundaryEdge = FindNearestBoundaryEdge(
								Point.Position,
								Building.GetFootprint(),
								DistanceToBoundary);

							if (BoundaryEdge == INDEX_NONE
								|| DistanceToBoundary > FloorGeometry->WallClearance * 1.5)
							{
								return false;
							}

							AllowedBoundaryEdges.AddUnique(BoundaryEdge);
							return true;
						};

					if (!AddExitBoundaryEdge(A.Point)
						|| !AddExitBoundaryEdge(B.Point))
					{
						continue;
					}

					// A route is allowed only when the straight line
					// stays inside the building and avoids solid walls.
					if (!CanTravel(
						A.Point.Position,
						B.Point.Position,
						Building.GetFootprint(),
						*FloorGeometry,
						AllowedBoundaryEdges))
					{
						continue;
					}

					Cost = IndoorTravelCost(A.Point.Position, B.Point.Position,
						Building.GetFootprint(), *FloorGeometry);
				}
			}
			else
			{
				// Different floors require an elevator,
				// stair, or escalator connector.
				if (!CanUseConnectorBetween(A, B))
				{
					continue;
				}

				// Vertical movement has a small extra cost.
				if (A.Point.Floor == UNavigationPanelWidget::OutdoorFloor
					|| B.Point.Floor == UNavigationPanelWidget::OutdoorFloor)
				{
					Cost = 5.0f;
				}
				else
				{
					// Vertical movement has a small extra cost.
					Cost =
						20.0f +
						FMath::Abs(A.Point.Floor - B.Point.Floor)
						* Building.GetFloorHeight()
						/ Scale;
				}
			}

			// Add the connection in both directions.
			// This makes the route graph bidirectional.
			Graph[I].Add({ J, Cost });
			Graph[J].Add({ I, Cost });
		}
	}

	/*
		Dijkstra's algorithm.

		This searches for the cheapest route instead of simply
		taking the first route found.

		Distance:
		Current cheapest known cost to reach each vertex.

		Previous:
		The previous vertex used to reach each vertex.

		Visited:
		Whether the cheapest route to that vertex is already confirmed.
	*/
	const float Infinity = TNumericLimits<float>::Max();

	TArray<float> Distance;
	TArray<int32> Previous;
	TArray<bool> Visited;

	Distance.Init(Infinity, Vertices.Num());
	Previous.Init(INDEX_NONE, Vertices.Num());
	Visited.Init(false, Vertices.Num());

	// The route starts at vertex 0.
	Distance[0] = 0.0f;

	for (int32 Step = 0;
		Step < Vertices.Num();
		++Step)
	{
		int32 Current = INDEX_NONE;

		// Find the unvisited vertex with the smallest cost.
		for (int32 I = 0;
			I < Vertices.Num();
			++I)
		{
			if (Visited[I])
			{
				continue;
			}

			if (Current == INDEX_NONE ||
				Distance[I] < Distance[Current])
			{
				Current = I;
			}
		}

		// Stop if no more reachable vertices exist.
		if (Current == INDEX_NONE ||
			Distance[Current] == Infinity)
		{
			break;
		}

		// Stop early when the destination is reached.
		if (Current == 1)
		{
			break;
		}

		Visited[Current] = true;

		// Try to improve the cost of every neighbour.
		for (const FRouteGraphEdge& Edge :
			Graph[Current])
		{
			if (Visited[Edge.To])
			{
				continue;
			}

			const float NewCost =
				Distance[Current] + Edge.Cost;

			// Keep the route only if it is cheaper
			// than the route currently stored.
			if (NewCost < Distance[Edge.To])
			{
				Distance[Edge.To] = NewCost;
				Previous[Edge.To] = Current;
			}
		}
	}

	// If the destination still has infinite cost,
	// no valid route could be found.
	if (Distance[1] == Infinity)
	{
		OutResult.Error =
			TEXT("No route found. Add a door or floor connector where needed.");

		return false;
	}

	/*
		Build the final route backwards.

		We start at the destination and repeatedly follow
		the Previous array until we reach the start.
	*/
	TArray<int32> ReversedRoute;

	for (int32 Current = 1;
		Current != INDEX_NONE;
		Current = Previous[Current])
	{
		ReversedRoute.Add(Current);
	}

	// A valid route must end at the start vertex.
	if (ReversedRoute.IsEmpty() ||
		ReversedRoute.Last() != 0)
	{
		OutResult.Error =
			TEXT("Route reconstruction failed.");

		return false;
	}

	// Reverse the route so it starts at the user's start point.
	for (int32 I = ReversedRoute.Num() - 1;
		I >= 0;
		--I)
	{
		OutResult.Points.Add(
			Vertices[ReversedRoute[I]].Point);
	}

	return true;
}
