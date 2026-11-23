#include "MobileLocationSource.h"

#if PLATFORM_ANDROID
#include "AndroidPermissionFunctionLibrary.h"
#include "LocationServicesBPLibrary.h"

namespace
{
	class FAndroidMobileLocationSource final : public IMobileLocationSource
	{
	public:
		~FAndroidMobileLocationSource() override
		{
			if (bStarted)
			{
				ULocationServices::StopLocationServices();
			}
		}

		TOptional<FVector2D> ReadLocation() override
		{
			const FString Fine(TEXT("android.permission.ACCESS_FINE_LOCATION"));
			const FString Coarse(TEXT("android.permission.ACCESS_COARSE_LOCATION"));
			const bool bFine = UAndroidPermissionFunctionLibrary::CheckPermission(Fine);
			if (!bFine && !UAndroidPermissionFunctionLibrary::CheckPermission(Coarse))
			{
				if (!bRequestedPermission)
				{
					bRequestedPermission = true;
					UAndroidPermissionFunctionLibrary::AcquirePermissions({Fine, Coarse});
				}
				return {};
			}

			if (!bStarted)
			{
				const ELocationAccuracy Accuracy = bFine ? ELocationAccuracy::LA_Best : ELocationAccuracy::LA_HundredMeters;
				bStarted = ULocationServices::InitLocationServices(Accuracy, 1000.f, 0.f)
					&& ULocationServices::StartLocationServices();
				if (!bStarted)
				{
					return {};
				}
			}

			const FLocationServicesData Fix = ULocationServices::GetLastKnownLocation();
			return Fix.Timestamp > 0.f ? TOptional<FVector2D>(FVector2D(Fix.Latitude, Fix.Longitude)) : TOptional<FVector2D>();
		}

	private:
		bool bRequestedPermission = false;
		bool bStarted = false;
	};
}
#endif

TUniquePtr<IMobileLocationSource> MakePlatformMobileLocationSource()
{
#if PLATFORM_ANDROID
	return MakeUnique<FAndroidMobileLocationSource>();
#else
	return nullptr;
#endif
}
