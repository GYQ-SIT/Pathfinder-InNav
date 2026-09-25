#include "FloorListEntryWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

UFloorListEntryWidget::UFloorListEntryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedRef<SWidget> UFloorListEntryWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_FloorEntry"));
		Button->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		Button->OnClicked.AddUniqueDynamic(this, &UFloorListEntryWidget::HandleClicked);

		Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_FloorEntryLabel"));
		Label->SetJustification(ETextJustify::Center);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));

		// Matches Add/Delete floor buttons' size exactly (UInteriorScreenWidget::RebuildWidget) so
		// every button in the Floor panel is visually the same size.
		USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_FloorEntry"));
		SizeBox->SetWidthOverride(150.f);
		SizeBox->SetHeightOverride(32.f);
		SizeBox->SetContent(Label);
		Button->SetContent(SizeBox);

		WidgetTree->RootWidget = Button;
	}

	ApplyVisuals();
	return Super::RebuildWidget();
}

void UFloorListEntryWidget::SetFloorNumber(int32 InFloorNumber, bool bInIsActive)
{
	FloorNumber = InFloorNumber;
	bIsActive = bInIsActive;
	ApplyVisuals();
}

void UFloorListEntryWidget::ApplyVisuals()
{
	if (Label)
	{
		Label->SetText(FText::FromString(FString::Printf(TEXT("Level %d"), FloorNumber)));
	}
	if (Button)
	{
		// Light purple when active, matching the hand-drawn mockup's highlight on the current floor.
		Button->SetBackgroundColor(bIsActive ? FLinearColor(0.75f, 0.68f, 0.95f, 1.f) : FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
	}
}

void UFloorListEntryWidget::HandleClicked()
{
	OnEntryClicked.Broadcast(FloorNumber);
}
