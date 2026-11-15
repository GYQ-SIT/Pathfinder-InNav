#pragma once

#include "CoreMinimal.h"
#include "MobileIcons.h"
#include "MobileStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

// The search field of the map and of the Explore tab: a white pill with a magnifier and the hint, 46 tall. It is a button, not a text box: a tap opens
// the full-screen search (UMobileSearchOverlayWidget), where the typing is. Header-only and inline, like MobileStyle.
namespace MobileSearchField
{
	static constexpr float Height = 46.f;

	// Returns the pill to put in a layout; OutButton is the button inside it, to hook the tap on.
	inline UWidget* Make(UWidgetTree* Tree, const FString& Name, const FString& Hint, UButton** OutButton)
	{
		using namespace MobileStyle;
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
		Button->SetStyle(StartStyle::Button(FLinearColor::Transparent, TapHover, TapHover * 2.f, FLinearColor::Transparent, 24.f / StartStyle::DesignToUi));
		Button->SetBackgroundColor(FLinearColor::White);

		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(Name + TEXT("_Row")));
		Row->AddChildToHorizontalBox(MobileIcons::Magnifier(Tree, 20.f, Ink))->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* HintSlot = Row->AddChildToHorizontalBox(MakeText(Tree, Name + TEXT("_Hint"), Hint, 15.f, Grey));
		HintSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		HintSlot->SetVerticalAlignment(VAlign_Center);
		HintSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		UBorder* Pill = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Name + TEXT("_Pill")));
		Pill->SetBrush(Rounded(White, FieldEdge, 24.f, 1.f));
		Pill->SetBrushColor(FLinearColor::White);
		Pill->SetPadding(FMargin(16.f, 0.f));
		Pill->SetVerticalAlignment(VAlign_Center);
		Pill->SetContent(Row);

		USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(Name + TEXT("_Size")));
		Box->SetHeightOverride(Height);
		Box->SetContent(Pill);
		// A button centres and shrinks its content by default; the pill has to fill the width it is given.
		if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Button->SetContent(Box)))
		{
			ContentSlot->SetHorizontalAlignment(HAlign_Fill);
			ContentSlot->SetVerticalAlignment(VAlign_Fill);
			ContentSlot->SetPadding(FMargin(0.f));
		}
		if (OutButton)
		{
			*OutButton = Button;
		}
		return Button;
	}
}
