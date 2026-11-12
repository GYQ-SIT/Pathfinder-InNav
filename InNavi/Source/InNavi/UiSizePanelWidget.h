// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DockPanelContent.h"
#include "UiSizePanelWidget.generated.h"

class UButton;
class USizeBox;
class UTextBlock;
class UUiSizeSubsystem;

// The UI Size dock panel, shared by the interior editor and the exterior screen: smaller, the size now, larger, and back to the default. It
// makes the whole UI bigger or smaller at any resolution (for a presentation or a small screen), on top of the scale the window size gives; the
// state lives in UUiSizeSubsystem (see UiSize.h), so Ctrl + / Ctrl - / Ctrl 0 keep the panel up to date too. The Window menu only shows or hides
// the panel, like every other panel.
UCLASS()
class INNAVI_API UUiSizePanelWidget : public UDockPanelContentWidget
{
	GENERATED_BODY()

public:
	// The width the controls are sized to (the dock group's width minus the panel padding).
	void SetContentWidth(float Width);

	// The size the panel shows now, in percent (tests).
	int32 GetShownPercentForTests() const { return ShownPercent; }
	// Lets a test give the panel its own subsystem: a test world has no game instance.
	void SetSubsystemForTests(UUiSizeSubsystem* Subsystem) { SubsystemForTests = Subsystem; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION()
	void OnSmallerClicked();
	UFUNCTION()
	void OnLargerClicked();
	UFUNCTION()
	void OnResetClicked();

	// The subsystem that holds the size, or null where there is none (the phone, a test world).
	UUiSizeSubsystem* GetSubsystem() const;
	// Writes the size into the value text.
	void RefreshValue();

	UPROPERTY()
	UButton* SmallerButton = nullptr;
	UPROPERTY()
	UButton* LargerButton = nullptr;
	UPROPERTY()
	UButton* ResetButton = nullptr;
	UPROPERTY()
	UTextBlock* ValueText = nullptr;
	UPROPERTY()
	UTextBlock* HelpText = nullptr;
	// The size boxes SetContentWidth gives the panel's width to: the help text, the row of three controls, the reset button.
	UPROPERTY()
	USizeBox* HelpSizeBox = nullptr;
	UPROPERTY()
	USizeBox* RowSizeBox = nullptr;
	UPROPERTY()
	USizeBox* ResetSizeBox = nullptr;
	UPROPERTY()
	TObjectPtr<UUiSizeSubsystem> SubsystemForTests = nullptr;

	float ContentWidth = 200.f;
	int32 ShownPercent = -1;
};
