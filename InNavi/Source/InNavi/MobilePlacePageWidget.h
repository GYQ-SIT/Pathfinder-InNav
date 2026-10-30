#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileNavSource.h"
#include "MobilePlacePageWidget.generated.h"

class UBorder;
class UButton;
class UHorizontalBox;
class UTextBlock;

// The bottom sheet's page for one place: its name, code and level, a photo (a "No photo" box when there is none), what kind of place it is and
// whether it is available, the description, Save place and Use as starting point, and Get directions pinned at the bottom. It shows what it is
// given and tells whoever made it what the person did; it keeps no state of its own about saved places or the starting point.
UCLASS()
class INNAVI_API UMobilePlacePageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// What the page shows: the place, and whether it is saved and is the starting point now.
	void SetPlace(const FMobilePlace& InPlace, bool bInSaved, bool bInIsStart);

	void SetOnBack(TFunction<void()> InCallback) { OnBack = MoveTemp(InCallback); }
	void SetOnDirections(TFunction<void(const FMobilePlace&)> InCallback) { OnDirections = MoveTemp(InCallback); }
	void SetOnSaveToggled(TFunction<void(const FMobilePlace&)> InCallback) { OnSaveToggled = MoveTemp(InCallback); }
	void SetOnUseAsStart(TFunction<void(const FMobilePlace&)> InCallback) { OnUseAsStart = MoveTemp(InCallback); }

	const FMobilePlace& GetPlace() const { return Place; }
	bool IsSaved() const { return bSaved; }
	bool IsStart() const { return bIsStart; }
	UWidget* FindNamedWidget(const FName& Name) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleBackClicked();
	UFUNCTION()
	void HandleDirectionsClicked();
	UFUNCTION()
	void HandleSaveClicked();
	UFUNCTION()
	void HandleStartClicked();

	void Refresh();

	UPROPERTY()
	UTextBlock* TitleText = nullptr;
	UPROPERTY()
	UTextBlock* SubtitleText = nullptr;
	UPROPERTY()
	UTextBlock* PhotoText = nullptr;
	UPROPERTY()
	UTextBlock* DescriptionText = nullptr;
	UPROPERTY()
	UHorizontalBox* PillRow = nullptr;
	UPROPERTY()
	UButton* SaveButton = nullptr;
	UPROPERTY()
	UTextBlock* SaveLabel = nullptr;
	UPROPERTY()
	UButton* StartButton = nullptr;
	UPROPERTY()
	UTextBlock* StartLabel = nullptr;

	FMobilePlace Place;
	bool bSaved = false;
	bool bIsStart = false;

	TFunction<void()> OnBack;
	TFunction<void(const FMobilePlace&)> OnDirections;
	TFunction<void(const FMobilePlace&)> OnSaveToggled;
	TFunction<void(const FMobilePlace&)> OnUseAsStart;
};
