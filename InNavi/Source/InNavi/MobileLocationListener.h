#pragma once

#include "LocationServicesBPLibrary.h"
#include "MobileLocationSource.h"
#include "MobileLocationListener.generated.h"

UCLASS()
class UMobileLocationListener final : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void OnLocationChanged(FLocationServicesData Location);

	TOptional<FMobileLocationFix> ReadFix() const;
	void Clear() { Fix.Reset(); }

private:
	TOptional<FMobileLocationFix> Fix;
	double ReceivedAt = 0.0;
};
