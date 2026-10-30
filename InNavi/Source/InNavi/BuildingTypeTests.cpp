#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingTypes.h"
#include "BuildingGenerator.h"
#include "ContextBuildingActor.h"
#include "PolyBuilding.h"
#include "EditScreenWidget.h"
#include "BuildingInfoPanelWidget.h"
#include "MapPanelWidget.h"
#include "UiTestFixture.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	UWorld* MakeBuildingTypeTestWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, false);
	}

	TArray<FVector2D> MakeBuildingTypeTestFootprint()
	{
		return { FVector2D(0.f, 0.f), FVector2D(1000.f, 0.f), FVector2D(1000.f, 800.f), FVector2D(0.f, 800.f) };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingTypeTableTest, "InNavi.Building.Types.Table", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingTypeTableTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Other is last"), BuildingTypes::All().Last(), BuildingTypes::Default());
	TSet<FString> TypeNames;
	TArray<FColor> TypeColors;
	for (const FString& Type : BuildingTypes::All())
	{
		TypeNames.Add(Type);
		const FColor Color = BuildingTypes::Color(Type).ToFColor(true);
		TestFalse(TEXT("colour is distinct from preceding types"), TypeColors.Contains(Color));
		TypeColors.Add(Color);
	}
	TestEqual(TEXT("types are unique"), TypeNames.Num(), BuildingTypes::All().Num());
	TestEqual(TEXT("normalize ignores case and spaces"), BuildingTypes::Normalize(TEXT(" commercial ")), FString(TEXT("Commercial")));
	TestEqual(TEXT("unknown type normalizes to Other"), BuildingTypes::Normalize(TEXT("unknown")), BuildingTypes::Default());
	struct FBuildingTypeMappingTest { const TCHAR* MapType; const TCHAR* Type; };
	const FBuildingTypeMappingTest Mappings[] = {
		{ TEXT("university"), TEXT("University") }, { TEXT("school"), TEXT("School") }, { TEXT("kindergarten"), TEXT("School") }, { TEXT("education"), TEXT("School") },
		{ TEXT("residential"), TEXT("Residential") }, { TEXT("house"), TEXT("Residential") }, { TEXT("detached"), TEXT("Residential") },
		{ TEXT("commercial"), TEXT("Commercial") }, { TEXT("retail"), TEXT("Commercial") }, { TEXT("public"), TEXT("Public") }, { TEXT("church"), TEXT("Public") },
		{ TEXT("train_station"), TEXT("Transport") }, { TEXT("parking"), TEXT("Transport") }, { TEXT("bridge"), TEXT("Transport") },
		{ TEXT("transportation"), TEXT("Transport") }, { TEXT("utility"), TEXT("Utility") } };
	for (const FBuildingTypeMappingTest& Mapping : Mappings)
	{
		TestEqual(FString::Printf(TEXT("%s maps correctly"), Mapping.MapType), BuildingTypes::FromMapDatabase(Mapping.MapType), FString(Mapping.Type));
	}
	TestEqual(TEXT("yes maps to Other"), BuildingTypes::FromMapDatabase(TEXT("yes")), BuildingTypes::Default());
	TestEqual(TEXT("roof maps to Other"), BuildingTypes::FromMapDatabase(TEXT("roof")), BuildingTypes::Default());
	TestEqual(TEXT("unknown map value maps to Other"), BuildingTypes::FromMapDatabase(TEXT("custom")), BuildingTypes::Default());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingTypeGeneratorTest, "InNavi.Building.Types.Generator", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingTypeGeneratorTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeBuildingTypeTestWorld();
	ABuildingGenerator* Generator = World ? World->SpawnActor<ABuildingGenerator>() : nullptr;
	if (!TestNotNull(TEXT("generator exists"), Generator))
	{
		return false;
	}
	APolyBuilding* Building = Generator->SpawnBuildingFromData(TEXT("TYPE-A"), TEXT("A"), MakeBuildingTypeTestFootprint(), 2, 300.f, FVector2D::ZeroVector);
	Generator->SpawnBuildingFromData(TEXT("TYPE-OTHER"), TEXT("Other"), MakeBuildingTypeTestFootprint(), 2, 300.f, FVector2D(1200.f, 0.f));
	if (!TestNotNull(TEXT("building exists"), Building))
	{
		return false;
	}
	TestTrue(TEXT("type assignment succeeds"), Generator->SetBuildingType(TEXT("TYPE-A"), TEXT(" commercial ")));
	TestEqual(TEXT("type is canonical"), Generator->GetBuildingType(TEXT("TYPE-A")), FString(TEXT("Commercial")));
	TestTrue(TEXT("body uses type colour"), Building->GetBodyColorForTests().Equals(BuildingTypes::Color(TEXT("Commercial")), 0.01f));
	Building->SetHighlighted(true);
	Building->SetHighlighted(false);
	TestTrue(TEXT("unhighlight restores type colour"), Building->GetBodyColorForTests().Equals(BuildingTypes::Color(TEXT("Commercial")), 0.01f));
	Generator->SetBuildingTypeVisible(TEXT("Commercial"), false);
	TestEqual(TEXT("hidden type is removed from results"), Generator->GetFilteredBuildings().Num(), 1);
	Generator->ResetBuildingTypeFilter();
	TestEqual(TEXT("reset restores results"), Generator->GetFilteredBuildings().Num(), 2);
	TestFalse(TEXT("missing id cannot be assigned"), Generator->SetBuildingType(TEXT("missing"), TEXT("School")));

	const FString FilePath = FPaths::ProjectSavedDir() / TEXT("BuildingTypeTest-") + FGuid::NewGuid().ToString() + TEXT(".json");
	TestTrue(TEXT("project saves"), Generator->SaveBuildingsToFile(FilePath));
	FString Json;
	FFileHelper::LoadFileToString(Json, *FilePath);
	TestTrue(TEXT("non-default type is saved"), Json.Contains(TEXT("\"buildingType\":\"Commercial\"")));
	TSharedPtr<FJsonObject> SavedRoot;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (FJsonSerializer::Deserialize(Reader, SavedRoot) && SavedRoot.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* SavedBuildings = nullptr;
		if (SavedRoot->TryGetArrayField(TEXT("buildings"), SavedBuildings))
		{
			for (const TSharedPtr<FJsonValue>& Value : *SavedBuildings)
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				if (Object.IsValid() && Object->GetStringField(TEXT("id")) == TEXT("TYPE-OTHER"))
				{
					FString SavedType;
					TestFalse(TEXT("Other building omits buildingType"), Object->TryGetStringField(TEXT("buildingType"), SavedType));
				}
			}
		}
	}
	ABuildingGenerator* Reloaded = World->SpawnActor<ABuildingGenerator>();
	if (Reloaded)
	{
		TestEqual(TEXT("project reloads"), Reloaded->LoadBuildingsFromFile(FilePath), 2);
		TestEqual(TEXT("type survives reload"), Reloaded->GetBuildingType(TEXT("TYPE-A")), FString(TEXT("Commercial")));
	}
	IFileManager::Get().Delete(*FilePath);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingTypeProjectLoadTest, "InNavi.Building.Types.ProjectLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingTypeProjectLoadTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeBuildingTypeTestWorld();
	ABuildingGenerator* Generator = World ? World->SpawnActor<ABuildingGenerator>() : nullptr;
	if (!TestNotNull(TEXT("generator exists"), Generator))
	{
		return false;
	}
	APolyBuilding* Existing = Generator->SpawnBuildingFromData(TEXT("EXISTING"), TEXT("Existing"), MakeBuildingTypeTestFootprint(), 2, 300.f, FVector2D::ZeroVector);
	Existing->SetBuildingType(TEXT("University"));
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("scale"), 1.f);
	TArray<TSharedPtr<FJsonValue>> Buildings;
	TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("id"), TEXT("EXISTING"));
	Entry->SetStringField(TEXT("name"), TEXT("Existing"));
	TArray<TSharedPtr<FJsonValue>> Points;
	for (const FVector2D& Point : MakeBuildingTypeTestFootprint())
	{
		TArray<TSharedPtr<FJsonValue>> Pair;
		Pair.Add(MakeShared<FJsonValueNumber>(Point.X));
		Pair.Add(MakeShared<FJsonValueNumber>(Point.Y));
		Points.Add(MakeShared<FJsonValueArray>(Pair));
	}
	Entry->SetArrayField(TEXT("footprint"), Points);
	Buildings.Add(MakeShared<FJsonValueObject>(Entry));
	Root->SetArrayField(TEXT("buildings"), Buildings);
	TestEqual(TEXT("existing building loads"), Generator->LoadBuildingsFromJson(Root), 1);
	TestEqual(TEXT("missing field preserves database type"), Generator->GetBuildingType(TEXT("EXISTING")), FString(TEXT("University")));

	const FString FilePath = FPaths::ProjectSavedDir() / TEXT("BuildingTypeDefaultTest-") + FGuid::NewGuid().ToString() + TEXT(".json");
	TestTrue(TEXT("default project saves"), Generator->SaveBuildingsToFile(FilePath));
	FString Json;
	FFileHelper::LoadFileToString(Json, *FilePath);
	TestFalse(TEXT("Other is omitted"), Json.Contains(TEXT("\"buildingType\":\"Other\"")));
	IFileManager::Get().Delete(*FilePath);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingTypeControllerUiTest, "InNavi.Building.Types.ControllerAndPanels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingTypeControllerUiTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	TestFalse(TEXT("interior editor blocks exterior type assignment"), Controller->SetSelectedBuildingType(TEXT("Commercial")));
	Fixture.Interior->ClearLoadedInterior();
	TestFalse(TEXT("no selection cannot be assigned"), Controller->SetSelectedBuildingType(TEXT("Commercial")));
	TestFalse(TEXT("no selection leaves project clean"), Controller->HasUnsavedChanges());
	TestTrue(TEXT("building selection works"), Controller->SelectBuildingById(TEXT("E2")));
	TestTrue(TEXT("same type is accepted"), Controller->SetSelectedBuildingType(BuildingTypes::Default()));
	TestFalse(TEXT("same type does not dirty project"), Controller->HasUnsavedChanges());
	TestTrue(TEXT("changed type is accepted"), Controller->SetSelectedBuildingType(TEXT("Commercial")));
	TestTrue(TEXT("changed type dirties project"), Controller->HasUnsavedChanges());
	UEditScreenWidget* Exterior = NewObject<UEditScreenWidget>();
	Exterior->Initialize();
	Exterior->TakeWidget();
	Exterior->SetOwningPlayer(Controller);
	UBuildingInfoPanelWidget* BuildingPanel = Exterior->GetBuildingInfoPanelForTests();
	UMapPanelWidget* MapPanel = Exterior->GetMapPanelForTests();
	if (!TestTrue(TEXT("panels exist"), BuildingPanel && MapPanel))
	{
		return false;
	}
	BuildingPanel->Refresh();
	UComboBoxString* Combo = BuildingPanel->GetBuildingTypeComboForTests();
	if (Combo)
	{
		TestEqual(TEXT("combo lists all types"), Combo->GetOptionCount(), BuildingTypes::All().Num());
		TestEqual(TEXT("combo shows selected type"), Combo->GetSelectedOption(), FString(TEXT("Commercial")));
	}
	MapPanel->Refresh();
	TestEqual(TEXT("map has a checkbox per type"), MapPanel->GetBuildingTypeFilterCountForTests(), BuildingTypes::All().Num());
	TestTrue(TEXT("all filters start checked"), MapPanel->AreBuildingTypeFiltersCheckedForTests());
	UCheckBox* FirstType = Cast<UCheckBox>(MapPanel->FindContentWidget(TEXT("Chk_BuildingType_0")));
	UButton* ResetButton = Cast<UButton>(MapPanel->FindContentWidget(TEXT("Btn_ResetBuildingFilters")));
	if (FirstType && ResetButton)
	{
		FirstType->SetIsChecked(false);
		FirstType->OnCheckStateChanged.Broadcast(false);
		ResetButton->OnClicked.Broadcast();
		TestTrue(TEXT("reset checks all types"), MapPanel->AreBuildingTypeFiltersCheckedForTests());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingTypeDatabaseFilterTest, "InNavi.Building.Types.DatabaseFilter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingTypeDatabaseFilterTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeBuildingTypeTestWorld();
	ABuildingGenerator* Generator = World ? World->SpawnActor<ABuildingGenerator>() : nullptr;
	if (!TestNotNull(TEXT("generator exists"), Generator))
	{
		return false;
	}
	const FString DatabasePath = FPaths::ProjectDir() / TEXT("Data/map_extended.db");
	TestTrue(TEXT("campus data loads"), Generator->LoadBuildingsFromDatabase(DatabasePath) > 0);
	TestTrue(TEXT("context data loads"), Generator->LoadContextBuildingsFromDatabase(DatabasePath) > 0);
	AContextBuildingActor* Context = nullptr;
	for (AActor* Actor : Generator->GetContextBuildingActorsForTests())
	{
		Context = Cast<AContextBuildingActor>(Actor);
		if (Context)
		{
			break;
		}
	}
	if (!TestNotNull(TEXT("context actor exists"), Context))
	{
		return false;
	}
	const FString Type = Context->GetBuildingType();
	Generator->SetNonCampusBuildingsVisible(true);
	Generator->SetBuildingTypeVisible(Type, false);
	TestFalse(TEXT("type filter hides context building"), Context->IsContextVisibleForTests());
	Generator->SetNonCampusBuildingsVisible(false);
	Generator->SetBuildingTypeVisible(Type, true);
	TestFalse(TEXT("Non-Campus filter continues to hide it"), Context->IsContextVisibleForTests());
	Generator->SetNonCampusBuildingsVisible(true);
	Generator->SetBuildingTypeVisible(Type, false);
	Generator->ResetBuildingTypeFilter();
	TestTrue(TEXT("reset restores context building"), Context->IsContextVisibleForTests());
	World->DestroyWorld(false);
	return true;
}

#endif
