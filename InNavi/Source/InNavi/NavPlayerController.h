// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InteriorTypes.h"
#include "NavPlayerController.generated.h"

class APolyBuilding;
class ABuildingGenerator;
class AInteriorGenerator;
class AInteriorRoom;

// Broadcast whenever build mode turns on/off, so UI can show an indicator without polling every tick.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuildModeChanged, bool, bNewBuildMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShapeModeChanged, bool, bNewShapeMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDrawRoomModeChanged, bool, bNewDrawRoomMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDrawWallModeChanged, bool, bNewDrawWallMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectModeChanged, bool, bNewSelectMode);
// Broadcast whenever the selected room/wall changes on the 2D floor plan canvas (select mode
// picking a different one, or clicking empty space to deselect - RoomId is empty in that case).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectedRoomChanged, FString, RoomId);
// Broadcast whenever the selected door changes (selecting one, selecting a different one, or
// deselecting - both ids are empty in that case). Mirrors FOnSelectedRoomChanged for doors, since
// the two selections are tracked and broadcast separately (see SelectRoom/SelectDoor).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSelectedDoorChanged, FString, RoomId, FString, DoorId);
// Broadcast whenever the selected connector (elevator/stairs/escalator) changes - GroupId is the
// shared FInteriorNavNode::ConnectorGroupId, not one floor-specific node's own Id, since selecting
// one selects the whole multi-floor connector (see SelectConnector). Empty when deselecting.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectedConnectorChanged, FString, GroupId);
// Broadcast after each footprint point is added or cleared, so UI can show a live point count.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFootprintPointCountChanged, int32, PointCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomPointCountChanged, int32, PointCount);
// Broadcast whenever a building is selected (left-click on a building outside of any draw mode -
// including re-clicking the one already selected), with its id, so UI can show/hide an "Enter
// Interior" button without polling every tick. Not broadcast for a deselect (clicking empty ground
// currently leaves the selection as-is - see SelectBuildingUnderCursor).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuildingSelected, FString, BuildingId);
// Broadcast whenever a user-triggered action fails (a button press that did nothing), with a
// short human-readable reason. UE_LOG still gets the detailed message for developers; this is
// the same idea aimed at UI - bind it once to show a toast/message, since a packaged game has no
// visible Output Log. Not exhaustive, but covers the common "why didn't anything happen" cases.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActionFailed, FString, Reason);

UCLASS()
class INNAVI_API ANavPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ANavPlayerController();

	// Bind this once to catch every failed action (see FOnActionFailed above) instead of hunting
	// through the Output Log.
	UPROPERTY(BlueprintAssignable, Category = "Feedback")
	FOnActionFailed OnActionFailed;

	// Plan A (freehand footprint). Callable from Blueprint/UMG as well as the B key, so a UI
	// button can drive this directly once one exists (e.g. for mobile, where there's no keyboard).
	UFUNCTION(BlueprintCallable, Category = "Building")
	void ToggleBuildMode();

	UFUNCTION(BlueprintPure, Category = "Building")
	bool IsInBuildMode() const { return bBuildMode; }

	UPROPERTY(BlueprintAssignable, Category = "Building")
	FOnBuildModeChanged OnBuildModeChanged;

	// Plan A: confirm (generate from the currently drawn points) / cancel the in-progress footprint.
	// Bind Finish to an on-screen button; pass CustomId/CustomName from optional text fields if you
	// want to let the user name the building, or leave them empty to auto-generate ("NEW_1", ...).
	UFUNCTION(BlueprintCallable, Category = "Building")
	void FinishBuilding(const FString& CustomId = FString(), const FString& CustomName = FString());

	UFUNCTION(BlueprintCallable, Category = "Building")
	void CancelBuilding();

	UPROPERTY(BlueprintAssignable, Category = "Building")
	FOnFootprintPointCountChanged OnFootprintPointCountChanged;

	// Opens the OS "open file" dialog filtered to .json and returns the path the user picked (empty
	// string if they cancelled). Doesn't load anything - pairs with LoadBuildingDataFromPath so UI
	// can show the picked path (e.g. in a text box) before the user commits to loading it.
	UFUNCTION(BlueprintCallable, Category = "Building")
	FString BrowseForBuildingDataFile();

	// Loads a buildings.json-format file from an explicit path (typically one
	// BrowseForBuildingDataFile just returned) the same way the startup buildings.json is loaded.
	// Clears out whatever was already spawned first, so buildings left over from a previous New/Open
	// in this session don't stick around alongside the new file.
	// On success, this also becomes the file SaveCurrentProject (Ctrl+S) writes back to.
	// Returns how many buildings were spawned.
	UFUNCTION(BlueprintCallable, Category = "Building")
	int32 LoadBuildingDataFromPath(const FString& FilePath);

	// The "New" button should call this before switching to the edit screen: destroys every
	// currently-spawned building/interior and resets back to a blank project (no file path, no
	// unsaved-changes flag) - without this, buildings drawn in a previous New/Open session would
	// still be sitting in the level.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void StartNewProject();

	// Puts the camera back where it was before LoadInteriorForBuilding snapped it to a top-down
	// view, and shows whatever building's floor/roof section that hid (see PositionCameraForFloor
	// below) - the interior screen's Back button should call this before switching back to the edit
	// screen. No-op if the camera was never moved.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void RestoreExteriorCameraView();

	// The file Ctrl+S / SaveCurrentProject saves to. Set automatically after a successful
	// LoadBuildingDataFromPath (Open) or the first SaveCurrentProject on a brand-new (New) session.
	UFUNCTION(BlueprintPure, Category = "Building")
	FString GetCurrentProjectFilePath() const { return CurrentProjectFilePath; }

	// True if something has been created/edited since the last save (or since Open/New, if never
	// saved yet). Set by every mutating action (FinishBuilding, CreateRectangleBuilding,
	// CreateRegularPolygonBuilding, FinishRoom, AddFloor, RemoveFloor); cleared by a successful
	// LoadBuildingDataFromPath, SaveCurrentProject, or SaveCurrentProjectAs. Check this before any
	// destructive navigation (Back to the start screen, Quit) and show a confirm popup if true -
	// UMG has no built-in "are you sure" dialog, so the UI needs to build that popup itself; this
	// getter is what its Branch node should check.
	UFUNCTION(BlueprintPure, Category = "Building")
	bool HasUnsavedChanges() const { return bHasUnsavedChanges; }

	// Saves every currently-spawned building (bound to Ctrl+S). If there's no current project file
	// yet (e.g. a New session), this opens the OS "save file" dialog first (same as BrowseForSaveLocation)
	// and remembers whatever the user picks; otherwise it saves straight to GetCurrentProjectFilePath().
	// Returns true on success; false if the user cancelled the dialog or the save failed.
	UFUNCTION(BlueprintCallable, Category = "Building")
	bool SaveCurrentProject();

	// Opens the OS "save file" dialog and returns the path picked (empty if cancelled). Doesn't save
	// anything - SaveCurrentProject already calls this automatically when needed, so most UI never
	// needs to call it directly. Exposed for an explicit "Save As" button later.
	UFUNCTION(BlueprintCallable, Category = "Building")
	FString BrowseForSaveLocation();

	// Saves to an explicit path (e.g. one BrowseForSaveLocation returned) and makes it the current
	// project file, so later Ctrl+S calls reuse it without re-prompting. Returns true on success.
	UFUNCTION(BlueprintCallable, Category = "Building")
	bool SaveCurrentProjectAs(const FString& FilePath);

	// Plan B: the permanent entry points Padre's parameter form should call directly (thin
	// pass-throughs to BuildingGenerator, so UI only ever needs a reference to the PlayerController).
	UFUNCTION(BlueprintCallable, Category = "Building")
	APolyBuilding* CreateRectangleBuilding(const FString& Id, const FString& Name, FVector2D Position, float Width, float Depth, float RotationDegrees, int32 FloorCount, float FloorHeight);

	UFUNCTION(BlueprintCallable, Category = "Building")
	APolyBuilding* CreateRegularPolygonBuilding(const FString& Id, const FString& Name, FVector2D Position, int32 Sides, float Radius, float RotationDegrees, int32 FloorCount, float FloorHeight);

	// TEST-ONLY - do not build UI against these two. See ConfirmShapeTest below.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void ToggleShapeTestMode();

	UFUNCTION(BlueprintPure, Category = "Building")
	bool IsInShapeTestMode() const { return bShapeTestMode; }

	UPROPERTY(BlueprintAssignable, Category = "Building")
	FOnShapeModeChanged OnShapeModeChanged;

	// Plan A-for-interiors (Phase 3): draws a room's footprint on the currently active floor of the
	// currently loaded building, the same way build mode draws a building's footprint. Mutually
	// exclusive with build mode / shape test mode. Fails to turn on if no building interior is
	// currently loaded (see InteriorGenerator::LoadInteriorForBuilding).
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void ToggleDrawRoomMode();

	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsInDrawRoomMode() const { return bDrawRoomMode; }

	UPROPERTY(BlueprintAssignable, Category = "Interior")
	FOnDrawRoomModeChanged OnDrawRoomModeChanged;

	// Confirm (generate from the currently drawn points) / cancel the in-progress room footprint.
	// RoomId/RoomName/bIsPublic/bRequiresKey feed the "Create Room" prompt; leave RoomId empty to
	// auto-generate one matching the {Building}-{Floor:2}-{Room:2} convention
	// (InteriorGenerator::PeekNextRoomId() previews it).
	UFUNCTION(BlueprintCallable, Category = "Interior")
	AInteriorRoom* FinishRoom(const FString& RoomId = FString(), const FString& RoomName = FString(), bool bIsPublic = true, bool bRequiresKey = false);

	UFUNCTION(BlueprintCallable, Category = "Interior")
	void CancelRoom();

	UPROPERTY(BlueprintAssignable, Category = "Interior")
	FOnRoomPointCountChanged OnRoomPointCountChanged;

	// Adds an explicit footprint point to the in-progress room/wall - what the 2D floor plan canvas
	// (see UInteriorFloorPlanWidget) calls on a click, since interior room/wall drawing has no 3D
	// raycast at all now (OnClick is a no-op in draw room/wall mode). No-op if not currently in
	// draw room/wall mode.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void AddRoomPointAtFootprintPosition(FVector2D FootprintPoint);

	// The room/wall points drawn so far in the current draw room/wall session - for the 2D floor
	// plan canvas to draw as it's being clicked out. Empty when not drawing.
	UFUNCTION(BlueprintPure, Category = "Interior")
	const TArray<FVector2D>& GetPendingRoomFootprint() const { return PendingRoomFootprint; }

	// Select mode: click a room/wall on the 2D floor plan canvas to select it (or click empty space
	// to deselect) - selecting doesn't do anything by itself, it just sets what
	// DeleteSelectedRoom/GrowSelectedRoom/ShrinkSelectedRoom (and, later, a properties-editing tool)
	// act on. The exterior building outline is a separate actor this never touches - only
	// rooms/walls tracked by InteriorGenerator can be selected this way. Mutually exclusive with
	// build mode / shape test mode / draw room mode / draw wall mode. Bound to the Q key for now,
	// not wired into UMG yet.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void ToggleSelectMode();

	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsInSelectMode() const { return bSelectMode; }

	// Vertex/edge snapping (see AInteriorGenerator::FindSnapOffset) - a standing setting, not a
	// mode, so it isn't mutually exclusive with anything and doesn't get turned off by switching
	// modes. On by default. P-mode's next click and dragging a selected room both check this before
	// calling FindSnapOffset. Bound to the N key for now, not wired into UMG yet.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void ToggleSnapEnabled();

	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsSnapEnabled() const { return bSnapEnabled; }

	UPROPERTY(BlueprintAssignable, Category = "Interior")
	FOnSelectModeChanged OnSelectModeChanged;

	// Sets the currently-selected room/wall - the 2D floor plan canvas's select-mode click handler
	// calls this with whichever room/wall it found near the click, or an empty string to deselect.
	// The selection persists even after leaving select mode, so Delete/Resize keep working on it.
	// Clears the selected-door state (see SelectDoor) - only one of a room or a door is ever
	// selected at a time.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void SelectRoom(const FString& RoomId);

	UFUNCTION(BlueprintPure, Category = "Interior")
	FString GetSelectedRoomId() const { return SelectedRoomId; }

	UPROPERTY(BlueprintAssignable, Category = "Interior")
	FOnSelectedRoomChanged OnSelectedRoomChanged;

	// Renames the currently-selected room/wall (see AInteriorGenerator::SetRoomName) - what a room
	// properties panel's "apply" button should call. No-op (returns false) if nothing is selected.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool SetSelectedRoomName(const FString& NewName);

	// Reads the currently-selected room's current name - what a room properties panel calls to
	// populate its field when a room gets selected. Empty if nothing is selected.
	UFUNCTION(BlueprintPure, Category = "Interior")
	FString GetSelectedRoomName() const;

	// Sets the currently-selected door, independently of the room it belongs to - the 2D floor plan
	// canvas's select-mode click handler calls this when the click lands on a door marker rather
	// than a plain wall edge. Clears whatever room was selected (see SelectRoom) - only one of the
	// two is ever selected at a time.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void SelectDoor(const FString& RoomId, const FString& DoorId);

	UFUNCTION(BlueprintPure, Category = "Interior")
	FString GetSelectedDoorRoomId() const { return SelectedDoorRoomId; }

	UFUNCTION(BlueprintPure, Category = "Interior")
	FString GetSelectedDoorId() const { return SelectedDoorId; }

	UPROPERTY(BlueprintAssignable, Category = "Interior")
	FOnSelectedDoorChanged OnSelectedDoorChanged;

	// Reads the currently-selected door's full data (Id/EdgeIndex/Offset/InputId/ConnectsTo/...) -
	// what a door properties panel calls to populate its fields when a door gets selected. InputId
	// is set automatically at creation time (see AddDoorToWall) to the room the door belongs to -
	// it's read-only, shown for context; only ConnectsTo is meant to be user-edited (see
	// SetSelectedDoorConnectsTo). Returns false (OutOpening left untouched) if no door is selected.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool GetSelectedDoorOpening(FInteriorOpening& OutOpening);

	// Sets the currently-selected door's ConnectsTo ("output" - the room/corridor/exterior id on
	// the other side of the door) - what a door properties panel's "apply" button should call. No-op
	// (returns false) if no door is selected.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool SetSelectedDoorConnectsTo(const FString& NewConnectsToLabel);

	// The valid ConnectsTo targets for the currently-selected door, for a door properties panel to
	// populate a dropdown with: "corridor" (the building's shared circulation space, for a door
	// that doesn't lead into another specific room) followed by every other closed room's CURRENT
	// NAME (not its id) on the door's own floor - excluding the door's own room (InputId), since a
	// door can't connect to the room it's already in, and excluding open walls, since only closed
	// rooms are valid targets. Shown/picked by name since that's what a user actually remembers a
	// room by (it's user-editable, unlike the auto-generated id) - SetSelectedDoorConnectsTo
	// resolves the picked name back to the room's stable id before storing it. Empty if no door is
	// selected.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	TArray<FString> GetConnectableTargetIds() const;

	// Converts a stored ConnectsTo value (a room id, or "corridor") into what the dropdown should
	// show as currently selected - the room's current name (see GetConnectableTargetIds), or
	// ConnectsToId unchanged if it's "corridor", empty, or the room can't be found anymore. Call
	// this on the door's saved ConnectsTo before feeding it to the dropdown's "set selected option",
	// since the dropdown's options are names, not ids.
	UFUNCTION(BlueprintPure, Category = "Interior")
	FString ResolveConnectsToDisplayName(const FString& ConnectsToId) const;

	// Sets the currently-selected connector (elevator/stairs/escalator) by its shared
	// ConnectorGroupId - the 2D floor plan canvas's select-mode click handler calls this when the
	// click lands on a connector marker. Clears whatever room/door was selected (see SelectRoom/
	// SelectDoor) - only one of the three is ever selected at a time. Selection persists across
	// floor changes (a floor the connector doesn't reach just won't show it highlighted there).
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void SelectConnector(const FString& GroupId);

	UFUNCTION(BlueprintPure, Category = "Interior")
	FString GetSelectedConnectorGroupId() const { return SelectedConnectorGroupId; }

	UPROPERTY(BlueprintAssignable, Category = "Interior")
	FOnSelectedConnectorChanged OnSelectedConnectorChanged;

	// Removes every floor's node for the currently-selected connector (see
	// AInteriorGenerator::DeleteConnectorGroup) and clears the selection. Bound as part of
	// DeleteSelection (Backspace). No-op (returns false) if no connector is selected.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool DeleteSelectedConnector();

	// The currently-selected connector's type (Elevator/Stairs/Escalator) - for a connector
	// properties panel to show read-only. PathNode if nothing is selected (never a real value for
	// an actual connector, since only Elevator/Stairs/Escalator are ever placed this way).
	UFUNCTION(BlueprintPure, Category = "Interior")
	EInteriorNavNodeType GetSelectedConnectorType() const;

	// The currently-selected connector's current floor list - for a connector properties panel to
	// initialize its floor checkboxes from. Empty if nothing is selected.
	UFUNCTION(BlueprintPure, Category = "Interior")
	TArray<int32> GetSelectedConnectorFloors() const;

	// Changes which floors the currently-selected connector reaches (see
	// AInteriorGenerator::SetConnectorFloors) - what a connector properties panel's "apply" button
	// should call after reading which floor checkboxes are ticked. No-op (returns false) if no
	// connector is selected.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool SetSelectedConnectorFloors(const TArray<int32>& NewFloors);

	// Removes a single room/wall by id (see AInteriorGenerator::DeleteRoom) - general-purpose,
	// explicit-id version. See also DeleteSelection. Also clears the door selection if the deleted
	// room happened to own the currently-selected door (it no longer exists either way).
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool DeleteRoom(const FString& RoomId);

	// Removes a single door by id (see AInteriorGenerator::DeleteDoor) - general-purpose, explicit-
	// id version. See also DeleteSelection. The room itself is untouched.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool DeleteDoor(const FString& RoomId, const FString& DoorId);

	// Deletes whichever is currently selected - a door takes priority if one is selected (see
	// SelectDoor), otherwise a selected room/wall (see SelectRoom) - and clears the corresponding
	// selection. Bound to the Backspace key for now. No-op (returns false) if nothing is selected.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool DeleteSelection();

	// Rescales a room/wall to fit new Width (X) / Depth (Y) dimensions, keeping it centered where
	// it is (see AInteriorGenerator::ResizeRoom) - general-purpose, explicit-id version. See also
	// GrowSelectedRoom/ShrinkSelectedRoom. Not wired into UMG yet.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool ResizeRoom(const FString& RoomId, float NewWidth, float NewDepth);

	// Moves a room/wall to a new footprint (see AInteriorGenerator::MoveRoom) - what the 2D floor
	// plan canvas's select-mode drag handler calls while dragging a selected room, recomputing the
	// whole translated footprint itself each move. No OnActionFailed broadcast on failure (called
	// continuously during a drag - a stray failure mid-drag shouldn't spam the UI, same reasoning as
	// MoveDoor).
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool MoveRoomTo(const FString& RoomId, const TArray<FVector2D>& NewFootprint);

	// Call once when a room drag ends (see AInteriorGenerator::CommitRoomMove) - reverts the room to
	// FallbackFootprint if it ended up overlapping another room/wall, otherwise leaves it where it
	// was dropped. Broadcasts OnActionFailed on a revert (unlike MoveRoomTo, this only fires once
	// per drag, so it won't spam the UI).
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool CommitRoomMove(const FString& RoomId, const TArray<FVector2D>& FallbackFootprint);

	// Add-door mode: click a wall edge on the 2D floor plan canvas to add a fixed-size door there
	// (centered on the click, clamped so it doesn't poke past either end of the edge) - InputId is
	// set automatically to the room the door belongs to; ConnectsTo is left blank, to be filled in
	// via a door properties panel (see SetSelectedDoorConnectsTo).
	// Mutually exclusive with every other mode. Bound to the K key for now, not wired into UMG yet.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void ToggleAddDoorMode();

	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsInAddDoorMode() const { return bAddDoorMode; }

	// Adds a fixed-size door to RoomId's EdgeIndex, centered at OffsetWorldUnits along that edge
	// (see AInteriorGenerator::AddDoorToRoom) - what the 2D floor plan canvas's add-door-mode click
	// handler calls after finding which wall edge was clicked. Selects the new door immediately
	// (like PlacePrefabRoomAt does for rooms) so it can be deleted right away if it's wrong. Returns
	// the new door's id, or an empty string on failure.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	FString AddDoorToWall(const FString& RoomId, int32 EdgeIndex, float OffsetWorldUnits);

	// Slides an existing door to a new position along its edge (see AInteriorGenerator::MoveDoor) -
	// what the 2D floor plan canvas's select-mode drag handler calls while dragging a door marker.
	// No OnActionFailed broadcast on failure (called continuously during a drag - a stray failure
	// mid-drag shouldn't spam the UI).
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool MoveDoor(const FString& RoomId, const FString& DoorId, float NewOffsetWorldUnits);

	// Prefab room placement: 1/2/3 (for now, not wired into UMG yet) each arm a different default
	// shape (rectangle / triangle / circle-as-many-sided-polygon) - while armed, clicking the 2D
	// floor plan canvas places one centered on the click, at a fixed default size (no door - that's
	// a separate tool). Mutually exclusive with every other mode; pressing the same key again, or
	// any other mode's key, disarms it.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void TogglePrefabRectangleMode();

	UFUNCTION(BlueprintCallable, Category = "Interior")
	void TogglePrefabTriangleMode();

	UFUNCTION(BlueprintCallable, Category = "Interior")
	void TogglePrefabCircleMode();

	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsInPrefabRoomMode() const { return ActivePrefabShape != 0; }

	// Places whichever prefab shape is currently armed (see TogglePrefabRectangleMode etc.),
	// centered on FootprintCenter - what the 2D floor plan canvas's click handler calls. No-op
	// (returns nullptr) if no prefab shape is currently armed.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	AInteriorRoom* PlacePrefabRoomAt(FVector2D FootprintCenter);

	// Connector (elevator/stairs/escalator) placement: 4/5/6 (for now, not wired into UMG yet) each
	// arm a different type - while armed, clicking the 2D floor plan canvas places one there (see
	// PlaceConnectorAt), defaulting to every floor for an elevator or just the adjacent floor(s) for
	// stairs/escalator (see AInteriorGenerator::PlaceConnectorNode's header comment). Mutually
	// exclusive with every other mode; pressing the same key again, or any other mode's key,
	// disarms it.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void ToggleElevatorMode();

	UFUNCTION(BlueprintCallable, Category = "Interior")
	void ToggleStairsMode();

	UFUNCTION(BlueprintCallable, Category = "Interior")
	void ToggleEscalatorMode();

	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsInConnectorMode() const { return ActiveConnectorType != 0; }

	// Places a connector of whichever type is currently armed (see ToggleElevatorMode etc.) at
	// FootprintPosition, on every floor it defaults to connecting - what the 2D floor plan canvas's
	// click handler calls. Returns the new connector's shared ConnectorGroupId (empty on failure,
	// or if no connector type is currently armed).
	UFUNCTION(BlueprintCallable, Category = "Interior")
	FString PlaceConnectorAt(FVector2D FootprintPosition);

	// Draws a standalone open wall/partition line instead of a closed room - same point-clicking
	// mechanism as draw room mode (shares OnRoomPointCountChanged for the live point count), just a
	// separate mode/key so a UI button (or the L key, for now) can trigger it independently.
	// Mutually exclusive with build mode / shape test mode / draw room mode.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void ToggleDrawWallMode();

	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsInDrawWallMode() const { return bDrawWallMode; }

	UPROPERTY(BlueprintAssignable, Category = "Interior")
	FOnDrawWallModeChanged OnDrawWallModeChanged;

	// Confirm (generate from the currently drawn points) / cancel the in-progress wall. WallId is
	// optional, same auto-generation convention as FinishRoom - a wall shares the room id namespace,
	// there's no separate id scheme for it.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	AInteriorRoom* FinishWall(const FString& WallId = FString());

	UFUNCTION(BlueprintCallable, Category = "Interior")
	void CancelWall();

	// Permanent pass-throughs to InteriorGenerator - same reasoning as the Building pass-throughs
	// above: Padre's UI should never need a separate InteriorGenerator reference, just this one.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	int32 LoadInteriorForBuilding(const FString& BuildingId);

	UFUNCTION(BlueprintCallable, Category = "Interior")
	void SetActiveFloor(int32 Level);

	UFUNCTION(BlueprintPure, Category = "Interior")
	int32 GetActiveFloor() const;

	UFUNCTION(BlueprintCallable, Category = "Interior")
	int32 AddFloor();

	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool RemoveFloor(int32 Level);

	UFUNCTION(BlueprintPure, Category = "Interior")
	FString GetLoadedBuildingId() const;

	// How many floors the currently-loaded building actually has - for a connector properties
	// panel to size its floor checklist against (see SetSelectedConnectorFloors) instead of
	// always showing a fixed set of checkboxes. Returns 0 if no building interior is loaded.
	UFUNCTION(BlueprintPure, Category = "Interior")
	int32 GetLoadedBuildingFloorCount() const;

	UFUNCTION(BlueprintPure, Category = "Interior")
	FString PeekNextRoomId() const;

	UFUNCTION(BlueprintPure, Category = "Interior")
	const TArray<FInteriorNavNode>& GetNavNodes() const;

	// The currently-selected building's id (left-click a building to select it), or empty if
	// nothing is selected. Feed this straight into LoadInteriorForBuilding for an "Enter Interior"
	// button.
	UFUNCTION(BlueprintPure, Category = "Building")
	FString GetSelectedBuildingId() const;

	UPROPERTY(BlueprintAssignable, Category = "Building")
	FOnBuildingSelected OnBuildingSelected;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	// Left-click: selects a building normally, or adds a footprint point while in build mode.
	void OnClick();
	void SelectBuildingUnderCursor();

	// Build mode (Plan A): click to trace out a new building's footprint on the ground.
	// FinishBuilding/CancelBuilding are declared public above (Blueprint-callable).
	void AddFootprintPointUnderCursor();

	// Ctrl+S handler - wraps SaveCurrentProject() because key bindings require a void() signature.
	void SaveCurrentProjectKeyHandler();

	// Shape test mode (Plan B): TEMPORARY stand-in for Padre's UI form. Reads
	// Data/test_shape_params.json (a "shape" field picks rectangle vs regular polygon) and calls
	// the matching Generator->Create...Building(...). Delete ConfirmShapeTest + the R-key/shape-mode
	// plumbing once the real UI calls those Create...Building functions directly - those functions
	// themselves are the permanent part.
	void ConfirmShapeTest();

	// Enter/Esc are shared between both modes; dispatch to whichever is active.
	void OnConfirmPressed();
	void OnCancelPressed();

	// TEST-ONLY (Phase 2.1 verification): cycles the loaded building's active floor up/down via
	// InteriorGen->SetActiveFloor(...). Delete once there's a real floor-switcher UI calling
	// SetActiveFloor directly.
	void CycleInteriorFloorUp();
	void CycleInteriorFloorDown();

	// TEST-ONLY (Phase 2.2 verification): adds a floor to the loaded building via
	// InteriorGen->AddFloor(). Delete once a real "+Add floor" button calls it directly.
	void AddInteriorFloor();

	// TEST-ONLY (Phase 2.2 verification): removes the currently active floor via
	// InteriorGen->RemoveFloor(GetActiveFloor()) - matches the UX call that deleting removes
	// whichever floor you're currently viewing, not always the top one. Delete once a real
	// "remove floor" button calls RemoveFloor directly.
	void RemoveActiveInteriorFloor();

	// TEST-ONLY (Resize verification): grows/shrinks the currently selected room (SelectedRoomId) by
	// 20 units each press, via ResizeRoom. Bound to =/-. Delete once a real resize UI (typed
	// Length/Width, or drag) exists.
	void GrowSelectedRoom();
	void ShrinkSelectedRoom();
	void NudgeSelectedRoomSize(float Delta);

	// Backspace handler - wraps DeleteSelection() because key bindings require a void() signature.
	void DeleteSelectionKeyHandler();

	UPROPERTY()
	APolyBuilding* SelectedBuilding;

	// The single BuildingGenerator placed in the level; found in BeginPlay.
	UPROPERTY()
	ABuildingGenerator* Generator;

	// The single InteriorGenerator placed in the level (if any); found in BeginPlay.
	UPROPERTY()
	AInteriorGenerator* InteriorGen;

	// See GetCurrentProjectFilePath/SaveCurrentProject above.
	UPROPERTY()
	FString CurrentProjectFilePath;

	// See HasUnsavedChanges above.
	bool bHasUnsavedChanges = false;

	bool bBuildMode = false;
	TArray<FVector2D> PendingFootprint;
	int32 NextNewBuildingIndex = 1;

	bool bShapeTestMode = false;

	bool bDrawRoomMode = false;
	TArray<FVector2D> PendingRoomFootprint;

	// Draw wall mode shares PendingRoomFootprint with draw room mode - points come from the 2D
	// floor plan canvas either way, they only differ in what Finish does
	// (FinishRoom vs FinishWall) and whether the result is a closed loop.
	bool bDrawWallMode = false;

	bool bSelectMode = false;

	// See ToggleSnapEnabled/IsSnapEnabled - a standing setting, not a mode, so it's declared away
	// from the mode flags above even though it's checked by some of the same code paths.
	bool bSnapEnabled = true;

	// See SelectRoom/GetSelectedRoomId - persists across mode changes, cleared on deselect (empty
	// id), StartNewProject, or once deleted.
	FString SelectedRoomId;

	// See SelectDoor/GetSelectedDoorRoomId/GetSelectedDoorId - same lifetime rules as
	// SelectedRoomId, and mutually exclusive with it (selecting one clears the other).
	FString SelectedDoorRoomId;
	FString SelectedDoorId;

	// See SelectConnector/GetSelectedConnectorGroupId - same lifetime rules as SelectedRoomId, and
	// mutually exclusive with both SelectedRoomId and SelectedDoorRoomId/SelectedDoorId.
	FString SelectedConnectorGroupId;

	// See TogglePrefabRectangleMode/TogglePrefabTriangleMode/TogglePrefabCircleMode/
	// IsInPrefabRoomMode. 0 = none armed; 1 = rectangle, 2 = triangle, 3 = circle.
	int32 ActivePrefabShape = 0;

	bool bAddDoorMode = false;

	// See ToggleElevatorMode/ToggleStairsMode/ToggleEscalatorMode/IsInConnectorMode. 0 = none
	// armed, otherwise the int32 value of the EInteriorNavNodeType being placed (matches the enum's
	// own values directly - Elevator=1/Stairs=2/Escalator=3 - so it casts straight across, see
	// ToggleConnectorMode).
	int32 ActiveConnectorType = 0;

	// Turns off whichever of draw room mode / draw wall mode is currently active, from any path
	// (self-toggle, or another mode taking over) - clears pending points. Clicks reaching the right
	// floor's ground plane is handled per-section on the building itself (see
	// APolyBuilding::SetVisibleInteriorLevel), not here.
	void ExitDrawRoomMode();

	// Turns off select mode if it's currently active - called by every other mode's toggle (so
	// entering build/draw-room/draw-wall/shape-test mode exits select mode) and by OnCancelPressed.
	// Does NOT clear SelectedRoomId - the selection is meant to outlive select mode itself.
	void ExitSelectMode();

	// Disarms prefab room placement if a shape is currently armed - called by every other mode's
	// toggle, same reasoning as ExitSelectMode.
	void ExitPrefabRoomMode();

	// Shared implementation behind TogglePrefabRectangleMode/TogglePrefabTriangleMode/
	// TogglePrefabCircleMode - ShapeId matches ActivePrefabShape's convention (1/2/3).
	void TogglePrefabRoomMode(int32 ShapeId);

	// Disarms connector placement if a type is currently armed - called by every other mode's
	// toggle, same reasoning as ExitPrefabRoomMode.
	void ExitConnectorMode();

	// Shared implementation behind ToggleElevatorMode/ToggleStairsMode/ToggleEscalatorMode.
	void ToggleConnectorMode(EInteriorNavNodeType Type);

	// Turns off add-door mode if it's currently active - called by every other mode's toggle, same
	// reasoning as ExitSelectMode.
	void ExitAddDoorMode();

	// Snaps the possessed pawn to a top-down view of the given building's currently active floor -
	// called automatically after a successful LoadInteriorForBuilding, so the interior screen shows
	// a readable floor plan instead of whatever angle the exterior camera happened to be left at.
	// Remembers the view it was at beforehand (only the first time, until RestoreExteriorCameraView
	// is called) so that can be restored later. The actual positioning is PositionCameraForFloor.
	void FocusCameraOnBuilding(APolyBuilding* Building);

	// Moves the camera to a tilted top-down view centered on Building's footprint, at a height just
	// above Level's own ceiling (so it stays close to whichever floor is being edited, regardless of
	// how many floors are above it), and hides that ceiling (see APolyBuilding::
	// SetVisibleInteriorLevel) so the view down onto Level's rooms isn't blocked. Used both for the
	// initial FocusCameraOnBuilding and to re-frame when the active floor changes (see
	// RepositionCameraToActiveFloor).
	void PositionCameraForFloor(APolyBuilding* Building, int32 Level);

	// Re-runs PositionCameraForFloor for whichever building/floor is currently loaded - called after
	// SetActiveFloor/AddFloor/RemoveFloor so the camera follows floor changes made while already
	// inside interior editing. No-op if nothing is loaded.
	void RepositionCameraToActiveFloor();

	// Pulls the pawn up to a tilted overview looking down at the whole building cluster's combined
	// footprint, instead of leaving it wherever PlayerStart put it (ground/character-eye level) -
	// called after StartNewProject and a successful LoadBuildingDataFromPath so New/Open always land
	// on a usable view of the site rather than standing on the ground looking at nothing. With no
	// buildings yet (a blank new project), just raises the camera above the origin instead.
	void FocusCameraOnAllBuildings();

	FVector SavedCameraLocation = FVector::ZeroVector;
	FRotator SavedCameraRotation = FRotator::ZeroRotator;
	bool bHasSavedCameraView = false;

	// Id of the building currently being interior-edited (i.e. has a floor/roof section hidden via
	// PositionCameraForFloor) - empty if none. RestoreExteriorCameraView shows everything again.
	FString InteriorBuildingHiddenById;
};