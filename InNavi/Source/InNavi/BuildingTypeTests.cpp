#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "BuildingInfoPanelWidget.h"
#include "BuildingTypes.h"
#include "ContextBuildingActor.h"
#include "EditScreenWidget.h"
#include "MapPanelWidget.h"
#include "PolyBuilding.h"
#include "UiTestFixture.h"

#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace BuildingTypeTests
{
	TSharedPtr<FJsonObject> ReadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		}
		return Root;
	}

	// The building with this id in a project file, or null.
	TSharedPtr<FJsonObject> FindSavedBuilding(const TSharedPtr<FJsonObject>& Project, const FString& Id)
	{
		const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
		if (Project.IsValid() && Project->TryGetArrayField(TEXT("buildings"), Buildings))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Buildings)
			{
				const TSharedPtr<FJsonObject> Building = Value->AsObject();
				if (Building.IsValid() && Building->GetStringField(TEXT("id")) == Id)
				{
					return Building;
				}
			}
		}
		return nullptr;
	}

	FString TemporaryProjectPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("building-types-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
	}

	TArray<FVector2D> TestOutline()
	{
		return { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 300), FVector2D(0, 300) };
	}

	int32 CountHiddenContextBuildings(UWorld* World, const FString& OnlyOfType = FString())
	{
		int32 Count = 0;
		for (TActorIterator<AContextBuildingActor> It(World); It; ++It)
		{
			if (It->IsHidden() && (OnlyOfType.IsEmpty() || It->GetBuildingType() == OnlyOfType))
			{
				++Count;
			}
		}
		return Count;
	}

	int32 CountContextBuildings(UWorld* World, const FString& OnlyOfType = FString())
	{
		int32 Count = 0;
		for (TActorIterator<AContextBuildingActor> It(World); It; ++It)
		{
			if (OnlyOfType.IsEmpty() || It->GetBuildingType() == OnlyOfType)
			{
				++Count;
			}
		}
		return Count;
	}
}

// The list: Other last, nothing twice, a colour of its own for every type, and the way a value is read as a type.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingTypesListTest, "InNavi.Building.Types.List", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingTypesListTest::RunTest(const FString& Parameters)
{
	const TArray<FString>& All = BuildingTypes::All();
	TestTrue(TEXT("there is more than one type"), All.Num() > 1);
	TestEqual(TEXT("Other is last"), All.Last(), BuildingTypes::Default());
	TestEqual(TEXT("no type is on the list twice"), TSet<FString>(All).Num(), All.Num());
	for (int32 First = 0; First < All.Num(); ++First)
	{
		for (int32 Second = First + 1; Second < All.Num(); ++Second)
		{
			TestFalse(*FString::Printf(TEXT("%s and %s have different colours"), *All[First], *All[Second]),
				BuildingTypes::Color(All[First]).Equals(BuildingTypes::Color(All[Second]), 0.001f));
		}
	}
	TestTrue(TEXT("a value that is not a type has the colour of Other"), BuildingTypes::Color(TEXT("Castle")).Equals(BuildingTypes::Color(BuildingTypes::Default()), 0.001f));

	TestEqual(TEXT("case and spaces do not matter"), BuildingTypes::Normalize(TEXT("  uNiVeRsity ")), FString(TEXT("University")));
	TestEqual(TEXT("a value that is not a type is Other"), BuildingTypes::Normalize(TEXT("Castle")), BuildingTypes::Default());
	TestEqual(TEXT("so is nothing"), BuildingTypes::Normalize(FString()), BuildingTypes::Default());

	// What the map database calls its buildings, as the list names them.
	const TCHAR* const Mappings[][2] = {
		{ TEXT("university"), TEXT("University") }, { TEXT("school"), TEXT("School") }, { TEXT("kindergarten"), TEXT("School") },
		{ TEXT("education"), TEXT("School") }, { TEXT("residential"), TEXT("Residential") }, { TEXT("house"), TEXT("Residential") },
		{ TEXT("detached"), TEXT("Residential") }, { TEXT("commercial"), TEXT("Commercial") }, { TEXT("retail"), TEXT("Commercial") },
		{ TEXT("public"), TEXT("Public") }, { TEXT("church"), TEXT("Public") }, { TEXT("train_station"), TEXT("Transport") },
		{ TEXT("parking"), TEXT("Transport") }, { TEXT("bridge"), TEXT("Transport") }, { TEXT("transportation"), TEXT("Transport") },
		{ TEXT("utility"), TEXT("Utility") }, { TEXT("yes"), TEXT("Other") }, { TEXT("roof"), TEXT("Other") },
		{ TEXT("construction"), TEXT("Other") }, { TEXT("proposed"), TEXT("Other") }, { TEXT("a made-up value"), TEXT("Other") } };
	for (const TCHAR* const (&Mapping)[2] : Mappings)
	{
		TestEqual(*FString::Printf(TEXT("the database's \"%s\""), Mapping[0]), BuildingTypes::FromMapDatabase(Mapping[0]), FString(Mapping[1]));
		TestTrue(*FString::Printf(TEXT("\"%s\" is on the list"), Mapping[1]), All.Contains(Mapping[1]));
	}
	return true;
}

// A building is drawn in the colour of its type; the highlight, and a rebuild of the building, do not lose it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingTypesAppearanceTest, "InNavi.Building.Types.Appearance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingTypesAppearanceTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	APolyBuilding* Building = Fixture.Generator->SpawnBuildingFromData(TEXT("TYPE-APPEARANCE"), TEXT("Appearance"), BuildingTypeTests::TestOutline(), 3, 300.f, FVector2D(9000.0, 9000.0));
	if (!TestNotNull(TEXT("the building"), Building))
	{
		return false;
	}
	const auto HasColor = [Building](const FString& Type)
	{
		return Building->GetBodyColorForTests().Equals(BuildingTypes::Color(Type), 0.001f);
	};

	TestEqual(TEXT("a new building is Other"), Building->GetBuildingType(), BuildingTypes::Default());
	TestTrue(TEXT("and has its colour"), HasColor(BuildingTypes::Default()));
	Building->SetBuildingType(TEXT("university"));
	TestEqual(TEXT("the type is the list's spelling"), Building->GetBuildingType(), FString(TEXT("University")));
	TestTrue(TEXT("the colour follows the type"), HasColor(TEXT("University")));

	Building->SetHighlighted(true);
	TestFalse(TEXT("a highlighted building is not in the colour of its type"), HasColor(TEXT("University")));
	Building->SetBuildingType(TEXT("School"));
	Building->SetHighlighted(false);
	TestTrue(TEXT("when the highlight goes the colour is the one of the type it has by then"), HasColor(TEXT("School")));

	Building->SetFloorCount(5); // rebuilds the whole mesh
	TestTrue(TEXT("a rebuilt building keeps the colour of its type"), HasColor(TEXT("School")));
	Building->SetBuildingType(TEXT("Castle"));
	TestEqual(TEXT("a value that is not a type is Other"), Building->GetBuildingType(), BuildingTypes::Default());
	return true;
}

// The type is in the project file (left out for Other), is read back, and an older file or an unknown value is Other.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingTypesSaveLoadTest, "InNavi.Building.Types.SaveAndLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingTypesSaveLoadTest::RunTest(const FString& Parameters)
{
	using namespace BuildingTypeTests;
	FNavigationSearchFixture Fixture;
	ABuildingGenerator* Generator = Fixture.Generator;
	Fixture.Interior->ClearLoadedInterior();
	Generator->ClearAllBuildings();
	Generator->SpawnBuildingFromData(TEXT("TYPE-A"), TEXT("Typed"), TestOutline(), 2, 300.f, FVector2D(0.0, 0.0));
	Generator->SpawnBuildingFromData(TEXT("TYPE-B"), TEXT("Untyped"), TestOutline(), 2, 300.f, FVector2D(9000.0, 0.0));
	TestTrue(TEXT("a building takes a type"), Generator->SetBuildingType(TEXT("TYPE-A"), TEXT("school")));
	TestFalse(TEXT("a building that is not there does not"), Generator->SetBuildingType(TEXT("NOBODY"), TEXT("School")));

	const FString Path = TemporaryProjectPath();
	if (!TestTrue(TEXT("the project is saved"), Generator->SaveBuildingsToFile(Path)))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Saved = ReadJson(Path);
	IFileManager::Get().Delete(*Path);
	const TSharedPtr<FJsonObject> SavedA = FindSavedBuilding(Saved, TEXT("TYPE-A"));
	const TSharedPtr<FJsonObject> SavedB = FindSavedBuilding(Saved, TEXT("TYPE-B"));
	if (!TestNotNull(TEXT("the typed building is in the file"), SavedA.Get()) || !TestNotNull(TEXT("the other building too"), SavedB.Get()))
	{
		return false;
	}
	TestEqual(TEXT("the type is written as the list spells it"), SavedA->GetStringField(TEXT("buildingType")), FString(TEXT("School")));
	TestFalse(TEXT("Other is left out"), SavedB->HasField(TEXT("buildingType")));

	// Read back from the file just saved.
	if (!TestTrue(TEXT("the project is saved again"), Generator->SaveBuildingsToFile(Path)))
	{
		return false;
	}
	Generator->ClearAllBuildings();
	TestEqual(TEXT("both buildings are loaded"), Generator->LoadBuildingsFromFile(Path), 2);
	IFileManager::Get().Delete(*Path);
	TestEqual(TEXT("the type comes back"), Generator->GetBuildingType(TEXT("TYPE-A")), FString(TEXT("School")));
	TestEqual(TEXT("the other is still Other"), Generator->GetBuildingType(TEXT("TYPE-B")), BuildingTypes::Default());

	// A file made by hand: no field (an older file), a type in other spelling, and a value that is not a type.
	const FString Footprint = TEXT("\"footprint\": [[0, 0], [100, 0], [100, 100], [0, 100]]");
	const FString Text = FString::Printf(TEXT("{ \"version\": 1, \"scale\": 100, \"buildings\": [ { \"id\": \"H1\", \"name\": \"Older\", %s }, ")
		TEXT("{ \"id\": \"H2\", \"name\": \"Spelled\", \"buildingType\": \"commercial\", %s }, ")
		TEXT("{ \"id\": \"H3\", \"name\": \"Unknown\", \"buildingType\": \"Castle\", %s } ] }"), *Footprint, *Footprint, *Footprint);
	if (!TestTrue(TEXT("the file is written"), FFileHelper::SaveStringToFile(Text, *Path)))
	{
		return false;
	}
	Generator->ClearAllBuildings();
	TestEqual(TEXT("its three buildings are loaded"), Generator->LoadBuildingsFromFile(Path), 3);
	IFileManager::Get().Delete(*Path);
	TestEqual(TEXT("a file without the field gives Other"), Generator->GetBuildingType(TEXT("H1")), BuildingTypes::Default());
	TestEqual(TEXT("another spelling gives the type"), Generator->GetBuildingType(TEXT("H2")), FString(TEXT("Commercial")));
	TestEqual(TEXT("a value that is not a type gives Other"), Generator->GetBuildingType(TEXT("H3")), BuildingTypes::Default());
	return true;
}

// Hiding a type hides the buildings of it, and combines with the Campus switch; changing a building's type moves it in or out.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingTypesFilterTest, "InNavi.Building.Types.Filter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingTypesFilterTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ABuildingGenerator* Generator = Fixture.Generator;
	APolyBuilding* First = Generator->FindBuildingById(TEXT("E1"));
	APolyBuilding* Second = Generator->FindBuildingById(TEXT("E2"));
	if (!TestNotNull(TEXT("E1"), First) || !TestNotNull(TEXT("E2"), Second))
	{
		return false;
	}
	Generator->SetBuildingType(TEXT("E1"), TEXT("University"));
	Generator->SetBuildingType(TEXT("E2"), TEXT("School"));
	const int32 AllBuildings = Generator->GetFilteredBuildings().Num();
	TestTrue(TEXT("both are listed to start with"), Generator->GetFilteredBuildings().Contains(First) && Generator->GetFilteredBuildings().Contains(Second));

	const uint64 Revision = Generator->GetBuildingResultsRevision();
	Generator->SetBuildingTypeVisible(TEXT("University"), false);
	TestFalse(TEXT("University is hidden"), Generator->IsBuildingTypeVisible(TEXT("University")));
	TestTrue(TEXT("the results changed, so the lists follow"), Generator->GetBuildingResultsRevision() != Revision);
	TestFalse(TEXT("its building is not listed"), Generator->GetFilteredBuildings().Contains(First));
	TestTrue(TEXT("and is hidden in the scene"), First->IsHidden());
	TestTrue(TEXT("the other type is still listed"), Generator->GetFilteredBuildings().Contains(Second));
	TestFalse(TEXT("and shown"), Second->IsHidden());

	// The Campus switch and the type filter both have to allow a building.
	Generator->SetCampusBuildingsVisible(false);
	TestEqual(TEXT("without Campus nothing is listed"), Generator->GetFilteredBuildings().Num(), 0);
	Generator->SetCampusBuildingsVisible(true);
	TestFalse(TEXT("with Campus back the hidden type stays hidden"), Generator->GetFilteredBuildings().Contains(First));

	// A building that changes type goes by the filter of its new type.
	Generator->SetBuildingTypeVisible(TEXT("School"), false);
	TestFalse(TEXT("School is hidden too"), Generator->GetFilteredBuildings().Contains(Second));
	Generator->SetBuildingType(TEXT("E2"), TEXT("Other"));
	TestTrue(TEXT("a building that becomes Other is listed"), Generator->GetFilteredBuildings().Contains(Second));
	TestFalse(TEXT("and shown"), Second->IsHidden());
	Generator->SetBuildingType(TEXT("E2"), TEXT("School"));
	TestFalse(TEXT("one that becomes School is not"), Generator->GetFilteredBuildings().Contains(Second));
	TestTrue(TEXT("and is hidden"), Second->IsHidden());

	Generator->ResetBuildingTypeFilter();
	TestEqual(TEXT("Reset shows everything again"), Generator->GetFilteredBuildings().Num(), AllBuildings);
	TestFalse(TEXT("also in the scene"), First->IsHidden() || Second->IsHidden());
	TestTrue(TEXT("every type is shown"), Generator->IsBuildingTypeVisible(TEXT("University")) && Generator->IsBuildingTypeVisible(TEXT("School")));
	return true;
}

// The real campus: campus and non-campus buildings take their type from the database, and the filter works on both.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingTypesDatabaseTest, "InNavi.Building.Types.Database", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingTypesDatabaseTest::RunTest(const FString& Parameters)
{
	using namespace BuildingTypeTests;
	FNavigationSearchFixture Fixture;
	ABuildingGenerator* Generator = Fixture.Generator;
	const FString Database = FPaths::ProjectDir() / TEXT("Data/map_extended.db");
	if (!TestTrue(TEXT("the campus database is there"), IFileManager::Get().FileExists(*Database)))
	{
		return false;
	}
	Fixture.Interior->ClearLoadedInterior();
	Generator->ClearAllBuildings();
	Generator->ClearAllRoadsAndPOIs();
	Generator->ClearAllContextBuildings();
	if (!TestTrue(TEXT("the campus is loaded"), Generator->LoadBuildingsFromDatabase(Database) > 0))
	{
		return false;
	}
	TestTrue(TEXT("the context buildings are loaded"), Generator->LoadContextBuildingsFromDatabase(Database) > 100);

	TMap<FString, int32> CampusTypes;
	for (const APolyBuilding* Building : Generator->GetFilteredBuildings())
	{
		CampusTypes.FindOrAdd(Building->GetBuildingType())++;
		TestTrue(*FString::Printf(TEXT("%s has a type of the list"), *Building->BuildingId), BuildingTypes::All().Contains(Building->GetBuildingType()));
	}
	TestTrue(TEXT("the campus has university buildings"), CampusTypes.FindRef(TEXT("University")) > 0);
	TestTrue(TEXT("and walkways and bridges, which are Transport"), CampusTypes.FindRef(TEXT("Transport")) > 0);

	UWorld* World = Fixture.World;
	const int32 Context = CountContextBuildings(World);
	const int32 Residential = CountContextBuildings(World, TEXT("Residential"));
	TestTrue(TEXT("most of the context buildings are houses and flats"), Residential > 0 && Residential < Context);
	for (TActorIterator<AContextBuildingActor> It(World); It; ++It)
	{
		if (!It->GetBodyColorForTests().Equals(BuildingTypes::Color(It->GetBuildingType()), 0.001f))
		{
			AddError(TEXT("a context building is not in the colour of its type"));
			break;
		}
	}
	TestEqual(TEXT("none is hidden to start with"), CountHiddenContextBuildings(World), 0);

	Generator->SetBuildingTypeVisible(TEXT("Residential"), false);
	TestEqual(TEXT("hiding Residential hides the Residential ones"), CountHiddenContextBuildings(World, TEXT("Residential")), Residential);
	TestEqual(TEXT("and only those"), CountHiddenContextBuildings(World), Residential);
	Generator->SetNonCampusBuildingsVisible(false);
	TestEqual(TEXT("the Non-Campus switch hides them all"), CountHiddenContextBuildings(World), Context);
	Generator->SetNonCampusBuildingsVisible(true);
	TestEqual(TEXT("and with it back the hidden type is still hidden"), CountHiddenContextBuildings(World), Residential);
	Generator->ResetBuildingTypeFilter();
	TestEqual(TEXT("Reset shows them again"), CountHiddenContextBuildings(World), 0);
	return true;
}

// The controller: choosing the selected building's type marks the project as changed, but only when the type is another.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingTypesControllerTest, "InNavi.Building.Types.Controller", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingTypesControllerTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	TestFalse(TEXT("with nothing selected there is no type to set"), Controller->SetSelectedBuildingType(TEXT("School")));
	TestFalse(TEXT("and nothing changed"), Controller->HasUnsavedChanges());

	if (!TestTrue(TEXT("E1 can be selected"), Controller->SelectBuildingById(TEXT("E1"))))
	{
		return false;
	}
	TestEqual(TEXT("it is Other to start with"), Controller->GetSelectedBuildingType(), BuildingTypes::Default());
	TestTrue(TEXT("setting the type it has works"), Controller->SetSelectedBuildingType(TEXT("Other")));
	TestFalse(TEXT("but changes nothing"), Controller->HasUnsavedChanges());
	TestTrue(TEXT("setting another type works"), Controller->SetSelectedBuildingType(TEXT("School")));
	TestEqual(TEXT("the building has it"), Controller->GetSelectedBuildingType(), FString(TEXT("School")));
	TestTrue(TEXT("and the project is marked as changed"), Controller->HasUnsavedChanges());
	return true;
}

// The two panels: the Building panel's drop-down sets the type; the Map panel has a check box for every type.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingTypesPanelsTest, "InNavi.Building.Types.Panels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingTypesPanelsTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	UEditScreenWidget* Exterior = NewObject<UEditScreenWidget>();
	Exterior->Initialize(); Exterior->TakeWidget(); Exterior->SetOwningPlayer(Controller);
	UBuildingInfoPanelWidget* Panel = Exterior->GetBuildingInfoPanelForTests();
	UMapPanelWidget* Map = Exterior->GetMapPanelForTests();
	if (!TestNotNull(TEXT("the Building panel"), Panel) || !TestNotNull(TEXT("the Map panel"), Map))
	{
		return false;
	}

	// The Building panel.
	TestTrue(TEXT("E1 can be selected"), Controller->SelectBuildingById(TEXT("E1")));
	Panel->Refresh();
	TestEqual(TEXT("the drop-down lists every type"), Panel->GetTypeOptionCountForTests(), BuildingTypes::All().Num());
	TestEqual(TEXT("it shows the type of the building"), Panel->GetTypeTextForTests(), BuildingTypes::Default());
	Panel->SelectTypeForTests(TEXT("Commercial"));
	TestEqual(TEXT("choosing a type sets it on the building"), Controller->GetSelectedBuildingType(), FString(TEXT("Commercial")));
	TestTrue(TEXT("and marks the project as changed"), Controller->HasUnsavedChanges());
	Panel->RefreshIfChanged();
	TestEqual(TEXT("the drop-down keeps showing it"), Panel->GetTypeTextForTests(), FString(TEXT("Commercial")));
	TestTrue(TEXT("E2 can be selected"), Controller->SelectBuildingById(TEXT("E2")));
	Panel->RefreshIfChanged();
	TestEqual(TEXT("the drop-down follows the selection"), Panel->GetTypeTextForTests(), BuildingTypes::Default());

	// The Map panel.
	Map->Refresh();
	TestEqual(TEXT("one check box for every type"), Map->GetBuildingTypeCheckboxCountForTests(), BuildingTypes::All().Num());
	for (const FString& Type : BuildingTypes::All())
	{
		TestTrue(*FString::Printf(TEXT("%s starts checked"), *Type), Map->IsBuildingTypeCheckedForTests(Type));
	}
	const int32 Listed = Map->GetBuildingRowCountForTests();
	Map->SetBuildingTypeCheckedForTests(TEXT("Commercial"), false);
	TestFalse(TEXT("unchecking a type hides it"), Fixture.Generator->IsBuildingTypeVisible(TEXT("Commercial")));
	TestEqual(TEXT("and the list loses the one building of that type"), Map->GetBuildingRowCountForTests(), Listed - 1);
	Map->ClickResetBuildingFiltersForTests();
	TestTrue(TEXT("Reset checks it again"), Map->IsBuildingTypeCheckedForTests(TEXT("Commercial")));
	TestTrue(TEXT("and shows it"), Fixture.Generator->IsBuildingTypeVisible(TEXT("Commercial")));
	TestEqual(TEXT("the list is whole again"), Map->GetBuildingRowCountForTests(), Listed);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
