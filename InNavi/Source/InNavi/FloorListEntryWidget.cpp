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
		Label->SetAutoWrapText(true);

		// Matches every other button on this screen's standard size exactly
		// (UInteriorScreenWidget::RebuildWidget's StandardButtonWidth/Height) - kept in sync by hand
		// since this widget is built in its own file. No font override here either, same as those -
		// this renders at the same default size as the rest of the screen.
		USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_FloorEntry"));
		SizeBox->SetWidthOverride(200.f);
		SizeBox->SetMinDesiredHeight(44.f);
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
