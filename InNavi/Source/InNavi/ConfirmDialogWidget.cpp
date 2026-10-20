#include "ConfirmDialogWidget.h"

#include "NavPlayerController.h"
#include "StartScreenStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
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

		// Same font size and dark near-black panel color as the rest of the app's chrome (see
		// UInteriorScreenWidget::StandardFontSize/DockPanelColor) - this dialog predates that pass and
		// was left at UMG's 24pt default with plain white/black boxes, which is why button labels like
		// "Discard changes and leave" overflowed their own button at that size.
		constexpr int32 DialogFontSize = 15;

		// Dims whatever's behind instead of blacking it out entirely - still blocks clicks through to
		// it (same modal purpose as before), but reads as a dialog over the app rather than a jarring
		// full-screen wipe to solid black.
		UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
		Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
		UCanvasPanelSlot* BackdropSlot = Canvas->AddChildToCanvas(Backdrop);
		BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackdropSlot->SetOffsets(FMargin(0.f));

		// The dialog card itself - one dark panel holding the message and button row together, instead
		// of a separate white message box floating above an unrelated button row.
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Card"));
		Card->SetBrush(StartStyle::Rounded(StartStyle::Rgb(17, 31, 50), StartStyle::Rgb(54, 70, 92), 12.f));
		Card->SetBrushColor(FLinearColor::White);
		Card->SetPadding(FMargin(20.f));
		UCanvasPanelSlot* CardSlot = Canvas->AddChildToCanvas(Card);
		CardSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CardSlot->SetAutoSize(true);

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ConfirmStack"));
		Card->SetContent(Stack);

		// Capped width + word wrap, rather than assuming the message is always one short sentence -
		// keeps the card a sensible size even for a longer message later.
		USizeBox* MessageSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		MessageSizeBox->SetMaxDesiredWidth(360.f);

		UTextBlock* MessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_Message"));
		MessageText->SetText(FText::FromString(TEXT("You have unsaved changes. Do you want to save them?")));
		MessageText->SetColorAndOpacity(FSlateColor(StartStyle::Rgb(226, 238, 252)));
		MessageText->SetJustification(ETextJustify::Center);
		MessageText->SetAutoWrapText(true);
		{
			FSlateFontInfo MessageFont = MessageText->GetFont();
			MessageFont.Size = DialogFontSize;
			MessageText->SetFont(MessageFont);
		}
		MessageSizeBox->SetContent(MessageText);

		UVerticalBoxSlot* MessageSlot = Stack->AddChildToVerticalBox(MessageSizeBox);
		MessageSlot->SetHorizontalAlignment(HAlign_Center);
		MessageSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));

		UHorizontalBox* ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ButtonRow"));
		UVerticalBoxSlot* ButtonRowSlot = Stack->AddChildToVerticalBox(ButtonRow);
		ButtonRowSlot->SetHorizontalAlignment(HAlign_Center);

		// Width is sized to comfortably fit each label at DialogFontSize on one line (no more wrapping
		// needed once the font shrank from UMG's 24pt default) - a shared per-button color instead of
		// the button's own uniform gray, so Discard (a destructive/leave-without-saving action) reads
		// differently from Save/Cancel at a glance.
		auto AddButton = [this, ButtonRow, DialogFontSize](const TCHAR* Name, const TCHAR* Label, float Width, const FButtonStyle& Style) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetStyle(Style);
			Button->SetBackgroundColor(FLinearColor::White);

			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString(Name) + TEXT("Label")));
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetJustification(ETextJustify::Center);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			{
				FSlateFontInfo ButtonFont = Text->GetFont();
				ButtonFont.Size = DialogFontSize;
				Text->SetFont(ButtonFont);
			}

			USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			SizeBox->SetWidthOverride(Width);
			SizeBox->SetHeightOverride(36.f);
			if (USizeBoxSlot* TextSlot = Cast<USizeBoxSlot>(SizeBox->SetContent(Text)))
			{
				TextSlot->SetHorizontalAlignment(HAlign_Center);
				TextSlot->SetVerticalAlignment(VAlign_Center);
			}
			Button->SetContent(SizeBox);

			UHorizontalBoxSlot* ButtonSlot = ButtonRow->AddChildToHorizontalBox(Button);
			ButtonSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
			return Button;
		};

		const FButtonStyle PrimaryStyle = StartStyle::PrimaryButton();
		const FButtonStyle NeutralStyle = StartStyle::SecondaryButton();
		const FButtonStyle DestructiveStyle = StartStyle::StatusButton(StartStyle::Rgb(150, 40, 48));

		SaveAndLeaveButton = AddButton(TEXT("Btn_SaveAndLeave"), TEXT("Save and Exit"), 140.f, PrimaryStyle);
		DiscardAndLeaveButton = AddButton(TEXT("Btn_DiscardAndLeave"), TEXT("Discard and Leave"), 150.f, DestructiveStyle);
		CancelButton = AddButton(TEXT("Btn_Cancel"), TEXT("Cancel"), 90.f, NeutralStyle);
		// Last button in the row - no trailing gap needed.
		if (UHorizontalBoxSlot* CancelSlot = Cast<UHorizontalBoxSlot>(CancelButton->Slot))
		{
			CancelSlot->SetPadding(FMargin(0.f));
		}
	}

	return Super::RebuildWidget();
}

void UConfirmDialogWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Plays once, right when the dialog itself appears - not tied to any of its buttons (see
	// PlayButtonClickSound below for those).
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayQuitConfirmSound();
	}

	if (SaveAndLeaveButton)
	{
		SaveAndLeaveButton->OnClicked.AddUniqueDynamic(this, &UConfirmDialogWidget::OnSaveAndLeaveClicked);
		SaveAndLeaveButton->OnClicked.AddUniqueDynamic(this, &UConfirmDialogWidget::PlayButtonClickSound);
	}
	if (DiscardAndLeaveButton)
	{
		DiscardAndLeaveButton->OnClicked.AddUniqueDynamic(this, &UConfirmDialogWidget::OnDiscardAndLeaveClicked);
		DiscardAndLeaveButton->OnClicked.AddUniqueDynamic(this, &UConfirmDialogWidget::PlayButtonClickSound);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.AddUniqueDynamic(this, &UConfirmDialogWidget::OnCancelClicked);
		CancelButton->OnClicked.AddUniqueDynamic(this, &UConfirmDialogWidget::PlayButtonClickSound);
	}
}

void UConfirmDialogWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
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
