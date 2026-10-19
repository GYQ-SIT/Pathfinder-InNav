#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "PolyBuilding.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingBaselineDiscardTest, "InNavi.Building.DiscardBaseline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingBaselineDiscardTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ABuildingGenerator* Generator = World->SpawnActor<ABuildingGenerator>();
	if (!TestNotNull(TEXT("generator"), Generator))
	{
		return false;
	}

	APolyBuilding* Building = Generator->CreateRectangleBuilding(TEXT("B1"), TEXT("Library"), FVector2D(0.0, 0.0), 400.f, 300.f, 0.f, 3, 300.f);
	if (!TestNotNull(TEXT("building"), Building))
	{
		return false;
	}

	auto MakeInterior = [](int32 Marker)
	{
		TSharedPtr<FJsonObject> Interior = MakeShared<FJsonObject>();
		Interior->SetNumberField(TEXT("marker"), Marker);
		Interior->SetArrayField(TEXT("floors"), TArray<TSharedPtr<FJsonValue>>());
		return Interior;
	};
	auto InteriorMarker = [&]() -> int32
	{
		const TSharedPtr<FJsonObject> Interior = Generator->GetInteriorDataForBuilding(TEXT("B1"));
		int32 Marker = -1;
		if (Interior.IsValid())
		{
			Interior->TryGetNumberField(TEXT("marker"), Marker);
		}
		return Marker;
	};

	// --- the reported case: floors added in the interior, then "discard" ---
	TestFalse(TEXT("no baseline before the interior is opened"), Generator->HasBuildingBaseline(TEXT("B1")));
	Generator->CaptureBuildingBaseline(TEXT("B1"));
	TestTrue(TEXT("baseline kept"), Generator->HasBuildingBaseline(TEXT("B1")));

	Building->SetFloorCount(6); // what AddFloor does to the exterior shell
	Building->BuildingName = TEXT("Renamed");
	Generator->SetInteriorDataForBuilding(TEXT("B1"), MakeInterior(7));
	Generator->RestoreBuildingBaselines();
	TestEqual(TEXT("floor count is back"), Building->GetFloorCount(), 3);
	TestEqual(TEXT("name is back"), Building->BuildingName, FString(TEXT("Library")));
	TestNull(TEXT("interior data that did not exist before is gone again"), Generator->GetInteriorDataForBuilding(TEXT("B1")).Get());

	// --- entering the same building again must not overwrite what the last save/open left behind ---
	Building->SetFloorCount(5);
	Generator->CaptureBuildingBaseline(TEXT("B1"));
	Generator->RestoreBuildingBaselines();
	TestEqual(TEXT("a second capture keeps the first baseline"), Building->GetFloorCount(), 3);

	// --- a save makes the current state the new baseline ---
	Building->SetFloorCount(4);
	Generator->SetInteriorDataForBuilding(TEXT("B1"), MakeInterior(1));
	Generator->RefreshBuildingBaselines();
	Building->SetFloorCount(8);
	Generator->SetInteriorDataForBuilding(TEXT("B1"), MakeInterior(2));
	Generator->RestoreBuildingBaselines();
	TestEqual(TEXT("restores to the saved floor count"), Building->GetFloorCount(), 4);
	TestEqual(TEXT("restores the saved interior data"), InteriorMarker(), 1);

	// --- restored data is a copy: editing the live object afterwards cannot reach the baseline ---
	Generator->GetInteriorDataForBuilding(TEXT("B1"))->SetNumberField(TEXT("marker"), 99);
	Generator->RestoreBuildingBaselines();
	TestEqual(TEXT("baseline is not aliased to live data"), InteriorMarker(), 1);

	// --- a building that no longer exists is skipped, and clearing the project drops the baselines ---
	Generator->ClearAllBuildings();
	Generator->RestoreBuildingBaselines();
	TestFalse(TEXT("baselines are dropped with the project"), Generator->HasBuildingBaseline(TEXT("B1")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
