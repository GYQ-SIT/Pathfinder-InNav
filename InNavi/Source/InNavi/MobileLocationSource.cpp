#include "MobileLocationSource.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if PLATFORM_ANDROID
#include "AndroidPermissionFunctionLibrary.h"
#include "LocationServicesBPLibrary.h"
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"

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
			if (bHeadingStarted)
			{
				if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
				{
					static jmethodID StopHeading = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_StopHeading", "()V", false);
					FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, StopHeading);
				}
			}
		}

		// The compass of the phone (InNavi_UPL.xml): started with the first read, then the last smoothed value, or nothing while the phone has none.
		TOptional<double> ReadHeading() override
		{
			JNIEnv* Env = FAndroidApplication::GetJavaEnv();
			if (!Env)
			{
				return {};
			}
			if (!bHeadingStarted)
			{
				bHeadingStarted = true;
				static jmethodID StartHeading = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_StartHeading", "()Z", false);
				FJavaWrapper::CallBooleanMethod(Env, FJavaWrapper::GameActivityThis, StartHeading);
			}
			static jmethodID GetHeading = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_GetHeading", "()F", false);
			const float Degrees = FJavaWrapper::CallFloatMethod(Env, FJavaWrapper::GameActivityThis, GetHeading);
			return Degrees >= 0.f && Degrees <= 360.f ? TOptional<double>(Degrees) : TOptional<double>();
		}

		TOptional<FVector2D> ReadLocation() override
		{
			// The permission is asked for, and the updates started, once; after that a read only asks for the last fix.
			if (!bStarted)
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
		bool bHeadingStarted = false;
	};
}
#endif

TOptional<FVector2D> ParseFakeMobileLocation(const FString& CommandLine)
{
	FString Text, Lat, Lon;
	// bShouldStopOnSeparator is off: the comma between the two numbers is part of the value.
	if (!FParse::Value(*CommandLine, TEXT("GpsFake="), Text, false) || !Text.Split(TEXT(","), &Lat, &Lon)
		|| !Lat.TrimStartAndEnd().IsNumeric() || !Lon.TrimStartAndEnd().IsNumeric())
	{
		return {};
	}
	const double Latitude = FCString::Atod(*Lat), Longitude = FCString::Atod(*Lon);
	return FMath::Abs(Latitude) <= 90.0 && FMath::Abs(Longitude) <= 180.0 ? TOptional<FVector2D>(FVector2D(Latitude, Longitude)) : TOptional<FVector2D>();
}

TOptional<double> ParseFakeMobileHeading(const FString& CommandLine)
{
	FString Text;
	if (!FParse::Value(*CommandLine, TEXT("GpsFakeHeading="), Text, false) || !Text.TrimStartAndEnd().IsNumeric())
	{
		return {};
	}
	const double Degrees = FCString::Atod(*Text);
	return FMath::IsFinite(Degrees) ? TOptional<double>(FMath::Fmod(FMath::Fmod(Degrees, 360.0) + 360.0, 360.0)) : TOptional<double>();
}

#if !UE_BUILD_SHIPPING
namespace
{
	// The position of the command line; the direction is the command line's too, or else the device's own compass when it has one.
	class FFakeCommandLineLocationSource final : public IMobileLocationSource
	{
	public:
		FFakeCommandLineLocationSource(const FVector2D& InFix, const TOptional<double>& InHeading, TUniquePtr<IMobileLocationSource> InCompass)
			: Fix(InFix), Heading(InHeading), Compass(MoveTemp(InCompass)) {}
		TOptional<FVector2D> ReadLocation() override { return Fix; }
		TOptional<double> ReadHeading() override { return Heading ? Heading : (Compass ? Compass->ReadHeading() : TOptional<double>()); }
	private:
		FVector2D Fix;
		TOptional<double> Heading;
		TUniquePtr<IMobileLocationSource> Compass;
	};
}
#endif

TUniquePtr<IMobileLocationSource> MakePlatformMobileLocationSource()
{
	TUniquePtr<IMobileLocationSource> Platform;
#if PLATFORM_ANDROID
	Platform = MakeUnique<FAndroidMobileLocationSource>();
#endif
#if !UE_BUILD_SHIPPING
	if (const TOptional<FVector2D> Fake = ParseFakeMobileLocation(FCommandLine::Get()))
	{
		return MakeUnique<FFakeCommandLineLocationSource>(*Fake, ParseFakeMobileHeading(FCommandLine::Get()), MoveTemp(Platform));
	}
#endif
	return Platform;
}
