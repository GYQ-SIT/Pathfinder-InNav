#if WITH_DEV_AUTOMATION_TESTS

#include "GeoProjection.h"
#include "MobileAppWidget.h"
#include "MobileMapWidget.h"
#include "MobileRealSource.h"

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace
{
	class FFakeMobileLocationSource final : public IMobileLocationSource
	{
	public:
		TOptional<FVector2D> ReadLocation() override
		{
			++ReadCount;
			return Fix;
		}

		TOptional<FVector2D> Fix;
		int32 ReadCount = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileGpsTest, "InNavi.Mobile.Gps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobileGpsTest::RunTest(const FString& Parameters)
{
	const auto Data = MakeShared<FMobileRealSource>(FPaths::ProjectDir() / TEXT("Data/map_extended.db"));
	const auto Sites = Data->ListSites();
	if (!TestTrue(TEXT("The bundled campus database loads"), !Sites.IsEmpty())) return false;
	const auto Origin = Data->GetGeoOrigin(Sites[0].Id);
	if (!TestTrue(TEXT("The database exposes its projection origin"), Origin.IsSet())) return false;

	auto* App = NewObject<UMobileAppWidget>();
	App->SetSource(Data, Sites[0].Id);
	const TSharedRef<SWidget> Widget = App->TakeWidget();
	auto* Map = App->GetMapView();
	if (!TestNotNull(TEXT("The app has a map"), Map)) return false;
	Map->SetViewSize(375.0, 812.0);

	auto Fake = MakeUnique<FFakeMobileLocationSource>();
	auto* Location = Fake.Get();
	App->SetLocationSource(MoveTemp(Fake));
	App->UpdateLocation(0.f);
	TestFalse(TEXT("No fix means no GPS dot"), Map->GetGpsPosition().IsSet());

	Location->Fix = FVector2D(Origin->X + 20.0 / GeoProjection::MetersPerDegreeLat,
		Origin->Y + 35.0 / GeoProjection::MetersPerDegreeLon(Origin->X));
	App->UpdateLocation(0.5f);
	TestEqual(TEXT("The source is not polled each frame"), Location->ReadCount, 1);
	App->UpdateLocation(0.5f);
	const auto Position = Map->GetGpsPosition();
	if (!TestTrue(TEXT("The first fix reaches the dot"), Position.IsSet())) return false;
	TestTrue(TEXT("The dot is in campus meters, north then east"), Position->Equals(FVector2D(20.0, 35.0), 1.e-6));

	App->LocateMe();
	TestTrue(TEXT("Locate switches to campus overview"), Map->IsOverview());
	const FVector2D PanAfterLocate = Map->GetPan();
	TestTrue(TEXT("Locate centers the campus view on the fix"), !PanAfterLocate.IsNearlyZero());

	Location->Fix = *Origin;
	App->UpdateLocation(1.f);
	TestTrue(TEXT("A new fix moves the dot without panning the map"),
		Map->GetGpsPosition().IsSet() && Map->GetGpsPosition()->IsNearlyZero() && Map->GetPan().Equals(PanAfterLocate));

	Location->Fix = FVector2D(91.0, 0.0);
	App->UpdateLocation(1.f);
	TestTrue(TEXT("An invalid fix leaves the last valid dot in place"),
		Map->GetGpsPosition().IsSet() && Map->GetGpsPosition()->IsNearlyZero());
	return true;
}

#endif
