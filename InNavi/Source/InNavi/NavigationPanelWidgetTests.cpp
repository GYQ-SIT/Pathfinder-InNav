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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationPanelStopsTest,
	"InNavi.Navigation.MultiStop.WaypointManagement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FNavigationPanelStopsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ABuildingGenerator* Generator = World->SpawnActor<ABuildingGenerator>();
	AInteriorGenerator* Interior = World->SpawnActor<AInteriorGenerator>();
	Interior->DispatchBeginPlay();

	const FString DataPath = FPaths::ProjectDir() / TEXT("Data") / TEXT("buildings.json");
	TestTrue(TEXT("Example building data loads"), Generator->LoadBuildingsFromFile(DataPath) > 0);
	TestTrue(TEXT("E2 interior loads"), Interior->LoadInteriorForBuilding(TEXT("E2")) >= 0);
	UNavigationPanelWidget* Panel = NewObject<UNavigationPanelWidget>();
	Panel->SetNavigationContext(Generator, Interior);
	const TArray<UNavigationPanelWidget::FNavigationRoomOption> Rooms = Panel->GetSelectableRooms();
	if (Rooms.Num() < 2)
	{
		AddError(TEXT("E2 needs at least two selectable rooms for stop management tests."));
		World->DestroyWorld(false);
		return false;
	}

	FNavigationRouteResult ExistingRoute;
	FNavigationRoutePoint RoutePoint;
	RoutePoint.Position = Rooms[0].Centroid;
	RoutePoint.Floor = Rooms[0].Floor;
	ExistingRoute.Points.Add(RoutePoint);
	RoutePoint.Position += FVector2D(20.f, 0.f);
	ExistingRoute.Points.Add(RoutePoint);
	Panel->ApplyRouteResult(ExistingRoute);
	TestTrue(TEXT("A room can be added as the first stop"), Panel->AddStopToRoom(Rooms[0].GetDisplayLabel()));
	TestTrue(TEXT("Adding a stop invalidates a previously planned route"), Panel->GetRoutePoints().IsEmpty());
	TestTrue(TEXT("Adding a stop clears old route leg ranges"), Panel->GetRouteLegs().IsEmpty());
	TestTrue(TEXT("A second room can be added as an ordered stop"), Panel->AddStopToRoom(Rooms[1].GetDisplayLabel()));
	TestEqual(TEXT("Both intermediate stops are retained"), Panel->GetStops().Num(), 2);
	if (Panel->GetStops().Num() == 2)
	{
		TestEqual(TEXT("Stops preserve their insertion order"), Panel->GetStops()[0].RoomOptionLabel,
			Rooms[0].GetDisplayLabel());
		TestEqual(TEXT("Second stop follows the first"), Panel->GetStops()[1].RoomOptionLabel,
			Rooms[1].GetDisplayLabel());
	}
	TestTrue(TEXT("Moving a stop changes its order"), Panel->MoveStop(0, 1));
	if (Panel->GetStops().Num() == 2)
	{
		TestEqual(TEXT("Moved stop is now first"), Panel->GetStops()[0].RoomOptionLabel,
			Rooms[1].GetDisplayLabel());
	}
	TestFalse(TEXT("Moving an invalid stop index is rejected"), Panel->MoveStop(3, 0));
	TestTrue(TEXT("A valid stop can be removed"), Panel->RemoveStop(0));
	TestEqual(TEXT("Removing a stop preserves the other stop"), Panel->GetStops().Num(), 1);
	Panel->ClearStops();
	TestTrue(TEXT("Clearing stops removes all waypoints"), Panel->GetStops().IsEmpty());

	for (int32 StopIndex = 0; StopIndex < UNavigationPanelWidget::MaxRouteStops; ++StopIndex)
	{
		TestTrue(FString::Printf(TEXT("Stop %d of the configured limit can be added"), StopIndex + 1),
			Panel->AddStopToRoom(Rooms[0].GetDisplayLabel()));
	}
	TestFalse(TEXT("The configured stop limit is enforced"), Panel->AddStopToRoom(Rooms[0].GetDisplayLabel()));
	TestEqual(TEXT("Rejected extra stop does not change the list"), Panel->GetStops().Num(),
		UNavigationPanelWidget::MaxRouteStops);

	World->DestroyWorld(false);
	return true;
}

#endif
