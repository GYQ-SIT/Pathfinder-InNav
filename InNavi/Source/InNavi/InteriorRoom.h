// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteriorTypes.h"
#include "InteriorRoom.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class INNAVI_API AInteriorRoom : public AActor
{
	GENERATED_BODY()

public:
	AInteriorRoom();

	// Builds this room's walls: one wall-segment box per footprint edge (not a continuous offset
	// ring - simpler, and avoids self-intersection issues that polygon offsetting has on concave
	// room shapes), each split into pieces around any openings on that edge. Footprint/Scale/Offset
	// use the same "pre-scale units x Scale" convention as PolyBuilding. FloorBaseZ positions this
	// floor's walls vertically relative to the building's ground level, e.g.
	// (Level - 1) * building's exterior FloorHeight, so floors stack correctly.
	void BuildWalls(const TArray<FVector2D>& Footprint, float Scale, FVector2D Offset, float FloorBaseZ, float WallHeight, float WallThickness, const TArray<FInteriorOpening>& Openings);

	UPROPERTY()
	FString RoomId;

	UPROPERTY()
	int32 FloorLevel = 1;

private:
	UPROPERTY()
	UProceduralMeshComponent* MeshComp;

	UPROPERTY()
	UMaterialInstanceDynamic* DynMaterial;
};
