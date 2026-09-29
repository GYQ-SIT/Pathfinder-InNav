// Fill out your copyright notice in the Description page of Project Settings.

#include "AudioFileRowWidget.h"

#include "NavPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

UAudioFileRowWidget::UAudioFileRowWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedRef<SWidget> UAudioFileRowWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_AudioEntry"));
		Button->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		Button->OnClicked.AddUniqueDynamic(this, &UAudioFileRowWidget::HandleClicked);
		Button->OnClicked.AddUniqueDynamic(this, &UAudioFileRowWidget::PlayButtonClickSound);

		Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_AudioEntryLabel"));
		Label->SetJustification(ETextJustify::Center);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Label->SetAutoWrapText(true);
		{
			// Matches UInteriorScreenWidget::StandardFontSize - keep the two in sync by hand if that
			// one changes, same reasoning as UFloorListEntryWidget's own font setup.
			FSlateFontInfo LabelFont = Label->GetFont();
			LabelFont.Size = 15;
			Label->SetFont(LabelFont);
		}

		SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_AudioEntry"));
		SizeBox->SetMinDesiredHeight(32.f);
		SizeBox->SetContent(Label);
		Button->SetContent(SizeBox);

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
		Button->SetBackgroundColor(bIsPlaying ? FLinearColor(0.3f, 0.75f, 0.35f, 1.f) : FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
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
