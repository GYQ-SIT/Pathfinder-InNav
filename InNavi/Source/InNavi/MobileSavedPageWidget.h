#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileNavSource.h"
#include "MobileSavedPageWidget.generated.h"

class UTextBlock;
class UVerticalBox;

// The Saved tab: the places the person saved, newest first, grouped by site. A tap on a place opens it; Remove takes it off the list. With none
// saved it says how to save one. It shows the ids it is given (the screen keeps the list) and asks an IMobileNavSource what they are.
UCLASS()
class INNAVI_API UMobileSavedPageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetSource(IMobileNavSource* InSource);
	// The saved places' ids, newest first. Unknown places can still be removed.
	void SetSaved(const TArray<FString>& InIds);

	void SetOnPlaceChosen(TFunction<void(const FMobilePlace&)> InCallback) { OnPlaceChosen = MoveTemp(InCallback); }
	void SetOnRemove(TFunction<void(const FString&)> InCallback) { OnRemove = MoveTemp(InCallback); }

	// A tap on a place's row, and on its Remove.
	void ChoosePlace(const FString& PlaceId);
	void RemovePlace(const FString& PlaceId);

	// What the list shows, for the tests.
	const TArray<FMobilePlace>& GetShownPlaces() const { return ShownPlaces; }
	int32 GetGroupCount() const { return GroupCount; }
	bool IsEmptyMessageShown() const { return bEmptyShown; }
	FString GetCountString() const;
	UWidget* FindNamedWidget(const FName& Name) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void Refresh();
	void AddRow(const FMobilePlace& Place);

	UPROPERTY()
	UTextBlock* CountText = nullptr;
	UPROPERTY()
	UVerticalBox* ListBox = nullptr;

	IMobileNavSource* Source = nullptr;
	TArray<FString> Ids;
	TArray<FMobilePlace> ShownPlaces;
	int32 GroupCount = 0;
	bool bEmptyShown = false;

	TFunction<void(const FMobilePlace&)> OnPlaceChosen;
	TFunction<void(const FString&)> OnRemove;
};
