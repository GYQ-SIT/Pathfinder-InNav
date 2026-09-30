// Fill out your copyright notice in the Description page of Project Settings.

#include "RoadSegmentActor.h"
#include "PolygonGeometryUtils.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

ARoadSegmentActor::ARoadSegmentActor()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComp = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;
}

void ARoadSegmentActor::BuildRoad(const TArray<FVector2D>& Points, float Width, float WorldZ, const FLinearColor& Color)
{
	if (Points.Num() < 2)
	{
		return;
	}

	// A thin box per segment, not a flat single-sided quad - same reasoning as
	// AInteriorConnectorMarker's own rail/step boxes: a flat quad backface-culls invisible from
	// whichever side it isn't wound toward, which a free-fly camera can easily end up on.
	constexpr float Thickness = 3.f;
	const float Half = Width * 0.5f;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;

	for (int32 Index = 0; Index < Points.Num() - 1; ++Index)
	{
		const FVector2D& A = Points[Index];
		const FVector2D& B = Points[Index + 1];
		const float SegmentLength = (B - A).Size();
		if (SegmentLength <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Dir = (B - A) / SegmentLength;
		const FVector2D Perp(-Dir.Y, Dir.X);

		const FVector2D Left0 = A - Perp * Half;
		const FVector2D Left1 = B - Perp * Half;
		const FVector2D Right0 = A + Perp * Half;
		const FVector2D Right1 = B + Perp * Half;

		AppendWallBox(Vertices, Triangles,
			FVector(Left0.X, Left0.Y, WorldZ), FVector(Left1.X, Left1.Y, WorldZ),
			FVector(Right0.X, Right0.Y, WorldZ), FVector(Right1.X, Right1.Y, WorldZ),
			FVector(Left0.X, Left0.Y, WorldZ + Thickness), FVector(Left1.X, Left1.Y, WorldZ + Thickness),
			FVector(Right0.X, Right0.Y, WorldZ + Thickness), FVector(Right1.X, Right1.Y, WorldZ + Thickness));
	}

	if (Triangles.Num() == 0)
	{
		return;
	}

	FVector2D PointSum = FVector2D::ZeroVector;
	for (const FVector2D& Point : Points)
	{
		PointSum += Point;
	}
	MidpointPosition = PointSum / Points.Num();

	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FProcMeshTangent> Tangents;
	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices, Triangles, UV0, Normals, Tangents);

	TArray<FColor> VertexColors;
	MeshComp->CreateMeshSection(0, Vertices, Triangles, Normals, UV0, VertexColors, Tangents, true);

	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMat)
	{
		UMaterialInstanceDynamic* DynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
		DynMaterial->SetVectorParameterValue(TEXT("Color"), Color);
		MeshComp->SetMaterial(0, DynMaterial);
	}
}
