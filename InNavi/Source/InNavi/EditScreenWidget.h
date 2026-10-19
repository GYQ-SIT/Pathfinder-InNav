// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteriorTypes.h"
#include "EditScreenWidget.generated.h"

class UBorder;
class UButton;
class UCheckBox;
class UDockHostWidget;
class UEditableTextBox;
class UScrollBox;
class USlider;
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
// Also has its own top menu bar (File: New/Load; Window: one entry per dockable panel plus Reset
// Layout) and three dockable panels - Audio, Map and Debug - laid out by the same dock system as the
// interior editor (DockLayoutModel / DockHostWidget / DockHostUMG): tab groups, splits, edge areas,
// drag-to-dock, floating windows, and a saved layout (ExteriorDockLayout.h). This class only builds
// each panel's content, registers it with DockHost, and reacts to layout changes. Enter Interior/Back
// live in this menu bar's own right side (Enter Interior, then Back) instead of the old standalone
// bottom-right button stack - do NOT confuse this Back (menu-bar chrome, returns to the start screen)
// with UInteriorScreenWidget's own Back (returns to THIS screen instead) - they're different buttons
// with different targets despite matching chrome/position.
UCLASS()
class INNAVI_API UEditScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UEditScreenWidget(const FObjectInitializer& ObjectInitializer);

	// The dock system hosting the Audio/Map/Debug panels (null until the widget tree is built).
	UDockHostWidget* GetDockHost() const { return DockHost; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnEnterInteriorClicked();

	// Calls PC->Increase/DecreaseCameraSpeedExterior() - the exterior free-fly camera's default speed
	// auto-scales to the loaded campus's span (see FocusCameraOnAllBuildings), but these let the user
	// tune it further either way without waiting for a reload.
	UFUNCTION()
	void OnIncreaseCameraSpeedClicked();

	UFUNCTION()
	void OnDecreaseCameraSpeedClicked();

	// Calls PC->ResetExteriorCameraToDefaultView() - snaps back to whatever the current default view
	// is (the shipped one, or a saved override - see OnSetDefaultViewClicked).
	UFUNCTION()
	void OnResetCameraViewClicked();

	// Calls PC->SetCurrentViewAsDefaultExterior() - saves the free-fly camera's current pose as the
	// new default (persisted, see ANavPlayerController::SaveExteriorCameraDefaultToDisk), so Reset and
	// every future New/Open land here from now on.
	UFUNCTION()
	void OnSetDefaultViewClicked();

	// Lets the speed be typed directly instead of only nudged by +/- - fires on both Enter and
	// clicking away (ETextCommit::OnEnter / OnUserMovedFocus), matching how a plain number field is
	// expected to behave. Re-reads the actual (possibly clamped) speed back into the box afterward via
	// RefreshCameraSpeedDisplay, so an out-of-range value the user typed doesn't linger displayed.
	UFUNCTION()
	void OnCameraSpeedTextCommitted(const FText& NewText, ETextCommit::Type CommitType);

	// Syncs CameraSpeedTextBox to PC->GetCameraMoveSpeed() - called after construction and after
	// anything that can change the speed (+/-, typing a new value) so the box never shows a stale
	// number.
	void RefreshCameraSpeedDisplay();

	// Bound alongside every button's own OnClicked so clicking anything plays the same click sound -
	// see ANavPlayerController::PlayButtonClickSound, which owns the actual caching/playback.
	UFUNCTION()
	void PlayButtonClickSound();

	// Bound to ANavPlayerController::OnBuildingSelected - reveals the Enter Interior button once
	// there's actually a building selected to enter.
	UFUNCTION()
	void HandleBuildingSelected(FString BuildingId);

	// Bound to ANavPlayerController::OnActionFailed - remembers the last failure so the Debug panel can
	// show it (File > New/Load and "Set Default" failures used to be completely silent on this screen).
	UFUNCTION()
	void HandleActionFailed(FString Reason, EActionFailureSeverity Severity);

	void GoToStartScreen();

	UFUNCTION()
	void HandleSaveAndLeave();

	UFUNCTION()
	void HandleDiscardAndLeave();

	UFUNCTION()
	void HandleCancel();

	// --- Top menu bar: File (New/Load) + Window (panel visibility, Reset Layout) ---
	// Mirrors UInteriorScreenWidget's own menu bar layout/behavior exactly (own dropdown per menu
	// button, MenuClickCatcher for click-elsewhere-closes-it) - see that class's own header comments
	// for the full reasoning, not repeated here.

	// Save icon button, just left of File - calls PC->SaveCurrentProject() directly (same as Ctrl+S),
	// no confirm dialog needed since saving is never destructive.
	UFUNCTION()
	void OnSaveClicked();

	UFUNCTION()
	void OnFileMenuClicked();

	UFUNCTION()
	void OnWindowMenuClicked();

	// Camera menu, after Window: Reset, the typed move speed, +, -, Set Default View. Same dropdown idea as
	// File/Window (and the same menu the interior editor has while in 3D preview).
	UFUNCTION()
	void OnCameraMenuClicked();

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

	// Window > Audio / Map / Debug - show or hide that panel in the dock (it comes back where it was).
	// Map is the panel behind OnWindowRoadsCheckChanged/RoadFilterPanel: labeled "Map" (not "Roads")
	// since it also filters buildings, not just roads/POIs - the variable/function names underneath
	// keep their original "Road..." names (cosmetic rename only, not worth the churn).
	UFUNCTION()
	void OnWindowAudioCheckChanged(bool bIsChecked);

	UFUNCTION()
	void OnWindowRoadsCheckChanged(bool bIsChecked);

	UFUNCTION()
	void OnWindowDebugCheckChanged(bool bIsChecked);

	// Window > Reset Layout - back to the team default arrangement.
	UFUNCTION()
	void OnWindowResetLayoutClicked();

	// Master switch, above the 6 category rows - forwards to PC->SetAllRoadsVisible and visually syncs
	// the 6 category checkboxes to match (same pattern as OnAudioMuteAllCheckChanged).
	UFUNCTION()
	void OnRoadHideAllCheckChanged(bool bIsChecked);

	// --- Map panel --- one handler per ERoadCategory, since UCheckBox::OnCheckStateChanged only
	// carries the new bool, not which row it came from. Each forwards to PC->SetRoadCategoryVisible
	// with its own hardcoded category.
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

	// Region filter, below the 6 category checkboxes - forwards to PC->SetRoadsAndPOIsWithinCampusOnly.
	// Starts checked (see NativeConstruct, which applies it once a PC/Generator exists).
	UFUNCTION()
	void OnRoadCampusOnlyCheckChanged(bool bIsChecked);

	// Building-visibility filter (Map panel's "Buildings" section) - one handler per bucket, same
	// reasoning as the 6 road-category handlers above.
	UFUNCTION()
	void OnBuildingCampusCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnBuildingNonCampusCheckChanged(bool bIsChecked);

	// --- Docking ---
	// Called once from NativeConstruct: loads the user's saved layout (else the team default) and hooks the
	// dock's change notifications.
	void InitializeDockLayout();
	bool IsDockPanelShown(FName Panel) const;
	void SetDockPanelHidden(FName Panel, bool bHidden);
	void HandleDockPanelClosed(FName Panel);
	void HandleDockLayoutChanged();
	// Ticks the Window menu's checkboxes to match which panels are actually shown (a tab's X, a re-dock,
	// Reset Layout... can all change that behind the menu's back). SetIsChecked doesn't fire
	// OnCheckStateChanged, so this is safe to call from the handlers themselves.
	void RefreshWindowMenuChecks();
	void ResetDockLayout();
	// Persists the arrangement to Saved/ExteriorDockLayout.json - a per-machine UI preference.
	void SaveDockLayoutToDisk() const;
	// The Audio panel's controls size themselves from the width of the dock group showing it.
	void SyncAudioPanelWidth();

	// The actual rendered size of the root canvas - NOT UWidgetLayoutLibrary::GetViewportSize, which
	// returns the platform's raw pixel resolution and only matches this Slate-local coordinate space
	// at DPI scale 1 (see UInteriorScreenWidget::GetReliableCanvasSize's own comment for the same
	// reasoning). Returns false (leaving OutSize untouched) if RebuildWidget hasn't had a real Slate
	// paint pass yet.
	bool GetReliableCanvasSize(FVector2D& OutSize) const;
	FVector2D GetCanvasSize() const;

	// --- Audio panel (Window > Audio) ---
	// Same Play/Stop All/mute-click-sound/file-list layout as UInteriorScreenWidget's own Audio
	// panel (reuses the same UAudioFileRowWidget class for rows, and the same PC-level
	// GetAvailableAudioFiles/PlayAudioFile/StopAllAudio/IsButtonClickSoundEnabled functions - none of
	// that is tied to the interior editor specifically). Starts hidden, same as the interior editor's own
	// Audio panel.
	void RefreshAudioList();

	UFUNCTION()
	void OnAudioStopAllClicked();

	UFUNCTION()
	void HandleAudioRowPlayClicked(FString FileName);

	UFUNCTION()
	void OnAudioClickSoundCheckChanged(bool bIsChecked);

	// Same idea as OnAudioClickSoundCheckChanged, one per remaining UI sound.
	UFUNCTION()
	void OnAudioSelectSoundCheckChanged(bool bIsChecked);

	UFUNCTION()
	void OnAudioQuitConfirmSoundCheckChanged(bool bIsChecked);

	// Checked means everything's muted (opposite sense from the three "X Sound" checkboxes above) -
	// forwards to ANavPlayerController::SetAllUiSoundsEnabled(!bIsChecked) and syncs the three
	// individual checkboxes to match.
	UFUNCTION()
	void OnAudioMuteAllCheckChanged(bool bIsChecked);

	// Forwards to ANavPlayerController::SetMasterVolume.
	UFUNCTION()
	void OnAudioVolumeChanged(float NewValue);

	UPROPERTY()
	UButton* BackButton = nullptr;

	UPROPERTY()
	UButton* EnterInteriorButton = nullptr;

	UPROPERTY()
	UButton* ResetCameraViewButton = nullptr;

	UPROPERTY()
	UButton* DecreaseCameraSpeedButton = nullptr;

	UPROPERTY()
	UButton* IncreaseCameraSpeedButton = nullptr;

	UPROPERTY()
	UButton* SetDefaultViewButton = nullptr;

	UPROPERTY()
	UEditableTextBox* CameraSpeedTextBox = nullptr;

	UPROPERTY()
	UButton* SaveButton = nullptr;

	UPROPERTY()
	UButton* FileMenuButton = nullptr;

	UPROPERTY()
	UButton* WindowMenuButton = nullptr;

	UPROPERTY()
	UBorder* FileMenuDropdown = nullptr;

	UPROPERTY()
	UBorder* WindowMenuDropdown = nullptr;

	UPROPERTY()
	UButton* CameraMenuButton = nullptr;

	UPROPERTY()
	UBorder* CameraMenuDropdown = nullptr;

	UPROPERTY()
	UCheckBox* WindowAudioCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* WindowRoadsCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* WindowDebugCheckbox = nullptr;

	// The dock system's host: lays out the three panels below. Stretches over the whole canvas; only its
	// chrome takes mouse input, so clicks on the empty area still reach the 3D scene.
	UPROPERTY()
	UDockHostWidget* DockHost = nullptr;

	bool bDockLayoutInitialized = false;

	// The canvas size the layout was last known to be arranged for - kept so the layout can still be saved
	// while the widget is being torn down (its geometry is no longer reliable by then).
	FVector2D LastKnownCanvasSize = FVector2D::ZeroVector;

	// Content roots of the three dock panels (the UBorders registered with DockHost).
	UPROPERTY()
	UBorder* AudioPanel = nullptr;

	// Map panel - 6 road-category checkboxes + region filter + 2 building filters (see
	// OnRoadHideAllCheckChanged and friends).
	UPROPERTY()
	UBorder* RoadFilterPanel = nullptr;

	UPROPERTY()
	UBorder* DebugPanel = nullptr;

	// Master "Hide All" checkbox, above the 6 category rows.
	UPROPERTY()
	UCheckBox* RoadHideAllCheckbox = nullptr;

	// The 6 category checkboxes themselves - stored (not just local RebuildWidget variables) so
	// OnRoadHideAllCheckChanged can visually sync them later.
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

	// Region/distance filter's own checkbox - the 7th row in the Roads section (see
	// OnRoadCampusOnlyCheckChanged).
	UPROPERTY()
	UCheckBox* RoadCampusOnlyCheckbox = nullptr;

	// Building-visibility filter's own checkboxes ("Buildings" section).
	UPROPERTY()
	UCheckBox* BuildingCampusCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* BuildingNonCampusCheckbox = nullptr;

	// Full-screen, fully transparent, Collapsed by default - see
	// UInteriorScreenWidget::OnMenuClickCatcherMouseButtonDown's own comment for why this is how
	// "click elsewhere closes the dropdown" works in UMG.
	UPROPERTY()
	UBorder* MenuClickCatcher = nullptr;

	// Set by OnFileLoadClicked, read back by HandleFileLoadSave/Discard once the user picks a
	// choice - same role as UInteriorScreenWidget's own PendingLoadFilePath.
	FString PendingLoadFilePath;

	UPROPERTY()
	UButton* AudioStopAllButton = nullptr;

	UPROPERTY()
	UCheckBox* AudioClickSoundCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* AudioSelectSoundCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* AudioQuitConfirmSoundCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* AudioMuteAllCheckbox = nullptr;

	UPROPERTY()
	USlider* AudioVolumeSlider = nullptr;

	UPROPERTY()
	UScrollBox* AudioFileListBox = nullptr;

	// Width the Audio panel's controls (Stop All, file rows, volume slider) are currently sized to - follows
	// the dock group's real width, see SyncAudioPanelWidth.
	float AudioContentWidth = 220.f;

	// Debug panel: live info text plus a "Last Error" line coloured by severity.
	UPROPERTY()
	UTextBlock* DebugInfoText = nullptr;

	UPROPERTY()
	UTextBlock* DebugLastErrorText = nullptr;

	float DebugSmoothedDeltaTime = 0.f;

	// Last message/severity broadcast by ANavPlayerController::OnActionFailed (see HandleActionFailed);
	// empty until the first failure.
	FString LastActionFailedMessage;
	EActionFailureSeverity LastActionFailedSeverity = EActionFailureSeverity::Warning;

	// 15pt, matching the rest of the app's UI text (see UInteriorScreenWidget::StandardFontSize).
	static constexpr int32 StandardFontSize = 15;
	static void ApplyStandardFont(UTextBlock* TextBlock);
	static void ApplyStandardFont(UEditableTextBox* TextBox);
};
