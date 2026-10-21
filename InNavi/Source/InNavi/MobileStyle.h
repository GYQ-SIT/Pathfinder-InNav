#pragma once

#include "CoreMinimal.h"
#include "StartScreenStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Styling/SlateTypes.h"

// Colours, sizes and small builders shared by the phone app's screens, taken from the design team's prototype. Header-only and inline, like
// StartScreenStyle.h, so no two .cpp files of a unity build each define their own copy.
//
// The phone screen is laid out in plain design pixels (375 wide) and scaled to the window as a whole, so none of these multiply by the
// desktop's UI scale; where they call StartStyle (which does) they hand it sizes that cancel it.
namespace MobileStyle
{
	inline const FLinearColor Blue = StartStyle::Rgb(30, 100, 240);
	inline const FLinearColor BlueSoft = StartStyle::Rgb(226, 236, 253);
	inline const FLinearColor Ink = StartStyle::Rgb(17, 24, 39);
	inline const FLinearColor Grey = StartStyle::Rgb(107, 114, 128);
	inline const FLinearColor Line = StartStyle::Rgb(229, 231, 235);
	inline const FLinearColor White = StartStyle::Rgb(255, 255, 255);
	inline const FLinearColor MapBackground = StartStyle::Rgb(222, 231, 238);
	inline const FLinearColor Red = StartStyle::Rgb(185, 28, 28);
	inline const FLinearColor RedSoft = StartStyle::Rgb(254, 226, 226);

	inline FSlateBrush Rounded(const FLinearColor& Fill, const FLinearColor& Outline, float Radius, float OutlineWidth = 1.f)
	{
		return StartStyle::Rounded(Fill, Outline, Radius / StartStyle::DesignToUi, OutlineWidth);
	}

	inline FSlateBrush RoundedTop(const FLinearColor& Fill, float Radius)
	{
		const float R = Radius / StartStyle::DesignToUi;
		return StartStyle::Rounded(Fill, FLinearColor::Transparent, R, R, 0.f, 0.f, 0.f);
	}

	inline void ApplyFont(UTextBlock* Block, float PixelSize, bool bBold)
	{
		if (!Block)
		{
			return;
		}
		FSlateFontInfo Font = Block->GetFont();
		Font.Size = FMath::RoundToInt(PixelSize * StartStyle::PixelsToFont);
		if (bBold)
		{
			Font.TypefaceFontName = TEXT("Bold");
		}
		Block->SetFont(Font);
	}

	inline UTextBlock* MakeText(UWidgetTree* Tree, const FString& Name, const FString& Value, float PixelSize, const FLinearColor& Color, bool bBold = false)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
		Block->SetText(FText::FromString(Value));
		Block->SetColorAndOpacity(FSlateColor(Color));
		ApplyFont(Block, PixelSize, bBold);
		return Block;
	}

	// A rounded button with a centred label.
	inline UButton* MakeButton(UWidgetTree* Tree, const FString& Name, const FString& Label, const FLinearColor& Fill, const FLinearColor& TextColor, float Radius, float Height, UTextBlock** OutLabel = nullptr, float HorizontalPadding = 16.f)
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
		Button->SetStyle(StartStyle::Button(Fill, Fill * 0.94f, Fill * 0.86f, Line, Radius / StartStyle::DesignToUi));
		Button->SetBackgroundColor(FLinearColor::White);
		UTextBlock* Text = MakeText(Tree, Name + TEXT("_Label"), Label, 14.f, TextColor, true);
		Text->SetJustification(ETextJustify::Center);
		USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(Name + TEXT("_Size")));
		Box->SetMinDesiredHeight(Height);
		UBorder* Pad = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Name + TEXT("_Pad")));
		Pad->SetBrushColor(FLinearColor::Transparent);
		Pad->SetPadding(FMargin(HorizontalPadding, 0.f));
		Pad->SetVerticalAlignment(VAlign_Center);
		Pad->SetHorizontalAlignment(HAlign_Center);
		Pad->SetContent(Text);
		Box->SetContent(Pad);
		Button->SetContent(Box);
		if (OutLabel)
		{
			*OutLabel = Text;
		}
		return Button;
	}

	// A text field with a rounded outline, in the prototype's look.
	inline FEditableTextBoxStyle SearchField(float PixelSize)
	{
		FEditableTextBoxStyle Style;
		Style.BackgroundImageNormal = Rounded(White, StartStyle::Rgb(203, 213, 225), 12.f);
		Style.BackgroundImageHovered = Rounded(White, StartStyle::Rgb(148, 163, 184), 12.f);
		Style.BackgroundImageFocused = Rounded(White, Blue, 12.f, 1.5f);
		Style.BackgroundImageReadOnly = Rounded(White, Line, 12.f);
		Style.Padding = FMargin(14.f, 12.f);
		Style.ForegroundColor = FSlateColor(Ink);
		Style.FocusedForegroundColor = FSlateColor(Ink);
		Style.TextStyle.Font.Size = FMath::RoundToInt(PixelSize * StartStyle::PixelsToFont);
		Style.TextStyle.ColorAndOpacity = FSlateColor(Ink);
		return Style;
	}
}
