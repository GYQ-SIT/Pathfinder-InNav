// Fill out your copyright notice in the Description page of Project Settings.

#include "PolygonGeometryUtils.h"

float PolygonSignedArea(const TArray<FVector2D>& Poly)
{
	float Area = 0.f;
	const int32 N = Poly.Num();
	for (int32 i = 0; i < N; i++)
	{
		const FVector2D& A = Poly[i];
		const FVector2D& B = Poly[(i + 1) % N];
		Area += (A.X * B.Y - B.X * A.Y);
	}
	return Area * 0.5f;
}

bool PointInTriangle(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
{
	float d1 = (P.X - B.X) * (A.Y - B.Y) - (A.X - B.X) * (P.Y - B.Y);
	float d2 = (P.X - C.X) * (B.Y - C.Y) - (B.X - C.X) * (P.Y - C.Y);
	float d3 = (P.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (P.Y - A.Y);
	bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
	bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
	return !(hasNeg && hasPos);
}

void TriangulatePolygon(const TArray<FVector2D>& Poly, TArray<int32>& OutTriangles)
{
	const int32 N = Poly.Num();
	if (N < 3) return;

	// Make sure we work with counter-clockwise winding
	TArray<int32> Indices;
	Indices.SetNum(N);
	if (PolygonSignedArea(Poly) < 0.f)
	{
		// clockwise -> reverse to CCW
		for (int32 i = 0; i < N; i++) Indices[i] = N - 1 - i;
	}
	else
	{
		for (int32 i = 0; i < N; i++) Indices[i] = i;
	}

	// Ear clipping
	int32 Guard = 0;
	while (Indices.Num() > 3 && Guard < 10000)
	{
		Guard++;
		bool bEarFound = false;
		const int32 M = Indices.Num();

		for (int32 i = 0; i < M; i++)
		{
			int32 i0 = Indices[(i + M - 1) % M];
			int32 i1 = Indices[i];
			int32 i2 = Indices[(i + 1) % M];

			const FVector2D& A = Poly[i0];
			const FVector2D& B = Poly[i1];
			const FVector2D& C = Poly[i2];

			// Convex check (cross product > 0 for CCW)
			float Cross = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
			if (Cross <= 0.f) continue; // reflex vertex, not an ear

			// Check no other vertex is inside this triangle
			bool bContains = false;
			for (int32 j = 0; j < M; j++)
			{
				int32 vj = Indices[j];
				if (vj == i0 || vj == i1 || vj == i2) continue;
				if (PointInTriangle(Poly[vj], A, B, C))
				{
					bContains = true;
					break;
				}
			}
			if (bContains) continue;

			// It's an ear -> add triangle, remove the tip
			OutTriangles.Add(i0);
			OutTriangles.Add(i1);
			OutTriangles.Add(i2);
			Indices.RemoveAt(i);
			bEarFound = true;
			break;
		}

		if (!bEarFound) break; // safety: avoid infinite loop on bad polygon
	}

	// Last triangle
	if (Indices.Num() == 3)
	{
		OutTriangles.Add(Indices[0]);
		OutTriangles.Add(Indices[1]);
		OutTriangles.Add(Indices[2]);
	}
}

void AppendWallBox(TArray<FVector>& Vertices, TArray<int32>& Triangles,
	const FVector& P000, const FVector& P100, const FVector& P010, const FVector& P110,
	const FVector& P001, const FVector& P101, const FVector& P011, const FVector& P111)
{
	auto AddQuad = [&Vertices, &Triangles](const FVector& A, const FVector& B, const FVector& C, const FVector& D)
	{
		const int32 Base = Vertices.Num();
		Vertices.Add(A);
		Vertices.Add(B);
		Vertices.Add(C);
		Vertices.Add(D);
		Triangles.Add(Base);     Triangles.Add(Base + 1); Triangles.Add(Base + 2);
		Triangles.Add(Base);     Triangles.Add(Base + 2); Triangles.Add(Base + 3);
	};

	AddQuad(P000, P001, P011, P010); // start end-cap
	AddQuad(P100, P110, P111, P101); // end end-cap
	AddQuad(P000, P100, P101, P001); // inner long face
	AddQuad(P010, P011, P111, P110); // outer long face
	AddQuad(P000, P010, P110, P100); // bottom
	AddQuad(P001, P101, P111, P011); // top
}

// ---- Smooth walls and flat roads (see the comment in the header) --------------------------------------------------------------------

namespace
{
	// Adds the triangle A, B, C turned so the engine reads its face as pointing along Want. The engine takes the facing of a triangle as
	// (P1 - P2) x (P0 - P2) (UKismetProceduralMeshLibrary::CalculateTangentsForMesh does the same), so the order is decided here by that
	// rule instead of by hand: a face whose order is wrong is drawn inside out, which also turns its lighting the wrong way round.
	void AddFacingTriangle(FShadedMesh& Mesh, int32 A, int32 B, int32 C, const FVector& Want)
	{
		const FVector& P0 = Mesh.Vertices[A];
		const FVector& P1 = Mesh.Vertices[B];
		const FVector& P2 = Mesh.Vertices[C];
		const FVector Facing = (P1 - P2) ^ (P0 - P2);
		if (FVector::DotProduct(Facing, Want) >= 0.f)
		{
			Mesh.AddTriangle(A, B, C);
		}
		else
		{
			Mesh.AddTriangle(A, C, B);
		}
	}

	// One quad of four corners in order round it, each with its own normal; its facing is the sum of those normals.
	void AddFacingQuad(FShadedMesh& Mesh, const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3,
		const FVector& N0, const FVector& N1, const FVector& N2, const FVector& N3)
	{
		const int32 A = Mesh.AddVertex(P0, N0);
		const int32 B = Mesh.AddVertex(P1, N1);
		const int32 C = Mesh.AddVertex(P2, N2);
		const int32 D = Mesh.AddVertex(P3, N3);
		const FVector Want = N0 + N1 + N2 + N3;
		AddFacingTriangle(Mesh, A, B, C, Want);
		AddFacingTriangle(Mesh, A, C, D, Want);
	}

	FVector ToVector3(const FVector2D& V, float Z) { return FVector(V.X, V.Y, Z); }

	FVector2D PerpendicularOf(const FVector2D& Dir) { return FVector2D(-Dir.Y, Dir.X); }

	// The nearest vertex before (Step = -1) or after (Step = +1) Index that is not at the same place, or INDEX_NONE. A chain that is not
	// closed ends at its first and last vertex instead of wrapping round.
	int32 NeighbourVertex(const TArray<FVector2D>& Poly, int32 Index, int32 Step, bool bClosed = true)
	{
		const int32 N = Poly.Num();
		for (int32 Taken = 1; Taken < N; ++Taken)
		{
			int32 Candidate = Index + Step * Taken;
			if (bClosed)
			{
				Candidate = (Candidate % N + N) % N;
			}
			else if (Candidate < 0 || Candidate >= N)
			{
				return INDEX_NONE;
			}
			if (FVector2D::DistSquared(Poly[Candidate], Poly[Index]) > KINDA_SMALL_NUMBER)
			{
				return Candidate;
			}
		}
		return INDEX_NONE;
	}
}

void ComputeWallCorners(const TArray<FVector2D>& Poly, float HalfThickness, TArray<FWallCorner>& OutCorners,
	float SmoothBelowDegrees, float SmoothMaxEdge, float MaxMiter, bool bClosed)
{
	const int32 N = Poly.Num();
	OutCorners.Reset();
	OutCorners.SetNum(N);

	for (int32 i = 0; i < N; ++i)
	{
		const int32 Before = NeighbourVertex(Poly, i, -1, bClosed);
		const int32 After = NeighbourVertex(Poly, i, +1, bClosed);
		FWallCorner& Corner = OutCorners[i];

		if (Before == INDEX_NONE || After == INDEX_NONE)
		{
			// The end of a free-standing wall: one edge only, a square end with the edge's own normal.
			if (Before != INDEX_NONE || After != INDEX_NONE)
			{
				const FVector2D Along = (Before != INDEX_NONE ? Poly[i] - Poly[Before] : Poly[After] - Poly[i]).GetSafeNormal();
				Corner.Offset = PerpendicularOf(Along) * HalfThickness;
				Corner.PrevNormal = Corner.NextNormal = PerpendicularOf(Along);
			}
			continue;
		}

		const FVector2D In = Poly[i] - Poly[Before];
		const FVector2D Out = Poly[After] - Poly[i];
		const float InLength = In.Size();
		const float OutLength = Out.Size();
		const FVector2D N0 = PerpendicularOf(In / InLength);
		const FVector2D N1 = PerpendicularOf(Out / OutLength);

		const FVector2D Sum = N0 + N1;
		if (Sum.SizeSquared() < 1.e-6f)
		{
			// The wall turns straight back on itself: no mitre exists. Each side ends square with its own cap (bJoin stays false); the
			// normals are those of the two edges, and MakeCornerWallEnd reads the edge's own normal for the end it builds.
			Corner.Offset = N1 * HalfThickness;
			Corner.PrevNormal = N0;
			Corner.NextNormal = N1;
			continue;
		}

		const FVector2D Mitre = Sum.GetSafeNormal();
		const float CosHalfTurn = FMath::Max(FVector2D::DotProduct(Mitre, N0), 1.f / FMath::Max(MaxMiter, 1.f));
		Corner.Offset = Mitre * (HalfThickness / CosHalfTurn);
		Corner.bJoin = true;

		const float TurnDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector2D::DotProduct(N0, N1), -1.f, 1.f)));
		const bool bSmooth = TurnDegrees < SmoothBelowDegrees && InLength <= SmoothMaxEdge && OutLength <= SmoothMaxEdge;
		Corner.PrevNormal = bSmooth ? Mitre : N0;
		Corner.NextNormal = bSmooth ? Mitre : N1;
	}
}

FWallEnd MakePlainWallEnd(const FVector2D& Center, const FVector2D& Perp, float HalfThickness)
{
	FWallEnd End;
	End.Center = Center;
	End.Offset = Perp * HalfThickness;
	End.Normal = Perp;
	End.bCap = true;
	return End;
}

FWallEnd MakeCornerWallEnd(const FVector2D& Vertex, const FWallCorner& Corner, bool bStart)
{
	FWallEnd End;
	End.Center = Vertex;
	End.Normal = bStart ? Corner.NextNormal : Corner.PrevNormal;
	if (Corner.bJoin)
	{
		End.Offset = Corner.Offset;
		End.bCap = false;
	}
	else
	{
		// Nothing to join: a square end of the edge's own thickness, closed with a cap.
		End.Offset = End.Normal * Corner.Offset.Size();
		End.bCap = true;
	}
	return End;
}

void AppendWallSpan(FShadedMesh& Mesh, const FWallEnd& Start, const FWallEnd& End, float Z0, float Z1)
{
	if (Z1 - Z0 <= KINDA_SMALL_NUMBER || FVector2D::DistSquared(Start.Center, End.Center) <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector2D Dir = (End.Center - Start.Center).GetSafeNormal();
	const FVector2D SP = Start.Center + Start.Offset; // plus side
	const FVector2D SM = Start.Center - Start.Offset; // minus side
	const FVector2D EP = End.Center + End.Offset;
	const FVector2D EM = End.Center - End.Offset;
	const FVector NS = ToVector3(Start.Normal, 0.f);
	const FVector NE = ToVector3(End.Normal, 0.f);
	const FVector Up(0.f, 0.f, 1.f);

	// The two long faces. Their normals are the ones the caller chose, so a curved front can be blended and a straight one stay flat.
	AddFacingQuad(Mesh, ToVector3(SP, Z0), ToVector3(EP, Z0), ToVector3(EP, Z1), ToVector3(SP, Z1), NS, NE, NE, NS);
	AddFacingQuad(Mesh, ToVector3(SM, Z0), ToVector3(EM, Z0), ToVector3(EM, Z1), ToVector3(SM, Z1), -NS, -NE, -NE, -NS);

	// Top and bottom.
	AddFacingQuad(Mesh, ToVector3(SM, Z1), ToVector3(EM, Z1), ToVector3(EP, Z1), ToVector3(SP, Z1), Up, Up, Up, Up);
	AddFacingQuad(Mesh, ToVector3(SM, Z0), ToVector3(EM, Z0), ToVector3(EP, Z0), ToVector3(SP, Z0), -Up, -Up, -Up, -Up);

	// A cap only where the wall really ends: a door gap, a jamb. A corner has none, the next span carries on from the same points.
	if (Start.bCap)
	{
		const FVector N = ToVector3(-Dir, 0.f);
		AddFacingQuad(Mesh, ToVector3(SM, Z0), ToVector3(SP, Z0), ToVector3(SP, Z1), ToVector3(SM, Z1), N, N, N, N);
	}
	if (End.bCap)
	{
		const FVector N = ToVector3(Dir, 0.f);
		AddFacingQuad(Mesh, ToVector3(EM, Z0), ToVector3(EP, Z0), ToVector3(EP, Z1), ToVector3(EM, Z1), N, N, N, N);
	}
}

namespace
{
	// Whether the pieces of a neighbouring edge that touch one of its ends (its start if bAtStart, else its end) cover the whole height
	// Z0..Z1 there.
	bool WallReachesVertex(const FWallEdge& Neighbour, float NeighbourLength, bool bAtStart, float Z0, float Z1)
	{
		constexpr float Tolerance = 0.01f;
		TArray<FWallPiece> Touching;
		for (const FWallPiece& Piece : Neighbour.Pieces)
		{
			const bool bTouches = bAtStart ? Piece.T0 <= Tolerance : Piece.T1 >= NeighbourLength - Tolerance;
			if (bTouches && Piece.T1 - Piece.T0 > Tolerance)
			{
				Touching.Add(Piece);
			}
		}
		Touching.Sort([](const FWallPiece& L, const FWallPiece& R) { return L.Z0 < R.Z0; });

		float Covered = Z0;
		for (const FWallPiece& Piece : Touching)
		{
			if (Piece.Z1 <= Covered)
			{
				continue;
			}
			if (Piece.Z0 > Covered + Tolerance)
			{
				return false;
			}
			Covered = Piece.Z1;
		}
		return Covered >= Z1 - Tolerance;
	}
}

void AppendWallEdges(FShadedMesh& Mesh, const TArray<FWallEdge>& Edges, const TArray<FWallCorner>& Corners, float HalfThickness, bool bClosed)
{
	const int32 NumEdges = Edges.Num();
	const int32 NumVertices = Corners.Num();
	if (NumEdges == 0 || NumVertices == 0)
	{
		return;
	}

	TArray<float> Lengths;
	Lengths.SetNum(NumEdges);
	for (int32 i = 0; i < NumEdges; ++i)
	{
		Lengths[i] = (Edges[i].B - Edges[i].A).Size();
	}

	// The nearest edge before (Step = -1) or after (Step = +1) one that has a length, or INDEX_NONE (a chain ends, it does not wrap).
	auto NeighbourEdge = [&](int32 Edge, int32 Step) -> int32
	{
		for (int32 Taken = 1; Taken < NumEdges; ++Taken)
		{
			int32 Candidate = Edge + Step * Taken;
			if (bClosed)
			{
				Candidate = (Candidate % NumEdges + NumEdges) % NumEdges;
			}
			else if (Candidate < 0 || Candidate >= NumEdges)
			{
				return INDEX_NONE;
			}
			if (Lengths[Candidate] > KINDA_SMALL_NUMBER)
			{
				return Candidate;
			}
		}
		return INDEX_NONE;
	};

	for (int32 i = 0; i < NumEdges; ++i)
	{
		const float Length = Lengths[i];
		if (Length <= KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const FVector2D Dir = (Edges[i].B - Edges[i].A) / Length;
		const FVector2D Perp = PerpendicularOf(Dir);
		const int32 Before = NeighbourEdge(i, -1);
		const int32 After = NeighbourEdge(i, +1);
		const int32 StartVertex = i;
		const int32 EndVertex = (i + 1) % NumVertices;

		for (const FWallPiece& Piece : Edges[i].Pieces)
		{
			if (Piece.T1 - Piece.T0 <= KINDA_SMALL_NUMBER || Piece.Z1 - Piece.Z0 <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const bool bJoinStart = Piece.T0 <= KINDA_SMALL_NUMBER && Before != INDEX_NONE && Corners[StartVertex].bJoin
				&& WallReachesVertex(Edges[Before], Lengths[Before], /*bAtStart=*/false, Piece.Z0, Piece.Z1);
			const bool bJoinEnd = Piece.T1 >= Length - KINDA_SMALL_NUMBER && After != INDEX_NONE && Corners[EndVertex].bJoin
				&& WallReachesVertex(Edges[After], Lengths[After], /*bAtStart=*/true, Piece.Z0, Piece.Z1);
			const FWallEnd Start = bJoinStart ? MakeCornerWallEnd(Edges[i].A, Corners[StartVertex], /*bStart=*/true)
				: MakePlainWallEnd(Edges[i].A + Dir * Piece.T0, Perp, HalfThickness);
			const FWallEnd End = bJoinEnd ? MakeCornerWallEnd(Edges[i].B, Corners[EndVertex], /*bStart=*/false)
				: MakePlainWallEnd(Edges[i].A + Dir * Piece.T1, Perp, HalfThickness);
			AppendWallSpan(Mesh, Start, End, Piece.Z0, Piece.Z1);
		}
	}
}

void AppendFlatCap(FShadedMesh& Mesh, const TArray<FVector2D>& Poly, float Z)
{
	if (Poly.Num() < 3)
	{
		return;
	}

	TArray<int32> Triangles;
	TriangulatePolygon(Poly, Triangles);

	const FVector Up(0.f, 0.f, 1.f);
	const int32 First = Mesh.Vertices.Num();
	for (const FVector2D& Point : Poly)
	{
		Mesh.AddVertex(ToVector3(Point, Z), Up);
	}
	for (int32 i = 0; i + 2 < Triangles.Num(); i += 3)
	{
		AddFacingTriangle(Mesh, First + Triangles[i], First + Triangles[i + 1], First + Triangles[i + 2], Up);
	}
}

void SmoothPolyline(const TArray<FVector2D>& Points, float MaxCut, TArray<FVector2D>& OutPoints, float MinTurnDegrees)
{
	OutPoints.Reset();

	// Points that sit on top of each other make a bend of no direction; leave them out.
	TArray<FVector2D> Clean;
	for (const FVector2D& Point : Points)
	{
		if (Clean.IsEmpty() || FVector2D::DistSquared(Clean.Last(), Point) > 0.01f)
		{
			Clean.Add(Point);
		}
	}
	const bool bClosed = Clean.Num() >= 4 && FVector2D::DistSquared(Clean[0], Clean.Last()) <= 0.01f;
	if (bClosed)
	{
		Clean.Pop();
	}
	const int32 N = Clean.Num();
	if (N < 3 || MaxCut <= KINDA_SMALL_NUMBER)
	{
		OutPoints = Points;
		return;
	}

	auto RoundBend = [&](const FVector2D& Before, const FVector2D& Bend, const FVector2D& After)
	{
		const FVector2D In = Bend - Before;
		const FVector2D Out = After - Bend;
		const float InLength = In.Size();
		const float OutLength = Out.Size();
		if (InLength <= KINDA_SMALL_NUMBER || OutLength <= KINDA_SMALL_NUMBER)
		{
			OutPoints.Add(Bend);
			return;
		}
		const FVector2D InDir = In / InLength;
		const FVector2D OutDir = Out / OutLength;
		const float TurnDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector2D::DotProduct(InDir, OutDir), -1.f, 1.f)));
		if (TurnDegrees < MinTurnDegrees)
		{
			OutPoints.Add(Bend);
			return;
		}

		// An arc that starts Cut before the bend and ends Cut after it: a quadratic curve with the bend as its control point.
		const float Cut = FMath::Min3(MaxCut, InLength * 0.5f, OutLength * 0.5f);
		const FVector2D From = Bend - InDir * Cut;
		const FVector2D To = Bend + OutDir * Cut;
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(TurnDegrees / 15.f), 2, 8);
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float T = static_cast<float>(Step) / Steps;
			const float U = 1.f - T;
			OutPoints.Add(From * (U * U) + Bend * (2.f * U * T) + To * (T * T));
		}
	};

	if (bClosed)
	{
		for (int32 i = 0; i < N; ++i)
		{
			RoundBend(Clean[(i + N - 1) % N], Clean[i], Clean[(i + 1) % N]);
		}
		const FVector2D LoopStart = OutPoints[0]; // a copy: adding an element of the array to itself may move it
		OutPoints.Add(LoopStart);
		return;
	}

	OutPoints.Add(Clean[0]);
	for (int32 i = 1; i < N - 1; ++i)
	{
		RoundBend(Clean[i - 1], Clean[i], Clean[i + 1]);
	}
	OutPoints.Add(Clean.Last());
}

void AppendFlatRibbon(FShadedMesh& Mesh, const TArray<FVector2D>& Points, float HalfWidth, float Z, bool bTwoSided, bool bRoundEnds)
{
	TArray<FVector2D> Line;
	for (const FVector2D& Point : Points)
	{
		if (Line.IsEmpty() || FVector2D::DistSquared(Line.Last(), Point) > 0.01f)
		{
			Line.Add(Point);
		}
	}
	const bool bClosed = Line.Num() >= 4 && FVector2D::DistSquared(Line[0], Line.Last()) <= 0.01f;
	if (bClosed)
	{
		Line.Pop();
	}
	const int32 N = Line.Num();
	if (N < 2 || HalfWidth <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector Up(0.f, 0.f, 1.f);

	// The two corners of the ribbon at every point: where the edges of the two pieces meeting there cross (the mitre).
	TArray<FVector2D> Offsets;
	Offsets.SetNum(N);
	auto DirectionBetween = [&Line](int32 From, int32 To) { return (Line[To] - Line[From]).GetSafeNormal(); };
	for (int32 i = 0; i < N; ++i)
	{
		const bool bHasBefore = bClosed || i > 0;
		const bool bHasAfter = bClosed || i < N - 1;
		const FVector2D PerpIn = bHasBefore ? PerpendicularOf(DirectionBetween((i + N - 1) % N, i)) : FVector2D::ZeroVector;
		const FVector2D PerpOut = bHasAfter ? PerpendicularOf(DirectionBetween(i, (i + 1) % N)) : FVector2D::ZeroVector;
		const FVector2D Sum = PerpIn + PerpOut;
		if (!bHasBefore || !bHasAfter || Sum.SizeSquared() < 1.e-6f)
		{
			Offsets[i] = (bHasAfter ? PerpOut : PerpIn) * HalfWidth;
		}
		else
		{
			const FVector2D Mitre = Sum.GetSafeNormal();
			Offsets[i] = Mitre * (HalfWidth / FMath::Max(FVector2D::DotProduct(Mitre, PerpIn), 0.5f));
		}
	}

	// One copy of the ribbon faces up; a second faces down when it should be seen from below as well.
	for (int32 Side = 0; Side < (bTwoSided ? 2 : 1); ++Side)
	{
		const FVector Normal = Side == 0 ? Up : -Up;
		const int32 First = Mesh.Vertices.Num();
		for (int32 i = 0; i < N; ++i)
		{
			Mesh.AddVertex(ToVector3(Line[i] + Offsets[i], Z), Normal);
			Mesh.AddVertex(ToVector3(Line[i] - Offsets[i], Z), Normal);
		}
		const int32 Pieces = bClosed ? N : N - 1;
		for (int32 i = 0; i < Pieces; ++i)
		{
			const int32 j = (i + 1) % N;
			AddFacingTriangle(Mesh, First + 2 * i, First + 2 * i + 1, First + 2 * j + 1, Normal);
			AddFacingTriangle(Mesh, First + 2 * i, First + 2 * j + 1, First + 2 * j, Normal);
		}

		if (bRoundEnds && !bClosed)
		{
			constexpr int32 DiscSteps = 6;
			for (int32 End = 0; End < 2; ++End)
			{
				const int32 Point = End == 0 ? 0 : N - 1;
				const FVector2D AlongRoad = End == 0 ? -DirectionBetween(0, 1) : DirectionBetween(N - 2, N - 1);
				const FVector2D Across = Offsets[Point].GetSafeNormal();
				const int32 Centre = Mesh.AddVertex(ToVector3(Line[Point], Z), Normal);
				int32 Previous = INDEX_NONE;
				for (int32 Step = 0; Step <= DiscSteps; ++Step)
				{
					const float Angle = UE_PI * Step / DiscSteps;
					const FVector2D Rim = Line[Point] + (Across * FMath::Cos(Angle) + AlongRoad * FMath::Sin(Angle)) * HalfWidth;
					const int32 Current = Mesh.AddVertex(ToVector3(Rim, Z), Normal);
					if (Previous != INDEX_NONE)
					{
						AddFacingTriangle(Mesh, Centre, Previous, Current, Normal);
					}
					Previous = Current;
				}
			}
		}
	}
}
