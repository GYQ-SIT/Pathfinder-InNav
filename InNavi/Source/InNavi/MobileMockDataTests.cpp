#if WITH_DEV_AUTOMATION_TESTS

#include "MobileMockSource.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace MobileMockDataTests
{
	// The prototype file, changed by Edit and written under Saved/AutomationTemp; the path is returned.
	FString WriteVariant(const TFunctionRef<void(const TSharedPtr<FJsonObject>&)>& Edit)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *FMobileMockSource::DefaultDataFile());
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		Edit(Root);
		FString Out;
		FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Out));
		const FString Path = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("mock-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
		FFileHelper::SaveStringToFile(Out, *Path);
		return Path;
	}
}

// The phone prototype's data is a file, not code, and says that it is made up.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMockDataFileTest, "InNavi.Mobile.MockData.File", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMockDataFileTest::RunTest(const FString& Parameters)
{
	FMobileMockSource Source;
	if (!TestTrue(TEXT("the prototype file is read"), Source.IsLoaded())) return false;
	TestTrue(TEXT("with nothing to complain about"), Source.GetLoadError().IsEmpty());
	TestTrue(TEXT("the screens are told it is simulated"), Source.GetDataNote().Contains(TEXT("simulated")));

	const TArray<FMobileSite> Sites = Source.ListSites();
	TestEqual(TEXT("three sites"), Sites.Num(), 3);
	const FMobileSite* Campus = Sites.FindByPredicate([](const FMobileSite& Site) { return Site.Id == TEXT("sit-punggol"); });
	const FMobileSite* Mall = Sites.FindByPredicate([](const FMobileSite& Site) { return Site.Id == TEXT("paya-lebar"); });
	if (!TestNotNull(TEXT("the campus"), Campus) || !TestNotNull(TEXT("the mall"), Mall)) return false;
	TestEqual(TEXT("the campus has three buildings"), Campus->BuildingCount, 3);
	TestEqual(TEXT("and its tallest has four levels (E6 has a mezzanine)"), Campus->LevelCount, 4);
	TestEqual(TEXT("the mall has seven levels"), Mall->LevelCount, 7);
	TestEqual(TEXT("four places on each of the campus's nine levels"), Source.SearchPlaces(Campus->Id, FString(), FMobilePlaceFilter()).Num(), 36);
	TestEqual(TEXT("and on the mall's seven"), Source.SearchPlaces(Mall->Id, FString(), FMobilePlaceFilter()).Num(), 28);

	// The code printed on a door is made by the pattern of the site: a campus "E2-01-101", a mall "#01-101", the mezzanine "E6-2M-101".
	FMobilePlace Place;
	TestTrue(TEXT("a campus place"), Source.FindPlace(TEXT("E2-01-101"), Place) && Place.Code == TEXT("E2-01-101"));
	TestTrue(TEXT("a mall place"), Source.FindPlace(TEXT("PLM-01-101"), Place) && Place.Code == TEXT("#01-101"));
	TestTrue(TEXT("a place on the mezzanine"), Source.FindPlace(TEXT("E6-2M-102"), Place) && Place.Code == TEXT("E6-2M-102") && Place.Level == 3);
	TestTrue(TEXT("the mall's second restaurant is closed on level 1"), Source.FindPlace(TEXT("PLM-01-102"), Place) && !Place.bAvailable);
	TestTrue(TEXT("and open above"), Source.FindPlace(TEXT("PLM-02-102"), Place) && Place.bAvailable);

	TestEqual(TEXT("the campus map has an outline for each building"), Source.GetCampusMap(Campus->Id).Buildings.Num(), 3);
	TestEqual(TEXT("and a pond and a courtyard"), Source.GetCampusMap(Campus->Id).Features.Num(), 2);
	FMobileLevelMap Level;
	TestTrue(TEXT("a level has a map"), Source.GetLevelMap(TEXT("E2"), 1, Level) && Level.Rooms.Num() == 4 && Level.Connectors.Num() == 2);
	TestTrue(TEXT("and the mall's has an escalator too"), Source.GetLevelMap(TEXT("PLM"), 2, Level) && Level.Connectors.Num() == 3);
	TestTrue(TEXT("a walk is made from it"), Source.FindRoute({ TEXT(""), TEXT("E6-2M-102") }).bFound);
	return true;
}

// A file that is missing, that does not read, or that does not say it is made up gives a source with nothing in it and a reason, never a crash.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMockDataRefusedTest, "InNavi.Mobile.MockData.Refused", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMockDataRefusedTest::RunTest(const FString& Parameters)
{
	using namespace MobileMockDataTests;
	auto Check = [this](const TCHAR* What, const FString& Path, const TCHAR* ExpectedWords)
	{
		FMobileMockSource Source(Path);
		TestFalse(*FString::Printf(TEXT("%s: nothing is loaded"), What), Source.IsLoaded());
		TestTrue(*FString::Printf(TEXT("%s: and it says why (%s)"), What, *Source.GetLoadError()), Source.GetLoadError().Contains(ExpectedWords));
		TestTrue(*FString::Printf(TEXT("%s: no sites"), What), Source.ListSites().IsEmpty());
		TestFalse(*FString::Printf(TEXT("%s: no route"), What), Source.FindRoute({ TEXT(""), TEXT("E2-01-101") }).bFound);
		FMobilePlace Place;
		TestFalse(*FString::Printf(TEXT("%s: no place"), What), Source.FindPlace(TEXT("E2-01-101"), Place));
	};
	Check(TEXT("missing file"), FPaths::ProjectSavedDir() / TEXT("AutomationTemp/no-such-file.json"), TEXT("missing"));
	const FString NotJson = FPaths::ProjectSavedDir() / TEXT("AutomationTemp/mock-not-json.json");
	FFileHelper::SaveStringToFile(TEXT("{ this is not json"), *NotJson);
	Check(TEXT("not JSON"), NotJson, TEXT("not valid JSON"));
	Check(TEXT("another format"), WriteVariant([](const TSharedPtr<FJsonObject>& Root) { Root->SetStringField(TEXT("format"), TEXT("something-else")); }), TEXT("innavi-mobile-prototype"));
	Check(TEXT("another version"), WriteVariant([](const TSharedPtr<FJsonObject>& Root) { Root->SetNumberField(TEXT("version"), 2); }), TEXT("version 1"));
	Check(TEXT("data that does not say it is made up"), WriteVariant([](const TSharedPtr<FJsonObject>& Root) { Root->SetBoolField(TEXT("synthetic"), false); }), TEXT("synthetic"));
	Check(TEXT("a site that uses a place set that is not there"), WriteVariant([](const TSharedPtr<FJsonObject>& Root)
	{
		TArray<TSharedPtr<FJsonValue>> Sites = Root->GetArrayField(TEXT("sites"));
		Sites[0]->AsObject()->SetStringField(TEXT("placeSet"), TEXT("nowhere"));
		Root->SetArrayField(TEXT("sites"), Sites);
	}), TEXT("nowhere"));
	Check(TEXT("a plan that is incomplete"), WriteVariant([](const TSharedPtr<FJsonObject>& Root) { Root->RemoveField(TEXT("plan")); }), TEXT("plan"));
	return true;
}

#endif
