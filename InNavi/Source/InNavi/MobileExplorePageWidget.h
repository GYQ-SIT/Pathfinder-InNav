#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileNavSource.h"
#include "MobileExplorePageWidget.generated.h"

class UBorder;
class UTextBlock;
class UVerticalBox;

// The Explore tab, as in the prototype: the app's name, "Find your way indoors", and a card for each site (what it is called, what it is, how
// far, and how many buildings or levels). A tap on a card opens the site. When the data is not real, a pill says so and a line at the bottom
// says what is simulated. It asks an IMobileNavSource for the sites.
UCLASS()
class INNAVI_API UMobileExplorePageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetSource(IMobileNavSource* InSource);
	void SetOnSiteChosen(TFunction<void(const FString&)> InCallback) { OnSiteChosen = MoveTemp(InCallback); }

	// A tap on a site's card.
	void ChooseSite(const FString& SiteId);

	// What the cards say (the cards are inside tap targets, which have trees of their own).
	const TArray<FMobileSite>& GetShownSites() const { return ShownSites; }
	FString GetCountLabel(const FMobileSite& Site) const;
	bool IsDataNoteShown() const { return bDataNoteShown; }
	UWidget* FindNamedWidget(const FName& Name) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
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

	TFunction<void(const FString&)> OnSiteChosen;
};
