// Fill out your copyright notice in the Description page of Project Settings.

#include "UiSizePanelWidget.h"

#include "StartScreenStyle.h"
#include "UiSize.h"
#include "UiSizeSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	constexpr float StepButtonWidth = 44.f;
	// Two step buttons and room for "200%" between them.
	constexpr float MinContentWidth = 2.f * StepButtonWidth + 60.f;
}

TSharedRef<SWidget> UUiSizePanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// The dock frame paints the panel background; this root only adds the usual panel padding.
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_UiSize"));
		Root->SetBrushColor(FLinearColor::Transparent);
		Root->SetPadding(FMargin(14.f));
		WidgetTree->RootWidget = Root;

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("UiSizeStack"));
		Root->SetContent(Stack);

		// What the panel does, and the keys that do the same; wraps instead of running out of the panel.
		HelpText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_UiSizeHelp"));
		HelpText->SetText(FText::FromString(TEXT("Makes everything on screen bigger or smaller. Ctrl + / Ctrl - / Ctrl 0 do the same.")));
		HelpText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		HelpText->SetAutoWrapText(true);
		ApplyStandardFont(HelpText);
		HelpSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size_UiSizeHelp"));
		HelpSizeBox->SetWidthOverride(ContentWidth);
		HelpSizeBox->SetContent(HelpText);
		Stack->AddChildToVerticalBox(HelpSizeBox)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

		// Smaller, the size now, larger.
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("UiSizeRow"));
		auto AddStepButton = [this, Row](UButton*& OutButton, const TCHAR* Name, const TCHAR* TextName, const TCHAR* Label)
		{
			OutButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			OutButton->SetStyle(StartStyle::SecondaryButton());
			OutButton->SetBackgroundColor(FLinearColor::White);
			OutButton->OnClicked.AddUniqueDynamic(this, &UUiSizePanelWidget::PlayButtonClickSound);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(TextName));
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetJustification(ETextJustify::Center);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(Text);
			USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			Size->SetWidthOverride(StepButtonWidth);
			Size->SetMinDesiredHeight(32.f);
			Size->SetContent(Text);
			OutButton->SetContent(Size);
			return Row->AddChildToHorizontalBox(OutButton);
		};

		AddStepButton(SmallerButton, TEXT("Btn_UiSizeSmaller"), TEXT("Txt_UiSizeSmaller"), TEXT("-"));
		SmallerButton->OnClicked.AddUniqueDynamic(this, &UUiSizePanelWidget::OnSmallerClicked);

		ValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_UiSizeValue"));
		ValueText->SetJustification(ETextJustify::Center);
		ValueText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(ValueText);
		UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(ValueText);
		ValueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ValueSlot->SetHorizontalAlignment(HAlign_Fill);
		ValueSlot->SetVerticalAlignment(VAlign_Center);

		AddStepButton(LargerButton, TEXT("Btn_UiSizeLarger"), TEXT("Txt_UiSizeLarger"), TEXT("+"));
		LargerButton->OnClicked.AddUniqueDynamic(this, &UUiSizePanelWidget::OnLargerClicked);

		RowSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size_UiSizeRow"));
		RowSizeBox->SetWidthOverride(ContentWidth);
		RowSizeBox->SetContent(Row);
		Stack->AddChildToVerticalBox(RowSizeBox)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

		// Back to the default size.
		ResetButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_UiSizeReset"));
		ResetButton->SetStyle(StartStyle::SecondaryButton());
		ResetButton->SetBackgroundColor(FLinearColor::White);
		ResetButton->OnClicked.AddUniqueDynamic(this, &UUiSizePanelWidget::OnResetClicked);
		ResetButton->OnClicked.AddUniqueDynamic(this, &UUiSizePanelWidget::PlayButtonClickSound);
		UTextBlock* ResetText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_UiSizeReset"));
		ResetText->SetText(FText::FromString(TEXT("Default size")));
		ResetText->SetJustification(ETextJustify::Center);
		ResetText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ResetText->SetAutoWrapText(true);
		ApplyStandardFont(ResetText);
		ResetSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size_UiSizeReset"));
		ResetSizeBox->SetWidthOverride(ContentWidth);
		ResetSizeBox->SetMinDesiredHeight(32.f);
		ResetSizeBox->SetContent(ResetText);
		ResetButton->SetContent(ResetSizeBox);
		Stack->AddChildToVerticalBox(ResetButton);

		RefreshValue();
	}
	return Super::RebuildWidget();
}

void UUiSizePanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshValue();
}

void UUiSizePanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// Ctrl + / Ctrl - / Ctrl 0 change the size behind the panel's back.
	RefreshValue();
}

void UUiSizePanelWidget::SetContentWidth(float Width)
{
	// Two step buttons and the figure between them need this much; a dock group squeezed below it clips the panel, like the other panels.
	Width = FMath::Max(Width, MinContentWidth);
	if (FMath::IsNearlyEqual(Width, ContentWidth, 0.5f))
	{
		return;
	}
	ContentWidth = Width;
	for (USizeBox* SizeBox : { HelpSizeBox, RowSizeBox, ResetSizeBox })
	{
		if (SizeBox)
		{
			SizeBox->SetWidthOverride(Width);
		}
	}
}

UUiSizeSubsystem* UUiSizePanelWidget::GetSubsystem() const
{
	return SubsystemForTests ? SubsystemForTests.Get() : UUiSizeSubsystem::Get(this);
}

void UUiSizePanelWidget::RefreshValue()
{
	const UUiSizeSubsystem* Subsystem = GetSubsystem();
	const int32 Percent = Subsystem ? Subsystem->GetPercent() : UiSize::DefaultPercent;
	if (Percent == ShownPercent || !ValueText)
	{
		return;
	}
	ShownPercent = Percent;
	ValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), Percent)));
}

void UUiSizePanelWidget::OnSmallerClicked()
{
	if (UUiSizeSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->Smaller();
	}
	RefreshValue();
}

void UUiSizePanelWidget::OnLargerClicked()
{
	if (UUiSizeSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->Larger();
	}
	RefreshValue();
}

void UUiSizePanelWidget::OnResetClicked()
{
	if (UUiSizeSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->Reset();
	}
	RefreshValue();
}
