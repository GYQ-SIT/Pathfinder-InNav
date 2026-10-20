#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProjectLibrary.h"
#include "ProjectCardWidget.generated.h"

class UButton;

// One project in the project library: a picture area with the number of buildings, the project's name, file name and
// description, when it was last written, and Open / Duplicate / Rename. The card does nothing itself - whoever shows it
// sets what the three buttons do.
UCLASS()
class INNAVI_API UProjectCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Has to be called before the card is shown.
	void SetEntry(const FProjectLibraryEntry& InEntry) { Entry = InEntry; }
	const FProjectLibraryEntry& GetEntry() const { return Entry; }

	TFunction<void(const FProjectLibraryEntry&)> OnOpen;
	TFunction<void(const FProjectLibraryEntry&)> OnDuplicate;
	TFunction<void(const FProjectLibraryEntry&)> OnRename;
	TFunction<void(const FProjectLibraryEntry&)> OnDelete;

	UButton* GetOpenButtonForTests() const { return OpenButton; }
	UButton* GetDuplicateButtonForTests() const { return DuplicateButton; }
	UButton* GetRenameButtonForTests() const { return RenameButton; }
	UButton* GetDeleteButtonForTests() const { return DeleteButton; }

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

	FProjectLibraryEntry Entry;

	UPROPERTY()
	UButton* OpenButton = nullptr;
	UPROPERTY()
	UButton* DuplicateButton = nullptr;
	UPROPERTY()
	UButton* RenameButton = nullptr;
	UPROPERTY()
	UButton* DeleteButton = nullptr;
};
