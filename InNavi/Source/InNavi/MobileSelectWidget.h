#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileSelectWidget.generated.h"

class UBorder;
class UButton;
class UOverlay;
class UOverlaySlot;
class USizeBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

struct FMobileSelectOption
{
	// What the select hands back, and what it shows.
	FString Value;
	FString Label;
	// What the field shows once the option is chosen, when that is less than the list does ("Study lounge" for "Study lounge . E2 . L1"); empty = the label.
	FString ShortLabel;
};

// How a select looks. Normal: the caption in the box, big text. Compact: the prototype's small drop-down (the caption over a short box, a small arrow).
// Bare: only the chosen text and a small arrow, no box (inside a card). Chip: a small white box with the chosen text only (a building, a level).
enum class EMobileSelectLook : uint8 { Normal, Compact, Bare, Chip };

// A drop-down for the phone screens, drawn by the screen itself: a field with a small caption and the chosen text, and under it, when it is
// open, the list of options. The list opens in the page (below the field), so it scales with the rest of the screen, which a pop-up menu of
// the engine's own combo box does not. A tap on an option chooses it and closes the list.
UCLASS()
class INNAVI_API UMobileSelectWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetCaption(const FString& InCaption);
	// The prototype's small drop-down (the directory's filters): the caption over a short box, a small arrow, small text. Set before the widget is built.
	void SetCompact(bool bInCompact) { Look = bInCompact ? EMobileSelectLook::Compact : EMobileSelectLook::Normal; }
	void SetLook(EMobileSelectLook InLook) { Look = InLook; }
	// The options, and which is chosen (an empty value is a valid option too). A chosen value that is not among them shows as nothing.
	void SetOptions(const TArray<FMobileSelectOption>& InOptions, const FString& InSelected);
	void SetSelected(const FString& InSelected);

	// Called when the person (not the program) chooses an option, and when the list opens.
	void SetOnChanged(TFunction<void(const FString&)> InCallback) { OnChanged = MoveTemp(InCallback); }
	void SetOnOpened(TFunction<void()> InCallback) { OnOpened = MoveTemp(InCallback); }
	// The list opens over the page instead of in it: it is put in this overlay (which covers the page), under the field, and so it does not push
	// the content below the field down. Set before the widget is built. Without a host the list opens in the page, under the field.
	void SetPopupHost(UOverlay* InHost) { PopupHost = InHost; }
	// The most the open list shows before it scrolls (the same as five rows of the directory's small drop-down), so that it never runs off the
	// screen when the sheet is only half open.
	void SetMaxListHeight(float Height);
	// Takes the popup list out of its host (the page rebuilds its rows, and the selects of the old ones go).
	void ClosePopup();
	float GetMaxListHeight() const { return MaxListHeight; }

	void SetOpen(bool bInOpen);
	bool IsOpen() const { return bOpen; }
	// A tap on the field, and on an option.
	void ToggleOpen();
	void Choose(const FString& Value);

	const FString& GetSelected() const { return Selected; }
	FString GetSelectedLabel() const;
	// What the field shows: the short label of the chosen option, else its label.
	FString GetSelectedShortLabel() const;
	int32 GetOptionCount() const { return Options.Num(); }
	// The text that shows the chosen option, and whether the field has an arrow.
	const UTextBlock* GetValueText() const { return ValueText; }
	bool HasArrow() const { return bHasArrow; }
	const TArray<FMobileSelectOption>& GetOptions() const { return Options; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void Refresh();
	// Puts the popup list under the field (the page may move while it is open: the sheet is dragged, the window is resized).
	void PlacePopup();

	UPROPERTY()
	UTextBlock* CaptionText = nullptr;
	UPROPERTY()
	UTextBlock* ValueText = nullptr;
	UPROPERTY()
	UBorder* ListPanel = nullptr;
	// The box of the field, whose edge is black while the list is open (as a drop-down of the prototype's browser is).
	UPROPERTY()
	UBorder* FieldBox = nullptr;
	UPROPERTY()
	UVerticalBox* ListBox = nullptr;
	UPROPERTY()
	USizeBox* ListLimit = nullptr;
	// The overlay the list opens in, when it opens over the page; the box that gives it the width of the field, and its place there.
	UPROPERTY()
	UOverlay* PopupHost = nullptr;
	UPROPERTY()
	USizeBox* PopupWidth = nullptr;
	// Over the page and under the list while it is open: it takes any touch that is not on the list and closes it, so that a tap on the page under it, or a
	// scroll of it, does not act (a list open over a page that still answers is a list nobody can tell is open).
	UPROPERTY()
	UButton* Catcher = nullptr;
	UFUNCTION()
	void HandleCatcherPressed();
	UPROPERTY()
	UOverlaySlot* PopupSlot = nullptr;
	float MaxListHeight = 190.f;
	// The most the list is allowed now (less than MaxListHeight where the page has no room for more), so it is not set again every frame.
	float ListHeightNow = -1.f;

	FString Caption;
	TArray<FMobileSelectOption> Options;
	FString Selected;
	bool bOpen = false;
	bool bHasArrow = false;
	EMobileSelectLook Look = EMobileSelectLook::Normal;

	TFunction<void(const FString&)> OnChanged;
	TFunction<void()> OnOpened;
};
