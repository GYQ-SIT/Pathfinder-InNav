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

	// Walls: one continuous ring (see ComputeWallCorners and AppendWallSpan), not one thin box per edge - the boxes left a notch at every
	// corner and were each shaded a little differently, so a curved front read as a row of pieces. The ring is a real thick wall, not a flat
	// single-sided quad, for the same reason as ARoadSegmentActor used to give: a flat quad backface-culls invisible from whichever side a
	// free-fly camera ends up on.
	constexpr float WallThickness = 20.f;
	TArray<FWallCorner> Corners;
	ComputeWallCorners(Footprint, WallThickness * 0.5f, Corners);

	FShadedMesh Mesh;
	const int32 NumPoints = Footprint.Num();
	for (int32 Index = 0; Index < NumPoints; ++Index)
	{
		const int32 Next = (Index + 1) % NumPoints;
		AppendWallSpan(Mesh,
			MakeCornerWallEnd(Footprint[Index], Corners[Index], /*bStart=*/true),
			MakeCornerWallEnd(Footprint[Next], Corners[Next], /*bStart=*/false),
			0.f, HeightWorldUnits);
	}

	// Flat roof cap, so the block doesn't look hollow/open from above.
	AppendFlatCap(Mesh, Footprint, HeightWorldUnits);

	if (Mesh.Triangles.Num() == 0)
	{
		return;
	}

	TArray<FProcMeshTangent> Tangents;
	TArray<FColor> VertexColors;
	MeshComp->CreateMeshSection(0, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UV0, VertexColors, Tangents, true);

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
