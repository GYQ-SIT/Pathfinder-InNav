#if WITH_DEV_AUTOMATION_TESTS

#include "GeoProjection.h"
#include "MobileAppWidget.h"
#include "MobileMapWidget.h"
#include "MobileRealSource.h"

#include "Components/Button.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"

namespace
{
	class FFakeMobileLocationSource final : public IMobileLocationSource
	{
	public:
		FMobileLocationReading ReadLocation() override
		{
			++ReadCount;
			return Reading;
		}

		FMobileLocationReading Reading;
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

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("The test world is created"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	auto* App = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (!TestNotNull(TEXT("The app is created"), App)) return false;
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
	TestTrue(TEXT("No fix shows the finding status"), Map->GetGpsStatus() == EMobileGpsStatus::Finding);

	Location->Reading.Fix = FMobileLocationFix{
		FVector2D(Origin->X + 20.0 / GeoProjection::MetersPerDegreeLat,
			Origin->Y + 35.0 / GeoProjection::MetersPerDegreeLon(Origin->X)), 12.0};
	App->UpdateLocation(0.5f);
	TestEqual(TEXT("The source is not polled each frame"), Location->ReadCount, 1);
	App->UpdateLocation(0.5f);
	const auto Position = Map->GetGpsPosition();
	if (!TestTrue(TEXT("The first fix reaches the dot"), Position.IsSet())) return false;
	TestTrue(TEXT("The dot is in campus meters, north then east"), Position->Equals(FVector2D(20.0, 35.0), 1.e-6));

	auto* Locate = Cast<UButton>(App->FindNamedWidget(TEXT("Btn_Locate")));
	if (!TestNotNull(TEXT("The app has a Locate button"), Locate)) return false;
	Locate->OnClicked.Broadcast();
	TestTrue(TEXT("Locate switches to campus overview"), Map->IsOverview());
	const FVector2D PanAfterLocate = Map->GetPan();
	TestTrue(TEXT("Locate centers the campus view on the fix"), !PanAfterLocate.IsNearlyZero());

	TestEqual(TEXT("Accuracy reaches the map in meters"), Map->GetGpsAccuracyMeters(), 12.0);
	TestTrue(TEXT("A fresh fix marks the dot current"), Map->GetGpsStatus() == EMobileGpsStatus::Current);

	Location->Reading.Fix = FMobileLocationFix{*Origin, 8.0};
	App->UpdateLocation(1.f);
	TestTrue(TEXT("A new fix moves the dot without panning the map"),
		Map->GetGpsPosition().IsSet() && Map->GetGpsPosition()->IsNearlyZero() && Map->GetPan().Equals(PanAfterLocate));

	Location->Reading.Fix = FMobileLocationFix{FVector2D(91.0, 0.0), 999.0};
	App->UpdateLocation(1.f);
	TestTrue(TEXT("An invalid fix leaves the last valid dot in place"),
		Map->GetGpsPosition().IsSet() && Map->GetGpsPosition()->IsNearlyZero());
	Location->Reading.Fix = FMobileLocationFix{*Origin, 8.0, 10.0};
	App->UpdateLocation(1.f);
	TestTrue(TEXT("A cached fix becomes last known without losing its point"),
		Map->GetGpsStatus() == EMobileGpsStatus::LastKnown && Map->GetGpsPosition().IsSet());

	Location->Reading.Fix = FMobileLocationFix{*Origin, 0.0};
	App->UpdateLocation(1.f);
	TestTrue(TEXT("A fresh fix at the same location clears the stale status"), Map->GetGpsStatus() == EMobileGpsStatus::Current);
	TestEqual(TEXT("Unknown accuracy does not draw a circle"), Map->GetGpsAccuracyMeters(), 0.0);

	Location->Reading = {true, {}};
	App->UpdateLocation(1.f);
	TestTrue(TEXT("Missing permission shows a status and removes the dot"),
		Map->GetGpsStatus() == EMobileGpsStatus::PermissionNeeded && !Map->GetGpsPosition());
	return true;
}

#endif
