// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AudioFileRowWidget.generated.h"

class UButton;
class USizeBox;
class UTextBlock;

// Broadcast with the filename this row represents - UButton::OnClicked itself carries no
// identifying data, so this is how UInteriorScreenWidget's Audio panel (see RefreshAudioList) finds
// out which row's Play button was clicked without needing one hand-written handler per file. Mirrors
// UFloorListEntryWidget::FOnFloorEntryClicked's own reasoning.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAudioRowPlayClicked, FString, FileName);

// One row in UInteriorScreenWidget's Audio panel file list - a filename label doubling as a "Play"
// button, tinted green while that file is currently playing (see
// ANavPlayerController::IsAudioFilePlaying). Built the same way as UFloorListEntryWidget
// (RebuildWidget()). Call SetFileName right after creating one, before adding it to the viewport/
// parent.
UCLASS()
class INNAVI_API UAudioFileRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UAudioFileRowWidget(const FObjectInitializer& ObjectInitializer);

	void SetFileName(const FString& InFileName, bool bInIsPlaying);

	// Same "not realized yet" caveat as SetFileName - safe to call before RebuildWidget has run.
	void SetWidth(float NewWidth);

	UPROPERTY(BlueprintAssignable, Category = "AudioFileRow")
	FOnAudioRowPlayClicked OnPlayClicked;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleClicked();

	// Bound alongside HandleClicked so clicking a row plays the same click sound every other button
	// in the app does, in addition to actually starting that file's own playback - see
	// ANavPlayerController::PlayButtonClickSound.
	UFUNCTION()
	void PlayButtonClickSound();

	// Applies FileName/bIsPlaying to Button/Label - called from RebuildWidget (so the very first
	// build already shows the right text/color) and again from SetFileName if the widget has already
	// been realized by then (same split as UFloorListEntryWidget::ApplyVisuals).
	void ApplyVisuals();

	UPROPERTY()
	UButton* Button = nullptr;

	UPROPERTY()
	UTextBlock* Label = nullptr;

	UPROPERTY()
	USizeBox* SizeBox = nullptr;

	FString FileName;
	bool bIsPlaying = false;
	float Width = 150.f;
};
