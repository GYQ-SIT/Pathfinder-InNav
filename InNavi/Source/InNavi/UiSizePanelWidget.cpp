// Fill out your copyright notice in the Description page of Project Settings.

#include "UiSizePanelWidget.h"

#include "StartScreenStyle.h"
#include "UiSize.h"
#include "UiSizeSubsystem.h"
#include "UiToolTip.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	constexpr float ControlHeight = 30.f;
	// Two step buttons, the gaps between the controls and room for "200%" in its box.
	constexpr float MinContentWidth = 2.f * ControlHeight + 2.f * 8.f + 60.f;

	// What each control does and its shortcut, in the form every hover hint of the UI has: a sentence, then [the keys].
	const TCHAR* const SmallerHint = TEXT("Make the UI smaller [Ctrl -]");
	const TCHAR* const LargerHint = TEXT("Make the UI larger [Ctrl +]");
	const TCHAR* const ResetHint = TEXT("Back to the default UI size, 100% [Ctrl 0]");
}

TSharedRef<SWidget> UUiSizePanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		using namespace StartStyle;

		// The dock frame paints the panel background; this root only adds the usual panel padding.
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_UiSize"));
		Root->SetBrushColor(FLinearColor::Transparent);
		Root->SetPadding(FMargin(14.f));
		WidgetTree->RootWidget = Root;

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("UiSizeStack"));
		Root->SetContent(Stack);

		// Smaller, the size now in a framed box, larger.
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("UiSizeRow"));
		auto AddStepButton = [this, Row](UButton*& OutButton, const TCHAR* Name, const TCHAR* TextName, const TCHAR* Label, const TCHAR* Hint)
		{
			OutButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			OutButton->SetStyle(StartStyle::SecondaryButton());
			OutButton->SetBackgroundColor(FLinearColor::White);
			OutButton->SetToolTip(UiToolTip::Make(WidgetTree, FString(Hint)));
			OutButton->OnClicked.AddUniqueDynamic(this, &UUiSizePanelWidget::PlayButtonClickSound);
			UTextBlock* Glyph = StartStyle::Text(WidgetTree, TextName, Label, 16.f, StartStyle::Rgb(241, 246, 255), true);
			Glyph->SetJustification(ETextJustify::Center);
			USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			Size->SetWidthOverride(ControlHeight);
			Size->SetHeightOverride(ControlHeight);
			Size->SetContent(Glyph);
			OutButton->SetContent(Size);
			UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(OutButton);
			Slot->SetVerticalAlignment(VAlign_Center);
			return Slot;
		};

		AddStepButton(SmallerButton, TEXT("Btn_UiSizeSmaller"), TEXT("Txt_UiSizeSmaller"), TEXT("-"), SmallerHint);
		SmallerButton->OnClicked.AddUniqueDynamic(this, &UUiSizePanelWidget::OnSmallerClicked);

		// The size now: a dark box with the same outline and height as the buttons beside it, taking the room between them.
		ValueText = Text(WidgetTree, TEXT("Txt_UiSizeValue"), TEXT("100%"), 14.f, Rgb(241, 246, 255), true);
		UBorder* ValueBox = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_UiSizeValue"));
		ValueBox->SetBrush(Rounded(Rgb(12, 22, 38), Rgb(61, 78, 104), 8.f));
		ValueBox->SetBrushColor(FLinearColor::White);
		ValueBox->SetHorizontalAlignment(HAlign_Center);
		ValueBox->SetVerticalAlignment(VAlign_Center);
		ValueBox->SetContent(ValueText);
		USizeBox* ValueSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ValueSize->SetHeightOverride(ControlHeight);
		ValueSize->SetContent(ValueBox);
		UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(ValueSize);
		ValueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ValueSlot->SetVerticalAlignment(VAlign_Center);
		ValueSlot->SetPadding(FMargin(8.f, 0.f));

		AddStepButton(LargerButton, TEXT("Btn_UiSizeLarger"), TEXT("Txt_UiSizeLarger"), TEXT("+"), LargerHint);
		LargerButton->OnClicked.AddUniqueDynamic(this, &UUiSizePanelWidget::OnLargerClicked);

		RowSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size_UiSizeRow"));
		RowSizeBox->SetWidthOverride(ContentWidth);
		RowSizeBox->SetContent(Row);
		Stack->AddChildToVerticalBox(RowSizeBox)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

		// Back to the default size: small, under the box, not a bar across the panel.
		UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("UiSizeFooter"));
		USpacer* FooterSpacerLeft = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
		Footer->AddChildToHorizontalBox(FooterSpacerLeft)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ResetButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_UiSizeReset"));
		ResetButton->SetStyle(SecondaryButton());
		ResetButton->SetBackgroundColor(FLinearColor::White);
		ResetButton->SetToolTip(UiToolTip::Make(WidgetTree, FString(ResetHint)));
		ResetButton->OnClicked.AddUniqueDynamic(this, &UUiSizePanelWidget::OnResetClicked);
		ResetButton->OnClicked.AddUniqueDynamic(this, &UUiSizePanelWidget::PlayButtonClickSound);
		UTextBlock* ResetText = Text(WidgetTree, TEXT("Txt_UiSizeReset"), TEXT("Default size"), 12.f, Rgb(241, 246, 255));
		UBorder* ResetPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		ResetPad->SetBrushColor(FLinearColor::Transparent);
		ResetPad->SetPadding(FMargin(10.f, 3.f));
		ResetPad->SetContent(ResetText);
		ResetButton->SetContent(ResetPad);
		Footer->AddChildToHorizontalBox(ResetButton);
		USpacer* FooterSpacerRight = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
		Footer->AddChildToHorizontalBox(FooterSpacerRight)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		FooterSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size_UiSizeFooter"));
		FooterSizeBox->SetWidthOverride(ContentWidth);
		FooterSizeBox->SetContent(Footer);
		Stack->AddChildToVerticalBox(FooterSizeBox);

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
	// Two step buttons and the box between them need this much; a dock group squeezed below it clips the panel, like the other panels.
	Width = FMath::Max(Width, MinContentWidth);
	if (FMath::IsNearlyEqual(Width, ContentWidth, 0.5f))
	{
		return;
	}
	ContentWidth = Width;
	for (USizeBox* SizeBox : { RowSizeBox, FooterSizeBox })
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
