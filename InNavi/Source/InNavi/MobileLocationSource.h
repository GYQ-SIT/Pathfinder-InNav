#pragma once

#include "CoreMinimal.h"

// A latitude/longitude pair in degrees, with latitude in X.
class IMobileLocationSource
{
public:
	virtual ~IMobileLocationSource() = default;
	virtual TOptional<FVector2D> ReadLocation() = 0;
};

TUniquePtr<IMobileLocationSource> MakePlatformMobileLocationSource();
