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

	// Triangulate the footprint once (ear clipping, handles concave shapes) - reused for the bottom
	// cap and every roof/slab section below.
	TArray<int32> CapTris;
	TriangulatePolygon(Poly2D, CapTris);

	CachedPoly2D = Poly2D;
	CachedCapTris = CapTris;

	if (!DynMaterial)
	{
		UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (BaseMat)
		{
			DynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
		}
	}

	// Section count changes with FloorCount (one slab section per inter-floor boundary) - clear
	// whatever sections a previous, possibly-taller build left behind.
	MeshComp->ClearAllMeshSections();

	// ---- Section 0: side walls + ground floor cap - always visible ----
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

	// Ground floor face (bottom ring, reverse winding to face down)
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
	MeshComp->SetMaterial(0, DynMaterial);

	// Sections 1..FloorCount (roof + inter-floor slabs) are all built by ApplySlabVisibility, which
	// also decides which one(s) currently have no collision/visibility based on VisibleInteriorLevel
	// - no separate "build them all visible, then hide some" step needed.
	ApplySlabVisibility();
	SetHighlighted(false);

	UE_LOG(LogTemp, Warning, TEXT("Built building %s: %d floors, height %f"), *BuildingId, FloorCount, Height);
}

void APolyBuilding::BuildCapSection(const TArray<FVector2D>& Poly2D, const TArray<int32>& CapTris, float Z, int32 SectionIndex, bool bCreateCollision)
{
	TArray<FVector> Vertices;
	Vertices.Reserve(Poly2D.Num());
	for (const FVector2D& Point : Poly2D)
	{
		Vertices.Add(FVector(Point.X, Point.Y, Z));
	}

	TArray<int32> Triangles;
	Triangles.Reserve(CapTris.Num());
	// CapTris is CCW (faces up); swap the last two indices per triangle to keep that up-facing
	// winding in this section's own 0-indexed vertex buffer.
	for (int32 i = 0; i < CapTris.Num(); i += 3)
	{
		Triangles.Add(CapTris[i]);
		Triangles.Add(CapTris[i + 2]);
		Triangles.Add(CapTris[i + 1]);
	}

	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FProcMeshTangent> Tangents;
	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices, Triangles, UV0, Normals, Tangents);

	TArray<FColor> VertexColors;
	MeshComp->CreateMeshSection(SectionIndex, Vertices, Triangles, Normals, UV0, VertexColors, Tangents, bCreateCollision);
	MeshComp->SetMaterial(SectionIndex, DynMaterial);
}

void APolyBuilding::SetVisibleInteriorLevel(int32 Level)
{
	VisibleInteriorLevel = Level;
	ApplySlabVisibility();
}

void APolyBuilding::ApplySlabVisibility()
{
	if (!MeshComp || CachedPoly2D.Num() == 0)
	{
		return;
	}

	for (int32 L = 1; L <= FloorCount; L++)
	{
		const int32 SectionIndex = (L == FloorCount) ? 1 : (1 + L);
		const float Z = (L == FloorCount) ? (FloorCount * FloorHeight) : (L * FloorHeight);

		// Hide every ceiling from the viewed floor up through the roof - the camera's framing
		// margin puts it well above a single floor height, so it needs a clear line of sight all
		// the way up, not just past its own ceiling. Everything at/below the viewed floor's own
		// ceiling stays opaque (and solid) so you don't see through - or click through - into the
		// floor below.
		const bool bHide = (VisibleInteriorLevel > 0 && L >= VisibleInteriorLevel);

		// UProceduralMeshComponent has no way to toggle an existing section's collision, so a
		// visibility change here means recreating the section - the geometry is identical either
		// way, only the collision flag differs. Keeping collision off in step with visibility keeps
		// any future 3D-view interaction (see the "3D mode" toggle in the PM's mockup) consistent
		// with what's actually visible, even though interior room/wall editing itself now happens
		// entirely through the 2D floor plan canvas (UInteriorFloorPlanWidget), not 3D raycasts.
		BuildCapSection(CachedPoly2D, CachedCapTris, Z, SectionIndex, /*bCreateCollision=*/!bHide);
		MeshComp->SetMeshSectionVisible(SectionIndex, !bHide);
	}
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
