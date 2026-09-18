#if WITH_DEV_AUTOMATION_TESTS

#include "NavigationRoutePlanner.h"

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "PolyBuilding.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationRoutePlannerIntegrationTest,
	"InNavi.Navigation.RoutePlanner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavigationRoutePlannerIntegrationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ABuildingGenerator* Generator = World->SpawnActor<ABuildingGenerator>();
	AInteriorGenerator* Interior = World->SpawnActor<AInteriorGenerator>();
	Interior->DispatchBeginPlay();

	const FString DataPath = FPaths::ProjectDir() / TEXT("Data") / TEXT("buildings.json");
	TestTrue(TEXT("Example building data loads"), Generator->LoadBuildingsFromFile(DataPath) > 0);
	TestTrue(TEXT("E1 interior loads"), Interior->LoadInteriorForBuilding(TEXT("E1")) >= 0);
	APolyBuilding* Building = Generator->FindBuildingById(TEXT("E1"));
	TestNotNull(TEXT("E1 exists"), Building);
	if (Building)
	{
		FNavigationRouteResult Route;
		TestTrue(TEXT("Same-floor route"), FNavigationRoutePlanner::FindRoute(*Building, *Interior,
			Generator->GetScale(), 1, FVector2D(100, 300), 1, FVector2D(110, 350), Route));
		TestTrue(TEXT("Same-floor route has endpoints"), Route.Points.Num() >= 2);

		TestTrue(TEXT("Cross-floor route uses E1 elevator"), FNavigationRoutePlanner::FindRoute(*Building, *Interior,
			Generator->GetScale(), 1, FVector2D(100, 300), 2, FVector2D(100, 300), Route));
		bool bChangedFloor = false;
		for (int32 I = 1; I < Route.Points.Num(); ++I)
		{
			bChangedFloor |= Route.Points[I - 1].Floor != Route.Points[I].Floor;
		}
		TestTrue(TEXT("Cross-floor route contains a floor transition"), bChangedFloor);

		TestFalse(TEXT("No connection to E1 floor 3"), FNavigationRoutePlanner::FindRoute(*Building, *Interior,
			Generator->GetScale(), 1, FVector2D(100, 300), 3, FVector2D(100, 300), Route));
		TestTrue(TEXT("No-route reason is present"), !Route.Error.IsEmpty());

		TestTrue(TEXT("Room-to-corridor route"), FNavigationRoutePlanner::FindRoute(*Building, *Interior,
			Generator->GetScale(), 2, FVector2D(100, 300), 2, FVector2D(100, 230), Route));
		TestTrue(TEXT("Route bends around the room wall"), Route.Points.Num() > 2);
		bool bUsesDoor = false;
		const FVector2D DoorMidpoint(60.f + 34.5f / Generator->GetScale(), 240.f);
		for (const FNavigationRoutePoint& Point : Route.Points)
		{
			bUsesDoor |= Point.Floor == 2 && Point.Position.Equals(DoorMidpoint, 0.01f);
		}
		TestTrue(TEXT("Room exit passes through the authored door"), bUsesDoor);
	}
	World->DestroyWorld(false);
	return true;
}

#endif
