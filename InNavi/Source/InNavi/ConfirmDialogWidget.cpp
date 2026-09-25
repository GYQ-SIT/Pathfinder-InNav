#include "ConfirmDialogWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

TSharedRef<SWidget> UConfirmDialogWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ConfirmDialogCanvas"));
		Canvas->SetVisibility(ESlateVisibility::Visible);
		WidgetTree->RootWidget = Canvas;

		// Full-screen opaque backdrop - blocks clicks to whatever's behind it, same as the
		// original dialog being a modal.
		UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
		Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 1.f));
		UCanvasPanelSlot* BackdropSlot = Canvas->AddChildToCanvas(Backdrop);
		BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackdropSlot->SetOffsets(FMargin(0.f));

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ConfirmStack"));
		UCanvasPanelSlot* StackSlot = Canvas->AddChildToCanvas(Stack);
		StackSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		StackSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		StackSlot->SetAutoSize(true);

		UBorder* MessageBackground = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MessageBackground"));
		MessageBackground->SetBrushColor(FLinearColor::White);
		MessageBackground->SetPadding(FMargin(4.f));

		UTextBlock* MessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_Message"));
		MessageText->SetText(FText::FromString(TEXT("You have unsaved changes. Do you want to save them?")));
		MessageText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		MessageBackground->SetContent(MessageText);

		UVerticalBoxSlot* MessageSlot = Stack->AddChildToVerticalBox(MessageBackground);
		MessageSlot->SetHorizontalAlignment(HAlign_Center);
		MessageSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

		UHorizontalBox* ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ButtonRow"));
		Stack->AddChildToVerticalBox(ButtonRow);

		auto AddButton = [this, ButtonRow](const TCHAR* Name, const TCHAR* Label, float Width) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));

			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString(Name) + TEXT("Label")));
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetJustification(ETextJustify::Center);
			Text->SetAutoWrapText(true);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));

			USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			SizeBox->SetWidthOverride(Width);
			SizeBox->SetHeightOverride(40.f);
			SizeBox->SetContent(Text);
			Button->SetContent(SizeBox);

			UHorizontalBoxSlot* Slot = ButtonRow->AddChildToHorizontalBox(Button);
			Slot->SetPadding(FMargin(0.f, 0.f, 2.f, 0.f));
			return Button;
		};

		SaveAndLeaveButton = AddButton(TEXT("Btn_SaveAndLeave"), TEXT("Save and Exit"), 150.f);
		DiscardAndLeaveButton = AddButton(TEXT("Btn_DiscardAndLeave"), TEXT("Discard changes and leave"), 220.f);
		CancelButton = AddButton(TEXT("Btn_Cancel"), TEXT("Cancel"), 110.f);
	}

	return Super::RebuildWidget();
}

void UConfirmDialogWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (SaveAndLeaveButton)
	{
		SaveAndLeaveButton->OnClicked.AddUniqueDynamic(this, &UConfirmDialogWidget::OnSaveAndLeaveClicked);
	}
	if (DiscardAndLeaveButton)
	{
		DiscardAndLeaveButton->OnClicked.AddUniqueDynamic(this, &UConfirmDialogWidget::OnDiscardAndLeaveClicked);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.AddUniqueDynamic(this, &UConfirmDialogWidget::OnCancelClicked);
	}
}

void UConfirmDialogWidget::OnSaveAndLeaveClicked()
{
	OnSaveAndLeave.Broadcast();
	RemoveFromParent();
}

void UConfirmDialogWidget::OnDiscardAndLeaveClicked()
{
	OnDiscardAndLeave.Broadcast();
	RemoveFromParent();
}

void UConfirmDialogWidget::OnCancelClicked()
{
	OnCancel.Broadcast();
	RemoveFromParent();
}
