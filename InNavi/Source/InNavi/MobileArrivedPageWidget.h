#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileNavSource.h"
#include "MobileArrivedPageWidget.generated.h"

class UButton;
class UTextBlock;

// The bottom sheet when the person has arrived, as in the prototype: "You have arrived", the place with its code and level, Find another place,
// Save this place and View destination. It keeps no state: the screen says whether the place is saved and hears what the person chooses.
UCLASS()
class INNAVI_API UMobileArrivedPageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPlace(const FMobilePlace& InPlace, bool bInSaved);

	void SetOnFindAnother(TFunction<void()> InCallback) { OnFindAnother = MoveTemp(InCallback); }
	void SetOnSaveToggled(TFunction<void(const FMobilePlace&)> InCallback) { OnSaveToggled = MoveTemp(InCallback); }
	void SetOnViewDestination(TFunction<void()> InCallback) { OnViewDestination = MoveTemp(InCallback); }

	FString GetPlaceString() const;
	FString GetSaveLabelString() const;
	bool IsSaved() const { return bSaved; }
	UWidget* FindNamedWidget(const FName& Name) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleFindAnotherClicked();
	UFUNCTION()
	void HandleSaveClicked();
	UFUNCTION()
	void HandleViewClicked();

	void Refresh();

	UPROPERTY()
	UTextBlock* PlaceText = nullptr;
	UPROPERTY()
	UButton* SaveButton = nullptr;
	UPROPERTY()
	UTextBlock* SaveLabel = nullptr;

	FMobilePlace Place;
	bool bSaved = false;

	TFunction<void()> OnFindAnother;
	TFunction<void(const FMobilePlace&)> OnSaveToggled;
	TFunction<void()> OnViewDestination;
};
