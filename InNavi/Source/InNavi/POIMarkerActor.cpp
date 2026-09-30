// Fill out your copyright notice in the Description page of Project Settings.

#include "POIMarkerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

APOIMarkerActor::APOIMarkerActor()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void APOIMarkerActor::BuildMarker(const FVector2D& WorldLocation, float Radius, const FLinearColor& Color)
{
	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!SphereMesh)
	{
		return;
	}
	MeshComp->SetStaticMesh(SphereMesh);

	// The engine's basic sphere has a 50-unit radius at scale 1 - scale it to the Radius callers
	// actually asked for, then sit it on the ground (its own Radius above WorldLocation's Z) instead
	// of centering it there, so it doesn't render half-buried.
	constexpr float EngineSphereRadius = 50.f;
	const float MeshScale = Radius / EngineSphereRadius;
	SetActorScale3D(FVector(MeshScale));
	SetActorLocation(FVector(WorldLocation.X, WorldLocation.Y, Radius));

	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMat)
	{
		UMaterialInstanceDynamic* DynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
		DynMaterial->SetVectorParameterValue(TEXT("Color"), Color);
		MeshComp->SetMaterial(0, DynMaterial);
	}
}
