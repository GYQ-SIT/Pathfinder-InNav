// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteriorScreenWidget.generated.h"

class UBorder;
class UButton;
class UComboBoxString;
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
};
