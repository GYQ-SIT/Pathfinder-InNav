#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteriorTypes.h"
#include "ProjectLibrary.h"
#include "ProjectLibraryWidget.generated.h"

class UButton;
class UEditableTextBox;
class UProjectCardWidget;
class USizeBox;
class UTextBlock;
class UWrapBox;

// The project library the start screen's Open project card leads to: every project file in the Data folder as a card
// (see UProjectCardWidget), newest first, with a search box, a New project button and Back. It lists the folder itself,
// so there is no file dialog.
UCLASS()
class INNAVI_API UProjectLibraryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Looks at the folder again and rebuilds the cards (those matching the search).
	void Refresh();

	// What the card buttons and the header buttons do.
	void OpenProject(const FProjectLibraryEntry& Entry);
	void DuplicateProject(const FProjectLibraryEntry& Entry);
	void RenameProject(const FProjectLibraryEntry& Entry);
	void GoBack();

	// Tests.
	int32 GetShownCardCountForTests() const { return Cards.Num(); }
	const TArray<UProjectCardWidget*>& GetCardsForTests() const { return Cards; }
	UEditableTextBox* GetSearchBoxForTests() const { return SearchBox; }
	FText GetCountTextForTests() const;
	FText GetMessageTextForTests() const;
	void SetSearchForTests(const FString& Text);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION()
	void HandleSearchChanged(const FText& Text);
	UFUNCTION()
	void HandleNewClicked();
	UFUNCTION()
	void HandleBackClicked();
	UFUNCTION()
	void PlayButtonClickSound();
	// Failed loads are reported by the controller; this screen shows them under the header.
	UFUNCTION()
	void HandleActionFailed(FString Reason, EActionFailureSeverity Severity);

	void GoToEditScreen();
	void ShowMessage(const FString& Message);
	void RebuildCards();

	UPROPERTY()
	UEditableTextBox* SearchBox = nullptr;
	UPROPERTY()
	UTextBlock* CountText = nullptr;
	UPROPERTY()
	UTextBlock* MessageText = nullptr;
	UPROPERTY()
	UTextBlock* EmptyText = nullptr;
	UPROPERTY()
	UWrapBox* Grid = nullptr;
	// The page content: as wide as the window allows, up to 1580 design pixels, centred (see NativeTick).
	UPROPERTY()
	USizeBox* ContentLimit = nullptr;
	UPROPERTY()
	UButton* NewButton = nullptr;
	UPROPERTY()
	UButton* BackButton = nullptr;
	UPROPERTY()
	TArray<UProjectCardWidget*> Cards;

	TArray<FProjectLibraryEntry> Entries;
};
