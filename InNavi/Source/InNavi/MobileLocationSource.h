#pragma once

#include "CoreMinimal.h"

enum class EMobileGpsStatus : uint8
{
	Hidden,
	Finding,
	PermissionNeeded,
	Current,
	LastKnown
};

struct FMobileLocationFix
{
	// A latitude/longitude pair in degrees, with latitude in X.
	FVector2D Coordinates;
	double AccuracyMeters = 0.0;
	double AgeSeconds = 0.0;
};

struct FMobileLocationReading
{
	bool bPermissionNeeded = false;
	TOptional<FMobileLocationFix> Fix;
};
class IMobileLocationSource
{
public:
	virtual ~IMobileLocationSource() = default;
	virtual FMobileLocationReading ReadLocation() = 0;
};

TUniquePtr<IMobileLocationSource> MakePlatformMobileLocationSource();
