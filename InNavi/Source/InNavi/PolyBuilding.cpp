// Fill out your copyright notice in the Description page of Project Settings.


#include "PolyBuilding.h"
#include "PolygonGeometryUtils.h"
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
	const int32 PreviousFloorCount = FloorCount;
	FloorCount = FMath::Max(1, InFloorCount);
	FloorHeight = InFloorHeight;

	RebuildMesh();
	if (FloorCount != PreviousFloorCount) OnFloorCountChanged.Broadcast(this);
}

void APolyBuilding::SetFloorCount(int32 NewFloorCount)
{
	if (CachedFootprint.Num() < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("SetFloorCount: building '%s' has no footprint yet"), *BuildingId);
		return;
	}

	const int32 PreviousFloorCount = FloorCount;
	FloorCount = FMath::Max(1, NewFloorCount);
	RebuildMesh();
	if (FloorCount != PreviousFloorCount) OnFloorCountChanged.Broadcast(this);
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
	bPolyReversed = false;
	if (PolygonSignedArea(Poly2D) < 0.f)
	{
		bPolyReversed = true;
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
			nullptr,
			TEXT("/Game/ImageAssets/Materials/M_BuildingTransparent.M_BuildingTransparent")
		);

		if (BaseMat)
		{
			DynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);

			DynMaterial->SetScalarParameterValue(TEXT("Opacity"), 1.0f);
			DynMaterial->SetVectorParameterValue(
				TEXT("Color"),
				FLinearColor(0.6f, 0.6f, 0.6f)
			);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("Failed to load M_BuildingTransparent material"));
		}
	}

	// Section count changes with FloorCount (one slab section per inter-floor boundary, one wall
	// section per floor) - clear whatever sections a previous, possibly-taller build left behind.
	MeshComp->ClearAllMeshSections();

	// ---- Section 0: ground floor cap only - always visible (closes off the exterior shell's
	// bottom); the side walls are built per-floor below instead, not bundled in here ----
	TArray<FVector> GroundVertices;
	TArray<int32> GroundTriangles;
	for (int32 i = 0; i < N; i++)
	{
		GroundVertices.Add(FVector(Poly2D[i].X, Poly2D[i].Y, 0.f));
	}
	// Reverse winding (vs CapTris' own CCW/up-facing order) so this cap faces down.
	for (int32 i = 0; i < CapTris.Num(); i += 3)
	{
		GroundTriangles.Add(CapTris[i]);
		GroundTriangles.Add(CapTris[i + 1]);
		GroundTriangles.Add(CapTris[i + 2]);
	}

	TArray<FVector> GroundNormals;
	TArray<FVector2D> GroundUV0;
	TArray<FProcMeshTangent> GroundTangents;
	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(
		GroundVertices, GroundTriangles, GroundUV0, GroundNormals, GroundTangents);

	TArray<FColor> GroundVertexColors;
	MeshComp->CreateMeshSection(0, GroundVertices, GroundTriangles, GroundNormals, GroundUV0, GroundVertexColors, GroundTangents, !bInspectionBottomHidden);
	MeshComp->SetMaterial(0, DynMaterial);
	MeshComp->SetMeshSectionVisible(0, !bInspectionBottomHidden);

	// Sections 1..FloorCount (roof + inter-floor slabs) and FloorCount+1..2*FloorCount (one wall ring
	// per floor, see GetWallSectionIndex) are all built by ApplySlabVisibility, which also decides
	// which one(s) currently have no collision/visibility based on VisibleInteriorLevel - no separate
	// "build them all visible, then hide some" step needed.
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

void APolyBuilding::BuildWallSection(const TArray<FVector2D>& Poly2D, float ZBottom, float ZTop, int32 SectionIndex, bool bCreateCollision, int32 Level)
{
	const int32 N = Poly2D.Num();

	// A real box per edge (AppendWallBox, the same helper AInteriorRoom's own walls use), not a flat
	// single-sided quad - a flat quad only has a normal facing one way, so the 3D preview camera
	// (unlike the near-top-down 2D one, or the always-outside exterior-editor camera) would see
	// straight through it the moment it ends up on the wrong side. ExteriorWallThickness matches
	// AInteriorRoom's own default wall thickness so the exterior shell and the rooms it contains read
	// as the same kind of wall.
	constexpr float ExteriorWallThickness = 20.f;
	const float HalfThickness = ExteriorWallThickness * 0.5f;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;

	for (int32 i = 0; i < N; i++)
	{
		const FVector2D& A2D = Poly2D[i];
		const FVector2D& B2D = Poly2D[(i + 1) % N];

		const float EdgeLength = (B2D - A2D).Size();
		if (EdgeLength <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Dir = (B2D - A2D) / EdgeLength;
		const FVector2D Perp(-Dir.Y, Dir.X);

		// The stretch of this edge between two distances along it, between two heights.
		auto AddSegment = [&](float T0, float T1, float Z0, float Z1)
		{
			if (T1 - T0 <= KINDA_SMALL_NUMBER || Z1 - Z0 <= KINDA_SMALL_NUMBER)
			{
				return;
			}
			const FVector2D P0 = A2D + Dir * T0;
			const FVector2D P1 = A2D + Dir * T1;
			const FVector2D Inner0 = P0 - Perp * HalfThickness;
			const FVector2D Inner1 = P1 - Perp * HalfThickness;
			const FVector2D Outer0 = P0 + Perp * HalfThickness;
			const FVector2D Outer1 = P1 + Perp * HalfThickness;
			AppendWallBox(Vertices, Triangles,
				FVector(Inner0.X, Inner0.Y, Z0), FVector(Inner1.X, Inner1.Y, Z0),
				FVector(Outer0.X, Outer0.Y, Z0), FVector(Outer1.X, Outer1.Y, Z0),
				FVector(Inner0.X, Inner0.Y, Z1), FVector(Inner1.X, Inner1.Y, Z1),
				FVector(Outer0.X, Outer0.Y, Z1), FVector(Outer1.X, Outer1.Y, Z1));
		};

		// Where this floor's openings cut this edge. The polygon may run the other way round from the footprint it was made from.
		TArray<TPair<float, float>> Gaps;
		for (const FPolyWallOpening& Opening : WallOpenings)
		{
			if (Opening.Level != Level || Opening.EdgeIndex < 0 || Opening.EdgeIndex >= N)
			{
				continue;
			}
			const int32 PolyEdge = bPolyReversed ? (N - 2 - Opening.EdgeIndex + N) % N : Opening.EdgeIndex;
			if (PolyEdge != i)
			{
				continue;
			}
			float T0 = Opening.Offset, T1 = Opening.Offset + Opening.Width;
			if (bPolyReversed)
			{
				T0 = EdgeLength - (Opening.Offset + Opening.Width);
				T1 = EdgeLength - Opening.Offset;
			}
			Gaps.Add(TPair<float, float>(FMath::Clamp(T0, 0.f, EdgeLength), FMath::Clamp(T1, 0.f, EdgeLength)));
		}
		if (Gaps.IsEmpty())
		{
			AddSegment(0.f, EdgeLength, ZBottom, ZTop);
			continue;
		}
		Gaps.Sort([](const TPair<float, float>& L, const TPair<float, float>& R) { return L.Key < R.Key; });
		constexpr float OpeningHeight = 240.f;
		float Cursor = 0.f;
		for (const TPair<float, float>& Gap : Gaps)
		{
			AddSegment(Cursor, Gap.Key, ZBottom, ZTop);
			// Above the opening the wall carries on.
			AddSegment(Gap.Key, Gap.Value, ZBottom + OpeningHeight, ZTop);
			Cursor = FMath::Max(Cursor, Gap.Value);
		}
		AddSegment(Cursor, EdgeLength, ZBottom, ZTop);
	}

	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FProcMeshTangent> Tangents;
	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices, Triangles, UV0, Normals, Tangents);

	TArray<FColor> VertexColors;
	MeshComp->CreateMeshSection(SectionIndex, Vertices, Triangles, Normals, UV0, VertexColors, Tangents, bCreateCollision);
	MeshComp->SetMaterial(SectionIndex, DynMaterial);
}

void APolyBuilding::SetWallOpenings(const TArray<FPolyWallOpening>& Openings)
{
	if (WallOpenings == Openings)
	{
		return;
	}
	WallOpenings = Openings;
	ApplySlabVisibility();
}

TArray<FVector> APolyBuilding::GetWallVerticesForTests(int32 Level) const
{
	TArray<FVector> Result;
	if (MeshComp && Level >= 1 && Level <= FloorCount)
	{
		if (const FProcMeshSection* Section = MeshComp->GetProcMeshSection(GetWallSectionIndex(Level)))
		{
			for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
			{
				Result.Add(Vertex.Position);
			}
		}
	}
	return Result;
}

void APolyBuilding::SetVisibleInteriorLevel(int32 Level)
{
	VisibleInteriorLevel = Level;
	ApplySlabVisibility();
}

void APolyBuilding::SetInspectionBottomHidden(bool bNewHidden)
{
	if (bInspectionBottomHidden == bNewHidden) return;
	bInspectionBottomHidden = bNewHidden;
	RebuildMesh();
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

		// Show only the viewed floor's own floor cap (L == VisibleInteriorLevel - 1, i.e. the slab at
		// the viewed floor's own base) - hide every other floor's cap, both above (its ceiling/the
		// roof) and below (every lower floor's own slab). The near-top-down 2D floor-plan camera never
		// needed the "below" half of this: looking almost straight down, the nearest opaque cap
		// already blocks the ones underneath it regardless of whether they're technically still
		// "visible". The 3D preview camera sits back at an oblique angle though, where every lower
		// floor's slab was visible past the edge of the one above it - a stack of floors "peeking out"
		// like the fins on a vent, rather than just the one floor being looked at.
		const bool bHide = (VisibleInteriorLevel > 0 && L != VisibleInteriorLevel - 1);

		// UProceduralMeshComponent has no way to toggle an existing section's collision, so a
		// visibility change here means recreating the section - the geometry is identical either
		// way, only the collision flag differs. Keeping collision off in step with visibility keeps
		// any future 3D-view interaction (see the "3D mode" toggle in the PM's mockup) consistent
		// with what's actually visible, even though interior room/wall editing itself now happens
		// entirely through the 2D floor plan canvas (UInteriorFloorPlanWidget), not 3D raycasts.
		BuildCapSection(CachedPoly2D, CachedCapTris, Z, SectionIndex, /*bCreateCollision=*/!bHide);
		MeshComp->SetMeshSectionVisible(SectionIndex, !bHide);

		// This floor's own exterior wall ring - visible whenever nothing's hidden yet (normal exterior
		// view, VisibleInteriorLevel <= 0) or L is exactly the floor being viewed; every other floor's
		// wall ring hides completely so the 3D interior preview camera (which sits outside the
		// footprint, unlike the near-top-down 2D floor-plan camera) isn't blocked by floors it isn't
		// looking at.
		const int32 WallSectionIndex = GetWallSectionIndex(L);
		const bool bHideWall = (VisibleInteriorLevel > 0 && L != VisibleInteriorLevel);
		BuildWallSection(CachedPoly2D, (L - 1) * FloorHeight, L * FloorHeight, WallSectionIndex, /*bCreateCollision=*/!bHideWall, L);
		MeshComp->SetMeshSectionVisible(WallSectionIndex, !bHideWall);
	}

	// With the bottom platform gone the first floor needs a floor of its own, like every floor above it has (the slab at its base).
	if (bInspectionBottomHidden)
	{
		const int32 FloorSection = GetInspectionFloorSectionIndex();
		const bool bShow = VisibleInteriorLevel <= 1;
		BuildCapSection(CachedPoly2D, CachedCapTris, 0.f, FloorSection, /*bCreateCollision=*/bShow);
		MeshComp->SetMeshSectionVisible(FloorSection, bShow);
	}
}

void APolyBuilding::SetHighlighted(bool bHighlighted)
{
	bIsHighlighted = bHighlighted;
	ApplyBodyColor();
}

void APolyBuilding::SetBuildingType(const FString& NewType)
{
	BuildingType = BuildingTypes::Normalize(NewType);
	ApplyBodyColor();
}

void APolyBuilding::ApplyBodyColor()
{
	const FLinearColor HighlightColor(1.0f, 0.85f, 0.0f);
	CurrentBodyColor = bIsHighlighted ? HighlightColor : BuildingTypes::Color(BuildingType);
	if (DynMaterial)
	{
		DynMaterial->SetVectorParameterValue(TEXT("Color"), CurrentBodyColor);
	}
}
void APolyBuilding::SetBuildingOpacity(float NewOpacity)
{
	if (!DynMaterial)
	{
		return;
	}

	const float ClampedOpacity = FMath::Clamp(NewOpacity, 0.0f, 1.0f);

	DynMaterial->SetScalarParameterValue(
		TEXT("Opacity"),
		ClampedOpacity
	);
}
