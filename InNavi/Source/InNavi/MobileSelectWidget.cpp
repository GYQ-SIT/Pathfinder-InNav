#include "MobileSelectWidget.h"

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
		CaptionText = MakeText(Tree, TEXT("Txt_SelectCaption"), Caption, 11.f, Grey, true);
		ValueText = MakeText(Tree, TEXT("Txt_SelectValue"), TEXT(""), 15.f, Ink, true);
		Texts->AddChildToVerticalBox(CaptionText);
		Texts->AddChildToVerticalBox(ValueText)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SelectRow"));
		Row->AddChildToHorizontalBox(Texts)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		// The arrow is a placeholder letter until the design team's icon is in.
		Row->AddChildToHorizontalBox(MakeText(Tree, TEXT("Txt_SelectArrow"), TEXT("v"), 13.f, Grey, true))->SetVerticalAlignment(VAlign_Center);
		UBorder* Box = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SelectBox"));
		Box->SetBrush(Rounded(White, StartStyle::Rgb(203, 213, 225), 12.f));
		Box->SetBrushColor(FLinearColor::White);
		Box->SetPadding(FMargin(14.f, 10.f));
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
		ListPanel->SetBrush(Rounded(White, StartStyle::Rgb(203, 213, 225), 12.f));
		ListPanel->SetBrushColor(FLinearColor::White);
		ListPanel->SetPadding(FMargin(0.f, 4.f));
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
	ValueText->SetText(FText::FromString(GetSelectedLabel()));
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
		Pad->SetPadding(FMargin(14.f, 10.f));
		UTextBlock* Text = MakeText(WidgetTree, FString(), Option.Label, 14.f, bChosen ? Blue : Ink, bChosen);
		Text->SetAutoWrapText(true);
		Pad->SetContent(Text);
		UMobileTapTarget* Row = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
		Row->Setup(Pad, Option.Value, bChosen ? BlueSoft : FLinearColor::Transparent, 0.f);
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
