#include "DockPanelContent.h"

#include "NavPlayerController.h"
#include "StartScreenStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
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

void UDockPanelContentWidget::ApplyStandardFont(UEditableTextBox* TextBox)
{
	if (!TextBox)
	{
		return;
	}
	FEditableTextBoxStyle Style = TextBox->GetWidgetStyle();
	Style.TextStyle.Font.Size = 15;
	// Matches UInteriorScreenWidget's StandardTextBoxBackgroundColor / StandardTextBoxTextColor.
	Style.BackgroundColor = FSlateColor(FLinearColor(0.149f, 0.149f, 0.169f, 1.f));
	Style.ForegroundColor = FSlateColor(FLinearColor::White);
	Style.FocusedForegroundColor = FSlateColor(FLinearColor::White);
	TextBox->SetWidgetStyle(Style);
}

UCheckBox* UDockPanelContentWidget::AddCheckRow(UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label, bool bChecked, float BottomPadding)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UCheckBox* CheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), FName(Name));
	CheckBox->SetIsChecked(bChecked);
	StartStyle::DarkCheckBox(CheckBox);
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
