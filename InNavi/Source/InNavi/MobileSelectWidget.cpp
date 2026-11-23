#include "MobileSelectWidget.h"

#include "MobileIcons.h"
#include "MobileStyle.h"
#include "MobileTapTarget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
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
		// A long name is cut with "..." where the field ends (in a card row, before the cross and the handle; in a filter, before the arrow and the next
		// filter: "bridge 1283901249" ran out of its box over the Level one). The "..." comes where the text meets the edge of its clip, so the text clips.
		ValueText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		ValueText->SetClipping(EWidgetClipping::ClipToBounds);
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
		UHorizontalBoxSlot* TextsSlot = Row->AddChildToHorizontalBox(Texts);
		TextsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		// A gap before the arrow, so that a name that fills the field does not touch it.
		TextsSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		// The design team's arrow down; until its texture is imported, a letter.
		// No arrow on a row of a card (Bare) or on a chip: the row is the control, as in the prototype.
		bHasArrow = Look != EMobileSelectLook::Chip && Look != EMobileSelectLook::Bare;
		if (bHasArrow)
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
		if (Look == EMobileSelectLook::Bare)
		{
			// The engine's button pads its content by 4: the name would start 4 further in than the line of the place under it.
			Field->SetContentPadding(FMargin(0.f));
		}
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
		// The small drop-downs (the directory's filters, the rows of a route) are phone lists: rounded, with a soft edge, rows inset in it and no scroll bar
		// (the prototype's browser draws a square list with a black edge and a desktop scroll bar, which looks out of place on a phone).
		ListPanel->SetBrush(bSmall ? Rounded(White, FieldEdge, 12.f, 1.f) : Rounded(White, FieldEdge, 12.f));
		ListPanel->SetBrushColor(FLinearColor::White);
		ListPanel->SetPadding(bSmall ? FMargin(4.f) : FMargin(0.f, 4.f));
		ListPanel->SetVisibility(ESlateVisibility::Collapsed);
		UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SelectScroll"));
		if (bSmall)
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

void UMobileSelectWidget::HandleCatcherPressed()
{
	SetOpen(false);
}

void UMobileSelectWidget::ClosePopup()
{
	bOpen = false;
	if (PopupHost && PopupWidth && PopupSlot)
	{
		PopupHost->RemoveChild(PopupWidth);
		if (Catcher)
		{
			PopupHost->RemoveChild(Catcher);
			Catcher = nullptr;
		}
		PopupSlot = nullptr;
	}
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
	// How tall the whole list would be (its rows, up to the most it shows), and how much room the page has under the field and over it. The list goes
	// under the field when it fits there; else on the side with more room, and then it is only as tall as that room (it scrolls), so that it is never
	// cut off by the top or the bottom of the page: with the sheet half open the field is near the top and has the room below.
	const float RowsHeight = ListBox && ListBox->GetDesiredSize().Y > 1.f ? static_cast<float>(ListBox->GetDesiredSize().Y) : Options.Num() * 34.f;
	const float Natural = FMath::Min(RowsHeight, MaxListHeight) + 8.f;
	const float FieldTop = Corner.Y - static_cast<float>(Field.GetLocalSize().Y);
	const float RoomBelow = static_cast<float>(Host.GetLocalSize().Y) - Edge - (Corner.Y + 4.f);
	const float RoomAbove = FieldTop - 4.f - Edge;
	const bool bAbove = RoomBelow < Natural && RoomAbove > RoomBelow;
	constexpr float TwoRows = 2.f * 34.f + 8.f;
	const float Allowed = FMath::Min(Natural, FMath::Max(bAbove ? RoomAbove : RoomBelow, TwoRows));
	if (!FMath::IsNearlyEqual(Allowed, ListHeightNow, 0.5f) && ListLimit)
	{
		ListHeightNow = Allowed;
		ListLimit->SetMaxDesiredHeight(Allowed - 8.f);
	}
	if (bAbove)
	{
		PopupSlot->SetVerticalAlignment(VAlign_Bottom);
		PopupSlot->SetPadding(FMargin(Left, 0.f, 0.f, FMath::Max(Edge, static_cast<float>(Host.GetLocalSize().Y) - (FieldTop - 4.f))));
	}
	else
	{
		PopupSlot->SetVerticalAlignment(VAlign_Top);
		PopupSlot->SetPadding(FMargin(Left, Corner.Y + 4.f, 0.f, 0.f));
	}
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
	// The list goes into the host's overlay the first time it opens (also when it was opened before the widget was built).
	if (bOpen && PopupHost && PopupWidth && !PopupSlot)
	{
		Catcher = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SelectCatcher"));
		Catcher->SetStyle(StartStyle::Button(FLinearColor::Transparent, FLinearColor::Transparent, FLinearColor::Transparent, FLinearColor::Transparent, 0.f));
		Catcher->SetBackgroundColor(FLinearColor::White);
		// It closes the list as the finger goes down (a scroll starts with a touch too), not when it comes up.
		Catcher->SetClickMethod(EButtonClickMethod::MouseDown);
		Catcher->SetTouchMethod(EButtonTouchMethod::Down);
		Catcher->OnClicked.AddUniqueDynamic(this, &UMobileSelectWidget::HandleCatcherPressed);
		UOverlaySlot* CatcherSlot = PopupHost->AddChildToOverlay(Catcher);
		CatcherSlot->SetHorizontalAlignment(HAlign_Fill);
		CatcherSlot->SetVerticalAlignment(VAlign_Fill);
		PopupSlot = PopupHost->AddChildToOverlay(PopupWidth);
		PopupSlot->SetHorizontalAlignment(HAlign_Left);
		PopupSlot->SetVerticalAlignment(VAlign_Top);
	}
	ListPanel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (Catcher)
	{
		Catcher->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
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
		Pad->SetPadding(bSmall ? FMargin(12.f, 9.f) : FMargin(14.f, 10.f));
		// The chosen option on soft blue, in blue.
		const FLinearColor TextColour = bChosen ? Blue : Ink;
		UTextBlock* Text = MakeText(WidgetTree, FString(), Option.Label, Look == EMobileSelectLook::Normal || Look == EMobileSelectLook::Bare ? 14.f : 13.f, TextColour, bChosen);
		Text->SetAutoWrapText(true);
		Pad->SetContent(Text);
		UMobileTapTarget* Row = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
		Row->Setup(Pad, Option.Value, bChosen ? BlueSoft : FLinearColor::Transparent, bSmall ? 8.f : 0.f);
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
