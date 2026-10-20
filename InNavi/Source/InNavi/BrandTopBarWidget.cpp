#include "BrandTopBarWidget.h"

#include "StartScreenStyle.h"

#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

float UBrandTopBarWidget::DesignHeight()
{
	return 64.f * StartStyle::DesignToUi;
}

TSharedRef<SWidget> UBrandTopBarWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		using namespace StartStyle;
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("TopBarRoot"));
		WidgetTree->RootWidget = Root;

		UBorder* Bar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TopBar"));
		Bar->SetBrushColor(Rgb(16, 32, 50));
		UOverlaySlot* BarSlot = Root->AddChildToOverlay(Bar);
		BarSlot->SetHorizontalAlignment(HAlign_Fill);
		BarSlot->SetVerticalAlignment(VAlign_Fill);

		// The divider along the lower edge.
		USizeBox* LineSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TopBarLineSize"));
		LineSize->SetHeightOverride(1.f);
		UBorder* Line = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TopBarLine"));
		Line->SetBrushColor(Rgb(41, 64, 87));
		LineSize->SetContent(Line);
		UOverlaySlot* LineSlot = Root->AddChildToOverlay(LineSize);
		LineSlot->SetHorizontalAlignment(HAlign_Fill);
		LineSlot->SetVerticalAlignment(VAlign_Bottom);

		// "InNav" with the product name in small capitals under it, vertically centred.
		UVerticalBox* Brand = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Brand"));
		Brand->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_BrandName"), TEXT("InNav"), 19.f, Rgb(241, 246, 255), true));
		Brand->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_BrandCaption"), TEXT("INDOOR NAVIGATION EDITOR"), 9.f, Rgb(171, 189, 212), true, 80))
			->SetPadding(FMargin(0.f, 1.f * DesignToUi, 0.f, 0.f));
		UOverlaySlot* BrandSlot = Root->AddChildToOverlay(Brand);
		BrandSlot->SetHorizontalAlignment(HAlign_Left);
		BrandSlot->SetVerticalAlignment(VAlign_Center);
		BrandSlot->SetPadding(FMargin(22.f * DesignToUi, 0.f, 0.f, 0.f));
	}
	return Super::RebuildWidget();
}
