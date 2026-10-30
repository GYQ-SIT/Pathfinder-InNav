#pragma once

#include "CoreMinimal.h"
#include "MobilePreferences.h"
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
	// The colours of the open theme (light, or the prototype's night theme). They are plain variables, set together by ApplyAppearance, and the
	// pages read them when they are built, so a change of theme builds the pages again. Light is what they start as.
	//   White: what cards, the sheet and the tab bar are made of (the surface); PageBg: what lies behind them; OnBlue: text and ticks on a Blue fill.
	inline FLinearColor Blue = StartStyle::Rgb(30, 100, 240);
	inline FLinearColor BlueSoft = StartStyle::Rgb(226, 236, 253);
	inline FLinearColor Ink = StartStyle::Rgb(17, 24, 39);
	inline FLinearColor Grey = StartStyle::Rgb(107, 114, 128);
	inline FLinearColor Line = StartStyle::Rgb(229, 231, 235);
	inline FLinearColor White = StartStyle::Rgb(255, 255, 255);
	inline FLinearColor PageBg = StartStyle::Rgb(255, 255, 255);
	inline FLinearColor OnBlue = StartStyle::Rgb(255, 255, 255);
	inline FLinearColor FieldEdge = StartStyle::Rgb(203, 213, 225);
	inline FLinearColor SoftPanel = StartStyle::Rgb(241, 245, 249);
	inline FLinearColor IconGrey = StartStyle::Rgb(209, 213, 219);
	inline FLinearColor MapBackground = StartStyle::Rgb(222, 231, 238);
	inline FLinearColor Red = StartStyle::Rgb(185, 28, 28);
	inline FLinearColor RedSoft = StartStyle::Rgb(254, 226, 226);
	inline FLinearColor Green = StartStyle::Rgb(22, 101, 52);
	inline FLinearColor GreenSoft = StartStyle::Rgb(220, 252, 231);
	// What a row shows under a finger that is over it.
	inline FLinearColor TapHover = FLinearColor(0.f, 0.f, 0.f, 0.04f);

	// What the map is painted with.
	struct FMapColours
	{
		FLinearColor Grass = StartStyle::Rgb(209, 235, 200);
		FLinearColor Water = StartStyle::Rgb(186, 220, 245);
		FLinearColor Slab = StartStyle::Rgb(148, 163, 184);
		FLinearColor BuildingTop = StartStyle::Rgb(245, 247, 250);
		FLinearColor Floor = StartStyle::Rgb(248, 250, 252);
		FLinearColor Edge = StartStyle::Rgb(100, 116, 139);
		FLinearColor RoomEdge = StartStyle::Rgb(148, 163, 184);
		FLinearColor Room = StartStyle::Rgb(226, 232, 240);
		FLinearColor Learning = StartStyle::Rgb(219, 234, 254);
		FLinearColor Facilities = StartStyle::Rgb(209, 250, 229);
		FLinearColor Dining = StartStyle::Rgb(254, 243, 199);
		FLinearColor Highlight = StartStyle::Rgb(191, 219, 254);
	};
	inline FMapColours MapColours;

	// How much bigger the text is when "Larger text" is on (the prototype raises its base size from 16 to 18).
	inline float TextScale = 1.f;
	// What ApplyAppearance last set, so a screen can tell whether it was built in the look that is open now.
	inline EMobileTheme CurrentTheme = EMobileTheme::Light;
	inline bool bCurrentLarger = false;

	inline void ApplyAppearance(EMobileTheme Theme, bool bLargerText)
	{
		TextScale = bLargerText ? 1.125f : 1.f;
		CurrentTheme = Theme;
		bCurrentLarger = bLargerText;
		if (Theme == EMobileTheme::Night)
		{
			Blue = StartStyle::Rgb(120, 173, 255);
			BlueSoft = StartStyle::Rgb(42, 63, 95);
			Ink = StartStyle::Rgb(237, 243, 252);
			Grey = StartStyle::Rgb(178, 193, 214);
			Line = StartStyle::Rgb(60, 77, 101);
			White = StartStyle::Rgb(25, 36, 55);
			PageBg = StartStyle::Rgb(16, 24, 39);
			OnBlue = StartStyle::Rgb(15, 23, 42);
			FieldEdge = StartStyle::Rgb(81, 99, 125);
			SoftPanel = StartStyle::Rgb(36, 50, 71);
			IconGrey = StartStyle::Rgb(81, 99, 125);
			MapBackground = StartStyle::Rgb(31, 45, 65);
			Red = StartStyle::Rgb(253, 164, 175);
			RedSoft = StartStyle::Rgb(74, 34, 48);
			Green = StartStyle::Rgb(134, 239, 172);
			GreenSoft = StartStyle::Rgb(29, 59, 42);
			TapHover = FLinearColor(1.f, 1.f, 1.f, 0.06f);
			FMapColours Night;
			Night.Grass = StartStyle::Rgb(36, 61, 52);
			Night.Water = StartStyle::Rgb(38, 70, 92);
			Night.Slab = StartStyle::Rgb(71, 85, 105);
			Night.BuildingTop = StartStyle::Rgb(48, 65, 90);
			Night.Floor = StartStyle::Rgb(37, 51, 72);
			Night.Edge = StartStyle::Rgb(148, 163, 184);
			Night.RoomEdge = StartStyle::Rgb(100, 116, 139);
			Night.Room = StartStyle::Rgb(48, 65, 90);
			Night.Learning = StartStyle::Rgb(43, 67, 110);
			Night.Facilities = StartStyle::Rgb(36, 84, 72);
			Night.Dining = StartStyle::Rgb(94, 76, 38);
			Night.Highlight = StartStyle::Rgb(55, 90, 150);
			MapColours = Night;
		}
		else
		{
			Blue = StartStyle::Rgb(30, 100, 240);
			BlueSoft = StartStyle::Rgb(226, 236, 253);
			Ink = StartStyle::Rgb(17, 24, 39);
			Grey = StartStyle::Rgb(107, 114, 128);
			Line = StartStyle::Rgb(229, 231, 235);
			White = StartStyle::Rgb(255, 255, 255);
			PageBg = StartStyle::Rgb(255, 255, 255);
			OnBlue = StartStyle::Rgb(255, 255, 255);
			FieldEdge = StartStyle::Rgb(203, 213, 225);
			SoftPanel = StartStyle::Rgb(241, 245, 249);
			IconGrey = StartStyle::Rgb(209, 213, 219);
			MapBackground = StartStyle::Rgb(222, 231, 238);
			Red = StartStyle::Rgb(185, 28, 28);
			RedSoft = StartStyle::Rgb(254, 226, 226);
			Green = StartStyle::Rgb(22, 101, 52);
			GreenSoft = StartStyle::Rgb(220, 252, 231);
			TapHover = FLinearColor(0.f, 0.f, 0.f, 0.04f);
			MapColours = FMapColours();
		}
	}

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
		Font.Size = FMath::RoundToInt(PixelSize * TextScale * StartStyle::PixelsToFont);
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
		Style.BackgroundImageNormal = Rounded(White, FieldEdge, 12.f);
		Style.BackgroundImageHovered = Rounded(White, FieldEdge * 0.8f, 12.f);
		Style.BackgroundImageFocused = Rounded(White, Blue, 12.f, 1.5f);
		Style.BackgroundImageReadOnly = Rounded(White, Line, 12.f);
		Style.Padding = FMargin(14.f, 12.f);
		Style.ForegroundColor = FSlateColor(Ink);
		Style.FocusedForegroundColor = FSlateColor(Ink);
		Style.TextStyle.Font.Size = FMath::RoundToInt(PixelSize * TextScale * StartStyle::PixelsToFont);
		Style.TextStyle.ColorAndOpacity = FSlateColor(Ink);
		return Style;
	}
}
