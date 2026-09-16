// Fill out your copyright notice in the Description page of Project Settings.


#include "PolyBuilding.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

APolyBuilding::APolyBuilding()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComp = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;

	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionResponseToAllChannels(ECR_Block);

	BuildingOffset = FVector2D::ZeroVector;
	BuildingHeight = 0.f;
}

// ---- Helper: signed area of polygon (to determine winding) ----
static float PolygonSignedArea(const TArray<FVector2D>& Poly)
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

// ---- Helper: is point P inside triangle ABC ----
static bool PointInTriangle(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
{
	float d1 = (P.X - B.X) * (A.Y - B.Y) - (A.X - B.X) * (P.Y - B.Y);
	float d2 = (P.X - C.X) * (B.Y - C.Y) - (B.X - C.X) * (P.Y - C.Y);
	float d3 = (P.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (P.Y - A.Y);
	bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
	bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
	return !(hasNeg && hasPos);
}

// ---- Ear clipping triangulation. Returns triangle indices into the polygon. ----
// Handles convex AND concave simple polygons.
static void TriangulatePolygon(const TArray<FVector2D>& Poly, TArray<int32>& OutTriangles)
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

void APolyBuilding::BuildFromFootprint(const TArray<FVector2D>& Footprint, float Scale, FVector2D Offset, int32 InFloorCount, float InFloorHeight)
{
	if (Footprint.Num() < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("Footprint needs at least 3 points"));
		return;
	}

	CachedFootprint = Footprint;
	CachedScale = Scale;
	BuildingOffset = Offset;
	FloorCount = FMath::Max(1, InFloorCount);
	FloorHeight = InFloorHeight;

	RebuildMesh();
}

void APolyBuilding::SetFloorCount(int32 NewFloorCount)
{
	if (CachedFootprint.Num() < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("SetFloorCount: building '%s' has no footprint yet"), *BuildingId);
		return;
	}

	FloorCount = FMath::Max(1, NewFloorCount);
	RebuildMesh();
}

void APolyBuilding::RebuildMesh()
{
	const int32 N = CachedFootprint.Num();
	const float Height = FloorCount * FloorHeight;
	BuildingHeight = Height;

	// Build the transformed 2D footprint (offset + scale)
	TArray<FVector2D> Poly2D;
	Poly2D.SetNum(N);
	for (int32 i = 0; i < N; i++)
	{
		Poly2D[i] = FVector2D((CachedFootprint[i].X + BuildingOffset.X) * CachedScale, (CachedFootprint[i].Y + BuildingOffset.Y) * CachedScale);
	}

	// Normalize to counter-clockwise winding so the side walls always face outward,
	// regardless of which direction the footprint points were traced/clicked in.
	if (PolygonSignedArea(Poly2D) < 0.f)
	{
		for (int32 i = 0, j = N - 1; i < j; i++, j--)
		{
			Poly2D.Swap(i, j);
		}
	}

	// Triangulate the footprint once (ear clipping, handles concave shapes) - reused for both the
	// bottom cap (section 0) and the roof (section 1, kept separate so it can be hidden
	// independently - see SetRoofVisible).
	TArray<int32> CapTris;
	TriangulatePolygon(Poly2D, CapTris);

	// ---- Section 0: side walls + bottom cap - stays visible even with the roof hidden ----
	TArray<FVector> WallVertices;
	TArray<int32> WallTriangles;

	// Bottom ring: 0..N-1, Top ring: N..2N-1
	for (int32 i = 0; i < N; i++)
	{
		WallVertices.Add(FVector(Poly2D[i].X, Poly2D[i].Y, 0.f));
	}
	for (int32 i = 0; i < N; i++)
	{
		WallVertices.Add(FVector(Poly2D[i].X, Poly2D[i].Y, Height));
	}

	for (int32 i = 0; i < N; i++)
	{
		int32 next = (i + 1) % N;
		int32 b0 = i;
		int32 b1 = next;
		int32 t0 = i + N;
		int32 t1 = next + N;

		WallTriangles.Add(b0); WallTriangles.Add(t0); WallTriangles.Add(b1);
		WallTriangles.Add(b1); WallTriangles.Add(t0); WallTriangles.Add(t1);
	}

	// Bottom face (bottom ring, reverse winding to face down)
	for (int32 i = 0; i < CapTris.Num(); i += 3)
	{
		WallTriangles.Add(CapTris[i]);
		WallTriangles.Add(CapTris[i + 1]);
		WallTriangles.Add(CapTris[i + 2]);
	}

	TArray<FVector> WallNormals;
	TArray<FVector2D> WallUV0;
	TArray<FProcMeshTangent> WallTangents;
	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(
		WallVertices, WallTriangles, WallUV0, WallNormals, WallTangents);

	TArray<FColor> WallVertexColors;
	MeshComp->CreateMeshSection(0, WallVertices, WallTriangles, WallNormals, WallUV0, WallVertexColors, WallTangents, true);

	// ---- Section 1: roof (top cap) only - its own section so SetRoofVisible can hide just this ----
	TArray<FVector> RoofVertices;
	TArray<int32> RoofTriangles;

	for (int32 i = 0; i < N; i++)
	{
		RoofVertices.Add(FVector(Poly2D[i].X, Poly2D[i].Y, Height));
	}
	// CapTris is CCW (faces up); this buffer is its own 0-indexed vertex set (no +N offset needed).
	for (int32 i = 0; i < CapTris.Num(); i += 3)
	{
		RoofTriangles.Add(CapTris[i]);
		RoofTriangles.Add(CapTris[i + 2]);
		RoofTriangles.Add(CapTris[i + 1]);
	}

	TArray<FVector> RoofNormals;
	TArray<FVector2D> RoofUV0;
	TArray<FProcMeshTangent> RoofTangents;
	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(
		RoofVertices, RoofTriangles, RoofUV0, RoofNormals, RoofTangents);

	TArray<FColor> RoofVertexColors;
	MeshComp->CreateMeshSection(1, RoofVertices, RoofTriangles, RoofNormals, RoofUV0, RoofVertexColors, RoofTangents, true);

	if (!DynMaterial)
	{
		UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (BaseMat)
		{
			DynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
		}
	}
	MeshComp->SetMaterial(0, DynMaterial);
	MeshComp->SetMaterial(1, DynMaterial);
	MeshComp->SetMeshSectionVisible(1, bRoofVisible);

	SetHighlighted(false);

	UE_LOG(LogTemp, Warning, TEXT("Built building %s: %d floors, height %f"), *BuildingId, FloorCount, Height);
}

void APolyBuilding::SetHighlighted(bool bHighlighted)
{
	if (!DynMaterial)
	{
		return;
	}

	if (bHighlighted)
	{
		DynMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.85f, 0.0f));
	}
	else
	{
		DynMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.6f, 0.6f, 0.6f));
	}
}

void APolyBuilding::SetRoofVisible(bool bVisible)
{
	bRoofVisible = bVisible;
	if (MeshComp)
	{
		MeshComp->SetMeshSectionVisible(1, bRoofVisible);
	}
}
