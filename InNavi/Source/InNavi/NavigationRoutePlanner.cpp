#include "NavigationRoutePlanner.h"
#include "NavigationPanelWidget.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "PolyBuilding.h"
#include "BuildingGenerator.h"
#include "InteriorEntrance.h"
#include "LayoutValidation.h"
#include "PolygonGeometryUtils.h"
#include "HAL/PlatformTime.h"
#include "Async/ParallelFor.h"

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
		TArray<FVector2D> RoomDoors;
		TArray<FSegment> DoorSegments;
		TArray<FVector2D> TurnPoints;
		TArray<FExteriorExit> ExteriorExits;
		TArray<TArray<FVector2D>> RestrictedRooms;
		double WallClearance = 0.0;
		double PreferredWallClearance = 0.0;
	};

	enum class EPortal : uint8 { None, ExteriorExit, BuildingLink };

	struct FVertex
	{
		FNavigationRoutePoint Point;
		TArray<int32> ConnectsFloors;
		EPortal Portal = EPortal::None;
	};

	FString LinkPortalId(const FString& Owner, const FString& DoorId)
	{
		return FString::Printf(TEXT("BuildingLink:%d:%s%s"), Owner.Len(), *Owner, *DoorId);
	}

	// Value snapshots let the planner read unopened interiors without spawning actors, switching
	// the editor's building, or replacing its undo history.
	struct FRouteRoom
	{
		FString RoomId;
		TArray<FVector2D> Footprint;
		TArray<FInteriorOpening> Openings;
		bool bClosed = true;
		bool bRestricted = false;
		const TArray<FVector2D>& GetFootprint() const { return Footprint; }
		const TArray<FInteriorOpening>& GetOpenings() const { return Openings; }
		bool IsClosedLoop() const { return bClosed; }
	};

	struct FRouteBuilding
	{
		const APolyBuilding* Building = nullptr;
		TMap<int32, FFloorGeometry> Floors;
		TArray<FInteriorNavNode> Nodes;
		TArray<FInteriorEntrance> Entrances;
		TMap<int32, TArray<FRouteRoom>> Rooms;
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

	bool IsExteriorExitVertex(const FVertex& Vertex)
	{
		return Vertex.Portal == EPortal::ExteriorExit;
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

	bool IsInsideRestrictedRoom(const FVector2D& Point, const FFloorGeometry& Geometry)
	{
		return Geometry.RestrictedRooms.ContainsByPredicate([&Point](const TArray<FVector2D>& Room)
		{
			return InsideOrOnBoundary(Point, Room);
		});
	}

	bool IsPointClearOfWalls(const FVector2D& Point, const TArray<FVector2D>& BuildingBoundary,
		const FFloorGeometry& Geometry)
	{
		if (!InsideOrOnBoundary(Point, BuildingBoundary) || IsInsideRestrictedRoom(Point, Geometry))
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

	double SignedDistanceInside(const FVector2D& Point, const TArray<FVector2D>& Polygon)
	{
		double Distance = TNumericLimits<double>::Max();
		for (int32 I = 0; I < Polygon.Num(); ++I)
		{
			Distance = FMath::Min(Distance, PointToSegmentDistance(Point, Polygon[I], Polygon[(I + 1) % Polygon.Num()]));
		}
		return InsideOrOnBoundary(Point, Polygon) ? Distance : -Distance;
	}

	bool FindClearRoomPoint(const TArray<FVector2D>& Polygon, const TArray<FVector2D>& Boundary,
		const FFloorGeometry& Geometry, FVector2D& OutPosition)
	{
		if (Polygon.Num() < 3) return false;
		auto IsUsable = [&](const FVector2D& Point)
		{
			return InsideOrOnBoundary(Point, Polygon) && IsPointClearOfWalls(Point, Boundary, Geometry);
		};

		TArray<int32> Triangles;
		TriangulatePolygon(Polygon, Triangles);
		double BestArea = -1.0;
		for (int32 I = 0; I + 2 < Triangles.Num(); I += 3)
		{
			const FVector2D A = Polygon[Triangles[I]], B = Polygon[Triangles[I + 1]], C = Polygon[Triangles[I + 2]];
			const FVector2D Point = (A + B + C) / 3.0;
			const double Area = FMath::Abs(Cross(A, B, C));
			if (Area > BestArea && IsUsable(Point))
			{
				OutPosition = Point;
				BestArea = Area;
			}
		}
		if (BestArea >= 0.0) return true;

		// Centroids can all hug the wall of a narrow room. Search the room bounds for
		// another point, prioritizing cells that can still meet the clearance.
		FBox2D Bounds(ForceInit);
		for (const FVector2D& Point : Polygon) Bounds += Point;
		struct FCell
		{
			FVector2D Center;
			FVector2D Extent;
			double BestPossibleClearance;
		};
		TArray<FCell> Cells;
		auto Better = [](const FCell& A, const FCell& B) { return A.BestPossibleClearance > B.BestPossibleClearance; };
		auto Visit = [&](const FVector2D& Center, const FVector2D& Extent)
		{
			if (IsUsable(Center))
			{
				OutPosition = Center;
				return true;
			}
			const double Required = Geometry.WallClearance - GeometryEpsilon;
			double Margin = FMath::Min(SignedDistanceInside(Center, Polygon), SignedDistanceInside(Center, Boundary) - Required);
			for (const FSegment& Wall : Geometry.Walls)
			{
				Margin = FMath::Min(Margin, PointToSegmentDistance(Center, Wall.A, Wall.B) - Required);
			}
			for (const auto& Room : Geometry.RestrictedRooms)
			{
				Margin = FMath::Min(Margin, -SignedDistanceInside(Center, Room));
			}
			// None of the distance margins can increase by more than the cell radius.
			const double BestPossibleClearance = Margin + Extent.Size();
			if (BestPossibleClearance >= -GeometryEpsilon && Extent.GetMax() > GeometryEpsilon)
			{
				Cells.HeapPush({Center, Extent, BestPossibleClearance}, Better);
			}
			return false;
		};
		if (Visit(Bounds.GetCenter(), Bounds.GetExtent())) return true;
		constexpr int32 MaxSubdivisions = 1024;
		for (int32 I = 0; I < MaxSubdivisions && !Cells.IsEmpty(); ++I)
		{
			FCell Cell;
			Cells.HeapPop(Cell, Better, EAllowShrinking::No);
			const bool bSplitX = Cell.Extent.X >= Cell.Extent.Y;
			const FVector2D Shift = bSplitX ? FVector2D(Cell.Extent.X * 0.5, 0.0) : FVector2D(0.0, Cell.Extent.Y * 0.5);
			const FVector2D Half = Cell.Extent - Shift;
			if (Visit(Cell.Center - Shift, Half) || Visit(Cell.Center + Shift, Half)) return true;
		}
		return false;
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

	FFloorGeometry MakeFloorGeometryFromRooms(const TArray<const FRouteRoom*>& Rooms, float WallThickness, float Scale, int32 Floor,
		const TArray<FVector2D>& BuildingBoundary)
	{
		FFloorGeometry Geometry;
		TArray<FSegment> DoorApproaches;
		Geometry.WallClearance =
			(WallThickness * 0.5 + RouteWallGapWorldUnits)
			/ FMath::Max(static_cast<double>(Scale), static_cast<double>(KINDA_SMALL_NUMBER));
		Geometry.PreferredWallClearance =
			(WallThickness * 0.5 + PreferredWallGapWorldUnits)
			/ FMath::Max(static_cast<double>(Scale), static_cast<double>(KINDA_SMALL_NUMBER));

		for (const FRouteRoom* Room : Rooms)
		{
			if (!Room)
			{
				continue;
			}
			const TArray<FVector2D>& Footprint = Room->GetFootprint();
			const bool bRestricted = Room->IsClosedLoop() && Room->bRestricted && Footprint.Num() >= 3;
			if (bRestricted)
			{
				Geometry.RestrictedRooms.Add(Footprint);
			}
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

				// An opening in a restricted room remains closed to public routing.
				if (bRestricted)
				{
					AddWall(Geometry, A, B);
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
						if (Opening.Type == EInteriorOpeningType::Door)
						{
							Geometry.RoomDoors.Add(DoorPosition);
							Geometry.DoorSegments.Add({ A + Direction * Begin, A + Direction * End });
						}
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
							Exit.Id = FString::Printf(TEXT("Room:%d:%s%s"), Room->RoomId.Len(), *Room->RoomId, *Opening.Id);
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
		for (const FRouteRoom* Room : Rooms)
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

	FFloorGeometry MakeFloorGeometry(const AInteriorGenerator& Interior, float Scale, int32 Floor,
		const TArray<FVector2D>& Boundary)
	{
		TArray<FRouteRoom> Values;
		for (const AInteriorRoom* Room : Interior.GetRoomsOnFloor(Floor))
		{
			if (IsValid(Room)) Values.Add({ Room->RoomId, Room->GetFootprint(), Room->GetOpenings(), Room->IsClosedLoop(), !Room->bIsPublic || Room->bRequiresKey });
		}
		TArray<const FRouteRoom*> Rooms;
		for (const FRouteRoom& Room : Values) Rooms.Add(&Room);
		return MakeFloorGeometryFromRooms(Rooms, Interior.GetWallThicknessForFloor(Floor), Scale, Floor, Boundary);
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
				&& IsExteriorExitVertex(A)
				&& IsExteriorExitVertex(B)
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
			A.Point.ConnectorId == B.Point.ConnectorId &&
			A.Point.ConnectorType == B.Point.ConnectorType &&
			A.Point.Position.Equals(B.Point.Position, 0.01f))
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
	if (IsInsideRestrictedRoom(Point, Geometry))
	{
		OutError = TEXT("Select a point in an area open to public navigation.");
		return false;
	}

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

namespace
{
	FVector2D ReadRouteXY(const TSharedPtr<FJsonValue>& Value)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
		return Value.IsValid() && Value->TryGetArray(Pair) && Pair->Num() >= 2
			? FVector2D((*Pair)[0]->AsNumber(), (*Pair)[1]->AsNumber()) : FVector2D::ZeroVector;
	}

	bool EntrancePosition(const FRouteBuilding& Model, const FInteriorEntrance& Door, float Scale, FVector2D& Position)
	{
		const auto& Boundary = Model.Building->GetFootprint();
		if (!Boundary.IsValidIndex(Door.EdgeIndex) || Door.Level < 1 || Door.Level > Model.Building->GetFloorCount()
			|| !FMath::IsFinite(Door.Offset) || !FMath::IsFinite(Door.Width) || Door.Offset < 0 || Door.Width <= 0) return false;
		const FVector2D A = Boundary[Door.EdgeIndex], B = Boundary[(Door.EdgeIndex + 1) % Boundary.Num()];
		if ((Door.Offset + Door.Width) / Scale > FVector2D::Distance(A, B) + GeometryEpsilon) return false;
		const FFloorGeometry* Floor = Model.Floors.Find(Door.Level);
		if (!Floor || Door.Width / Scale <= Floor->WallClearance * 2) return false;
		Position = A + (B - A).GetSafeNormal() * ((Door.Offset + Door.Width * 0.5) / Scale);
		return true;
	}

	FRouteBuilding ReadRouteBuilding(const APolyBuilding& Building, const AInteriorGenerator& Live,
		const TSharedPtr<FJsonObject>& Saved, const TArray<FInteriorEntrance>& Entrances, float Scale, bool bReadFloors,
		int32 OnlyFloor = INDEX_NONE)
	{
		FRouteBuilding Model;
		Model.Building = &Building;
		Model.Entrances = Entrances;
		if (!bReadFloors) return Model;
		const bool bLive = Live.GetLoadedBuildingId() == Building.BuildingId;
		const TArray<TSharedPtr<FJsonValue>>* SavedFloors = nullptr;
		if (Saved.IsValid()) Saved->TryGetArrayField(TEXT("floors"), SavedFloors);
		for (int32 Level = 1; Level <= Building.GetFloorCount(); ++Level)
		{
			if (OnlyFloor != INDEX_NONE && OnlyFloor != Level) continue;
			if (bLive)
			{
				for (const auto* Room : Live.GetRoomsOnFloor(Level)) if (IsValid(Room))
					Model.Rooms.FindOrAdd(Level).Add({ Room->RoomId, Room->GetFootprint(), Room->GetOpenings(), Room->IsClosedLoop(), !Room->bIsPublic || Room->bRequiresKey });
				Model.Floors.Add(Level, MakeFloorGeometry(Live, Scale, Level, Building.GetFootprint()));
				continue;
			}
			TArray<FRouteRoom> Values;
			float Thickness = 20.f;
			if (SavedFloors) for (const auto& FloorValue : *SavedFloors)
			{
				const auto Floor = FloorValue->AsObject();
				int32 SavedLevel = 1;
				if (!Floor.IsValid()) continue;
				Floor->TryGetNumberField(TEXT("level"), SavedLevel);
				if (SavedLevel != Level) continue;
				Floor->TryGetNumberField(TEXT("wallThickness"), Thickness);
				const TArray<TSharedPtr<FJsonValue>>* Rooms = nullptr;
				if (Floor->TryGetArrayField(TEXT("rooms"), Rooms)) for (const auto& RoomValue : *Rooms)
				{
					const auto Room = RoomValue->AsObject();
					if (!Room.IsValid()) continue;
					FRouteRoom Data;
					Room->TryGetStringField(TEXT("id"), Data.RoomId);
					Room->TryGetBoolField(TEXT("closed"), Data.bClosed);
					FString Access;
					bool bRequiresKey = false;
					Room->TryGetStringField(TEXT("access"), Access);
					Room->TryGetBoolField(TEXT("requireKey"), bRequiresKey);
					Data.bRestricted = Access == TEXT("private") || bRequiresKey;
					const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
					if (Room->TryGetArrayField(TEXT("footprint"), Points)) for (const auto& P : *Points) Data.Footprint.Add(ReadRouteXY(P));
					const TArray<TSharedPtr<FJsonValue>>* Openings = nullptr;
					if (Room->TryGetArrayField(TEXT("openings"), Openings)) for (const auto& OpeningValue : *Openings)
					{
						const auto Opening = OpeningValue->AsObject();
						if (!Opening.IsValid()) continue;
						FInteriorOpening O;
						FString Type;
						Opening->TryGetStringField(TEXT("id"), O.Id);
						Opening->TryGetStringField(TEXT("type"), Type);
						O.Type = Type == TEXT("window") ? EInteriorOpeningType::Window : Type == TEXT("void") ? EInteriorOpeningType::Void : EInteriorOpeningType::Door;
						Opening->TryGetNumberField(TEXT("edgeIndex"), O.EdgeIndex);
						Opening->TryGetNumberField(TEXT("offset"), O.Offset);
						Opening->TryGetNumberField(TEXT("width"), O.Width);
						Opening->TryGetStringField(TEXT("connectsTo"), O.ConnectsTo);
						Data.Openings.Add(O);
					}
					Values.Add(MoveTemp(Data));
				}
				const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
				if (Floor->TryGetArrayField(TEXT("navNodes"), Nodes)) for (const auto& NodeValue : *Nodes)
				{
					const auto Node = NodeValue->AsObject();
					if (!Node.IsValid()) continue;
					FInteriorNavNode N;
					N.Level = Level;
					FString Type;
					Node->TryGetStringField(TEXT("id"), N.Id);
					Node->TryGetStringField(TEXT("type"), Type);
					Node->TryGetStringField(TEXT("connectorGroupId"), N.ConnectorGroupId);
					N.Type = Type == TEXT("elevator") ? EInteriorNavNodeType::Elevator : Type == TEXT("stairs") ? EInteriorNavNodeType::Stairs
						: Type == TEXT("escalator") ? EInteriorNavNodeType::Escalator : EInteriorNavNodeType::PathNode;
					const TArray<TSharedPtr<FJsonValue>>* Position = nullptr;
					if (Node->TryGetArrayField(TEXT("position"), Position) && Position->Num() >= 2) N.Position = FVector2D((*Position)[0]->AsNumber(), (*Position)[1]->AsNumber());
					const TArray<TSharedPtr<FJsonValue>>* Levels = nullptr;
					if (Node->TryGetArrayField(TEXT("connectsFloors"), Levels)) for (const auto& L : *Levels) N.ConnectsFloors.Add(static_cast<int32>(L->AsNumber()));
					Model.Nodes.Add(N);
				}
			}
			TArray<const FRouteRoom*> Rooms;
			for (const auto& Room : Values) Rooms.Add(&Room);
			Model.Floors.Add(Level, MakeFloorGeometryFromRooms(Rooms, Thickness, Scale, Level, Building.GetFootprint()));
			Model.Rooms.Add(Level, MoveTemp(Values));
		}
		if (bLive) Model.Nodes = Live.GetNavNodes();
		for (const auto& Door : Entrances)
		{
			FVector2D Position;
			if (Door.Kind == EInteriorEntranceKind::Main && Door.Level == 1 && EntrancePosition(Model, Door, Scale, Position))
				Model.Floors.FindChecked(1).ExteriorExits.Add({ TEXT("Main:") + Door.Id, Position });
		}
		return Model;
	}

	struct FRouteDoorway
	{
		FString BuildingId;
		int32 Floor;
		FSegment Opening;
	};

	struct FCampusGraph
	{
		TArray<FRouteDoorway> Doorways;
		TArray<FVertex> Vertices;
		TArray<TArray<FRouteGraphEdge>> Edges;
		// The points a search is about (the start and the destination, or the rooms a check asks about): vertices after the StaticCount of
		// the buildings, or INDEX_NONE for a point that cannot be used.
		TArray<int32> SeedVertices;
		int32 StaticCount = 0;
	};

	// The part of a campus graph that does not depend on the seeds: the vertices of the buildings and which of them can see each other.
	// Joining them in pairs is nearly all the work of a route in a big building (thousands of vertices); a seed is only joined to what is
	// already there. So it is kept, keyed by everything it is made from, and found again by the next route.
	struct FStaticGraph
	{
		TArray<FVertex> Vertices;
		TArray<TArray<FRouteGraphEdge>> Edges;
	};

	struct FGraphCache
	{
		FCriticalSection Lock;
		TArray<TPair<uint64, TSharedRef<const FStaticGraph>>> Entries;
		int32 Hits = 0;
		int32 Misses = 0;
		// A few: the whole campus, a building on its own, and the same with other preferences for lifts and stairs.
		static constexpr int32 MaxEntries = 4;
	};

	FGraphCache& GraphCache()
	{
		static FGraphCache Cache;
		return Cache;
	}

	struct FInputHasher
	{
		uint64 State = 1469598103934665603ull;
		void Bytes(const void* Data, SIZE_T Length)
		{
			const uint8* Bytes = static_cast<const uint8*>(Data);
			for (SIZE_T I = 0; I < Length; ++I) { State ^= Bytes[I]; State *= 1099511628211ull; }
		}
		template <typename T> void Pod(const T& Value) { Bytes(&Value, sizeof(T)); }
		void Text(const FString& Value) { const int32 Length = Value.Len(); Pod(Length); Bytes(*Value, Length * sizeof(TCHAR)); }
		void Points(const TArray<FVector2D>& Values) { Pod(Values.Num()); for (const FVector2D& Value : Values) { Pod(Value.X); Pod(Value.Y); } }
	};

	// Everything the static graph is made from: if none of it changed, the graph is the same.
	uint64 HashGraphInputs(const TArray<FRouteBuilding>& Models, float Scale, bool bNeedsOutdoor)
	{
		FInputHasher Hash;
		Hash.Pod(Scale); Hash.Pod(bNeedsOutdoor); Hash.Pod(Models.Num());
		for (const FRouteBuilding& Model : Models)
		{
			Hash.Text(Model.Building->BuildingId);
			Hash.Pod(Model.Building->BuildingOffset.X); Hash.Pod(Model.Building->BuildingOffset.Y);
			Hash.Pod(Model.Building->GetFloorCount()); Hash.Pod(Model.Building->GetFloorHeight());
			Hash.Points(Model.Building->GetFootprint());
			TArray<int32> Levels;
			Model.Floors.GetKeys(Levels);
			Levels.Sort();
			for (const int32 Level : Levels)
			{
				const FFloorGeometry& Floor = Model.Floors.FindChecked(Level);
				Hash.Pod(Level); Hash.Pod(Floor.WallClearance); Hash.Pod(Floor.PreferredWallClearance);
				Hash.Pod(Floor.Walls.Num());
				for (const FSegment& Wall : Floor.Walls) { Hash.Pod(Wall.A.X); Hash.Pod(Wall.A.Y); Hash.Pod(Wall.B.X); Hash.Pod(Wall.B.Y); }
				Hash.Pod(Floor.RestrictedRooms.Num());
				for (const auto& Room : Floor.RestrictedRooms) Hash.Points(Room);
				Hash.Points(Floor.DoorPoints); Hash.Points(Floor.RoomDoors); Hash.Points(Floor.TurnPoints);
				Hash.Pod(Floor.ExteriorExits.Num());
				for (const FExteriorExit& Exit : Floor.ExteriorExits) { Hash.Text(Exit.Id); Hash.Pod(Exit.Position.X); Hash.Pod(Exit.Position.Y); }
			}
			Hash.Pod(Model.Nodes.Num());
			for (const FInteriorNavNode& Node : Model.Nodes)
			{
				Hash.Text(Node.Id); Hash.Pod(static_cast<uint8>(Node.Type)); Hash.Pod(Node.Level); Hash.Pod(Node.Position.X); Hash.Pod(Node.Position.Y);
				Hash.Text(Node.ConnectorGroupId); Hash.Pod(Node.ConnectsFloors.Num());
				for (const int32 Connected : Node.ConnectsFloors) Hash.Pod(Connected);
			}
			Hash.Pod(Model.Entrances.Num());
			for (const FInteriorEntrance& Door : Model.Entrances)
			{
				Hash.Text(Door.Id); Hash.Pod(static_cast<uint8>(Door.Kind)); Hash.Pod(Door.Level); Hash.Pod(Door.EdgeIndex);
				Hash.Pod(Door.Offset); Hash.Pod(Door.Width); Hash.Text(Door.ConnectsTo); Hash.Pod(Door.bMirrored); Hash.Text(Door.SourceBuildingId);
			}
		}
		return Hash.State;
	}

	// The campus graph, built in two parts so that a long build can be spread over several frames: Begin makes the
	// vertices, and Step joins them, as far as its time allows.
	class FCampusGraphBuilder
	{
	public:
		FCampusGraphBuilder(const TArray<FRouteBuilding>& InModels, float InScale) : Models(InModels), Scale(InScale)
		{
			for (const auto& M : Models) ModelsById.FindOrAdd(M.Building->BuildingId, &M);
		}
		bool Begin(const TArray<FNavigationRoutePoint>& Seeds, FString& Error);
		// Joins vertices until FPlatformTime::Seconds() reaches Deadline; true once every vertex is joined. The vertices of the buildings are
		// worked out several at a time, on the worker threads; the graph does not depend on how many.
		bool Step(double Deadline);
		// 0 to 1. Earlier vertices have more later ones to be joined with, so they take longer.
		float GetProgress() const
		{
			const double Count = Out.Vertices.Num();
			const double Left = Count > 0 ? (Count - NextVertex) / Count : 0.0;
			return static_cast<float>(1.0 - Left * Left);
		}
		FCampusGraph Out;
		// For the timing log (see FNavigationRouteJob): the seconds spent joining the vertices of the buildings and joining the start and
		// the destination to them, and whether the joined vertices of the buildings were found kept from an earlier route.
		double StaticJoinSeconds = 0.0;
		double SeedJoinSeconds = 0.0;
		bool bCacheHit = false;

	private:
		void EdgesOf(int32 I, TArray<int32>& Scratch, TArray<FRouteGraphEdge>& Edges) const;
		const FRouteBuilding* ModelFor(const FString& Id) const
		{
			const FRouteBuilding* const* Found = ModelsById.Find(Id);
			return Found ? *Found : nullptr;
		}
		// Keeps the joined vertices of the buildings (not the seeds) for the next route, once they are all joined.
		void StoreStatic()
		{
			bStaticStored = true;
			TSharedRef<FStaticGraph> Kept = MakeShared<FStaticGraph>();
			Kept->Vertices.Append(Out.Vertices.GetData(), StaticCount);
			Kept->Edges.Append(Out.Edges.GetData(), StaticCount);
			FGraphCache& Cache = GraphCache();
			FScopeLock Guard(&Cache.Lock);
			Cache.Entries.RemoveAll([this](const TPair<uint64, TSharedRef<const FStaticGraph>>& Entry) { return Entry.Key == Key; });
			Cache.Entries.Emplace(Key, Kept);
			if (Cache.Entries.Num() > FGraphCache::MaxEntries) Cache.Entries.RemoveAt(0);
		}

		const TArray<FRouteBuilding>& Models;
		const float Scale;
		TMap<FString, const FRouteBuilding*> ModelsById;
		TMap<FString, TArray<int32>> ByBuilding, ByLink;
		TArray<int32> Outdoor, Candidates;
		int32 NextVertex = 0;
		// How many vertices of the buildings are worked out at once; it follows how long a batch takes (see Step).
		int32 BatchSize = 16;
		uint64 Key = 0;
		int32 StaticCount = 0;
		bool bStaticStored = false;
	};

	bool FCampusGraphBuilder::Begin(const TArray<FNavigationRoutePoint>& Seeds, FString& Error)
	{
		Out = {};
		if (!FMath::IsFinite(Scale) || Scale <= KINDA_SMALL_NUMBER) { Error = TEXT("The project scale is invalid."); return false; }
		auto Stamp = [&](FNavigationRoutePoint& Point, const APolyBuilding& Building)
		{
			Point.BuildingId = Building.BuildingId;
			const FVector2D XY = (Point.Position + Building.BuildingOffset) * Scale;
			Point.WorldPosition = FVector(XY.X, XY.Y, FMath::Max(0, Point.Floor - 1) * Building.GetFloorHeight());
			Point.bHasWorldPosition = true;
		};
		auto Validate = [&](FNavigationRoutePoint& Point) -> bool
		{
			const auto* M = ModelFor(Point.BuildingId);
			if (!M || !FMath::IsFinite(Point.Position.X) || !FMath::IsFinite(Point.Position.Y)) return false;
			if (Point.Floor == 0)
			{
				if (!UNavigationPanelWidget::IsOutdoorPoint(*M->Building, Point.Position)) return false;
				const FVector2D Campus = Point.Position + M->Building->BuildingOffset;
				for (const auto& Other : Models) if (InsideOrOnBoundary(Campus - Other.Building->BuildingOffset, Other.Building->GetFootprint())) return false;
			}
			else
			{
				const auto* Floor = M->Floors.Find(Point.Floor);
				if (!Floor || !IsPointClearOfWalls(Point.Position, M->Building->GetFootprint(), *Floor)) return false;
			}
			Stamp(Point, *M->Building);
			return true;
		};
		TArray<FVertex>& Vertices = Out.Vertices;
		bool bNeedsOutdoor = false;
		for (const FNavigationRoutePoint& Seed : Seeds) bNeedsOutdoor |= Seed.Floor == 0;
		for (const auto& M : Models) for (const auto& Floor : M.Floors)
			bNeedsOutdoor |= !Floor.Value.ExteriorExits.IsEmpty();
		auto Add = [&](const FRouteBuilding& M, FVector2D P, int32 Floor, const FString& Id = FString(),
			EInteriorNavNodeType Type = EInteriorNavNodeType::PathNode, const TArray<int32>& Levels = TArray<int32>(),
			bool bIsDoorway = false, EPortal Portal = EPortal::None)
		{
			const FFloorGeometry* Geometry = M.Floors.Find(Floor);
			if (Geometry && IsInsideRestrictedRoom(P, *Geometry)) return false;

			FNavigationRoutePoint Point;
			Point.Position = P; Point.Floor = Floor; Point.ConnectorId = Id; Point.ConnectorType = Type;
			Point.bIsDoorway = bIsDoorway;
			Stamp(Point, *M.Building);
			Vertices.Add({ Point, Levels, Portal });
			return true;
		};
		for (const auto& M : Models)
		{
			for (const auto& Pair : M.Floors)
			{
				for (const auto& Door : Pair.Value.DoorSegments) Out.Doorways.Add({ M.Building->BuildingId, Pair.Key, Door });
				for (const auto& Door : Pair.Value.DoorPoints)
				{
					Add(M, Door, Pair.Key, {}, EInteriorNavNodeType::PathNode, {}, Pair.Value.RoomDoors.Contains(Door));
				}
				for (const auto& Turn : Pair.Value.TurnPoints) Add(M, Turn, Pair.Key);
				for (const auto& Exit : Pair.Value.ExteriorExits)
				{
					const FString Id = TEXT("OutsideExit:") + M.Building->BuildingId + TEXT(":") + Exit.Id;
					if (Add(M, Exit.Position, Pair.Key, Id, EInteriorNavNodeType::PathNode, {}, false, EPortal::ExteriorExit))
						Add(M, Exit.Position, 0, Id, EInteriorNavNodeType::PathNode, {}, false, EPortal::ExteriorExit);
				}
			}
			for (const auto& Node : M.Nodes) if (M.Floors.Contains(Node.Level)) Add(M, Node.Position, Node.Level, Node.ConnectorGroupId, Node.Type, Node.ConnectsFloors);
			for (const auto& Door : M.Entrances)
			{
				FVector2D P;
				if (Door.Kind != EInteriorEntranceKind::Link || !EntrancePosition(M, Door, Scale, P)) continue;
				const FString Source = Door.bMirrored ? Door.SourceBuildingId : M.Building->BuildingId;
				Add(M, P, Door.Level, LinkPortalId(Source, Door.Id), EInteriorNavNodeType::PathNode, {}, false, EPortal::BuildingLink);
			}
			// Outdoor visibility vertices allow a route to go around every building, including
			// buildings that have no entrances/interior data. Coordinates retain their owner.
			if (bNeedsOutdoor) for (const auto& Corner : M.Building->GetFootprint()) for (int32 Ray = 0; Ray < 16; ++Ray)
			{
				const double Angle = Ray * 2 * PI / 16;
				const FVector2D P = Corner + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * (40.0 / Scale);
				const FVector2D Campus = P + M.Building->BuildingOffset;
				bool bFree = true;
				for (const auto& Other : Models) if (InsideOrOnBoundary(Campus - Other.Building->BuildingOffset, Other.Building->GetFootprint())) { bFree = false; break; }
				if (bFree) Add(M, P, 0);
			}
		}
		// The seeds come after the vertices of the buildings, so that those can be kept for the next route (FStaticGraph).
		StaticCount = Vertices.Num();
		Out.StaticCount = StaticCount;
		Key = HashGraphInputs(Models, Scale, bNeedsOutdoor);
		for (auto Seed : Seeds) Out.SeedVertices.Add(Validate(Seed) ? Vertices.Add({ Seed, {} }) : INDEX_NONE);
		TArray<TArray<FRouteGraphEdge>>& Graph = Out.Edges;
		TSharedPtr<const FStaticGraph> Found;
		{
			FGraphCache& Cache = GraphCache();
			FScopeLock Guard(&Cache.Lock);
			for (const TPair<uint64, TSharedRef<const FStaticGraph>>& Entry : Cache.Entries) if (Entry.Key == Key) { Found = Entry.Value; break; }
			if (Found.IsValid() && Found->Vertices.Num() == StaticCount && Found->Edges.Num() == StaticCount) ++Cache.Hits;
			else { Found.Reset(); ++Cache.Misses; }
		}
		if (Found.IsValid())
		{
			// Already joined: only the seeds are left.
			Graph = Found->Edges;
			NextVertex = StaticCount;
			bStaticStored = true;
			bCacheHit = true;
		}
		Graph.SetNum(Vertices.Num());
		// Only some pairs can ever be joined: two outdoor points, two points of one building (any floors), or the two
		// halves of one building link. Testing every pair of the campus costs time with the square of its size.
		for (int32 I = 0; I < Vertices.Num(); ++I)
		{
			const FNavigationRoutePoint& Point = Vertices[I].Point;
			ByBuilding.FindOrAdd(Point.BuildingId).Add(I);
			if (Point.Floor == 0) Outdoor.Add(I);
			if (Vertices[I].Portal == EPortal::BuildingLink) ByLink.FindOrAdd(Point.ConnectorId).Add(I);
		}
		return true;
	}

	// The edges from vertex I to the vertices it is joined with, in increasing order of the other vertex. A vertex of a building is joined to the
	// later ones of the buildings; a seed to every vertex before it, the buildings and the seeds before it, so each pair is met once. It reads
	// the graph and changes nothing in it, so the vertices of the buildings can be worked out several at a time (Step).
	void FCampusGraphBuilder::EdgesOf(int32 I, TArray<int32>& Scratch, TArray<FRouteGraphEdge>& Edges) const
	{
		const TArray<FVertex>& Vertices = Out.Vertices;
		const bool bSeed = I >= StaticCount;
		Scratch.Reset();
		Edges.Reset();
		auto Gather = [&](const TArray<int32>& Group) { for (const int32 J : Group) if (bSeed ? J < I : (J > I && J < StaticCount)) Scratch.Add(J); };
		Gather(ByBuilding.FindChecked(Vertices[I].Point.BuildingId));
		if (Vertices[I].Point.Floor == 0) Gather(Outdoor);
		if (Vertices[I].Portal == EPortal::BuildingLink) Gather(ByLink.FindChecked(Vertices[I].Point.ConnectorId));
		// Met in increasing order and once each, as the loop over every pair would, so the graph comes out the same.
		Scratch.Sort();
		int32 Last = INDEX_NONE;
		for (const int32 J : Scratch)
		{
			if (J == Last) continue;
			Last = J;
			const FVertex& A = Vertices[I]; const FVertex& B = Vertices[J];
			const auto* M = ModelFor(A.Point.BuildingId);
			float Cost = 0;
			if (A.Point.Floor == 0 && B.Point.Floor == 0)
			{
				bool bClear = true;
				for (const auto& Other : Models)
				{
					const FVector2D PA = FVector2D(A.Point.WorldPosition) / Scale - Other.Building->BuildingOffset;
					const FVector2D PB = FVector2D(B.Point.WorldPosition) / Scale - Other.Building->BuildingOffset;
					if (!CanTravelOutside(PA, PB, Other.Building->GetFootprint(),
						IsExteriorExitVertex(A) && A.Point.BuildingId == Other.Building->BuildingId,
						IsExteriorExitVertex(B) && B.Point.BuildingId == Other.Building->BuildingId)
						|| InsideOrOnBoundary((PA + PB) * 0.5, Other.Building->GetFootprint())) { bClear = false; break; }
				}
				if (!bClear) continue;
				Cost = FVector::Distance(A.Point.WorldPosition, B.Point.WorldPosition) / Scale;
			}
			else if (A.Point.BuildingId != B.Point.BuildingId)
			{
				// Mirrored links represent one physical doorway. A floor number alone cannot
				// connect buildings of different heights, and a missing/dangling link cannot teleport.
				if (A.Portal != EPortal::BuildingLink || B.Portal != EPortal::BuildingLink || A.Point.ConnectorId != B.Point.ConnectorId
					|| A.Point.Floor != B.Point.Floor || !InteriorEntrances::AreElevationsAligned(A.Point.WorldPosition.Z, B.Point.WorldPosition.Z)
					|| FVector::Distance(A.Point.WorldPosition, B.Point.WorldPosition) > InteriorEntrances::MaxLinkDistanceWorldUnits) continue;
				Cost = FMath::Max(0.01f, static_cast<float>(FVector::Distance(A.Point.WorldPosition, B.Point.WorldPosition) / Scale));
			}
			else if (A.Point.Floor == B.Point.Floor)
			{
				const auto* Floor = M->Floors.Find(A.Point.Floor);
				if (!Floor) continue;
				TArray<int32> Allowed;
				bool bValid = true;
				for (const auto* Vertex : { &A, &B }) if (Vertex->Portal != EPortal::None)
				{
					double Distance;
					const int32 Edge = FindNearestBoundaryEdge(Vertex->Point.Position, M->Building->GetFootprint(), Distance);
					if (Edge == INDEX_NONE || Distance > Floor->WallClearance * 1.5) { bValid = false; break; }
					Allowed.AddUnique(Edge);
				}
				if (!bValid || !CanTravel(A.Point.Position, B.Point.Position, M->Building->GetFootprint(), *Floor, Allowed)) continue;
				Cost = IndoorTravelCost(A.Point.Position, B.Point.Position, M->Building->GetFootprint(), *Floor);
			}
			else
			{
				if (!CanUseConnectorBetween(A, B)) continue;
				Cost = A.Point.Floor == 0 || B.Point.Floor == 0 ? 5.f : 20.f + FMath::Abs(A.Point.Floor - B.Point.Floor) * M->Building->GetFloorHeight() / Scale;
			}
			Edges.Add({ J, Cost });
		}
	}

	bool FCampusGraphBuilder::Step(double Deadline)
	{
		const TArray<FVertex>& Vertices = Out.Vertices;
		TArray<TArray<FRouteGraphEdge>>& Graph = Out.Edges;
		for (bool bFirst = true; NextVertex < Vertices.Num(); bFirst = false)
		{
			if (!bFirst && FPlatformTime::Seconds() >= Deadline) return false;
			// The vertices of the buildings are all joined: keep them, before the first seed is.
			if (NextVertex == StaticCount && !bStaticStored) StoreStatic();
			const double Started = FPlatformTime::Seconds();
			if (NextVertex >= StaticCount)
			{
				// A seed (the start, the destination): there are two, and each is joined on its own.
				const int32 I = NextVertex++;
				TArray<FRouteGraphEdge> Edges;
				EdgesOf(I, Candidates, Edges);
				for (const FRouteGraphEdge& Edge : Edges) { Graph[I].Add({ Edge.To, Edge.Cost }); Graph[Edge.To].Add({ I, Edge.Cost }); }
				SeedJoinSeconds += FPlatformTime::Seconds() - Started;
				continue;
			}
			// The vertices of the buildings, several at a time: each is worked out on its own (EdgesOf) and its edges are put into the graph
			// afterwards, in the order of the vertices, so the graph comes out the same as when they are joined one after the other (the list
			// of every vertex runs from the lowest to the highest other vertex). How many go at once follows how long the last batch took, so
			// that a slice is about as long as it is allowed to be.
			const int32 First = NextVertex;
			const int32 Count = FMath::Min(BatchSize, StaticCount - First);
			TArray<TArray<FRouteGraphEdge>> Found;
			Found.SetNum(Count);
			ParallelFor(Count, [this, First, &Found](int32 Index)
			{
				TArray<int32> Scratch;
				EdgesOf(First + Index, Scratch, Found[Index]);
			});
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const int32 I = First + Index;
				for (const FRouteGraphEdge& Edge : Found[Index]) Graph[Edge.To].Add({ I, Edge.Cost });
				Graph[I].Append(Found[Index]);
			}
			NextVertex = First + Count;
			const double Elapsed = FPlatformTime::Seconds() - Started;
			StaticJoinSeconds += Elapsed;
			const double Target = FMath::Clamp(Deadline - Started, 0.002, 0.02);
			if (Elapsed > Target) BatchSize = FMath::Max(1, BatchSize / 2);
			else if (Elapsed < Target * 0.5) BatchSize = FMath::Min(512, BatchSize * 2);
		}
		if (!bStaticStored && NextVertex >= StaticCount) StoreStatic();
		return true;
	}

	// The shortest way from the first vertex (the start) to the second (the destination), found by Dijkstra's method. Like the
	// graph builder it can stop and go on later, so that a long search can be spread over several frames.
	class FRouteSolver
	{
	public:
		explicit FRouteSolver(const FCampusGraph& InGraph) : Graph(InGraph)
		{
			const int32 Count = Graph.Vertices.Num();
			Distances.Init(TNumericLimits<float>::Max(), Count); Previous.Init(INDEX_NONE, Count); Visited.Init(false, Count);
			Start = Graph.SeedVertices.IsValidIndex(0) ? Graph.SeedVertices[0] : INDEX_NONE;
			Destination = Graph.SeedVertices.IsValidIndex(1) ? Graph.SeedVertices[1] : INDEX_NONE;
			if (Start != INDEX_NONE) Distances[Start] = 0;
		}
		// Works until FPlatformTime::Seconds() reaches Deadline; true once the search is over.
		bool Step(double Deadline)
		{
			const TArray<FVertex>& Vertices = Graph.Vertices;
			for (bool bFirst = true; !bDone; bFirst = false)
			{
				if (!bFirst && FPlatformTime::Seconds() >= Deadline) return false;
				int32 Current = INDEX_NONE;
				for (int32 I = 0; I < Vertices.Num(); ++I) if (!Visited[I] && (Current == INDEX_NONE || Distances[I] < Distances[Current])) Current = I;
				if (Steps >= Vertices.Num() || Current == INDEX_NONE || Distances[Current] == TNumericLimits<float>::Max() || Current == Destination) { bDone = true; break; }
				Visited[Current] = true;
				for (const auto& Edge : Graph.Edges[Current]) if (!Visited[Edge.To] && Distances[Current] + Edge.Cost < Distances[Edge.To])
				{ Distances[Edge.To] = Distances[Current] + Edge.Cost; Previous[Edge.To] = Current; }
				++Steps;
			}
			return true;
		}
		float GetProgress() const { return Graph.Vertices.Num() > 0 ? FMath::Min(1.f, static_cast<float>(Steps) / Graph.Vertices.Num()) : 1.f; }
		int32 GetSteps() const { return Steps; }
		// The length of the shortest route to the destination once the search is over; the largest float when there is none.
		float GetCost() const { return Destination != INDEX_NONE ? Distances[Destination] : TNumericLimits<float>::Max(); }
		// The route when there is one; otherwise false, with the reason in Result.Error.
		bool Finish(FNavigationRouteResult& Result) const
		{
			if (Destination == INDEX_NONE || Distances[Destination] == TNumericLimits<float>::Max()) { Result.Error = TEXT("No route found. Check main/link entrances, room doors, and floor connectors."); return false; }
			TArray<int32> Reverse;
			for (int32 I = Destination; I != INDEX_NONE; I = Previous[I]) Reverse.Add(I);
			for (int32 I = Reverse.Num() - 1; I >= 0; --I) Result.Points.Add(Graph.Vertices[Reverse[I]].Point);
			// A visibility edge may cross a door without using its centre as a graph vertex. Keep that
			// landmark for guidance by inserting a collinear point; the walkable path remains identical.
			TArray<FNavigationRoutePoint> WithDoors;
			if (!Result.Points.IsEmpty()) WithDoors.Add(Result.Points[0]);
			for (int32 I = 1; I < Result.Points.Num(); ++I)
			{
				const auto& A = Result.Points[I - 1]; auto B = Result.Points[I];
				TArray<double> Crossings;
				if (A.Floor > 0 && A.Floor == B.Floor && A.BuildingId == B.BuildingId)
				{
					const FVector2D Direction = B.Position - A.Position;
					for (const auto& Door : Graph.Doorways)
					{
						if (Door.Floor != A.Floor || Door.BuildingId != A.BuildingId) continue;
						const FVector2D Along = Door.Opening.B - Door.Opening.A, Offset = Door.Opening.A - A.Position;
						const double Denominator = FVector2D::CrossProduct(Direction, Along);
						if (FMath::Abs(Denominator) <= GeometryEpsilon) continue;
						const double T = FVector2D::CrossProduct(Offset, Along) / Denominator;
						const double U = FVector2D::CrossProduct(Offset, Direction) / Denominator;
						if (T < -GeometryEpsilon || T > 1 + GeometryEpsilon || U < 0 || U > 1) continue;
						if (T <= GeometryEpsilon) WithDoors.Last().bIsDoorway = true;
						else if (T >= 1 - GeometryEpsilon) B.bIsDoorway = true;
						else Crossings.Add(T);
					}
				}
				Crossings.Sort(); double PreviousT = -1;
				for (const double T : Crossings)
				{
					if (FMath::IsNearlyEqual(T, PreviousT, GeometryEpsilon)) continue;
					PreviousT = T; auto P = A;
					P.Position = FMath::Lerp(A.Position, B.Position, T); P.WorldPosition = FMath::Lerp(A.WorldPosition, B.WorldPosition, T);
					P.ConnectorId.Reset(); P.ConnectorType = EInteriorNavNodeType::PathNode; P.bIsDoorway = true;
					WithDoors.Add(MoveTemp(P));
				}
				WithDoors.Add(MoveTemp(B));
			}
			Result.Points = MoveTemp(WithDoors);
			return true;
		}

	private:
		const FCampusGraph& Graph;
		TArray<float> Distances;
		TArray<int32> Previous;
		TArray<bool> Visited;
		int32 Steps = 0;
		bool bDone = false;
		// The start and the destination: the seeds of the graph, not necessarily its first vertices.
		int32 Start = INDEX_NONE;
		int32 Destination = INDEX_NONE;
	};

	// The start and the destination are the graph's first two seeds, unless one of them is not a usable point.
	bool BothEndsFound(const FCampusGraph& Graph, FNavigationRouteResult& Result)
	{
		if (Graph.SeedVertices[0] != INDEX_NONE && Graph.SeedVertices[1] != INDEX_NONE) return true;
		Result.Error = TEXT("A selected point is no longer reachable. Check its building, floor, and wall clearance, then select it again.");
		return false;
	}

	bool SolveCampus(const TArray<FRouteBuilding>& Models, float Scale, FNavigationRoutePoint Start,
		FNavigationRoutePoint Destination, FNavigationRouteResult& Result)
	{
		Result = {};
		FCampusGraphBuilder Builder(Models, Scale);
		if (!Builder.Begin({ Start, Destination }, Result.Error)) return false;
		Builder.Step(TNumericLimits<double>::Max());
		if (!BothEndsFound(Builder.Out, Result)) return false;
		FRouteSolver Solver(Builder.Out);
		Solver.Step(TNumericLimits<double>::Max());
		return Solver.Finish(Result);
	}
}

int32 FNavigationRoutePlanner::GetGraphCacheHits()
{
	FGraphCache& Cache = GraphCache();
	FScopeLock Guard(&Cache.Lock);
	return Cache.Hits;
}

int32 FNavigationRoutePlanner::GetGraphCacheMisses()
{
	FGraphCache& Cache = GraphCache();
	FScopeLock Guard(&Cache.Lock);
	return Cache.Misses;
}

void FNavigationRoutePlanner::ClearGraphCache()
{
	FGraphCache& Cache = GraphCache();
	FScopeLock Guard(&Cache.Lock);
	Cache.Entries.Reset();
	Cache.Hits = 0;
	Cache.Misses = 0;
}

bool FNavigationRoutePlanner::FindRoute(const APolyBuilding& Building, const AInteriorGenerator& Interior,
	float Scale, int32 StartFloor, FVector2D Start, int32 DestinationFloor, FVector2D Destination, FNavigationRouteResult& OutResult)
{
	FNavigationRoutePoint A, B;
	A.Position = Start; A.Floor = StartFloor; A.BuildingId = Building.BuildingId;
	B.Position = Destination; B.Floor = DestinationFloor; B.BuildingId = Building.BuildingId;
	if (Scale <= KINDA_SMALL_NUMBER) { OutResult = {}; OutResult.Error = TEXT("The building scale is invalid."); return false; }
	return SolveCampus({ ReadRouteBuilding(Building, Interior, nullptr, Interior.GetAllEntrances(), Scale, true) }, Scale, A, B, OutResult);
}

namespace
{
	// The buildings of the campus as the route search sees them. The floors of a building are read when it has doors in its outer wall or an
	// interior (saved with it, or the one open now), and of every building when bAllFloors says so. That does not depend on where a route
	// starts and ends: the graph made from these, and its place in the cache, are the same for every leg of a trip. (A leg that ended in another
	// building used to make a graph of its own, so a trip through four buildings made four.)
	TArray<FRouteBuilding> ReadCampusModels(const ABuildingGenerator& Generator, const AInteriorGenerator& Interior, bool bAllFloors)
	{
		const float Scale = Generator.GetScale();
		const auto Buildings = Generator.GetAllBuildings();
		TMap<FString, TArray<FInteriorEntrance>> Entrances;
		for (const auto* Building : Buildings) if (IsValid(Building)) Entrances.Add(Building->BuildingId,
			Building->BuildingId == Interior.GetLoadedBuildingId() ? Interior.GetEntrances() : Generator.GetStoredEntrances(Building->BuildingId));
		// Generate each mirror once. In particular, use live unsaved source entrances.
		for (const auto* Building : Buildings) if (IsValid(Building))
		{
			const auto Own = Entrances.FindChecked(Building->BuildingId);
			for (const auto& Door : Own) if (!Door.bMirrored && Door.Kind == EInteriorEntranceKind::Link && Door.ConnectsTo != Building->BuildingId)
			{
				FInteriorEntrance Mirror;
				if (auto* Target = Entrances.Find(Door.ConnectsTo); Target && Generator.MakeMirrorOfEntrance(Building->BuildingId, Door, Door.ConnectsTo, Mirror)) Target->Add(Mirror);
			}
		}
		TArray<FRouteBuilding> Models;
		for (const auto* Building : Buildings) if (IsValid(Building))
		{
			const auto& Doors = Entrances.FindChecked(Building->BuildingId);
			const bool bHasInterior = Building->BuildingId == Interior.GetLoadedBuildingId() || Generator.GetInteriorDataForBuilding(Building->BuildingId).IsValid();
			Models.Add(ReadRouteBuilding(*Building, Interior, Generator.GetInteriorDataForBuilding(Building->BuildingId), Doors, Scale,
				bAllFloors || !Doors.IsEmpty() || bHasInterior));
		}
		return Models;
	}
}

bool FNavigationRoutePlanner::FindCampusRoute(const ABuildingGenerator& Generator, const AInteriorGenerator& Interior,
	const FNavigationRoutePoint& Start, const FNavigationRoutePoint& Destination, FNavigationRouteResult& OutResult,
	const FNavigationRoutePreferences& Preferences)
{
	FNavigationRouteJob Job(Generator, Interior, Start, Destination, Preferences);
	Job.Step(TNumericLimits<double>::Max());
	OutResult = Job.GetResult();
	return Job.Succeeded();
}

bool FNavigationRoutePlanner::FindRoomDestination(const ABuildingGenerator& Generator, const AInteriorGenerator& Interior,
	const FString& BuildingId, int32 Floor, const TArray<FVector2D>& Polygon, FVector2D& OutPosition, const FVector2D* PreferredPosition)
{
	const auto* Building = Generator.FindBuildingById(BuildingId);
	if (!Building || Polygon.Num() < 3 || Generator.GetScale() <= KINDA_SMALL_NUMBER) return false;
	const auto Model = ReadRouteBuilding(*Building, Interior, Generator.GetInteriorDataForBuilding(BuildingId),
		Generator.GetStoredEntrances(BuildingId), Generator.GetScale(), true, Floor);
	const auto* Geometry = Model.Floors.Find(Floor);
	if (Geometry && PreferredPosition && InsideOrOnBoundary(*PreferredPosition, Polygon)
		&& IsPointClearOfWalls(*PreferredPosition, Building->GetFootprint(), *Geometry))
	{
		OutPosition = *PreferredPosition;
		return true;
	}
	return Geometry && FindClearRoomPoint(Polygon, Building->GetFootprint(), *Geometry, OutPosition);
}

namespace
{
	enum class ECheckPhase : uint8 { Read, Vertices, Joining, Finish, Done };
}

// What the check keeps between slices. Only the Read part looks at live editor data; the rest works on the copies in Models.
struct FNavigationLayoutCheck::FImpl
{
	struct FRoomProbe { FString Building, Room; int32 Floor, Seed; FVector2D Position; };

	TWeakObjectPtr<const ABuildingGenerator> LiveGenerator;
	TWeakObjectPtr<const AInteriorGenerator> LiveInterior;
	FString ScopeBuildingId;
	float Scale = 1.f;
	ECheckPhase Phase = ECheckPhase::Read;
	TArray<FRouteBuilding> Models;
	TArray<TWeakObjectPtr<const APolyBuilding>> Buildings;
	TArray<FRoomProbe> Probes;
	TArray<FNavigationRoutePoint> Seeds;
	TUniquePtr<FCampusGraphBuilder> Builder;

	static void AddIssue(LayoutValidation::FReport& Report, const TCHAR* Rule, LayoutValidation::ESeverity Severity, const FString& Building, int32 Floor,
		LayoutValidation::EObjectKind Kind, const FString& Id, const FString& Message, const FString& Suggestion, FVector2D Position)
	{
		LayoutValidation::FIssue Issue; Issue.Rule = Rule; Issue.Severity = Severity; Issue.BuildingId = Building; Issue.Floor = Floor;
		Issue.Kind = Kind; Issue.ObjectId = Id; Issue.Message = Message; Issue.Suggestion = Suggestion;
		Issue.Position = Position; Issue.bHasPosition = true; Report.Issues.Add(MoveTemp(Issue));
	}

	void NoEntryPoints(LayoutValidation::FReport& Report) const
	{
		LayoutValidation::FIssue Issue; Issue.Rule = TEXT("NO_ENTRY_POINTS"); Issue.Severity = LayoutValidation::ESeverity::Warning;
		Issue.Message = TEXT("Entrance-based reachability was not checked: the campus has no usable exterior entry points.");
		Issue.Suggestion = TEXT("Add a main entrance or a supported ground-floor outside door, then validate again.");
		Report.Issues.Add(MoveTemp(Issue)); Report.RoomsSkipped += Probes.Num();
	}

	bool IsStillThere() const
	{
		for (const auto& Building : Buildings) if (!Building.IsValid()) return false;
		return LiveGenerator.IsValid() && LiveInterior.IsValid();
	}

	void Read(LayoutValidation::FReport& Report);
	void MakeVertices(LayoutValidation::FReport& Report);
	void Finish(LayoutValidation::FReport& Report);
};

FNavigationLayoutCheck::FNavigationLayoutCheck(const ABuildingGenerator& Generator, const AInteriorGenerator& Interior, const FString& ScopeBuildingId)
	: Impl(MakeUnique<FImpl>())
{
	Impl->LiveGenerator = &Generator; Impl->LiveInterior = &Interior; Impl->ScopeBuildingId = ScopeBuildingId; Impl->Scale = Generator.GetScale();
}

FNavigationLayoutCheck::~FNavigationLayoutCheck() = default;

void FNavigationLayoutCheck::FImpl::Read(LayoutValidation::FReport& Report)
{
	using namespace LayoutValidation;
	const ABuildingGenerator& Generator = *LiveGenerator.Get();
	const AInteriorGenerator& Interior = *LiveInterior.Get();
	Models = ReadCampusModels(Generator, Interior, true);
	for (const auto& Model : Models) Buildings.Add(Model.Building);
	// Shell-only floors must not become invented walkable interiors during validation.
	for (auto& Model : Models)
	{
		const auto Data = Interior.GetLoadedBuildingId() == Model.Building->BuildingId
			? Interior.CaptureInteriorSnapshot() : Generator.GetInteriorDataForBuilding(Model.Building->BuildingId);
		TSet<int32> Authored;
		const TArray<TSharedPtr<FJsonValue>>* Floors = nullptr;
		if (Data && Data->TryGetArrayField(TEXT("floors"), Floors)) for (const auto& Value : *Floors)
		{
			int32 Level = 1;
			Value->AsObject()->TryGetNumberField(TEXT("level"), Level); Authored.Add(Level);
		}
		for (auto It = Model.Floors.CreateIterator(); It; ++It) if (!Authored.Contains(It.Key())) It.RemoveCurrent();
	}
	for (const auto& Model : Models)
	{
		if (!ScopeBuildingId.IsEmpty() && Model.Building->BuildingId != ScopeBuildingId) continue;
		for (const auto& Pair : Model.Rooms)
		{
			const auto* Geometry = Model.Floors.Find(Pair.Key); if (!Geometry) continue;
			for (const auto& Room : Pair.Value)
			{
				if (!Room.bClosed) continue;
				if (Room.bRestricted)
				{
					++Report.RoomsSkipped;
					continue;
				}
				FVector2D Position;
				if (FindClearRoomPoint(Room.Footprint, Model.Building->GetFootprint(), *Geometry, Position))
				{
					FNavigationRoutePoint Point; Point.BuildingId = Model.Building->BuildingId; Point.Floor = Pair.Key; Point.Position = Position;
					Probes.Add({ Point.BuildingId, Room.RoomId, Pair.Key, Seeds.Add(Point), Position });
				}
				else
				{
					++Report.RoomsSkipped;
					AddIssue(Report, TEXT("ROOM_PROBE_UNAVAILABLE"), ESeverity::Warning, Model.Building->BuildingId, Pair.Key, EObjectKind::Room, Room.RoomId,
						TEXT("No destination point with the planner's wall clearance could be established in this room."),
						TEXT("Review room size, internal walls and clearance. Reachability for this room was not checked."), Room.Footprint.IsEmpty() ? FVector2D::ZeroVector : Room.Footprint[0]);
				}
			}
		}
	}
	// With no way in from outside and no building link, the graph could not change the answer, and building it is the slow part.
	bool bAnyWayIn = false;
	for (const auto& Model : Models)
	{
		for (const auto& Floor : Model.Floors) bAnyWayIn |= !Floor.Value.ExteriorExits.IsEmpty();
		for (const auto& Door : Model.Entrances) bAnyWayIn |= Door.Kind == EInteriorEntranceKind::Link;
	}
	if (!bAnyWayIn && FMath::IsFinite(Scale) && Scale > KINDA_SMALL_NUMBER) { NoEntryPoints(Report); Phase = ECheckPhase::Done; return; }
	Phase = ECheckPhase::Vertices;
}

void FNavigationLayoutCheck::FImpl::MakeVertices(LayoutValidation::FReport& Report)
{
	Builder = MakeUnique<FCampusGraphBuilder>(Models, Scale);
	FString Error;
	if (Builder->Begin(Seeds, Error)) { Phase = ECheckPhase::Joining; return; }
	LayoutValidation::FIssue Issue; Issue.Rule = TEXT("NAVIGATION_SKIPPED"); Issue.Severity = LayoutValidation::ESeverity::Warning; Issue.Message = Error;
	Report.Issues.Add(MoveTemp(Issue)); Report.RoomsSkipped += Probes.Num();
	Phase = ECheckPhase::Done;
}

void FNavigationLayoutCheck::FImpl::Finish(LayoutValidation::FReport& Report)
{
	using namespace LayoutValidation;
	Phase = ECheckPhase::Done;
	const FCampusGraph& Graph = Builder->Out;
	TArray<bool> Reachable; Reachable.Init(false, Graph.Vertices.Num());
	TArray<int32> Queue;
	for (int32 I = 0; I < Graph.Vertices.Num(); ++I)
	{
		const auto& Point = Graph.Vertices[I].Point;
		if (Point.Floor == 0 && IsExteriorExitVertex(Graph.Vertices[I])) { Reachable[I] = true; Queue.Add(I); }
	}
	// Also inspect authored links when no main entrance exists: a valid link is stored on only one side.
	for (const auto& Model : Models) for (const auto& Door : Model.Entrances)
	{
		if (Door.bMirrored || Door.Kind != EInteriorEntranceKind::Link || (!ScopeBuildingId.IsEmpty() && Model.Building->BuildingId != ScopeBuildingId)) continue;
		const FString Id = LinkPortalId(Model.Building->BuildingId, Door.Id);
		bool bConnected = false;
		for (int32 I = 0; I < Graph.Vertices.Num(); ++I)
		{
			const auto& P = Graph.Vertices[I].Point;
			if (Graph.Vertices[I].Portal != EPortal::BuildingLink || P.BuildingId != Model.Building->BuildingId || P.ConnectorId != Id) continue;
			for (const auto& Edge : Graph.Edges[I]) bConnected |= Graph.Vertices[Edge.To].Point.BuildingId == Door.ConnectsTo;
		}
		if (!bConnected)
		{
			FVector2D Position = FVector2D::ZeroVector; EntrancePosition(Model, Door, Scale, Position);
			AddIssue(Report, TEXT("LINK_UNUSABLE"), ESeverity::Error, Model.Building->BuildingId, Door.Level, EObjectKind::Entrance, Door.Id,
				TEXT("The route planner cannot join this link entrance to the target building."),
				TEXT("Check target floors, floor elevations, wall proximity and whether the mirrored opening fits."), Position);
		}
	}
	if (Queue.IsEmpty()) { NoEntryPoints(Report); return; }
	for (int32 Head = 0; Head < Queue.Num(); ++Head) for (const auto& Edge : Graph.Edges[Queue[Head]])
	{
		if (!Reachable[Edge.To]) { Reachable[Edge.To] = true; Queue.Add(Edge.To); }
	}
	Report.bNavigationChecked = true;
	for (const auto& Probe : Probes)
	{
		const int32 Vertex = Graph.SeedVertices[Probe.Seed];
		if (Vertex == INDEX_NONE) { ++Report.RoomsSkipped; continue; }
		++Report.RoomsChecked;
		if (!Reachable[Vertex]) AddIssue(Report, TEXT("ROOM_UNREACHABLE"), ESeverity::Error, Probe.Building, Probe.Floor, EObjectKind::Room, Probe.Room,
			TEXT("No route reaches this room's tested destination from any campus entrance."),
			TEXT("Check room openings, corridor walls, floor connectors and main/link entrances."), Probe.Position);
	}
	for (const auto& Model : Models) for (const auto& Node : Model.Nodes)
	{
		if (!ScopeBuildingId.IsEmpty() && Model.Building->BuildingId != ScopeBuildingId) continue;
		const auto* Geometry = Model.Floors.Find(Node.Level);
		if (!Geometry || !IsPointClearOfWalls(Node.Position, Model.Building->GetFootprint(), *Geometry))
			AddIssue(Report, TEXT("CONNECTOR_CLEARANCE"), ESeverity::Error, Model.Building->BuildingId, Node.Level, EObjectKind::Connector,
				Node.ConnectorGroupId.IsEmpty() ? Node.Id : Node.ConnectorGroupId, TEXT("Navigation node is not clear of the floor's walls."),
				TEXT("Move the node away from walls using the existing connector tools."), Node.Position);
	}
}

bool FNavigationLayoutCheck::Step(double BudgetSeconds, LayoutValidation::FReport& Report)
{
	FImpl& State = *Impl;
	const double Deadline = FPlatformTime::Seconds() + BudgetSeconds;
	while (State.Phase != ECheckPhase::Done)
	{
		// The copies in Models point at buildings; if the project changed under the check, stop rather than read what is gone.
		if (!State.IsStillThere())
		{
			LayoutValidation::FIssue Issue; Issue.Rule = TEXT("NAVIGATION_SKIPPED"); Issue.Severity = LayoutValidation::ESeverity::Warning;
			Issue.Message = TEXT("Navigation was not checked because the project was changed or closed while validating.");
			Issue.Suggestion = TEXT("Validate again.");
			Report.Issues.Add(MoveTemp(Issue)); State.Phase = ECheckPhase::Done;
			break;
		}
		switch (State.Phase)
		{
		case ECheckPhase::Read: State.Read(Report); break;
		case ECheckPhase::Vertices: State.MakeVertices(Report); break;
		case ECheckPhase::Joining: if (State.Builder->Step(Deadline)) State.Phase = ECheckPhase::Finish; break;
		case ECheckPhase::Finish: State.Finish(Report); break;
		default: break;
		}
		if (FPlatformTime::Seconds() >= Deadline) break;
	}
	return State.Phase == ECheckPhase::Done;
}

float FNavigationLayoutCheck::GetProgress() const
{
	switch (Impl->Phase)
	{
	case ECheckPhase::Read: return 0.f;
	case ECheckPhase::Vertices: return 0.05f;
	case ECheckPhase::Joining: return 0.1f + 0.85f * Impl->Builder->GetProgress();
	case ECheckPhase::Finish: return 0.95f;
	default: return 1.f;
	}
}

FString FNavigationLayoutCheck::GetPhase() const
{
	switch (Impl->Phase)
	{
	case ECheckPhase::Read: return TEXT("Reading the buildings");
	case ECheckPhase::Vertices: return TEXT("Preparing the routes");
	case ECheckPhase::Joining: return TEXT("Checking the routes");
	case ECheckPhase::Finish: return TEXT("Checking which rooms can be reached");
	default: return TEXT("Done");
	}
}

void FNavigationRoutePlanner::ValidateLayoutConnectivity(const ABuildingGenerator& Generator, const AInteriorGenerator& Interior,
	const FString& ScopeBuildingId, LayoutValidation::FReport& Report)
{
	FNavigationLayoutCheck Check(Generator, Interior, ScopeBuildingId);
	Check.Step(TNumericLimits<double>::Max(), Report);
}

namespace
{
	enum class ERoutePhase : uint8 { Read, Vertices, Joining, Solving, Done };
}

// What the route search keeps between slices. As with the layout check, only the Read part looks at live editor data.
struct FNavigationRouteJob::FImpl
{
	TWeakObjectPtr<const ABuildingGenerator> LiveGenerator;
	TWeakObjectPtr<const AInteriorGenerator> LiveInterior;
	FNavigationRoutePoint Start, Destination;
	FNavigationRoutePreferences Preferences;
	uint64 BuildingRevision = 0, InteriorRevision = 0;
	float Scale = 1.f;
	ERoutePhase Phase = ERoutePhase::Read;
	TArray<FRouteBuilding> Models;
	TArray<TWeakObjectPtr<const APolyBuilding>> Buildings;
	TUniquePtr<FCampusGraphBuilder> Builder;
	TUniquePtr<FRouteSolver> Solver;
	FNavigationRouteResult Result;
	bool bFound = false;

	// A trip within one building is first searched with only that building and nothing outside it: the outside is where
	// nearly all the work of a large campus goes. That answer is kept only if it is shorter than anything that goes out
	// and in again could be (OutsideBound); otherwise the search is made again with everything, as before.
	TArray<FRouteBuilding> IndoorModels;
	bool bIndoorOnly = false;
	double OutsideBound = TNumericLimits<double>::Max();
	mutable float ShownProgress = 0.f;

	// For the log line that ends a search. The search is spread over frames, so the time since it was started (Wall) is longer than the
	// time it worked (PhaseSeconds, by phase); a search that is made again with the whole campus has two graphs.
	double StartedAt = 0.0;
	double PhaseSeconds[5] = {};
	int32 Slices = 0;
	int32 Graphs = 0;
	FString GraphNotes;
	bool bLogged = false;

	void NoteGraph()
	{
		const FCampusGraph& Graph = Builder->Out;
		int32 Edges = 0;
		for (const TArray<FRouteGraphEdge>& List : Graph.Edges) Edges += List.Num();
		++Graphs;
		GraphNotes += FString::Printf(TEXT("\n    graph %d (%s): %d vertices (%d of the buildings), %d edges, %s; joined %.2f s, start and destination %.2f s"),
			Graphs, bIndoorOnly ? TEXT("this building only") : TEXT("whole campus"), Graph.Vertices.Num(), Graph.StaticCount, Edges / 2,
			Builder->bCacheHit ? TEXT("the buildings were found ready") : TEXT("the buildings were joined now"), Builder->StaticJoinSeconds, Builder->SeedJoinSeconds);
	}

	void LogTimings() const
	{
		const double Wall = FPlatformTime::Seconds() - StartedAt;
		double Worked = 0.0;
		for (const double Seconds : PhaseSeconds) Worked += Seconds;
		UE_LOG(LogTemp, Display, TEXT("[RouteTiming] %s L%d to %s L%d: %s after %.2f s (worked %.2f s: reading %.2f, vertices %.2f, joining %.2f, search %.2f; %d slices, %d search steps)%s"),
			*Start.BuildingId, Start.Floor, *Destination.BuildingId, Destination.Floor, bFound ? TEXT("found") : TEXT("not found"), Wall, Worked,
			PhaseSeconds[static_cast<int32>(ERoutePhase::Read)], PhaseSeconds[static_cast<int32>(ERoutePhase::Vertices)],
			PhaseSeconds[static_cast<int32>(ERoutePhase::Joining)], PhaseSeconds[static_cast<int32>(ERoutePhase::Solving)],
			Slices, Solver ? Solver->GetSteps() : 0, *GraphNotes);
	}

	bool IsStillThere() const
	{
		for (const auto& Building : Buildings) if (!Building.IsValid()) return false;
		return LiveGenerator.IsValid() && LiveInterior.IsValid();
	}

	// Sets up the search without the outside when the trip is within one building and nothing but its own main doors leads out.
	void PrepareIndoorOnly()
	{
		if (Start.Floor == UNavigationPanelWidget::OutdoorFloor || Destination.Floor == UNavigationPanelWidget::OutdoorFloor
			|| Start.BuildingId != Destination.BuildingId) return;
		const FRouteBuilding* Own = Models.FindByPredicate([&](const FRouteBuilding& M) { return M.Building->BuildingId == Start.BuildingId; });
		// A link to another building is another way out (and in), which the bound below does not cover.
		if (!Own || Own->Entrances.ContainsByPredicate([](const FInteriorEntrance& Door) { return Door.Kind == EInteriorEntranceKind::Link; })) return;
		// Going out and coming back costs at least the straight distances from the start to a door and from a door to the
		// destination, the two steps between the floor and the ground (5 each), and the lift or stairs between the ground
		// floor (where the doors are) and each of the start and the destination that is on another floor.
		double FromStart = TNumericLimits<double>::Max(), ToDestination = TNumericLimits<double>::Max();
		for (const auto& Floor : Own->Floors) for (const auto& Exit : Floor.Value.ExteriorExits)
		{
			FromStart = FMath::Min(FromStart, FVector2D::Distance(Start.Position, Exit.Position));
			ToDestination = FMath::Min(ToDestination, FVector2D::Distance(Exit.Position, Destination.Position));
		}
		auto ToGroundFloor = [&](int32 Floor) { return Floor == 1 ? 0.0 : 20.0 + (Floor - 1) * Own->Building->GetFloorHeight() / Scale; };
		OutsideBound = FromStart < TNumericLimits<double>::Max()
			? 10.0 + FromStart + ToDestination + ToGroundFloor(Start.Floor) + ToGroundFloor(Destination.Floor) : TNumericLimits<double>::Max();
		IndoorModels.Reset();
		IndoorModels.Add(*Own);
		for (auto& Floor : IndoorModels[0].Floors) Floor.Value.ExteriorExits.Reset();
		bIndoorOnly = true;
	}
};

FNavigationRouteJob::FNavigationRouteJob(const ABuildingGenerator& Generator, const AInteriorGenerator& Interior,
	const FNavigationRoutePoint& Start, const FNavigationRoutePoint& Destination, const FNavigationRoutePreferences& Preferences)
	: Impl(MakeUnique<FImpl>())
{
	Impl->LiveGenerator = &Generator; Impl->LiveInterior = &Interior; Impl->Start = Start; Impl->Destination = Destination;
	Impl->Preferences = Preferences; Impl->Scale = Generator.GetScale();
	Impl->BuildingRevision = Generator.GetNavigationRevision(); Impl->InteriorRevision = Interior.GetNavigationRevision();
	Impl->StartedAt = FPlatformTime::Seconds();
}

FNavigationRouteJob::~FNavigationRouteJob() = default;

bool FNavigationRouteJob::Step(double BudgetSeconds)
{
	FImpl& State = *Impl;
	const double Deadline = FPlatformTime::Seconds() + BudgetSeconds;
	++State.Slices;
	while (State.Phase != ERoutePhase::Done)
	{
		// The copies in Models point at buildings; if the project changed under the search, stop rather than read what is gone.
		if (!State.IsStillThere() || State.LiveGenerator->GetNavigationRevision() != State.BuildingRevision
			|| State.LiveInterior->GetNavigationRevision() != State.InteriorRevision)
		{
			State.Result = {};
			State.Result.Error = TEXT("The route could not be found because the project was changed or closed meanwhile. Find the route again.");
			State.bFound = false; State.Phase = ERoutePhase::Done;
			break;
		}
		const ERoutePhase Running = State.Phase;
		const double PhaseStarted = FPlatformTime::Seconds();
		switch (State.Phase)
		{
		case ERoutePhase::Read:
		{
			if (State.Scale <= KINDA_SMALL_NUMBER) { State.Result.Error = TEXT("The project scale is invalid."); State.Phase = ERoutePhase::Done; break; }
			State.Models = ReadCampusModels(*State.LiveGenerator.Get(), *State.LiveInterior.Get(), false);
			for (auto& Model : State.Models) Model.Nodes.RemoveAll([&](const FInteriorNavNode& Node) { return !State.Preferences.Allows(Node.Type); });
			for (const auto& Model : State.Models) State.Buildings.Add(Model.Building);
			State.PrepareIndoorOnly();
			State.Phase = ERoutePhase::Vertices;
			break;
		}
		case ERoutePhase::Vertices:
			State.Builder = MakeUnique<FCampusGraphBuilder>(State.bIndoorOnly ? State.IndoorModels : State.Models, State.Scale);
			State.Phase = State.Builder->Begin({ State.Start, State.Destination }, State.Result.Error) ? ERoutePhase::Joining : ERoutePhase::Done;
			break;
		case ERoutePhase::Joining:
			if (!State.Builder->Step(Deadline)) break;
			State.NoteGraph();
			if (BothEndsFound(State.Builder->Out, State.Result))
			{
				State.Solver = MakeUnique<FRouteSolver>(State.Builder->Out);
				State.Phase = ERoutePhase::Solving;
			}
			else State.Phase = ERoutePhase::Done;
			break;
		case ERoutePhase::Solving:
		{
			if (!State.Solver->Step(Deadline)) break;
			const bool bReached = State.Solver->GetCost() < TNumericLimits<float>::Max();
			const bool bOutsideMayDoBetter = bReached ? State.Solver->GetCost() > State.OutsideBound : State.OutsideBound < TNumericLimits<double>::Max();
			if (State.bIndoorOnly && bOutsideMayDoBetter)
			{
				// Search again with the whole campus.
				State.bIndoorOnly = false; State.IndoorModels.Reset(); State.Builder.Reset(); State.Solver.Reset();
				State.Phase = ERoutePhase::Vertices;
				break;
			}
			State.bFound = State.Solver->Finish(State.Result);
			State.Phase = ERoutePhase::Done;
			break;
		}
		default: break;
		}
		State.PhaseSeconds[static_cast<int32>(Running)] += FPlatformTime::Seconds() - PhaseStarted;
		if (FPlatformTime::Seconds() >= Deadline) break;
	}
	if (State.Phase == ERoutePhase::Done && !State.bLogged)
	{
		State.bLogged = true;
		State.LogTimings();
	}
	return State.Phase == ERoutePhase::Done;
}

bool FNavigationRouteJob::Succeeded() const { return Impl->bFound; }
const FNavigationRouteResult& FNavigationRouteJob::GetResult() const { return Impl->Result; }

float FNavigationRouteJob::GetProgress() const
{
	float Now = 1.f;
	switch (Impl->Phase)
	{
	case ERoutePhase::Read: Now = 0.f; break;
	case ERoutePhase::Vertices: Now = 0.05f; break;
	case ERoutePhase::Joining: Now = 0.1f + 0.8f * Impl->Builder->GetProgress(); break;
	case ERoutePhase::Solving: Now = 0.9f + 0.1f * Impl->Solver->GetProgress(); break;
	default: break;
	}
	// A search made again with the whole campus does not take the bar back.
	Impl->ShownProgress = FMath::Max(Impl->ShownProgress, Now);
	return Impl->ShownProgress;
}

bool FNavigationRouteJob::WasIndoorOnly() const { return Impl->bIndoorOnly; }

FString FNavigationRouteJob::GetPhase() const
{
	switch (Impl->Phase)
	{
	case ERoutePhase::Read: return TEXT("Reading the buildings");
	case ERoutePhase::Vertices: return TEXT("Preparing the routes");
	case ERoutePhase::Joining: return TEXT("Looking for ways between places");
	case ERoutePhase::Solving: return TEXT("Choosing the shortest route");
	default: return TEXT("Done");
	}
}
