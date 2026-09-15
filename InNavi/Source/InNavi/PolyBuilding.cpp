// Fill out your copyright notice in the Description page of Project Settings.


#include "PolyBuilding.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"

APolyBuilding::APolyBuilding()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComp = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;
}

void APolyBuilding::BeginPlay()
{
	Super::BeginPlay();

	// --- TEST: hard-coded L-shape footprint ---
	TArray<FVector2D> TestFootprint;
	TestFootprint.Add(FVector2D(0.f, 0.f));
	TestFootprint.Add(FVector2D(400.f, 0.f));
	TestFootprint.Add(FVector2D(400.f, 200.f));
	TestFootprint.Add(FVector2D(200.f, 200.f));
	TestFootprint.Add(FVector2D(200.f, 400.f));
	TestFootprint.Add(FVector2D(0.f, 400.f));

	BuildFromFootprint(TestFootprint, 500.f);
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

	// Side walls: for each edge, make a quad (2 triangles)
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

	// Top face: fan triangulation from first top vertex
	for (int32 i = 1; i < N - 1; i++)
	{
		Triangles.Add(N);
		Triangles.Add(N + i + 1);
		Triangles.Add(N + i);
	}

	// Bottom face: fan triangulation (reverse winding)
	for (int32 i = 1; i < N - 1; i++)
	{
		Triangles.Add(0);
		Triangles.Add(i + 1);
		Triangles.Add(i);
	}

	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FProcMeshTangent> Tangents;

	// Auto-calculate normals and tangents so the surfaces render correctly
	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(
		Vertices, Triangles, UV0, Normals, Tangents);

	TArray<FColor> VertexColors;
	MeshComp->CreateMeshSection(0, Vertices, Triangles, Normals, UV0, VertexColors, Tangents, false);

	UE_LOG(LogTemp, Warning, TEXT("Built polygon building: %d verts, height %f"), N, Height);
}