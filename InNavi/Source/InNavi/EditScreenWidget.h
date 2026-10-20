// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteriorTypes.h"
#include "EditScreenWidget.generated.h"

class UBorder;
class UButton;
class UAudioPanelWidget;
class UDebugPanelWidget;
class UMapPanelWidget;
class UBuildingInfoPanelWidget;
class UCheckBox;
class UDockHostWidget;
class UComboBoxString;
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

	// Finds a widget of this screen by name; GetWidgetFromName cannot see the dock panels' contents (they are
	// registered with DockHost, not parented in the widget tree). For tests and tooling.
	UWidget* FindScreenWidget(FName Name) const;
	UAudioPanelWidget* GetAudioPanelForTests() const { return AudioPanelWidget; }
	UDebugPanelWidget* GetDebugPanelForTests() const { return DebugPanelWidget; }
	UMapPanelWidget* GetMapPanelForTests() const { return MapPanelWidget; }
	UBuildingInfoPanelWidget* GetBuildingInfoPanelForTests() const { return BuildingInfoPanelWidget; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	UFUNCTION()
	void OnBackClicked();

	// Opens the selected building in the interior editor: the 2D floor editor, or (b3DView) straight into its 3D view.
	void OpenInteriorEditor(bool b3DView);

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
	void OnWindowBuildingCheckChanged(bool bIsChecked);

	UFUNCTION()
	void OnWindowDebugCheckChanged(bool bIsChecked);

	// Window > Reset Layout - back to the team default arrangement.
	UFUNCTION()
	void OnWindowResetLayoutClicked();

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
	// Same for the Map panel's building controls.
	void SyncMapPanelWidth();
	// And the Building panel.
	void SyncBuildingPanelWidth();

	// The actual rendered size of the root canvas - NOT UWidgetLayoutLibrary::GetViewportSize, which
	// returns the platform's raw pixel resolution and only matches this Slate-local coordinate space
	// at DPI scale 1 (see UInteriorScreenWidget::GetReliableCanvasSize's own comment for the same
	// reasoning). Returns false (leaving OutSize untouched) if RebuildWidget hasn't had a real Slate
	// paint pass yet.
	bool GetReliableCanvasSize(FVector2D& OutSize) const;
	FVector2D GetCanvasSize() const;

	UPROPERTY()
	UButton* BackButton = nullptr;

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
	UCheckBox* WindowBuildingCheckbox = nullptr;

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

	// The Audio dock panel (shared with the interior editor, see AudioPanelWidget.h). Starts hidden.
	UPROPERTY()
	UAudioPanelWidget* AudioPanelWidget = nullptr;

	// The Map dock panel (shared with the interior editor, see MapPanelWidget.h): road filters plus, here, the
	// building filters. Starts shown.
	UPROPERTY()
	UMapPanelWidget* MapPanelWidget = nullptr;

	// The Building dock panel (BuildingInfoPanelWidget.h): the selected building and the ways into it. Starts shown.
	UPROPERTY()
	UBuildingInfoPanelWidget* BuildingInfoPanelWidget = nullptr;

	// The Debug dock panel (shared with the interior editor, see DebugPanelWidget.h). Starts hidden.
	UPROPERTY()
	UDebugPanelWidget* DebugPanelWidget = nullptr;

	// Full-screen, fully transparent, Collapsed by default - see
	// UInteriorScreenWidget::OnMenuClickCatcherMouseButtonDown's own comment for why this is how
	// "click elsewhere closes the dropdown" works in UMG.
	UPROPERTY()
	UBorder* MenuClickCatcher = nullptr;

	// Set by OnFileLoadClicked, read back by HandleFileLoadSave/Discard once the user picks a
	// choice - same role as UInteriorScreenWidget's own PendingLoadFilePath.
	FString PendingLoadFilePath;

	// 15pt, matching the rest of the app's UI text (see UInteriorScreenWidget::StandardFontSize).
	static constexpr int32 StandardFontSize = 15;
	static void ApplyStandardFont(UTextBlock* TextBlock);
	static void ApplyStandardFont(UEditableTextBox* TextBox);
};
