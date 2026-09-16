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
	// bClosedLoop: true wraps the last point back to the first (a closed room, N points -> N walls -
	// the normal case); false stops after the last segment (a standalone open wall/partition line,
	// N points -> N-1 walls, no closing edge). Footprint needs at least 3 points when closed, 2 when
	// open.
	void BuildWalls(const TArray<FVector2D>& Footprint, float Scale, FVector2D Offset, float FloorBaseZ, float WallHeight, float WallThickness, const TArray<FInteriorOpening>& Openings, bool bClosedLoop);

	// Repositions this room vertically and regenerates the mesh - used when removing a floor
	// shifts every floor above it down by one level. Footprint/openings/etc. are unchanged, only
	// FloorBaseZ moves.
	void SetFloorBaseZ(float NewFloorBaseZ);

	// Replaces the footprint (e.g. after a resize - see AInteriorGenerator::ResizeRoom) while
	// keeping every other cached input (scale/offset/floor height/wall height/thickness/openings/
	// closed-loop) the same, and regenerates the mesh. Openings' EdgeIndex/Offset aren't remapped -
	// fine for now since nothing that calls this has openings yet.
	void ResizeFootprint(const TArray<FVector2D>& NewFootprint);

	// Adds a single opening (e.g. from the "add door" tool - see AInteriorGenerator::AddDoorToRoom)
	// to whatever openings this room already has, and regenerates the mesh. Unlike BuildWalls, this
	// doesn't replace the footprint or any other cached input, just appends one more opening.
	void AddOpening(const FInteriorOpening& Opening);

	// Removes a single opening by id (e.g. from selecting a door and deleting it independently of
	// its room - see AInteriorGenerator::DeleteDoor) and regenerates the mesh, so the wall goes back
	// to solid there. No-op if no opening with that id exists.
	void RemoveOpening(const FString& OpeningId);

	// Slides a single opening (by id) to a new Offset along the same edge it's already on (e.g.
	// from dragging a door - see AInteriorGenerator::MoveDoor) and regenerates the mesh. No-op if no
	// opening with that id exists.
	void MoveOpening(const FString& OpeningId, float NewOffsetWorldUnits);

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

	// The footprint/openings as originally given to BuildWalls - what serializing this room back
	// to JSON (see InteriorGenerator::SerializeCurrentInterior) reads from, so a saved-then-reloaded
	// room round-trips exactly.
	const TArray<FVector2D>& GetFootprint() const { return CachedFootprint; }
	const TArray<FInteriorOpening>& GetOpenings() const { return CachedOpenings; }
	bool IsClosedLoop() const { return CachedClosedLoop; }

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
	bool CachedClosedLoop = true;

	UPROPERTY()
	UProceduralMeshComponent* MeshComp;

	UPROPERTY()
	UMaterialInstanceDynamic* DynMaterial;
};
