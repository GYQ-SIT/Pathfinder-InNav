#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DuplicateToFloorsDialogWidget.generated.h"

class UButton;
class UCheckBox;

// The levels the user chose to copy onto. A native delegate: it carries a list, and an Actor does not run script events before the world has begun
// play, so a dynamic one could not be tested against the controller.
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDuplicateFloorsConfirmed, const TArray<int32>&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDuplicateFloorsCancelled);

// "Duplicate to Floors" (PAT-200): lists every floor of the building with a tick box, every floor but the one the rooms are on ticked by default;
// the floor the room is on is shown but cannot be ticked. Duplicate copies onto the ticked floors, Cancel closes without doing anything. Like the
// confirm dialog it is a widget of its own on the viewport (not part of a screen's canvas) and removes itself when a choice is made. It does not copy
// anything itself: the caller binds OnConfirmed and does the work.
UCLASS()
class INNAVI_API UDuplicateToFloorsDialogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Call before adding to the viewport. Levels are the floors of the building (lowest first), FromLevels the floors the rooms are on (shown,
	// disabled and unticked), Heading says what is being copied ("Server room" or "3 rooms").
	void Setup(const TArray<int32>& InLevels, const TArray<int32>& InFromLevels, const FString& InHeading);

	FOnDuplicateFloorsConfirmed OnConfirmed;

	UPROPERTY(BlueprintAssignable, Category = "Duplicate")
	FOnDuplicateFloorsCancelled OnCancelled;

	// The floors that are ticked now.
	TArray<int32> GetTickedLevels() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void OnDuplicateClicked();
	UFUNCTION()
	void OnCancelClicked();
	UFUNCTION()
	void HandleBoxChanged(bool bIsChecked);
	UFUNCTION()
	void PlayButtonClickSound();

	// Duplicate is only on while at least one floor is ticked.
	void RefreshDuplicateButton();

	TArray<int32> Levels;
	TArray<int32> FromLevels;
	FString Heading;

	UPROPERTY()
	TArray<UCheckBox*> FloorBoxes;
	UPROPERTY()
	UButton* DuplicateButton = nullptr;
	UPROPERTY()
	UButton* CancelButton = nullptr;
};
