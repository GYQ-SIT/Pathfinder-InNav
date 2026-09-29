// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EditScreenWidget.generated.h"

class UBorder;
class UButton;
class UCheckBox;
class UScrollBox;
class UTextBlock;

// Pure-C++ replacement for WBP_EditScreen (the exterior building-placement screen), built the
// same way as StartScreenWidget/ws-jh's NavigationPanelWidget. "Back" returns to the start
// screen, guarded by the same unsaved-changes confirm flow as the interior screen (see
// ConfirmDialogWidget) - includes a fix for a bug the original WBP_EditScreen had: Discard never
// called DiscardUnsavedChanges(), so the very next unrelated navigation could wrongly re-prompt
// (same class of bug already fixed on the interior screen this session). "Enter Interior" loads
// the selected building's interior and hands off to UInteriorScreenWidget, and is hidden until a
// building is actually selected.
//
// Also has its own top menu bar (File: New/Load; Window: Audio panel visibility) and Audio panel,
// mirroring UInteriorScreenWidget's own - but this screen has no dockable-panel system at all, so
// the Audio panel here is just a fixed Border (no drag/resize/dock chrome) and Window only ever
// has the one Audio entry, not a panel-per-checkbox list. Enter Interior/Back live in this menu
// bar's own right side (Enter Interior, then Back) instead of the old standalone bottom-right
// button stack - do NOT confuse this Back (menu-bar chrome, returns to the start screen) with
// UInteriorScreenWidget's own Back (returns to THIS screen instead) - they're different buttons
// with different targets despite matching chrome/position.
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

	// --- Top menu bar: File (New/Load) + Window (Audio panel visibility) ---
	// Mirrors UInteriorScreenWidget's own menu bar layout/behavior exactly (own dropdown per menu
	// button, MenuClickCatcher for click-elsewhere-closes-it) - see that class's own header comments
	// for the full reasoning, not repeated here.

	UFUNCTION()
	void OnFileMenuClicked();

	UFUNCTION()
	void OnWindowMenuClicked();

	void OpenMenuDropdown(UBorder* Dropdown);
	void CloseMenuDropdowns();

	UFUNCTION()
	FEventReply OnMenuClickCatcherMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	// File > New / File > Load - the exact same actions as UStartScreenWidget's own two buttons
	// (PC->StartNewProject / PC->BrowseForBuildingDataFile + PC->LoadBuildingDataFromPath), reached
	// from inside the exterior edit screen instead - so unlike the start screen (which can never
	// have unsaved changes), these check PC->HasUnsavedChanges() first and go through the same
	// UConfirmDialogWidget Save/Discard/Cancel flow already used for Back. Unlike
	// UInteriorScreenWidget's own File>New/Load, this screen IS ALREADY the exterior view - no
	// RestoreExteriorCameraView or screen navigation needed, New/Load just resets/reloads the
	// building cluster currently shown and stays right here.
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

	// Window > Audio - the only entry (this screen has no dockable-panel system to list panels
	// for). Toggles AudioPanel's own visibility.
	UFUNCTION()
	void OnWindowAudioCheckChanged(bool bIsChecked);

	// --- Audio panel (Window > Audio) ---
	// Same Play/Stop All/mute-click-sound/file-list layout as UInteriorScreenWidget's own Audio
	// panel (reuses the same UAudioFileRowWidget class for rows, and the same PC-level
	// GetAvailableAudioFiles/PlayAudioFile/StopAllAudio/IsButtonClickSoundEnabled functions - none of
	// that is tied to the interior editor specifically), just a fixed Border with no drag/resize/
	// dock chrome, since this screen has no dock system to plug into. Starts hidden, same as the
	// interior editor's own Audio panel.
	void RefreshAudioList();

	UFUNCTION()
	void OnAudioStopAllClicked();

	UFUNCTION()
	void HandleAudioRowPlayClicked(FString FileName);

	UFUNCTION()
	void OnAudioClickSoundCheckChanged(bool bIsChecked);

	UPROPERTY()
	UButton* BackButton = nullptr;

	UPROPERTY()
	UButton* EnterInteriorButton = nullptr;

	UPROPERTY()
	UButton* FileMenuButton = nullptr;

	UPROPERTY()
	UButton* WindowMenuButton = nullptr;

	UPROPERTY()
	UBorder* FileMenuDropdown = nullptr;

	UPROPERTY()
	UBorder* WindowMenuDropdown = nullptr;

	// Full-screen, fully transparent, Collapsed by default - see
	// UInteriorScreenWidget::OnMenuClickCatcherMouseButtonDown's own comment for why this is how
	// "click elsewhere closes the dropdown" works in UMG.
	UPROPERTY()
	UBorder* MenuClickCatcher = nullptr;

	UPROPERTY()
	UCheckBox* WindowAudioCheckbox = nullptr;

	// Set by OnFileLoadClicked, read back by HandleFileLoadSave/Discard once the user picks a
	// choice - same role as UInteriorScreenWidget's own PendingLoadFilePath.
	FString PendingLoadFilePath;

	UPROPERTY()
	UBorder* AudioPanel = nullptr;

	UPROPERTY()
	UButton* AudioStopAllButton = nullptr;

	UPROPERTY()
	UCheckBox* AudioClickSoundCheckbox = nullptr;

	UPROPERTY()
	UScrollBox* AudioFileListBox = nullptr;

	// 15pt, matching the rest of the app's UI text (see UInteriorScreenWidget::StandardFontSize).
	static constexpr int32 StandardFontSize = 15;
	static void ApplyStandardFont(UTextBlock* TextBlock);
};
