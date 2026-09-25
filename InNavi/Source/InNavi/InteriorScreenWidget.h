// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteriorScreenWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanelSlot;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class UInteriorFloorPlanWidget;
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

	// Drag-to-resize for the left/right sidebar bars - each has its own thin handle at its inner
	// edge (LeftSidebarResizeHandle on the left bar's right edge, RightSidebarResizeHandle on the
	// right bar's left edge). MouseButtonDown on a handle starts the drag and captures the mouse;
	// MouseMove (while captured) recomputes that bar's width from how far the mouse has moved since
	// the drag started, clamped to [SidebarMinWidth, SidebarMaxWidth], repositions the handle to stay
	// glued to the bar's new edge, and calls RefreshSidebarContentWidths so the content inside
	// actually tracks the resize instead of just the bar's background; MouseButtonUp ends it.
	UFUNCTION()
	FEventReply OnLeftSidebarResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	UFUNCTION()
	FEventReply OnLeftSidebarResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	UFUNCTION()
	FEventReply OnLeftSidebarResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	UFUNCTION()
	FEventReply OnRightSidebarResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	UFUNCTION()
	FEventReply OnRightSidebarResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	UFUNCTION()
	FEventReply OnRightSidebarResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	// Re-applies every content width derived from LeftSidebarWidth/RightSidebarWidth - the
	// building-switcher combo box, both panel borders (via their wrapping SizeBox, found through
	// GetParent() rather than a separate stored pointer per element), every button inside them (via
	// GetContent()), the wrapped text labels (via GetParent()), and the floor-list rows (each one
	// individually, via UFloorListEntryWidget::SetWidth). Called after every resize step and once
	// from NativeConstruct so construction-time widths already match the live formula.
	void RefreshSidebarContentWidths();

	// Shared by RefreshSidebarContentWidths and RefreshFloorList (new rows need the current width
	// at creation time too, not just existing ones on the next resize).
	float ComputeRightSidebarButtonWidth() const { return FMath::Max(80.f, RightSidebarWidth - 48.f); }

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

	// The panel border itself - RefreshSidebarContentWidths reaches its wrapping SizeBox via
	// FloorPanel->GetParent() rather than storing that SizeBox separately.
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

	// Wraps ConnectsToRow (RefreshSidebarContentWidths keeps this in sync with the panel width) -
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

	// Wraps the floor checkboxes (RefreshSidebarContentWidths keeps this in sync with the panel
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

	// See the comment above OnLeftSidebarResizeMouseButtonDown. Also used by RebuildWidget for the
	// bars' initial layout, so both bars' construction and their resize handlers agree on these.
	// Raised from 260 - at 260, RightButtonWidth (RightSidebarWidth - 48) bottoms out at 212, which
	// wasn't enough room for "Name :" plus a typical room id to both stay readable in a Fill row.
	static constexpr float SidebarMinWidth = 340.f;
	static constexpr float SidebarMaxWidth = 600.f;
	static constexpr float SidebarTopMargin = 10.f;
	static constexpr float SidebarBottomMargin = 10.f;
	static constexpr float SidebarResizeHandleWidth = 8.f;

	UPROPERTY()
	UCanvasPanelSlot* LeftSidebarCanvasSlot = nullptr;

	UPROPERTY()
	UCanvasPanelSlot* RightSidebarCanvasSlot = nullptr;

	UPROPERTY()
	UCanvasPanelSlot* LeftSidebarResizeHandleSlot = nullptr;

	UPROPERTY()
	UCanvasPanelSlot* RightSidebarResizeHandleSlot = nullptr;

	UPROPERTY()
	UBorder* LeftSidebarResizeHandle = nullptr;

	UPROPERTY()
	UBorder* RightSidebarResizeHandle = nullptr;

	float LeftSidebarWidth = SidebarMinWidth;
	float RightSidebarWidth = SidebarMinWidth;

	bool bIsResizingLeftSidebar = false;
	float LeftSidebarResizeStartMouseX = 0.f;
	float LeftSidebarResizeStartWidth = 0.f;

	bool bIsResizingRightSidebar = false;
	float RightSidebarResizeStartMouseX = 0.f;
	float RightSidebarResizeStartWidth = 0.f;
};
