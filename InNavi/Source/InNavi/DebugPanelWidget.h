#pragma once

#include "CoreMinimal.h"
#include "DockPanelContent.h"
#include "InteriorTypes.h"
#include "DebugPanelWidget.generated.h"

class UTextBlock;

// The Debug dock panel, shared by the interior editor and the exterior screen: a block of live information, then
// FPS and frame time, then a "Last Error" line for the most recent failed action (yellow: rejected but nothing was
// lost; red: data may have been lost - see EActionFailureSeverity). The screen supplies only the lines that are
// specific to it.
UCLASS()
class INNAVI_API UDebugPanelWidget : public UDockPanelContentWidget
{
	GENERATED_BODY()

public:
	// What the screen wants shown above the FPS lines: one "Label: value" per line, no trailing newline. Called every
	// tick while the panel is on screen.
	void SetInfoProvider(TFunction<FString()> InProvider) { InfoProvider = MoveTemp(InProvider); }

	// Starts listening for failed actions. Called once the screen has its player controller; safe to call again.
	void BindToController();

	// The text the panel would show right now (tests).
	FString GetInfoTextForTests() const;
	FString GetLastErrorTextForTests() const;
	FLinearColor GetLastErrorColorForTests() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	// Bound to ANavPlayerController::OnActionFailed, which covers every rejected action in the app but until it was
	// shown here only reached the Output Log (not visible in a packaged build).
	UFUNCTION()
	void HandleActionFailed(FString Reason, EActionFailureSeverity Severity);

	void RefreshTexts();

	UPROPERTY()
	UTextBlock* InfoText = nullptr;
	// Its own text block so it can carry its own colour.
	UPROPERTY()
	UTextBlock* LastErrorText = nullptr;

	TFunction<FString()> InfoProvider;
	float SmoothedDeltaTime = 0.f;
	// Empty until the first failure.
	FString LastActionFailedMessage;
	EActionFailureSeverity LastActionFailedSeverity = EActionFailureSeverity::Warning;
};
