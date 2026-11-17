#pragma once

#include "CoreMinimal.h"
#include "MobileStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

// Small icons for the phone app: the design team's line icons as textures (/Game/ImageAssets/Mobile/Mobile_<Name>, white on transparent, made from
// the SVGs by scripts/render-mobile-icons.py) tinted to the colour asked for, so the same icon is right in the light and the night theme. While a
// texture is not imported (a test, a fresh checkout), the icon is drawn from shapes instead, so the screens always have something to show.
// Header-only and inline, like MobileStyle. Each icon is a square of Size design pixels in one colour and gives back the widget to put in a layout.
namespace MobileIcons
{
	// The texture of an icon, or null when it is not there. Looked for once: a name that is not there is not looked for again in this run.
	inline UTexture2D* LoadTexture(const TCHAR* Name)
	{
		static TMap<FString, TWeakObjectPtr<UTexture2D>> Loaded;
		static TSet<FString> Missing;
		const FString Key(Name);
		if (Missing.Contains(Key))
		{
			return nullptr;
		}
		if (const TWeakObjectPtr<UTexture2D>* Found = Loaded.Find(Key))
		{
			if (Found->IsValid())
			{
				return Found->Get();
			}
		}
		const FString Path = FString::Printf(TEXT("/Game/ImageAssets/Mobile/Mobile_%s.Mobile_%s"), Name, Name);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
		if (!Texture)
		{
			Missing.Add(Key);
			return nullptr;
		}
		Loaded.Add(Key, Texture);
		return Texture;
	}

	// The picture of an icon, white, at Size; null when its texture is not imported. A screen that changes the colour later (the tabs) keeps it and calls
	// SetColorAndOpacity: the colour is multiplied with the white of the texture. The icons that keep their own colours (CompassColour) are asked for with white.
	inline UImage* Image(UWidgetTree* Tree, const TCHAR* Name, float Size)
	{
		UTexture2D* Texture = LoadTexture(Name);
		if (!Texture)
		{
			return nullptr;
		}
		UImage* Picture = Tree->ConstructWidget<UImage>(UImage::StaticClass());
		Picture->SetBrushFromTexture(Texture, false);
		Picture->SetDesiredSizeOverride(FVector2D(Size, Size));
		return Picture;
	}

	// The icon as a tinted picture in a square of Size; null when its texture is not imported.
	inline UWidget* Picture(UWidgetTree* Tree, const TCHAR* Name, float Size, const FLinearColor& Colour)
	{
		UImage* Image = MobileIcons::Image(Tree, Name, Size);
		if (!Image)
		{
			return nullptr;
		}
		Image->SetColorAndOpacity(Colour);
		USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetWidthOverride(Size);
		Box->SetHeightOverride(Size);
		Box->SetContent(Image);
		return Box;
	}

	namespace Detail
	{
		// A bar (a filled rounded rectangle) at a position of the icon's square, turned by Degrees about its own centre.
		inline UBorder* AddBar(UWidgetTree* Tree, UCanvasPanel* Canvas, const FLinearColor& Colour, const FVector2D& Centre, const FVector2D& Extent, float Degrees)
		{
			UBorder* Bar = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Bar->SetBrush(MobileStyle::Rounded(Colour, FLinearColor::Transparent, static_cast<float>(FMath::Min(Extent.X, Extent.Y) * 0.5), 0.f));
			Bar->SetBrushColor(FLinearColor::White);
			UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Bar);
			Slot->SetSize(Extent);
			Slot->SetPosition(Centre - Extent * 0.5);
			if (!FMath::IsNearlyZero(Degrees))
			{
				Bar->SetRenderTransformAngle(Degrees);
			}
			return Bar;
		}

		inline UBorder* AddRing(UWidgetTree* Tree, UCanvasPanel* Canvas, const FLinearColor& Colour, const FVector2D& Position, float Diameter, float Stroke)
		{
			UBorder* Ring = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Ring->SetBrush(MobileStyle::Rounded(FLinearColor::Transparent, Colour, Diameter * 0.5f, Stroke));
			Ring->SetBrushColor(FLinearColor::White);
			UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Ring);
			Slot->SetSize(FVector2D(Diameter, Diameter));
			Slot->SetPosition(Position);
			return Ring;
		}

		inline USizeBox* Square(UWidgetTree* Tree, UCanvasPanel* Canvas, float Size)
		{
			USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			Box->SetWidthOverride(Size);
			Box->SetHeightOverride(Size);
			Box->SetContent(Canvas);
			return Box;
		}
	}

	// A magnifying glass: a ring at the top left and a handle to the bottom right.
	inline UWidget* Magnifier(UWidgetTree* Tree, float Size, const FLinearColor& Colour)
	{
		if (UWidget* Image = Picture(Tree, TEXT("Search"), Size, Colour))
		{
			return Image;
		}
		UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		const float Stroke = FMath::Max(1.5f, Size * 0.1f);
		Detail::AddRing(Tree, Canvas, Colour, FVector2D(0.0, 0.0), Size * 0.68f, Stroke);
		Detail::AddBar(Tree, Canvas, Colour, FVector2D(Size * 0.82f, Size * 0.82f), FVector2D(Stroke, Size * 0.36f), -45.f);
		return Detail::Square(Tree, Canvas, Size);
	}

	// A clock: a ring with an hour hand up and a minute hand to the right.
	inline UWidget* Clock(UWidgetTree* Tree, float Size, const FLinearColor& Colour)
	{
		if (UWidget* Image = Picture(Tree, TEXT("Recent"), Size, Colour))
		{
			return Image;
		}
		UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		const float Stroke = FMath::Max(1.5f, Size * 0.09f);
		Detail::AddRing(Tree, Canvas, Colour, FVector2D(0.0, 0.0), Size, Stroke);
		Detail::AddBar(Tree, Canvas, Colour, FVector2D(Size * 0.5f, Size * 0.36f), FVector2D(Stroke, Size * 0.3f), 0.f);
		Detail::AddBar(Tree, Canvas, Colour, FVector2D(Size * 0.62f, Size * 0.5f), FVector2D(Size * 0.26f, Stroke), 0.f);
		return Detail::Square(Tree, Canvas, Size);
	}

	// The arrow at the end of a row that opens something (the prototype's "move forward": a corner turning right); a chevron when the texture is missing.
	inline UWidget* Arrow(UWidgetTree* Tree, float Size, const FLinearColor& Colour)
	{
		if (UWidget* Image = Picture(Tree, TEXT("MoveForward"), Size, Colour))
		{
			return Image;
		}
		return MobileStyle::MakeText(Tree, FString(), TEXT("›"), Size * 1.1f, Colour, true);
	}

	// A cross, for the button that clears or closes a search; a multiplication sign when the texture is missing.
	inline UWidget* Cross(UWidgetTree* Tree, float Size, const FLinearColor& Colour)
	{
		if (UWidget* Image = Picture(Tree, TEXT("ClearSearch"), Size, Colour))
		{
			return Image;
		}
		return MobileStyle::MakeText(Tree, FString(), TEXT("×"), Size * 1.2f, Colour, true);
	}

	// The close button of a page's header: a cross in a 44 square that lights up round when pressed (the prototype's circled cross).
	inline UButton* CloseButton(UWidgetTree* Tree, const FString& Name)
	{
		using namespace MobileStyle;
		UButton* Close = Tree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
		Close->SetStyle(StartStyle::Button(FLinearColor::Transparent, TapHover, TapHover * 2.f, FLinearColor::Transparent, 22.f / StartStyle::DesignToUi));
		Close->SetBackgroundColor(FLinearColor::White);
		UBorder* Centre = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Name + TEXT("_Centre")));
		Centre->SetBrushColor(FLinearColor::Transparent);
		Centre->SetHorizontalAlignment(HAlign_Center);
		Centre->SetVerticalAlignment(VAlign_Center);
		UWidget* Icon = Picture(Tree, TEXT("Close"), 24.f, Grey);
		Centre->SetContent(Icon ? Icon : static_cast<UWidget*>(MakeText(Tree, FString(), TEXT("\u00D7"), 24.f, Grey, true)));
		USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(Name + TEXT("_Size")));
		Box->SetWidthOverride(44.f);
		Box->SetHeightOverride(44.f);
		Box->SetContent(Centre);
		Close->SetContent(Box);
		return Close;
	}

	// A rounded button with an icon before its centred label (Save place with a heart, Get directions with a pin). The label is named <Name>_Label; OutIcon is
	// the picture, to tint when the button changes colour (null while the texture is missing, and then the button is just the label).
	inline UButton* IconLabelButton(UWidgetTree* Tree, const FString& Name, const FString& Label, const TCHAR* IconName, const FLinearColor& Fill, const FLinearColor& TextColor, float Radius, float Height, UTextBlock** OutLabel, UImage** OutIcon)
	{
		using namespace MobileStyle;
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
		Button->SetStyle(StartStyle::Button(Fill, Fill * 0.94f, Fill * 0.86f, Line, Radius / StartStyle::DesignToUi));
		Button->SetBackgroundColor(FLinearColor::White);
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(Name + TEXT("_Row")));
		UImage* Icon = Image(Tree, IconName, 18.f);
		if (Icon)
		{
			Icon->SetColorAndOpacity(TextColor);
			USizeBox* IconBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(Name + TEXT("_IconSize")));
			IconBox->SetWidthOverride(18.f);
			IconBox->SetHeightOverride(18.f);
			IconBox->SetContent(Icon);
			Row->AddChildToHorizontalBox(IconBox)->SetVerticalAlignment(VAlign_Center);
		}
		UTextBlock* Text = MakeText(Tree, Name + TEXT("_Label"), Label, 14.f, TextColor, true);
		UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
		TextSlot->SetPadding(FMargin(Icon ? 8.f : 0.f, 0.f, 0.f, 0.f));
		TextSlot->SetVerticalAlignment(VAlign_Center);
		UBorder* Centre = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Name + TEXT("_Centre")));
		Centre->SetBrushColor(FLinearColor::Transparent);
		Centre->SetHorizontalAlignment(HAlign_Center);
		Centre->SetVerticalAlignment(VAlign_Center);
		Centre->SetContent(Row);
		USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(Name + TEXT("_Size")));
		Box->SetMinDesiredHeight(Height);
		Box->SetContent(Centre);
		Button->SetContent(Box);
		if (OutLabel)
		{
			*OutLabel = Text;
		}
		if (OutIcon)
		{
			*OutIcon = Icon;
		}
		return Button;
	}

	// A check mark of two bars, for a ticked box.
	inline UWidget* Tick(UWidgetTree* Tree, float Size, const FLinearColor& Colour)
	{
		UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		const float Stroke = FMath::Max(2.f, Size * 0.14f);
		Detail::AddBar(Tree, Canvas, Colour, FVector2D(Size * 0.335f, Size * 0.60f), FVector2D(Size * 0.27f, Stroke), 45.f);
		Detail::AddBar(Tree, Canvas, Colour, FVector2D(Size * 0.585f, Size * 0.49f), FVector2D(Size * 0.55f, Stroke), -50.f);
		return Detail::Square(Tree, Canvas, Size);
	}
}
