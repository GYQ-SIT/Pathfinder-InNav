#pragma once

#include "CoreMinimal.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

// Colours, sizes and small builders shared by the start-side screens (top bar, project library, project cards, New
// project dialog), taken from the design team's page. Header-only and inline so that no two .cpp files of a unity build
// each define their own copy.
//
// The page's sizes are browser pixels. The project's UI scale is 0.77 (see DefaultEngine.ini), so a size is multiplied by
// DesignToUi to come out the same on screen; font sizes are points drawn at 96 DPI, 3/4 of a pixel size each.
namespace StartStyle
{
	inline constexpr float DesignToUi = 1.f / 0.77f;
	inline constexpr float PixelsToFont = 0.75f;

	inline FLinearColor Rgb(uint8 R, uint8 G, uint8 B, uint8 A = 255)
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor(R, G, B));
		Color.A = A / 255.f;
		return Color;
	}

	// A rounded box. Radii are TopLeft, TopRight, BottomRight, BottomLeft, in design pixels.
	inline FSlateBrush Rounded(const FLinearColor& Fill, const FLinearColor& Outline, float TopLeft, float TopRight, float BottomRight, float BottomLeft, float OutlineWidth = 1.f)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(TopLeft * DesignToUi, TopRight * DesignToUi, BottomRight * DesignToUi, BottomLeft * DesignToUi);
		Brush.OutlineSettings.Color = FSlateColor(Outline);
		Brush.OutlineSettings.Width = OutlineWidth;
		return Brush;
	}

	inline FSlateBrush Rounded(const FLinearColor& Fill, const FLinearColor& Outline, float Radius, float OutlineWidth = 1.f)
	{
		return Rounded(Fill, Outline, Radius, Radius, Radius, Radius, OutlineWidth);
	}

	inline UTextBlock* Text(UWidgetTree* Tree, const FString& Name, const FString& Value, float PixelSize, const FLinearColor& Color, bool bBold = false, int32 Spacing = 0)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
		Block->SetText(FText::FromString(Value));
		Block->SetColorAndOpacity(FSlateColor(Color));
		FSlateFontInfo Font = Block->GetFont();
		Font.Size = FMath::RoundToInt(PixelSize * DesignToUi * PixelsToFont);
		Font.LetterSpacing = Spacing;
		if (bBold)
		{
			Font.TypefaceFontName = TEXT("Bold");
		}
		Block->SetFont(Font);
		return Block;
	}

	inline FButtonStyle Button(const FLinearColor& Fill, const FLinearColor& Hover, const FLinearColor& Pressed, const FLinearColor& Outline, float Radius = 6.f)
	{
		FButtonStyle Style;
		Style.Normal = Rounded(Fill, Outline, Radius);
		Style.Hovered = Rounded(Hover, Outline, Radius);
		Style.Pressed = Rounded(Pressed, Outline, Radius);
		Style.Disabled = Rounded(Fill * 0.45f, Outline * 0.6f, Radius);
		Style.NormalPadding = FMargin(0.f);
		Style.PressedPadding = FMargin(0.f);
		return Style;
	}

	// The two button looks of the page: the blue primary one and the outlined secondary one.
	inline FButtonStyle PrimaryButton()
	{
		return Button(Rgb(21, 110, 255), Rgb(46, 132, 255), Rgb(10, 88, 214), Rgb(21, 110, 255));
	}
	inline FButtonStyle SecondaryButton()
	{
		return Button(Rgb(27, 41, 63), Rgb(38, 56, 84), Rgb(22, 34, 54), Rgb(61, 78, 104));
	}

	// Gives a drop-down the page's dark look: a dark button that keeps the engine's arrow (in a light colour), and a dark list
	// whose rows light up blue. Its items are made by the owner (their text is white), so the list must not be white.
	inline void DarkCombo(UComboBoxString* Combo)
	{
		FComboBoxStyle Style = Combo->GetWidgetStyle();
		Style.ComboButtonStyle.ButtonStyle = Button(Rgb(15, 26, 42), Rgb(24, 40, 62), Rgb(12, 21, 34), Rgb(54, 70, 92), 6.f);
		Style.ComboButtonStyle.DownArrowImage.TintColor = FSlateColor(Rgb(170, 190, 214));
		Style.ComboButtonStyle.MenuBorderBrush = Rounded(Rgb(15, 26, 42), Rgb(54, 70, 92), 6.f);
		Style.ComboButtonStyle.MenuBorderPadding = FMargin(4.f);
		Combo->SetWidgetStyle(Style);

		FTableRowStyle Row = Combo->GetItemStyle();
		const FSlateBrush Dark = Rounded(Rgb(15, 26, 42), FLinearColor::Transparent, 4.f, 0.f);
		const FSlateBrush Hover = Rounded(Rgb(31, 51, 80), FLinearColor::Transparent, 4.f, 0.f);
		const FSlateBrush Blue = Rounded(Rgb(21, 110, 255), FLinearColor::Transparent, 4.f, 0.f);
		Row.EvenRowBackgroundBrush = Dark;
		Row.OddRowBackgroundBrush = Dark;
		Row.EvenRowBackgroundHoveredBrush = Hover;
		Row.OddRowBackgroundHoveredBrush = Hover;
		Row.ActiveBrush = Blue;
		Row.ActiveHoveredBrush = Blue;
		Row.InactiveBrush = Blue;
		Row.InactiveHoveredBrush = Blue;
		Row.TextColor = FSlateColor(Rgb(226, 238, 252));
		Row.SelectedTextColor = FSlateColor(FLinearColor::White);
		Combo->SetItemStyle(Row);
	}

	inline FEditableTextBoxStyle Field(float PixelSize)
	{
		FEditableTextBoxStyle Style;
		const FLinearColor Fill = Rgb(15, 26, 42);
		const FLinearColor Border = Rgb(54, 70, 92);
		Style.BackgroundImageNormal = Rounded(Fill, Border, 5.f);
		Style.BackgroundImageHovered = Rounded(Fill, Rgb(74, 90, 113), 5.f);
		Style.BackgroundImageFocused = Rounded(Fill, Rgb(217, 119, 6), 5.f);
		Style.BackgroundImageReadOnly = Rounded(Fill, Border, 5.f);
		Style.Padding = FMargin(10.f * DesignToUi, 9.f * DesignToUi);
		Style.ForegroundColor = FSlateColor(Rgb(244, 247, 252));
		Style.FocusedForegroundColor = FSlateColor(Rgb(244, 247, 252));
		Style.TextStyle.Font.Size = FMath::RoundToInt(PixelSize * DesignToUi * PixelsToFont);
		Style.TextStyle.ColorAndOpacity = FSlateColor(Rgb(244, 247, 252));
		return Style;
	}
}
