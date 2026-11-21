#include "MobileSelectWidget.h"

#include "MobileIcons.h"
#include "MobileStyle.h"
#include "MobileTapTarget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
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
		// The directory's small drop-down is a phone list: rounded, with a soft edge, rows inset in it and no scroll bar (the prototype's browser draws
		// a square list with a black edge and a desktop scroll bar, which looks out of place on a phone). The others keep their look.
		ListPanel->SetBrush(bCompact ? Rounded(White, FieldEdge, 12.f, 1.f) : bSmall ? Rounded(White, Ink, 3.f, 1.f) : Rounded(White, FieldEdge, 12.f));
		ListPanel->SetBrushColor(FLinearColor::White);
		ListPanel->SetPadding(bCompact ? FMargin(4.f) : bSmall ? FMargin(0.f) : FMargin(0.f, 4.f));
		ListPanel->SetVisibility(ESlateVisibility::Collapsed);
		UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SelectScroll"));
		if (bCompact)
		{
			Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
			MobileStyle::NoScrollShadow(Scroll);
		}
		ListBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SelectOptions"));
		Scroll->AddChild(ListBox);
		ListLimit = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SelectLimit"));
		ListLimit->SetMaxDesiredHeight(MaxListHeight);
		ListLimit->SetContent(Scroll);
		ListPanel->SetContent(ListLimit);
		if (PopupHost)
		{
			// Over the page: it goes into the host's overlay when it is first opened (the host is still being built now), as wide as the field.
			PopupWidth = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SelectPopup"));
			PopupWidth->SetContent(ListPanel);
			PopupWidth->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			Root->AddChildToVerticalBox(ListPanel)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		}

		Refresh();
	}
	return Super::RebuildWidget();
}

void UMobileSelectWidget::SetMaxListHeight(float Height)
{
	MaxListHeight = Height;
	if (ListLimit)
	{
		ListLimit->SetMaxDesiredHeight(MaxListHeight);
	}
}

void UMobileSelectWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bOpen && PopupSlot)
	{
		PlacePopup();
	}
}

void UMobileSelectWidget::PlacePopup()
{
	if (!PopupHost || !PopupSlot || !PopupWidth || !FieldBox)
	{
		return;
	}
	const FGeometry Field = FieldBox->GetCachedGeometry();
	const FGeometry Host = PopupHost->GetCachedGeometry();
	if (Field.GetLocalSize().X <= 0.f || Host.GetLocalSize().X <= 0.f)
	{
		return;
	}
	// The bottom left corner of the field, in the host's own units. The list is as wide as the field, but not narrower than a name needs (a field
	// is a third of the screen), and it is moved left when the field is at the right edge, so that it stays on the screen.
	const FVector2D Corner = Host.AbsoluteToLocal(Field.GetAbsolutePositionAtCoordinates(FVector2D(0.f, 1.f)));
	constexpr float MinWidth = 168.f;
	constexpr float Edge = 8.f;
	const float Width = FMath::Min(FMath::Max(Field.GetLocalSize().X, MinWidth), Host.GetLocalSize().X - 2.f * Edge);
	const float Left = FMath::Clamp(Corner.X, Edge, FMath::Max(Edge, Host.GetLocalSize().X - Width - Edge));
	PopupSlot->SetPadding(FMargin(Left, Corner.Y + 4.f, 0.f, 0.f));
	PopupWidth->SetWidthOverride(Width);
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
	if (bOpen && PopupHost && PopupWidth && !PopupSlot)
	{
		PopupSlot = PopupHost->AddChildToOverlay(PopupWidth);
		PopupSlot->SetHorizontalAlignment(HAlign_Left);
		PopupSlot->SetVerticalAlignment(VAlign_Top);
	}
	Refresh();
	if (bOpen)
	{
		PlacePopup();
	}
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
		FieldBox->SetBrush(Rounded(White, bOpen ? Blue : FieldEdge, 8.f, bOpen ? 1.5f : 1.f));
	}
	ListPanel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (PopupWidth)
	{
		// Its box is only there while it is open, so that it does not take a tap meant for the page under it.
		PopupWidth->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
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
		Pad->SetPadding(bCompact ? FMargin(12.f, 9.f) : bSmall ? FMargin(8.f, 5.f) : FMargin(14.f, 10.f));
		// A small list, as the prototype's browser draws it: the chosen option on blue with white text. The directory's own: on soft blue, in blue.
		const FLinearColor TextColour = bCompact ? (bChosen ? Blue : Ink) : bSmall ? (bChosen ? OnBlue : Ink) : (bChosen ? Blue : Ink);
		UTextBlock* Text = MakeText(WidgetTree, FString(), Option.Label, Look == EMobileSelectLook::Normal || Look == EMobileSelectLook::Bare ? 14.f : bCompact ? 13.f : 12.f, TextColour, bChosen && (!bSmall || bCompact));
		Text->SetAutoWrapText(true);
		Pad->SetContent(Text);
		UMobileTapTarget* Row = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
		Row->Setup(Pad, Option.Value, bChosen ? (bCompact ? BlueSoft : bSmall ? Blue : BlueSoft) : FLinearColor::Transparent, bCompact ? 8.f : 0.f);
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
