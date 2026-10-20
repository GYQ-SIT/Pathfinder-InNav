// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BuildingSummary.h"
#include "GameFramework/PlayerController.h"
#include "InteriorTypes.h"
#include "RoadSegmentActor.h"
#include "NavPlayerController.generated.h"

class APolyBuilding;
class ABuildingGenerator;
class AInteriorGenerator;
class AInteriorRoom;
class AInteriorConnectorMarker;
class UNavigationPanelWidget;
class UAudioComponent;

// One currently-playing audio file (see PlayAudioFile/StopAllAudio). AudioComponent already keeps
// its own USoundWaveProcedural alive via its own Sound UPROPERTY, so there's no need to separately
// track the sound wave here too.
USTRUCT()
struct FActiveAudioPlayback
{
	GENERATED_BODY()

	UPROPERTY()
	FString FileName;

	UPROPERTY()
	UAudioComponent* AudioComponent = nullptr;
};

// Broadcast whenever build mode turns on/off, so UI can show an indicator without polling every tick.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuildModeChanged, bool, bNewBuildMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShapeModeChanged, bool, bNewShapeMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDrawRoomModeChanged, bool, bNewDrawRoomMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDrawWallModeChanged, bool, bNewDrawWallMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectModeChanged, bool, bNewSelectMode);
// Broadcast whenever ANY edit mode turns on/off/switches - draw room/wall, select, add-door, a
// prefab shape, or a connector type (the ones above only cover draw/select/wall; add-door, prefab
// shape, and connector don't have individual per-mode delegates). No params - a listener re-reads
// whichever of IsInDrawRoomMode/IsInDrawWallMode/IsInSelectMode/IsInAddDoorMode/GetActivePrefabShape/
// GetActiveConnectorType it cares about rather than being handed the new state directly, since more
// than one of those can change in a single mode switch (the old mode turning off, the new one on).
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEditModeChanged);
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
// Broadcast whenever the active interior floor actually changes - from SetActiveFloor directly
// (the Floor panel's own row clicks, but also the [ and ] keyboard shortcuts, which call it via
// CycleInteriorFloorUp/Down without going through any UI widget at all), and from AddFloor/
// RemoveFloor on success (F/X keys, or a real "+Add floor"/"Delete floor" button, both of which can
// shift which floor ends up active, or just change what exists on floors that were already active).
// No params - a listener re-reads GetActiveFloor()/whatever floor-scoped data it cares about,
// rather than being handed the new floor number directly, so it works the same regardless of
// which of these actually triggered it.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnActiveFloorChanged);
// Broadcast whenever a user-triggered action fails (a button press that did nothing), with a
// short human-readable reason and how serious it is (see EActionFailureSeverity in InteriorTypes.h).
// UE_LOG still gets the detailed message for developers; this is the same idea aimed at UI - bind
// it once to show a toast/message, since a packaged game has no visible Output Log. Not
// exhaustive, but covers the common "why didn't anything happen" cases.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActionFailed, FString, Reason, EActionFailureSeverity, Severity);
// Broadcast whenever a project save actually succeeds - both from SaveCurrentProject (Ctrl+S, or
// any UI button that calls it) and SaveCurrentProjectAs (Save As, or the very first save of a
// brand-new project that had no file yet). No params - a listener just needs to know "a save just
// happened," not the path. Added so UI-only state that isn't part of bHasUnsavedChanges at all
// (e.g. InteriorScreenWidget's own panel layout) can still piggyback on the same save action
// instead of needing its own separate save trigger the user has to remember.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProjectSaved);
// Broadcast whenever a room is created, deleted, or renamed (PlacePrefabRoomAt/FinishRoom on
// success, DeleteRoom on success, SetSelectedRoomName on success) - anything that could make a
// room-name list (the Layers panel, the Navigation panel's Start/Destination room dropdowns) go
// stale. Deliberately not folded into OnActiveFloorChanged - that one means "the active floor
// itself changed", this means "room data changed", and they fire from mostly-disjoint call sites.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInteriorContentChanged);
// Broadcast whenever 3D preview mode is entered/left (see SetIn3DPreviewMode) - bIsActive is the
// new state. UInteriorScreenWidget listens to hide/show its 2D floor-plan overlay and update its
// own 2D/3D toggle buttons' highlight, since the mode can also be left from other triggers than
// its own buttons in principle (defensive symmetry with every other PC-state delegate here).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOn3DPreviewModeChanged, bool, bIsActive);
// Broadcast whenever 3D preview's View/Edit sub-mode changes (see SetIn3DEditMode) - bIsEditMode is
// the new state. Only meaningful while 3D preview itself is active; leaving 3D preview entirely
// resets this back to View (see SetIn3DPreviewMode) without a separate broadcast of its own, since
// UInteriorScreenWidget's View/Edit button is already hidden at that point (see
// HandlePreviewModeChanged) and just re-syncs to View the next time 3D preview is re-entered.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEdit3DModeChanged, bool, bIsEditMode);
// Broadcast whenever vertex snapping is toggled (see ToggleSnapEnabled) - bIsEnabled is the new
// state. UInteriorScreenWidget listens to update its snap toggle button's icon, since snapping can
// also be toggled from the N key, not just that button (defensive symmetry with every other
// PC-state delegate here).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSnapEnabledChanged, bool, bIsEnabled);

UCLASS()
class INNAVI_API ANavPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ANavPlayerController();

	// Used by the 2D floor plan while a navigation endpoint is armed.
	bool IsSelectingNavigationPoint() const;
	void SelectNavigationPoint(FVector2D FootprintPosition, int32 Floor);
	UFUNCTION(BlueprintPure, Category = "Navigation")
	UNavigationPanelWidget* GetNavigationPanel() const { return NavigationPanel; }

	// A screen with dockable panels tells the controller where its chrome is, so a mouse press over a panel
	// is not also treated as a click on whatever 3D scene lies behind it (selecting a building, placing a
	// room...). Slate handling the press is not enough: the left-click action reaches this controller anyway.
	// ScreenOwner identifies the screen, so a screen being torn down never clears the one that replaced it.
	void SetPointerOverUICallback(const UObject* ScreenOwner, TFunction<bool(const FVector2D&)> Callback);
	void ClearPointerOverUICallback(const UObject* ScreenOwner);
	bool IsPointerOverUI() const;

	// Bind this once to catch every failed action (see FOnActionFailed above) instead of hunting
	// through the Output Log.
	UPROPERTY(BlueprintAssignable, Category = "Feedback")
	FOnActionFailed OnActionFailed;

	// See FOnProjectSaved above.
	UPROPERTY(BlueprintAssignable, Category = "Feedback")
	FOnProjectSaved OnProjectSaved;

	// See FOnInteriorContentChanged above.
	UPROPERTY(BlueprintAssignable, Category = "Feedback")
	FOnInteriorContentChanged OnInteriorContentChanged;

	// See FOn3DPreviewModeChanged above.
	UPROPERTY(BlueprintAssignable, Category = "Feedback")
	FOn3DPreviewModeChanged OnPreviewModeChanged;

	// See FOnEdit3DModeChanged above.
	UPROPERTY(BlueprintAssignable, Category = "Feedback")
	FOnEdit3DModeChanged OnEdit3DModeChanged;

	// See FOnSnapEnabledChanged above.
	UPROPERTY(BlueprintAssignable, Category = "Feedback")
	FOnSnapEnabledChanged OnSnapEnabledChanged;

	// Whether the interior editor is currently showing the 3D preview (an oblique view of the real
	// spawned geometry, current building/floor only) instead of the 2D floor-plan overlay. View-only
	// - no room/wall/door editing happens while this is active (see the OnClick() guard near
	// IsSelectingNavigationPoint, and UInteriorFloorPlanWidget being hidden entirely while active).
	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsIn3DPreviewMode() const { return bIsIn3DPreviewMode; }

	// Enters/leaves 3D preview mode: positions the camera at a fixed oblique angle over the current
	// floor (see PositionCameraFor3DPreview), hides every other loaded building
	// (SetOtherBuildingsHidden), and broadcasts OnPreviewModeChanged so
	// UInteriorScreenWidget can hide/show its 2D overlay in step. Leaving restores the normal 2D
	// top-down camera pose via the existing RepositionCameraToActiveFloor. No-op if there's no
	// building/floor currently loaded. Always resets bIsIn3DEditMode back to View on the way out (see
	// SetIn3DEditMode) - re-entering 3D preview always starts fresh in View mode.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void SetIn3DPreviewMode(bool bEnable);

	// Whether 3D preview is currently in its Edit sub-mode rather than plain View. Only meaningful
	// while IsIn3DPreviewMode() is true - always false in 2D mode or outside interior editing.
	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsIn3DEditMode() const { return bIsIn3DEditMode; }

	// Switches 3D preview's View/Edit sub-mode and broadcasts OnEdit3DModeChanged. No-op outside 3D
	// preview mode (bIsIn3DPreviewMode) - there's no "Edit" concept in 2D or the exterior view.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void SetIn3DEditMode(bool bEnable);

	// Snaps the camera back to the current floor's default best-fit framing (see ResetFloorFraming) -
	// the free-fly WASD/mouse-look this mode leaves enabled (see SetIn3DPreviewMode) makes it easy to
	// wander off to an awkward angle or position, so this is the "un-lose yourself" button rather than
	// a locked/orbit-only camera. No-op outside 3D preview mode.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void ResetCamera3DPreview();

	// Adjusts the free-fly pawn's own WASD move speed (its UFloatingPawnMovement's MaxSpeed) within a
	// fixed min/max - only meaningful in 3D preview, where WASD is actually enabled. No-op outside it.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void IncreaseMoveSpeed3DPreview();

	UFUNCTION(BlueprintCallable, Category = "Interior")
	void DecreaseMoveSpeed3DPreview();

	// Same idea as IncreaseMoveSpeed3DPreview/DecreaseMoveSpeed3DPreview, but for the exterior view's
	// own free-fly camera (see FocusCameraOnAllBuildings) - a separate pair, not a reuse of those two,
	// because the exterior's usable speed range is far wider (a hand-placed campus vs. a real-world-
	// scale database-loaded one can differ by 40x) and steps multiplicatively instead of by a fixed
	// amount so a single click still feels meaningful at either end of that range.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void IncreaseCameraSpeedExterior();

	UFUNCTION(BlueprintCallable, Category = "Building")
	void DecreaseCameraSpeedExterior();

	// Sets the exterior camera's speed to an exact value (typed into a speed box) rather than
	// stepping it - clamped the same as IncreaseCameraSpeedExterior/DecreaseCameraSpeedExterior.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetCameraSpeedExterior(float NewSpeed);

	// Same idea for 3D preview's own speed box.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void SetMoveSpeed3DPreview(float NewSpeed);

	// Reads the free-fly pawn's current WASD/camera speed - shared by both the exterior page's and
	// 3D preview's speed display so each stays in sync with whatever last changed it (+/-, typing a
	// value, or the auto-computed default on load). 0 if there's no pawn/movement component yet.
	UFUNCTION(BlueprintPure, Category = "Camera")
	float GetCameraMoveSpeed() const;

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

	// Opens the OS "open file" dialog filtered to .json and .db (one combined filter, not a format
	// picker) and returns the path the user picked (empty string if they cancelled). Doesn't load
	// anything - pairs with LoadBuildingDataFromPathOrDatabase, which dispatches on the picked path's
	// own extension, so UI can show the picked path (e.g. in a text box) before the user commits to
	// loading it.
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

	// Second, database-backed way to load exterior buildings - see
	// ABuildingGenerator::LoadBuildingsFromDatabase for the schema/coordinate-conversion details.
	// Same clear-first behavior as LoadBuildingDataFromPath, but does NOT set CurrentProjectFilePath
	// (see its own comment for why) - a database-loaded project needs an explicit Save As before
	// Ctrl+S has anywhere to write. Returns how many buildings were spawned.
	UFUNCTION(BlueprintCallable, Category = "Building")
	int32 LoadBuildingDataFromDatabase(const FString& DatabasePath);

	// What BrowseForBuildingDataFile's callers should call with whatever path the (now dual-format)
	// file picker returned, instead of picking a loader themselves - dispatches to
	// LoadBuildingDataFromDatabase for a ".db" path, LoadBuildingDataFromPath for anything else (kept
	// as the default so an unrecognized/missing extension still gets the same "not valid JSON" error
	// it always did, rather than a new, less informative failure mode). Returns how many buildings
	// were spawned.
	UFUNCTION(BlueprintCallable, Category = "Building")
	int32 LoadBuildingDataFromPathOrDatabase(const FString& FilePath);

	// ABuildingGenerator::GetScale() pass-through, defaulting to 1 if no BuildingGenerator is placed
	// in the level - what any footprint-space size/distance constant (snap tolerance, prefab room
	// size, etc.) should divide by to convert to a fixed WORLD size, instead of assuming footprint
	// units are already sanely sized (see PlacePrefabRoomAt's own comment - that assumption breaks
	// for a database-loaded building, where Scale is much larger than buildings.json's own ~10).
	UFUNCTION(BlueprintPure, Category = "Building")
	float GetBuildingScale() const;

	// The "New" button should call this before switching to the edit screen: destroys every
	// currently-spawned building/interior and resets back to a blank project (no file path, no
	// unsaved-changes flag) - without this, buildings drawn in a previous New/Open session would
	// still be sitting in the level.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void StartNewProject();

	// The folder project files live in (InNavi/Data, as an absolute path) - where New project writes its file and where
	// the Open dialog starts.
	FString GetProjectsDirectory() const;

	// The file CreateNewProject would write for this name: the name with the characters Windows does not allow in a file
	// name replaced, plus ".json". Empty if nothing usable is left of the name.
	FString GetProjectFilePathForName(const FString& Name) const;

	// What the New project dialog's Create button does: starts a blank project, gives it the name and description, writes
	// it as a blank file in the projects folder, and makes that file the one Ctrl+S saves to. Returns false and says why in
	// OutError (shown in the dialog) if the name is empty or unusable, a project with that name already exists (nothing is
	// ever overwritten), or the file could not be written.
	bool CreateNewProject(const FString& Name, const FString& Description, FString& OutError);

	// Tests only: use another folder instead of InNavi/Data.
	void SetProjectsDirectoryOverrideForTests(const FString& Directory) { ProjectsDirectoryOverride = Directory; }

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

	// What the confirm dialog's "Discard" button calls, right before it navigates away (Back to
	// exterior, Back to start screen, etc.): puts every building that was opened for editing back to how
	// it was at the last save/open - floor count (the exterior shell's height), name and interior data
	// (see ABuildingGenerator::RestoreBuildingBaselines) - and clears the unsaved-changes flag, so the very
	// next unrelated navigation does not wrongly prompt again. Edits to the live interior itself are
	// already gone once it is left (they only reach the building data on a save), so callers must leave or
	// reload the interior afterwards; this does not touch the interior that is currently loaded. Only
	// buildings that were opened for editing are covered - it never reloads the project, so it stays cheap
	// however big the database is.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void DiscardUnsavedChanges();

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

	// Debug helper for tuning FocusCameraOnAllBuildings' default exterior view - fly the free-fly
	// camera to wherever looks right, press G, and read the pawn's Location/Rotation off the Output
	// Log (and on-screen) to hardcode as the new default. Not gated to any particular screen/mode.
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void LogCameraTransform();

	// What the exterior page's Reset button calls - just FocusCameraOnAllBuildings under a public
	// name, since that's private (also called automatically after New/Open). Not named
	// ResetExteriorCameraView to avoid confusion with the existing, unrelated
	// RestoreExteriorCameraView (unwinds interior-editing camera/mode state on Back, not a "reset to
	// default view" at all despite the similar name).
	UFUNCTION(BlueprintCallable, Category = "Building")
	void ResetExteriorCameraToDefaultView();

	// What the exterior page's "set current as default" button calls - reads back the free-fly
	// camera's current pitch/yaw and its distance from the loaded campus's own center as a fraction of
	// the campus's own span (not a raw world position, which would only make sense for this exact
	// campus - see ApplyExteriorCameraSpeed's own reasoning for the same "ratio, not raw value" idea),
	// stores them as the new ExteriorDefaultView.../persists to disk, and FocusCameraOnAllBuildings
	// picks them up from then on. No-op (with OnActionFailed) if nothing is loaded to measure against.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetCurrentViewAsDefaultExterior();

	// Passthrough to Generator->SetRoadCategoryVisible - what the exterior page's road-filter panel
	// checkboxes call.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetRoadCategoryVisible(ERoadCategory Category, bool bVisible);

	// Passthrough to Generator->SetAllRoadsVisible - what the filter panel's master "Hide All"
	// checkbox calls.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetAllRoadsVisible(bool bVisible);

	// What the exterior page's "Campus Only" checkbox calls. Computes the campus's own bounding
	// center/radius from the loaded (university-only) buildings via ComputeAllBuildingsBounds, then
	// forwards Center and a padded radius to Generator->SetRoadsAndPOIsWithinCampusOnly. No-op if
	// nothing is loaded yet.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetRoadsAndPOIsWithinCampusOnly(bool bEnable);

	// Passthroughs to Generator->SetCampusBuildingsVisible/SetNonCampusBuildingsVisible - what the
	// filter panel's two building-visibility checkboxes call.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetCampusBuildingsVisible(bool bVisible);
	void SetBuildingFilter(const FString& Query, int32 MinimumFloors, int32 MaximumFloors);
	FString GetBuildingFilterText() const;
	int32 GetBuildingFilterMinimumFloors() const;
	int32 GetBuildingFilterMaximumFloors() const;
	bool AreCampusBuildingsVisible() const;
	const TMap<FString, FString>& GetFilteredBuildingOptions() const;

	// One building of the exterior list (the Map panel's list), for the buildings that pass the current filters.
	struct FBuildingListItem
	{
		FString Id;
		FString Name;
		int32 Floors = 0;
	};
	// Those buildings, by name.
	TArray<FBuildingListItem> GetFilteredBuildingList() const;

	// What the Building panel shows about the selected building. False when nothing is selected.
	bool GetSelectedBuildingSummary(FBuildingSummary& OutSummary) const;
	uint64 GetBuildingResultsRevision() const;
	int32 GetEditableBuildingCount() const;
	bool SelectBuildingById(const FString& BuildingId);
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetNonCampusBuildingsVisible(bool bVisible);

	UPROPERTY(BlueprintAssignable, Category = "Interior")
	FOnSelectModeChanged OnSelectModeChanged;

	// See FOnEditModeChanged's own comment - covers every edit mode, including the ones without a
	// dedicated per-mode delegate (add-door, prefab shape, connector type).
	UPROPERTY(BlueprintAssignable, Category = "Interior")
	FOnEditModeChanged OnEditModeChanged;

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

	// The selected room's (or wall's) bounding box in footprint units: its top-left corner and its size. False if none is selected.
	bool GetSelectedRoomBounds(FVector2D& OutPosition, FVector2D& OutSize) const;
	bool SetSelectedRoomTags(const TArray<FString>& InTags);
	TArray<FString> GetSelectedRoomTags() const;

	// Whether the currently-selected room/wall is an actual closed room (true) or an open
	// standalone wall (false) - see AInteriorGenerator::IsRoomClosedLoop. What the room properties
	// panel should check to show "Wall property" instead of "Room property" for a selected wall.
	// Returns false if nothing is selected.
	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsSelectedRoomClosedLoop() const;

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
	// populate a dropdown with: "corridor", "Outside" on Floor 1, followed by every other closed
	// room's CURRENT NAME (not its id) on the door's own floor - excluding the door's own room
	// (InputId), since a door can't connect to the room it's already in, and excluding open walls,
	// since only closed rooms are valid targets. Shown/picked by name since that's what a user
	// actually remembers a room by (it's user-editable, unlike the auto-generated id) -
	// SetSelectedDoorConnectsTo resolves the picked name back to the room's stable id before storing
	// it. Empty if no door is selected.
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

	// Steps the interior back to its previous state (see AInteriorGenerator::Undo) and clears the
	// current room selection, since the undone state may no longer contain the selected room. Bound
	// to Ctrl+Z (see UndoInteriorKeyHandler) and to the interior editor's own Undo button. No-op
	// (returns false) if there's nothing to undo.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool UndoInteriorEdit();

	// Steps the interior forward again after an Undo (see AInteriorGenerator::Redo) - same
	// selection-clearing reasoning as UndoInteriorEdit. Bound to Ctrl+Y and to the interior editor's
	// own Redo button. No-op (returns false) if there's nothing to redo.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool RedoInteriorEdit();

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

	// Which shape is currently armed - 0 = none, 1 = Rectangle, 2 = Triangle, 3 = Circle (matches
	// TogglePrefabRoomMode's ShapeId convention). For UI that needs to tell the three apart, unlike
	// the plain on/off of IsInPrefabRoomMode.
	UFUNCTION(BlueprintPure, Category = "Interior")
	int32 GetActivePrefabShape() const { return ActivePrefabShape; }

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

	// Which connector type is currently armed - 0 = none, otherwise the int32 value of
	// EInteriorNavNodeType (Elevator=1/Stairs=2/Escalator=3), matching ToggleConnectorMode's
	// convention. For UI that needs to tell the three apart, unlike the plain on/off of
	// IsInConnectorMode.
	UFUNCTION(BlueprintPure, Category = "Interior")
	int32 GetActiveConnectorType() const { return ActiveConnectorType; }

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

	// How tall one floor of the loaded building is, in world units (cm); 300 if nothing is loaded.
	float GetLoadedBuildingFloorHeight() const;

	UFUNCTION(BlueprintPure, Category = "Interior")
	FString PeekNextRoomId() const;

	UFUNCTION(BlueprintPure, Category = "Interior")
	const TArray<FInteriorNavNode>& GetNavNodes() const;

	// Every room/wall id on Level (AInteriorRoom::RoomId, both closed rooms and open standalone
	// walls) - for a Layers panel to list without needing to touch AInteriorRoom* directly (same
	// id/string-only convention every other UI-facing accessor here already follows).
	UFUNCTION(BlueprintPure, Category = "Interior")
	TArray<FString> GetRoomIdsOnFloor(int32 Level) const;

	// Any room/wall's current name by id (unlike GetSelectedRoomName, which only reads whichever one
	// is currently selected) - what a Layers panel calls to label each Rooms/Walls row. Empty if no
	// room with that id exists.
	UFUNCTION(BlueprintPure, Category = "Interior")
	FString GetRoomName(const FString& RoomId) const;
	// The tags of any room on the loaded building (empty if the room does not exist or has none).
	TArray<FString> GetRoomTags(const FString& RoomId) const;

	// Any room/wall's current closed-loop state by id (unlike IsSelectedRoomClosedLoop, which only
	// reads whichever one is currently selected) - what a Layers panel calls to sort each id from
	// GetRoomIdsOnFloor into its Rooms or Walls category. False if no room with that id exists.
	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsRoomClosedLoop(const FString& RoomId) const;

	// Per-category and per-item visibility on the 2D floor plan (see InteriorFloorPlanWidget's
	// NativePaint, which reads these to decide what to actually draw) - driven by the interior
	// editor's Layers panel. Two-level model, like a real Outliner: a category switch that hides
	// everything in it at once, plus per-item overrides underneath (so turning the category back on
	// doesn't undo an individual item's own hidden state). All default visible.
	UFUNCTION(BlueprintPure, Category = "Interior|Layers")
	bool IsBoundaryVisible() const { return bBoundaryVisible; }
	UFUNCTION(BlueprintCallable, Category = "Interior|Layers")
	void SetBoundaryVisible(bool bVisible) { bBoundaryVisible = bVisible; }

	// Closed rooms and open standalone walls (AInteriorRoom::IsClosedLoop - see IsRoomClosedLoop
	// above) are separate Layers categories despite sharing the same underlying id/class - Rooms
	// covers the closed ones, Walls the open ones.
	UFUNCTION(BlueprintPure, Category = "Interior|Layers")
	bool AreRoomsVisible() const { return bRoomsVisible; }
	UFUNCTION(BlueprintCallable, Category = "Interior|Layers")
	void SetRoomsVisible(bool bVisible) { bRoomsVisible = bVisible; }

	UFUNCTION(BlueprintPure, Category = "Interior|Layers")
	bool AreWallsVisible() const { return bWallsVisible; }
	UFUNCTION(BlueprintCallable, Category = "Interior|Layers")
	void SetWallsVisible(bool bVisible) { bWallsVisible = bVisible; }

	UFUNCTION(BlueprintPure, Category = "Interior|Layers")
	bool ArePathNodesVisible() const { return bPathNodesVisible; }
	UFUNCTION(BlueprintCallable, Category = "Interior|Layers")
	void SetPathNodesVisible(bool bVisible) { bPathNodesVisible = bVisible; }

	UFUNCTION(BlueprintPure, Category = "Interior|Layers")
	bool AreIONodesVisible() const { return bIONodesVisible; }
	UFUNCTION(BlueprintCallable, Category = "Interior|Layers")
	void SetIONodesVisible(bool bVisible) { bIONodesVisible = bVisible; }

	UFUNCTION(BlueprintPure, Category = "Interior|Layers")
	bool IsRoomHidden(const FString& RoomId) const { return HiddenRoomIds.Contains(RoomId); }
	UFUNCTION(BlueprintCallable, Category = "Interior|Layers")
	void SetRoomHidden(const FString& RoomId, bool bNewHidden);

	UFUNCTION(BlueprintPure, Category = "Interior|Layers")
	bool IsPathNodeHidden(const FString& NodeId) const { return HiddenPathNodeIds.Contains(NodeId); }
	UFUNCTION(BlueprintCallable, Category = "Interior|Layers")
	void SetPathNodeHidden(const FString& NodeId, bool bNewHidden);

	UFUNCTION(BlueprintPure, Category = "Interior|Layers")
	bool IsIONodeHidden(const FString& NodeId) const { return HiddenIONodeIds.Contains(NodeId); }
	UFUNCTION(BlueprintCallable, Category = "Interior|Layers")
	void SetIONodeHidden(const FString& NodeId, bool bNewHidden);

	// The currently-selected building's id (left-click a building to select it), or empty if
	// nothing is selected. Feed this straight into LoadInteriorForBuilding for an "Enter Interior"
	// button.
	UFUNCTION(BlueprintPure, Category = "Building")
	FString GetSelectedBuildingId() const;

	UPROPERTY(BlueprintAssignable, Category = "Building")
	FOnBuildingSelected OnBuildingSelected;

	// See FOnActiveFloorChanged above.
	UPROPERTY(BlueprintAssignable, Category = "Interior")
	FOnActiveFloorChanged OnActiveFloorChanged;

	// Every building's current display name - for a building-switcher dropdown (see
	// WBP_InteriorScreen) to list. Populate the dropdown with this, then resolve the picked
	// option back to an id via FindBuildingIdByName before calling LoadInteriorForBuilding.
	UFUNCTION(BlueprintCallable, Category = "Building")
	TArray<FString> GetAllBuildingNames() const;

	// Resolves a building name (as shown in the dropdown above) back to its stable id. Empty if no
	// building currently has that name.
	UFUNCTION(BlueprintPure, Category = "Building")
	FString FindBuildingIdByName(const FString& Name) const;

	// The currently-loaded building's display name (see LoadInteriorForBuilding) - for a
	// building-switcher dropdown to show as its initial selection. Empty if nothing is loaded.
	UFUNCTION(BlueprintPure, Category = "Building")
	FString GetLoadedBuildingName() const;

	// Renames the currently-loaded building - what the dropdown's little rename/pencil control
	// should call. No-op (returns false) if nothing is loaded, or if NewName is already used by a
	// different building (see ABuildingGenerator::SetBuildingName). Sets HasUnsavedChanges() on
	// success, same as every other mutating action - switching buildings (or leaving) right after
	// a rename will prompt to save, same as any other unsaved edit.
	UFUNCTION(BlueprintCallable, Category = "Building")
	bool SetLoadedBuildingName(const FString& NewName);

	// Grow/shrink whatever's currently selected - a room by a fixed 20-unit nudge, or a connector
	// (elevator/stairs/escalator) by a SizeScale multiplier step (see NudgeSelectedRoomSize, which
	// despite the name handles both - kept the room-era name since it's already bound to =/- and the
	// Toolbar panel's Gizmos Grow/Shrink buttons). Move itself is intentionally NOT supported for a
	// connector - only rotate/scale, per the "选中后旋转和缩放（不能移动，只能旋转/缩放）" requirement.
	UFUNCTION(BlueprintCallable, Category = "Room")
	void GrowSelectedRoom();
	UFUNCTION(BlueprintCallable, Category = "Room")
	void ShrinkSelectedRoom();

	// Continuous rotation of whatever's currently selected (room or connector) while held
	// (RotationDegreesPerSecond, applied in Tick) - bound to 9/0 and called directly by the Toolbar
	// panel's Gizmos Rotate buttons' OnPressed/OnReleased. CCW = counter-clockwise, CW = clockwise.
	UFUNCTION(BlueprintCallable, Category = "Room")
	void StartRotateCCW();
	UFUNCTION(BlueprintCallable, Category = "Room")
	void StopRotateCCW();
	UFUNCTION(BlueprintCallable, Category = "Room")
	void StartRotateCW();
	UFUNCTION(BlueprintCallable, Category = "Room")
	void StopRotateCW();

	// Lists every playable audio file (.wav) currently sitting in InNavi/Data/Audio/ - just
	// filenames, sorted, for the Audio panel to list (mirrors the Data/ folder convention
	// buildings.json already uses - a raw folder next to the .uproject, not imported UE assets).
	// Rescans the folder on every call rather than caching it, so a file dropped in while the app is
	// running shows up the next time the panel refreshes, no restart needed. Empty if the folder
	// doesn't exist yet.
	UFUNCTION(BlueprintCallable, Category = "Audio")
	TArray<FString> GetAvailableAudioFiles() const;

	// Starts playing FileName (one of GetAvailableAudioFiles' own results) - see PlayWavFile, which
	// does the actual loading/playing. No-op (returns false) if FileName is already playing (see
	// IsAudioFilePlaying) or doesn't exist. Multiple different files can play at once, each with its
	// own UAudioComponent (see FActiveAudioPlayback) - Play the same file again once it's finished
	// (or after StopAllAudio) to replay it.
	UFUNCTION(BlueprintCallable, Category = "Audio")
	bool PlayAudioFile(const FString& FileName);

	// Stops and cleans up every currently-playing audio file at once.
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void StopAllAudio();

	UFUNCTION(BlueprintPure, Category = "Audio")
	bool IsAudioFilePlaying(const FString& FileName) const;

	// Plays a short click sound (InNavi/Data/Audio/click_button.wav) - bound to every button's own
	// OnClicked across the app (see each screen/row widget's own PlayButtonClickSound wrapper method,
	// which forwards here, bound right alongside that button's own specific handler) so clicking
	// anything gives the same audible feedback. Fire-and-forget - see PlayWavFile/PlayShortSoundEffect,
	// which auto-destroy the UAudioComponent once the clip finishes, nothing to stop early or list as
	// "playing".
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void PlayButtonClickSound();

	// The Audio panel's own checkbox toggles this (see UInteriorScreenWidget::
	// OnAudioClickSoundCheckChanged) - PlayButtonClickSound no-ops while it's false. Not persisted
	// across sessions (a UI preference, not project data - matches the dock panels' own
	// bDockPanelUserHidden, which isn't saved either); always starts true.
	UFUNCTION(BlueprintPure, Category = "Audio")
	bool IsButtonClickSoundEnabled() const { return bButtonClickSoundEnabled; }

	UFUNCTION(BlueprintCallable, Category = "Audio")
	void SetButtonClickSoundEnabled(bool bEnabled) { bButtonClickSoundEnabled = bEnabled; }

	// Plays a short selection sound (InNavi/Data/Audio/select_ui.wav) - called from
	// SelectBuildingUnderCursor only when a click newly selects an exterior building (not on deselect,
	// and not for anything selected inside the interior editor - rooms/doors/connectors have their own
	// selection flow entirely separate from this one). Same fresh-decode-every-call pattern as
	// PlayButtonClickSound (see its own comment for why).
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void PlayBuildingSelectSound();

	// See IsButtonClickSoundEnabled/SetButtonClickSoundEnabled's own comment - same idea, own flag.
	UFUNCTION(BlueprintPure, Category = "Audio")
	bool IsBuildingSelectSoundEnabled() const { return bBuildingSelectSoundEnabled; }

	UFUNCTION(BlueprintCallable, Category = "Audio")
	void SetBuildingSelectSoundEnabled(bool bEnabled) { bBuildingSelectSoundEnabled = bEnabled; }

	// Plays a short notification sound (InNavi/Data/Audio/quit_confirm.wav) - called once from
	// UConfirmDialogWidget::NativeConstruct, so it plays the instant the Save/Discard/Cancel dialog
	// appears (leaving via Back, File > New, File > Load, switching buildings - anywhere
	// HasUnsavedChanges() triggers it), not tied to any button click inside it. Same fresh-decode-
	// every-call pattern as the other short sound effects.
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void PlayQuitConfirmSound();

	// See IsButtonClickSoundEnabled/SetButtonClickSoundEnabled's own comment - same idea, own flag.
	UFUNCTION(BlueprintPure, Category = "Audio")
	bool IsQuitConfirmSoundEnabled() const { return bQuitConfirmSoundEnabled; }

	UFUNCTION(BlueprintCallable, Category = "Audio")
	void SetQuitConfirmSoundEnabled(bool bEnabled) { bQuitConfirmSoundEnabled = bEnabled; }

	// Backs the Audio panel's "Mute All" checkbox - sets all three UI-sound flags
	// (click/select/quit-confirm) to bEnabled at once, e.g. SetAllUiSoundsEnabled(false) to mute
	// everything in one call. Deliberately doesn't track "what was on before" - un-muting always turns
	// all three back on, rather than restoring whatever subset was on before muting.
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void SetAllUiSoundsEnabled(bool bEnabled);

	// Multiplies the volume of every sound this class plays (the Audio panel's own file playback and
	// all three short UI effects) - see PlayWavFile, which applies this to every UAudioComponent it
	// creates. Clamped to [0, 1]. Live-updates every currently-playing ActiveAudioPlaybacks entry too,
	// so dragging the Audio panel's volume slider is heard immediately, not just on the next play.
	UFUNCTION(BlueprintPure, Category = "Audio")
	float GetMasterVolume() const { return MasterVolume; }

	UFUNCTION(BlueprintCallable, Category = "Audio")
	void SetMasterVolume(float NewVolume);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	// Left-click: selects a building normally, or adds a footprint point while in build mode.
	void OnClick();

	UPROPERTY()
	UNavigationPanelWidget* NavigationPanel = nullptr;

	TWeakObjectPtr<const UObject> PointerOverUIOwner;
	TFunction<bool(const FVector2D&)> PointerOverUICallback;
	UPROPERTY(VisibleAnywhere, Category = "Navigation")
	class UNavigationRouteVisualization* RouteVisualization = nullptr;

	void SelectBuildingUnderCursor();

	// Build mode (Plan A): click to trace out a new building's footprint on the ground.
	// FinishBuilding/CancelBuilding are declared public above (Blueprint-callable).
	void AddFootprintPointUnderCursor();

	// Ctrl+S handler - wraps SaveCurrentProject() because key bindings require a void() signature.
	void SaveCurrentProjectKeyHandler();
	void UndoInteriorKeyHandler();
	void RedoInteriorKeyHandler();
	void CopyInteriorKeyHandler();
	void PasteInteriorKeyHandler();
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

	// Bound to =/-, and called directly by the Toolbar panel's Gizmos Grow/Shrink buttons - see
	// NudgeSelectedRoomSize. GrowSelectedRoom/ShrinkSelectedRoom themselves are declared public above
	// (Blueprint/UMG-callable); this is just their shared implementation. Also handles a selected
	// connector (elevator/stairs/escalator) instead of a room - see NudgeSelectedRoomSize's own
	// comment.
	void NudgeSelectedRoomSize(float Delta);

	// See NudgeSelectedRoomSize's connector branch - AInteriorGenerator::ScaleConnectorGroup's own
	// SizeScale multiplier, not an absolute unit size like a room's Delta.
	static constexpr float ConnectorScaleStep = 0.1f;

	static constexpr float RotationDegreesPerSecond = 90.f;
	// No OnActionFailed broadcast on failure - called every tick while a rotation key is held,
	// same reasoning as MoveRoomTo/MoveDoor.
	void RotateSelectedRoom(float DeltaDegrees);
	bool bRotatingCCW = false;
	bool bRotatingCW = false;
	// Snapshot of SelectedRoomId's footprint taken when a rotation gesture starts (neither key held
	// -> one held), passed as the fallback to CommitRoomRotation when it ends (one held -> neither) -
	// same drag-start/drag-end split as MoveRoom/CommitRoomMove, but for a key-hold gesture instead
	// of a mouse drag.
	TArray<FVector2D> RotationStartFootprint;
	void BeginRotationGestureIfNeeded();
	void EndRotationGestureIfFinished();

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

	FString ProjectsDirectoryOverride;

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
	FString CopiedRoomId;
	TArray<FVector2D> CopiedRoomFootprint;
	FString CopiedRoomName;
	TArray<FString> CopiedRoomTags;
	bool bHasCopiedRoom = false;
	bool bCopiedRoomIsPublic = true;
	bool bCopiedRoomRequiresKey = false;
	// Whole-building copy/paste
	bool bHasCopiedBuilding = false;
	FString CopiedBuildingId;
	FString CopiedBuildingName;
	TArray<FVector2D> CopiedBuildingFootprint;
	int32 CopiedBuildingFloorCount = 0;
	float CopiedBuildingFloorHeight = 300.f;
	FVector2D CopiedBuildingOffset = FVector2D::ZeroVector;
	TSharedPtr<FJsonObject> CopiedBuildingInterior;
	// See SelectDoor/GetSelectedDoorRoomId/GetSelectedDoorId - same lifetime rules as
	// SelectedRoomId, and mutually exclusive with it (selecting one clears the other).
	FString SelectedDoorRoomId;
	FString SelectedDoorId;

	// See SelectConnector/GetSelectedConnectorGroupId - same lifetime rules as SelectedRoomId, and
	// mutually exclusive with both SelectedRoomId and SelectedDoorRoomId/SelectedDoorId.
	FString SelectedConnectorGroupId;

	// Whichever AInteriorRoom/AInteriorConnectorMarker is currently tinted as the 3D Edit mode
	// selection indicator (see RefreshSelectionHighlight) - null if nothing is, or if 3D preview has
	// never been entered yet. Untyped since it can be either kind of actor; resolved with Cast when
	// un-highlighting.
	UPROPERTY()
	AActor* HighlightedSelectionActor = nullptr;

	// Un-highlights HighlightedSelectionActor, then re-highlights whichever room/connector
	// SelectedRoomId/SelectedDoorRoomId/SelectedConnectorGroupId now points at (a selected door
	// highlights its owning room - there's no separate 3D visual for "just this door"). Called at the
	// end of SelectRoom/SelectDoor/SelectConnector regardless of bIsIn3DPreviewMode, since tinting a
	// mesh that's currently hidden behind the 2D overlay is harmless and is already correct the next
	// time 3D preview opens.
	void RefreshSelectionHighlight();

	// See IsBoundaryVisible/AreWallsVisible/ArePathNodesVisible/AreIONodesVisible and the
	// IsXHidden/SetXHidden pairs above - driven by the interior editor's Layers panel, read by
	// InteriorFloorPlanWidget::NativePaint to decide what to actually draw.
	bool bBoundaryVisible = true;
	bool bRoomsVisible = true;
	bool bWallsVisible = true;
	bool bPathNodesVisible = true;
	bool bIONodesVisible = true;
	TSet<FString> HiddenRoomIds;
	TSet<FString> HiddenPathNodeIds;
	TSet<FString> HiddenIONodeIds;

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

	// Moves the exterior camera so the whole building is in view, from an angle above and in front of it, for
	// picking a building from the filter results list. Unlike FocusCameraOnBuilding (the top-down framing used
	// when a building's interior is entered) it never hides any floors, never touches the saved exterior view
	// and never marks the building as cut open - the building stays complete.
	void FocusCameraOnBuildingFromExterior(APolyBuilding* Building);

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

	// Shared by FocusCameraOnAllBuildings/IncreaseCameraSpeedExterior/DecreaseCameraSpeedExterior/
	// SetCameraSpeedExterior - clamps NewSpeed and applies it as MaxSpeed, and scales
	// Acceleration/Deceleration to match (see its own .cpp comment for why that second part matters).
	void ApplyExteriorCameraSpeed(float NewSpeed);

	// The footprint-bounds-to-world-space Center/Span computation FocusCameraOnAllBuildings and
	// SetCurrentViewAsDefaultExterior both need (the same math GetBuildingFootprintBounds does for a
	// single building, just across every loaded one). Returns false (leaving OutCenter/OutSpan
	// untouched) if Generator is unset or nothing is loaded.
	bool ComputeAllBuildingsBounds(FVector2D& OutCenter, float& OutSpan) const;

	// See SetCurrentViewAsDefaultExterior's own comment - loaded once in BeginPlay, saved by that
	// function. Tries Saved/ExteriorEditorLayout.json first (same Saved-dir/per-machine convention as
	// the interior editor's own InteriorDockLayout.json), then falls back to the team's shipped
	// Data/DefaultLayouts/DefaultExteriorLayout.json if no per-user save exists yet.
	void LoadExteriorCameraDefaultFromDisk();
	void SaveExteriorCameraDefaultToDisk() const;

	// Shared by LoadExteriorCameraDefaultFromDisk for both paths above - same file format either way,
	// just a different path. Returns false (leaving the ExteriorDefaultView... members untouched) if
	// FilePath doesn't exist or doesn't parse.
	bool TryLoadExteriorCameraDefaultFromFile(const FString& FilePath);

	// Defaults match the values FocusCameraOnAllBuildings shipped with before this became overridable
	// (see its own history) - overwritten by LoadExteriorCameraDefaultFromDisk if a save exists.
	float ExteriorDefaultViewPitch = -40.3f;
	float ExteriorDefaultViewYaw = 270.1f;
	float ExteriorDefaultViewDistanceRatio = 0.38f;
	static constexpr float ExteriorDefaultViewMinDistance = 2000.f;

	// Shared by PositionCameraForFloor and PositionCameraFor3DPreview - both need the same
	// footprint-bounds-to-world-space Center/Span computation, just for different camera poses.
	void GetBuildingFootprintBounds(APolyBuilding* Building, FVector2D& OutCenter, float& OutSpan) const;

	// Recomputes ThreeDPreviewCameraDistance from the building's own footprint (see
	// GetBuildingFootprintBounds - deliberately not scoped to the current floor's own rooms, so every
	// floor of a given building frames at the same distance instead of a sparsely-drawn floor zooming
	// in tight) and resets the pose back to the default oblique angle - i.e. "best fit" for Building.
	// Called on entering 3D preview, on every floor switch while already in it, and by
	// ResetCamera3DPreview, so switching buildings picks up that building's own footprint instead of
	// inheriting whatever the previous building (or the user's own free-fly) left behind. Does NOT
	// reposition the camera itself - callers still need their own PositionCameraFor3DPreview afterward.
	void ResetFloorFraming(APolyBuilding* Building);

	// A genuine oblique/isometric camera pose over Building's Level (as opposed to
	// PositionCameraForFloor's near-top-down `-80°` floor-plan pose), aimed at Building's own footprint
	// center (see GetBuildingFootprintBounds) from ThreeDPreviewYaw/Pitch at ThreeDPreviewCameraDistance
	// - all three are set by ResetFloorFraming/ZoomIn/Out3DPreview, this function just reads whatever
	// they currently are. Also hides Building's ceiling above Level, same reasoning as
	// PositionCameraForFloor. WASD/mouse-look then freely move the camera away from this pose until
	// the next floor switch or ResetCamera3DPreview snaps it back.
	void PositionCameraFor3DPreview(APolyBuilding* Building, int32 Level);

	// Hides (or restores) every loaded building except the one currently being edited - only
	// meaningful while 3D preview mode is active; 2D mode leaves every building visible (just out of
	// frame) same as it always has.
	void SetOtherBuildingsHidden(bool bNewHidden);

	// Bound to mouse wheel (see SetupInputComponent) - no-ops unless bIsIn3DPreviewMode. Adjusts
	// ThreeDPreviewCameraDistance within a fixed min/max and re-runs PositionCameraFor3DPreview.
	void ZoomIn3DPreview();
	void ZoomOut3DPreview();

	// Refreshes the route's dedicated renderer on point/route/preview/floor changes. Includes
	// direction arrows, endpoints and transition labels without clearing editor drawing guides.
	UFUNCTION()
	void RefreshRoute3DVisualization();

	// 3D Edit mode click routing - the 3D-view equivalent of everything InteriorFloorPlanWidget's own
	// mouse handlers do for the 2D floor plan. Called from OnClick() when bIsIn3DPreviewMode &&
	// bIsIn3DEditMode; dispatches on the same mode flags 2D's widget already checks (draw room/wall,
	// prefab, connector placement, add-door, select), each branch calling the exact same mutation
	// function 2D calls, just fed a 3D-derived point/edge/offset instead of a 2D one.
	void Handle3DEditClick();

	// Draws a persistent debug sphere/line for the most recently added point of PendingRoomFootprint
	// (mirrors AddFootprintPointUnderCursor's own debug draw for the exterior footprint tool) - without
	// this, draw room/wall mode in 3D has no 2D widget to paint the points being clicked out, so there
	// was no way to see where they landed. Called from Handle3DEditClick right after a point is
	// successfully added; FinishRoom/CancelRoom/FinishWall/CancelWall already call
	// FlushPersistentDebugLines on their own, which clears these same markers.
	void DrawPendingRoomPointMarker3D();

	// Converts a 3D click into a footprint-space point (what AddRoomPointAtFootprintPosition/
	// PlacePrefabRoomAt/PlaceConnectorAt all expect) - Hit.Location.XY / Scale - BuildingOffset when
	// Hit is a real blocking hit (mirrors AddFootprintPointUnderCursor's own unscale-and-subtract and
	// LocalPositionToFootprintPoint's inverse); otherwise falls back to intersecting the camera ray
	// with the active floor's own Z-plane (DeprojectMousePositionToWorld + FMath::RayPlaneIntersection),
	// so a click just outside the building's real geometry still resolves to something. Returns false
	// only if even that plane intersection is degenerate.
	bool Get3DFloorFootprintPoint(const FHitResult& Hit, FVector2D& OutPoint) const;

	// Finds the closest edge of Room's footprint to WorldHit (a real 3D world position, not a widget-
	// local one - mirrors UInteriorFloorPlanWidget::FindRoomNearLocalPosition's own projection math,
	// just without the screen/pixel step since this is already world space) and the offset along that
	// edge, in world units, matching AddDoorToWall's expected units exactly. When
	// MaxDistanceWorldUnits >= 0, returns false if even the closest edge is farther than that (used by
	// Select3DEditTarget to tell "clicked near a wall" from "clicked in the middle of the room" - add-
	// door mode passes no limit, since it only ever fires from a wall hit already). OutOffsetWorldUnits
	// is the raw click-projected offset by default; bCenterOffsetForDoorWidth shifts it back by half a
	// door's width (same as FindRoomNearLocalPosition always does) so it's ready to feed straight into
	// AddDoorToWall as a new door's start position - only the add-door call site wants that shift,
	// since FindDoorAtEdgeOffset needs the raw click position to test against an EXISTING door's range.
	bool FindNearestRoomEdge3D(AInteriorRoom* Room, const FVector& WorldHit, int32& OutEdgeIndex, float& OutOffsetWorldUnits, float MaxDistanceWorldUnits = -1.f, bool bCenterOffsetForDoorWidth = false) const;

	// Finds a Door-type opening on Room's EdgeIndex whose [Offset, Offset+Width] range contains
	// OffsetWorldUnits - used by Select3DEditTarget to tell a door-marker click from a plain-wall
	// click on the same edge. Null if none matches.
	const FInteriorOpening* FindDoorAtEdgeOffset(AInteriorRoom* Room, int32 EdgeIndex, float OffsetWorldUnits) const;

	// Select-mode click dispatch for 3D Edit mode - mirrors the 2D floor plan's own hit-test priority
	// (connector marker -> door on the hit room's wall -> room). A room's floor slab and walls share
	// one actor/collision (see AInteriorRoom), so a click anywhere inside the room - not just near a
	// wall - already resolves to it directly, with no separate "room interior" fallback needed the way
	// 2D's point-in-polygon FindRoomContainingLocalPosition is. Called by Handle3DEditClick.
	void Select3DEditTarget(const FHitResult& Hit);

	// Drag-to-move a selected room in 3D Edit mode - the 3D equivalent of InteriorFloorPlanWidget's
	// own mouse-down/move/up drag sequence, driven by a separate LeftMouseButton press/release
	// binding (see SetupInputComponent) plus Tick polling instead of native widget mouse events, since
	// there's no 2D widget underneath to deliver those in 3D. OnLeftMouseDown3DEdit only actually
	// starts a drag if Edit mode + select mode are active and the click hit a room; OnLeftMouseUp3DEdit
	// commits it (or reverts, via CommitRoomMove) if one was in progress. Connectors are excluded -
	// they're rotate/scale only, never moved (see RotateSelectedRoom/NudgeSelectedRoomSize's own
	// connector branches).
	void OnLeftMouseDown3DEdit();
	void OnLeftMouseUp3DEdit();
	bool bIsDragging3DRoom = false;
	FString Dragging3DRoomId;
	TArray<FVector2D> DragStart3DRoomFootprint;
	FVector2D DragStart3DFootprintPosition = FVector2D::ZeroVector;

	bool bIsIn3DPreviewMode = false;
	bool bIsIn3DEditMode = false;
	float ThreeDPreviewCameraDistance = 0.f;
	float ThreeDPreviewYaw = -45.f;
	float ThreeDPreviewPitch = -35.f;
	static constexpr float ThreeDPreviewMinCameraDistance = 300.f;
	static constexpr float ThreeDPreviewMaxCameraDistance = 6000.f;
	static constexpr float ThreeDPreviewZoomStep = 200.f;
	static constexpr float ThreeDPreviewDefaultYaw = -45.f;
	static constexpr float ThreeDPreviewDefaultPitch = -35.f;
	static constexpr float ThreeDPreviewMoveSpeedStep = 300.f;
	static constexpr float ThreeDPreviewMinMoveSpeed = 200.f;
	static constexpr float ThreeDPreviewMaxMoveSpeed = 5000.f;

	// See FocusCameraOnAllBuildings and IncreaseCameraSpeedExterior/DecreaseCameraSpeedExterior's own
	// comments. Min/max also double as FocusCameraOnAllBuildings' own clamp range, so a manually-tuned
	// speed and the auto-computed default always agree on the same floor/ceiling.
	static constexpr float ExteriorCameraSpeedFactor = 1.25f;
	static constexpr float ExteriorCameraMinSpeed = 2000.f;
	static constexpr float ExteriorCameraMaxSpeed = 300000.f;

	FVector SavedCameraLocation = FVector::ZeroVector;
	FRotator SavedCameraRotation = FRotator::ZeroRotator;
	bool bHasSavedCameraView = false;

	// Id of the building currently being interior-edited (i.e. has a floor/roof section hidden via
	// PositionCameraForFloor) - empty if none. RestoreExteriorCameraView shows everything again.
	FString InteriorBuildingHiddenById;

	// See GetAvailableAudioFiles/PlayAudioFile's own comments.
	static FString GetAudioFolderPath();

	// Hosts every currently-playing UAudioComponent - one small persistent actor with no visible mesh,
	// rather than attaching to the possessed Pawn (which gets destroyed/swapped when entering/leaving
	// interiors), spawned lazily on the first PlayWavFile call.
	UPROPERTY()
	AActor* AudioActor = nullptr;

	UPROPERTY()
	TArray<FActiveAudioPlayback> ActiveAudioPlaybacks;

	// Removes any playback whose UAudioComponent has actually finished (IsPlaying() false) - see
	// PlayWavFile's own comment for what makes that actually happen for a USoundWaveProcedural.
	void PruneFinishedAudioPlaybacks();

	// Loads FileName (a name under GetAudioFolderPath()) via LoadWavFileAsSoundWave and plays it
	// through a fresh UAudioComponent on AudioActor - the shared core behind both PlayAudioFile
	// (bDestroyWhenFinished=false; the caller tracks/stops it via ActiveAudioPlaybacks) and
	// PlayShortSoundEffect (bDestroyWhenFinished=true; nothing else references it). Returns
	// nullptr if the file doesn't exist or isn't a WAV this understands.
	//
	// USoundWaveProcedural doesn't signal "finished" on its own - once the audio it was queued with
	// (see LoadWavFileAsSoundWave) runs out, it just pads silence forever rather than ending the
	// voice, so a component playing one would otherwise report IsPlaying()==true indefinitely. This
	// schedules an explicit Stop() (and DestroyComponent() if bDestroyWhenFinished) at the clip's
	// own Duration instead of relying on that, which is what actually makes IsAudioFilePlaying/
	// PruneFinishedAudioPlaybacks see it as finished, and what stops a fire-and-forget effect from
	// occupying a voice forever.
	UAudioComponent* PlayWavFile(const FString& FileName, bool bDestroyWhenFinished);

	// Thin wrapper over PlayWavFile for the short, fire-and-forget UI sound effects (click/select/
	// quit-confirm) - see PlayButtonClickSound's own comment for why nothing needs to track these.
	void PlayShortSoundEffect(const TCHAR* FileName);

	// See IsButtonClickSoundEnabled/SetButtonClickSoundEnabled's own comment.
	bool bButtonClickSoundEnabled = true;

	// See IsBuildingSelectSoundEnabled/SetBuildingSelectSoundEnabled's own comment.
	bool bBuildingSelectSoundEnabled = true;

	// See IsQuitConfirmSoundEnabled/SetQuitConfirmSoundEnabled's own comment.
	bool bQuitConfirmSoundEnabled = true;

	// See GetMasterVolume/SetMasterVolume's own comment.
	float MasterVolume = 1.f;
};
