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
