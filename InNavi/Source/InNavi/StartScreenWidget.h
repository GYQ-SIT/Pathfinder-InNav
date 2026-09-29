// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StartScreenWidget.generated.h"

class UButton;

// Pure-C++ replacement for WBP_StartScreen (built the same way as ws/jh's NavigationPanelWidget -
// RebuildWidget() constructs the layout in code instead of the UMG Designer, so the whole screen
// lives in a text file git can diff/merge normally). "New" starts a blank project; "Open" browses
// for and loads an existing buildings.json. Both then hand off to the edit screen.
UCLASS()
class INNAVI_API UStartScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UStartScreenWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void OnNewClicked();

	UFUNCTION()
	void OnOpenClicked();

	// The actual browse-and-load, deferred a few frames out of OnOpenClicked - see OnOpenClicked's
	// own comment for why.
	void BrowseAndLoadBuildingData();

	// Bound alongside every button's own OnClicked so clicking anything plays the same click sound -
	// see ANavPlayerController::PlayButtonClickSound, which owns the actual caching/playback.
	UFUNCTION()
	void PlayButtonClickSound();

	// Spawns the edit screen and removes this widget - what both New and a successful Open do once
	// there's something to edit.
	void GoToEditScreen();

	UPROPERTY()
	UButton* NewButton = nullptr;

	UPROPERTY()
	UButton* OpenButton = nullptr;
};
