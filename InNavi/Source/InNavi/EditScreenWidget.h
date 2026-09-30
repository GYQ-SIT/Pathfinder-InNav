// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EditScreenWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanelSlot;
class UCheckBox;
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
// Also has its own top menu bar (File: New/Load; Window: Audio panel visibility) and Audio panel,
// mirroring UInteriorScreenWidget's own - but this screen has no dockable-panel system at all, so
// the Audio panel here is just a single freely-draggable Border (see AudioDragHandle - same
// drag-by-a-small-corner-handle-only idea as UInteriorScreenWidget's dock panels, just without any
// resize/dock/snap/column chrome, since there's only ever the one panel here) and Window only ever
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
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

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

	// Save icon button, just left of File - calls PC->SaveCurrentProject() directly (same as Ctrl+S),
	// no confirm dialog needed since saving is never destructive.
	UFUNCTION()
	void OnSaveClicked();

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

	// Window > Map - toggles RoadFilterPanel's own visibility. Labeled "Map" (not "Roads") since the
	// panel now also filters buildings, not just roads/POIs - the variable/function names underneath
	// keep their original "Road..." names (cosmetic rename only, not worth the churn of renaming every
	// symbol that reads them).
	UFUNCTION()
	void OnWindowRoadsCheckChanged(bool bIsChecked);

	// Master switch, above the 6 category rows - forwards to PC->SetAllRoadsVisible and visually syncs
	// the 6 category checkboxes to match (same pattern as OnAudioMuteAllCheckChanged).
	UFUNCTION()
	void OnRoadHideAllCheckChanged(bool bIsChecked);

	// --- Map filter panel (Window > Map) --- one handler per ERoadCategory, since
	// UCheckBox::OnCheckStateChanged only carries the new bool, not which row it came from. Each
	// forwards to PC->SetRoadCategoryVisible with its own hardcoded category.
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
	// Starts unchecked (shows everything, matching today's behavior with no region filter at all).
	UFUNCTION()
	void OnRoadCampusOnlyCheckChanged(bool bIsChecked);

	// Building-visibility filter (Map panel's "Buildings" section) - one handler per bucket, same
	// reasoning as the 6 road-category handlers above. Both start checked (everything shown, matching
	// today's behavior before this filter existed).
	UFUNCTION()
	void OnBuildingCampusCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnBuildingNonCampusCheckChanged(bool bIsChecked);

	// RoadFilterPanel's own small corner drag handle - same idea as AudioDragHandle (reuses its
	// AudioDragHandleSize/AudioPanelPadding/AudioPanelDragMinY constants, generic enough for either
	// panel), just move-only - this panel's content never resizes, so there's no bottom-resize handle
	// to go with it.
	void RefreshRoadFilterDragHandle();

	// The actual rendered size of the root canvas - NOT UWidgetLayoutLibrary::GetViewportSize, which
	// returns the platform's raw pixel resolution and only matches this Slate-local coordinate space
	// at DPI scale 1 (see UInteriorScreenWidget::GetReliableCanvasSize's own comment for the same
	// reasoning). Returns false (leaving OutSize untouched) if RebuildWidget hasn't had a real Slate
	// paint pass yet - see PositionRoadFilterPanelInitially's own retry.
	bool GetReliableCanvasSize(FVector2D& OutSize) const;

	// Places RoadFilterPanel a fixed margin in from the canvas' own actual right edge (screen-size/
	// resolution independent, unlike a hardcoded X) - called every tick from NativeTick until it
	// succeeds once (both the canvas' and the panel's own CachedGeometry need a real paint pass
	// first, so this can't just run once at construction), then never again (the user's own drag
	// takes over from there - see bHasPositionedRoadFilterPanelInitially).
	void PositionRoadFilterPanelInitially();
	UFUNCTION()
	FEventReply OnRoadFilterDragHandleMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION()
	FEventReply OnRoadFilterPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION()
	FEventReply OnRoadFilterPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	// --- Audio panel (Window > Audio) ---
	// Same Play/Stop All/mute-click-sound/file-list layout as UInteriorScreenWidget's own Audio
	// panel (reuses the same UAudioFileRowWidget class for rows, and the same PC-level
	// GetAvailableAudioFiles/PlayAudioFile/StopAllAudio/IsButtonClickSoundEnabled functions - none of
	// that is tied to the interior editor specifically), a single freely-draggable Border (see
	// AudioDragHandle) with no resize/dock/snap/column chrome, since this screen only ever has the
	// one panel to plug into a dock system for. Starts hidden, same as the interior editor's own
	// Audio panel.
	void RefreshAudioList();

	// Sizes AudioDragHandleSlot off AudioTitleText's own rendered size (same reasoning as
	// UInteriorScreenWidget::GetDockDragHandleSize - GetCachedGeometry is the text's real painted
	// size, not a pre-paint estimate) and positions it at AudioPanel's current top-left corner, inset
	// by AudioPanelPadding to match where the title text itself starts. Called once right after
	// construction, again every tick for AudioDragHandleSettleFrameCount ticks after that (the text
	// isn't necessarily painted yet on the very first call - same class of staleness
	// UInteriorScreenWidget hits, see its own LayoutGeometrySettleFramesRemaining), and again on every
	// drag move (the panel's own position changed, not its size, but the handle must track it too).
	void RefreshAudioDragHandle();

	UFUNCTION()
	FEventReply OnAudioDragHandleMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	UFUNCTION()
	FEventReply OnAudioPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	UFUNCTION()
	FEventReply OnAudioPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	// Positions AudioBottomResizeHandleSlot at AudioPanel's current bottom edge (AudioPanelHeight if
	// the user has dragged it at least once, else the panel's own natural painted height) and spans
	// its current width. Called from the same trigger points as RefreshAudioDragHandle, plus every
	// bottom-drag mouse move.
	void RefreshAudioBottomResizeHandle();

	UFUNCTION()
	FEventReply OnAudioBottomResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	UFUNCTION()
	FEventReply OnAudioBottomResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	UFUNCTION()
	FEventReply OnAudioBottomResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

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
	UCheckBox* WindowRoadsCheckbox = nullptr;

	// Freely-draggable panel (see RoadFilterDragHandle), Visible by default (unlike AudioPanel) - just
	// 6 checkboxes, one per ERoadCategory, each toggling PC->SetRoadCategoryVisible.
	UPROPERTY()
	UBorder* RoadFilterPanel = nullptr;

	UPROPERTY()
	UCanvasPanelSlot* RoadFilterPanelSlot = nullptr;

	// RoadFilterPanel's own title UTextBlock - RefreshRoadFilterDragHandle sizes the drag handle off
	// its rendered size, same reasoning as AudioTitleText/RefreshAudioDragHandle.
	UPROPERTY()
	UTextBlock* RoadFilterTitleText = nullptr;

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

	UPROPERTY()
	UBorder* RoadFilterDragHandle = nullptr;

	UPROPERTY()
	UCanvasPanelSlot* RoadFilterDragHandleSlot = nullptr;

	bool bIsDraggingRoadFilterPanel = false;
	FVector2D RoadFilterPanelDragStartMouse = FVector2D::ZeroVector;
	FVector2D RoadFilterPanelDragStartPosition = FVector2D::ZeroVector;

	// See RefreshAudioDragHandle's own comment on why more than one retry is needed - a separate
	// counter from AudioDragHandleSettleFramesRemaining since the two panels' refreshes are
	// independent (sharing one counter would make one panel's visibility change reset the other's
	// in-progress retry).
	int32 RoadFilterDragHandleSettleFramesRemaining = 0;

	// See PositionRoadFilterPanelInitially - true once that one-time repositioning has actually run,
	// so NativeTick stops retrying (and doesn't fight the user's own drag afterward).
	bool bHasPositionedRoadFilterPanelInitially = false;

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

	// AudioPanel's own CanvasPanelSlot - promoted to a member (RebuildWidget's other slots stay local)
	// purely so the drag handlers can read/set its position without threading it through every call.
	UPROPERTY()
	UCanvasPanelSlot* AudioPanelSlot = nullptr;

	// AudioPanel's title UTextBlock - see RefreshAudioDragHandle's own comment.
	UPROPERTY()
	UTextBlock* AudioTitleText = nullptr;

	// The small move-drag handle box in AudioPanel's top-left corner - same idea as
	// UInteriorScreenWidget's own DockDragHandles (only this specific box starts a drag; clicking
	// anywhere else on the panel, like its checkboxes or file list, does not), just for the one panel
	// this screen has instead of a per-panel array.
	UPROPERTY()
	UBorder* AudioDragHandle = nullptr;

	UPROPERTY()
	UCanvasPanelSlot* AudioDragHandleSlot = nullptr;

	bool bIsDraggingAudioPanel = false;
	FVector2D AudioPanelDragStartMouse = FVector2D::ZeroVector;
	FVector2D AudioPanelDragStartPosition = FVector2D::ZeroVector;

	// See RefreshAudioDragHandle's own comment on why more than one retry is needed.
	int32 AudioDragHandleSettleFramesRemaining = 0;
	static constexpr int32 AudioDragHandleSettleFrameCount = 6;

	// Matches UInteriorScreenWidget::DockDragHandleSize - the floor each axis of the handle is
	// clamped to, so a not-yet-painted or empty title never shrinks it to something unclickable.
	static constexpr float AudioDragHandleSize = 12.f;

	// Matches AudioPanel's own SetPadding(FMargin(10.f)) in RebuildWidget - the handle is inset by
	// this same amount so it sits right where the title text itself starts, not some separate margin.
	static constexpr float AudioPanelPadding = 10.f;

	// Matches RebuildWidget's own local EditMenuBarHeight (kept as a separate class constant rather
	// than shared - see RebuildWidget's own comment on why EditMenuBarHeight is a local, not a class
	// member) - the drag clamp's own top bound, so the panel can't be dragged up under the menu bar.
	static constexpr float AudioPanelDragMinY = 32.f;

	// The small resize handle bar at AudioPanel's bottom edge - only this class' one panel needs it,
	// so unlike UInteriorScreenWidget's per-panel DockBottomResizeHandles array this is just a single
	// widget. Dragging it sets AudioPanelHeight to an explicit value via AudioPanelWrapper's own
	// SetHeightOverride (see its construction comment in RebuildWidget for why AudioPanel needs a
	// wrapping SizeBox at all).
	UPROPERTY()
	UBorder* AudioBottomResizeHandle = nullptr;

	UPROPERTY()
	UCanvasPanelSlot* AudioBottomResizeHandleSlot = nullptr;

	// 0 = never manually resized, keep sizing to natural content height - same "0 means auto" idiom as
	// UInteriorScreenWidget::DockPanelHeights.
	float AudioPanelHeight = 0.f;
	bool bIsResizingAudioPanelHeight = false;
	float AudioPanelResizeStartMouseY = 0.f;
	float AudioPanelResizeStartHeight = 0.f;

	// Floor for AudioPanelHeight - matches UInteriorScreenWidget::DockPanelMinHeight's role, just a
	// smaller value since this one panel's natural content is itself much shorter than a dock panel's.
	static constexpr float AudioPanelMinHeight = 60.f;

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

	// 15pt, matching the rest of the app's UI text (see UInteriorScreenWidget::StandardFontSize).
	static constexpr int32 StandardFontSize = 15;
	static void ApplyStandardFont(UTextBlock* TextBlock);
	static void ApplyStandardFont(UEditableTextBox* TextBox);
};
