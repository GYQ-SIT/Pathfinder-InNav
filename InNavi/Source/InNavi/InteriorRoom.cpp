// Fill out your copyright notice in the Description page of Project Settings.


#include "InteriorRoom.h"
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

// ---- Helper: append one box (8 logical corners, 6 hard-edged faces) to the mesh arrays. ----
// Corner naming: P<x><y><z> where x=start/end along the wall edge, y=inner/outer across the wall
// thickness, z=bottom/top. Each quad below is listed in outward-facing CCW order, verified against
// the right-handed (Dir, Perp, Up) frame BuildWalls constructs each box in.
static void AddBox(TArray<FVector>& Vertices, TArray<int32>& Triangles,
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

void AInteriorRoom::BuildWalls(const TArray<FVector2D>& Footprint, float Scale, FVector2D Offset, float FloorBaseZ, float WallHeight, float WallThickness, const TArray<FInteriorOpening>& Openings)
{
	if (Footprint.Num() < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("AInteriorRoom::BuildWalls: room '%s' footprint needs at least 3 points"), *RoomId);
		return;
	}

	CachedFootprint = Footprint;
	CachedScale = Scale;
	CachedOffset = Offset;
	CachedFloorBaseZ = FloorBaseZ;
	CachedWallHeight = WallHeight;
	CachedWallThickness = WallThickness;
	CachedOpenings = Openings;

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

	for (int32 EdgeIndex = 0; EdgeIndex < N; EdgeIndex++)
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

			AddBox(Vertices, Triangles,
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

	UE_LOG(LogTemp, Warning, TEXT("Built room %s: %d edges, %d verts"), *RoomId, N, Vertices.Num());
}
