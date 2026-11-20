#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteriorTypes.h"
#include "ProjectLibrary.h"
#include "ProjectLibraryWidget.generated.h"

class UButton;
class UEditableTextBox;
class UProjectCardWidget;
class UTexture2D;
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
	// The whole page is drawn this much bigger than it is laid out, so that its Back button is as big as the start screen's Quit button (120 x 44, label 17;
	// the page was laid out with 12-pixel labels): the fonts, the icons, the cards and the spaces between them all grow together.
	static constexpr float PageScale = 17.f / 12.f;

	// Looks at the folder again and rebuilds the cards (those matching the search).
	void Refresh();

	// What the card buttons and the header buttons do.
	void OpenProject(const FProjectLibraryEntry& Entry);
	void DuplicateProject(const FProjectLibraryEntry& Entry);
	void RenameProject(const FProjectLibraryEntry& Entry);
	// The picture area of a card: asks for a picture (the system's file dialog) and makes it the project's cover, or takes the cover away.
	void ChooseCover(const FProjectLibraryEntry& Entry);
	void SetCoverFromFile(const FProjectLibraryEntry& Entry, const FString& PicturePath);
	void RemoveCoverOf(const FProjectLibraryEntry& Entry);
	// Asks first, then moves the file into the Data/Deleted folder (see ProjectLibrary::SoftDelete).
	void DeleteProject(const FProjectLibraryEntry& Entry);
	void GoBack();
	// A click on this screen must not reach the 3D scene behind it (see UStartScreenWidget::BlockClicksToTheScene).
	void BlockClicksToTheScene();

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
	// The cover of an entry as a texture, read once and kept until the file changes (the cards are rebuilt on every key of the search).
	UTexture2D* CoverTextureFor(const FProjectLibraryEntry& Entry);

	UPROPERTY()
	TMap<FString, UTexture2D*> CoverTextures;

	UPROPERTY()
	UEditableTextBox* SearchBox = nullptr;
	UPROPERTY()
	UTextBlock* CountText = nullptr;
	UPROPERTY()
	UTextBlock* MessageText = nullptr;
	UPROPERTY()
	UTextBlock* EmptyText = nullptr;
	// The open folder over the words when there is nothing to show (null while its texture is not imported).
	UPROPERTY()
	UWidget* EmptyIconWidget = nullptr;
	UPROPERTY()
	UWrapBox* Grid = nullptr;
	// The page content: as wide as the window allows, up to 1580 design pixels, centred (see NativeTick).
	UPROPERTY()
	USizeBox* ContentLimit = nullptr;
	UPROPERTY()
	USizeBox* ContentInner = nullptr;
	UPROPERTY()
	UButton* NewButton = nullptr;
	UPROPERTY()
	UButton* BackButton = nullptr;
	UPROPERTY()
	TArray<UProjectCardWidget*> Cards;

	TArray<FProjectLibraryEntry> Entries;
};
