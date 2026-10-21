// Fill out your copyright notice in the Description page of Project Settings.


#include "InteriorRoom.h"
#include "PolygonGeometryUtils.h"
#include "RoomTypes.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

AInteriorRoom::AInteriorRoom()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComp = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;

	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
}

// ---- Helper: for one edge, split [0, EdgeLength] into solid wall spans around its openings. ----
struct FWallSpan
{
	float XStart;
	float XEnd;
	float ZBottom;
	float ZTop;
};

static void BuildWallSpansForEdge(float EdgeLength, float WallHeight, const TArray<const FInteriorOpening*>& EdgeOpenings, TArray<FWallSpan>& OutSpans)
{
	float Cursor = 0.f;

	for (const FInteriorOpening* Opening : EdgeOpenings)
	{
		const float OpenStart = FMath::Clamp(Opening->Offset, 0.f, EdgeLength);
		const float OpenEnd = FMath::Clamp(Opening->Offset + Opening->Width, OpenStart, EdgeLength);

		if (OpenStart > Cursor)
		{
			OutSpans.Add({ Cursor, OpenStart, 0.f, WallHeight });
		}

		if (Opening->Type == EInteriorOpeningType::Window)
		{
			const float SillTop = FMath::Clamp(Opening->SillHeight, 0.f, WallHeight);
			if (SillTop > 0.f)
			{
				OutSpans.Add({ OpenStart, OpenEnd, 0.f, SillTop });
			}

			const float LintelBottom = FMath::Clamp(Opening->SillHeight + Opening->Height, 0.f, WallHeight);
			if (LintelBottom < WallHeight)
			{
				OutSpans.Add({ OpenStart, OpenEnd, LintelBottom, WallHeight });
			}
		}
		// Door/Void: nothing generated for this horizontal range - it's a full-height gap.

		Cursor = FMath::Max(Cursor, OpenEnd);
	}

	if (Cursor < EdgeLength)
	{
		OutSpans.Add({ Cursor, EdgeLength, 0.f, WallHeight });
	}
}

void AInteriorRoom::BuildWalls(const TArray<FVector2D>& Footprint, float Scale, FVector2D Offset, float FloorBaseZ, float WallHeight, float WallThickness, const TArray<FInteriorOpening>& Openings, bool bClosedLoop)
{
	const int32 MinPoints = bClosedLoop ? 3 : 2;
	if (Footprint.Num() < MinPoints)
	{
		UE_LOG(LogTemp, Error, TEXT("AInteriorRoom::BuildWalls: '%s' footprint needs at least %d points"), *RoomId, MinPoints);
		return;
	}

	CachedFootprint = Footprint;
	CachedScale = Scale;
	CachedOffset = Offset;
	CachedFloorBaseZ = FloorBaseZ;
	CachedWallHeight = WallHeight;
	CachedWallThickness = WallThickness;
	CachedOpenings = Openings;
	CachedClosedLoop = bClosedLoop;

	RebuildMesh();
}

void AInteriorRoom::SetFloorBaseZ(float NewFloorBaseZ)
{
	if (CachedFootprint.Num() < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("SetFloorBaseZ: room '%s' has no walls built yet"), *RoomId);
		return;
	}

	CachedFloorBaseZ = NewFloorBaseZ;
	RebuildMesh();
}

void AInteriorRoom::ResizeFootprint(const TArray<FVector2D>& NewFootprint)
{
	const int32 MinPoints = CachedClosedLoop ? 3 : 2;
	if (NewFootprint.Num() < MinPoints)
	{
		UE_LOG(LogTemp, Error, TEXT("ResizeFootprint: '%s' needs at least %d points"), *RoomId, MinPoints);
		return;
	}

	CachedFootprint = NewFootprint;
	RebuildMesh();
}

void AInteriorRoom::AddOpening(const FInteriorOpening& Opening)
{
	CachedOpenings.Add(Opening);
	RebuildMesh();
}

void AInteriorRoom::RemoveOpening(const FString& OpeningId)
{
	const int32 RemovedCount = CachedOpenings.RemoveAll([&OpeningId](const FInteriorOpening& Opening)
	{
		return Opening.Id == OpeningId;
	});

	if (RemovedCount > 0)
	{
		RebuildMesh();
	}
}

void AInteriorRoom::MoveOpening(const FString& OpeningId, float NewOffsetWorldUnits)
{
	for (FInteriorOpening& Opening : CachedOpenings)
	{
		if (Opening.Id == OpeningId)
		{
			Opening.Offset = NewOffsetWorldUnits;
			RebuildMesh();
			return;
		}
	}
}

void AInteriorRoom::SetOpeningConnectsTo(const FString& OpeningId, const FString& NewConnectsTo)
{
	for (FInteriorOpening& Opening : CachedOpenings)
	{
		if (Opening.Id == OpeningId)
		{
			Opening.ConnectsTo = NewConnectsTo;
			return;
		}
	}
}

void AInteriorRoom::RebuildMesh()
{
	const int32 N = CachedFootprint.Num();

	TArray<FVector2D> Poly2D;
	Poly2D.SetNum(N);
	for (int32 i = 0; i < N; i++)
	{
		Poly2D[i] = FVector2D((CachedFootprint[i].X + CachedOffset.X) * CachedScale, (CachedFootprint[i].Y + CachedOffset.Y) * CachedScale);
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	const float HalfThickness = CachedWallThickness * 0.5f;

	// Closed (room): N points -> N edges, wrapping back to point 0. Open (standalone wall): N
	// points -> N-1 edges, no closing edge.
	const int32 NumEdges = CachedClosedLoop ? N : N - 1;
	for (int32 EdgeIndex = 0; EdgeIndex < NumEdges; EdgeIndex++)
	{
		const FVector2D& A2D = Poly2D[EdgeIndex];
		const FVector2D& B2D = Poly2D[(EdgeIndex + 1) % N];

		const float EdgeLength = (B2D - A2D).Size();
		if (EdgeLength <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Dir = (B2D - A2D) / EdgeLength;
		const FVector2D Perp(-Dir.Y, Dir.X);

		TArray<const FInteriorOpening*> EdgeOpenings;
		for (const FInteriorOpening& Opening : CachedOpenings)
		{
			if (Opening.EdgeIndex == EdgeIndex)
			{
				EdgeOpenings.Add(&Opening);
			}
		}
		EdgeOpenings.Sort([](const FInteriorOpening& A, const FInteriorOpening& B) { return A.Offset < B.Offset; });

		TArray<FWallSpan> Spans;
		BuildWallSpansForEdge(EdgeLength, CachedWallHeight, EdgeOpenings, Spans);

		for (const FWallSpan& Span : Spans)
		{
			const FVector2D Inner0 = A2D + Dir * Span.XStart - Perp * HalfThickness;
			const FVector2D Inner1 = A2D + Dir * Span.XEnd   - Perp * HalfThickness;
			const FVector2D Outer0 = A2D + Dir * Span.XStart + Perp * HalfThickness;
			const FVector2D Outer1 = A2D + Dir * Span.XEnd   + Perp * HalfThickness;

			const float ZBottom = CachedFloorBaseZ + Span.ZBottom;
			const float ZTop = CachedFloorBaseZ + Span.ZTop;

			AppendWallBox(Vertices, Triangles,
				FVector(Inner0.X, Inner0.Y, ZBottom), FVector(Inner1.X, Inner1.Y, ZBottom),
				FVector(Outer0.X, Outer0.Y, ZBottom), FVector(Outer1.X, Outer1.Y, ZBottom),
				FVector(Inner0.X, Inner0.Y, ZTop),    FVector(Inner1.X, Inner1.Y, ZTop),
				FVector(Outer0.X, Outer0.Y, ZTop),    FVector(Outer1.X, Outer1.Y, ZTop));
		}
	}

	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FProcMeshTangent> Tangents;
	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices, Triangles, UV0, Normals, Tangents);

	TArray<FColor> VertexColors;
	MeshComp->CreateMeshSection(0, Vertices, Triangles, Normals, UV0, VertexColors, Tangents, true);

	if (!DynMaterial)
	{
		UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (BaseMat)
		{
			DynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
			DynMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.85f, 0.85f, 0.82f));
		}
	}
	MeshComp->SetMaterial(0, DynMaterial);

	// -- Section 1: floor slab - a flat, upward-facing cap of the room's own footprint. Not visible
	// in the 2D floor plan (that's a UMG overlay drawn on top of an opaque background, not a render
	// of this mesh), but needed for the 3D preview mode to have an actual floor to look down at
	// instead of empty space between the walls. Skipped for standalone open walls (CachedClosedLoop
	// false) - ear clipping needs a closed polygon, and there's no "floor" under a freestanding wall
	// segment anyway. Hidden/shown together with the walls above for free, since both live on this
	// same actor (see AInteriorGenerator::SetActiveFloor's per-floor SetActorHiddenInGame).
	if (CachedClosedLoop)
	{
		TArray<int32> CapTris;
		TriangulatePolygon(Poly2D, CapTris);

		// The ground floor's slab (Level 1, CachedFloorBaseZ == 0) sits exactly coplanar with the
		// level's own Z=0 ground plane - two flat surfaces sharing a depth means z-fighting, which at
		// the 3D preview's shallow oblique angle can make the whole slab flicker away to nothing. A
		// small upward nudge (imperceptible at normal room scale) gives it its own depth so it always
		// wins against whatever's underneath.
		constexpr float FloorSlabZOffset = 2.f;

		TArray<FVector> FloorVertices;
		FloorVertices.Reserve(Poly2D.Num());
		for (const FVector2D& Point : Poly2D)
		{
			FloorVertices.Add(FVector(Point.X, Point.Y, CachedFloorBaseZ + FloorSlabZOffset));
		}

		TArray<int32> FloorTriangles;
		FloorTriangles.Reserve(CapTris.Num());
		// Same winding-flip APolyBuilding::BuildCapSection uses for its own upward-facing roof/slab
		// sections (TriangulatePolygon's raw output needs this to actually face up once rebuilt into
		// a section-local vertex buffer) - keep the two in sync if that ever changes.
		for (int32 i = 0; i < CapTris.Num(); i += 3)
		{
			FloorTriangles.Add(CapTris[i]);
			FloorTriangles.Add(CapTris[i + 2]);
			FloorTriangles.Add(CapTris[i + 1]);
		}

		TArray<FVector> FloorNormals;
		TArray<FVector2D> FloorUV0;
		TArray<FProcMeshTangent> FloorTangents;
		UKismetProceduralMeshLibrary::CalculateTangentsForMesh(FloorVertices, FloorTriangles, FloorUV0, FloorNormals, FloorTangents);

		TArray<FColor> FloorVertexColors;
		MeshComp->CreateMeshSection(1, FloorVertices, FloorTriangles, FloorNormals, FloorUV0, FloorVertexColors, FloorTangents, true);

		if (!FloorDynMaterial)
		{
			UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			if (BaseMat)
			{
				FloorDynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
				FloorDynMaterial->SetVectorParameterValue(TEXT("Color"), FloorColour());
			}
		}
		MeshComp->SetMaterial(1, FloorDynMaterial);
	}
	else
	{
		MeshComp->ClearMeshSection(1);
	}

	UE_LOG(LogTemp, Warning, TEXT("Built room %s: %d edges, %d verts"), *RoomId, N, Vertices.Num());
}

FLinearColor AInteriorRoom::FloorColour() const
{
	if (bHighlightedNow)
	{
		return FLinearColor(0.55f, 1.0f, 0.6f);
	}
	if (bTypeColourShown && CachedClosedLoop)
	{
		return RoomTypes::Color(RoomType);
	}
	return FLinearColor(0.55f, 0.55f, 0.58f);
}

void AInteriorRoom::RefreshFloorTint()
{
	if (FloorDynMaterial)
	{
		FloorDynMaterial->SetVectorParameterValue(TEXT("Color"), FloorColour());
	}
}

void AInteriorRoom::SetTypeColourShown(bool bShown)
{
	bTypeColourShown = bShown;
	RefreshFloorTint();
}

void AInteriorRoom::SetHighlighted(bool bHighlighted)
{
	bHighlightedNow = bHighlighted;
	if (DynMaterial)
	{
		DynMaterial->SetVectorParameterValue(TEXT("Color"), bHighlighted ? FLinearColor(0.55f, 1.0f, 0.6f) : FLinearColor(0.85f, 0.85f, 0.82f));
	}
	RefreshFloorTint();
}
