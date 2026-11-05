#include "MobileProfilePageWidget.h"

#include "MobileStyle.h"
#include "MobileTapTarget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace MobileStyle;

TSharedRef<SWidget> UMobileProfilePageWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ProfileScroll"));
		Tree->RootWidget = Scroll;
		UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ProfileContent"));
		UBorder* Pad = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Pad->SetBrushColor(FLinearColor::Transparent);
		Pad->SetPadding(FMargin(16.f, 22.f, 16.f, 24.f));
		Pad->SetContent(Content);
		Scroll->AddChild(Pad);

		auto Add = [Content](UWidget* Child, const FMargin& Margin)
		{
			Content->AddChildToVerticalBox(Child)->SetPadding(Margin);
		};
		auto Wrapped = [Tree](const TCHAR* Name, const FString& Value, float Size, const FLinearColor& Color, bool bBold)
		{
			UTextBlock* Text = MakeText(Tree, Name, Value, Size, Color, bBold);
			Text->SetAutoWrapText(true);
			return Text;
		};

		Add(MakeText(Tree, TEXT("Txt_ProfileTitle"), TEXT("Make it yours"), 26.f, Ink, true), FMargin(0.f));
		Add(Wrapped(TEXT("Txt_ProfileSubtitle"), TEXT("Preferences stay on this device."), 13.f, Grey, false), FMargin(0.f, 6.f, 0.f, 0.f));

		// The visitor card: there is no account.
		UBorder* AvatarCircle = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		AvatarCircle->SetBrush(Rounded(BlueSoft, FLinearColor::Transparent, 20.f, 0.f));
		AvatarCircle->SetBrushColor(FLinearColor::White);
		AvatarCircle->SetHorizontalAlignment(HAlign_Center);
		AvatarCircle->SetVerticalAlignment(VAlign_Center);
		AvatarCircle->SetContent(MakeText(Tree, FString(), TEXT("V"), 16.f, Blue, true));
		USizeBox* AvatarSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		AvatarSize->SetWidthOverride(40.f);
		AvatarSize->SetHeightOverride(40.f);
		AvatarSize->SetContent(AvatarCircle);
		UVerticalBox* WhoTexts = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		WhoTexts->AddChildToVerticalBox(MakeText(Tree, TEXT("Txt_ProfileName"), TEXT("Visitor"), 15.f, Ink, true));
		WhoTexts->AddChildToVerticalBox(MakeText(Tree, TEXT("Txt_ProfileAccount"), TEXT("No account needed for this prototype"), 12.f, Grey))->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		UHorizontalBox* Who = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Who->AddChildToHorizontalBox(AvatarSize)->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* WhoTextSlot = Who->AddChildToHorizontalBox(WhoTexts);
		WhoTextSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		WhoTextSlot->SetVerticalAlignment(VAlign_Center);
		UBorder* WhoCard = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ProfileCard"));
		WhoCard->SetBrush(Rounded(White, Line, 14.f));
		WhoCard->SetBrushColor(FLinearColor::White);
		WhoCard->SetPadding(FMargin(14.f));
		WhoCard->SetContent(Who);
		Add(WhoCard, FMargin(0.f, 18.f, 0.f, 0.f));

		// Appearance: a light or night theme, and larger text.
		Add(MakeText(Tree, TEXT("Txt_ProfileAppearance"), TEXT("Appearance"), 15.f, Ink, true), FMargin(0.f, 24.f, 0.f, 0.f));
		Add(Wrapped(TEXT("Txt_ProfileAppearanceNote"), TEXT("Switch the full visual system without changing your route."), 12.f, Grey, false), FMargin(0.f, 4.f, 0.f, 0.f));

		TWeakObjectPtr<UMobileProfilePageWidget> WeakThis(this);
		auto MakePill = [this, Tree, WeakThis](const TCHAR* Label, const FString& Payload, UBorder*& OutPill, UTextBlock*& OutLabel) -> UMobileTapTarget*
		{
			OutPill = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
			OutPill->SetBrushColor(FLinearColor::White);
			OutPill->SetPadding(FMargin(14.f, 9.f));
			OutLabel = MakeText(Tree, FString(), Label, 13.f, Ink, true);
			OutPill->SetContent(OutLabel);
			UMobileTapTarget* Target = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
			Target->Setup(OutPill, Payload, FLinearColor::Transparent, 18.f);
			Target->SetOnTapped([WeakThis](const FString& Chosen)
			{
				if (UMobileProfilePageWidget* Self = WeakThis.Get())
				{
					Self->ChooseTheme(Chosen == TEXT("night") ? EMobileTheme::Night : EMobileTheme::Light);
				}
			});
			return Target;
		};
		UHorizontalBox* Pills = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ProfileThemes"));
		Pills->AddChildToHorizontalBox(MakePill(TEXT("InNav light"), TEXT("light"), LightPill, LightLabel));
		Pills->AddChildToHorizontalBox(MakePill(TEXT("Night theme"), TEXT("night"), NightPill, NightLabel))->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		Add(Pills, FMargin(0.f, 12.f, 0.f, 0.f));

		// Larger text: a box and its label, all one tap.
		LargerTick = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		LargerTick->SetBrush(Rounded(White, FLinearColor::Transparent, 2.f, 0.f));
		LargerTick->SetBrushColor(FLinearColor::White);
		USizeBox* TickSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		TickSize->SetWidthOverride(8.f);
		TickSize->SetHeightOverride(8.f);
		TickSize->SetContent(LargerTick);
		LargerBox = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		LargerBox->SetBrushColor(FLinearColor::White);
		LargerBox->SetHorizontalAlignment(HAlign_Center);
		LargerBox->SetVerticalAlignment(VAlign_Center);
		LargerBox->SetContent(TickSize);
		USizeBox* BoxSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		BoxSize->SetWidthOverride(20.f);
		BoxSize->SetHeightOverride(20.f);
		BoxSize->SetContent(LargerBox);
		UHorizontalBox* LargerRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		LargerRow->AddChildToHorizontalBox(MakeText(Tree, TEXT("Txt_ProfileLarger"), TEXT("Larger text"), 14.f, Ink))->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* BoxSlot = LargerRow->AddChildToHorizontalBox(BoxSize);
		BoxSlot->SetHorizontalAlignment(HAlign_Right);
		BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BoxSlot->SetVerticalAlignment(VAlign_Center);
		UBorder* LargerPad = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		LargerPad->SetBrushColor(FLinearColor::Transparent);
		LargerPad->SetPadding(FMargin(0.f, 14.f));
		LargerPad->SetContent(LargerRow);
		UMobileTapTarget* Larger = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
		Larger->Setup(LargerPad, TEXT("larger"), FLinearColor::Transparent, 0.f);
		Larger->SetOnTapped([WeakThis](const FString&)
		{
			if (UMobileProfilePageWidget* Self = WeakThis.Get())
			{
				Self->ToggleLargerText();
			}
		});
		Add(Larger, FMargin(0.f, 6.f, 0.f, 0.f));

		USizeBox* Divider = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Divider->SetHeightOverride(1.f);
		UBorder* Rule = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Rule->SetBrushColor(Line);
		Divider->SetContent(Rule);
		Add(Divider, FMargin(0.f, 6.f, 0.f, 0.f));

		// Sound: the volume of the app's sounds, a slider and the number it stands for.
		Add(MakeText(Tree, TEXT("Txt_ProfileSound"), TEXT("Sound"), 15.f, Ink, true), FMargin(0.f, 20.f, 0.f, 0.f));
		Add(Wrapped(TEXT("Txt_ProfileSoundNote"), TEXT("How loud the app's sounds are."), 12.f, Grey, false), FMargin(0.f, 4.f, 0.f, 0.f));
		VolumeSlider = Tree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("Slider_ProfileVolume"));
		VolumeSlider->SetMinValue(0.f);
		VolumeSlider->SetMaxValue(1.f);
		VolumeSlider->SetStepSize(0.05f);
		VolumeSlider->SetValue(Volume);
		VolumeSlider->SetSliderBarColor(Line);
		VolumeSlider->SetSliderHandleColor(Blue);
		VolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UMobileProfilePageWidget::HandleVolumeSlider);
		VolumeSlider->OnMouseCaptureEnd.AddUniqueDynamic(this, &UMobileProfilePageWidget::HandleVolumeReleased);
		USizeBox* SliderSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SliderSize->SetHeightOverride(36.f);
		SliderSize->SetContent(VolumeSlider);
		VolumeText = MakeText(Tree, TEXT("Txt_ProfileVolume"), TEXT("100%"), 14.f, Ink, true);
		VolumeText->SetJustification(ETextJustify::Right);
		USizeBox* VolumeTextSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		VolumeTextSize->SetWidthOverride(48.f);
		VolumeTextSize->SetContent(VolumeText);
		UHorizontalBox* VolumeRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ProfileVolumeRow"));
		UHorizontalBoxSlot* SliderSlot = VolumeRow->AddChildToHorizontalBox(SliderSize);
		SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SliderSlot->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* VolumeTextSlot = VolumeRow->AddChildToHorizontalBox(VolumeTextSize);
		VolumeTextSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		VolumeTextSlot->SetVerticalAlignment(VAlign_Center);
		Add(VolumeRow, FMargin(0.f, 10.f, 0.f, 0.f));

		USizeBox* SoundDivider = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SoundDivider->SetHeightOverride(1.f);
		UBorder* SoundRule = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		SoundRule->SetBrushColor(Line);
		SoundDivider->SetContent(SoundRule);
		Add(SoundDivider, FMargin(0.f, 14.f, 0.f, 0.f));

		// About, and the reset.
		Add(MakeText(Tree, TEXT("Txt_ProfileAbout"), TEXT("About InNav"), 15.f, Ink, true), FMargin(0.f, 20.f, 0.f, 0.f));
		Add(Wrapped(TEXT("Txt_ProfileAboutText"), TEXT("Indoor navigation by Team Pathfinder. This interactive reference demonstrates interface behaviour for the engineering team."), 12.f, Grey, false), FMargin(0.f, 6.f, 0.f, 0.f));

		UBorder* ResetFace = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		ResetFace->SetBrush(Rounded(White, Blue, 12.f));
		ResetFace->SetBrushColor(FLinearColor::White);
		ResetFace->SetPadding(FMargin(14.f, 13.f));
		ResetFace->SetHorizontalAlignment(HAlign_Center);
		ResetFace->SetContent(MakeText(Tree, TEXT("Txt_ProfileReset"), TEXT("Reset saved places & preferences"), 14.f, Blue, true));
		UMobileTapTarget* Reset = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
		Reset->Setup(ResetFace, TEXT("reset"), FLinearColor::Transparent, 12.f);
		Reset->SetOnTapped([WeakThis](const FString&)
		{
			if (UMobileProfilePageWidget* Self = WeakThis.Get())
			{
				Self->TapReset();
			}
		});
		Add(Reset, FMargin(0.f, 18.f, 0.f, 0.f));

		StatusText = MakeText(Tree, TEXT("Txt_ProfileStatus"), TEXT(""), 12.f, Blue, true);
		StatusText->SetJustification(ETextJustify::Center);
		Add(StatusText, FMargin(0.f, 10.f, 0.f, 0.f));
		NoteText = Wrapped(TEXT("Txt_ProfileNote"), TEXT(""), 11.f, Grey, false);
		Add(NoteText, FMargin(0.f, 18.f, 0.f, 0.f));
		Refresh();
	}
	return Super::RebuildWidget();
}

void UMobileProfilePageWidget::SetPreferences(const FMobilePreferences& InPreferences)
{
	Theme = InPreferences.GetTheme();
	bLargerText = InPreferences.IsLargerText();
	Volume = InPreferences.GetVolume();
	Refresh();
}

void UMobileProfilePageWidget::SetNote(const FString& InNote)
{
	Note = InNote;
	Refresh();
}

UWidget* UMobileProfilePageWidget::FindNamedWidget(const FName& Name) const
{
	return WidgetTree ? WidgetTree->FindWidget(Name) : nullptr;
}

FString UMobileProfilePageWidget::GetStatusString() const
{
	return StatusText ? StatusText->GetText().ToString() : FString();
}

FString UMobileProfilePageWidget::GetNoteString() const
{
	return NoteText ? NoteText->GetText().ToString() : FString();
}

void UMobileProfilePageWidget::SetStatus(const FString& Text)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Text));
	}
}

void UMobileProfilePageWidget::ChooseTheme(EMobileTheme NewTheme)
{
	Theme = NewTheme;
	Refresh();
	SetStatus(TEXT("Appearance updated"));
	if (OnThemeChosen)
	{
		OnThemeChosen(NewTheme);
	}
}

void UMobileProfilePageWidget::ToggleLargerText()
{
	bLargerText = !bLargerText;
	Refresh();
	SetStatus(TEXT("Appearance updated"));
	if (OnLargerTextToggled)
	{
		OnLargerTextToggled(bLargerText);
	}
}

void UMobileProfilePageWidget::ChooseVolume(float NewVolume, bool bFinal)
{
	Volume = FMath::IsFinite(NewVolume) ? FMath::Clamp(NewVolume, 0.f, 1.f) : 1.f;
	Refresh();
	if (bFinal)
	{
		SetStatus(TEXT("Volume updated"));
	}
	if (OnVolumeChanged)
	{
		OnVolumeChanged(Volume, bFinal);
	}
}

void UMobileProfilePageWidget::HandleVolumeSlider(float NewValue)
{
	// Moving the slider is heard at once; it is kept when the finger is lifted (HandleVolumeReleased).
	if (!bRefreshing)
	{
		ChooseVolume(NewValue, false);
	}
}

void UMobileProfilePageWidget::HandleVolumeReleased()
{
	ChooseVolume(VolumeSlider ? VolumeSlider->GetValue() : Volume, true);
}

void UMobileProfilePageWidget::TapReset()
{
	SetStatus(TEXT("Saved places and preferences were reset"));
	if (OnReset)
	{
		OnReset();
	}
}

void UMobileProfilePageWidget::Refresh()
{
	if (NoteText)
	{
		NoteText->SetText(FText::FromString(Note));
	}
	// The chosen theme is a filled pill, the other an outlined one.
	auto Style = [](UBorder* Pill, UTextBlock* Label, bool bChosen)
	{
		if (Pill)
		{
			Pill->SetBrush(Rounded(bChosen ? Blue : White, bChosen ? Blue : Line, 18.f));
		}
		if (Label)
		{
			Label->SetColorAndOpacity(FSlateColor(bChosen ? OnBlue : Ink));
		}
	};
	Style(LightPill, LightLabel, Theme == EMobileTheme::Light);
	Style(NightPill, NightLabel, Theme == EMobileTheme::Night);
	// The slider stands where the volume is (setting it here does not count as the person moving it).
	TGuardValue<bool> Refreshing(bRefreshing, true);
	if (VolumeSlider && !FMath::IsNearlyEqual(VolumeSlider->GetValue(), Volume, 0.001f))
	{
		VolumeSlider->SetValue(Volume);
	}
	if (VolumeText)
	{
		VolumeText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Volume * 100.f))));
	}
	if (LargerBox)
	{
		LargerBox->SetBrush(Rounded(bLargerText ? Blue : White, bLargerText ? Blue : Grey, 5.f));
	}
	if (LargerTick)
	{
		LargerTick->SetBrush(Rounded(bLargerText ? OnBlue : FLinearColor::Transparent, FLinearColor::Transparent, 2.f, 0.f));
	}
}
