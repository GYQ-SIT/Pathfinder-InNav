#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileNavSource.h"
#include "MobileSearchOverlayWidget.generated.h"

class FMobileSearchHistory;
class UBorder;
class UButton;
class UEditableTextBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

// The phone app's full-screen search, as in the prototype's second iteration: the search field on top, and under it either the recent searches
// (nothing typed yet) or what matches what is typed. It covers the whole phone screen, the header and the tabs too, until it is closed. One widget,
// two uses:
//  - Places (the search on the map): buildings and places of the site. Recent searches are the ones picked before; a tap picks one and closes.
//  - Sites (the search on the Explore tab): the sites. Recent searches are the words typed before (Enter keeps them); a tap on one types it again.
// It asks an IMobileNavSource for everything and keeps what was searched in an FMobileSearchHistory (the app's, or none).
UCLASS()
class INNAVI_API UMobileSearchOverlayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	enum class EMode : uint8 { Places, Sites };

	// Most rows a list shows (a campus has thousands of places): past it, a line says to type more.
	static constexpr int32 MaxRows = 50;

	// The source must outlive the widget. SiteId is the site the Places search looks in.
	void SetSource(IMobileNavSource* InSource, const FString& InSiteId);
	// Where the recent searches are kept; the history must outlive the widget. None = nothing is remembered and the lists of recent searches are empty.
	void SetHistory(FMobileSearchHistory* InHistory);

	// What a tap on a row does (and the field's close button).
	void SetOnBuildingChosen(TFunction<void(const FString&)> InCallback) { OnBuildingChosen = MoveTemp(InCallback); }
	void SetOnPlaceChosen(TFunction<void(const FMobilePlace&)> InCallback) { OnPlaceChosen = MoveTemp(InCallback); }
	void SetOnSiteChosen(TFunction<void(const FString&)> InCallback) { OnSiteChosen = MoveTemp(InCallback); }
	void SetOnClosed(TFunction<void()> InCallback) { OnClosed = MoveTemp(InCallback); }

	// Shows the search for a kind of thing, with an empty field and the keyboard in it. Close hides it again (nothing typed is kept).
	void Open(EMode InMode);
	void Close();
	bool IsOpen() const { return bOpen; }
	EMode GetMode() const { return Mode; }

	// What is typed; every change lists again.
	void SetQuery(const FString& NewQuery);
	const FString& GetQuery() const { return Query; }
	// Enter on the field: in the Sites search the words are kept as a recent search; the Places search keeps what is picked, not what is typed.
	void Submit();
	// "Clear history": forgets the recent searches of this kind.
	void ClearHistory();
	// A tap on a row, by its key (the same as GetShownKeys()): "building:E2", "place:E2-01-101", "site:sit-punggol", "query:punggol".
	void ChooseRow(const FString& Key);

	// What the list shows now, for the tests: the heading, the keys of the rows in order, the line shown instead of rows (empty when there are rows),
	// and whether the "Clear history" button is there.
	const FString& GetHeading() const { return Heading; }
	const TArray<FString>& GetShownKeys() const { return ShownKeys; }
	const FString& GetEmptyText() const { return EmptyText; }
	bool IsClearHistoryShown() const { return bClearShown; }
	bool IsShowingHistory() const { return Query.TrimStartAndEnd().IsEmpty(); }
	UWidget* FindNamedWidget(const FName& Name) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleQueryChanged(const FText& Text);
	UFUNCTION()
	void HandleQueryCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION()
	void HandleCloseClicked();
	UFUNCTION()
	void HandleClearClicked();

	void Refresh();
	void AddRow(const FString& Key, const FString& Title, const FString& Subtitle, bool bClockIcon);
	void AddHeading(const FString& Text, bool bWithClear);
	void AddNote(const FString& Text);
	// The line under a place's name: "E2-01-101 . E2 . L1".
	static FString PlaceSubtitle(const FMobilePlace& Place);

	UPROPERTY()
	UEditableTextBox* SearchBox = nullptr;
	UPROPERTY()
	UVerticalBox* ListBox = nullptr;
	UPROPERTY()
	UScrollBox* ListScroll = nullptr;

	IMobileNavSource* Source = nullptr;
	FMobileSearchHistory* History = nullptr;
	FString SiteId;
	EMode Mode = EMode::Places;
	bool bOpen = false;
	FString Query;
	FString Heading;
	FString EmptyText;
	bool bClearShown = false;
	TArray<FString> ShownKeys;

	TFunction<void(const FString&)> OnBuildingChosen;
	TFunction<void(const FMobilePlace&)> OnPlaceChosen;
	TFunction<void(const FString&)> OnSiteChosen;
	TFunction<void()> OnClosed;
};
