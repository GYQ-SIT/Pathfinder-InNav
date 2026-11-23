#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "GeoProjection.h"
#include "MobileAppWidget.h"
#include "MobileMapWidget.h"
#include "MobileRealSource.h"
#include "NavPlayerController.h"
#include "UiTestFixture.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// The latitude and longitude the meters of a project are measured from ("geoOrigin") live in the file, so a project of a map database keeps its place on
// the earth: the phone's GPS needs it to put the device on the map.
namespace GeoOriginTests
{
	FString TempProject(const TCHAR* Prefix)
	{
		return FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (FString(Prefix) + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
	}

	TSharedPtr<FJsonObject> ReadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *Path)) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		return Root;
	}

	bool WriteJson(const TSharedPtr<FJsonObject>& Root, const FString& Path)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		return FJsonSerializer::Serialize(Root.ToSharedRef(), Writer) && FFileHelper::SaveStringToFile(Text, *Path);
	}

	TSharedPtr<FJsonObject> Origin(double Lat, double Lon)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("lat"), Lat);
		Object->SetNumberField(TEXT("lon"), Lon);
		return Object;
	}
}

// Saved with the project, read back with it, and forgotten with it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoOriginRoundTripTest, "InNavi.Project.GeoOrigin.RoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGeoOriginRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace GeoOriginTests;
	FNavigationSearchFixture Fixture;
	const FString Database = FPaths::ProjectDir() / TEXT("Data/map_extended.db");
	ANavPlayerController* Controller = Fixture.MakeController();
	ABuildingGenerator* Generator = Fixture.Generator;
	if (!TestTrue(TEXT("the campus database is opened"), Controller->LoadBuildingDataFromPathOrDatabase(Database) > 0)) return false;
	const TOptional<FVector2D> Opened = Generator->GetDatabaseGeoOrigin();
	if (!TestTrue(TEXT("a database project has an origin"), Opened.IsSet())) return false;

	const FString Path = TempProject(TEXT("geo-origin-"));
	if (!TestTrue(TEXT("it is saved as JSON"), Controller->SaveCurrentProjectAs(Path))) return false;
	const TSharedPtr<FJsonObject> Saved = ReadJson(Path);
	if (!TestTrue(TEXT("the file is read"), Saved.IsValid())) return false;
	const TSharedPtr<FJsonObject>* GeoOrigin = nullptr;
	if (TestTrue(TEXT("the file says where its meters are measured from"), Saved->TryGetObjectField(TEXT("geoOrigin"), GeoOrigin)))
	{
		TestTrue(TEXT("the latitude is kept"), FMath::IsNearlyEqual((*GeoOrigin)->GetNumberField(TEXT("lat")), Opened->X, 1.e-9));
		TestTrue(TEXT("and the longitude"), FMath::IsNearlyEqual((*GeoOrigin)->GetNumberField(TEXT("lon")), Opened->Y, 1.e-9));
	}
	TestEqual(TEXT("a project with an origin is version 2"), (int32)Saved->GetNumberField(TEXT("version")), ABuildingGenerator::ProjectFileVersion);

	// Opened again, the project has the same origin.
	TestTrue(TEXT("it is opened again"), Controller->LoadBuildingDataFromPathOrDatabase(Path) > 0);
	const TOptional<FVector2D> Reopened = Fixture.Generator->GetDatabaseGeoOrigin();
	TestTrue(TEXT("the origin is read back"), Reopened.IsSet() && Reopened->Equals(*Opened, 1.e-9));

	// A file without one has none, and does not keep the origin of the project opened before.
	Saved->RemoveField(TEXT("geoOrigin"));
	const FString Without = TempProject(TEXT("geo-origin-none-"));
	TestTrue(TEXT("a copy without the origin is written"), WriteJson(Saved, Without));
	TestTrue(TEXT("and opened"), Controller->LoadBuildingDataFromPathOrDatabase(Without) > 0);
	TestFalse(TEXT("a project without an origin has none"), Fixture.Generator->GetDatabaseGeoOrigin().IsSet());

	// A latitude or longitude that is not one is not taken for an origin.
	for (const TPair<double, double>& Bad : { TPair<double, double>(200.0, 103.9), TPair<double, double>(1.4, 400.0) })
	{
		Saved->SetObjectField(TEXT("geoOrigin"), Origin(Bad.Key, Bad.Value));
		TestTrue(TEXT("a copy with an impossible origin is written"), WriteJson(Saved, Without));
		TestTrue(TEXT("and opened"), Controller->LoadBuildingDataFromPathOrDatabase(Without) > 0);
		TestFalse(TEXT("an impossible origin is ignored"), Fixture.Generator->GetDatabaseGeoOrigin().IsSet());
	}

	// Forgotten with the project.
	TestTrue(TEXT("the project with an origin is opened"), Controller->LoadBuildingDataFromPathOrDatabase(Path) > 0);
	TestTrue(TEXT("it has it"), Fixture.Generator->GetDatabaseGeoOrigin().IsSet());
	Fixture.Generator->ClearAllBuildings();
	TestFalse(TEXT("clearing the project forgets its origin"), Fixture.Generator->GetDatabaseGeoOrigin().IsSet());

	IFileManager::Get().Delete(*Path);
	IFileManager::Get().Delete(*Without);
	return true;
}

// The phone reads the origin of a JSON project, so the GPS dot works in a project of the campus, not only in the database itself.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoOriginMobileTest, "InNavi.Mobile.Source.RealGeoOrigin", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGeoOriginMobileTest::RunTest(const FString& Parameters)
{
	using namespace GeoOriginTests;
	const TSharedPtr<FJsonObject> Fixture = ReadJson(FPaths::ProjectDir() / TEXT("../docs/test-data/e2-test-fixture.json"));
	if (!TestTrue(TEXT("the test fixture is read"), Fixture.IsValid())) return false;

	Fixture->SetObjectField(TEXT("geoOrigin"), Origin(1.4135, 103.9117));
	const FString With = TempProject(TEXT("mobile-geo-"));
	TestTrue(TEXT("a project with an origin is written"), WriteJson(Fixture, With));
	const auto WithData = MakeShared<FMobileRealSource>(With);
	const auto WithSites = WithData->ListSites();
	if (TestEqual(TEXT("it is one site"), WithSites.Num(), 1))
	{
		const auto Found = WithData->GetGeoOrigin(WithSites[0].Id);
		TestTrue(TEXT("the phone reads its origin"), Found.IsSet() && Found->Equals(FVector2D(1.4135, 103.9117), 1.e-9));
	}

	Fixture->RemoveField(TEXT("geoOrigin"));
	const FString Without = TempProject(TEXT("mobile-nogeo-"));
	TestTrue(TEXT("a project without one is written"), WriteJson(Fixture, Without));
	const auto WithoutData = MakeShared<FMobileRealSource>(Without);
	const auto WithoutSites = WithoutData->ListSites();
	if (TestEqual(TEXT("it is one site"), WithoutSites.Num(), 1))
	{
		TestFalse(TEXT("a project without an origin has none, so no GPS dot"), WithoutData->GetGeoOrigin(WithoutSites[0].Id).IsSet());
	}

	IFileManager::Get().Delete(*With);
	IFileManager::Get().Delete(*Without);
	return true;
}

// The GPS dot inside a building: on the plan of the building the device is in (or a few metres from its wall), on whichever level is shown; Locate stays
// on that plan instead of leaving for the campus.
namespace GeoOriginTests
{
	class FFixedLocationSource final : public IMobileLocationSource
	{
	public:
		TOptional<FVector2D> ReadLocation() override { return Fix; }
		TOptional<double> ReadHeading() override { return Heading; }
		TOptional<FVector2D> Fix;
		TOptional<double> Heading;
	};

	// The fix of a point given in metres north and east of the campus origin.
	FVector2D FixAt(const FVector2D& Origin, const FVector2D& Metres)
	{
		return FVector2D(Origin.X + Metres.X / GeoProjection::MetersPerDegreeLat, Origin.Y + Metres.Y / GeoProjection::MetersPerDegreeLon(Origin.X));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGpsIndoorTest, "InNavi.Mobile.GpsIndoor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGpsIndoorTest::RunTest(const FString& Parameters)
{
	using namespace GeoOriginTests;
	const TSharedPtr<FJsonObject> Fixture = ReadJson(FPaths::ProjectDir() / TEXT("../docs/test-data/e2-test-fixture.json"));
	if (!TestTrue(TEXT("the test fixture is read"), Fixture.IsValid())) return false;
	Fixture->SetObjectField(TEXT("geoOrigin"), Origin(1.4135, 103.9117));
	const FString Path = TempProject(TEXT("gps-indoor-"));
	if (!TestTrue(TEXT("a project with an origin is written"), WriteJson(Fixture, Path))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*Path); };

	const auto Data = MakeShared<FMobileRealSource>(Path);
	const auto Sites = Data->ListSites();
	if (!TestEqual(TEXT("it is one site"), Sites.Num(), 1)) return false;
	const TOptional<FVector2D> Geo = Data->GetGeoOrigin(Sites[0].Id);
	if (!TestTrue(TEXT("with an origin"), Geo.IsSet())) return false;
	const FMobileCampusMap Campus = Data->GetCampusMap(Sites[0].Id);
	const FMobileCampusBuilding* Building = Campus.Buildings.FindByPredicate([](const FMobileCampusBuilding& B) { return B.BuildingId.EndsWith(TEXT("E2")); });
	if (!TestNotNull(TEXT("the building E2 is on the campus map"), Building)) return false;
	FVector2D Centre = FVector2D::ZeroVector;
	for (const FVector2D& Corner : Building->Outline) Centre += Corner;
	Centre /= Building->Outline.Num();

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("the test world is created"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* App = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (!TestNotNull(TEXT("the app is created"), App)) return false;
	App->SetSource(Data, Sites[0].Id);
	const TSharedRef<SWidget> Widget = App->TakeWidget();
	auto* Map = App->GetMapView();
	if (!TestNotNull(TEXT("the app has a map"), Map)) return false;
	Map->SetViewSize(375.0, 812.0);
	App->OpenBuilding(Building->BuildingId);
	if (!TestFalse(TEXT("the plan of E2 is shown"), Map->IsOverview())) return false;

	auto Fake = MakeUnique<FFixedLocationSource>();
	auto* Location = Fake.Get();
	App->SetLocationSource(MoveTemp(Fake));

	// A fix inside the building is on its plan, where it is.
	Location->Fix = FixAt(*Geo, Centre);
	App->UpdateLocation(1.f);
	const TOptional<FVector2D> OnPlan = Map->GetGpsLevelPosition();
	if (TestTrue(TEXT("a fix inside the building is on the plan"), OnPlan.IsSet()))
	{
		TestTrue(TEXT("at the place in the building's own units (the campus position less the building's offset)"), OnPlan->Equals(Centre - Building->Offset, 1.e-3));
	}

	// The arrow: the heading of the device reaches the map, and its cone points the way the plan's north and east go on the screen.
	TestFalse(TEXT("no heading, no cone"), Map->GetGpsHeading().IsSet());
	Location->Heading = 0.0;
	App->UpdateLocation(0.2f);
	TestTrue(TEXT("the heading reaches the map"), Map->GetGpsHeading().IsSet() && FMath::IsNearlyEqual(*Map->GetGpsHeading(), 0.0));
	const FVector2D PlanPoint = *OnPlan;
	const TOptional<FVector2D> North = Map->GetGpsHeadingOnScreen(PlanPoint);
	Location->Heading = 90.0;
	App->UpdateLocation(0.2f);
	const TOptional<FVector2D> East = Map->GetGpsHeadingOnScreen(PlanPoint);
	if (TestTrue(TEXT("the cone has a direction"), North.IsSet() && East.IsSet()))
	{
		TestTrue(TEXT("unit vectors"), FMath::IsNearlyEqual(North->Size(), 1.0, 1.e-6) && FMath::IsNearlyEqual(East->Size(), 1.0, 1.e-6));
		TestTrue(TEXT("east is a quarter turn clockwise from north on the plan"), FMath::IsNearlyZero(FVector2D::DotProduct(*North, *East), 1.e-6));
		const FVector2D PlanNorth = Map->ToWidget(PlanPoint + FVector2D(5.0, 0.0), 375.0, 812.0) - Map->ToWidget(PlanPoint, 375.0, 812.0);
		TestTrue(TEXT("north is the way the plan's X goes on the screen"), FVector2D::DotProduct(*North, PlanNorth.GetSafeNormal()) > 0.999);
		// A real project is drawn north up: facing north points to the top of the screen, facing east to the right.
		TestTrue(TEXT("the map has north at the top"), Map->IsPlanNorthOnX());
		TestTrue(TEXT("facing north points up the screen"), North->Y < -0.999 && FMath::Abs(North->X) < 1.e-3);
		TestTrue(TEXT("facing east points to the right"), East->X > 0.999 && FMath::Abs(East->Y) < 1.e-3);
	}
	Location->Heading.Reset();
	App->UpdateLocation(0.2f);
	TestFalse(TEXT("a device without a compass has no cone"), Map->GetGpsHeading().IsSet());

	App->LocateMe();
	TestFalse(TEXT("Locate inside the building stays on its plan"), Map->IsOverview());
	TestTrue(TEXT("and still has the dot"), Map->GetGpsLevelPosition().IsSet());

	// A fix far from the building is not on its plan.
	Location->Fix = FixAt(*Geo, Centre + FVector2D(500.0, 0.0));
	App->UpdateLocation(1.f);
	TestFalse(TEXT("a fix 500 m away is not on the plan"), Map->GetGpsLevelPosition().IsSet());
	TestTrue(TEXT("but is still the campus's"), Map->GetGpsPosition().IsSet());
	App->LocateMe();
	TestTrue(TEXT("Locate outside the building goes to the campus overview"), Map->IsOverview());
	return true;
}

// -GpsFake=<latitude>,<longitude> stands in for the device's GPS in a build that is not Shipping.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGpsFakeParseTest, "InNavi.Mobile.GpsFakeParse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGpsFakeParseTest::RunTest(const FString& Parameters)
{
	const TOptional<FVector2D> Fake = ParseFakeMobileLocation(TEXT("InNavi -game -GpsFake=1.4141,103.9125 -windowed"));
	if (TestTrue(TEXT("a latitude and longitude are read"), Fake.IsSet()))
	{
		TestTrue(TEXT("latitude first, then longitude"), Fake->Equals(FVector2D(1.4141, 103.9125), 1.e-9));
	}
	const TOptional<FVector2D> South = ParseFakeMobileLocation(TEXT("-GpsFake=-33.8688,151.2093"));
	TestTrue(TEXT("a negative one too"), South.IsSet() && South->Equals(FVector2D(-33.8688, 151.2093), 1.e-9));
	TestFalse(TEXT("no argument, no fake"), ParseFakeMobileLocation(TEXT("InNavi -game")).IsSet());
	TestFalse(TEXT("words are not a position"), ParseFakeMobileLocation(TEXT("-GpsFake=here,there")).IsSet());
	TestFalse(TEXT("one number is not a position"), ParseFakeMobileLocation(TEXT("-GpsFake=1.41")).IsSet());
	TestFalse(TEXT("an impossible latitude is not one"), ParseFakeMobileLocation(TEXT("-GpsFake=200,103.9")).IsSet());
	TestFalse(TEXT("an impossible longitude is not one"), ParseFakeMobileLocation(TEXT("-GpsFake=1.4,400")).IsSet());

	const TOptional<double> Heading = ParseFakeMobileHeading(TEXT("InNavi -GpsFake=1.4,103.9 -GpsFakeHeading=90"));
	TestTrue(TEXT("a heading is read"), Heading.IsSet() && FMath::IsNearlyEqual(*Heading, 90.0));
	const TOptional<double> Wrapped = ParseFakeMobileHeading(TEXT("-GpsFakeHeading=-90"));
	TestTrue(TEXT("a heading is kept between 0 and 360"), Wrapped.IsSet() && FMath::IsNearlyEqual(*Wrapped, 270.0));
	TestFalse(TEXT("no heading argument, no heading"), ParseFakeMobileHeading(TEXT("-GpsFake=1.4,103.9")).IsSet());
	TestFalse(TEXT("a heading that is not a number is none"), ParseFakeMobileHeading(TEXT("-GpsFakeHeading=north")).IsSet());
	return true;
}

#endif
