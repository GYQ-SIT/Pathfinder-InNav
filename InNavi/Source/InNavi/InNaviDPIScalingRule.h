#pragma once

#include "CoreMinimal.h"
#include "Engine/DPICustomScalingRule.h"
#include "InNaviDPIScalingRule.generated.h"

// Scale the desktop's existing 1080p design to fit both viewport dimensions.
// Using UMG's DPI rule keeps layout, popup placement and pointer coordinates in sync.
UCLASS()
class INNAVI_API UInNaviDPIScalingRule : public UDPICustomScalingRule
{
	GENERATED_BODY()

public:
	virtual float GetDPIScaleBasedOnSize(FIntPoint Size) const override;

	static float CalculateScale(FIntPoint Size, bool bMobileApp);
};
