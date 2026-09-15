// Fill out your copyright notice in the Description page of Project Settings.


#include "Building.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ABuilding::ABuilding()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeAsset.Succeeded())
	{
		MeshComp->SetStaticMesh(CubeAsset.Object);
	}

	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
}

void ABuilding::InitBuilding(const FString& InName, FVector Size)
{
	BuildingName = InName;
	MeshComp->SetWorldScale3D(Size / 100.f);

	// Create a dynamic material instance based on the engine's basic material
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMat)
	{
		DynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
		MeshComp->SetMaterial(0, DynMaterial);
	}

	// Start un-highlighted
	SetHighlighted(false);
}

void ABuilding::SetHighlighted(bool bHighlighted)
{
	if (!DynMaterial)
	{
		return;
	}

	// BasicShapeMaterial has a "Color" parameter we can drive
	if (bHighlighted)
	{
		DynMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.85f, 0.0f)); // yellow
	}
	else
	{
		DynMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.6f, 0.6f, 0.6f)); // gray
	}
}