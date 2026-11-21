#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FullRouteCalloutLayer.generated.h"

class UCanvasPanel;

// A layer of the screen for the call-outs of the full route overview (see ANavPlayerController::AddFullRouteCallout): a canvas over the whole screen that
// takes no touch. The controller puts the call-outs in it and moves them every frame.
UCLASS()
class INNAVI_API UFullRouteCalloutLayer : public UUserWidget
{
	GENERATED_BODY()

public:
	UCanvasPanel* GetCanvas() const { return Canvas; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY()
	UCanvasPanel* Canvas = nullptr;
};
