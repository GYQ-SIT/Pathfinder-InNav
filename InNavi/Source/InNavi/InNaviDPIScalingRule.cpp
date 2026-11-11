#include "InNaviDPIScalingRule.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

float UInNaviDPIScalingRule::GetDPIScaleBasedOnSize(FIntPoint Size) const
{
#if PLATFORM_ANDROID || PLATFORM_IOS
	constexpr bool bMobileApp = true;
#else
	const bool bMobileApp = FParse::Param(FCommandLine::Get(), TEXT("MobileApp"));
#endif
	return CalculateScale(Size, bMobileApp);
}

float UInNaviDPIScalingRule::CalculateScale(FIntPoint Size, bool bMobileApp)
{
	constexpr float ReferenceScale = 0.77f;
	if (bMobileApp || Size.X <= 0 || Size.Y <= 0)
	{
		// The phone UI already manages its own scale. Zero sizes can occur while minimized.
		return ReferenceScale;
	}

	const float Fit = FMath::Min(Size.X / 1920.f, Size.Y / 1080.f);
	return FMath::Max(0.01f, ReferenceScale * Fit);
}
