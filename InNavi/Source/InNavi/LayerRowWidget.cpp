#include "LayerRowWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"

ULayerRowWidget::ULayerRowWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedRef<SWidget> ULayerRowWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		RowSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_LayerRow"));
		WidgetTree->RootWidget = RowSizeBox;

		Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HBox_LayerRow"));
		RowSizeBox->SetContent(Row);

		// Reserves the indent width whether or not this particular row shows an expand arrow, so
		// every row's label lines up at the same X regardless of depth.
		IndentSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_LayerRowIndent"));
		UHorizontalBoxSlot* IndentSlot = Row->AddChildToHorizontalBox(IndentSizeBox);
		IndentSlot->SetVerticalAlignment(VAlign_Center);

		ExpandButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_LayerRowExpand"));
		ExpandButton->SetBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		ExpandButton->OnClicked.AddUniqueDynamic(this, &ULayerRowWidget::HandleExpandClicked);
		ExpandButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_LayerRowExpand"));
		ExpandButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ExpandButtonText->SetJustification(ETextJustify::Center);
		USizeBox* ExpandGlyphSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ExpandGlyphSizeBox->SetWidthOverride(20.f);
		ExpandGlyphSizeBox->SetHeightOverride(20.f);
		if (USizeBoxSlot* ExpandGlyphSlot = Cast<USizeBoxSlot>(ExpandGlyphSizeBox->SetContent(ExpandButtonText)))
		{
			ExpandGlyphSlot->SetHorizontalAlignment(HAlign_Center);
			ExpandGlyphSlot->SetVerticalAlignment(VAlign_Center);
		}
		ExpandButton->SetContent(ExpandGlyphSizeBox);
		UHorizontalBoxSlot* ExpandButtonSlot = Row->AddChildToHorizontalBox(ExpandButton);
		ExpandButtonSlot->SetVerticalAlignment(VAlign_Center);
		ExpandButtonSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));

		Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_LayerRowLabel"));
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Label->SetAutoWrapText(true);
		{
			// Matches UInteriorScreenWidget::StandardFontSize - keep the two in sync by hand if that
			// one changes.
			FSlateFontInfo LabelFont = Label->GetFont();
			LabelFont.Size = 15;
			Label->SetFont(LabelFont);
		}
		UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetHorizontalAlignment(HAlign_Fill);
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));

		VisibilityButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_LayerRowVisibility"));
		VisibilityButton->SetBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		VisibilityButton->OnClicked.AddUniqueDynamic(this, &ULayerRowWidget::HandleVisibilityClicked);
		VisibilityButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_LayerRowVisibility"));
		VisibilityButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		VisibilityButtonText->SetJustification(ETextJustify::Center);
		USizeBox* VisibilityGlyphSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		VisibilityGlyphSizeBox->SetWidthOverride(24.f);
		VisibilityGlyphSizeBox->SetHeightOverride(24.f);
		if (USizeBoxSlot* VisibilityGlyphSlot = Cast<USizeBoxSlot>(VisibilityGlyphSizeBox->SetContent(VisibilityButtonText)))
		{
			VisibilityGlyphSlot->SetHorizontalAlignment(HAlign_Center);
			VisibilityGlyphSlot->SetVerticalAlignment(VAlign_Center);
		}
		VisibilityButton->SetContent(VisibilityGlyphSizeBox);
		UHorizontalBoxSlot* VisibilityButtonSlot = Row->AddChildToHorizontalBox(VisibilityButton);
		VisibilityButtonSlot->SetVerticalAlignment(VAlign_Center);
	}

	ApplyVisuals();
	return Super::RebuildWidget();
}

void ULayerRowWidget::Setup(const FString& InRowKey, const FString& InLabel, float IndentPx, bool bInShowExpandArrow, bool bInIsExpanded, bool bInIsVisible)
{
	RowKey = InRowKey;
	LabelText = InLabel;
	Indent = IndentPx;
	bShowExpandArrow = bInShowExpandArrow;
	bIsExpanded = bInIsExpanded;
	bIsVisible = bInIsVisible;
	ApplyVisuals();
}

void ULayerRowWidget::SetWidth(float NewWidth)
{
	Width = NewWidth;
	if (RowSizeBox)
	{
		RowSizeBox->SetWidthOverride(Width);
	}
}

void ULayerRowWidget::ApplyVisuals()
{
	if (IndentSizeBox)
	{
		IndentSizeBox->SetWidthOverride(Indent);
	}
	if (ExpandButton)
	{
		// Hidden (not Collapsed) - keeps its slot's width reserved so every row's label still lines
		// up at the same X regardless of whether that particular row shows an arrow.
		ExpandButton->SetVisibility(bShowExpandArrow ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}
	if (ExpandButtonText)
	{
		ExpandButtonText->SetText(FText::FromString(bIsExpanded ? TEXT("\x25BC") : TEXT("\x25B6"))); // ▼ / ▶
	}
	if (Label)
	{
		Label->SetText(FText::FromString(LabelText));
	}
	if (VisibilityButtonText)
	{
		VisibilityButtonText->SetText(FText::FromString(bIsVisible ? TEXT("\x25CF") : TEXT("\x25CB"))); // ● / ○
	}
	if (RowSizeBox)
	{
		RowSizeBox->SetWidthOverride(Width);
	}
}

void ULayerRowWidget::HandleExpandClicked()
{
	OnExpandClicked.Broadcast(RowKey);
}

void ULayerRowWidget::HandleVisibilityClicked()
{
	OnVisibilityClicked.Broadcast(RowKey);
}
