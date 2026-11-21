#include "FloorListEntryWidget.h"
#include "LayoutValidation.h"

#include "NavPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "StartScreenStyle.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

UFloorListEntryWidget::UFloorListEntryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

namespace
{
	// A floor row's look: plain, or amber for the active floor (the same amber as the Map panel's selected building). A basement is red
	// instead, dark when it is not the active floor and bright when it is.
	FButtonStyle FloorRowStyle(bool bActive, bool bBasement)
	{
		using namespace StartStyle;
		if (bBasement)
		{
			return bActive ? Button(Rgb(150, 40, 50), Rgb(170, 52, 62), Rgb(120, 30, 40), Rgb(255, 128, 128), 8.f)
				: Button(Rgb(62, 24, 32), Rgb(84, 32, 42), Rgb(46, 18, 24), Rgb(128, 52, 62), 8.f);
		}
		return bActive ? ActiveButton()
			: Button(Rgb(24, 38, 60), Rgb(33, 54, 84), Rgb(18, 30, 50), Rgb(46, 66, 94), 8.f);
	}
}

TSharedRef<SWidget> UFloorListEntryWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		using namespace StartStyle;
		Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_FloorEntry"));
		Button->SetBackgroundColor(FLinearColor::White);
		Button->OnClicked.AddUniqueDynamic(this, &UFloorListEntryWidget::HandleClicked);
		Button->OnClicked.AddUniqueDynamic(this, &UFloorListEntryWidget::PlayButtonClickSound);

		// Width starts at whatever SetWidth was last called with (or the 150 default if never called).
		SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_FloorEntry"));
		SizeBox->SetMinDesiredHeight(32.f);
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HBox_FloorEntry"));
		UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pad_FloorEntry"));
		Pad->SetBrushColor(FLinearColor::Transparent);
		Pad->SetPadding(FMargin(12.f * DesignToUi, 8.f * DesignToUi, 12.f * DesignToUi, 8.f * DesignToUi));
		Pad->SetContent(Row);
		SizeBox->SetContent(Pad);
		if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Button->SetContent(SizeBox)))
		{
			ContentSlot->SetHorizontalAlignment(HAlign_Fill);
			ContentSlot->SetVerticalAlignment(VAlign_Fill);
		}

		// A small floor mark, the name, and how high the floor is on the right.
		UHorizontalBoxSlot* MarkSlot = Row->AddChildToHorizontalBox(Text(WidgetTree, TEXT("Txt_FloorEntryMark"), TEXT("\u25A4"), 15.f, Rgb(176, 198, 228)));
		MarkSlot->SetVerticalAlignment(VAlign_Center);
		Label = Text(WidgetTree, TEXT("Txt_FloorEntryLabel"), TEXT(""), 13.f, FLinearColor::White, true);
		Label->SetAutoWrapText(true);
		UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label);
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(10.f * DesignToUi, 0.f, 8.f * DesignToUi, 0.f));
		ElevationLabel = Text(WidgetTree, TEXT("Txt_FloorEntryElevation"), TEXT(""), 12.f, Rgb(176, 198, 228), true);
		Row->AddChildToHorizontalBox(ElevationLabel)->SetVerticalAlignment(VAlign_Center);

		WidgetTree->RootWidget = Button;
	}

	ApplyVisuals();
	return Super::RebuildWidget();
}

void UFloorListEntryWidget::SetFloorNumber(int32 InFloorNumber, bool bInIsActive, float InElevationMeters, const FString& InName)
{
	FloorName = InName;
	FloorNumber = InFloorNumber;
	bIsActive = bInIsActive;
	ElevationMeters = InElevationMeters;
	ApplyVisuals();
}

void UFloorListEntryWidget::SetWidth(float NewWidth)
{
	Width = NewWidth;
	if (SizeBox)
	{
		SizeBox->SetWidthOverride(Width);
	}
}

void UFloorListEntryWidget::ApplyVisuals()
{
	if (Label)
	{
		// The floor's own name when it has one, else "Level N" ("Level B1" for a basement).
		Label->SetText(FText::FromString(FloorName.IsEmpty() ? TEXT("Level ") + LayoutValidation::LevelLabel(FloorNumber) : FloorName));
	}
	if (ElevationLabel)
	{
		// "3 m", or "3.5 m" when the floor height is not a whole number of metres.
		const bool bWhole = FMath::IsNearlyEqual(ElevationMeters, FMath::RoundToFloat(ElevationMeters), 0.05f);
		ElevationLabel->SetText(FText::FromString(bWhole ? FString::Printf(TEXT("%d m"), FMath::RoundToInt(ElevationMeters)) : FString::Printf(TEXT("%.1f m"), ElevationMeters)));
	}
	if (Button)
	{
		Button->SetStyle(FloorRowStyle(bIsActive, FloorNumber < 0));
	}
	if (SizeBox)
	{
		SizeBox->SetWidthOverride(Width);
	}
}

void UFloorListEntryWidget::HandleClicked()
{
	OnEntryClicked.Broadcast(FloorNumber);
}

void UFloorListEntryWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
	}
}
