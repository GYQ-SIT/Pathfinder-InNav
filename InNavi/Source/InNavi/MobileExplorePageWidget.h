#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileNavSource.h"
#include "MobileExplorePageWidget.generated.h"

class UBorder;
class UTextBlock;
class UVerticalBox;

// The Explore tab, as in the prototype: "Explore", a search field for the sites, and a card for each site (what it is called, what it is, how
// far, and how many buildings or levels). A tap on a card opens the site; a tap on the search field asks the screen for the full-screen search
// (UMobileSearchOverlayWidget). When the data is not real, a pill says so and a line at the bottom says what is simulated. It asks an
// IMobileNavSource for the sites.
UCLASS()
class INNAVI_API UMobileExplorePageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetSource(IMobileNavSource* InSource);
	void SetOnSiteChosen(TFunction<void(const FString&)> InCallback) { OnSiteChosen = MoveTemp(InCallback); }
	// A tap on the search field.
	void SetOnSearchRequested(TFunction<void()> InCallback) { OnSearchRequested = MoveTemp(InCallback); }
	void RequestSearch();

	// A tap on a site's card.
	void ChooseSite(const FString& SiteId);

	// What the cards say (the cards are inside tap targets, which have trees of their own).
	const TArray<FMobileSite>& GetShownSites() const { return ShownSites; }
	FString GetCountLabel(const FMobileSite& Site) const;
	bool IsDataNoteShown() const { return bDataNoteShown; }
	// How many cards show a cover picture instead of the placeholder drawing.
	int32 GetCoverCountForTests() const { return CoversShown; }
	UWidget* FindNamedWidget(const FName& Name) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleSearchClicked();
	void Refresh();
	void AddCard(const FMobileSite& Site);

	UPROPERTY()
	UVerticalBox* CardList = nullptr;
	UPROPERTY()
	UBorder* NotePill = nullptr;
	UPROPERTY()
	UTextBlock* NoteText = nullptr;

	IMobileNavSource* Source = nullptr;
	TArray<FMobileSite> ShownSites;
	bool bDataNoteShown = false;
	int32 CoversShown = 0;

	TFunction<void(const FString&)> OnSiteChosen;
	TFunction<void()> OnSearchRequested;
};
