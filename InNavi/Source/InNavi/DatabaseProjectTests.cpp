#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "BuildingTypes.h"
#include "ContextBuildingActor.h"
#include "NavPlayerController.h"
#include "PolyBuilding.h"
#include "POIMarkerActor.h"
#include "RoadSegmentActor.h"
#include "UiTestFixture.h"

#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace DatabaseProjectTests
{
	// What a project shows of the map database: the campus buildings by type, and how many of the rest.
	struct FMapSnapshot
	{
		TMap<FString, int32> CampusTypes;
		TMap<FString, int32> ContextTypes;
		int32 Roads = 0;
		int32 Pois = 0;
	};

	FMapSnapshot Describe(UWorld* World, const ABuildingGenerator& Generator)
	{
		FMapSnapshot Scene;
		for (const APolyBuilding* Building : Generator.GetAllBuildings())
		{
			Scene.CampusTypes.FindOrAdd(Building->GetBuildingType())++;
		}
		for (TActorIterator<AContextBuildingActor> It(World); It; ++It)
		{
			Scene.ContextTypes.FindOrAdd(It->GetBuildingType())++;
		}
		for (TActorIterator<ARoadSegmentActor> It(World); It; ++It)
		{
			++Scene.Roads;
		}
		for (TActorIterator<APOIMarkerActor> It(World); It; ++It)
		{
			++Scene.Pois;
		}
		return Scene;
	}

	FString Text(const TMap<FString, int32>& Counts)
	{
		TArray<FString> Parts;
		for (const TPair<FString, int32>& Pair : Counts)
		{
			Parts.Add(FString::Printf(TEXT("%s=%d"), *Pair.Key, Pair.Value));
		}
		Parts.Sort();
		return FString::Join(Parts, TEXT(", "));
	}
}

// A map database saved as a project keeps all of it: the campus buildings with their types, the buildings around the campus with theirs,
// the roads and the places of interest.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDatabaseProjectKeepsEverythingTest, "InNavi.Project.Database.SaveKeepsEverything", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDatabaseProjectKeepsEverythingTest::RunTest(const FString& Parameters)
{
	using namespace DatabaseProjectTests;
	FNavigationSearchFixture Fixture;
	const FString Database = FPaths::ProjectDir() / TEXT("Data/map_extended.db");
	if (!TestTrue(TEXT("the campus database is there"), IFileManager::Get().FileExists(*Database)))
	{
		return false;
	}
	ANavPlayerController* Controller = Fixture.MakeController();
	ABuildingGenerator* Generator = Fixture.Generator;
	if (!TestTrue(TEXT("the database is opened"), Controller->LoadBuildingDataFromPathOrDatabase(Database) > 0))
	{
		return false;
	}
	const FMapSnapshot Opened = Describe(Fixture.World, *Generator);
	AddInfo(FString::Printf(TEXT("opened: campus [%s] context [%s] roads %d pois %d"), *Text(Opened.CampusTypes), *Text(Opened.ContextTypes), Opened.Roads, Opened.Pois));
	TestTrue(TEXT("the campus has a type besides Other"), Opened.CampusTypes.Num() > 1 || !Opened.CampusTypes.Contains(BuildingTypes::Default()));
	TestTrue(TEXT("the database has buildings around the campus and roads"), Opened.ContextTypes.Num() > 0 && Opened.Roads > 0);
	TestEqual(TEXT("its places of interest (the coloured spheres) are not shown"), Opened.Pois, 0);

	const FString Path = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("database-project-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
	if (!TestTrue(TEXT("the project is saved as JSON"), Controller->SaveCurrentProjectAs(Path)))
	{
		return false;
	}
	FString SavedText;
	TSharedPtr<FJsonObject> SavedRoot;
	if (TestTrue(TEXT("the saved file is read"), FFileHelper::LoadFileToString(SavedText, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SavedText), SavedRoot) && SavedRoot.IsValid()))
	{
		TestEqual(TEXT("a project with the map is version 2, which an older build does not take for its own"), (int32)SavedRoot->GetNumberField(TEXT("version")), ABuildingGenerator::ProjectFileVersion);
	}
	TestTrue(TEXT("and opened again"), Controller->LoadBuildingDataFromPathOrDatabase(Path) > 0);
	TestEqual(TEXT("it reads back as version 2"), Generator->GetLoadedFileVersion(), ABuildingGenerator::ProjectFileVersion);
	const FMapSnapshot Reopened = Describe(Fixture.World, *Generator);
	AddInfo(FString::Printf(TEXT("reopened: campus [%s] context [%s] roads %d pois %d"), *Text(Reopened.CampusTypes), *Text(Reopened.ContextTypes), Reopened.Roads, Reopened.Pois));
	IFileManager::Get().Delete(*Path);

	TestEqual(TEXT("each campus type is kept"), Text(Reopened.CampusTypes), Text(Opened.CampusTypes));
	TestEqual(TEXT("each type of the buildings around the campus is kept"), Text(Reopened.ContextTypes), Text(Opened.ContextTypes));
	TestEqual(TEXT("the roads are kept"), Reopened.Roads, Opened.Roads);
	TestEqual(TEXT("and still none after reopening"), Reopened.Pois, 0);
	TestFalse(TEXT("none are written to the file"), SavedRoot.IsValid() && SavedRoot->HasField(TEXT("pois")));
	return true;
}

// The map of a project file is read with care: a point that is not two numbers is left out (not read as 0), a road needs two points and a
// building around the campus three, and opening a project ends the tools of the one that was open.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDatabaseProjectCarefulReadTest, "InNavi.Project.Database.MapIsReadWithCare", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDatabaseProjectCarefulReadTest::RunTest(const FString& Parameters)
{
	using namespace DatabaseProjectTests;
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	const FString Path = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("map-care-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
	const FString Json = TEXT("{ \"version\": 1, \"scale\": 100, \"buildings\": [], ")
		TEXT("\"roads\": [ { \"highway\": \"footway\", \"points\": [[0, 0], [1, \"x\"]] }, { \"highway\": \"footway\", \"points\": [[0, 0], [5, 5]] }, ")
		TEXT("{ \"highway\": \"footway\", \"points\": [[0, 0], [null, 3], [7, 7]] } ], ")
		TEXT("\"contextBuildings\": [ { \"buildingType\": \"school\", \"footprint\": [[0, 0], [4, 0]] }, { \"buildingType\": \"school\", \"footprint\": [[0, 0], [4, 0], [4, 4]] } ], ")
		TEXT("\"pois\": [ { \"poiType\": \"shop\", \"position\": [2, 2] } ] }");
	if (!TestTrue(TEXT("the file is written"), FFileHelper::SaveStringToFile(Json, *Path)))
	{
		return false;
	}

	// Tools in use in the project that is open.
	TestTrue(TEXT("a tool is on"), (Controller->ToggleSelectMode(), Controller->IsInSelectMode()));
	TestTrue(TEXT("the project is opened"), Controller->TryLoadBuildingDataFromPathOrDatabase(Path));
	IFileManager::Get().Delete(*Path);
	TestFalse(TEXT("opening a project ends the tools"), Controller->IsInSelectMode());

	const FMapSnapshot Map = Describe(Fixture.World, *Fixture.Generator);
	TestEqual(TEXT("two of the three roads have two points that are numbers"), Map.Roads, 2);
	TestEqual(TEXT("one of the two buildings around the campus has three points"), Map.ContextTypes.FindRef(TEXT("School")), 1);
	TestEqual(TEXT("a places-of-interest list in an older file is read past"), Map.Pois, 0);
	return true;
}

#endif
