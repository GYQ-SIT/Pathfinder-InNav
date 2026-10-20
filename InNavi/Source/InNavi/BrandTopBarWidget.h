#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BrandTopBarWidget.generated.h"

// The strip along the top of the start screen and the project library: "InNav" with the product name in small capitals
// under it, on a dark bar with a thin divider line along its lower edge. Fills whatever slot it is put in; the slot should
// be DesignHeight() tall.
UCLASS()
class INNAVI_API UBrandTopBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// The bar's height in UI units (64 design pixels).
	static float DesignHeight();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};
