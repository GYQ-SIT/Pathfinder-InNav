#include "MapBuildingRowWidget.h"

#include "NavPlayerController.h"
#include "StartScreenStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

TSharedRef<SWidget> UMapBuildingRowWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		using namespace StartStyle;
		RowButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("RowButton"));
		RowButton->SetStyle(bSelected
			? Button(Rgb(14, 66, 148), Rgb(20, 80, 172), Rgb(10, 54, 124), Rgb(30, 118, 235), 8.f)
			: Button(Rgb(23, 38, 60), Rgb(31, 51, 80), Rgb(18, 31, 50), Rgb(44, 62, 88), 8.f));
		RowButton->SetBackgroundColor(FLinearColor::White);
		WidgetTree->RootWidget = RowButton;

		UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RowContent"));
		UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RowPad"));
		Pad->SetBrushColor(FLinearColor::Transparent);
		Pad->SetPadding(FMargin(12.f * DesignToUi, 10.f * DesignToUi, 12.f * DesignToUi, 10.f * DesignToUi));
		Pad->SetContent(Content);
		if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(RowButton->SetContent(Pad)))
			{
				// A button centres and shrinks its content by default; these rows have to fill it so the right-hand part (the
				// shortcut, the chevron, the +/-) lines up in one column.
				ContentSlot->SetHorizontalAlignment(HAlign_Fill);
				ContentSlot->SetVerticalAlignment(VAlign_Fill);
			}

		// The building mark: an outlined block with two vertical lines.
		USizeBox* MarkSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RowMarkSize"));
		MarkSize->SetWidthOverride(20.f * DesignToUi);
		MarkSize->SetHeightOverride(22.f * DesignToUi);
		UCanvasPanel* Mark = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RowMark"));
		const FLinearColor MarkColor = bSelected ? Rgb(190, 220, 255) : Rgb(150, 176, 210);
		UBorder* Block = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RowMarkBlock"));
		Block->SetBrush(Rounded(FLinearColor::Transparent, MarkColor, 2.f, 1.5f));
		Block->SetBrushColor(FLinearColor::White);
		UCanvasPanelSlot* BlockSlot = Mark->AddChildToCanvas(Block);
		BlockSlot->SetPosition(FVector2D::ZeroVector);
		BlockSlot->SetSize(FVector2D(20.f, 22.f) * DesignToUi);
		for (int32 Line = 0; Line < 2; ++Line)
		{
			UBorder* Bar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *FString::Printf(TEXT("RowMarkLine%d"), Line));
			Bar->SetBrushColor(MarkColor);
			UCanvasPanelSlot* BarSlot = Mark->AddChildToCanvas(Bar);
			BarSlot->SetPosition(FVector2D(7.f + 5.f * Line, 4.f) * DesignToUi);
			BarSlot->SetSize(FVector2D(1.5f, 14.f) * DesignToUi);
		}
		MarkSize->SetContent(Mark);
		UHorizontalBoxSlot* MarkSlot = Content->AddChildToHorizontalBox(MarkSize);
		MarkSlot->SetVerticalAlignment(VAlign_Center);

		UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RowTexts"));
		UTextBlock* NameText = Text(WidgetTree, TEXT("Txt_RowName"), Name, 14.f, Rgb(241, 246, 255), true);
		NameText->SetAutoWrapText(true);
		Texts->AddChildToVerticalBox(NameText);
		Texts->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_RowFloors"), Floors == 1 ? FString(TEXT("1 floor")) : FString::Printf(TEXT("%d floors"), Floors), 12.f,
			bSelected ? Rgb(190, 214, 245) : Rgb(136, 156, 182)))->SetPadding(FMargin(0.f, 2.f * DesignToUi, 0.f, 0.f));
		UHorizontalBoxSlot* TextsSlot = Content->AddChildToHorizontalBox(Texts);
		TextsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextsSlot->SetVerticalAlignment(VAlign_Center);
		TextsSlot->SetPadding(FMargin(12.f * DesignToUi, 0.f, 8.f * DesignToUi, 0.f));

		UHorizontalBoxSlot* ChevronSlot = Content->AddChildToHorizontalBox(Text(WidgetTree, TEXT("Txt_RowChevron"), TEXT("›"), 18.f, Rgb(150, 172, 202), true));
		ChevronSlot->SetVerticalAlignment(VAlign_Center);
	}
	return Super::RebuildWidget();
}

void UMapBuildingRowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (RowButton)
	{
		RowButton->OnClicked.AddUniqueDynamic(this, &UMapBuildingRowWidget::PlayButtonClickSound);
		RowButton->OnClicked.AddUniqueDynamic(this, &UMapBuildingRowWidget::HandleClicked);
	}
}

void UMapBuildingRowWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
	}
}

void UMapBuildingRowWidget::HandleClicked()
{
	if (OnClicked)
	{
		OnClicked(BuildingId);
	}
}
