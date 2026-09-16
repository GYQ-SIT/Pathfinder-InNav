// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteriorTypes.h"
#include "InteriorGenerator.generated.h"

class ABuildingGenerator;
class AInteriorRoom;
class FJsonObject;

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

	// (Re)generates a building's interior from whatever interior data is stored for it in the
	// currently loaded project (see BuildingGenerator::GetInteriorDataForBuilding - interior data
	// lives inside the same project file as the exterior building list, keyed to this specific
	// building in this specific project, not just an id string shared globally). Destroys whatever
	// this generator had loaded before, spawns one AInteriorRoom per room per floor, and syncs the
	// exterior shell's floor count to match (see APolyBuilding::SetFloorCount).
	// If the building has no interior data yet, this is not an error - it starts a blank interior
	// (0 floors) ready for AddFloor, same as a brand-new building.
	// Returns how many rooms were spawned, or -1 if the building itself doesn't exist.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	int32 LoadInteriorForBuilding(const FString& BuildingId);

	// Serializes the currently loaded building's interior (all floors/rooms/openings/nav nodes) and
	// stashes it in BuildingGenerator via SetInteriorDataForBuilding, so a following
	// BuildingGenerator::SaveBuildingsToFile includes it. Call this before saving the project -
	// otherwise in-progress interior edits aren't reflected in the save. No-op if nothing is loaded.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void SyncInteriorToBuildingGenerator();

	// Destroys every currently-spawned room and forgets all floor/nav-node state, without loading
	// anything new - back to the state right after BeginPlay. Call this before starting a New
	// project or before BuildingGenerator::ClearAllBuildings, so this generator doesn't keep pointing
	// at rooms whose building actor is about to be destroyed.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void ClearLoadedInterior();

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

	// Plan A-for-interiors: builds a room on the currently active floor of the currently loaded
	// building from a freehand-drawn footprint (reuses the active floor's wallHeight/wallThickness;
	// no openings - those come later from the window/void tools). RoomId/RoomName are optional -
	// leave empty to auto-generate an id matching the {Building}-{Floor:2}-{Room:2} convention.
	// Fails (returns nullptr) if nothing is loaded, the footprint is invalid/self-intersecting, or
	// RoomId is already taken.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	AInteriorRoom* CreateRoomFromFootprint(const TArray<FVector2D>& Footprint, const FString& RoomId, const FString& RoomName, bool bIsPublic, bool bRequiresKey);

	// Builds a standalone open wall/partition line on the active floor - same id/floor rules as
	// CreateRoomFromFootprint, but the points aren't closed into a loop (at least 2 points, not 3)
	// and there's no name/access/key to set. Shares the same room id namespace (PeekNextRoomId/
	// UsedRoomIds) - a wall is just an open-loop room as far as ids are concerned.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	AInteriorRoom* CreateWallFromPoints(const TArray<FVector2D>& Points, const FString& WallId);

	// The id CreateRoomFromFootprint would auto-generate right now if given an empty RoomId -
	// exposed so UI can show it as a placeholder/default in the "Create Room" form.
	UFUNCTION(BlueprintPure, Category = "Interior")
	FString PeekNextRoomId() const;

	// Removes a single room/wall by id, on whichever floor it's on - destroys its actor and forgets
	// its id (so it can be reused). Only ever removes something tracked here (a room or standalone
	// wall); the exterior building outline is a separate actor (APolyBuilding) this never touches.
	// Returns false if no room/wall with that id currently exists.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool DeleteRoom(const FString& RoomId);

	// Rescales a room/wall's existing footprint to fit new bounding-box dimensions (NewWidth along
	// X, NewDepth along Y, same pre-scale units as everything else), keeping it centered where it
	// currently is. Works for any room/wall regardless of how it was created (prefab or freehand) -
	// it's a bounding-box stretch of whatever points are already there, not tied to a particular
	// shape. Returns false if no room with that id exists, or its current bounding box is
	// degenerate (zero width or depth - can't compute a scale factor from that).
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool ResizeRoom(const FString& RoomId, float NewWidth, float NewDepth);

	// Adds a fixed-size door to RoomId's EdgeIndex, centered at OffsetWorldUnits along that edge
	// (world units, matching FInteriorOpening::Offset - the "add door" tool on the 2D floor plan
	// canvas already clamps this so the door fits within the edge). InputId/ConnectsTo are left
	// blank - filled in later via a proper UI form, not here. Returns the new door's id, or an empty
	// string if no room with that id exists.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	FString AddDoorToRoom(const FString& RoomId, int32 EdgeIndex, float OffsetWorldUnits);

	// Removes a single door from RoomId by its own id (independent of DeleteRoom - the room itself
	// is untouched, only that one opening goes away and the wall becomes solid there again). Returns
	// false if the room or the door within it doesn't exist.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool DeleteDoor(const FString& RoomId, const FString& DoorId);

	// Slides an existing door to a new position along the same edge it's already on
	// (NewOffsetWorldUnits, world units, matching FInteriorOpening::Offset) - the caller (dragging a
	// door on the 2D floor plan canvas) is responsible for clamping this so it stays within the
	// edge. Returns false if the room or the door within it doesn't exist.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool MoveDoor(const FString& RoomId, const FString& DoorId, float NewOffsetWorldUnits);

	// Nav nodes (Path Node/Elevator/Stairs) for the currently loaded building, all floors. Not yet
	// turned into actors - representation is pending alignment with the navigation
	// (NavMesh/NavLinks) team on what they actually need to consume.
	UFUNCTION(BlueprintPure, Category = "Interior")
	const TArray<FInteriorNavNode>& GetNavNodes() const { return NavNodes; }

	// The rooms/walls spawned on a given floor - read-only, for a 2D floor plan renderer to draw
	// (see UInteriorFloorPlanWidget). Empty if that floor doesn't exist.
	UFUNCTION(BlueprintPure, Category = "Interior")
	const TArray<AInteriorRoom*>& GetRoomsOnFloor(int32 Level) const;

protected:
	virtual void BeginPlay() override;

private:
	// Builds a JSON object matching the "interior" schema (floors[].{wallHeight,wallThickness,
	// rooms[],navNodes[]}) from the currently loaded building's live state. Used by
	// SyncInteriorToBuildingGenerator.
	TSharedRef<FJsonObject> SerializeCurrentInterior() const;

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

	// Room ids in use for the currently loaded building, for CreateRoomFromFootprint's uniqueness
	// check. Cleared whenever a different building is loaded.
	TSet<FString> UsedRoomIds;
};
