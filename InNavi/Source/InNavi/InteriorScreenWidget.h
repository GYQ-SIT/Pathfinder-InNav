// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteriorScreenWidget.generated.h"

class UBorder;
class UButton;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class UInteriorFloorPlanWidget;
class UTextBlock;

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

	// Door properties panel - shown/populated whenever the selected door changes (see
	// ANavPlayerController::OnSelectedDoorChanged), hidden when deselected (both ids empty).
	UFUNCTION()
	void HandleSelectedDoorChanged(FString RoomId, FString DoorId);

	UFUNCTION()
	void OnApplyDoorClicked();

	// Room properties panel - shown/populated whenever the selected room/wall changes (see
	// ANavPlayerController::OnSelectedRoomChanged), hidden when deselected (empty id). Title
	// switches between "Room property"/"Wall property" via IsSelectedRoomClosedLoop().
	UFUNCTION()
	void HandleSelectedRoomChanged(FString RoomId);

	UFUNCTION()
	void OnApplyRoomClicked();

	// Connector (elevator/stairs/escalator) properties panel - shown/populated whenever the
	// selected connector changes (see ANavPlayerController::OnSelectedConnectorChanged), hidden
	// when deselected (empty GroupId). The floor checklist only shows boxes for floors that
	// actually exist on the loaded building (GetLoadedBuildingFloorCount) - a plain loop here
	// instead of the 20 hand-wired Blueprint blocks the UMG version needed.
	UFUNCTION()
	void HandleSelectedConnectorChanged(FString GroupId);

	UFUNCTION()
	void OnApplyConnectorClicked();

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

	UPROPERTY()
	UInteriorFloorPlanWidget* FloorPlanWidget = nullptr;

	UPROPERTY()
	UButton* BackButton = nullptr;

	UPROPERTY()
	UBorder* DoorPanel = nullptr;

	UPROPERTY()
	UTextBlock* DoorRoomText = nullptr;

	UPROPERTY()
	UComboBoxString* ConnectsToComboBox = nullptr;

	UPROPERTY()
	UButton* ApplyDoorButton = nullptr;

	UPROPERTY()
	UBorder* RoomPanel = nullptr;

	UPROPERTY()
	UTextBlock* RoomTitleText = nullptr;

	UPROPERTY()
	UEditableTextBox* RoomNameTextBox = nullptr;

	UPROPERTY()
	UButton* ApplyRoomButton = nullptr;

	UPROPERTY()
	UBorder* ConnectorPanel = nullptr;

	UPROPERTY()
	UTextBlock* ConnectorTypeText = nullptr;

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
};
