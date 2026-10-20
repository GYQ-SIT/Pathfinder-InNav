// Fill out your copyright notice in the Description page of Project Settings.

#include "AudioFileRowWidget.h"

#include "NavPlayerController.h"
#include "StartScreenStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

namespace
{
	// A sound's row: plain, or green while it plays.
	FButtonStyle Button_Style_AudioRow(bool bPlaying)
	{
		using namespace StartStyle;
		return bPlaying ? Button(Rgb(26, 138, 84), Rgb(36, 160, 100), Rgb(18, 110, 66), Rgb(26, 138, 84), 8.f)
			: Button(Rgb(24, 38, 60), Rgb(33, 54, 84), Rgb(18, 30, 50), Rgb(46, 66, 94), 8.f);
	}
}

UAudioFileRowWidget::UAudioFileRowWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedRef<SWidget> UAudioFileRowWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		using namespace StartStyle;
		Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_AudioEntry"));
		Button->SetStyle(Button_Style_AudioRow(false));
		Button->SetBackgroundColor(FLinearColor::White);
		Button->OnClicked.AddUniqueDynamic(this, &UAudioFileRowWidget::HandleClicked);
		Button->OnClicked.AddUniqueDynamic(this, &UAudioFileRowWidget::PlayButtonClickSound);

		Label = Text(WidgetTree, TEXT("Txt_AudioEntryLabel"), TEXT(""), 13.f, FLinearColor::White, true);
		Label->SetAutoWrapText(true);

		SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_AudioEntry"));
		SizeBox->SetMinDesiredHeight(32.f);
		UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pad_AudioEntry"));
		Pad->SetBrushColor(FLinearColor::Transparent);
		Pad->SetPadding(FMargin(12.f * DesignToUi, 8.f * DesignToUi));
		Pad->SetContent(Label);
		SizeBox->SetContent(Pad);
		if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Button->SetContent(SizeBox)))
		{
			ContentSlot->SetHorizontalAlignment(HAlign_Fill);
			ContentSlot->SetVerticalAlignment(VAlign_Fill);
		}

		WidgetTree->RootWidget = Button;
	}

	ApplyVisuals();
	return Super::RebuildWidget();
}

void UAudioFileRowWidget::SetFileName(const FString& InFileName, bool bInIsPlaying)
{
	FileName = InFileName;
	bIsPlaying = bInIsPlaying;
	ApplyVisuals();
}

void UAudioFileRowWidget::SetWidth(float NewWidth)
{
	Width = NewWidth;
	if (SizeBox)
	{
		SizeBox->SetWidthOverride(Width);
	}
}

void UAudioFileRowWidget::ApplyVisuals()
{
	if (Label)
	{
		Label->SetText(FText::FromString(bIsPlaying ? FileName + TEXT(" (Playing)") : FileName));
	}
	if (Button)
	{
		// Green while playing, matching the light-green 3D-selection-highlight's own "something is
		// active" convention elsewhere in this project.
		Button->SetStyle(Button_Style_AudioRow(bIsPlaying));
	}
	if (SizeBox)
	{
		SizeBox->SetWidthOverride(Width);
	}
}

void UAudioFileRowWidget::HandleClicked()
{
	OnPlayClicked.Broadcast(FileName);
}

void UAudioFileRowWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
	}
}
