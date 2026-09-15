// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PolyBuilding.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class INNAVI_API APolyBuilding : public AActor
{
	GENERATED_BODY()

public:
	APolyBuilding();

	// Build the 3D mesh by extruding a 2D polygon footprint up to a height
	void BuildFromFootprint(const TArray<FVector2D>& Footprint, float Height);

	// Highlight / unhighlight this building
	void SetHighlighted(bool bHighlighted);

	// This building's id and name (e.g. "E2", "Block E2")
	UPROPERTY()
	FString BuildingId;

	UPROPERTY()
	FString BuildingName;

private:
	UPROPERTY()
	UProceduralMeshComponent* MeshComp;

	UPROPERTY()
	UMaterialInstanceDynamic* DynMaterial;
};