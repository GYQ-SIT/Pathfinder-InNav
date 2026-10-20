#include "AudioPanelWidget.h"

#include "AudioFileRowWidget.h"
#include "StartScreenStyle.h"
#include "NavPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	constexpr float AudioFileListHeight = 150.f;
}

TSharedRef<SWidget> UAudioPanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// The dock frame paints the panel background; this root only adds the usual panel padding.
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_Audio"));
		Root->SetBrushColor(FLinearColor::Transparent);
		Root->SetPadding(FMargin(14.f));
		WidgetTree->RootWidget = Root;

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AudioStack"));
		Root->SetContent(Stack);

		StopAllButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_AudioStopAll"));
		StopAllButton->SetStyle(StartStyle::Button(StartStyle::Rgb(150, 40, 48), StartStyle::Rgb(180, 52, 62), StartStyle::Rgb(120, 32, 40), StartStyle::Rgb(180, 52, 62)));
		StopAllButton->SetBackgroundColor(FLinearColor::White);
		StopAllButton->OnClicked.AddUniqueDynamic(this, &UAudioPanelWidget::OnStopAllClicked);
		StopAllButton->OnClicked.AddUniqueDynamic(this, &UAudioPanelWidget::PlayButtonClickSound);
		UTextBlock* StopAllText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_AudioStopAll"));
		StopAllText->SetText(FText::FromString(TEXT("Stop All")));
		StopAllText->SetJustification(ETextJustify::Center);
		StopAllText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		StopAllText->SetAutoWrapText(true);
		ApplyStandardFont(StopAllText);
		USizeBox* StopAllSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size_AudioStopAll"));
		StopAllSize->SetWidthOverride(ContentWidth);
		StopAllSize->SetMinDesiredHeight(32.f);
		StopAllSize->SetContent(StopAllText);
		StopAllButton->SetContent(StopAllSize);
		Stack->AddChildToVerticalBox(StopAllButton)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		// Each UI sound can be muted on its own, without touching the files below. These are settings, not
		// playable files, so they are plain rows.
		ClickSoundCheckbox = AddCheckRow(Stack, TEXT("Chk_AudioClickSound"), TEXT("Button Click Sound"), true);
		ClickSoundCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UAudioPanelWidget::OnClickSoundCheckChanged);
		SelectSoundCheckbox = AddCheckRow(Stack, TEXT("Chk_AudioSelectSound"), TEXT("Building Select Sound"), true);
		SelectSoundCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UAudioPanelWidget::OnSelectSoundCheckChanged);
		QuitConfirmSoundCheckbox = AddCheckRow(Stack, TEXT("Chk_AudioQuitConfirmSound"), TEXT("Confirm Dialog Sound"), true);
		QuitConfirmSoundCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UAudioPanelWidget::OnQuitConfirmSoundCheckChanged);
		// Checked = everything muted, so it starts unchecked (nothing is muted at first).
		MuteAllCheckbox = AddCheckRow(Stack, TEXT("Chk_AudioMuteAll"), TEXT("Mute All"), false);
		MuteAllCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UAudioPanelWidget::OnMuteAllCheckChanged);

		UHorizontalBox* VolumeRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("AudioVolumeRow"));
		UTextBlock* VolumeLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_AudioVolume"));
		VolumeLabel->SetText(FText::FromString(TEXT("Volume")));
		VolumeLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(VolumeLabel);
		UHorizontalBoxSlot* VolumeLabelSlot = VolumeRow->AddChildToHorizontalBox(VolumeLabel);
		VolumeLabelSlot->SetVerticalAlignment(VAlign_Center);
		VolumeLabelSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		VolumeSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("Slider_AudioVolume"));
		VolumeSlider->SetMinValue(0.f);
		VolumeSlider->SetMaxValue(1.f);
		VolumeSlider->SetValue(1.f);
		StartStyle::DarkSlider(VolumeSlider);
		VolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UAudioPanelWidget::OnVolumeChanged);
		USizeBox* VolumeSliderSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		VolumeSliderSize->SetWidthOverride(FMath::Max(60.f, ContentWidth - 60.f));
		VolumeSliderSize->SetContent(VolumeSlider);
		VolumeRow->AddChildToHorizontalBox(VolumeSliderSize)->SetVerticalAlignment(VAlign_Center);
		Stack->AddChildToVerticalBox(VolumeRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		// The rows themselves are built by RefreshAudioList (one per file under Data/Audio/).
		FileListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("AudioFileListBox"));
		USizeBox* FileListSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("AudioFileListSizeBox"));
		FileListSize->SetHeightOverride(AudioFileListHeight);
		FileListSize->SetWidthOverride(ContentWidth);
		FileListSize->SetContent(FileListBox);
		Stack->AddChildToVerticalBox(FileListSize);
	}
	return Super::RebuildWidget();
}

void UAudioPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Refresh();
}

void UAudioPanelWidget::Refresh()
{
	if (ANavPlayerController* PC = GetController())
	{
		// These settings live on the controller, not on any screen, so they survive the screen being torn down
		// and rebuilt; the check boxes were created with fixed values and must be brought in line with them.
		if (ClickSoundCheckbox)
		{
			ClickSoundCheckbox->SetIsChecked(PC->IsButtonClickSoundEnabled());
		}
		if (SelectSoundCheckbox)
		{
			SelectSoundCheckbox->SetIsChecked(PC->IsBuildingSelectSoundEnabled());
		}
		if (QuitConfirmSoundCheckbox)
		{
			QuitConfirmSoundCheckbox->SetIsChecked(PC->IsQuitConfirmSoundEnabled());
		}
		if (VolumeSlider)
		{
			VolumeSlider->SetValue(PC->GetMasterVolume());
		}
	}
	RefreshAudioList();
}

void UAudioPanelWidget::SetContentWidth(float Width)
{
	if (FMath::IsNearlyEqual(Width, ContentWidth, 0.5f))
	{
		return;
	}
	ContentWidth = Width;
	if (StopAllButton)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(StopAllButton->GetContent()))
		{
			SizeBox->SetWidthOverride(Width);
		}
	}
	if (FileListBox)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(FileListBox->GetParent()))
		{
			SizeBox->SetWidthOverride(Width);
		}
	}
	if (VolumeSlider)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(VolumeSlider->GetParent()))
		{
			SizeBox->SetWidthOverride(FMath::Max(60.f, Width - 60.f));
		}
	}
	// Rows are rebuilt from scratch with the new width baked in - cheap for a list this small.
	RefreshAudioList();
}

void UAudioPanelWidget::RefreshAudioList()
{
	ANavPlayerController* PC = GetController();
	if (!PC || !FileListBox)
	{
		return;
	}

	FileListBox->ClearChildren();
	for (const FString& FileName : PC->GetAvailableAudioFiles())
	{
		UAudioFileRowWidget* Row = CreateWidget<UAudioFileRowWidget>(PC, UAudioFileRowWidget::StaticClass());
		if (!Row)
		{
			continue;
		}
		Row->SetFileName(FileName, PC->IsAudioFilePlaying(FileName));
		Row->SetWidth(ContentWidth);
		Row->OnPlayClicked.AddUniqueDynamic(this, &UAudioPanelWidget::HandleRowPlayClicked);
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(FileListBox->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}
	}
}

void UAudioPanelWidget::OnStopAllClicked()
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->StopAllAudio();
		RefreshAudioList();
	}
}

void UAudioPanelWidget::HandleRowPlayClicked(FString FileName)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->PlayAudioFile(FileName);
		RefreshAudioList();
	}
}

void UAudioPanelWidget::OnClickSoundCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetButtonClickSoundEnabled(bIsChecked);
	}
}

void UAudioPanelWidget::OnSelectSoundCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetBuildingSelectSoundEnabled(bIsChecked);
	}
}

void UAudioPanelWidget::OnQuitConfirmSoundCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetQuitConfirmSoundEnabled(bIsChecked);
	}
}

void UAudioPanelWidget::OnMuteAllCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		// Checked means "muted", the opposite of the three "X Sound" check boxes (checked when ENABLED), so the
		// three get the inverse and are synced to match.
		PC->SetAllUiSoundsEnabled(!bIsChecked);
	}
	if (ClickSoundCheckbox)
	{
		ClickSoundCheckbox->SetIsChecked(!bIsChecked);
	}
	if (SelectSoundCheckbox)
	{
		SelectSoundCheckbox->SetIsChecked(!bIsChecked);
	}
	if (QuitConfirmSoundCheckbox)
	{
		QuitConfirmSoundCheckbox->SetIsChecked(!bIsChecked);
	}
}

void UAudioPanelWidget::OnVolumeChanged(float NewValue)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetMasterVolume(NewValue);
	}
}
