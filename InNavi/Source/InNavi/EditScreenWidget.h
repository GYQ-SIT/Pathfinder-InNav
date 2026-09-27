// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EditScreenWidget.generated.h"

class UButton;

// Pure-C++ replacement for WBP_EditScreen (the exterior building-placement screen), built the
// same way as StartScreenWidget/ws-jh's NavigationPanelWidget. "Back" returns to the start
// screen, guarded by the same unsaved-changes confirm flow as the interior screen (see
// ConfirmDialogWidget) - includes a fix for a bug the original WBP_EditScreen had: Discard never
// called DiscardUnsavedChanges(), so the very next unrelated navigation could wrongly re-prompt
// (same class of bug already fixed on the interior screen this session). "Enter Interior" loads
// the selected building's interior and hands off to WBP_InteriorScreen (still a UMG Blueprint -
// not converted in this pass), and is hidden until a building is actually selected.
UCLASS()
class INNAVI_API UEditScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UEditScreenWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnEnterInteriorClicked();

	// Bound alongside every button's own OnClicked so clicking anything plays the same click sound -
	// see ANavPlayerController::PlayButtonClickSound, which owns the actual caching/playback.
	UFUNCTION()
	void PlayButtonClickSound();

	// Bound to ANavPlayerController::OnBuildingSelected - reveals the Enter Interior button once
	// there's actually a building selected to enter.
	UFUNCTION()
	void HandleBuildingSelected(FString BuildingId);

	void GoToStartScreen();

	UFUNCTION()
	void HandleSaveAndLeave();

	UFUNCTION()
	void HandleDiscardAndLeave();

	UFUNCTION()
	void HandleCancel();

	UPROPERTY()
	UButton* BackButton = nullptr;

	UPROPERTY()
	UButton* EnterInteriorButton = nullptr;
};
