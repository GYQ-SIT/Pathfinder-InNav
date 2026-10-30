#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "LayoutToolPanel.generated.h"

class UUserWidget;

UINTERFACE(MinimalAPI)
class ULayoutToolPanel : public UInterface
{
    GENERATED_BODY()
};

// Optional UI adapter contract. The validator's JSON API is independent of this interface.
class INNAVILAYOUTVALIDATOR_API ILayoutToolPanel
{
    GENERATED_BODY()
public:
    virtual void InitializeToolPanel(UUserWidget* Screen, bool bExterior, TFunction<void()> FocusCompleted) = 0;
    virtual void SetToolPanelWidth(float Width) = 0;
};
