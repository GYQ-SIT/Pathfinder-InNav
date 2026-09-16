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

	// Build the 3D mesh by extruding a 2D polygon footprint into FloorCount floors of FloorHeight
	// each. Offset shifts the whole building in the XY plane (in the same units as the footprint,
	// before scale). Footprint/Scale/Offset/FloorCount/FloorHeight are cached so the mesh can be
	// regenerated later (see SetFloorCount) without needing them passed in again.
	void BuildFromFootprint(const TArray<FVector2D>& Footprint, float Scale, FVector2D Offset, int32 FloorCount, float FloorHeight);

	// Changes floor count and regenerates the exterior mesh at the new height. This is how the
	// interior editor's "Add floor" (or remove floor) stays in sync with the exterior shell -
	// interior floor count is the source of truth, this just re-extrudes to match it.
	// NewFloorCount must be >= 1; smaller values are clamped.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetFloorCount(int32 NewFloorCount);

	UFUNCTION(BlueprintPure, Category = "Building")
	int32 GetFloorCount() const { return FloorCount; }

	UFUNCTION(BlueprintPure, Category = "Building")
	float GetFloorHeight() const { return FloorHeight; }

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
	// Regenerates the mesh section from the cached footprint/scale/offset and current
	// FloorCount/FloorHeight. Called by both BuildFromFootprint and SetFloorCount.
	void RebuildMesh();

	TArray<FVector2D> CachedFootprint;
	float CachedScale = 1.f;
	int32 FloorCount = 1;
	float FloorHeight = 300.f;

	UPROPERTY()
	UProceduralMeshComponent* MeshComp;

	UPROPERTY()
	UMaterialInstanceDynamic* DynMaterial;
};