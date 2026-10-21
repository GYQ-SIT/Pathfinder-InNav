#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileNavSource.h"
#include "MobileSearchPageWidget.generated.h"

class UBorder;
class UEditableTextBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UWrapBox;

// The bottom sheet's "Where to?" page: a search box, filters, and the places grouped by "building . level". With nothing typed and no
// filter it lists the places of the building and level the person is in, with a Browse all places button; a search or a filter lists
// every match. Tapping a place hands it to whoever opened the page. It asks an IMobileNavSource for everything.
UCLASS()
class INNAVI_API UMobileSearchPageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// The source must outlive the page. The place the person is at (the list shown first) is Building's level Level.
	void SetSource(IMobileNavSource* InSource, const FString& InSiteId, const FString& InContextBuildingId, int32 InContextLevel);

	// Called when a place is tapped, and when the back arrow is.
	void SetOnPlaceChosen(TFunction<void(const FMobilePlace&)> InCallback) { OnPlaceChosen = MoveTemp(InCallback); }
	void SetOnBack(TFunction<void()> InCallback) { OnBack = MoveTemp(InCallback); }

	// What is typed, what is filtered, and what is listed; each of them rebuilds the list.
	void SetQuery(const FString& NewQuery);
	void ToggleCategory(const FString& Category);
	void ToggleAvailableOnly();
	void ClearFilters();
	void SetFiltersOpen(bool bOpen);
	void BrowseAll();

	// What the list shows now, for the tests: the places in order, and how many group headings.
	const TArray<FMobilePlace>& GetShownPlaces() const { return ShownPlaces; }
	int32 GetGroupCount() const { return GroupCount; }
	bool IsBrowsingAll() const { return bBrowseAll; }
	bool AreFiltersOpen() const { return bFiltersOpen; }
	const FMobilePlaceFilter& GetFilter() const { return Filter; }
	const FString& GetQuery() const { return Query; }
	UWidget* FindNamedWidget(const FName& Name) const;
	// A tap on a place's row.
	void ChoosePlace(const FString& PlaceId);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleQueryChanged(const FText& Text);
	UFUNCTION()
	void HandleBackClicked();
	UFUNCTION()
	void HandleFiltersClicked();
	UFUNCTION()
	void HandleBrowseAllClicked();

	void Refresh();
	void RefreshFilterChips();
	void AddGroupHeading(const FString& Label);
	void AddPlaceRow(const FMobilePlace& Place);

	UPROPERTY()
	UEditableTextBox* SearchBox = nullptr;
	UPROPERTY()
	UVerticalBox* FilterPanel = nullptr;
	UPROPERTY()
	UWrapBox* ChipBox = nullptr;
	UPROPERTY()
	UTextBlock* FiltersLabel = nullptr;
	UPROPERTY()
	UScrollBox* ListScroll = nullptr;
	UPROPERTY()
	UVerticalBox* ListBox = nullptr;

	IMobileNavSource* Source = nullptr;
	FString SiteId;
	FString ContextBuildingId;
	int32 ContextLevel = 1;
	FString Query;
	FMobilePlaceFilter Filter;
	bool bFiltersOpen = false;
	bool bBrowseAll = false;
	TArray<FMobilePlace> ShownPlaces;
	int32 GroupCount = 0;
	FString LastGroup;

	TFunction<void(const FMobilePlace&)> OnPlaceChosen;
	TFunction<void()> OnBack;
};
