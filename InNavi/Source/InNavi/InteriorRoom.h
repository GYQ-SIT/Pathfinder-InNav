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

	// Repositions this room vertically and regenerates the mesh - used when removing a floor
	// shifts every floor above it down by one level. Footprint/openings/etc. are unchanged, only
	// FloorBaseZ moves.
	void SetFloorBaseZ(float NewFloorBaseZ);

	UPROPERTY()
	FString RoomId;

	UPROPERTY()
	FString RoomName;

	UPROPERTY()
	int32 FloorLevel = 1;

	// Access control, set by the "Create Room" step in the editor (public/private, key required).
	// Not enforced by anything yet - this is just where the authored data lives; whichever system
	// gates entry (navigation, gameplay) reads these off the room it cares about.
	UPROPERTY()
	bool bIsPublic = true;

	UPROPERTY()
	bool bRequiresKey = false;

private:
	// Regenerates the mesh from the cached inputs below. Called by both BuildWalls and
	// SetFloorBaseZ.
	void RebuildMesh();

	TArray<FVector2D> CachedFootprint;
	float CachedScale = 1.f;
	FVector2D CachedOffset = FVector2D::ZeroVector;
	float CachedFloorBaseZ = 0.f;
	float CachedWallHeight = 280.f;
	float CachedWallThickness = 20.f;
	TArray<FInteriorOpening> CachedOpenings;

	UPROPERTY()
	UProceduralMeshComponent* MeshComp;

	UPROPERTY()
	UMaterialInstanceDynamic* DynMaterial;
};
