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
struct FNavigationRoutePoint;
class UInteriorFloorPlanWidget;
class ULayerRowWidget;
class UAudioFileRowWidget;
class UAudioPanelWidget;
class UNavigationDockPanelWidget;
class UMapPanelWidget;
class UDebugPanelWidget;
class ULayoutValidationPanelWidget;
class UPluginToolHost;
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

	// The dock system hosting the eight panels (null until the widget tree is built).
	class UDockHostWidget* GetDockHost() const { return DockHost; }
	UAudioPanelWidget* GetAudioPanelForTests() const { return AudioPanelWidget; }
	UDebugPanelWidget* GetDebugPanelForTests() const { return DebugPanelWidget; }
	ULayoutValidationPanelWidget* GetValidationPanelForTests() const { return ValidationPanelWidget; }

	// Finds a widget of this screen by name. GetWidgetFromName cannot see the dock panels' contents - they are
	// registered with DockHost and are not parented in the widget tree - so this also looks at everything the
	// screen created. For tests and tooling.
	UWidget* FindScreenWidget(FName Name) const;
	UMapPanelWidget* GetMapPanelForTests() const { return MapPanelWidget; }
	void ShowValidationPanel();
	// Step by step navigation: a step outside is shown on the exterior screen (the campus). Returns true when this screen has handed over
	// to it (what was edited here stays, unsaved). Registered with the controller (SetNavigationStepScreenCallback); public for tests.
	bool HandOverToCampusForStep(const FNavigationRoutePoint& Point);

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

	// Bound to ANavPlayerController::OnSelectedEntranceChanged: shows which kind of door an outer-wall door is and where it leads, with Delete.
	UFUNCTION()
	void HandleSelectedEntranceChanged(FString EntranceId);

	// Bound to ANavPlayerController::OnBeforeProjectSave: applies what was typed or picked in the Properties panel but not
	// applied yet (room name, type and tags; a connector's floors), so a save never leaves it behind.
	UFUNCTION()
	void HandleBeforeProjectSave();

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

	// Bound to ANavPlayerController::OnRoomColoursChanged: the button's swatch shows the colours or greys out.
	UFUNCTION()
	void HandleRoomColoursChanged(bool bVisible);

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
	// Opens another building's interior (asking what to do with unsaved changes first). What the switcher and the Map panel do.
	void SwitchToBuilding(const FString& NewBuildingId);
	// What shows the open building, after another one has been opened.
	void RefreshAfterBuildingSwitch();

	UFUNCTION()
	void OnEditBuildingNameClicked();

	// Shared by both combo boxes on this screen (building switcher, door "Connect to") - the
	// engine's default-generated dropdown item text renders too dark to read against this screen's
	// dark theme, so both are bound to build their own white-text item widget instead.
	UFUNCTION()
	UWidget* HandleGenerateComboBoxItem(FString Item);

	// The same for the room-type drop-down, with the type's colour as a small square before its name.
	UFUNCTION()
	UWidget* HandleGenerateRoomTypeItem(FString Item);

	// What the drop-downs hand to Slate is a UMG widget made on the spot; nothing else owns it, so without this it can be collected while the
	// drop-down still draws it (a crash on the next paint). Only the newest are kept: older ones are long off screen.
	UPROPERTY()
	TArray<TObjectPtr<UWidget>> RetainedComboItems;
	UWidget* RetainComboItem(UWidget* Item);

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

	// Camera menu (after Window, only while in 3D preview): Reset, the typed speed box, +, -. Opens its own
	// dropdown like File/Window do.
	UFUNCTION()
	void OnCameraMenuClicked();

	// Window > Reset Layout: puts every panel back where the team default has it.
	UFUNCTION()
	void OnWindowResetLayoutClicked();


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

	// Turns the room colours in the 2D plan on or off (the button after Snapping).
	UFUNCTION()
	void OnRoomColoursClicked();

	// Calls PC->UndoInteriorEdit()/RedoInteriorEdit() - same row as Snap, always visible in both 2D
	// and 3D (mirrors the Ctrl+Z/Ctrl+Y key bindings, doesn't replace them).
	UFUNCTION()
	void OnUndoClicked();

	UFUNCTION()
	void OnRedoClicked();

	// Calls PC->ResetCamera3DPreview() - a row of the Camera menu, which is only shown while in 3D preview
	// (see ResetView3DButton).
	UFUNCTION()
	void OnResetView3DClicked();

	// Calls PC->Increase/DecreaseMoveSpeed3DPreview() - rows of the Camera menu (see
	// IncreaseSpeedButton/DecreaseSpeedButton).
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

	// File > Generate floor: fills the floor being edited with rooms along its outer wall and a core beside each door (one undo step).
	UFUNCTION()
	void OnFileGenerateFloorClicked();

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

	// A search-result row was clicked: switches to the room's floor and selects it ("result:<floor>:<room id>").
	UFUNCTION()
	void HandleLayerRowLabelClicked(FString RowKey);

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

	// Navigation panel - the shared UNavigationDockPanelWidget (see NavigationDockPanelWidget.h), which shows the state of
	// ws/jh's UNavigationPanelWidget (route selection/planning logic). HandleNavigationStateChanged (bound to that
	// state's OnStateChanged) repaints it.
	UFUNCTION()
	void HandleNavigationStateChanged();
	// The step being shown has changed (step by step navigation): the plan is shown whole, so the step is in view.
	UFUNCTION()
	void HandleGuidedStepChanged();

	void RefreshNavigationPanelDisplay();

	// The lines of the Debug panel that are specific to this screen (building, floor, tool, selection...).
	FString BuildDebugInfo() const;

	// Bound alongside every button's own OnClicked across this whole screen (see each button's
	// construction site in RebuildWidget()/NativeConstruct()) so clicking anything plays the same
	// click sound - forwards to ANavPlayerController::PlayButtonClickSound, which owns the actual
	// caching/playback.
	UFUNCTION()
	void PlayButtonClickSound();

	// Deletes whatever is selected (room, wall, door or connector) - what Backspace does.
	UFUNCTION()
	void OnDeleteSelectionClicked();

	// Saves these tags (a comma-separated list) on the selected room - part of Save changes.
	void ApplyRoomTags(const FString& TagsText);

	// Shows the selected room's type in the type drop-down.
	void RefreshRoomTypeDisplay();

	// The kind tag at the top of Properties: its text, and the colour of its outline.
	void SetKindTag(const FString& Label, const FLinearColor& OutlineColor);

	// Fills the read-only position / size fields from the selected room's bounding box.
	void RefreshRoomBoundsFields();

	// Refills the navigation panel's Start/Destination dropdowns. Only needed when the room list could actually have
	// changed (building/floor structure), not on every click - see the call sites.
	void RefreshNavRoomDropdowns();

	// --- Docking ---
	// The eight panels are laid out by the dock system (DockLayoutModel / DockHostWidget / DockHostUMG): tab
	// groups, splits, edge areas, drag-to-dock, floating. This class only builds each panel's content, registers
	// it with DockHost, and reacts to layout changes. Back and the menu bars are fixed chrome, not dockable.
	// Navigation, Audio and Debug start hidden (see InteriorDock::BuildDefaultLayout); the Window menu shows them.
	enum class EDockPanelId : uint8{Toolbar,Floor,Properties,Layers,Navigation,Audio,Map,Debug,Validation
};

	static constexpr int32 NumDockPanels = 9;

	void InitializeDockRuntimeArrays();

	// The canvas's own current local size - what every panel's Position/Size actually means its
	// coordinates in. UWidgetLayoutLibrary::GetViewportSize returns the platform's raw pixel
	// resolution, which is NOT the same space once a DPI scale curve is in effect (the two only
	// happen to match at DPI scale 1) - mixing the two was why dragging used to clamp panels into a
	// box visibly smaller than the real screen. Falls back to GetViewportSize only on the very first
	// call, before the canvas has painted even once and its cached geometry is still zero.
	FVector2D GetCanvasSize() const;

	// Like GetCanvasSize, but never falls back to GetViewportSize - returns false (OutSize untouched)
	// if RootCanvas's cached geometry isn't valid yet.
	bool GetReliableCanvasSize(FVector2D& OutSize) const;

	// The last canvas size that was real (the screen had been laid out), kept for saving the dock layout when the screen is
	// being torn down and no longer has a geometry. Zero until the screen has been on screen.
	mutable FVector2D LastKnownCanvasSize = FVector2D::ZeroVector;

	// Window menu show/hide for one panel (restores its last position when shown again).
	void SetDockPanelUserHidden(EDockPanelId Id, bool bHidden);

	void InitializeDockLayout();
	bool IsDockPanelShown(EDockPanelId Id) const;
	void HandleDockPanelClosed(FName Panel);
	void CancelNavigationPickIfNavigationHidden();
	void HandleDockLayoutChanged();
	void RefreshWindowMenuChecks();
	void ResetDockLayout();
	// Content widgets size themselves from their panel's width; this feeds them the real width of the
	// dock group currently showing each panel.
	void SyncDockPanelWidths();

	UPROPERTY()
	class UDockHostWidget* DockHost = nullptr;

	// Persists the dock layout to one global JSON file under Saved/ - a per-machine UI preference, not tied
	// to any particular building/project. Called right before PC->SaveCurrentProject() from the places that
	// invoke it (Ctrl+S, Back's "Save and leave", the building switcher's "Save and switch").
	void SaveDockLayoutToDisk() const;

	static const TCHAR* GetDockPanelName(EDockPanelId Id);

	// Re-applies each panel's own content widths (buttons, wrapped labels, floor list rows, ...) from its
	// current DockPanelWidths entry.
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

	// The panel's content-root border (registered with DockHost).
	UPROPERTY()
	UBorder* FloorPanel = nullptr;

	// Scrollable (not a plain VerticalBox) and height-capped to ~3 rows in RebuildWidget - a
	// building can have many floors, and listing them all would eat too much screen space.
	UPROPERTY()
	UScrollBox* FloorListBox = nullptr;

	// The number of floors, in the Floor panel's heading.
	UPROPERTY()
	UTextBlock* FloorCountText = nullptr;

	UPROPERTY()
	UButton* AddFloorButton = nullptr;

	UPROPERTY()
	UButton* RemoveFloorButton = nullptr;

	// Shared by Door/Room/Connector properties - see the comment above HandleSelectedDoorChanged.
	UPROPERTY()
	UBorder* PropertiesPanel = nullptr;

	UPROPERTY()
	UTextBlock* PropertiesTitleText = nullptr;

	// What the selection is, in the panel's heading (ROOM, WALL, DOOR, CONNECTOR); hidden with nothing selected.
	UPROPERTY()
	UBorder* PropertiesKindTag = nullptr;
	UPROPERTY()
	UTextBlock* PropertiesKindText = nullptr;
	// The selected room's name, under the section heading.
	UPROPERTY()
	UTextBlock* RoomSubtitleText = nullptr;
	// The label of the room section's Delete button ("Delete room" or "Delete wall").
	UPROPERTY()
	UTextBlock* RoomDeleteLabel = nullptr;
	// Read-only: the selected room's bounding box.
	UPROPERTY()
	UEditableTextBox* RoomXBox = nullptr;
	UPROPERTY()
	UEditableTextBox* RoomYBox = nullptr;
	UPROPERTY()
	UEditableTextBox* RoomWidthBox = nullptr;
	UPROPERTY()
	UEditableTextBox* RoomHeightBox = nullptr;
	// Every box the Properties panel puts in a SizeBox so its width follows the panel (see RefreshDockPanelContentWidths).
	UPROPERTY()
	TArray<USizeBox*> PropertiesWidthBoxes;

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
	// The room's type, picked from RoomTypes::All(); saved with Save changes.
	UPROPERTY()
	UComboBoxString* RoomTypeComboBox = nullptr;

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

	// "LEVEL n", in the Layers panel's heading: the floor whose layers are listed.
	UPROPERTY()
	UTextBlock* LayersLevelText = nullptr;

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

	// The Navigation dock panel (shared with the exterior screen, see NavigationDockPanelWidget.h). The state it shows lives in
	// ANavPlayerController::GetNavigationPanel.
	UPROPERTY()
	UNavigationDockPanelWidget* NavigationPanelWidget = nullptr;

	// The Audio dock panel (shared with the exterior screen, see AudioPanelWidget.h). Starts hidden (see
	// InteriorDock::BuildDefaultLayout).
	UPROPERTY()
	UAudioPanelWidget* AudioPanelWidget = nullptr;

	// The Map dock panel (shared with the exterior screen, see MapPanelWidget.h): the Roads section only, since this
	// screen has no outside buildings to filter. Starts shown.
	UPROPERTY()
	UMapPanelWidget* MapPanelWidget = nullptr;

	// The Debug dock panel (shared with the exterior screen, see DebugPanelWidget.h). Starts hidden.
	UPROPERTY()
	UDebugPanelWidget* DebugPanelWidget = nullptr;
	UPROPERTY()
	ULayoutValidationPanelWidget* ValidationPanelWidget = nullptr;
	UPROPERTY() UPluginToolHost* PluginToolHost = nullptr;

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

	UPROPERTY()
	UButton* RoomColoursButton = nullptr;

	UPROPERTY()
	UImage* RoomColoursIcon = nullptr;

	// Undo/Redo - same row as Snap, always visible in both 2D and 3D (this row isn't hidden by
	// preview mode) - see OnUndoClicked/OnRedoClicked.
	UPROPERTY()
	UButton* UndoButton = nullptr;

	UPROPERTY()
	UButton* RedoButton = nullptr;

	// A row of the Camera menu's dropdown (the whole menu only exists while in 3D preview - see
	// HandlePreviewModeChanged). 3D preview leaves WASD/mouse-look free rather than locked to a fixed angle,
	// so this is the "un-lose yourself" button - see PC->ResetCamera3DPreview's own comment.
	UPROPERTY()
	UButton* ResetView3DButton = nullptr;

	// Adjust the free-fly pawn's WASD move speed - see PC->IncreaseMoveSpeed3DPreview's own comment.
	// Also a row of the Camera menu's dropdown.
	UPROPERTY()
	UButton* DecreaseSpeedButton = nullptr;

	UPROPERTY()
	UButton* IncreaseSpeedButton = nullptr;

	// Typed speed entry, between DecreaseSpeedButton/IncreaseSpeedButton - see
	// OnCameraSpeedTextCommitted. Lives in the Camera menu's dropdown with the other camera controls.
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

	// The little square in front of "Save": yellow while there are unsaved changes, green once saved.
	UPROPERTY()
	UBorder* SaveIndicatorOuter = nullptr;

	UPROPERTY()
	UBorder* SaveIndicatorCore = nullptr;

	// -1 until first painted, then 0 saved / 1 unsaved.
	int8 SaveIndicatorState = -1;

	void RefreshSaveIndicator();

	UPROPERTY()
	UButton* FileMenuButton = nullptr;

	UPROPERTY()
	UBorder* FileMenuDropdown = nullptr;

	UPROPERTY()
	UButton* WindowMenuButton = nullptr;

	UPROPERTY()
	UBorder* WindowMenuDropdown = nullptr;

	// Only visible while in 3D preview (see HandlePreviewModeChanged).
	UPROPERTY()
	UButton* CameraMenuButton = nullptr;

	UPROPERTY()
	UBorder* CameraMenuDropdown = nullptr;

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

	// Heights of the two fixed bars pinned across the top of the canvas (File/Window menu, then the
	// building switcher bar directly below it) - dock panels' top boundary (SidebarTopMargin, below)
	// sits flush under both, same as every panel's left/right edges already sit flush against the
	// screen's own edges.
	static constexpr float MenuBarHeight = 40.f;
	static constexpr float BuildingSwitcherBarHeight = 56.f;
	static constexpr float SidebarTopMargin = MenuBarHeight + BuildingSwitcherBarHeight;
	static constexpr float SidebarBottomMargin = 10.f;
	// LayersListBox's own wrapping SizeBox is given a fixed height at construction (see RebuildWidget
	// - 8 rows, matches StandardButtonHeight=44/row-padding=4 there, kept in sync with these by hand)
	// so a building with many rooms/nodes doesn't make the panel absurdly tall - it scrolls internally
	// past that instead.
	static constexpr float LayersListDefaultHeight = (44.f + 4.f) * 8.f;

	// The content root of each panel (the UBorder registered with DockHost), parallel to EDockPanelId.
	UPROPERTY()
	TArray<UBorder*> DockPanelBorders;

	// The static title text at the top of each panel's content. Hidden - the dock tabs show the name.
	UPROPERTY()
	TArray<UTextBlock*> DockPanelTitleTexts;

	// The real width of the dock group currently showing each panel (see SyncDockPanelWidths).
	TArray<float> DockPanelWidths;

	bool bDockLayoutInitialized = false;
};
