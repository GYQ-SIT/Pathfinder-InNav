#include "DockPanelContent.h"

#include "NavPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

UWidget* UDockPanelContentWidget::FindContentWidget(FName Name) const
{
	return WidgetTree ? FindObject<UWidget>(WidgetTree, *Name.ToString()) : nullptr;
}

ANavPlayerController* UDockPanelContentWidget::GetController() const
{
	if (ANavPlayerController* Own = GetOwningPlayer<ANavPlayerController>())
	{
		return Own;
	}
	const UUserWidget* Screen = OwnerScreen.Get();
	return Screen ? Screen->GetOwningPlayer<ANavPlayerController>() : nullptr;
}

void UDockPanelContentWidget::ApplyStandardFont(UTextBlock* TextBlock)
{
	if (!TextBlock)
	{
		return;
	}
	FSlateFontInfo Font = TextBlock->GetFont();
	Font.Size = 15;
	TextBlock->SetFont(Font);
}

UCheckBox* UDockPanelContentWidget::AddCheckRow(UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label, bool bChecked, float BottomPadding)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UCheckBox* CheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), FName(Name));
	CheckBox->SetIsChecked(bChecked);
	UHorizontalBoxSlot* CheckSlot = Row->AddChildToHorizontalBox(CheckBox);
	CheckSlot->SetVerticalAlignment(VAlign_Center);
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(FString(Label)));
	Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ApplyStandardFont(Text);
	UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
	TextSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
	TextSlot->SetVerticalAlignment(VAlign_Center);
	UVerticalBoxSlot* RowSlot = Stack->AddChildToVerticalBox(Row);
	RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPadding));
	return CheckBox;
}

void UDockPanelContentWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->PlayButtonClickSound();
	}
}
