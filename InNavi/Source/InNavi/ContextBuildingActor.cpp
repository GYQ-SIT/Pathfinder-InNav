// Fill out your copyright notice in the Description page of Project Settings.

#include "ContextBuildingActor.h"
#include "PolygonGeometryUtils.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

AContextBuildingActor::AContextBuildingActor()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComp = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;
}

void AContextBuildingActor::BuildFromFootprint(const TArray<FVector2D>& Footprint, float HeightWorldUnits, const FString& InBuildingType)
{
	BuildingType = BuildingTypes::Normalize(InBuildingType);
	CurrentBodyColor = BuildingTypes::Color(BuildingType);
	if (Footprint.Num() < 3)
	{
		return;
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;

	// Walls - one thin box per edge (not a flat single-sided quad), same reasoning as
	// ARoadSegmentActor::BuildRoad: a flat quad backface-culls invisible from whichever side a
	// free-fly camera ends up on.
	constexpr float WallThickness = 20.f;
	const int32 NumPoints = Footprint.Num();
	for (int32 Index = 0; Index < NumPoints; ++Index)
	{
		const FVector2D& A = Footprint[Index];
		const FVector2D& B = Footprint[(Index + 1) % NumPoints];
		const float SegmentLength = (B - A).Size();
		if (SegmentLength <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Dir = (B - A) / SegmentLength;
		const FVector2D Perp(-Dir.Y, Dir.X);
		const FVector2D Inner0 = A - Perp * (WallThickness * 0.5f);
		const FVector2D Inner1 = B - Perp * (WallThickness * 0.5f);
		const FVector2D Outer0 = A + Perp * (WallThickness * 0.5f);
		const FVector2D Outer1 = B + Perp * (WallThickness * 0.5f);

		AppendWallBox(Vertices, Triangles,
			FVector(Inner0.X, Inner0.Y, 0.f), FVector(Inner1.X, Inner1.Y, 0.f),
			FVector(Outer0.X, Outer0.Y, 0.f), FVector(Outer1.X, Outer1.Y, 0.f),
			FVector(Inner0.X, Inner0.Y, HeightWorldUnits), FVector(Inner1.X, Inner1.Y, HeightWorldUnits),
			FVector(Outer0.X, Outer0.Y, HeightWorldUnits), FVector(Outer1.X, Outer1.Y, HeightWorldUnits));
	}

	// Flat roof cap, so the block doesn't look hollow/open from above.
	TArray<int32> CapTriangles;
	TriangulatePolygon(Footprint, CapTriangles);
	const int32 CapVertexStart = Vertices.Num();
	for (const FVector2D& Point : Footprint)
	{
		Vertices.Add(FVector(Point.X, Point.Y, HeightWorldUnits));
	}
	// CapTriangles is CCW (faces up); swap the last two indices per triangle to keep that up-facing
	// winding once remapped into this mesh's own vertex buffer - same convention as
	// APolyBuilding::BuildCapSection.
	for (int32 Index = 0; Index < CapTriangles.Num(); Index += 3)
	{
		Triangles.Add(CapVertexStart + CapTriangles[Index]);
		Triangles.Add(CapVertexStart + CapTriangles[Index + 2]);
		Triangles.Add(CapVertexStart + CapTriangles[Index + 1]);
	}

	if (Triangles.Num() == 0)
	{
		return;
	}

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
		DynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
		MeshComp->SetMaterial(0, DynMaterial);
		ApplyBodyColor();
	}
}

void AContextBuildingActor::ApplyBodyColor()
{
	CurrentBodyColor = BuildingTypes::Color(BuildingType);
	if (DynMaterial)
	{
		DynMaterial->SetVectorParameterValue(TEXT("Color"), CurrentBodyColor);
	}
}
