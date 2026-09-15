// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Building.generated.h"

UCLASS()
class INNAVI_API ABuilding : public AActor
{
	GENERATED_BODY()

public:
	ABuilding();

	// Set up this building's size and name
	void InitBuilding(const FString& InName, FVector Size);

	// Highlight / unhighlight this building
	void SetHighlighted(bool bHighlighted);

	// This building's display name
	UPROPERTY()
	FString BuildingName;

private:
	UPROPERTY()
	UStaticMeshComponent* MeshComp;

	// Dynamic material so we can change color at runtime
	UPROPERTY()
	UMaterialInstanceDynamic* DynMaterial;
};