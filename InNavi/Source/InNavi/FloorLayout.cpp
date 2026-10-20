#include "FloorLayout.h"

#include "RoomTypes.h"

#include "Algo/Reverse.h"

namespace
{
	// Sizes in metres.
	constexpr double WallMargin = 0.35;
	constexpr double RoomGap = 0.3;
	constexpr double MinRoomWidth = 3.5;
	constexpr double MaxRoomWidth = 8.0;
	constexpr double MinRoomDepth = 3.0;
	constexpr double MaxRoomDepth = 7.0;
	// The narrowest walkable middle left between rows of rooms.
	constexpr double MinCorridor = 3.4;
	constexpr double PassageSideRoom = 0.6;
	constexpr double CoreLength = 9.0;
	constexpr double CoreDepth = 5.0;
	// Two cores are at least this far apart.
	constexpr double CoreSeparation = 14.0;
	constexpr double FrontClear = 1.0;
	constexpr double ConnectorKeepClear = 0.6;
	constexpr double VoidChance = 0.07;
	// The room nearest a core becomes its toilet (one a core), and how far away it may be.
	constexpr int32 MaxToiletsPerFloor = 3;
	constexpr double ToiletReach = 14.0;

	using FQuad = TArray<FVector2D>;

	double Cross(const FVector2D& O, const FVector2D& A, const FVector2D& B)
	{
		return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X);
	}

	double Cross2(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	double SignedArea(const TArray<FVector2D>& Poly)
	{
		double Sum = 0.0;
		for (int32 i = 0; i < Poly.Num(); ++i)
		{
			Sum += Cross2(Poly[i], Poly[(i + 1) % Poly.Num()]);
		}
		return Sum * 0.5;
	}

	bool PointInPolygon(const FVector2D& P, const TArray<FVector2D>& Poly)
	{
		bool bInside = false;
		for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
		{
			const FVector2D& A = Poly[i];
			const FVector2D& B = Poly[j];
			if ((A.Y > P.Y) != (B.Y > P.Y) && P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	bool SegmentsCross(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
	{
		const double D1 = Cross(A, B, C), D2 = Cross(A, B, D), D3 = Cross(C, D, A), D4 = Cross(C, D, B);
		return ((D1 > 0) != (D2 > 0)) && ((D3 > 0) != (D4 > 0)) && D1 != 0 && D2 != 0 && D3 != 0 && D4 != 0;
	}

	bool PointInConvexQuad(const FVector2D& P, const FQuad& Q)
	{
		bool bPositive = false, bNegative = false;
		for (int32 i = 0; i < Q.Num(); ++i)
		{
			const double C = Cross(Q[i], Q[(i + 1) % Q.Num()], P);
			bPositive = bPositive || C > 1e-9;
			bNegative = bNegative || C < -1e-9;
		}
		return !(bPositive && bNegative);
	}

	// Whether a (convex) quad lies wholly inside the polygon, also for an outline with notches.
	bool QuadInside(const TArray<FVector2D>& Poly, const FQuad& Q)
	{
		for (const FVector2D& Corner : Q)
		{
			if (!PointInPolygon(Corner, Poly))
			{
				return false;
			}
		}
		for (int32 i = 0; i < Poly.Num(); ++i)
		{
			const FVector2D& A = Poly[i];
			const FVector2D& B = Poly[(i + 1) % Poly.Num()];
			for (int32 e = 0; e < Q.Num(); ++e)
			{
				if (SegmentsCross(A, B, Q[e], Q[(e + 1) % Q.Num()]))
				{
					return false;
				}
			}
			// A vertex of the outline poking into the quad (a notch).
			bool bStrictlyInside = true;
			for (int32 e = 0; e < Q.Num(); ++e)
			{
				bStrictlyInside = bStrictlyInside && Cross(Q[e], Q[(e + 1) % Q.Num()], A) * Cross(Q[0], Q[1], Q[2]) > 1e-9;
			}
			if (bStrictlyInside)
			{
				return false;
			}
		}
		return true;
	}

	// Two convex quads overlap if no side of either separates them; a penetration of less than Eps does not count.
	bool QuadsOverlap(const FQuad& A, const FQuad& B, double Eps)
	{
		if (A.Num() < 3 || B.Num() < 3)
		{
			return false;
		}
		for (const FQuad* Shape : { &A, &B })
		{
			for (int32 i = 0; i < Shape->Num(); ++i)
			{
				const FVector2D Edge = (*Shape)[(i + 1) % Shape->Num()] - (*Shape)[i];
				const FVector2D Axis = FVector2D(-Edge.Y, Edge.X).GetSafeNormal();
				double MinA = TNumericLimits<double>::Max(), MaxA = TNumericLimits<double>::Lowest();
				double MinB = TNumericLimits<double>::Max(), MaxB = TNumericLimits<double>::Lowest();
				for (const FVector2D& P : A)
				{
					const double D = FVector2D::DotProduct(P, Axis);
					MinA = FMath::Min(MinA, D);
					MaxA = FMath::Max(MaxA, D);
				}
				for (const FVector2D& P : B)
				{
					const double D = FVector2D::DotProduct(P, Axis);
					MinB = FMath::Min(MinB, D);
					MaxB = FMath::Max(MaxB, D);
				}
				if (MaxA <= MinB + Eps || MaxB <= MinA + Eps)
				{
					return false;
				}
			}
		}
		return true;
	}

	// How far from Origin along Direction before the polygon's boundary; a large number if it never is.
	double RayDistance(const TArray<FVector2D>& Poly, const FVector2D& Origin, const FVector2D& Direction)
	{
		double Best = 1e12;
		for (int32 i = 0; i < Poly.Num(); ++i)
		{
			const FVector2D& A = Poly[i];
			const FVector2D E = Poly[(i + 1) % Poly.Num()] - A;
			const double Denominator = Cross2(Direction, E);
			if (FMath::Abs(Denominator) < 1e-12)
			{
				continue;
			}
			const FVector2D Delta = A - Origin;
			const double T = Cross2(Delta, E) / Denominator;
			const double S = Cross2(Delta, Direction) / Denominator;
			if (T > 1e-9 && S >= 0.0 && S <= 1.0)
			{
				Best = FMath::Min(Best, T);
			}
		}
		return Best;
	}

	// An outline edge's own frame: u along the edge from its start, v straight into the building.
	struct FEdgeFrame
	{
		FVector2D Origin = FVector2D::ZeroVector;
		FVector2D U = FVector2D(1.0, 0.0);
		FVector2D V = FVector2D(0.0, 1.0);
		double Length = 0.0;
		FVector2D At(double u, double v) const { return Origin + U * u + V * v; }
		FQuad Quad(double U0, double U1, double V0, double V1) const { return { At(U0, V0), At(U1, V0), At(U1, V1), At(U0, V1) }; }
	};

	FVector2D Centre(const FQuad& Q)
	{
		FVector2D Sum = FVector2D::ZeroVector;
		for (const FVector2D& P : Q)
		{
			Sum += P;
		}
		return Sum / FMath::Max(Q.Num(), 1);
	}

	struct FPlaced
	{
		FQuad Quad;
		int32 Edge = 0;
		FVector2D Front = FVector2D::ZeroVector;
		double AreaSquareMetres = 0.0;
		bool bToilet = false;
	};

	struct FZone
	{
		FQuad Quad;
		FVector2D Centre = FVector2D::ZeroVector;
		TArray<FFloorConnector> Connectors;
	};

	struct FWeightedType
	{
		const TCHAR* Type;
		int32 Weight;
	};

	// The kinds of room that sit together: teaching rooms, offices and stores, and study areas.
	const TArray<FWeightedType>& Family(int32 Index)
	{
		static const TArray<FWeightedType> Teaching = { { TEXT("Classroom"), 4 }, { TEXT("Lab"), 3 }, { TEXT("Meeting room"), 2 } };
		static const TArray<FWeightedType> Offices = { { TEXT("Office"), 4 }, { TEXT("Storage"), 1 } };
		static const TArray<FWeightedType> Study = { { TEXT("Study area"), 1 } };
		return Index == 0 ? Teaching : (Index == 1 ? Offices : Study);
	}

	FString PickFrom(FRandomStream& Random, const TArray<FWeightedType>& Table)
	{
		int32 Total = 0;
		for (const FWeightedType& Entry : Table)
		{
			Total += Entry.Weight;
		}
		int32 Roll = Random.RandRange(0, FMath::Max(Total - 1, 0));
		for (const FWeightedType& Entry : Table)
		{
			Roll -= Entry.Weight;
			if (Roll < 0)
			{
				return Entry.Type;
			}
		}
		return Table[0].Type;
	}
}

bool FloorLayout::Generate(const FFloorLayoutParams& Params, FFloorLayout& OutLayout, FString& OutError)
{
	OutLayout = FFloorLayout();
	OutError.Empty();
	const TArray<FVector2D>& Outline = Params.Outline;
	const double K = Params.UnitsPerMetre;
	if (Outline.Num() < 3 || K <= KINDA_SMALL_NUMBER)
	{
		OutError = TEXT("The building has no usable outline.");
		return false;
	}

	const int32 N = Outline.Num();
	const double Orient = SignedArea(Outline) >= 0.0 ? 1.0 : -1.0;
	TArray<FEdgeFrame> Frames;
	for (int32 i = 0; i < N; ++i)
	{
		FEdgeFrame Frame;
		Frame.Origin = Outline[i];
		const FVector2D Edge = Outline[(i + 1) % N] - Outline[i];
		Frame.Length = Edge.Size();
		Frame.U = Frame.Length > KINDA_SMALL_NUMBER ? Edge / Frame.Length : FVector2D(1.0, 0.0);
		Frame.V = Orient * FVector2D(-Frame.U.Y, Frame.U.X);
		Frames.Add(Frame);
	}

	// Everything rooms must stay out of: the passage through the rooms at each door, the lifts and stairs that stand already, and the cores made below.
	TArray<FQuad> Blocked;
	TArray<FVector2D> DoorPoints;
	for (const FFloorDoor& Door : Params.Doors)
	{
		if (!Frames.IsValidIndex(Door.EdgeIndex))
		{
			continue;
		}
		const FEdgeFrame& Frame = Frames[Door.EdgeIndex];
		const double Half = Door.Width * 0.5 + PassageSideRoom * K;
		Blocked.Add(Frame.Quad(FMath::Max(Door.MiddleAlong - Half, 0.0), FMath::Min(Door.MiddleAlong + Half, Frame.Length), 0.0, (MaxRoomDepth + WallMargin) * K));
		DoorPoints.Add(Frame.At(Door.MiddleAlong, 0.0));
	}
	for (int32 i = 0; i < Params.ConnectorFootprints.Num(); ++i)
	{
		FQuad Grown;
		const FVector2D Middle = Params.ConnectorCentres.IsValidIndex(i) ? Params.ConnectorCentres[i] : Centre(Params.ConnectorFootprints[i]);
		for (const FVector2D& Corner : Params.ConnectorFootprints[i])
		{
			Grown.Add(Corner + (Corner - Middle).GetSafeNormal() * ConnectorKeepClear * K);
		}
		Blocked.Add(Grown);
	}

	// The cores: one beside each door (one in all if there are none), against the wall, a stretch of wall each.
	TArray<FZone> Zones;
	if (Params.bMakeCore)
	{
		TArray<FVector2D> Targets = DoorPoints;
		if (Targets.IsEmpty())
		{
			int32 Longest = 0;
			for (int32 i = 1; i < N; ++i)
			{
				Longest = Frames[i].Length > Frames[Longest].Length ? i : Longest;
			}
			Targets.Add(Frames[Longest].At(0.0, 0.0));
		}
		for (const FVector2D& Target : Targets)
		{
			FZone Best;
			double BestScore = TNumericLimits<double>::Max();
			for (int32 Edge = 0; Edge < N; ++Edge)
			{
				const FEdgeFrame& Frame = Frames[Edge];
				for (double U0 = WallMargin * K; U0 + CoreLength * K <= Frame.Length - WallMargin * K; U0 += 1.0 * K)
				{
					FZone Candidate;
					Candidate.Quad = Frame.Quad(U0, U0 + CoreLength * K, WallMargin * K, (WallMargin + CoreDepth) * K);
					Candidate.Centre = Centre(Candidate.Quad);
					const double Score = FVector2D::Distance(Candidate.Centre, Target);
					if (Score >= BestScore || !QuadInside(Outline, Candidate.Quad))
					{
						continue;
					}
					bool bClear = true;
					for (const FQuad& Other : Blocked)
					{
						bClear = bClear && !QuadsOverlap(Candidate.Quad, Other, 0.02 * K);
					}
					for (const FZone& Other : Zones)
					{
						bClear = bClear && FVector2D::Distance(Candidate.Centre, Other.Centre) >= CoreSeparation * K;
					}
					if (!bClear)
					{
						continue;
					}
					// Two lifts and a stair against the wall, turned along it.
					const float Angle = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Frame.U.Y, Frame.U.X)));
					auto Place = [&](EInteriorNavNodeType Type, double Along, double Deep, double HalfAlong, double HalfDeep) -> bool
					{
						const FVector2D Position = Frame.At(U0 + Along * K, Deep * K);
						const FQuad Footprint = Frame.Quad(U0 + (Along - HalfAlong) * K, U0 + (Along + HalfAlong) * K, (Deep - HalfDeep) * K, (Deep + HalfDeep) * K);
						if (!QuadInside(Outline, Footprint))
						{
							return false;
						}
						Candidate.Connectors.Add({ Type, Position, Angle });
						return true;
					};
					if (Place(EInteriorNavNodeType::Elevator, 1.3, WallMargin + 1.35, 1.0, 1.0) && Place(EInteriorNavNodeType::Elevator, 3.7, WallMargin + 1.35, 1.0, 1.0)
						&& Place(EInteriorNavNodeType::Stairs, 6.9, WallMargin + 1.15, 1.25, 0.75))
					{
						Best = Candidate;
						BestScore = Score;
					}
				}
			}
			if (Best.Connectors.IsEmpty() && !Zones.IsEmpty())
			{
				// No room for another core this close to the first: that one serves this door too.
				continue;
			}
			if (Best.Connectors.IsEmpty())
			{
				OutError = TEXT("The building has no stretch of outer wall long enough for the lifts and stairs.");
				return false;
			}
			Blocked.Add(Best.Quad);
			Zones.Add(Best);
		}
	}

	// A big building gets fewer, larger rooms rather than dozens of small ones: stretch them until the floor holds few enough.
	TArray<FPlaced> Placed;
	static const double WidthScales[] = { 1.0, 1.3, 1.7, 2.2, 2.9, 3.8 };
	for (int32 ScaleIndex = 0; ScaleIndex < UE_ARRAY_COUNT(WidthScales); ++ScaleIndex)
	{
		const double WidthScale = WidthScales[ScaleIndex];
		FRandomStream Random(Params.Seed + ScaleIndex * 131);
		Placed.Reset();

		// At each convex corner one of the two edges takes the corner and the other stops short of it, chosen at random.
		TArray<bool> EdgeOwnsStartCorner;
		for (int32 Vertex = 0; Vertex < N; ++Vertex)
		{
			EdgeOwnsStartCorner.Add(Random.FRand() < 0.5f);
		}
		auto TrimAt = [&](int32 Edge, bool bStart) -> double
		{
			const int32 Vertex = bStart ? Edge : (Edge + 1) % N;
			const FVector2D& DirectionIn = Frames[(Vertex + N - 1) % N].U;
			const FVector2D& DirectionOut = Frames[Vertex].U;
			const double Turn = FMath::Atan2(Orient * Cross2(DirectionIn, DirectionOut), FVector2D::DotProduct(DirectionIn, DirectionOut));
			if (Turn <= 0.05)
			{
				// A reflex corner: the rows of the two edges move apart.
				return WallMargin * K;
			}
			const bool bOwns = bStart ? EdgeOwnsStartCorner[Vertex] : !EdgeOwnsStartCorner[Vertex];
			if (bOwns)
			{
				return WallMargin * K;
			}
			// Far enough from the corner that the door of the neighbour's corner room (a metre past its inner side) opens beside this row, not into it.
			const double Interior = UE_PI - Turn;
			return FMath::Min((WallMargin + MaxRoomDepth + FrontClear + 0.3) * K / FMath::Max(FMath::Tan(Interior * 0.5), 0.2), Frames[Edge].Length * 0.4);
		};

		for (int32 Edge = 0; Edge < N; ++Edge)
		{
			const FEdgeFrame& Frame = Frames[Edge];
			const double UEnd = Frame.Length - TrimAt(Edge, false);
			double U = TrimAt(Edge, true);
			bool bPreviousLeftOut = false;
			while (UEnd - U >= MinRoomWidth * K * WidthScale)
			{
				double Width = Random.FRandRange(static_cast<float>(MinRoomWidth), static_cast<float>(MaxRoomWidth)) * K * WidthScale;
				if (UEnd - (U + Width) < MinRoomWidth * K * WidthScale)
				{
					Width = UEnd - U;
				}
				// Now and then a room is left out, so the row is not wall to wall; never two in a row.
				if (!bPreviousLeftOut && Random.FRand() < VoidChance)
				{
					U += Width + RoomGap * K;
					bPreviousLeftOut = true;
					continue;
				}
				bPreviousLeftOut = false;

				// How deep: as deep as the building is wide there allows, leaving a walkable middle for the rows across from each other.
				const double Across = RayDistance(Outline, Frame.At(U + Width * 0.5, 0.05 * K), Frame.V) / K;
				const double Deepest = FMath::Min(MaxRoomDepth, (Across - 2.0 * WallMargin - MinCorridor) * 0.5);
				bool bPlaced = false;
				for (double Depth = Deepest; Depth >= MinRoomDepth - 0.001 && !bPlaced; Depth -= 0.5)
				{
					const FQuad Quad = Frame.Quad(U, U + Width, WallMargin * K, (WallMargin + Depth) * K);
					if (!QuadInside(Outline, Frame.Quad(U - 0.1 * K, U + Width + 0.1 * K, (WallMargin - 0.1) * K, (WallMargin + Depth + 0.1) * K)))
					{
						continue;
					}
					bool bClear = true;
					for (const FQuad& Other : Blocked)
					{
						bClear = bClear && !QuadsOverlap(Quad, Other, 0.02 * K);
					}
					for (const FPlaced& Other : Placed)
					{
						bClear = bClear && !QuadsOverlap(Quad, Other.Quad, 0.02 * K);
					}
					if (bClear)
					{
						FPlaced Room;
						Room.Quad = Quad;
						Room.Edge = Edge;
						Room.Front = Frame.At(U + Width * 0.5, (WallMargin + Depth + FrontClear) * K);
						Room.AreaSquareMetres = Width / K * Depth;
						Placed.Add(Room);
						bPlaced = true;
					}
				}
				U += bPlaced ? Width + RoomGap * K : 1.0 * K;
			}
		}

		// A room whose door would open onto a wall or another room is dropped.
		for (int32 i = Placed.Num() - 1; i >= 0; --i)
		{
			bool bBlocked = !PointInPolygon(Placed[i].Front, Outline);
			for (int32 j = 0; j < Placed.Num() && !bBlocked; ++j)
			{
				bBlocked = j != i && PointInConvexQuad(Placed[i].Front, Placed[j].Quad);
			}
			if (bBlocked)
			{
				Placed.RemoveAt(i);
			}
		}
		if (Placed.Num() <= Params.MaxRooms)
		{
			break;
		}
		if (ScaleIndex == UE_ARRAY_COUNT(WidthScales) - 1)
		{
			// Still too many at the largest size: leave some out at random.
			while (Placed.Num() > Params.MaxRooms)
			{
				Placed.RemoveAt(Random.RandRange(0, Placed.Num() - 1));
			}
		}
	}
	if (Placed.IsEmpty())
	{
		OutError = TEXT("The building is too small or too thin for a row of rooms along its walls.");
		return false;
	}

	// The room nearest each core is its toilet: one a core, three on a floor at most. A building's own lifts and stairs count once per
	// cluster, not once per lift.
	TArray<FVector2D> CoreCentres;
	for (const FZone& Zone : Zones)
	{
		CoreCentres.Add(Zone.Centre);
	}
	for (const FVector2D& Existing : Params.ConnectorCentres)
	{
		bool bNearACore = false;
		for (const FVector2D& Core : CoreCentres)
		{
			bNearACore = bNearACore || FVector2D::Distance(Existing, Core) < CoreLength * K;
		}
		if (!bNearACore)
		{
			CoreCentres.Add(Existing);
		}
	}
	int32 Toilets = 0;
	for (const FVector2D& Core : CoreCentres)
	{
		if (Toilets >= MaxToiletsPerFloor)
		{
			break;
		}
		int32 Nearest = INDEX_NONE;
		double NearestDistance = ToiletReach * K;
		for (int32 i = 0; i < Placed.Num(); ++i)
		{
			const double Distance = FVector2D::Distance(Centre(Placed[i].Quad), Core);
			if (!Placed[i].bToilet && Distance <= NearestDistance)
			{
				Nearest = i;
				NearestDistance = Distance;
			}
		}
		if (Nearest != INDEX_NONE)
		{
			Placed[Nearest].bToilet = true;
			++Toilets;
		}
	}

	// The kinds of the others: rooms of one kind sit together, in runs of two to five along the wall; a big room can be a lecture hall.
	FRandomStream TypeRandom(Params.Seed + 9973);
	int32 FamilyIndex = 0, RunLeft = 0;
	for (FPlaced& Room : Placed)
	{
		FFloorRoom Out;
		if (Room.bToilet)
		{
			Out.Type = TEXT("Toilet");
		}
		else
		{
			if (RunLeft <= 0)
			{
				const int32 Roll = TypeRandom.RandRange(0, 8);
				FamilyIndex = Roll < 5 ? 0 : (Roll < 8 ? 1 : 2);
				RunLeft = TypeRandom.RandRange(2, 5);
			}
			--RunLeft;
			Out.Type = FamilyIndex == 0 && Room.AreaSquareMetres >= 60.0 && TypeRandom.FRand() < 0.3f ? FString(TEXT("Lecture hall")) : PickFrom(TypeRandom, Family(FamilyIndex));
		}
		Out.Footprint = Room.Quad;
		Out.DoorEdgeIndex = 2;
		if (SignedArea(Out.Footprint) < 0.0)
		{
			Algo::Reverse(Out.Footprint);
			// Turned round, the side that faces the corridor is the first edge.
			Out.DoorEdgeIndex = 0;
		}
		OutLayout.Rooms.Add(MoveTemp(Out));
	}
	for (const FZone& Zone : Zones)
	{
		OutLayout.Connectors.Append(Zone.Connectors);
		OutLayout.CoreCentres.Add(Zone.Centre);
	}
	return true;
}
