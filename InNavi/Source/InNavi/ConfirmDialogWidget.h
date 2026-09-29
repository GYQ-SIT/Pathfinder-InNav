// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ConfirmDialogWidget.generated.h"

class UButton;

// Broadcast by whichever button the user clicks, no parameters - a caller binds to whichever
// ones it cares about (see NavPlayerController's HasUnsavedChanges()/DiscardUnsavedChanges()
// callers for what each choice should do).
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnConfirmDialogChoice);

// Pure-C++ replacement for WBP_ConfirmDialog (built the same way as StartScreenWidget/ws-jh's
// NavigationPanelWidget - RebuildWidget() constructs the layout in code). Reused from multiple
// places (interior screen's Back button, exterior edit screen's Back button, ...) whenever
// HasUnsavedChanges() is true - each caller creates one, binds to the three dispatchers below,
// and adds it to the viewport. This widget removes itself once a choice is made; it does not
// decide what "save"/"discard"/"cancel" actually do - that's each caller's own job.
UCLASS()
class INNAVI_API UConfirmDialogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "ConfirmDialog")
	FOnConfirmDialogChoice OnSaveAndLeave;

	UPROPERTY(BlueprintAssignable, Category = "ConfirmDialog")
	FOnConfirmDialogChoice OnDiscardAndLeave;

	UPROPERTY(BlueprintAssignable, Category = "ConfirmDialog")
	FOnConfirmDialogChoice OnCancel;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void OnSaveAndLeaveClicked();

	UFUNCTION()
	void OnDiscardAndLeaveClicked();

	UFUNCTION()
	void OnCancelClicked();

	// Bound alongside every button's own OnClicked so clicking anything plays the same click sound -
	// see ANavPlayerController::PlayButtonClickSound, which owns the actual caching/playback.
	UFUNCTION()
	void PlayButtonClickSound();

	UPROPERTY()
	UButton* SaveAndLeaveButton = nullptr;

	UPROPERTY()
	UButton* DiscardAndLeaveButton = nullptr;

	UPROPERTY()
	UButton* CancelButton = nullptr;
};
