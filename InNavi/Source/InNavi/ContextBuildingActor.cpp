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
	TArray<FWallEdge> Edges;
	Edges.SetNum(NumPoints);
	for (int32 Index = 0; Index < NumPoints; ++Index)
	{
		FWallPiece Piece;
		Piece.T1 = FVector2D::Distance(Footprint[Index], Footprint[(Index + 1) % NumPoints]);
		Piece.Z1 = HeightWorldUnits;
		Edges[Index].A = Footprint[Index];
		Edges[Index].B = Footprint[(Index + 1) % NumPoints];
		Edges[Index].Pieces.Add(Piece);
	}
	AppendWallEdges(Mesh, Edges, Corners, WallThickness * 0.5f, /*bClosed=*/true);

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
