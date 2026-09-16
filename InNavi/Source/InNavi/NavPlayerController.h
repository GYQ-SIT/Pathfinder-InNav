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

	// Add-door mode: click a wall edge on the 2D floor plan canvas to add a fixed-size door there
	// (centered on the click, clamped so it doesn't poke past either end of the edge) - Input/
	// Output (ConnectsTo) ids are left blank for now, to be filled in later via a proper UI form.
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

	// See SelectRoom/GetSelectedRoomId - persists across mode changes, cleared on deselect (empty
	// id), StartNewProject, or once deleted.
	FString SelectedRoomId;

	// See SelectDoor/GetSelectedDoorRoomId/GetSelectedDoorId - same lifetime rules as
	// SelectedRoomId, and mutually exclusive with it (selecting one clears the other).
	FString SelectedDoorRoomId;
	FString SelectedDoorId;

	// See TogglePrefabRectangleMode/TogglePrefabTriangleMode/TogglePrefabCircleMode/
	// IsInPrefabRoomMode. 0 = none armed; 1 = rectangle, 2 = triangle, 3 = circle.
	int32 ActivePrefabShape = 0;

	bool bAddDoorMode = false;

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

	FVector SavedCameraLocation = FVector::ZeroVector;
	FRotator SavedCameraRotation = FRotator::ZeroRotator;
	bool bHasSavedCameraView = false;

	// Id of the building currently being interior-edited (i.e. has a floor/roof section hidden via
	// PositionCameraForFloor) - empty if none. RestoreExteriorCameraView shows everything again.
	FString InteriorBuildingHiddenById;
};