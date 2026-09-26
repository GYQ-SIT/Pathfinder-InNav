// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteriorScreenWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UCanvasPanelSlot;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class FJsonObject;
class UInteriorFloorPlanWidget;
class ULayerRowWidget;
class UPanelWidget;
class UScrollBox;
class USizeBox;
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

	// Bound to ANavPlayerController::OnActiveFloorChanged - see the comment on that delegate. Covers
	// the [/]/F/X keyboard shortcuts (and anything else that changes the active floor or its content)
	// on top of this screen's own Floor panel button handlers, which already call these directly.
	UFUNCTION()
	void HandleActiveFloorChanged();

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

	// Top menu bar (File/Window) - fixed chrome, same reasoning as Back (see RebuildWidget), not
	// part of the dock system. File and Window each toggle their own dropdown open/closed via these,
	// closing the other one's dropdown at the same time (only one open at a time).
	UFUNCTION()
	void OnFileMenuClicked();

	UFUNCTION()
	void OnWindowMenuClicked();

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

	// --- Docking system for the Toolbar/Floor/Properties/Layers panels ---
	// Each of the 4 panels below is an independently draggable UBorder placed directly on the root
	// canvas (not nested in a shared bar/VerticalBox like the old Left/Right sidebar bars). Dragging
	// a panel by its own background (not one of its buttons - see IsScreenPositionOverPanelButton)
	// moves it freely; releasing it with an edge close to another panel's edge (TrySnapPanel) snaps
	// them together into a shared "column" that fills the screen height end to end, splitting that
	// height among the column's members (adjustable per-boundary via a splitter handle, mirrors
	// dragging the divider between two stacked panels in a real editor). Back is deliberately NOT
	// part of this system (see RebuildWidget) - it's fixed chrome, not a dockable panel.
	enum class EDockPanelId : uint8 { Toolbar, Floor, Properties, Layers };
	static constexpr int32 NumDockPanels = 4;
	static constexpr int32 MaxDockSplitters = NumDockPanels - 1; // worst case: all 4 panels in one column

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

	// Called once from InitializeDefaultDockLayout - if a previously-saved layout file exists,
	// applies it and returns true (InitializeDefaultDockLayout then skips its own placement guesses
	// entirely); returns false if there's nothing saved yet (first-ever run), leaving
	// InitializeDefaultDockLayout to fall back to its normal default placement.
	bool LoadDockLayoutFromDisk();

	TSharedPtr<FJsonObject> BuildLayoutSaveData() const;
	void ApplyLayoutSaveData(const TSharedPtr<FJsonObject>& Root);

	static const TCHAR* GetDockPanelName(EDockPanelId Id);
	static bool FindDockPanelIdByName(const FString& Name, EDockPanelId& OutId);

	// Was RefreshSidebarContentWidths - re-applies each panel's own content widths (building
	// switcher combo box, buttons, wrapped labels, floor list rows) from its current
	// DockPanelWidths entry, now tracked per-panel instead of per-bar.
	void RefreshDockPanelContentWidths();
	float ComputePanelButtonWidth(EDockPanelId Id) const { return FMath::Max(80.f, DockPanelWidths[(int32)Id] - 48.f); }

	FVector2D GetPanelPosition(EDockPanelId Id) const;
	FVector2D GetPanelSize(EDockPanelId Id) const;
	bool IsScreenPositionOverPanelButton(EDockPanelId Id, const FVector2D& ScreenPosition) const;

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

	UFUNCTION() FEventReply OnSplitter0MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter0MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter0MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter1MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter1MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter1MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter2MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter2MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply OnSplitter2MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

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

	// The Toolbar panel's own root border (building switcher + mode toolbar) - was LeftSidebarBar.
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

	UPROPERTY()
	UComboBoxString* BuildingSwitcherComboBox = nullptr;

	UPROPERTY()
	UEditableTextBox* BuildingNameTextBox = nullptr;

	UPROPERTY()
	UButton* EditBuildingNameButton = nullptr;

	UPROPERTY()
	UTextBlock* EditBuildingNameButtonText = nullptr;

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

	// Top menu bar widgets - see the comment above OnFileMenuClicked/OnWindowToolbarCheckChanged.
	UPROPERTY()
	UButton* FileMenuButton = nullptr;

	UPROPERTY()
	UBorder* FileMenuDropdown = nullptr;

	UPROPERTY()
	UButton* WindowMenuButton = nullptr;

	UPROPERTY()
	UBorder* WindowMenuDropdown = nullptr;

	UPROPERTY()
	UCheckBox* WindowToolbarCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* WindowFloorCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* WindowPropertiesCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* WindowLayersCheckbox = nullptr;

	// Parallel to EDockPanelId, sized NumDockPanels - see SetDockPanelUserHidden. All false by
	// default (every panel visible), set from the Window menu's checkboxes.
	TArray<bool> bDockPanelUserHidden;

	// Shared width bounds for every dock panel, and the snap/splitter tuning constants - also used
	// by RebuildWidget for the panels' initial width. Raised from 260 - at 260, a panel's button
	// width (Width - 48) bottoms out at 212, which wasn't enough room for "Name :" plus a typical
	// room id to both stay readable in a Fill row.
	static constexpr float SidebarMinWidth = 340.f;
	static constexpr float SidebarMaxWidth = 600.f;
	// Height of the fixed File/Window menu bar pinned across the top of the canvas - dock panels'
	// top boundary (SidebarTopMargin, below) sits flush against it, same as every panel's left/right
	// edges already sit flush against the screen's own edges.
	static constexpr float MenuBarHeight = 32.f;
	static constexpr float SidebarTopMargin = MenuBarHeight;
	static constexpr float SidebarBottomMargin = 10.f;
	static constexpr float SidebarResizeHandleWidth = 8.f;
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

	UPROPERTY()
	TArray<UBorder*> SplitterHandles;

	UPROPERTY()
	TArray<UCanvasPanelSlot*> SplitterCanvasSlots;

	// --- Plain runtime state (no UObjects - safe as ordinary TArrays), parallel to EDockPanelId /
	// splitter index, same sizing as the widget arrays above. Populated in InitializeDockRuntimeArrays. ---
	TArray<float> DockPanelWidths;
	TArray<int32> DockPanelColumnIndex; // INDEX_NONE = floating

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

	TArray<FDockColumn> DockColumns;

	TArray<int32> SplitterColumnIndex;        // INDEX_NONE = this splitter slot is unused right now
	TArray<int32> SplitterUpperIndexInColumn; // splitter sits between this and the next panel in the column
	TArray<bool> bSplitterIsDragging;
	TArray<float> SplitterDragStartMouseY;
	TArray<TArray<float>> SplitterDragStartFractions;

	bool bDockLayoutInitialized = false;

	// InitializeDefaultDockLayout runs synchronously from NativeConstruct, before this widget has
	// been through even one real Slate layout/paint pass - the docked panels' background borders end
	// up a frame stale as a result (a small chunk of background missing at a panel's bottom edge
	// until the next unrelated click forces a fresh layout pass). Set there, consumed by the first
	// real NativeTick to re-run RefreshDockLayout once everything has actually settled.
	bool bNeedsPostConstructLayoutRefresh = false;

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
};
