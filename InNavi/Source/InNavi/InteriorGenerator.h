// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteriorTypes.h"
#include "InteriorGenerator.generated.h"

class ABuildingGenerator;
class AInteriorRoom;
class AInteriorConnectorMarker;
class APolyBuilding;
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
	TSharedPtr<FJsonObject> CreateTranslatedInteriorCopy(const TSharedPtr<FJsonObject>& SourceInterior,const FVector2D& MoveOffset) const;
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

	// Wall thickness is stored per floor in world units. Navigation uses this to build an accurate
	// clearance boundary around the authored wall centre lines instead of assuming every floor has
	// the default thickness.
	float GetWallThicknessForFloor(int32 Level) const;

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
	AInteriorRoom* CreateRoomFromFootprint(const TArray<FVector2D>& Footprint,const FString& RoomId,const FString& RoomName,bool bIsPublic,bool bRequiresKey,bool bAllowOverlap = false);

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

	// Rotates a room/wall's footprint by DeltaDegrees around its own bounding-box center (positive
	// = counter-clockwise). Rejects (no-op, returns false) if the rotated footprint would extend
	// outside the building's outline or overlap another room/wall - checked every call (i.e. every
	// tick while a rotation key is held), same as ResizeRoom/MoveRoom's boundary check. Does NOT
	// check against connectors (elevator/stairs/escalator) - rotating through one is allowed
	// mid-gesture; see CommitRoomRotation for the deferred check applied once the key is released.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool RotateRoom(const FString& RoomId, float DeltaDegrees);

	// Call once when a rotation gesture ends (a rotation key is released) - reverts the room to
	// FallbackFootprint if it ended up walling in a connector (elevator/stairs/escalator), the one
	// check RotateRoom itself intentionally skips mid-gesture. Mirrors CommitRoomMove's role for a
	// drag. Returns false (and reverts) if blocked, or if RoomId doesn't exist.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool CommitRoomRotation(const FString& RoomId, const TArray<FVector2D>& FallbackFootprint);

	// Replaces a room/wall's footprint wholesale with NewFootprint (same pre-scale units as
	// everything else, same point count/winding expected - this is a translate, not a reshape) -
	// what dragging a selected room on the 2D floor plan canvas calls, recomputing the whole
	// footprint fresh each move from a drag-start snapshot (see UInteriorFloorPlanWidget) rather
	// than accumulating small per-frame deltas, so dragging doesn't drift. Rejects (returns false,
	// leaves the room where it was) a footprint that would extend outside the building's own outline
	// - checked on every call, so a drag simply can't push a room past the building's wall, even
	// momentarily. Does NOT check for overlap with other rooms/walls - that's allowed while
	// dragging, and only enforced once the drag ends (see CommitRoomMove). Returns false if no room
	// with that id exists.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool MoveRoom(const FString& RoomId, const TArray<FVector2D>& NewFootprint);

	// Call once when a room drag ends: if the room's current footprint now overlaps another
	// room/wall on the same floor (allowed to happen transiently while dragging - see MoveRoom),
	// reverts it to FallbackFootprint (the drag-start snapshot) and returns false; otherwise leaves
	// it where it was dropped and returns true. Returns false if no room with that id exists.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool CommitRoomMove(const FString& RoomId, const TArray<FVector2D>& FallbackFootprint);

	// Snapping: finds the closest point, across every edge of every OTHER room/wall on Level
	// (RoomIdToExclude skips one room's own edges, e.g. so a room being dragged doesn't snap to
	// itself) plus the currently-loaded building's own exterior outline - not just each edge's
	// endpoints, so this snaps to the middle of a wall as well as a corner - to any point in
	// Footprint, within ToleranceFootprintUnits. If one exists, returns the offset that would shift
	// Footprint so that point lands exactly on it (add it to every point in Footprint). Used both
	// for a single point (P-mode's next click) and a whole room's footprint (dragging - see
	// UInteriorFloorPlanWidget). Returns false (OutSnapOffset left untouched) if nothing is within
	// tolerance.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool FindSnapOffset(int32 Level, const TArray<FVector2D>& Footprint, const FString& RoomIdToExclude, float ToleranceFootprintUnits, FVector2D& OutSnapOffset) const;

	// Adds a fixed-size door to RoomId's EdgeIndex, centered at OffsetWorldUnits along that edge
	// (world units, matching FInteriorOpening::Offset - the "add door" tool on the 2D floor plan
	// canvas already clamps this so the door fits within the edge). InputId is set automatically to
	// RoomId (the door's own owning room - that's the "input" side by definition); ConnectsTo is
	// left blank, filled in later via a door properties panel (see SetDoorConnectsTo). Returns the
	// new door's id, or an empty string if no room with that id exists.
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

	// Reads RoomId's DoorId opening's full data - for a door properties panel to populate its fields.
	// Returns false (OutOpening left untouched) if the room or the door within it doesn't exist.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool GetDoorOpening(const FString& RoomId, const FString& DoorId, FInteriorOpening& OutOpening) const;

	// Sets a door's ConnectsTo ("output" - the room/corridor/exterior id on the other side of the
	// door, as opposed to InputId, which is always the room the door itself belongs to). Returns
	// false if the room or the door within it doesn't exist.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool SetDoorConnectsTo(const FString& RoomId, const FString& DoorId, const FString& NewConnectsTo);

	// Renames a room/wall (the human-readable RoomName, distinct from its auto-generated RoomId -
	// see AInteriorRoom::RoomName). Returns false if no room with that id exists.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool SetRoomName(const FString& RoomId, const FString& NewName);

	// Reads a room's current human-readable name - for a room properties panel to populate its
	// field. A plain getter with no side effects, so this is BlueprintPure (no exec pins needed).
	// Returns an empty string if no room with that id exists.
	UFUNCTION(BlueprintPure, Category = "Interior")
	FString GetRoomName(const FString& RoomId) const;

	// Whether RoomId is an actual closed room (true) or an open standalone wall (false, drawn via
	// draw-wall mode) - rooms and walls share this same id namespace/class (see AInteriorRoom::
	// IsClosedLoop), so a properties panel needs this to tell them apart, e.g. to show "Wall
	// property" instead of "Room property" for a selected wall. Returns false (same as a wall) if
	// no room with that id exists.
	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsRoomClosedLoop(const FString& RoomId) const;

	// Nav nodes (Path Node/Elevator/Stairs/Escalator) for the currently loaded building, all
	// floors. Data format agreed with the navigation (NavMesh/NavLinks) team - see
	// FInteriorNavNode's own comment.
	UFUNCTION(BlueprintPure, Category = "Interior")
	const TArray<FInteriorNavNode>& GetNavNodes() const { return NavNodes; }

	// Places an Elevator/Stairs/Escalator connector at Position (same pre-scale footprint units as
	// everything else), creating one FInteriorNavNode per floor listed in ConnectsToFloors, all
	// sharing a freshly-generated ConnectorGroupId (returned). All-or-nothing: every requested floor
	// is validated first (must exist, Position must be inside the building's outline and not inside
	// a wall's solid thickness there - being inside a room's open interior is fine, this only
	// rejects sitting inside the wall itself) - if any floor fails, nothing is created on any floor.
	// Returns an empty string on that kind of total failure.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	FString PlaceConnectorNode(EInteriorNavNodeType Type, FVector2D Position, const TArray<int32>& ConnectsToFloors);

	// Removes every floor's FInteriorNavNode sharing GroupId (see PlaceConnectorNode) - deleting a
	// connector removes it everywhere it appeared, not just on the current floor. Returns false if
	// no node with that ConnectorGroupId exists.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool DeleteConnectorGroup(const FString& GroupId);

	// Changes which floors an existing connector (GroupId) reaches to NewConnectsToFloors - a floor
	// newly added gets a fresh node created there (same position/type, same all-or-nothing
	// validation as PlaceConnectorNode - if any newly-added floor fails, nothing about the connector
	// changes), a floor dropped has its node removed, and every remaining node's ConnectsFloors is
	// updated to the new full list. Rejects an empty NewConnectsToFloors (use DeleteConnectorGroup
	// to remove a connector entirely instead). Returns false if no node with that ConnectorGroupId
	// exists, or the change was rejected.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool SetConnectorFloors(const FString& GroupId, const TArray<int32>& NewConnectsToFloors);

	// Rotates every floor instance of connector GroupId by DeltaDegrees about its own center
	// (positive = counter-clockwise, same convention as RotateRoom) - they share one orientation
	// (see FInteriorNavNode::RotationDegrees), so this validates the new footprint on EVERY floor the
	// connector reaches (building outline, walls, other connectors, other buildings - see
	// CanPlaceConnectorOnFloor) and only applies it if all of them pass; no partial rotation. Unlike
	// RotateRoom, there's no deferred/mid-gesture-allowed check to speak of, so this is fully
	// validated on every call - safe to call every tick while a rotate key/button is held, same usage
	// pattern as RotateRoom.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool RotateConnectorGroup(const FString& GroupId, float DeltaDegrees);

	// Adjusts every floor instance of connector GroupId's SizeScale by DeltaScale (clamped to
	// [MinConnectorScale, MaxConnectorScale]) - same all-floors-must-pass validation as
	// RotateConnectorGroup, and a discrete immediate step (no drag/release split) like
	// GrowSelectedRoom/ShrinkSelectedRoom's NudgeSelectedRoomSize.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool ScaleConnectorGroup(const FString& GroupId, float DeltaScale);

	// The rooms/walls spawned on a given floor - read-only, for a 2D floor plan renderer to draw
	// (see UInteriorFloorPlanWidget). Empty if that floor doesn't exist.
	UFUNCTION(BlueprintPure, Category = "Interior")
	const TArray<AInteriorRoom*>& GetRoomsOnFloor(int32 Level) const;
	UFUNCTION(BlueprintPure, Category = "Interior")
	AInteriorRoom* GetRoomById(const FString& RoomId) const;
	// ---- Undo / Redo ----

// Save the current interior state into the undo history.
	UFUNCTION(BlueprintCallable, Category = "Interior|History")
	void RecordHistorySnapshot();

	// Go back to the previous interior state.
	UFUNCTION(BlueprintCallable, Category = "Interior|History")
	bool Undo();

	// Go forward to the next interior state.
	UFUNCTION(BlueprintCallable, Category = "Interior|History")
	bool Redo();

	// Remove all stored undo/redo history.
	UFUNCTION(BlueprintCallable, Category = "Interior|History")
	void ClearHistory();
protected:
	virtual void BeginPlay() override;

private:
	// Builds a JSON object matching the "interior" schema (floors[].{wallHeight,wallThickness,
	// rooms[],navNodes[]}) from the currently loaded building's live state. Used by
	// SyncInteriorToBuildingGenerator.
	TSharedRef<FJsonObject> SerializeCurrentInterior() const;

	// Shared validation behind PlaceConnectorNode, SetConnectorFloors, RotateConnectorGroup, and
	// ScaleConnectorGroup: can a Type connector with this RotationDegrees/SizeScale sit at Position on
	// Level - the floor exists, and the connector's real 3D footprint (see
	// GetConnectorFootprintCorners, InteriorTypes.h - the same shape AInteriorConnectorMarker actually
	// builds, so this and the 3D preview never disagree) fits entirely inside the building's own
	// outline, doesn't come within half the wall thickness of any room/wall on that floor (being
	// inside a room's open interior is fine, only overlapping a wall itself is rejected), doesn't
	// overlap another connector's own footprint on that floor, and doesn't overlap a neighboring
	// building's own footprint. Logs the specific reason on failure.
	//
	// IgnoreGroupId skips nodes belonging to that ConnectorGroupId in the connector-vs-connector
	// check - needed by RotateConnectorGroup/ScaleConnectorGroup, which re-validate a connector's OWN
	// existing floor instances against everything else; without it, a connector would always overlap
	// itself and could never be rotated or scaled in place. Left empty (default) for a brand new
	// placement, which has no group of its own yet to exclude.
	bool CanPlaceConnectorOnFloor(EInteriorNavNodeType Type, FVector2D Position, int32 Level, float RotationDegrees, float SizeScale, const FString& IgnoreGroupId = FString()) const;

	// The reverse of CanPlaceConnectorOnFloor's wall-thickness check: would this room/wall
	// footprint's own walls end up inside a wall's solid thickness of an existing connector
	// (elevator/stairs/escalator) on Level - i.e. would creating/moving/resizing/rotating a room
	// to this footprint bury a connector inside a wall. Used by CreateRoomFromFootprint,
	// CommitRoomMove, ResizeRoom, and RotateRoom's commit step.
	bool DoesFootprintBlockConnector(const TArray<FVector2D>& Footprint, bool bClosedLoop, int32 Level) const;

	// Spawns Node's 3D preview placeholder marker (see AInteriorConnectorMarker) and assigns it to
	// Node.MarkerActor, then calls RebuildConnectorMarker to actually build it - called from every
	// site that does NavNodes.Add() (the JSON load path, PlaceConnectorNode, SetConnectorFloors).
	// No-op for PathNode (nothing to spawn) or if Building/Generator aren't available.
	void SpawnConnectorMarker(FInteriorNavNode& Node, APolyBuilding* Building);

	// Rebuilds Node.MarkerActor's mesh from Node's current Position/Level/RotationDegrees/SizeScale
	// and hides it if Node.Level isn't the currently active floor - called by SpawnConnectorMarker
	// right after spawning (initial build) and by RotateConnectorGroup/ScaleConnectorGroup (in-place
	// rebuild of an existing marker, no respawn). No-op if Node.MarkerActor is null.
	void RebuildConnectorMarker(FInteriorNavNode& Node, APolyBuilding* Building) const;

	// See ScaleConnectorGroup.
	static constexpr float MinConnectorScale = 0.5f;
	static constexpr float MaxConnectorScale = 2.0f;

	UPROPERTY()
	ABuildingGenerator* Generator;

	UPROPERTY()
	FString LoadedBuildingId;

	int32 ActiveFloor = 1;

	// Rooms spawned for the currently loaded building, keyed by floor level.
	UPROPERTY()
	TMap<int32, FInteriorRoomList> RoomsByFloor;

	// Elevator/stairs/escalator/path nodes for the currently loaded building. UPROPERTY() so the
	// struct's own MarkerActor UPROPERTY() (InteriorTypes.h) is actually reachable by GC reflection -
	// without it on this containing TArray, UHT has no path down into FInteriorNavNode at all (matches
	// RoomsByFloor's own UPROPERTY() right above, which this had drifted out of sync with).
	UPROPERTY()
	TArray<FInteriorNavNode> NavNodes;

	// Wall defaults per loaded floor, keyed by level - kept around after the JSON parse so
	// AddFloor has something sensible to inherit.
	TMap<int32, FInteriorFloorInfo> FloorInfoByLevel;

	// Room ids in use for the currently loaded building, for CreateRoomFromFootprint's uniqueness
	// check. Cleared whenever a different building is loaded.
	TSet<FString> UsedRoomIds;

	// ---- Undo / Redo History ----

	TArray<FString> InteriorHistory;

	int32 HistoryIndex = -1;

	bool bIsRestoringHistory = false;
};
