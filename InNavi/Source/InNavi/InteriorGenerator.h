// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteriorTypes.h"
#include "InteriorGenerator.generated.h"

class ABuildingGenerator;
class AInteriorRoom;

// GC-safe wrapper so RoomsByFloor can be a TMap<int32, ...> - UHT doesn't support a TArray
// directly as a TMap value, but a UPROPERTY TArray inside a plain USTRUCT is fine.
USTRUCT()
struct FInteriorRoomList
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<AInteriorRoom*> Rooms;
};

// Per-floor settings that outlive the JSON parse - needed so AddFloor knows what defaults to
// inherit for a brand new floor.
struct FInteriorFloorInfo
{
	float WallHeight = 280.f;
	float WallThickness = 20.f;
};

UCLASS()
class INNAVI_API AInteriorGenerator : public AActor
{
	GENERATED_BODY()

public:
	AInteriorGenerator();

	// Loads Data/Interiors/{BuildingId}.json and (re)generates that building's interior: destroys
	// whatever this generator had loaded before, spawns one AInteriorRoom per room per floor, and
	// syncs the exterior shell's floor count to match (see APolyBuilding::SetFloorCount) - the
	// interior's floor list is the source of truth for how tall the building actually is.
	// Returns how many rooms were spawned, or -1 if the building/file couldn't be loaded.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	int32 LoadInteriorForBuilding(const FString& BuildingId);

	// Shows only the given floor's rooms, hides the rest.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void SetActiveFloor(int32 Level);

	UFUNCTION(BlueprintPure, Category = "Interior")
	int32 GetActiveFloor() const { return ActiveFloor; }

	// Adds a new, empty floor above the highest currently-loaded floor for the currently loaded
	// building, and grows the exterior shell to match (APolyBuilding::SetFloorCount) - the new
	// floor has no rooms yet, Phase 3's drawing tools are what would populate it.
	// WallHeight/WallThickness default to the topmost existing floor's values (or 280/20 if this
	// is the first floor). Switches the active floor to the new one.
	// Returns the new floor's level, or -1 if no building interior is currently loaded.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	int32 AddFloor();

	// Removes the given floor (can be any floor, not just the top one - matches "delete whatever
	// floor you're currently viewing"). Destroys that floor's rooms, then shifts every floor above
	// it down by one level - renumbering them, moving their rooms' vertical position, and
	// regenerating those rooms' meshes (see AInteriorRoom::SetFloorBaseZ). Shrinks the exterior
	// shell to match. Refuses to remove the last remaining floor. Returns true on success.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool RemoveFloor(int32 Level);

	UFUNCTION(BlueprintPure, Category = "Interior")
	FString GetLoadedBuildingId() const { return LoadedBuildingId; }

	// Nav nodes (Path Node/Elevator/Stairs) for the currently loaded building, all floors. Not yet
	// turned into actors - representation is pending alignment with the navigation
	// (NavMesh/NavLinks) team on what they actually need to consume.
	UFUNCTION(BlueprintPure, Category = "Interior")
	const TArray<FInteriorNavNode>& GetNavNodes() const { return NavNodes; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	ABuildingGenerator* Generator;

	UPROPERTY()
	FString LoadedBuildingId;

	int32 ActiveFloor = 1;

	// Rooms spawned for the currently loaded building, keyed by floor level.
	UPROPERTY()
	TMap<int32, FInteriorRoomList> RoomsByFloor;

	TArray<FInteriorNavNode> NavNodes;

	// Wall defaults per loaded floor, keyed by level - kept around after the JSON parse so
	// AddFloor has something sensible to inherit.
	TMap<int32, FInteriorFloorInfo> FloorInfoByLevel;
};
