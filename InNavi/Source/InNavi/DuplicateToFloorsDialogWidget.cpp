#include "DuplicateToFloorsDialogWidget.h"

#include "LayoutValidation.h"
#include "NavPlayerController.h"
#include "StartScreenStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"

void UDuplicateToFloorsDialogWidget::Setup(const TArray<int32>& InLevels, const TArray<int32>& InFromLevels, const FString& InHeading)
{
	Levels = InLevels;
	FromLevels = InFromLevels;
	Heading = InHeading;
}

TSharedRef<SWidget> UDuplicateToFloorsDialogWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("DuplicateDialogCanvas"));
		Canvas->SetVisibility(ESlateVisibility::Visible);
		WidgetTree->RootWidget = Canvas;

		// Dims what is behind and blocks clicks through to it, like the confirm dialog.
		UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
		Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
		UCanvasPanelSlot* BackdropSlot = Canvas->AddChildToCanvas(Backdrop);
		BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackdropSlot->SetOffsets(FMargin(0.f));

		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Card"));
		Card->SetBrush(StartStyle::Rounded(StartStyle::Rgb(17, 31, 50), StartStyle::Rgb(54, 70, 92), 12.f));
		Card->SetBrushColor(FLinearColor::White);
		Card->SetPadding(FMargin(20.f));
		UCanvasPanelSlot* CardSlot = Canvas->AddChildToCanvas(Card);
		CardSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CardSlot->SetAutoSize(true);

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DuplicateStack"));
		Card->SetContent(Stack);

		USizeBox* BodySize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BodySize"));
		BodySize->SetWidthOverride(360.f);
		UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Body"));
		BodySize->SetContent(Body);
		Stack->AddChildToVerticalBox(BodySize);

		Body->AddChildToVerticalBox(StartStyle::Text(WidgetTree, TEXT("Txt_Title"), TEXT("Duplicate to floors"), 18.f, StartStyle::Rgb(241, 246, 255), true));
		UTextBlock* Subtitle = StartStyle::Text(WidgetTree, TEXT("Txt_Heading"), Heading, 12.f, StartStyle::Rgb(170, 190, 214));
		Subtitle->SetAutoWrapText(true);
		Body->AddChildToVerticalBox(Subtitle)->SetPadding(FMargin(0.f, 4.f, 0.f, 12.f));

		// Every floor, lowest first. The floors the rooms are on cannot be ticked.
		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("FloorScroll"));
		USizeBox* ScrollSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FloorScrollSize"));
		ScrollSize->SetMaxDesiredHeight(220.f);
		ScrollSize->SetContent(Scroll);
		UWrapBox* Wrap = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("FloorWrap"));
		Wrap->SetInnerSlotPadding(FVector2D(14.f, 8.f));
		Scroll->AddChild(Wrap);
		Body->AddChildToVerticalBox(ScrollSize);

		FloorBoxes.Reset();
		for (const int32 Level : Levels)
		{
			const bool bFromFloor = FromLevels.Contains(Level);
			UHorizontalBox* Entry = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*FString::Printf(TEXT("Entry_%d"), Level)));
			UCheckBox* Box = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), FName(*FString::Printf(TEXT("Chk_Floor_%d"), Level)));
			StartStyle::DarkCheckBox(Box);
			Box->SetIsChecked(!bFromFloor);
			Box->SetIsEnabled(!bFromFloor);
			Box->OnCheckStateChanged.AddUniqueDynamic(this, &UDuplicateToFloorsDialogWidget::HandleBoxChanged);
			Entry->AddChildToHorizontalBox(Box)->SetVerticalAlignment(VAlign_Center);
			const FString Label = bFromFloor ? FString::Printf(TEXT("%s (here)"), *LayoutValidation::LevelLabel(Level)) : LayoutValidation::LevelLabel(Level);
			UHorizontalBoxSlot* LabelSlot = Entry->AddChildToHorizontalBox(StartStyle::Text(WidgetTree, FString::Printf(TEXT("Txt_Floor_%d"), Level), Label, 13.f,
				bFromFloor ? StartStyle::Rgb(110, 126, 148) : StartStyle::Rgb(214, 226, 242)));
			LabelSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
			LabelSlot->SetVerticalAlignment(VAlign_Center);
			Wrap->AddChildToWrapBox(Entry);
			FloorBoxes.Add(Box);
		}

		UHorizontalBox* ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ButtonRow"));
		Stack->AddChildToVerticalBox(ButtonRow)->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));
		auto AddButton = [this, ButtonRow](const TCHAR* Name, const TCHAR* Label, float Width, const FButtonStyle& Style) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetStyle(Style);
			Button->SetBackgroundColor(FLinearColor::White);
			UTextBlock* Text = StartStyle::Text(WidgetTree, FString(Name) + TEXT("Label"), Label, 14.f, FLinearColor::White, true);
			Text->SetJustification(ETextJustify::Center);
			USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			Size->SetWidthOverride(Width);
			Size->SetHeightOverride(36.f);
			if (USizeBoxSlot* TextSlot = Cast<USizeBoxSlot>(Size->SetContent(Text)))
			{
				TextSlot->SetHorizontalAlignment(HAlign_Center);
				TextSlot->SetVerticalAlignment(VAlign_Center);
			}
			Button->SetContent(Size);
			ButtonRow->AddChildToHorizontalBox(Button)->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
			return Button;
		};
		DuplicateButton = AddButton(TEXT("Btn_Duplicate"), TEXT("Duplicate"), 120.f, StartStyle::PrimaryButton());
		CancelButton = AddButton(TEXT("Btn_Cancel"), TEXT("Cancel"), 100.f, StartStyle::SecondaryButton());
		RefreshDuplicateButton();
	}

	return Super::RebuildWidget();
}

void UDuplicateToFloorsDialogWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (DuplicateButton)
	{
		DuplicateButton->OnClicked.AddUniqueDynamic(this, &UDuplicateToFloorsDialogWidget::OnDuplicateClicked);
		DuplicateButton->OnClicked.AddUniqueDynamic(this, &UDuplicateToFloorsDialogWidget::PlayButtonClickSound);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.AddUniqueDynamic(this, &UDuplicateToFloorsDialogWidget::OnCancelClicked);
		CancelButton->OnClicked.AddUniqueDynamic(this, &UDuplicateToFloorsDialogWidget::PlayButtonClickSound);
	}
}

TArray<int32> UDuplicateToFloorsDialogWidget::GetTickedLevels() const
{
	TArray<int32> Ticked;
	for (int32 Index = 0; Index < FloorBoxes.Num() && Index < Levels.Num(); ++Index)
	{
		if (FloorBoxes[Index] && FloorBoxes[Index]->GetIsEnabled() && FloorBoxes[Index]->IsChecked()) Ticked.Add(Levels[Index]);
	}
	return Ticked;
}

void UDuplicateToFloorsDialogWidget::RefreshDuplicateButton()
{
	if (DuplicateButton) DuplicateButton->SetIsEnabled(!GetTickedLevels().IsEmpty());
}

void UDuplicateToFloorsDialogWidget::HandleBoxChanged(bool bIsChecked)
{
	RefreshDuplicateButton();
}

void UDuplicateToFloorsDialogWidget::OnDuplicateClicked()
{
	const TArray<int32> Ticked = GetTickedLevels();
	if (Ticked.IsEmpty()) return;
	OnConfirmed.Broadcast(Ticked);
	RemoveFromParent();
}

void UDuplicateToFloorsDialogWidget::OnCancelClicked()
{
	OnCancelled.Broadcast();
	RemoveFromParent();
}

void UDuplicateToFloorsDialogWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
	}
}
