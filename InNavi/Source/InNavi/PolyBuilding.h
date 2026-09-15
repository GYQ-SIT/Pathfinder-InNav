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

	// Build the 3D mesh by extruding a 2D polygon footprint up to a height.
	// Offset shifts the whole building in the XY plane (in the same units as the footprint, before scale).
	void BuildFromFootprint(const TArray<FVector2D>& Footprint, float Height, float Scale, FVector2D Offset);

	// Highlight / unhighlight this building
	void SetHighlighted(bool bHighlighted);

	// Data
	UPROPERTY()
	FString BuildingId;

	UPROPERTY()
	FString BuildingName;

	UPROPERTY()
	FVector2D BuildingOffset;

	UPROPERTY()
	float BuildingHeight;

private:
	UPROPERTY()
	UProceduralMeshComponent* MeshComp;

	UPROPERTY()
	UMaterialInstanceDynamic* DynMaterial;
};