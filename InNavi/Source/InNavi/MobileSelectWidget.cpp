#include "MobileSelectWidget.h"

#include "MobileIcons.h"
#include "MobileStyle.h"
#include "MobileTapTarget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace MobileStyle;

TSharedRef<SWidget> UMobileSelectWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		UVerticalBox* Root = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Select"));
		Tree->RootWidget = Root;

		// The field: the caption over the chosen text, and a small arrow.
		UVerticalBox* Texts = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SelectTexts"));
		const bool bSmall = Look != EMobileSelectLook::Normal;
		const bool bCompact = Look == EMobileSelectLook::Compact;
		const bool bCaptioned = Look == EMobileSelectLook::Normal || bCompact;
		CaptionText = MakeText(Tree, TEXT("Txt_SelectCaption"), Caption, 11.f, Grey, !bCompact);
		ValueText = MakeText(Tree, TEXT("Txt_SelectValue"), TEXT(""), Look == EMobileSelectLook::Normal ? 15.f : Look == EMobileSelectLook::Bare ? 15.f : 12.f, Ink, Look == EMobileSelectLook::Normal);
		if (bCompact)
		{
			// The caption is over the box, not in it.
			Root->AddChildToVerticalBox(CaptionText)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}
		else if (bCaptioned)
		{
			Texts->AddChildToVerticalBox(CaptionText);
		}
		Texts->AddChildToVerticalBox(ValueText)->SetPadding(FMargin(0.f, bCaptioned && !bCompact ? 2.f : 0.f, 0.f, 0.f));
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SelectRow"));
		Row->AddChildToHorizontalBox(Texts)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		// The design team's arrow down; until its texture is imported, a letter.
		if (Look != EMobileSelectLook::Chip)
		{
			UWidget* SelectArrow = MobileIcons::Picture(Tree, TEXT("LevelDropdown"), bSmall ? 14.f : 16.f, bSmall ? Ink : Grey);
			Row->AddChildToHorizontalBox(SelectArrow ? SelectArrow : static_cast<UWidget*>(MakeText(Tree, TEXT("Txt_SelectArrow"), TEXT("v"), 13.f, Grey, true)))->SetVerticalAlignment(VAlign_Center);
		}
		UBorder* Box = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SelectBox"));
		FieldBox = Box;
		Box->SetBrush(Look == EMobileSelectLook::Bare ? Rounded(FLinearColor::Transparent, FLinearColor::Transparent, 0.f, 0.f)
			: Look == EMobileSelectLook::Chip ? Rounded(White, FLinearColor::Transparent, 7.f, 0.f) : Rounded(White, FieldEdge, bCompact ? 8.f : 12.f));
		Box->SetBrushColor(FLinearColor::White);
		Box->SetPadding(Look == EMobileSelectLook::Bare ? FMargin(0.f, 6.f) : Look == EMobileSelectLook::Chip ? FMargin(8.f, 6.f) : bCompact ? FMargin(8.f, 7.f) : FMargin(14.f, 10.f));
		Box->SetContent(Row);
		UMobileTapTarget* Field = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
		Field->Setup(Box, TEXT("field"), FLinearColor::Transparent, 12.f);
		TWeakObjectPtr<UMobileSelectWidget> WeakThis(this);
		Field->SetOnTapped([WeakThis](const FString&)
		{
			if (UMobileSelectWidget* Self = WeakThis.Get())
			{
				Self->ToggleOpen();
			}
		});
		Root->AddChildToVerticalBox(Field);

		// The options, under the field when it is open.
		ListPanel = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SelectList"));
		ListPanel->SetBrush(bSmall ? Rounded(White, Ink, 3.f, 1.f) : Rounded(White, FieldEdge, 12.f));
		ListPanel->SetBrushColor(FLinearColor::White);
		ListPanel->SetPadding(bSmall ? FMargin(0.f) : FMargin(0.f, 4.f));
		ListPanel->SetVisibility(ESlateVisibility::Collapsed);
		UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SelectScroll"));
		ListBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SelectOptions"));
		Scroll->AddChild(ListBox);
		USizeBox* Limit = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SelectLimit"));
		Limit->SetMaxDesiredHeight(232.f);
		Limit->SetContent(Scroll);
		ListPanel->SetContent(Limit);
		Root->AddChildToVerticalBox(ListPanel)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

		Refresh();
	}
	return Super::RebuildWidget();
}

void UMobileSelectWidget::SetCaption(const FString& InCaption)
{
	Caption = InCaption;
	if (CaptionText)
	{
		CaptionText->SetText(FText::FromString(Caption));
	}
}

void UMobileSelectWidget::SetOptions(const TArray<FMobileSelectOption>& InOptions, const FString& InSelected)
{
	Options = InOptions;
	Selected = InSelected;
	Refresh();
}

void UMobileSelectWidget::SetSelected(const FString& InSelected)
{
	Selected = InSelected;
	Refresh();
}

FString UMobileSelectWidget::GetSelectedShortLabel() const
{
	const FMobileSelectOption* Found = Options.FindByPredicate([this](const FMobileSelectOption& Option) { return Option.Value == Selected; });
	return Found ? (Found->ShortLabel.IsEmpty() ? Found->Label : Found->ShortLabel) : FString();
}

FString UMobileSelectWidget::GetSelectedLabel() const
{
	const FMobileSelectOption* Found = Options.FindByPredicate([this](const FMobileSelectOption& Option) { return Option.Value == Selected; });
	return Found ? Found->Label : FString();
}

void UMobileSelectWidget::SetOpen(bool bInOpen)
{
	bOpen = bInOpen;
	Refresh();
}

void UMobileSelectWidget::ToggleOpen()
{
	const bool bWillOpen = !bOpen;
	SetOpen(bWillOpen);
	if (bWillOpen && OnOpened)
	{
		OnOpened();
	}
}

void UMobileSelectWidget::Choose(const FString& Value)
{
	Selected = Value;
	bOpen = false;
	Refresh();
	if (OnChanged)
	{
		OnChanged(Value);
	}
}

void UMobileSelectWidget::Refresh()
{
	if (!ValueText || !WidgetTree)
	{
		return;
	}
	ValueText->SetText(FText::FromString(GetSelectedShortLabel()));
	const bool bSmall = Look != EMobileSelectLook::Normal;
	const bool bCompact = Look == EMobileSelectLook::Compact;
	if (bCompact && FieldBox)
	{
		// Black edge while the list is open, as the prototype's drop-down has.
		FieldBox->SetBrush(Rounded(White, bOpen ? Ink : FieldEdge, 8.f, bOpen ? 1.5f : 1.f));
	}
	ListPanel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	ListBox->ClearChildren();
	if (!bOpen)
	{
		return;
	}
	for (const FMobileSelectOption& Option : Options)
	{
		const bool bChosen = Option.Value == Selected;
		UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Pad->SetBrushColor(FLinearColor::Transparent);
		Pad->SetPadding(bSmall ? FMargin(8.f, 5.f) : FMargin(14.f, 10.f));
		// A small list, as the prototype's browser draws it: the chosen option on blue with white text.
		const FLinearColor TextColour = bSmall ? (bChosen ? OnBlue : Ink) : (bChosen ? Blue : Ink);
		UTextBlock* Text = MakeText(WidgetTree, FString(), Option.Label, Look == EMobileSelectLook::Normal || Look == EMobileSelectLook::Bare ? 14.f : 12.f, TextColour, bChosen && !bSmall);
		Text->SetAutoWrapText(true);
		Pad->SetContent(Text);
		UMobileTapTarget* Row = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
		Row->Setup(Pad, Option.Value, bChosen ? (bSmall ? Blue : BlueSoft) : FLinearColor::Transparent, 0.f);
		TWeakObjectPtr<UMobileSelectWidget> WeakThis(this);
		Row->SetOnTapped([WeakThis](const FString& Value)
		{
			if (UMobileSelectWidget* Self = WeakThis.Get())
			{
				Self->Choose(Value);
			}
		});
		ListBox->AddChildToVerticalBox(Row);
	}
}
