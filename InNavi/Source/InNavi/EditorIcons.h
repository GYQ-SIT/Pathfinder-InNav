#pragma once

#include "CoreMinimal.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

// The design team's icons in the editor's screens (Content/ImageAssets/Icon_<Name>: white lines on a transparent ground, drawn in the colour the screen
// asks for). A texture that is not imported yet is not an error: the screen keeps what it had before (a symbol, a drawing, or just the words), so a
// screen can be changed before its textures are there. The phone app's own set is MobileIcons.h.
namespace EditorIcons
{
	// The texture of an icon (its asset name, "Icon_Back"), or null when it is not imported.
	inline UTexture2D* Load(const TCHAR* Asset)
	{
		return LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/ImageAssets/%s.%s"), Asset, Asset), nullptr, LOAD_Quiet | LOAD_NoWarn);
	}

	// The icon as a picture of Size (the size the screen draws it at), tinted, in a box of exactly that size (a picture asked to be a size on its own stays
	// 32 x 32, the size of its brush); null when its texture is not imported. The picture itself is called Name, the box Name + "Size".
	inline UWidget* Picture(UWidgetTree* Tree, const FString& Name, const TCHAR* Asset, float Size, const FLinearColor& Tint)
	{
		UTexture2D* Texture = Load(Asset);
		if (!Texture)
		{
			return nullptr;
		}
		UImage* Image = Tree->ConstructWidget<UImage>(UImage::StaticClass(), FName(Name));
		Image->SetBrushFromTexture(Texture, false);
		Image->SetColorAndOpacity(Tint);
		USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(Name + TEXT("Size")));
		Box->SetWidthOverride(Size);
		Box->SetHeightOverride(Size);
		Box->SetVisibility(ESlateVisibility::HitTestInvisible);
		Box->SetContent(Image);
		return Box;
	}

	// What goes inside a button: the icon, then the label, side by side; just the label when the icon is not imported.
	inline UWidget* IconAndLabel(UWidgetTree* Tree, const FString& Name, const TCHAR* Asset, float IconSize, float Gap, UTextBlock* Label, const FLinearColor& Tint)
	{
		UWidget* Image = Picture(Tree, Name + TEXT("Icon"), Asset, IconSize, Tint);
		if (!Image)
		{
			return Label;
		}
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(Name + TEXT("Row")));
		Row->AddChildToHorizontalBox(Image)->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(Gap, 0.f, 0.f, 0.f));
		return Row;
	}
}
