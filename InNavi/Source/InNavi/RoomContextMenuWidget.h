#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RoomContextMenuWidget.generated.h"

class UButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomContextMenuChoice);

// The menu that opens on a right-click on a room in the 2D plan (PAT-200): one row, "Duplicate to Floors...". A widget of its own on the viewport,
// like the confirm dialog, with a see-through layer behind it that closes the menu when anything else is clicked. It removes itself when a row is
// chosen or the menu is dismissed.
UCLASS()
class INNAVI_API URoomContextMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Where the menu's top-left corner goes, in viewport units. Call before adding to the viewport.
	void SetMenuPosition(FVector2D InPosition) { MenuPosition = InPosition; }

	UPROPERTY(BlueprintAssignable, Category = "ContextMenu")
	FOnRoomContextMenuChoice OnDuplicateToFloors;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void OnDuplicateClicked();
	UFUNCTION()
	void OnDismissClicked();
	UFUNCTION()
	void PlayButtonClickSound();

	FVector2D MenuPosition = FVector2D::ZeroVector;

	UPROPERTY()
	UButton* DuplicateButton = nullptr;
	UPROPERTY()
	UButton* DismissButton = nullptr;
};
