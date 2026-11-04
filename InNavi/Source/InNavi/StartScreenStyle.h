#pragma once

#include "CoreMinimal.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
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

	// A selected / active button or row: the page's amber - a dark gold fill with an orange outline.
	inline FButtonStyle ActiveButton(float Radius = 8.f)
	{
		return Button(Rgb(92, 62, 18), Rgb(112, 76, 22), Rgb(72, 48, 14), Rgb(230, 150, 30), Radius);
	}

	// A toggle that is switched on: the bright orange of the interior editor's 3D View button (the off look is SecondaryButton).
	inline FButtonStyle OrangeButton(float Radius = 8.f)
	{
		return Button(Rgb(196, 112, 8), Rgb(220, 130, 14), Rgb(160, 90, 4), Rgb(196, 112, 8), Radius);
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

	// A button filled with one status colour (green / red) that keeps the outlined shape of the others; hover and press shade it.
	inline FButtonStyle StatusButton(const FLinearColor& Fill)
	{
		auto Shade = [](const FLinearColor& Color, float Factor) { return FLinearColor(FMath::Min(Color.R * Factor, 1.f), FMath::Min(Color.G * Factor, 1.f), FMath::Min(Color.B * Factor, 1.f), Color.A); };
		return Button(Fill, Shade(Fill, 1.2f), Shade(Fill, 0.8f), Shade(Fill, 1.3f));
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

	// A check box in the page's colours: the engine's own box and check mark, tinted - light grey when empty, blue when checked.
	inline void DarkCheckBox(UCheckBox* Box)
	{
		FCheckBoxStyle Style = Box->GetWidgetStyle();
		auto Tint = [](FSlateBrush& Brush, const FLinearColor& Color) { Brush.TintColor = FSlateColor(Color); };
		Tint(Style.UncheckedImage, Rgb(150, 170, 196));
		Tint(Style.UncheckedHoveredImage, Rgb(214, 228, 246));
		Tint(Style.UncheckedPressedImage, Rgb(120, 140, 166));
		Tint(Style.CheckedImage, Rgb(66, 150, 255));
		Tint(Style.CheckedHoveredImage, Rgb(120, 182, 255));
		Tint(Style.CheckedPressedImage, Rgb(46, 120, 225));
		Tint(Style.UndeterminedImage, Rgb(150, 170, 196));
		Tint(Style.UndeterminedHoveredImage, Rgb(214, 228, 246));
		Tint(Style.UndeterminedPressedImage, Rgb(120, 140, 166));
		Box->SetWidgetStyle(Style);
	}

	// A slider's bar and handle in the page's colours.
	inline void DarkSlider(USlider* Slider)
	{
		Slider->SetSliderBarColor(Rgb(54, 70, 92));
		Slider->SetSliderHandleColor(Rgb(66, 150, 255));
	}

	// A text button in a menu bar (File, Window, ...): no box until the mouse is over it.
	inline FButtonStyle MenuButton()
	{
		return Button(FLinearColor::Transparent, Rgb(31, 51, 80), Rgb(22, 36, 58), FLinearColor::Transparent, 6.f);
	}

	// A button row inside a menu's panel: a box of its own, so it reads as a button.
	inline FButtonStyle MenuRowButton()
	{
		return Button(Rgb(24, 38, 60), Rgb(33, 54, 84), Rgb(18, 30, 50), Rgb(46, 66, 94), 6.f);
	}

	// The panel a menu opens.
	inline FSlateBrush MenuPanel()
	{
		return Rounded(Rgb(17, 31, 50), Rgb(54, 70, 92), 8.f);
	}

	inline FButtonStyle SaveButtonStyle()
	{
		return Button(Rgb(22, 42, 62), Rgb(31, 58, 84), Rgb(16, 32, 48), Rgb(54, 84, 108), 8.f);
	}

	// The menu bar's Save button: a boxed button, a small square indicator in front of the word "Save". The indicator is
	// yellow while the project has unsaved changes and green once it is saved (see PaintSaveIndicator).
	inline UButton* SaveBarButton(UWidgetTree* Tree, UBorder*& OutOuter, UBorder*& OutInner, float WidthPixels)
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_Save"));
		Button->SetStyle(SaveButtonStyle());
		Button->SetBackgroundColor(FLinearColor::White);

		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row_Save"));

		OutOuter = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Img_SaveIndicator"));
		OutOuter->SetBrushColor(FLinearColor::White);
		OutOuter->SetPadding(FMargin(3.f * DesignToUi));
		USizeBox* InnerSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size_SaveIndicator"));
		InnerSize->SetWidthOverride(6.f * DesignToUi);
		InnerSize->SetHeightOverride(6.f * DesignToUi);
		OutInner = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Img_SaveIndicatorCore"));
		OutInner->SetBrushColor(FLinearColor::White);
		InnerSize->SetContent(OutInner);
		OutOuter->SetContent(InnerSize);
		UHorizontalBoxSlot* IndicatorSlot = Row->AddChildToHorizontalBox(OutOuter);
		IndicatorSlot->SetVerticalAlignment(VAlign_Center);
		IndicatorSlot->SetPadding(FMargin(0.f, 0.f, 8.f * DesignToUi, 0.f));

		UTextBlock* Label = Text(Tree, TEXT("Txt_Save"), TEXT("Save"), 14.f, Rgb(226, 238, 252), true);
		Row->AddChildToHorizontalBox(Label)->SetVerticalAlignment(VAlign_Center);

		USizeBox* Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size_Save"));
		Size->SetMinDesiredWidth(WidthPixels);
		Size->SetMinDesiredHeight(30.f * DesignToUi);
		if (USizeBoxSlot* RowSlot = Cast<USizeBoxSlot>(Size->SetContent(Row)))
		{
			RowSlot->SetHorizontalAlignment(HAlign_Center);
			RowSlot->SetVerticalAlignment(VAlign_Center);
		}
		Button->SetContent(Size);
		return Button;
	}

	inline void PaintSaveIndicator(UBorder* Outer, UBorder* Inner, bool bUnsaved)
	{
		const FLinearColor Color = bUnsaved ? Rgb(245, 184, 30) : Rgb(52, 199, 120);
		if (Outer)
		{
			Outer->SetBrush(Rounded(FLinearColor::Transparent, Color, 3.f, 1.5f));
		}
		if (Inner)
		{
			Inner->SetBrush(Rounded(Color, FLinearColor::Transparent, 1.5f, 0.f));
		}
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
