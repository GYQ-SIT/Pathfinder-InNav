// Fill out your copyright notice in the Description page of Project Settings.

#include "UiToolTip.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"

UWidget* UiToolTip::Make(UWidgetTree* Tree, const FString& Label)
{
	UTextBlock* TooltipText = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	TooltipText->SetText(FText::FromString(Label));
	TooltipText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	// The card is as wide as its text, up to MaxWidth, and a longer text wraps there. (It used to be a box of a fixed width of 1000 that the text
	// wrapped against, because a maximum width alone let the wrap collapse to one word per line: nothing gave AutoWrapText a width to wrap against.
	// But then a short hint like "Save [Ctrl+S]" was a bar across the whole screen. WrapTextAt is that width: it wraps there, and a shorter text
	// does not stretch to it.)
	TooltipText->SetWrapTextAt(MaxWidth);
	// Left-justified text left a ragged, lopsided-looking gap on the right of every line shorter than the longest one - centering each wrapped
	// line reads as a balanced block instead.
	TooltipText->SetJustification(ETextJustify::Center);
	FSlateFontInfo TooltipFont = TooltipText->GetFont();
	TooltipFont.TypefaceFontName = TEXT("Regular"); // the default font style otherwise renders bold
	TooltipText->SetFont(TooltipFont);

	UBorder* TooltipBorder = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
	TooltipBorder->SetBrushColor(FLinearColor::White);
	TooltipBorder->SetPadding(FMargin(10.f, 6.f));
	TooltipBorder->SetContent(TooltipText);

	// UWidget::SetToolTip builds its SToolTip with no border/background of its own
	// (BorderImage(nullptr)), unlike the engine's native default tooltip (SetToolTipText),
	// which has a soft drop shadow baked into its brush. Faking that here with a second,
	// darker copy of the same box behind the real one, offset a few px down-right - an Overlay
	// stretches every child to match its largest sibling by default, so the shadow layer ends
	// up the same size as the real box even though it has no text of its own.
	UBorder* ShadowBorder = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
	ShadowBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.35f));

	// Overlay slots default to Left/Top, not Fill - without Fill on both, the empty shadow
	// layer would just collapse to zero size instead of stretching to match its sibling.
	UOverlay* TooltipOverlay = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	UOverlaySlot* ShadowSlot = TooltipOverlay->AddChildToOverlay(ShadowBorder);
	ShadowSlot->SetHorizontalAlignment(HAlign_Fill);
	ShadowSlot->SetVerticalAlignment(VAlign_Fill);
	ShadowSlot->SetPadding(FMargin(3.f, 3.f, 0.f, 0.f));
	UOverlaySlot* ForegroundSlot = TooltipOverlay->AddChildToOverlay(TooltipBorder);
	ForegroundSlot->SetHorizontalAlignment(HAlign_Fill);
	ForegroundSlot->SetVerticalAlignment(VAlign_Fill);
	ForegroundSlot->SetPadding(FMargin(0.f, 0.f, 3.f, 3.f));

	return TooltipOverlay;
}
