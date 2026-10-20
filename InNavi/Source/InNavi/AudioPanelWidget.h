#pragma once

#include "CoreMinimal.h"
#include "DockPanelContent.h"
#include "AudioPanelWidget.generated.h"

class UButton;
class UCheckBox;
class UScrollBox;
class USlider;

// The Audio dock panel, shared by the interior editor and the exterior screen: Stop All, the per-sound mute check
// boxes, Mute All, the master volume, and one row per file under Data/Audio/ with a Play button.
UCLASS()
class INNAVI_API UAudioPanelWidget : public UDockPanelContentWidget
{
	GENERATED_BODY()

public:
	// The width the controls are sized to (the dock group's width minus the panel padding). Rebuilds the file rows
	// when it changed, because each row has the width baked in.
	void SetContentWidth(float Width);

	// Re-reads the file list (a file may have been added to Data/Audio/ while the panel was hidden).
	void RefreshAudioList();

	// Shows the controller's real sound settings (they live on the controller, so they survive the screen being
	// torn down and rebuilt) and refreshes the file list.
	void Refresh();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void OnStopAllClicked();
	UFUNCTION()
	void HandleRowPlayClicked(FString FileName);
	UFUNCTION()
	void OnClickSoundCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnSelectSoundCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnQuitConfirmSoundCheckChanged(bool bIsChecked);
	// Checked means everything is muted (the opposite sense of the three check boxes above); the three follow.
	UFUNCTION()
	void OnMuteAllCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnVolumeChanged(float NewValue);

	UPROPERTY()
	UButton* StopAllButton = nullptr;
	UPROPERTY()
	UCheckBox* ClickSoundCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* SelectSoundCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* QuitConfirmSoundCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* MuteAllCheckbox = nullptr;
	UPROPERTY()
	USlider* VolumeSlider = nullptr;
	UPROPERTY()
	UScrollBox* FileListBox = nullptr;

	float ContentWidth = 200.f;
};
