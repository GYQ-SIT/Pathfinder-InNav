#pragma once

#include "CoreMinimal.h"

// A latitude/longitude pair in degrees, with latitude in X.
class IMobileLocationSource
{
public:
	virtual ~IMobileLocationSource() = default;
	virtual TOptional<FVector2D> ReadLocation() = 0;
	// The direction the device faces, in degrees clockwise from north (0 to 360), when the device knows it (its compass). Read often, it is cheap.
	virtual TOptional<double> ReadHeading() { return {}; }
};

// The position the command line says to use instead of the device's GPS: -GpsFake=<latitude>,<longitude> (for testing where the device is not, e.g. at
// home with the campus map). Not honoured in a Shipping build. Empty when the argument is absent or is not a latitude and a longitude.
TOptional<FVector2D> ParseFakeMobileLocation(const FString& CommandLine);
// -GpsFakeHeading=<degrees clockwise from north>, the direction that goes with the fake position (the arrow of the dot).
TOptional<double> ParseFakeMobileHeading(const FString& CommandLine);

TUniquePtr<IMobileLocationSource> MakePlatformMobileLocationSource();
