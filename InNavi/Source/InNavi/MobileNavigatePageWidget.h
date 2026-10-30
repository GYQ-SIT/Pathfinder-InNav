#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileNavSource.h"
#include "MobileNavigatePageWidget.generated.h"

class UTextBlock;
class UButton;

// The bottom sheet while walking, as in the prototype: where to, Exit, the instruction for the step the person is on, "Step 2 of 3", how many
// metres are left, and Next instruction. It stays low so the map has room. It shows the step it is given; the screen moves on to the next one
// (on a real phone, as the person walks).
UCLASS()
class INNAVI_API UMobileNavigatePageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// The place walked to, the route, and which step (0-based) the person is on.
	void SetNavigation(const FMobilePlace& InDestination, const FMobileRoute& InRoute, int32 InStepIndex);

	void SetOnExit(TFunction<void()> InCallback) { OnExit = MoveTemp(InCallback); }
	void SetOnNext(TFunction<void()> InCallback) { OnNext = MoveTemp(InCallback); }

	// What the page says now.
	FString GetDestinationString() const;
	FString GetInstructionString() const;
	FString GetProgressString() const;
	FString GetRemainingString() const;
	FString GetNextLabelString() const;
	UWidget* FindNamedWidget(const FName& Name) const;

	// The metres still to walk from a step on (that step's own distance and every later one's).
	static double MetresRemaining(const FMobileRoute& Route, int32 StepIndex);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleExitClicked();
	UFUNCTION()
	void HandleNextClicked();

	void Refresh();

	UPROPERTY()
	UTextBlock* DestinationText = nullptr;
	UPROPERTY()
	UTextBlock* InstructionText = nullptr;
	UPROPERTY()
	UTextBlock* LevelText = nullptr;
	UPROPERTY()
	UTextBlock* ProgressText = nullptr;
	UPROPERTY()
	UTextBlock* RemainingText = nullptr;
	UPROPERTY()
	UTextBlock* NextLabel = nullptr;

	FMobilePlace Destination;
	FMobileRoute Route;
	int32 StepIndex = 0;

	TFunction<void()> OnExit;
	TFunction<void()> OnNext;
};
