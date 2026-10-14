// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteriorTypes.h"
#include "InteriorScreenWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UCanvasPanelSlot;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class FJsonObject;
class UImage;
class UInteriorFloorPlanWidget;
class ULayerRowWidget;
class UAudioFileRowWidget;
class UPanelWidget;
class UScrollBox;
class USizeBox;
class USlider;
class UTextBlock;
class UVerticalBox;

// Pure-C++ replacement for WBP_InteriorScreen, built the same way as the other converted screens.
// This is the biggest/most complex screen in the project (mode toolbar, door/room/connector
// property panels, building switcher) - built up in parts; this first pass covers the overall
// frame, the embedded 2D floor plan, the mode toolbar, and Back. Property panels and the building
// switcher are separate follow-up passes.
UCLASS()
class INNAVI_API UInteriorScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UInteriorScreenWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Swallows the scroll wheel whenever it's hovering any dock panel (Toolbar/Floor/Properties/
	// Layers/Navigation), so it can't fall through to the game viewport and trigger PC's own
	// MouseScrollUp/Down-bound 3D preview zoom - UBorder (what dock panels are built from) has no
	// OnMouseWheelEvent of its own to bind for this, unlike its OnMouseButtonDown/Up/Move, so this is
	// handled at the screen root instead, where every panel's hover state can just be checked directly.
	// A wheel over the FloorListBox/LayersScrollBox themselves scrolls those first (Slate bubbles
	// events from the deepest hit-tested widget up to its parents) - this only catches what those
	// don't consume, e.g. hovering a panel's padding or its buttons.
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	UFUNCTION()
	void OnNavigationToggleClicked();
	void RefreshNavigationToggle();
	UPROPERTY()
	UButton* NavigationToggleButton = nullptr;
	UPROPERTY()
	UTextBlock* NavigationToggleText = nullptr;

	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnDrawRoomModeClicked();

	UFUNCTION()
	void OnDrawWallModeClicked();

	UFUNCTION()
	void OnSelectModeClicked();

	UFUNCTION()
	void OnAddRectangleClicked();

	UFUNCTION()
	void OnAddTriangleClicked();

	UFUNCTION()
	void OnAddCircleClicked();

	UFUNCTION()
	void OnAddDoorModeClicked();

	UFUNCTION()
	void OnAddStairsModeClicked();

	UFUNCTION()
	void OnAddElevatorModeClicked();

	UFUNCTION()
	void OnAddEscalatorModeClicked();

	// Gizmos group: Grow/Shrink are one-shot nudges (mirrors PC's own =/- keys), Rotate CW/CCW are
	// hold-to-rotate (mirrors 9/0) - OnPressed/OnReleased on the button itself, not OnClicked, so
	// holding the mouse down keeps rotating exactly like holding the key does.
	UFUNCTION()
	void OnGrowRoomClicked();
	UFUNCTION()
	void OnShrinkRoomClicked();
	UFUNCTION()
	void OnRotateCCWPressed();
	UFUNCTION()
	void OnRotateCCWReleased();
	UFUNCTION()
	void OnRotateCWPressed();
	UFUNCTION()
	void OnRotateCWReleased();

	// Re-colors each of the 10 mode buttons green if its mode is currently active, gray otherwise -
	// bound to ANavPlayerController::OnEditModeChanged (fires on every mode on/off/switch, including
	// via hotkey, not just this screen's own buttons) so the highlight stays correct regardless of
	// how the mode actually changed.
	UFUNCTION()
	void HandleEditModeChanged();

	// Bound to ANavPlayerController::OnProjectSaved - see the comment on that delegate and on
	// SaveDockLayoutToDisk. Covers Ctrl+S and any other trigger that doesn't route through this
	// widget's own Back/building-switch handlers.
	UFUNCTION()
	void HandleProjectSaved();

	// Bound to ANavPlayerController::OnActionFailed - that delegate already covers every rejected
	// action across the whole app (room/door/connector validation, save/load, renames, ...) with a
	// human-readable reason, but until now nothing displayed it: it only ever reached the Output
	// Log, which isn't visible in a packaged build. Stores the message/severity into
	// LastActionFailedMessage/LastActionFailedSeverity so the Debug panel can show it, coloured by
	// severity (see NativeTick's DebugLastErrorText).
	UFUNCTION()
	void HandleActionFailed(FString Reason, EActionFailureSeverity Severity);

	// Bound to ANavPlayerController::OnActiveFloorChanged - see the comment on that delegate. Covers
	// the [/]/F/X keyboard shortcuts (and anything else that changes the active floor or its content)
	// on top of this screen's own Floor panel button handlers, which already call these directly.
	UFUNCTION()
	void HandleActiveFloorChanged();

	// Bound to ANavPlayerController::OnInteriorContentChanged - see the comment on that delegate.
	// Rooms are created/deleted/renamed via floor-plan clicks and the Properties panel, none of which
	// go through this screen's own Floor/Navigation handlers, so without this the Layers list and the
	// Navigation panel's room dropdowns would silently go stale until some unrelated refresh happened
	// to also run.
	UFUNCTION()
	void HandleInteriorContentChanged();

	// Bound to ANavPlayerController::OnPreviewModeChanged - hides/shows Background+FloorPlanWidget
	// opposite bIsActive (the 3D preview scene sits behind Background, already there, just normally
	// hidden) and updates the 2D/3D toggle buttons' highlight. The 5 dock panels are untouched, so
	// Floor/Layers/Navigation stay usable while previewing.
	UFUNCTION()
	void HandlePreviewModeChanged(bool bIsActive);

	// Bound to ANavPlayerController::OnEdit3DModeChanged - updates Edit3DModeToggleButton's label
	// ("View"/"Edit") and color (yellow/green).
	UFUNCTION()
	void HandleEdit3DModeChanged(bool bIsEditMode);

	// Bound to ANavPlayerController::OnSnapEnabledChanged - swaps SnapToggleIcon between
	// Icon_SnappingOn/Icon_SnappingOff so it stays correct even when toggled via the N key.
	// Parameter isn't called bIsEnabled - that shadows UWidget's own bIsEnabled property.
	UFUNCTION()
	void HandleSnapEnabledChanged(bool bNewSnapEnabled);

	void RefreshModeButtonHighlights();

	// Door/Room/Connector properties all share one PropertiesPanel (see RebuildWidget) instead of
	// three separately-positioned panels - DoorFieldsBox/RoomFieldsBox/ConnectorFieldsBox are its
	// three mutually-exclusive content sections, only one of which is ever visible (selection is
	// already mutually exclusive at the PC level). PropertiesPanel itself is shown whenever any one
	// of the three is populated, hidden when all three are empty - see RefreshPropertiesPanelVisibility.

	// Shown/populated whenever the selected door changes (see
	// ANavPlayerController::OnSelectedDoorChanged), hidden when deselected (both ids empty).
	UFUNCTION()
	void HandleSelectedDoorChanged(FString RoomId, FString DoorId);

	UFUNCTION()
	void OnApplyDoorClicked();

	// Shown/populated whenever the selected room/wall changes (see
	// ANavPlayerController::OnSelectedRoomChanged), hidden when deselected (empty id). Title
	// switches between "Room property"/"Wall property" via IsSelectedRoomClosedLoop().
	UFUNCTION()
	void HandleSelectedRoomChanged(FString RoomId);

	UFUNCTION()
	void OnApplyRoomClicked();

	// Shown/populated whenever the selected connector (elevator/stairs/escalator) changes (see
	// ANavPlayerController::OnSelectedConnectorChanged), hidden when deselected (empty GroupId).
	// The floor checklist only shows boxes for floors that actually exist on the loaded building
	// (GetLoadedBuildingFloorCount) - a plain loop here instead of the 20 hand-wired Blueprint
	// blocks the UMG version needed.
	UFUNCTION()
	void HandleSelectedConnectorChanged(FString GroupId);

	UFUNCTION()
	void OnApplyConnectorClicked();

	// Shows PropertiesPanel if any of Door/Room/Connector is currently selected on the PC, hides it
	// if none are - called at the end of each HandleSelected*Changed once that handler has already
	// shown/hidden its own fields box.
	void RefreshPropertiesPanelVisibility();

	// Building switcher - lets the user jump straight to a different building's interior without
	// going back to the exterior first. Dropdown lists every building by name (GetAllBuildingNames)
	// and defaults to the loaded one; picking a different one goes through the same
	// unsaved-changes guard as Back. "Direct" selections (our own code calling SetSelectedOption,
	// e.g. to populate the initial value or revert on Cancel) are ignored - only a real user pick
	// should trigger a switch.
	UFUNCTION()
	void HandleBuildingSwitcherChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnEditBuildingNameClicked();

	// Shared by both combo boxes on this screen (building switcher, door "Connect to") - the
	// engine's default-generated dropdown item text renders too dark to read against this screen's
	// dark theme, so both are bound to build their own white-text item widget instead.
	UFUNCTION()
	UWidget* HandleGenerateComboBoxItem(FString Item);

	void RefreshBuildingSwitcher();

	UFUNCTION()
	void HandleSwitchBuildingSave();

	UFUNCTION()
	void HandleSwitchBuildingDiscard();

	UFUNCTION()
	void HandleSwitchBuildingCancel();

	UFUNCTION()
	void HandleSaveAndLeave();

	UFUNCTION()
	void HandleDiscardAndLeave();

	UFUNCTION()
	void HandleCancel();

	void GoToEditScreen();

	// Save icon button, just left of File - calls PC->SaveCurrentProject() directly (same as Ctrl+S),
	// no confirm dialog needed since saving is never destructive.
	UFUNCTION()
	void OnSaveClicked();

	// Top menu bar (File/Window) - fixed chrome, same reasoning as Back (see RebuildWidget), not
	// part of the dock system. File and Window each toggle their own dropdown open/closed via these,
	// closing the other one's dropdown at the same time (only one open at a time).
	UFUNCTION()
	void OnFileMenuClicked();

	UFUNCTION()
	void OnWindowMenuClicked();

	// 2D/3D toggle, next to File/Window - calls PC->SetIn3DPreviewMode(false)/(true). The resulting
	// UI change (hiding/showing Background+FloorPlanWidget, button highlight) happens in
	// HandlePreviewModeChanged instead of here, since PC->OnPreviewModeChanged needs to be able to
	// drive the same UI change from other triggers too (defensive symmetry, matching every other
	// PC-state delegate in this file).
	UFUNCTION()
	void OnSwitch2DClicked();

	UFUNCTION()
	void OnSwitch3DClicked();

	// Toggles PC->IsSnapEnabled() via PC->ToggleSnapEnabled() - next to the 3D button, always visible
	// (unlike Reset/speed/View-Edit, snapping matters in 2D too). Icon itself is refreshed by
	// HandleSnapEnabledChanged, not here, since the N key can toggle it too.
	UFUNCTION()
	void OnSnapToggleClicked();

	// Calls PC->UndoInteriorEdit()/RedoInteriorEdit() - same row as Snap, always visible in both 2D
	// and 3D (mirrors the Ctrl+Z/Ctrl+Y key bindings, doesn't replace them).
	UFUNCTION()
	void OnUndoClicked();

	UFUNCTION()
	void OnRedoClicked();

	// Calls PC->ResetCamera3DPreview() - only shown while in 3D preview (see ResetView3DButton).
	UFUNCTION()
	void OnResetView3DClicked();

	// Calls PC->Increase/DecreaseMoveSpeed3DPreview() - only shown while in 3D preview (see
	// DecreaseSpeedButton/IncreaseSpeedButton).
	UFUNCTION()
	void OnDecreaseSpeedClicked();

	UFUNCTION()
	void OnIncreaseSpeedClicked();

	// Lets 3D preview's own speed be typed directly - same reasoning/commit behavior as
	// UEditScreenWidget::OnCameraSpeedTextCommitted (Enter or clicking away both commit it).
	UFUNCTION()
	void OnCameraSpeedTextCommitted(const FText& NewText, ETextCommit::Type CommitType);

	// Syncs CameraSpeedTextBox to PC->GetCameraMoveSpeed() - see UEditScreenWidget's own
	// RefreshCameraSpeedDisplay for the full reasoning.
	void RefreshCameraSpeedDisplay();

	// Toggles PC's View/Edit sub-mode (PC->SetIn3DEditMode(!PC->IsIn3DEditMode())) - only shown while
	// in 3D preview (see Edit3DModeToggleButton).
	UFUNCTION()
	void OnEdit3DModeToggleClicked();

	// Shared by OnFileMenuClicked/OnWindowMenuClicked (open one, implicitly closing the other) and
	// MenuClickCatcher (close whichever is open) - also shows/hides MenuClickCatcher in step, so it's
	// only ever hit-testable while a dropdown genuinely has something to dismiss.
	void OpenMenuDropdown(UBorder* Dropdown);
	void CloseMenuDropdowns();

	// MenuClickCatcher is a full-screen, fully transparent Border sitting above every dock panel but
	// below the dropdowns/menu bar themselves (see RebuildWidget) - the standard way to get "click
	// anywhere else closes the dropdown" in UMG, since a raw OnMouseButtonDownEvent bound to one
	// specific widget (e.g. the background) only fires when THAT widget is what's actually under the
	// cursor, not for clicks that land on some other widget instead (a dock panel, a button, ...).
	// Swallows the click that closes it (Handled) rather than also letting it act on whatever's
	// underneath - the same first-click-just-dismisses convention most UIs use for this.
	UFUNCTION()
	FEventReply OnMenuClickCatcherMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	// File > New / File > Load - the exact same actions as UStartScreenWidget's own two buttons
	// (PC->StartNewProject / PC->BrowseForBuildingDataFile + PC->LoadBuildingDataFromPath), reached
	// from inside the interior editor instead - so unlike the start screen (which can never have
	// unsaved changes), these check PC->HasUnsavedChanges() first and go through the same
	// UConfirmDialogWidget Save/Discard/Cancel flow already used for Back and the building switcher.
	UFUNCTION()
	void OnFileNewClicked();

	UFUNCTION()
	void OnFileLoadClicked();

	UFUNCTION()
	void HandleFileNewSave();

	UFUNCTION()
	void HandleFileNewDiscard();

	UFUNCTION()
	void HandleFileNewCancel();

	UFUNCTION()
	void HandleFileLoadSave();

	UFUNCTION()
	void HandleFileLoadDiscard();

	UFUNCTION()
	void HandleFileLoadCancel();

	// Window menu - one checkbox per dock panel (Toolbar/Floor/Properties/Layers), all checked by
	// default; unchecking one hides that whole panel, like Unreal's own Window menu. Each checkbox
	// needs its own tiny UFUNCTION wrapper (OnCheckStateChanged carries no per-instance identity),
	// same reasoning as the panel-drag/resize handler trios.
	UFUNCTION()
	void OnWindowToolbarCheckChanged(bool bIsChecked);

	UFUNCTION()
	void OnWindowFloorCheckChanged(bool bIsChecked);

	UFUNCTION()
	void OnWindowPropertiesCheckChanged(bool bIsChecked);

	UFUNCTION()
	void OnWindowLayersCheckChanged(bool bIsChecked);

	UFUNCTION()
	void OnWindowNavigationCheckChanged(bool bIsChecked);

	// Audio starts unchecked (see NativeConstruct) - unlike the other 5, so the panel doesn't clutter
	// the screen for every project that isn't using audio.
	UFUNCTION()
	void OnWindowAudioCheckChanged(bool bIsChecked);

	// Map panel (road filter) - starts checked, matching UEditScreenWidget's own "Map" panel default.
	UFUNCTION()
	void OnWindowMapCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnWindowDebugCheckChanged(bool bIsChecked);
	// --- Map panel (Window > Map) --- Roads section only, ported from UEditScreenWidget's own "Map"
	// panel - same PC functions (SetRoadCategoryVisible/SetAllRoadsVisible/
	// SetRoadsAndPOIsWithinCampusOnly), no Buildings section (this screen has no exterior building-
	// visibility concept).
	UFUNCTION()
	void OnRoadHideAllCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadPrimaryCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadSecondaryCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadResidentialCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadCyclewayCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadFootwayCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadOtherCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadCampusOnlyCheckChanged(bool bIsChecked);

	// Floor panel - lists every floor of the loaded building (top first, matching the mockup),
	// highlighting the active one; clicking a row switches to it. "+Add floor"/"Delete floor" call
	// straight through to ANavPlayerController::AddFloor/RemoveFloor(GetActiveFloor()), which
	// already validate and report failure (see OnActionFailed) - this panel just re-lists afterward.
	UFUNCTION()
	void HandleFloorEntryClicked(int32 FloorNumber);

	UFUNCTION()
	void OnAddFloorClicked();

	UFUNCTION()
	void OnRemoveFloorClicked();

	// Rebuilds FloorListBox's rows from scratch against the loaded building's current floor count
	// and active floor - simplest way to stay in sync since both can change from several different
	// places (this panel's own buttons, or a building switch elsewhere on this screen).
	void RefreshFloorList();

	// Layers panel - a Hierarchy/Outliner-style tree over the 2D floor plan's own content, grouped
	// into the 4 categories InteriorFloorPlanWidget::NativePaint actually draws (Boundary/Walls/
	// Path Nodes/I/O Nodes - see ANavPlayerController's IsBoundaryVisible/AreWallsVisible/
	// ArePathNodesVisible/AreIONodesVisible and the per-item IsXHidden pairs, which NativePaint reads
	// to decide what to draw). Each row's own eye toggles its visibility there; the search box
	// filters which item rows are listed (never hides a category header itself).
	UFUNCTION()
	void HandleLayerRowExpandClicked(FString RowKey);

	UFUNCTION()
	void HandleLayerRowVisibilityClicked(FString RowKey);

	// UEditableTextBox::OnTextChanged's own signature is (const FText&), not (FString) like most
	// other UFUNCTION handlers on this screen - AddUniqueDynamic needs an exact match.
	UFUNCTION()
	void HandleLayersSearchTextChanged(const FText& NewText);

	// Rebuilds LayersListBox's rows from scratch against the loaded building's current floor
	// content, ExpandedLayerCategories, and the search box's current text - called from
	// NativeConstruct, from the 3 handlers above, and anywhere else the active floor's content
	// could have changed (floor switch/add/remove, building switch).
	void RefreshLayersList();

	// Shrinks LayersListSizeBox (found via LayersListBox->GetParent(), same reasoning as
	// ConnectsToRowSizeBox etc.) to fit within AvailablePanelHeight when Layers is actually docked
	// into a column smaller than its own fixed default (see LayersListDefaultHeight); pass a
	// negative value to mean "not forced" (floating, or the sole visible member of its column),
	// restoring LayersListDefaultHeight instead. Called from RefreshDockLayout, both branches.
	void ResizeLayersListToFit(float AvailablePanelHeight);

	// Navigation panel - presentation layer for ws/jh's UNavigationPanelWidget (route selection/
	// planning logic), restyled to match this screen's own panels instead of its original bespoke
	// draggable-overlay look. Each handler just forwards to ANavPlayerController::GetNavigationPanel()'s
	// public request methods; HandleNavigationStateChanged (bound to NavigationPanel's own
	// OnStateChanged) is what actually repaints this panel's text/button state - see
	// RefreshNavigationPanelDisplay.
	UFUNCTION()
	void OnNavSetStartClicked();

	UFUNCTION()
	void OnNavSetDestinationClicked();

	UFUNCTION()
	void OnNavFindRouteClicked();

	UFUNCTION()
	void OnNavClearRouteClicked();

	UFUNCTION()
	void OnNavToggleRouteClicked();

	UFUNCTION()
	void HandleNavigationStateChanged();

	void RefreshNavigationPanelDisplay();

	// Audio panel - reads InNavi/Data/Audio/ (see ANavPlayerController::GetAvailableAudioFiles) and
	// lists every file as its own UAudioFileRowWidget row; clicking a row plays that file
	// (PlayAudioFile) and Stop All stops every currently-playing one at once (StopAllAudio). Rebuilt
	// from scratch on every refresh (no per-row width tracking like Layers/Floor) since the file list
	// is expected to be small and rarely changes mid-session.
	void RefreshAudioList();

	UFUNCTION()
	void OnAudioStopAllClicked();

	UFUNCTION()
	void HandleAudioRowPlayClicked(FString FileName);

	// AudioClickSoundCheckbox's own handler - forwards to ANavPlayerController::
	// SetButtonClickSoundEnabled.
	UFUNCTION()
	void OnAudioClickSoundCheckChanged(bool bIsChecked);

	// AudioSelectSoundCheckbox/AudioQuitConfirmSoundCheckbox's own handlers - same idea as
	// OnAudioClickSoundCheckChanged, one per sound.
	UFUNCTION()
	void OnAudioSelectSoundCheckChanged(bool bIsChecked);

	UFUNCTION()
	void OnAudioQuitConfirmSoundCheckChanged(bool bIsChecked);

	// AudioMuteAllCheckbox's own handler - checked means everything's muted (opposite sense from the
	// three "X Sound" checkboxes above, which are checked when their sound is ENABLED) - forwards to
	// ANavPlayerController::SetAllUiSoundsEnabled(!bIsChecked) and syncs the three individual
	// checkboxes to match (all unchecked while muted, all checked once un-muted).
	UFUNCTION()
	void OnAudioMuteAllCheckChanged(bool bIsChecked);

	// AudioVolumeSlider's own handler - forwards to ANavPlayerController::SetMasterVolume.
	UFUNCTION()
	void OnAudioVolumeChanged(float NewValue);

	// Bound alongside every button's own OnClicked across this whole screen (see each button's
	// construction site in RebuildWidget()/NativeConstruct()) so clicking anything plays the same
	// click sound - forwards to ANavPlayerController::PlayButtonClickSound, which owns the actual
	// caching/playback.
	UFUNCTION()
	void PlayButtonClickSound();

	// Start/Destination room dropdowns - an alternative to clicking a point on the floor plan.
	// Populated from UNavigationPanelWidget::GetSelectableRooms() (real rooms only, across every
	// floor); picking one calls straight through to SetStartToRoom/SetDestinationToRoom, which sets
	// the room's own centroid as the point (see that function's own comment for why a centroid, not
	// a door). Only repopulated when the room list could actually have changed (building/floor
	// structure), not on every click - see the RefreshNavRoomDropdowns call sites.
	UFUNCTION()
	void HandleNavStartRoomChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleNavDestinationRoomChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void HandleDestinationSearchChanged(const FText& Text);
	UFUNCTION()
	void OnApplyRoomTagsClicked();

	void RefreshNavRoomDropdowns();

	// --- Docking system for the Toolbar/Floor/Properties/Layers/Navigation/Audio panels ---
	// Each of the 6 panels below is an independently draggable UBorder placed directly on the root
	// canvas (not nested in a shared bar/VerticalBox like the old Left/Right sidebar bars). Dragging
	// a panel by its own small top-left drag handle (see DockDragHandleSize) moves it freely;
	// releasing it with an edge close to another panel's edge (TrySnapPanel) snaps
	// them together into a shared "column" that fills the screen height end to end, splitting that
	// height among the column's members (adjustable per-boundary via a splitter handle, mirrors
	// dragging the divider between two stacked panels in a real editor). Back is deliberately NOT
	// part of this system (see RebuildWidget) - it's fixed chrome, not a dockable panel. Audio and
	// Navigation both start user-hidden (see bDockPanelUserHidden's own init) unlike the other 4 -
	// see the Window menu. Map (the road-filter panel ported from UEditScreenWidget's own "Map" panel,
	// Roads section only - no Buildings section, since this screen has no exterior building-visibility
	// concept) starts VISIBLE, matching that panel's own default there.
	enum class EDockPanelId : uint8{Toolbar,Floor,Properties,Layers,Navigation,Audio,Map,Debug
};

	static constexpr int32 NumDockPanels = 8;
	static constexpr int32 MaxDockSplitters = NumDockPanels - 1; // worst case: all 7 panels in one column

	struct FDockColumn
	{
		TArray<EDockPanelId> Panels;     // top to bottom
		TArray<float> HeightFractions;   // sums to 1, parallel to Panels
		float X = 0.f;                   // shared by every panel in the column
	};

	void InitializeDockRuntimeArrays();

	// The canvas's own current local size - what every panel's Position/Size actually means its
	// coordinates in. UWidgetLayoutLibrary::GetViewportSize returns the platform's raw pixel
	// resolution, which is NOT the same space once a DPI scale curve is in effect (the two only
	// happen to match at DPI scale 1) - mixing the two was why dragging used to clamp panels into a
	// box visibly smaller than the real screen. Falls back to GetViewportSize only on the very first
	// call, before the canvas has painted even once and its cached geometry is still zero.
	FVector2D GetCanvasSize() const;

	// Like GetCanvasSize, but never falls back to GetViewportSize - returns false (OutSize untouched)
	// if RootCanvas's cached geometry isn't valid yet, rather than silently returning a value in the
	// wrong coordinate system. GetCanvasSize's own fallback turned out to not be a one-frame thing in
	// practice (confirmed via temporary logging - RootCanvas's geometry can still be unset several
	// ticks into a freshly-constructed screen, not just on the literal first call), which broke the
	// dock-layout save/restore fraction math specifically: it round-trips a panel's Position through
	// division-then-multiplication by this size, so using the raw-pixel fallback for either half of
	// that round trip (even just once) introduces a real, visible drift by the DPI scale factor - a
	// panel saved flush against the right edge would restore short of it (or past it) by however many
	// pixels that scale factor represents at the panel's own distance from center. Used by
	// BuildLayoutSaveData/ApplyLayoutSaveData for exactly this reason; every other caller of
	// GetCanvasSize (default panel positions, live drag clamping, etc.) tolerates the fallback fine,
	// since those don't require this round-trip precision.
	bool GetReliableCanvasSize(FVector2D& OutSize) const;

	// Called once from NativeConstruct - places Toolbar floating top-left and pre-docks Floor +
	// Properties into one column top-right, matching today's default look (nothing should appear
	// different until the user actually drags something).
	void InitializeDefaultDockLayout();

	// The floating position InitializeDefaultDockLayout gives Id on a fresh install - also used by
	// ApplyLayoutSaveData as a per-panel fallback, since a layout file saved before Id existed as a
	// panel (e.g. an existing save from before the Layers panel was added) won't have an entry for
	// it, and would otherwise leave it wherever its CanvasSlot's own uninitialized default position
	// is (the canvas's top-left corner).
	FVector2D GetDefaultPanelPosition(EDockPanelId Id, const FVector2D& ViewportSize) const;

	// Recomputes every panel's position/size and every splitter's position/visibility from the
	// current DockPanelWidths/DockColumns state. Called after any drag (move, width-resize, or
	// splitter) changes that state.
	void RefreshDockLayout();
	void RefreshSplitterLayout();

	// Re-splits ColumnIndex's HeightFractions proportionally to its currently-visible members'
	// actual natural content heights (GetPanelSize), instead of leaving them at whatever arbitrary
	// split they last had. Only meaningful right after a previously-hidden panel becomes visible
	// with real content in it (see PropertiesFractionRecalcFramesRemaining) - GetPanelSize needs a
	// settled, already-painted geometry to mean anything, which is why this is deferred (and
	// repeated over several ticks) rather than called inline from RefreshPropertiesPanelVisibility.
	void RecomputeColumnFractionsFromNaturalSizes(int32 ColumnIndex);

	// Docks Properties in directly underneath Floor - called once from InitializeDefaultDockLayout on
	// a fresh install (no saved layout yet) so it lands somewhere sensible by default instead of
	// appearing as a lone floating panel wherever GetDefaultPanelPosition happened to leave it. A
	// no-op if Properties already belongs to a column by then. Reuses TrySnapPanel's own
	// proportional-split logic by placing Properties directly below Floor's current bottom edge
	// first, then snapping exactly as if the user had dragged it there.
	void AutoDockPropertiesBelowFloor();

	// Sets/clears bDockPanelUserHidden[Id] (see the Window-menu comment above
	// OnWindowToolbarCheckChanged), applies it (Properties routes through
	// RefreshPropertiesPanelVisibility, which respects the flag instead of always forcing Visible;
	// the other 3 have nothing else fighting over their own visibility, so their Border is set
	// directly), and refreshes the dock layout so its dock-mates reflow into the freed space.
	void SetDockPanelUserHidden(EDockPanelId Id, bool bHidden);

	// Persists the dock layout (every panel's position/width, which columns exist, their member
	// order and height split) to one global JSON file under Saved/ - a per-machine UI preference,
	// the same for every project, not tied to any particular building/project file. Called right
	// before PC->SaveCurrentProject() from the two places that actually invoke it (Back's "Save and
	// leave", the building switcher's "Save and switch") - i.e. tied to the same deliberate save
	// action the user already performs, not saved continuously on every drag.
	void SaveDockLayoutToDisk() const;

	// Called once from InitializeDefaultDockLayout - tries the user's own saved layout first, then
	// falls back to the team's shipped default arrangement (see TryLoadDockLayoutFromFile's own
	// comment), applying whichever one actually exists and returning true; returns false only if
	// neither file is usable, leaving InitializeDefaultDockLayout to fall back to
	// GetDefaultPanelPosition's per-panel guesses.
	bool LoadDockLayoutFromDisk();

	// Shared by LoadDockLayoutFromDisk for both the per-user save (InteriorEditorLayout.json under
	// Saved/) and the team's shipped default (DefaultInteriorLayout.json under Data/DefaultLayouts/) -
	// same file format, same layoutVersion check, same ApplyLayoutSaveData/
	// PendingSavedLayoutToReapply handling either way, just a different path.
	bool TryLoadDockLayoutFromFile(const FString& FilePath);

	TSharedPtr<FJsonObject> BuildLayoutSaveData() const;

	// Converts Root's saved viewport-relative fractions back to pixel positions and applies them.
	// Returns false (applying nothing) without RootCanvas's REAL cached Slate geometry to convert
	// against - GetViewportSize()'s raw-platform-pixel fallback is a different coordinate system than
	// what UCanvasPanelSlot positions are actually in whenever the platform's DPI scale isn't 1.0 (see
	// GetReliableCanvasSize's own comment), so unlike every other caller of GetCanvasSize() in this
	// file, this one specifically cannot tolerate that fallback - the caller (NativeTick, the only
	// caller of this on the restore path) keeps retrying on a later tick instead.
	bool ApplyLayoutSaveData(const TSharedPtr<FJsonObject>& Root);

	static const TCHAR* GetDockPanelName(EDockPanelId Id);
	static bool FindDockPanelIdByName(const FString& Name, EDockPanelId& OutId);

	// Was RefreshSidebarContentWidths - re-applies each panel's own content widths (building
	// switcher combo box, buttons, wrapped labels, floor list rows) from its current
	// DockPanelWidths entry, now tracked per-panel instead of per-bar.
	void RefreshDockPanelContentWidths();
	float ComputePanelButtonWidth(EDockPanelId Id) const { return FMath::Max(80.f, DockPanelWidths[(int32)Id] - 48.f); }

	// Sets TextBlock's font size to StandardFontSize, leaving every other font property (typeface,
	// style) as whatever it already was. A plain member function rather than a RebuildWidget-local
	// lambda since HandleGenerateComboBoxItem (a combo box's per-item text, used by the building
	// switcher and the Navigation room dropdowns) needs it too, outside RebuildWidget's own scope.
	static void ApplyStandardFont(UTextBlock* TextBlock);
	// Same, for a UEditableTextBox (the building name field, room name field, Layers search box) -
	// its font lives on its WidgetStyle.TextStyle rather than being a direct SetFont() call.
	static void ApplyStandardFont(UEditableTextBox* TextBox);

	// SetToolTipText's default tooltip renders at the engine's small default font size with no way to
	// resize just that text. SetToolTip(Widget) takes over the whole tooltip instead, but its SToolTip
	// wrapper is built with no border/background of its own (BorderImage(nullptr)) - this builds a
	// small light box to replicate the default tooltip's look, just with the font doubled. A plain
	// member function (not a RebuildWidget-local lambda) since HandleSnapEnabledChanged needs to
	// rebuild SnapToggleButton's tooltip after construction too, not just at construction time.
	UWidget* MakeToolTipWidget(const TCHAR* TooltipLabel);

	FVector2D GetPanelPosition(EDockPanelId Id) const;
	FVector2D GetPanelSize(EDockPanelId Id) const;

	// DockPanelTitleTexts[Id]'s own rendered size (clamped to a sane minimum on each axis), or
	// (DockDragHandleSize, DockDragHandleSize) if that entry is somehow still null - see
	// DockPanelTitleTexts' own comment.
	FVector2D GetDockDragHandleSize(EDockPanelId Id) const;

	// Applies NewWidth to Id, and to every sibling in Id's column too (a docked column always stays
	// a uniform width, like a real docked group), then refreshes content widths + layout.
	void SetPanelWidth(EDockPanelId Id, float NewWidth);

	// Removes Id from whatever column it's currently in (giving its freed height share to a
	// neighbour), dissolving that column back to nothing if it drops to 1 remaining member. Called
	// at the start of every move-drag - the drag itself always behaves like a floating panel;
	// TrySnapPanel (on drag end) re-evaluates whether it should re-dock.
	void RemovePanelFromColumn(EDockPanelId Id);

	// Called on move-drag end: compares Id's edges against every other panel's edges, and if within
	// DockSnapThresholdPx of a match (with overlapping X ranges), snaps flush against it and
	// joins/creates a column. No match leaves Id floating exactly where it was dropped.
	void TrySnapPanel(EDockPanelId Id);

	FEventReply HandlePanelMouseButtonDown(EDockPanelId Id, const FPointerEvent& MouseEvent);
	FEventReply HandlePanelMouseMove(EDockPanelId Id, const FPointerEvent& MouseEvent);
	FEventReply HandlePanelMouseButtonUp(EDockPanelId Id, const FPointerEvent& MouseEvent);
	FEventReply HandleResizeMouseButtonDown(EDockPanelId Id, const FPointerEvent& MouseEvent);
	FEventReply HandleResizeMouseMove(EDockPanelId Id, const FPointerEvent& MouseEvent);
	FEventReply HandleResizeMouseButtonUp(EDockPanelId Id, const FPointerEvent& MouseEvent);

	// Same as HandleResizeMouse* above, but for the LEFT-edge handle - keeps the panel's right edge
	// fixed and grows/shrinks it from the left instead, shifting its X (or its whole column's X, if
	// docked - column members share X) by however much the width actually changed.
	FEventReply HandleLeftResizeMouseButtonDown(EDockPanelId Id, const FPointerEvent& MouseEvent);
	FEventReply HandleLeftResizeMouseMove(EDockPanelId Id, const FPointerEvent& MouseEvent);
	FEventReply HandleLeftResizeMouseButtonUp(EDockPanelId Id, const FPointerEvent& MouseEvent);

	// BOTTOM-edge HEIGHT handle - only meaningful while Id is floating (DockBottomResizeHandles[Id]
	// stays Collapsed, and this never gets a mouse-down to start from, while docked - see
	// DockBottomResizeHandles' own comment). Sets DockPanelHeights[Id] to an explicit value instead of
	// leaving it auto/natural, same idea as HandleResizeMouseMove but for height instead of width, and
	// with no docked-column sibling-sync to do (DockPanelHeights is per-panel only, never shared).
	FEventReply HandleBottomResizeMouseButtonDown(EDockPanelId Id, const FPointerEvent& MouseEvent);
	FEventReply HandleBottomResizeMouseMove(EDockPanelId Id, const FPointerEvent& MouseEvent);
	FEventReply HandleBottomResizeMouseButtonUp(EDockPanelId Id, const FPointerEvent& MouseEvent);

	FEventReply HandleSplitterMouseButtonDown(int32 SplitterIndex, const FPointerEvent& MouseEvent);
	FEventReply HandleSplitterMouseMove(int32 SplitterIndex, const FPointerEvent& MouseEvent);
	FEventReply HandleSplitterMouseButtonUp(int32 SplitterIndex, const FPointerEvent& MouseEvent);

	// BindDynamic can't bind a lambda or a function with extra bound parameters, so each panel/
	// splitter needs its own tiny UFUNCTION wrapper forwarding into the Handle* functions above with
	// its identity already known - mechanical, but this is the only way Slate's dynamic delegates
	// support per-instance drag state without one fully separate handler body per widget.
	UFUNCTION() FEventReply OnToolbarPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnToolbarPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnToolbarPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnFloorPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnFloorPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnFloorPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnPropertiesPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnPropertiesPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnPropertiesPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnLayersPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnLayersPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnLayersPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnNavigationPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnNavigationPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnNavigationPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnAudioPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnAudioPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnAudioPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnMapPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnMapPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnMapPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnDebugPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnDebugPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnDebugPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnToolbarResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnToolbarResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnToolbarResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnFloorResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnFloorResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnFloorResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnPropertiesResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnPropertiesResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnPropertiesResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnLayersResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnLayersResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnLayersResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnNavigationResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnNavigationResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnNavigationResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnAudioResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnAudioResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnAudioResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnMapResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnMapResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnMapResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnDebugResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnDebugResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnDebugResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnToolbarLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnToolbarLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnToolbarLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnFloorLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnFloorLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnFloorLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnPropertiesLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnPropertiesLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnPropertiesLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnLayersLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnLayersLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnLayersLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnNavigationLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnNavigationLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnNavigationLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnAudioLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnAudioLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnAudioLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnMapLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnMapLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnMapLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnDebugLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnDebugLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnDebugLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnToolbarBottomResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnToolbarBottomResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnToolbarBottomResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnFloorBottomResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnFloorBottomResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnFloorBottomResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnPropertiesBottomResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnPropertiesBottomResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnPropertiesBottomResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnLayersBottomResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnLayersBottomResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnLayersBottomResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnNavigationBottomResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnNavigationBottomResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnNavigationBottomResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnAudioBottomResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnAudioBottomResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnAudioBottomResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnMapBottomResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnMapBottomResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnMapBottomResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnDebugBottomResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnDebugBottomResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnDebugBottomResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter0MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter0MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter0MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter1MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter1MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter1MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter2MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter2MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter2MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter3MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter3MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter3MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter4MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter4MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter4MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	// The root canvas every dock panel is a direct child of - see GetCanvasSize.
	UPROPERTY()
	UCanvasPanel* RootCanvas = nullptr;

	UPROPERTY()
	UInteriorFloorPlanWidget* FloorPlanWidget = nullptr;

	UPROPERTY()
	UButton* BackButton = nullptr;

	// The 10 mode toolbar buttons - kept so RefreshModeButtonHighlights can re-color whichever one
	// is currently active. AddRect/Triangle/Circle share PC->GetActivePrefabShape() (1/2/3);
	// AddElevator/Stairs/Escalator share PC->GetActiveConnectorType() (EInteriorNavNodeType's int
	// value) - both need the specific button, not just "is some prefab/connector mode active".
	UPROPERTY()
	UButton* SelectModeButton = nullptr;

	UPROPERTY()
	UButton* DrawRoomModeButton = nullptr;

	UPROPERTY()
	UButton* DrawWallModeButton = nullptr;

	UPROPERTY()
	UButton* AddRectModeButton = nullptr;

	UPROPERTY()
	UButton* AddTriangleModeButton = nullptr;

	UPROPERTY()
	UButton* AddCircleModeButton = nullptr;

	UPROPERTY()
	UButton* AddDoorModeButton = nullptr;

	UPROPERTY()
	UButton* AddStairsModeButton = nullptr;

	UPROPERTY()
	UButton* AddElevatorModeButton = nullptr;

	UPROPERTY()
	UButton* AddEscalatorModeButton = nullptr;

	UPROPERTY()
	UButton* GrowRoomButton = nullptr;

	UPROPERTY()
	UButton* ShrinkRoomButton = nullptr;

	UPROPERTY()
	UButton* RotateCCWButton = nullptr;

	UPROPERTY()
	UButton* RotateCWButton = nullptr;

	// The Toolbar panel's own root border (mode toolbar) - was LeftSidebarBar.
	UPROPERTY()
	UBorder* ToolbarPanel = nullptr;

	// The panel border itself - RefreshDockLayout/RefreshDockPanelContentWidths reach its wrapping
	// SizeBox via FloorPanel->GetParent() rather than storing that SizeBox separately.
	UPROPERTY()
	UBorder* FloorPanel = nullptr;

	// Scrollable (not a plain VerticalBox) and height-capped to ~3 rows in RebuildWidget - a
	// building can have many floors, and listing them all would eat too much screen space.
	UPROPERTY()
	UScrollBox* FloorListBox = nullptr;

	UPROPERTY()
	UButton* AddFloorButton = nullptr;

	UPROPERTY()
	UButton* RemoveFloorButton = nullptr;

	// Shared by Door/Room/Connector properties - see the comment above HandleSelectedDoorChanged.
	UPROPERTY()
	UBorder* PropertiesPanel = nullptr;

	UPROPERTY()
	UTextBlock* PropertiesTitleText = nullptr;

	UPROPERTY()
	UVerticalBox* DoorFieldsBox = nullptr;

	UPROPERTY()
	UTextBlock* DoorRoomText = nullptr;

	UPROPERTY()
	UComboBoxString* ConnectsToComboBox = nullptr;

	// Wraps ConnectsToRow (RefreshDockPanelContentWidths keeps this in sync with the panel width) -
	// stored directly rather than reached via ConnectsToComboBox->GetParent()->GetParent(), since
	// that chain breaks silently the moment another wrapper is inserted between them (as happened
	// with RoomNameRowSizeBox/RoomNameTextBoxMinSize below).
	UPROPERTY()
	USizeBox* ConnectsToRowSizeBox = nullptr;

	UPROPERTY()
	UButton* ApplyDoorButton = nullptr;

	UPROPERTY()
	UVerticalBox* RoomFieldsBox = nullptr;

	// See the comment above ConnectsToRowSizeBox.
	UPROPERTY()
	USizeBox* RoomNameRowSizeBox = nullptr;

	UPROPERTY()
	UEditableTextBox* RoomNameTextBox = nullptr;
	UPROPERTY()
	UVerticalBox* RoomTagsFields = nullptr;
	UPROPERTY() USizeBox* RoomTagsFieldsSizeBox = nullptr;
	UPROPERTY()
	UEditableTextBox* RoomTagsTextBox = nullptr;
	UPROPERTY()
	UButton* ApplyRoomTagsButton = nullptr;

	UPROPERTY()
	UButton* ApplyRoomButton = nullptr;

	UPROPERTY()
	UVerticalBox* ConnectorFieldsBox = nullptr;

	UPROPERTY()
	UTextBlock* ConnectorTypeText = nullptr;

	// Wraps the floor checkboxes (RefreshDockPanelContentWidths keeps this in sync with the panel
	// width) - no other stored widget sits close enough in the tree to reach it via GetParent(), so
	// this is stored directly instead.
	UPROPERTY()
	USizeBox* FloorsWrapSizeBox = nullptr;

	// Index i = floor (i + 1). Fixed at 20 entries (the building's actual floor count is applied
	// via per-entry Visibility, not by changing the array size).
	UPROPERTY()
	TArray<UCheckBox*> FloorCheckboxes;

	UPROPERTY()
	UButton* ApplyConnectorButton = nullptr;

	// Layers panel - see the comment above HandleLayerRowExpandClicked.
	UPROPERTY()
	UBorder* LayersPanel = nullptr;

	UPROPERTY()
	UEditableTextBox* LayersSearchBox = nullptr;

	// Scrollable, same reasoning as FloorListBox - a building can have many rooms/walls/nodes, and
	// the fully-expanded tree could easily run past the panel's own height.
	UPROPERTY()
	UScrollBox* LayersListBox = nullptr;

	// Which category rows are currently expanded (RowKey category strings, e.g. "category:Walls") -
	// UI-only presentation state, not persisted (resets each time the interior screen reopens, same
	// as every other widget's expand/collapse state here already does).
	TSet<FString> ExpandedLayerCategories;

	// Navigation panel - see the comment above OnNavSetStartClicked. NavigationPanel here is this
	// screen's own dock panel Border, distinct from ANavPlayerController::NavigationPanel (the
	// UNavigationPanelWidget instance holding the actual selection/route state this panel displays).
	UPROPERTY()
	UBorder* NavigationPanel = nullptr;

	UPROPERTY()
	UTextBlock* NavContextText = nullptr;

	UPROPERTY()
	UComboBoxString* NavStartRoomComboBox = nullptr;

	// Frame around NavStartRoomComboBox, tinted green only when Start was set by picking this
	// dropdown (as opposed to clicking a point on the floor plan) - see RefreshNavigationPanelDisplay.
	// Transparent otherwise, so it doesn't read as a permanent gray box.
	UPROPERTY()
	UBorder* NavStartRoomBorder = nullptr;

	UPROPERTY()
	UComboBoxString* NavDestinationRoomComboBox = nullptr;
	UPROPERTY()
	UEditableTextBox* DestinationSearchBox = nullptr;
	UPROPERTY()
	UTextBlock* DestinationSearchResultsText = nullptr;

	// Same as NavStartRoomBorder, tinted red for Destination.
	UPROPERTY()
	UBorder* NavDestinationRoomBorder = nullptr;

	UPROPERTY()
	UButton* NavSetStartButton = nullptr;

	UPROPERTY()
	UTextBlock* NavSetStartButtonLabel = nullptr;

	UPROPERTY()
	UButton* NavSetDestinationButton = nullptr;

	UPROPERTY()
	UTextBlock* NavSetDestinationButtonLabel = nullptr;

	UPROPERTY()
	UButton* NavFindRouteButton = nullptr;

	UPROPERTY()
	UButton* NavClearRouteButton = nullptr;

	UPROPERTY()
	UButton* NavToggleRouteButton = nullptr;

	UPROPERTY()
	UTextBlock* NavToggleRouteButtonLabel = nullptr;

	UPROPERTY()
	UTextBlock* NavStatusText = nullptr;

	UPROPERTY()
	UTextBlock* NavRouteDetailsText = nullptr;

	// Audio panel - see the comment above RefreshAudioList. Starts hidden (bDockPanelUserHidden, set
	// in NativeConstruct) unlike every other dock panel.
	UPROPERTY()
	UBorder* AudioPanel = nullptr;

	// Map panel (road filter, Roads section only) - see EDockPanelId::Map's own comment.
	UPROPERTY()
	UBorder* MapPanel = nullptr;
	UPROPERTY()
	UBorder* DebugPanel = nullptr;
	UPROPERTY()
	UTextBlock* DebugInfoText = nullptr;
	// Separate text block (own colour) for the Last Error line - see its own construction comment.
	UPROPERTY()
	UTextBlock* DebugLastErrorText = nullptr;
	float DebugSmoothedDeltaTime = 0.f;
	// Last message/severity broadcast by ANavPlayerController::OnActionFailed (see
	// HandleActionFailed's own comment); shown as a coloured "Last Error" line in the Debug panel.
	// Message is empty until the first failure.
	FString LastActionFailedMessage;
	EActionFailureSeverity LastActionFailedSeverity = EActionFailureSeverity::Warning;
	UPROPERTY()
	UCheckBox* RoadHideAllCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadPrimaryCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadSecondaryCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadResidentialCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadCyclewayCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadFootwayCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadOtherCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadCampusOnlyCheckbox = nullptr;

	UPROPERTY()
	UButton* AudioStopAllButton = nullptr;

	// Toggles ANavPlayerController::SetButtonClickSoundEnabled - see OnAudioClickSoundCheckChanged.
	// Checked by default, matching bButtonClickSoundEnabled's own default.
	UPROPERTY()
	UCheckBox* AudioClickSoundCheckbox = nullptr;

	// Same as AudioClickSoundCheckbox, one per remaining UI sound - see OnAudioSelectSoundCheckChanged/
	// OnAudioQuitConfirmSoundCheckChanged.
	UPROPERTY()
	UCheckBox* AudioSelectSoundCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* AudioQuitConfirmSoundCheckbox = nullptr;

	// See OnAudioMuteAllCheckChanged - unchecked by default (nothing's muted at first).
	UPROPERTY()
	UCheckBox* AudioMuteAllCheckbox = nullptr;

	// See OnAudioVolumeChanged - ANavPlayerController::MasterVolume as a 0-1 slider.
	UPROPERTY()
	USlider* AudioVolumeSlider = nullptr;

	// One UAudioFileRowWidget child per file under Data/Audio/, rebuilt by RefreshAudioList. Scrollable
	// (same reasoning as LayersListBox) since the folder could hold more files than fit the panel.
	UPROPERTY()
	UScrollBox* AudioFileListBox = nullptr;

	UPROPERTY()
	UComboBoxString* BuildingSwitcherComboBox = nullptr;

	UPROPERTY()
	UEditableTextBox* BuildingNameTextBox = nullptr;

	UPROPERTY()
	UButton* EditBuildingNameButton = nullptr;

	UPROPERTY()
	UTextBlock* EditBuildingNameButtonText = nullptr;

	// 2D/3D toggle + "Reset View"/move-speed +/- - see the comment above OnSwitch2DClicked. Live on
	// the building switcher bar (same row as EditBuildingNameButton), not the menu bar above it.
	UPROPERTY()
	UButton* Switch2DButton = nullptr;

	UPROPERTY()
	UButton* Switch3DButton = nullptr;

	// Next to Switch3DButton, always visible (unlike the 3D-preview-only controls after it) - see
	// OnSnapToggleClicked/HandleSnapEnabledChanged.
	UPROPERTY()
	UButton* SnapToggleButton = nullptr;

	UPROPERTY()
	UImage* SnapToggleIcon = nullptr;

	// Undo/Redo - same row as Snap, always visible in both 2D and 3D (this row isn't hidden by
	// preview mode) - see OnUndoClicked/OnRedoClicked.
	UPROPERTY()
	UButton* UndoButton = nullptr;

	UPROPERTY()
	UButton* RedoButton = nullptr;

	// Only shown while in 3D preview (see HandlePreviewModeChanged). 3D preview leaves WASD/mouse-look
	// free rather than locked to a fixed angle, so this is the "un-lose yourself" button - see
	// PC->ResetCamera3DPreview's own comment.
	UPROPERTY()
	UButton* ResetView3DButton = nullptr;

	// Adjust the free-fly pawn's WASD move speed - see PC->IncreaseMoveSpeed3DPreview's own comment.
	// Shown/hidden together with ResetView3DButton.
	UPROPERTY()
	UButton* DecreaseSpeedButton = nullptr;

	UPROPERTY()
	UButton* IncreaseSpeedButton = nullptr;

	// Typed speed entry, between DecreaseSpeedButton/IncreaseSpeedButton - see
	// OnCameraSpeedTextCommitted. Shown/hidden together with the other 3D-preview-only controls.
	UPROPERTY()
	UEditableTextBox* CameraSpeedTextBox = nullptr;

	// Toggles 3D preview's View/Edit sub-mode (see PC->SetIn3DEditMode) - "View" in yellow by default,
	// "Edit" in green once toggled. Shown/hidden together with ResetView3DButton; always shows "View"
	// again the next time 3D preview is entered (see HandleEdit3DModeChanged/HandlePreviewModeChanged).
	UPROPERTY()
	UButton* Edit3DModeToggleButton = nullptr;

	UPROPERTY()
	UTextBlock* Edit3DModeToggleButtonText = nullptr;

	bool bIsEditingBuildingName = false;

	// Set right before showing the unsaved-changes confirm dialog for a building switch; read by
	// HandleSwitchBuildingSave/Discard once the user picks a choice.
	FString PendingBuildingId;

	// Set right before showing the dialog - what HandleSwitchBuildingCancel reverts the dropdown
	// selection back to.
	FString PreviousBuildingName;

	// Set right before showing the unsaved-changes confirm dialog for File > Load; read by
	// HandleFileLoadSave/Discard once the user picks a choice - same role as PendingBuildingId above.
	FString PendingLoadFilePath;

	// Blocks the 3D scene behind the 2D floor-plan overlay - promoted from a RebuildWidget local so
	// HandlePreviewModeChanged can hide it (Collapsed) while 3D preview mode is active.
	UPROPERTY()
	UBorder* Background = nullptr;

	// Top menu bar widgets - see the comment above OnFileMenuClicked/OnWindowToolbarCheckChanged.
	UPROPERTY()
	UButton* SaveButton = nullptr;

	UPROPERTY()
	UButton* FileMenuButton = nullptr;

	UPROPERTY()
	UBorder* FileMenuDropdown = nullptr;

	UPROPERTY()
	UButton* WindowMenuButton = nullptr;

	UPROPERTY()
	UBorder* WindowMenuDropdown = nullptr;

	UPROPERTY()
	UBorder* MenuClickCatcher = nullptr;

	UPROPERTY()
	UCheckBox* WindowToolbarCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* WindowFloorCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* WindowPropertiesCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* WindowLayersCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* WindowNavigationCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* WindowAudioCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* WindowMapCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* WindowDebugCheckbox = nullptr;
	// Parallel to EDockPanelId, sized NumDockPanels - see SetDockPanelUserHidden. False by default
	// (visible) for every panel except Audio and Navigation, which InitializeDockRuntimeArrays sets
	// true right after initializing this array - each only shows once the user actually checks it in
	// the Window menu.
	TArray<bool> bDockPanelUserHidden;

	// Shared width bounds for every dock panel, and the snap/splitter tuning constants - also used
	// by RebuildWidget for the panels' initial width. Originally 340 (raised from 260, which wasn't
	// enough room for "Name :" plus a typical room id to both stay readable in a Fill row - but that
	// was back when every panel's text rendered at UTextBlock's 24pt default). Now that text renders
	// at StandardFontSize instead, the same content needs meaningfully less width, so this has come
	// back down further still. Toolbar in particular never needed anywhere near 340 - its buttons are
	// a fixed 48x48 regardless of panel width.
	static constexpr float SidebarMinWidth = 100.f;
	// Every panel's own text (titles, button labels, status/context/detail text, dropdown items) is
	// set to this instead of leaving UTextBlock's own default (24pt) - applied via ApplyStandardFont
	// at each text's construction site, since UMG has no single "default style" to override once for
	// every independently-constructed UTextBlock.
	static constexpr int32 StandardFontSize = 15;

	// Every editable text box's background/typed-text color (see ApplyStandardFont's UEditableTextBox
	// overload) - the engine default background/foreground read as nearly the same gray against this
	// app's dark panel chrome, making typed text hard to read. A shade lighter than the panel
	// background (0.03) but darker than a normal button (0.6) so a text box reads as its own distinct
	// "this is editable" element rather than blending into either.
	static const FLinearColor StandardTextBoxBackgroundColor;
	static const FLinearColor StandardTextBoxTextColor;

	static constexpr float SidebarMaxWidth = 600.f;
	// Heights of the two fixed bars pinned across the top of the canvas (File/Window menu, then the
	// building switcher bar directly below it) - dock panels' top boundary (SidebarTopMargin, below)
	// sits flush under both, same as every panel's left/right edges already sit flush against the
	// screen's own edges.
	static constexpr float MenuBarHeight = 40.f;
	static constexpr float BuildingSwitcherBarHeight = 52.f;
	static constexpr float SidebarTopMargin = MenuBarHeight + BuildingSwitcherBarHeight;
	static constexpr float SidebarBottomMargin = 10.f;
	static constexpr float SidebarResizeHandleWidth = 8.f;
	// A dedicated small box pinned to every panel's top-left corner - HandlePanelMouseButtonDown only
	// starts a move-drag when the click lands on THIS, not anywhere else on the panel (see its own
	// comment for why: clicking panel content used to be indistinguishable from clicking empty panel
	// space, so an ordinary click meant to interact with content could accidentally start a drag).
	// Inset to roughly sit right where the panel's own title text starts (see DockPanelBorderPadding),
	// not just some arbitrary corner offset - floating disconnected from the title looked wrong in
	// practice. The handle's actual on-screen size instead tracks the title text's own rendered size on
	// both axes (see GetDockDragHandleSize), so "Floor" gets a smaller handle than "Properties" - this
	// constant is just the floor each axis is clamped to, so a very short/empty title never shrinks it
	// down to something unclickable.
	static constexpr float DockDragHandleSize = 12.f;
	static constexpr float DockSnapThresholdPx = 20.f;
	static constexpr float SplitterHeight = 8.f;
	static constexpr float DockPanelMinHeight = 80.f; // floor for a docked panel's height share

	// LayersListBox's own wrapping SizeBox is given a fixed height at construction (see RebuildWidget
	// - 8 rows, matches StandardButtonHeight=44/row-padding=4 there, kept in sync with these by hand)
	// so a building with many rooms/nodes doesn't make the panel absurdly tall while floating - it
	// scrolls internally past that instead. But a docked panel's OUTER height can be forced smaller
	// than that fixed 8-row amount (a crowded column), and since neither the panel's Border nor its
	// SizeBox clip, the list's still-384px content just painted straight through the panel's own
	// forced-smaller edge into whatever was below it. RefreshDockLayout shrinks LayersListSizeBox to
	// fit whenever Layers is actually docked, and restores this default whenever it's floating again.
	static constexpr float LayersListDefaultHeight = (44.f + 4.f) * 8.f;
	static constexpr float LayersListMinHeight = 44.f + 4.f; // at least 1 row
	static constexpr float LayersListNonListOverhead = 90.f; // title + search box + panel/row padding, generously rounded up

	// --- Widget pointers (UPROPERTY - GC-safe TArrays, same as FloorCheckboxes above), parallel to
	// EDockPanelId (size NumDockPanels) / splitter index (size MaxDockSplitters). ---
	UPROPERTY()
	TArray<UBorder*> DockPanelBorders;

	// The SizeBox wrapping each entry in DockPanelBorders is the actual Canvas child (constrains
	// width always via WidthOverride; constrains height too via HeightOverride while docked, or
	// leaves it auto/natural via ClearHeightOverride while floating) - this slot is that SizeBox's.
	UPROPERTY()
	TArray<UCanvasPanelSlot*> DockPanelCanvasSlots;

	UPROPERTY()
	TArray<UBorder*> DockResizeHandles;

	UPROPERTY()
	TArray<UCanvasPanelSlot*> DockResizeHandleSlots;

	// Second, LEFT-edge width handle per panel - see the comment above HandleLeftResizeMouseMove.
	UPROPERTY()
	TArray<UBorder*> DockLeftResizeHandles;

	UPROPERTY()
	TArray<UCanvasPanelSlot*> DockLeftResizeHandleSlots;

	// BOTTOM-edge HEIGHT handle per panel - see the comment above HandleBottomResizeMouseMove. Only
	// ever shown for a FLOATING panel (RefreshDockLayout's docked branch keeps this Collapsed) - a
	// docked panel's height already comes from its column's splitter(s), so there's nothing for this
	// to control there.
	UPROPERTY()
	TArray<UBorder*> DockBottomResizeHandles;

	UPROPERTY()
	TArray<UCanvasPanelSlot*> DockBottomResizeHandleSlots;

	// The small move-drag handle box in each panel's top-left corner - see DockDragHandleSize's own
	// comment. Positioned wherever RefreshDockLayout/HandlePanelMouseMove already position that
	// panel's other handles, so it always tracks the panel it belongs to.
	UPROPERTY()
	TArray<UBorder*> DockDragHandles;

	// Each panel's own title UTextBlock (Toolbar has no separate panel-level title - it opens straight
	// into its Default/Draw/Navigate group labels, so this holds the "Default" group label instead,
	// the first thing at its own top-left corner) - purely so the drag handle can be sized off this
	// text's own rendered size (see GetDockDragHandleSize), matching however long/short and tall the
	// panel's own title happens to be instead of one fixed size for every panel regardless of
	// "Floor" vs "Properties" vs "Navigation".
	UPROPERTY()
	TArray<UTextBlock*> DockPanelTitleTexts;

	UPROPERTY()
	TArray<UCanvasPanelSlot*> DockDragHandleSlots;

	UPROPERTY()
	TArray<UBorder*> SplitterHandles;

	UPROPERTY()
	TArray<UCanvasPanelSlot*> SplitterCanvasSlots;

	// --- Plain runtime state (no UObjects - safe as ordinary TArrays), parallel to EDockPanelId /
	// splitter index, same sizing as the widget arrays above. Populated in InitializeDockRuntimeArrays. ---
	TArray<float> DockPanelWidths;
	TArray<int32> DockPanelColumnIndex; // INDEX_NONE = floating

	// Each panel's own BorderPadding argument (see ConstructDockPanel), stashed here purely so the
	// drag handle can be inset by the SAME amount as the panel's own content (that's where the title
	// text itself starts) instead of some separate arbitrary margin that doesn't line up with it.
	TArray<float> DockPanelBorderPadding;

	TArray<bool> bDockPanelIsDraggingMove;
	TArray<FVector2D> DockPanelMoveDragStartMouse;
	TArray<FVector2D> DockPanelMoveDragStartPosition;

	TArray<bool> bDockPanelIsResizingWidth;
	TArray<float> DockPanelResizeStartMouseX;
	TArray<float> DockPanelResizeStartWidth;

	TArray<bool> bDockPanelIsResizingLeftWidth;
	TArray<float> DockPanelLeftResizeStartMouseX;
	TArray<float> DockPanelLeftResizeStartWidth;
	TArray<float> DockPanelLeftResizeStartPanelX; // the panel's (or its column's) X when the drag started

	// A FLOATING panel's explicit height, set once the user drags DockBottomResizeHandles[Id] for the
	// first time - 0 (its InitializeDockRuntimeArrays default) means "never manually resized, keep
	// sizing to natural content height" (RefreshDockLayout's floating branch only calls
	// SetHeightOverride once this is actually positive). Meaningless while docked - a docked panel's
	// height always comes from its column's own HeightFractions/splitters instead, same as
	// DockPanelWidths already does for width regardless of docked/floating.
	TArray<float> DockPanelHeights;

	TArray<bool> bDockPanelIsResizingHeight;
	TArray<float> DockPanelResizeStartMouseY;
	TArray<float> DockPanelResizeStartHeight;

	TArray<FDockColumn> DockColumns;

	TArray<int32> SplitterColumnIndex;        // INDEX_NONE = this splitter slot is unused right now
	TArray<int32> SplitterUpperIndexInColumn; // splitter sits between this and the next panel in the column
	TArray<bool> bSplitterIsDragging;
	TArray<float> SplitterDragStartMouseY;
	TArray<TArray<float>> SplitterDragStartFractions;

	bool bDockLayoutInitialized = false;

	// A saved layout loaded via LoadDockLayoutFromDisk, held onto until the first real NativeTick so
	// it can be RE-applied then (see LayoutGeometrySettleFramesRemaining's own comment on why
	// NativeConstruct-time is unreliable). ApplyLayoutSaveData converts every saved position from a
	// viewport-relative fraction back to pixels using GetCanvasSize() at the moment it runs - called
	// from NativeConstruct (as it normally is), that can return a premature/undersized viewport before
	// the widget's first real paint, which throws off the fraction math for every panel (not just the
	// border-staleness NativeTick already re-runs RefreshDockLayout for) - e.g. panels dragged flush
	// against the right edge and saved would drift left on the very next load, even on the exact same
	// screen. Null once there's nothing pending to redo (the common case, once NativeTick has run).
	TSharedPtr<FJsonObject> PendingSavedLayoutToReapply;

	// Set by RefreshPropertiesPanelVisibility on every selection change (Properties is a permanent
	// panel now - its content, not its own visibility, is what changes: Door/Room/Connector fields
	// vs the empty placeholder, each a different natural height) - counts down over the next several
	// NativeTicks, calling RecomputeColumnFractionsFromNaturalSizes + RefreshDockLayout on each one,
	// so its share (and its docked sibling's) ends up matching actual content instead of the
	// arbitrary split left over from whenever the column was first formed. More than one frame is
	// genuinely needed here, not just one for paranoia's sake - a WrapBox (the connector floor
	// checklist in particular) can take a couple of ticks to finish reflowing at its new visible
	// width before GetPanelSize reports its real settled height; stopping after just one frame could
	// lock in a still-too-small height, pushing that panel's own content (e.g. its Apply button)
	// past its box and off the bottom of the screen.
	int32 PropertiesFractionRecalcFramesRemaining = 0;
	static constexpr int32 PropertiesFractionRecalcFrameCount = 6;

	// General-purpose "give Slate a few more frames to settle, then re-run RefreshDockLayout" retry -
	// every use so far is the same underlying cause: something was just set (a Border's height
	// override, a title UTextBlock's text) that Slate hasn't actually re-measured/re-painted yet, so
	// reading it back immediately (GetPanelSize's GetCachedGeometry, GetDockDragHandleSize's
	// GetDesiredSize) returns a stale value - and, per PropertiesFractionRecalcFramesRemaining's own
	// comment, one retry isn't reliably enough (a WrapBox in particular can take a couple of ticks to
	// finish reflowing). Set by:
	// - InitializeDefaultDockLayout, unconditionally, right after it runs synchronously from
	//   NativeConstruct - before this widget has been through even one real Slate layout/paint pass,
	//   which left the docked panels' background borders AND every drag handle's title-text-based
	//   width a frame stale in practice.
	// - TrySnapPanel, right after a panel joins an EXISTING column (2+ members already) - the member
	//   it snapped against (e.g. Layers, when Properties joins below it) just had its own Border
	//   resized smaller via SetHeightOverride to make room, and RefreshSplitterLayout's new splitter
	//   position (which reads that member's GetPanelSize) needs the resize to have actually painted
	//   first.
	int32 LayoutGeometrySettleFramesRemaining = 0;
	static constexpr int32 LayoutGeometrySettleFrameCount = 6;
};
