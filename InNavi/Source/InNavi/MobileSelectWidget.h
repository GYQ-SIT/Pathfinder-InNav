#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileSelectWidget.generated.h"

class UBorder;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

struct FMobileSelectOption
{
	// What the select hands back, and what it shows.
	FString Value;
	FString Label;
};

// A drop-down for the phone screens, drawn by the screen itself: a field with a small caption and the chosen text, and under it, when it is
// open, the list of options. The list opens in the page (below the field), so it scales with the rest of the screen, which a pop-up menu of
// the engine's own combo box does not. A tap on an option chooses it and closes the list.
UCLASS()
class INNAVI_API UMobileSelectWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetCaption(const FString& InCaption);
	// The options, and which is chosen (an empty value is a valid option too). A chosen value that is not among them shows as nothing.
	void SetOptions(const TArray<FMobileSelectOption>& InOptions, const FString& InSelected);
	void SetSelected(const FString& InSelected);

	// Called when the person (not the program) chooses an option, and when the list opens.
	void SetOnChanged(TFunction<void(const FString&)> InCallback) { OnChanged = MoveTemp(InCallback); }
	void SetOnOpened(TFunction<void()> InCallback) { OnOpened = MoveTemp(InCallback); }

	void SetOpen(bool bInOpen);
	bool IsOpen() const { return bOpen; }
	// A tap on the field, and on an option.
	void ToggleOpen();
	void Choose(const FString& Value);

	const FString& GetSelected() const { return Selected; }
	FString GetSelectedLabel() const;
	int32 GetOptionCount() const { return Options.Num(); }
	const TArray<FMobileSelectOption>& GetOptions() const { return Options; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void Refresh();

	UPROPERTY()
	UTextBlock* CaptionText = nullptr;
	UPROPERTY()
	UTextBlock* ValueText = nullptr;
	UPROPERTY()
	UBorder* ListPanel = nullptr;
	UPROPERTY()
	UVerticalBox* ListBox = nullptr;

	FString Caption;
	TArray<FMobileSelectOption> Options;
	FString Selected;
	bool bOpen = false;

	TFunction<void(const FString&)> OnChanged;
	TFunction<void()> OnOpened;
};
