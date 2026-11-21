#include "FullRouteCalloutLayer.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"

TSharedRef<SWidget> UFullRouteCalloutLayer::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("FullRouteCallouts"));
		WidgetTree->RootWidget = Canvas;
	}
	return Super::RebuildWidget();
}
