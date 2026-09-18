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

	bool SameConnector(const FVertex& A, const FVertex& B)
	{
		if (A.Point.ConnectorType == EInteriorNavNodeType::PathNode
			|| A.Point.ConnectorType != B.Point.ConnectorType
			|| A.Point.Floor == B.Point.Floor)
		{
			return false;
		}
		if (!A.Point.ConnectorId.IsEmpty() && A.Point.ConnectorId == B.Point.ConnectorId)
		{
			return true;
		}
		// Older JSON had no group id. Its per-floor copies share a type, position and floor list.
		return A.Point.ConnectorId.IsEmpty() && B.Point.ConnectorId.IsEmpty()
			&& A.Point.Position.Equals(B.Point.Position, 0.01)
			&& A.ConnectsFloors.Contains(B.Point.Floor)
			&& B.ConnectsFloors.Contains(A.Point.Floor);
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

bool FNavigationRoutePlanner::FindRoute(const APolyBuilding& Building,
	const AInteriorGenerator& Interior, float Scale, int32 StartFloor, FVector2D Start,
	int32 DestinationFloor, FVector2D Destination, FNavigationRouteResult& OutResult)
{
	OutResult = FNavigationRouteResult();
	if (!IsValidPoint(Building, Interior, Scale, StartFloor, Start, OutResult.Error)
		|| !IsValidPoint(Building, Interior, Scale, DestinationFloor, Destination, OutResult.Error))
	{
		return false;
	}

	TMap<int32, FFloorGeometry> Floors;
	TArray<FVertex> Vertices;
	Vertices.Add({ { Start, StartFloor, FString(), EInteriorNavNodeType::PathNode }, {} });
	Vertices.Add({ { Destination, DestinationFloor, FString(), EInteriorNavNodeType::PathNode }, {} });
	for (int32 Floor = 1; Floor <= Building.GetFloorCount(); ++Floor)
	{
		FFloorGeometry Geometry = MakeFloorGeometry(Interior, Scale, Floor);
		for (const FVector2D& Door : Geometry.DoorPoints)
		{
			Vertices.Add({ { Door, Floor, FString(), EInteriorNavNodeType::PathNode }, {} });
		}
		for (const FVector2D& Turn : Geometry.TurnPoints)
		{
			Vertices.Add({ { Turn, Floor, FString(), EInteriorNavNodeType::PathNode }, {} });
		}
		for (const FVector2D& Corner : Building.GetFootprint())
		{
			Vertices.Add({ { Corner, Floor, FString(), EInteriorNavNodeType::PathNode }, {} });
		}
		Floors.Add(Floor, MoveTemp(Geometry));
	}
	for (const FInteriorNavNode& Node : Interior.GetNavNodes())
	{
		if (Node.Level >= 1 && Node.Level <= Building.GetFloorCount())
		{
			Vertices.Add({ { Node.Position, Node.Level, Node.ConnectorGroupId, Node.Type }, Node.ConnectsFloors });
		}
	}

	TArray<TArray<FRouteGraphEdge>> Graph;
	Graph.SetNum(Vertices.Num());
	for (int32 I = 0; I < Vertices.Num(); ++I)
	{
		for (int32 J = I + 1; J < Vertices.Num(); ++J)
		{
			const FVertex& A = Vertices[I];
			const FVertex& B = Vertices[J];
			float Cost = 0.f;
			if (A.Point.Floor == B.Point.Floor)
			{
				if (!CanTravel(A.Point.Position, B.Point.Position, Building.GetFootprint(), Floors[A.Point.Floor]))
				{
					continue;
				}
				Cost = FVector2D::Distance(A.Point.Position, B.Point.Position);
			}
			else if (SameConnector(A, B)
				&& (A.Point.ConnectorType == EInteriorNavNodeType::Elevator
					|| FMath::Abs(A.Point.Floor - B.Point.Floor) == 1))
			{
				Cost = 20.f + FMath::Abs(A.Point.Floor - B.Point.Floor) * Building.GetFloorHeight() / Scale;
			}
			else
			{
				continue;
			}
			Graph[I].Add({ J, Cost });
			Graph[J].Add({ I, Cost });
		}
	}

	TArray<float> Distance;
	TArray<int32> Previous;
	TArray<bool> Visited;
	Distance.Init(TNumericLimits<float>::Max(), Vertices.Num());
	Previous.Init(INDEX_NONE, Vertices.Num());
	Visited.Init(false, Vertices.Num());
	Distance[0] = 0.f;
	for (int32 Step = 0; Step < Vertices.Num(); ++Step)
	{
		int32 Current = INDEX_NONE;
		for (int32 I = 0; I < Vertices.Num(); ++I)
		{
			if (!Visited[I] && (Current == INDEX_NONE || Distance[I] < Distance[Current]))
			{
				Current = I;
			}
		}
		if (Current == INDEX_NONE || Distance[Current] == TNumericLimits<float>::Max() || Current == 1)
		{
			break;
		}
		Visited[Current] = true;
		for (const FRouteGraphEdge& Edge : Graph[Current])
		{
			if (Distance[Current] + Edge.Cost < Distance[Edge.To])
			{
				Distance[Edge.To] = Distance[Current] + Edge.Cost;
				Previous[Edge.To] = Current;
			}
		}
	}

	if (Distance[1] == TNumericLimits<float>::Max())
	{
		OutResult.Error = TEXT("No route found. Add a door or floor connector where needed.");
		return false;
	}
	TArray<int32> Reversed;
	for (int32 Index = 1; Index != INDEX_NONE; Index = Previous[Index])
	{
		Reversed.Add(Index);
	}
	for (int32 I = Reversed.Num() - 1; I >= 0; --I)
	{
		OutResult.Points.Add(Vertices[Reversed[I]].Point);
	}
	return true;
}
