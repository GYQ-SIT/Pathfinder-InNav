#include "MobileLocationSource.h"

#include "HAL/PlatformTime.h"
#include "MobileLocationListener.h"

void UMobileLocationListener::OnLocationChanged(FLocationServicesData Location)
{
	if (!FMath::IsFinite(Location.Latitude) || !FMath::IsFinite(Location.Longitude)
		|| FMath::Abs(Location.Latitude) > 90.f || FMath::Abs(Location.Longitude) > 180.f)
	{
		return;
	}
	Fix = FMobileLocationFix{FVector2D(Location.Latitude, Location.Longitude), Location.HorizontalAccuracy};
	// Android's plugin exposes its epoch timestamp as a float. Its precision is too low for GPS freshness.
	ReceivedAt = FPlatformTime::Seconds();
}

TOptional<FMobileLocationFix> UMobileLocationListener::ReadFix() const
{
	TOptional<FMobileLocationFix> Result = Fix;
	if (Result)
	{
		Result->AgeSeconds = FPlatformTime::Seconds() - ReceivedAt;
	}
	return Result;
}

#if PLATFORM_ANDROID
#include "AndroidPermissionFunctionLibrary.h"
#include "LocationServicesImpl.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	class FAndroidMobileLocationSource final : public IMobileLocationSource
	{
	public:
		~FAndroidMobileLocationSource() override
		{
			Stop();
			if (ULocationServicesImpl* Service = ULocationServices::GetLocationServicesImpl(); Service && Listener)
			{
				Service->OnLocationChanged.RemoveDynamic(Listener.Get(), &UMobileLocationListener::OnLocationChanged);
			}
		}

		FMobileLocationReading ReadLocation() override
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
				Stop();
				if (Listener) Listener->Clear();
				return {true, {}};
			}

			if (!bStarted)
			{
				const ELocationAccuracy Accuracy = bFine ? ELocationAccuracy::LA_Best : ELocationAccuracy::LA_HundredMeters;
				ULocationServicesImpl* Service = ULocationServices::GetLocationServicesImpl();
				if (!Service || !ULocationServices::InitLocationServices(Accuracy, 1000.f, 0.f)) return {};
				if (!Listener) Listener.Reset(NewObject<UMobileLocationListener>());
				Service->OnLocationChanged.AddUniqueDynamic(Listener.Get(), &UMobileLocationListener::OnLocationChanged);
				bStarted = ULocationServices::StartLocationServices();
				if (!bStarted)
				{
					return {};
				}
			}

			return {false, Listener->ReadFix()};
		}

	private:
		void Stop()
		{
			if (bStarted)
			{
				ULocationServices::StopLocationServices();
				bStarted = false;
			}
		}

		TStrongObjectPtr<UMobileLocationListener> Listener;
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
