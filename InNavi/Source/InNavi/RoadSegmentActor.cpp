// Fill out your copyright notice in the Description page of Project Settings.

#include "RoadSegmentActor.h"
#include "PolygonGeometryUtils.h"
#include "ProceduralMeshComponent.h"
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

	const float Half = Width * 0.5f;

	// A road is flat paint on the ground, not a solid: one ribbon that follows the whole line, with no thickness. It used to be a thin box per
	// segment, which left a gap on the outside of every bend, put an end cap inside every joint, and (the boxes overlapping where roads meet)
	// made the shadows fight each other into a checked pattern. Now the bends are rounded first (SmoothPolyline), the ribbon turns them with
	// a mitred corner, each open end gets a half disc so roads that meet end to end close up, and the ribbon casts no shadow. It is built
	// facing up and again facing down, so a camera under the ground still sees it, as a flat quad would not be seen from behind.
	TArray<FVector2D> Smoothed;
	SmoothPolyline(Points, Width * 2.f, Smoothed);

	FShadedMesh Mesh;
	AppendFlatRibbon(Mesh, Smoothed, Half, WorldZ, /*bTwoSided=*/true, /*bRoundEnds=*/true);

	if (Mesh.Triangles.Num() == 0)
	{
		return;
	}

	FVector2D PointSum = FVector2D::ZeroVector;
	for (const FVector2D& Point : Points)
	{
		PointSum += Point;
	}
	MidpointPosition = PointSum / Points.Num();
	Bounds2D = FBox2D(ForceInit);
	for (const FVector2D& Point : Points)
	{
		Bounds2D += Point;
	}
	Bounds2D = FBox2D(Bounds2D.Min - FVector2D(Half, Half), Bounds2D.Max + FVector2D(Half, Half));

	TArray<FProcMeshTangent> Tangents;
	TArray<FColor> VertexColors;
	MeshComp->CreateMeshSection(0, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UV0, VertexColors, Tangents, true);
	MeshComp->SetCastShadow(false);

	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMat)
	{
		UMaterialInstanceDynamic* DynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
		DynMaterial->SetVectorParameterValue(TEXT("Color"), Color);
		MeshComp->SetMaterial(0, DynMaterial);
	}
}
