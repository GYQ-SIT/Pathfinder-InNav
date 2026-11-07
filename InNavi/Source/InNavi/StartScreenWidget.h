// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StartScreenWidget.generated.h"

class UButton;
class UTexture2D;

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

	// The whole screen is in front of the 3D scene (the buildings of the project that was open are still in it): a click on it must not reach
	// the scene, where it selected a building and played the building-select sound. Tells the controller that the pointer is over this screen
	// everywhere.
	void BlockClicksToTheScene();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void OnNewClicked();

	UFUNCTION()
	void OnOpenClicked();

	// Quits the whole application - only meaningful in a packaged build (the editor's own Stop
	// button/window close covers this during PIE testing, which is why it went unnoticed until now).
	UFUNCTION()
	void OnQuitClicked();

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

	UPROPERTY()
	UButton* QuitButton = nullptr;

	// The background is drawn in code like the design team's page: a colour gradient with a soft glow, and a faint
	// grid. They are made once here and kept alive by these pointers.
	UPROPERTY()
	UTexture2D* BackgroundGradientTexture = nullptr;
	UPROPERTY()
	UTexture2D* BackgroundGridTexture = nullptr;
};
