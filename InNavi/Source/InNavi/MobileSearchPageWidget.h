#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileNavSource.h"
#include "MobileSearchPageWidget.generated.h"

class UBorder;
class UButton;
class UImage;
class UMobileSelectWidget;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

// The bottom sheet's first page, the directory of the site, as in the prototype's second iteration: the site's name (or the building's, when one is chosen)
// with a filters button, and under it the places grouped by "building . level". Finding a place by its name is the full-screen search of the map
// (UMobileSearchOverlayWidget), not a box here. The filters button opens three drop-downs - Category, Building (a campus only) and Level - and shows how many are
// on. Tapping a place hands it to whoever opened the page. It asks an IMobileNavSource for everything.
UCLASS()
class INNAVI_API UMobileSearchPageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// The source must outlive the page. The filters start again (all categories, buildings and levels).
	void SetSource(IMobileNavSource* InSource, const FString& InSiteId);

	// Called when a place is tapped.
	void SetOnPlaceChosen(TFunction<void(const FMobilePlace&)> InCallback) { OnPlaceChosen = MoveTemp(InCallback); }

	// Where the walk starts, when the person chose a place for it ("Use as starting point"): shown under the title, with a way to clear it. Empty = My location.
	void SetStartLabel(const FString& Label);
	void SetOnClearStart(TFunction<void()> InCallback) { OnClearStart = MoveTemp(InCallback); }
	FString GetStartLabel() const { return StartLabel; }
	bool IsStartRowShown() const { return StartLabel.Len() > 0; }
	void ClearStart() { if (OnClearStart) OnClearStart(); }

	// The filters: a category (empty = all), a building (empty = all; only a campus has more than one), a level (INDEX_NONE = all). Each lists again.
	void SetCategory(const FString& Category);
	void SetBuilding(const FString& BuildingId);
	void SetLevel(int32 Level);
	void ClearFilters();
	void SetFiltersOpen(bool bOpen);
	// The building the map shows (empty while the map shows the whole site): as in the prototype, the directory is of that building - its name is the title and
	// its places the list - unless the Building filter says another. Not a filter: it is not counted and ClearFilters leaves it.
	void SetContextBuilding(const FString& BuildingId);
	const FString& GetContextBuilding() const { return ContextBuildingId; }

	// What the list shows now, for the tests: the places in order, how many group headings, the title, and how many filters are on.
	const TArray<FMobilePlace>& GetShownPlaces() const { return ShownPlaces; }
	int32 GetGroupCount() const { return GroupCount; }
	bool AreFiltersOpen() const { return bFiltersOpen; }
	const FMobilePlaceFilter& GetFilter() const { return Filter; }
	int32 GetActiveFilterCount() const;
	// The site's name, or the name of the building the filter or the map has.
	FString GetTitle() const;
	UWidget* FindNamedWidget(const FName& Name) const;
	UMobileSelectWidget* GetCategorySelect() const { return CategorySelect; }
	UMobileSelectWidget* GetBuildingSelect() const { return BuildingSelect; }
	UMobileSelectWidget* GetLevelSelect() const { return LevelSelect; }
	// A tap on a place's row.
	void ChoosePlace(const FString& PlaceId);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleFiltersClicked();
	UFUNCTION()
	void HandleClearStartClicked();

	void Refresh();
	// The title, the button (blue and with the count when filters are on) and the three drop-downs with their options.
	void RefreshHeader();
	void CloseOtherSelects(UMobileSelectWidget* Keep);
	void AddGroupHeading(const FString& Label);
	void AddPlaceRow(const FMobilePlace& Place);

	UPROPERTY()
	UBorder* StartRow = nullptr;
	UPROPERTY()
	UTextBlock* StartText = nullptr;
	UPROPERTY()
	UTextBlock* TitleText = nullptr;
	UPROPERTY()
	UButton* FiltersButton = nullptr;
	// The button's picture (white, tinted), or its label when the icon's texture is not imported.
	UPROPERTY()
	UImage* FiltersIcon = nullptr;
	UPROPERTY()
	UTextBlock* FiltersLabel = nullptr;
	UPROPERTY()
	UBorder* FiltersBadge = nullptr;
	UPROPERTY()
	UTextBlock* FiltersBadgeText = nullptr;
	UPROPERTY()
	UVerticalBox* FilterPanel = nullptr;
	// How tall a filter list gets before it scrolls: five and a half rows of the small drop-down (the category list is five, and the half row
	// that shows under a longer list says that it goes on, as there is no scroll bar).
	static constexpr float FilterListHeight = 190.f;
	UPROPERTY()
	UMobileSelectWidget* CategorySelect = nullptr;
	UPROPERTY()
	UMobileSelectWidget* BuildingSelect = nullptr;
	UPROPERTY()
	UMobileSelectWidget* LevelSelect = nullptr;
	UPROPERTY()
	UScrollBox* ListScroll = nullptr;
	UPROPERTY()
	UVerticalBox* ListBox = nullptr;

	IMobileNavSource* Source = nullptr;
	FString SiteId;
	FString StartLabel;
	FMobilePlaceFilter Filter;
	FString ContextBuildingId;
	bool bFiltersOpen = false;
	TArray<FMobilePlace> ShownPlaces;
	int32 GroupCount = 0;
	FString LastGroup;

	TFunction<void(const FMobilePlace&)> OnPlaceChosen;
	TFunction<void()> OnClearStart;
};
