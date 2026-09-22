#include "NavigationRoutePlanner.h"

#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "PolyBuilding.h"

namespace
{
	constexpr double GeometryEpsilon = 0.0001;
	constexpr double SelectionWallClearance = 1.0; // footprint units; avoid selecting a wall line

	struct FSegment
	{
		FVector2D A;
		FVector2D B;
	};

	struct FFloorGeometry
	{
		TArray<FSegment> Walls;
		TArray<FVector2D> DoorPoints;
		TArray<FVector2D> TurnPoints;
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

	bool SegmentsCrossAwayFromEnds(const FVector2D& A, const FVector2D& B,
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

		// A line may turn at a wall endpoint, but cannot follow a solid wall or touch its middle.
		if (FMath::Abs(C1) <= GeometryEpsilon && FMath::Abs(C2) <= GeometryEpsilon)
		{
			const FVector2D Direction = B - A;
			const double LengthSquared = Direction.SizeSquared();
			if (LengthSquared > GeometryEpsilon)
			{
				const double T1 = FVector2D::DotProduct(C - A, Direction) / LengthSquared;
				const double T2 = FVector2D::DotProduct(D - A, Direction) / LengthSquared;
				return FMath::Min(T1, T2) < 1.0 - GeometryEpsilon
					&& FMath::Max(T1, T2) > GeometryEpsilon;
			}
		}

		return (OnSegment(C, A, B) && !C.Equals(A, GeometryEpsilon) && !C.Equals(B, GeometryEpsilon))
			|| (OnSegment(D, A, B) && !D.Equals(A, GeometryEpsilon) && !D.Equals(B, GeometryEpsilon))
			|| (OnSegment(A, C, D) && !A.Equals(C, GeometryEpsilon) && !A.Equals(D, GeometryEpsilon))
			|| (OnSegment(B, C, D) && !B.Equals(C, GeometryEpsilon) && !B.Equals(D, GeometryEpsilon));
	}

	void AddWall(FFloorGeometry& Geometry, FVector2D A, FVector2D B)
	{
		if (!A.Equals(B, GeometryEpsilon))
		{
			Geometry.Walls.Add({ A, B });
		}
	}

	FFloorGeometry MakeFloorGeometry(const AInteriorGenerator& Interior, float Scale, int32 Floor)
	{
		FFloorGeometry Geometry;
		for (const AInteriorRoom* Room : Interior.GetRoomsOnFloor(Floor))
		{
			if (!Room)
			{
				continue;
			}
			const TArray<FVector2D>& Footprint = Room->GetFootprint();
			if (Room->IsClosedLoop() && Footprint.Num() >= 3)
			{
				FVector2D Center = FVector2D::ZeroVector;
				for (const FVector2D& Corner : Footprint)
				{
					Center += Corner;
				}
				Center /= Footprint.Num();
				for (const FVector2D& Corner : Footprint)
				{
					// Keep turns outside closed rooms. A wall corner itself would let a path
					// change from the room interior to the exterior without using a door.
					Geometry.TurnPoints.Add(Corner + (Corner - Center).GetSafeNormal() * SelectionWallClearance);
				}
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
						Geometry.DoorPoints.Add(A + Direction * ((Begin + End) * 0.5));
					}
				}
				Gaps.Sort([](const TPair<double, double>& Left, const TPair<double, double>& Right)
				{
				return Left.Key < Right.Key;
				});

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
			if (!Room->IsClosedLoop())
			{
				Geometry.TurnPoints.Append(Footprint);
			}
		}
		return Geometry;
	}

	bool CanTravel(const FVector2D& A, const FVector2D& B,
		const TArray<FVector2D>& BuildingBoundary, const FFloorGeometry& Geometry)
	{
		for (double T : { 0.25, 0.5, 0.75 })
		{
			if (!InsideOrOnBoundary(FMath::Lerp(A, B, T), BuildingBoundary))
			{
				return false;
			}
		}
		for (int32 I = 0; I < BuildingBoundary.Num(); ++I)
		{
			if (SegmentsCrossAwayFromEnds(A, B, BuildingBoundary[I], BuildingBoundary[(I + 1) % BuildingBoundary.Num()]))
			{
				return false;
			}
		}
		for (const FSegment& Wall : Geometry.Walls)
		{
			if (SegmentsCrossAwayFromEnds(A, B, Wall.A, Wall.B))
			{
				return false;
			}
		}
		return true;
	}

	bool CanUseConnectorBetween(const FVertex& A, const FVertex& B)
	{
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
	if (Scale <= KINDA_SMALL_NUMBER || Floor < 1 || Floor > Building.GetFloorCount())
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

	for (int32 Index = 0; Index < Boundary.Num(); ++Index)
	{
		const FVector2D& A = Boundary[Index];
		const FVector2D& B = Boundary[(Index + 1) % Boundary.Num()];

		const FVector2D Closest =
			FMath::ClosestPointOnSegment2D(Point, A, B);

		if (FVector2D::Distance(Point, Closest) < SelectionWallClearance)
		{
			OutError = TEXT("Select a point away from the building wall.");
			return false;
		}
	}

	const FFloorGeometry Geometry = MakeFloorGeometry(Interior, Scale, Floor);
	for (const FSegment& Wall : Geometry.Walls)
	{
		const FVector2D Closest = FMath::ClosestPointOnSegment2D(Point, Wall.A, Wall.B);
		if (FVector2D::Distance(Point, Closest) < SelectionWallClearance)
		{
			OutError = TEXT("Select a point away from a wall.");
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
			MakeFloorGeometry(Interior, Scale, Floor);

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

		// Add the building boundary corners.
		for (const FVector2D& Corner :
			Building.GetFootprint())
		{
			Vertices.Add(
				{
					{
						Corner,
						Floor,
						FString(),
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
				// Find the geometry for this floor.
				const FFloorGeometry* FloorGeometry =
					Floors.Find(A.Point.Floor);

				if (!FloorGeometry)
				{
					continue;
				}

				// A route is allowed only when the straight line
				// stays inside the building and avoids solid walls.
				if (!CanTravel(
					A.Point.Position,
					B.Point.Position,
					Building.GetFootprint(),
					*FloorGeometry))
				{
					continue;
				}

				// The distance between the points is the movement cost.
				Cost = FVector2D::Distance(
					A.Point.Position,
					B.Point.Position);
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
				Cost =
					20.0f +
					FMath::Abs(
						A.Point.Floor - B.Point.Floor) *
					Building.GetFloorHeight() /
					Scale;
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