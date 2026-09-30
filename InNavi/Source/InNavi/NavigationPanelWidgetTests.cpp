#if WITH_DEV_AUTOMATION_TESTS

#include "NavigationPanelWidget.h"

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "PolyBuilding.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationPanelOutdoorPresentationTest,
	"InNavi.Navigation.PersonBOutdoorPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavigationPanelOutdoorPresentationTest::RunTest(const FString& Parameters)
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
		const FBox2D OutdoorBounds = UNavigationPanelWidget::GetOutdoorBounds(*Building);
		TestTrue(TEXT("Outdoor display bounds are valid"), OutdoorBounds.bIsValid);
		TestTrue(TEXT("Outer corner is selectable outside"),
			UNavigationPanelWidget::IsOutdoorPoint(*Building, OutdoorBounds.Min));
		TestFalse(TEXT("Indoor point is not classified outside"),
			UNavigationPanelWidget::IsOutdoorPoint(*Building, FVector2D(100.f, 300.f)));

		UNavigationPanelWidget* Panel = NewObject<UNavigationPanelWidget>();
		Panel->SetNavigationContext(Generator, Interior);
		Panel->ToggleSelectStart();
		Panel->SelectPoint(OutdoorBounds.Min, 1);
		TestTrue(TEXT("Floor 1 blue area sets an outdoor start"), Panel->IsStartOutside());
		TestEqual(TEXT("Outdoor point uses floor-zero contract"), Panel->GetStart().Floor,
			UNavigationPanelWidget::OutdoorFloor);

		Panel->ToggleSelectDestination();
		Panel->SelectPoint(FVector2D(100.f, 300.f), 1);
		TestTrue(TEXT("Indoor destination remains selectable"), Panel->HasDestination());
		TestFalse(TEXT("Indoor destination is labelled indoor"), Panel->IsDestinationOutside());

		FNavigationRouteResult CombinedRoute;
		CombinedRoute.Points.Add(Panel->GetStart());
		FNavigationRoutePoint Entrance;
		Entrance.Position = FVector2D(100.f, 300.f);
		Entrance.Floor = 1;
		CombinedRoute.Points.Add(Entrance);
		FNavigationRoutePoint UpperFloor = Entrance;
		UpperFloor.Floor = 2;
		UpperFloor.ConnectorType = EInteriorNavNodeType::Stairs;
		CombinedRoute.Points.Add(UpperFloor);
		Panel->ApplyRouteResult(CombinedRoute);
		const FString Directions = Panel->BuildRouteDetails();
		TestTrue(TEXT("Directions identify outside"), Directions.Contains(TEXT("Outside")));
		TestTrue(TEXT("Directions explain entering the building"), Directions.Contains(TEXT("Enter the building")));
		TestTrue(TEXT("Directions explain the floor transition"), Directions.Contains(TEXT("stairs")));

		Panel->ToggleSelectDestination();
		Panel->SelectPoint(FVector2D(110.f, 350.f), 1);
		TestTrue(TEXT("Changing an endpoint clears the old route"), Panel->GetRoutePoints().IsEmpty());
		TestTrue(TEXT("Changing an endpoint asks for a new route"),
			Panel->GetDisplayStatusMessage().Contains(TEXT("press Find Route again")));

		Panel->ToggleSelectStart();
		Panel->SelectPoint(OutdoorBounds.Min - FVector2D(1000.f, 1000.f), 1);
		TestEqual(TEXT("An invalid click keeps selection armed"), Panel->GetSelectionMode(),
			ENavigationSelectionMode::Start);
		TestTrue(TEXT("An invalid click reports an error"),
			Panel->GetDisplayStatusMessage().Contains(TEXT("Invalid area")));
	}

	World->DestroyWorld(false);
	return true;
}

#endif
