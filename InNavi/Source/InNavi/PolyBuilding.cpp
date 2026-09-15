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

	// Enable collision so it can be clicked (ray-cast)
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
}

void APolyBuilding::BuildFromFootprint(const TArray<FVector2D>& Footprint, float Height)
{
	const int32 N = Footprint.Num();
	if (N < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("Footprint needs at least 3 points"));
		return;
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;

	// Bottom vertices (z=0): indices 0 .. N-1
	for (int32 i = 0; i < N; i++)
	{
		Vertices.Add(FVector(Footprint[i].X, Footprint[i].Y, 0.f));
	}
	// Top vertices (z=Height): indices N .. 2N-1
	for (int32 i = 0; i < N; i++)
	{
		Vertices.Add(FVector(Footprint[i].X, Footprint[i].Y, Height));
	}

	// Side walls
	for (int32 i = 0; i < N; i++)
	{
		int32 next = (i + 1) % N;
		int32 b0 = i;
		int32 b1 = next;
		int32 t0 = i + N;
		int32 t1 = next + N;

		Triangles.Add(b0); Triangles.Add(t0); Triangles.Add(b1);
		Triangles.Add(b1); Triangles.Add(t0); Triangles.Add(t1);
	}

	// Top face
	for (int32 i = 1; i < N - 1; i++)
	{
		Triangles.Add(N);
		Triangles.Add(N + i + 1);
		Triangles.Add(N + i);
	}

	// Bottom face
	for (int32 i = 1; i < N - 1; i++)
	{
		Triangles.Add(0);
		Triangles.Add(i + 1);
		Triangles.Add(i);
	}

	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FProcMeshTangent> Tangents;

	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(
		Vertices, Triangles, UV0, Normals, Tangents);

	TArray<FColor> VertexColors;
	MeshComp->CreateMeshSection(0, Vertices, Triangles, Normals, UV0, VertexColors, Tangents, true);

	// Set up a dynamic material so we can change color for highlight
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMat)
	{
		DynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
		MeshComp->SetMaterial(0, DynMaterial);
	}

	SetHighlighted(false);

	UE_LOG(LogTemp, Warning, TEXT("Built building %s: %d verts, height %f"), *BuildingId, N, Height);
}

void APolyBuilding::SetHighlighted(bool bHighlighted)
{
	if (!DynMaterial)
	{
		return;
	}

	if (bHighlighted)
	{
		DynMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.85f, 0.0f)); // yellow
	}
	else
	{
		DynMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.6f, 0.6f, 0.6f)); // gray
	}
}