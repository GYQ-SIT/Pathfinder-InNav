#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "MobileRealSource.h"
#include "NavPlayerController.h"
#include "UiTestFixture.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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

#endif
