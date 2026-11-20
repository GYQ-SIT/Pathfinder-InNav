#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProjectLibrary.h"
#include "Components/Widget.h"
#include "ProjectCardWidget.generated.h"

class UBorder;
class UButton;
class UImage;
class UTexture2D;

// One project in the project library: a picture area with the number of buildings, the project's name, file name and
// description, when it was last written, and Open / Duplicate / Rename / Delete. The card does nothing itself - whoever shows it
// sets what the buttons do.
//
// The picture area is a button too: a click asks for a cover picture (OnChangeCover), which then replaces the card's drawing; a small cross at
// its corner takes the cover away (OnRemoveCover). The texture of the cover is given by whoever shows the card (SetCoverTexture), before it is shown.
UCLASS()
class INNAVI_API UProjectCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Has to be called before the card is shown.
	void SetEntry(const FProjectLibraryEntry& InEntry) { Entry = InEntry; }
	void SetCoverTexture(UTexture2D* InTexture) { CoverTexture = InTexture; }
	const FProjectLibraryEntry& GetEntry() const { return Entry; }

	TFunction<void(const FProjectLibraryEntry&)> OnOpen;
	TFunction<void(const FProjectLibraryEntry&)> OnDuplicate;
	TFunction<void(const FProjectLibraryEntry&)> OnRename;
	TFunction<void(const FProjectLibraryEntry&)> OnDelete;
	TFunction<void(const FProjectLibraryEntry&)> OnChangeCover;
	TFunction<void(const FProjectLibraryEntry&)> OnRemoveCover;

	UButton* GetOpenButtonForTests() const { return OpenButton; }
	UButton* GetDuplicateButtonForTests() const { return DuplicateButton; }
	UButton* GetRenameButtonForTests() const { return RenameButton; }
	UButton* GetDeleteButtonForTests() const { return DeleteButton; }
	UButton* GetPictureButtonForTests() const { return PictureButton; }
	UButton* GetRemoveCoverButtonForTests() const { return RemoveCoverButton; }
	bool ShowsCoverForTests() const { return CoverImage != nullptr; }
	bool ShowsDrawingForTests() const { return DrawingIcon != nullptr; }
	// Whether the hint over the picture ("Click to change the cover") is showing, as it does while the pointer is over the picture.
	bool ShowsHoverHintForTests() const;
	void HoverPictureForTests(bool bHovered) { SetHoverHint(bHovered); }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void HandleOpenClicked();
	UFUNCTION()
	void HandleDuplicateClicked();
	UFUNCTION()
	void HandleRenameClicked();
	UFUNCTION()
	void HandleDeleteClicked();
	UFUNCTION()
	void PlayButtonClickSound();
	UFUNCTION()
	void HandlePictureClicked();
	UFUNCTION()
	void HandleRemoveCoverClicked();
	UFUNCTION()
	void HandlePictureHovered();
	UFUNCTION()
	void HandlePictureUnhovered();
	void SetHoverHint(bool bShow);

	FProjectLibraryEntry Entry;

	UPROPERTY()
	UButton* OpenButton = nullptr;
	UPROPERTY()
	UButton* DuplicateButton = nullptr;
	UPROPERTY()
	UButton* RenameButton = nullptr;
	UPROPERTY()
	UButton* DeleteButton = nullptr;
	// The picture area: the button over all of it, the cover (or the drawing, when there is none), the hint that shows when the pointer is over it, and
	// the cross that takes the cover away.
	UPROPERTY()
	UButton* PictureButton = nullptr;
	UPROPERTY()
	UButton* RemoveCoverButton = nullptr;
	UPROPERTY()
	UImage* CoverImage = nullptr;
	UPROPERTY()
	UWidget* DrawingIcon = nullptr;
	UPROPERTY()
	UBorder* HoverHint = nullptr;
	UPROPERTY()
	UTexture2D* CoverTexture = nullptr;
};
