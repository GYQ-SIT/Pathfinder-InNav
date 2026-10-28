#if WITH_DEV_AUTOMATION_TESTS

#include "NavigationPanelWidget.h"
#include "NavigationRouteVisualization.h"

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

	// Two places in turn: a stop where the route already is would be refused.
	for (int32 StopIndex = 0; StopIndex < UNavigationPanelWidget::MaxRouteStops; ++StopIndex)
	{
		TestTrue(FString::Printf(TEXT("Stop %d of the configured limit can be added"), StopIndex + 1),
			Panel->AddStopToRoom(Rooms[StopIndex % 2].GetDisplayLabel()));
	}
	TestFalse(TEXT("The configured stop limit is enforced"), Panel->AddStopToRoom(Rooms[UNavigationPanelWidget::MaxRouteStops % 2].GetDisplayLabel()));
	TestEqual(TEXT("Rejected extra stop does not change the list"), Panel->GetStops().Num(),
		UNavigationPanelWidget::MaxRouteStops);

	World->DestroyWorld(false);
	return true;
}

// A journey through stops: planned leg by leg (all at once, or a little each frame) into one route whose legs meet, with the
// directions, markers and failures to match.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationJourneyTest,
	"InNavi.Navigation.MultiStop.Journey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationJourneyTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ABuildingGenerator* Generator = World->SpawnActor<ABuildingGenerator>();
	AInteriorGenerator* Interior = World->SpawnActor<AInteriorGenerator>();
	Interior->DispatchBeginPlay();
	TestTrue(TEXT("Example building data loads"), Generator->LoadBuildingsFromFile(FPaths::ProjectDir() / TEXT("Data") / TEXT("buildings.json")) > 0);
	TestTrue(TEXT("E2 interior loads"), Interior->LoadInteriorForBuilding(TEXT("E2")) >= 0);
	UNavigationPanelWidget* Panel = NewObject<UNavigationPanelWidget>();
	Panel->SetNavigationContext(Generator, Interior);

	// E2: a room (Lab) and the vestibule on Floor 1, a room on Floor 2 and one that no door joins to the others.
	auto Label = [Panel](const TCHAR* RoomId)
	{
		const auto* Option = Panel->GetSelectableRooms().FindByPredicate([RoomId](const UNavigationPanelWidget::FNavigationRoomOption& Candidate) { return Candidate.RoomId == RoomId; });
		return Option ? Option->GetDisplayLabel() : FString();
	};
	const FString Lab = Label(TEXT("E2-01-01")), Vestibule = Label(TEXT("E2-01-OUTSIDE-ENTRY")), Upstairs = Label(TEXT("E2-02-01")), Cut = Label(TEXT("E2-01-02"));
	if (!TestTrue(TEXT("the rooms are there"), !Lab.IsEmpty() && !Vestibule.IsEmpty() && !Upstairs.IsEmpty() && !Cut.IsEmpty()))
	{
		World->DestroyWorld(false);
		return false;
	}
	auto Journey = [&](const FString& From, const TArray<FString>& Via, const FString& To)
	{
		Panel->ClearAll();
		Panel->SetStartToRoom(From);
		Panel->SetDestinationToRoom(To);
		for (const FString& Stop : Via) Panel->AddStopToRoom(Stop);
	};

	// Without stops it is one leg: the route the planner finds.
	Journey(Lab, {}, Upstairs);
	Panel->RequestFindRoute();
	TestEqual(TEXT("one leg without stops"), Panel->GetRouteLegs().Num(), 1);
	const int32 DirectPoints = Panel->GetRoutePoints().Num();

	// Through the vestibule: two legs that meet at the stop and cover the route.
	Journey(Lab, { Vestibule }, Upstairs);
	Panel->RequestFindRoute();
	const TArray<FNavigationRouteLeg>& Legs = Panel->GetRouteLegs();
	if (!TestEqual(TEXT("two legs through one stop"), Legs.Num(), 2)) { World->DestroyWorld(false); return false; }
	TestFalse(TEXT("found"), Panel->DidLastFindRouteFail());
	TestEqual(TEXT("the first leg begins the route"), Legs[0].FirstPointIndex, 0);
	TestEqual(TEXT("the second leg begins where the first ends"), Legs[1].FirstPointIndex, Legs[0].LastPointIndex);
	TestEqual(TEXT("the second leg ends the route"), Legs[1].LastPointIndex, Panel->GetRoutePoints().Num() - 1);
	TestTrue(TEXT("the stop is where the legs meet"), Panel->GetRoutePoints()[Legs[0].LastPointIndex].Position.Equals(Panel->GetStops()[0].Point.Position, 0.5));
	TestTrue(TEXT("a detour is longer than the direct route"), Panel->GetRoutePoints().Num() > DirectPoints);
	TestTrue(TEXT("the status says it went through a stop"), Panel->GetDisplayStatusMessage().Contains(TEXT("1 stop")));

	// The directions mention the stop, and end where the route does.
	const TArray<UNavigationPanelWidget::FNavigationRouteStep> Steps = Panel->BuildRouteSteps();
	TestTrue(TEXT("a step reaches the stop"), Steps.ContainsByPredicate([](const UNavigationPanelWidget::FNavigationRouteStep& Step) { return Step.Text.StartsWith(TEXT("Stop 1:")); }));
	TestTrue(TEXT("the last step is the arrival"), !Steps.IsEmpty() && Steps.Last().bFinal && Steps.Last().Text.StartsWith(TEXT("Arrive")));

	// Found a little each frame, the same journey comes out.
	const TArray<FNavigationRoutePoint> AllAtOnce = Panel->GetRoutePoints();
	Panel->StartFindRoute();
	TestTrue(TEXT("searching"), Panel->IsFindingRoute());
	TestTrue(TEXT("the progress names the leg"), Panel->GetFindRouteProgressText().Contains(TEXT("Leg 1 of 2")));
	float Last = 0.f; int32 Steps2 = 0;
	while (!Panel->StepFindRoute(0.0) && Steps2 < 100000)
	{
		++Steps2;
		TestTrue(TEXT("progress only goes forward"), Panel->GetFindRouteProgress() >= Last); Last = Panel->GetFindRouteProgress();
	}
	TestFalse(TEXT("finished"), Panel->IsFindingRoute());
	bool bSame = Panel->GetRoutePoints().Num() == AllAtOnce.Num() && Panel->GetRouteLegs().Num() == 2;
	for (int32 I = 0; bSame && I < AllAtOnce.Num(); ++I)
		bSame = Panel->GetRoutePoints()[I].Floor == AllAtOnce[I].Floor && Panel->GetRoutePoints()[I].Position.Equals(AllAtOnce[I].Position);
	TestTrue(TEXT("the same route as all at once"), bSame);

	// 3D: the stop is marked, and each stretch belongs to its leg.
	const FNavigationRoutePoint Start = Panel->GetStart();
	const FNavigationRoutePoint Destination = Panel->GetDestination();
	const FNavigation3DView View = UNavigationRouteVisualization::BuildCampusView(Panel->GetRoutePoints(), &Start, &Destination, true, &Panel->GetStops(), &Panel->GetRouteLegs());
	TestEqual(TEXT("the stop is marked in the campus view"), View.Stops.Num(), 1);
	TestTrue(TEXT("and the legs have their colours"), UNavigationRouteVisualization::LegColor(0) != UNavigationRouteVisualization::LegColor(1)
		&& UNavigationRouteVisualization::LegColor(0) == UNavigationRouteVisualization::LegColor(5));

	// A leg that cannot be planned is named.
	Journey(Lab, { Cut }, Upstairs);
	Panel->RequestFindRoute();
	TestTrue(TEXT("no route"), Panel->DidLastFindRouteFail() && Panel->GetRoutePoints().IsEmpty() && Panel->GetRouteLegs().IsEmpty());
	TestTrue(TEXT("the failing leg is named"), Panel->GetDisplayStatusMessage().Contains(TEXT("leg 1 of 2")));

	// A stop where the route already is would be a leg of no length.
	Journey(Lab, {}, Upstairs);
	TestFalse(TEXT("not the Start"), Panel->AddStopToRoom(Lab));
	TestFalse(TEXT("not the Destination"), Panel->AddStopToRoom(Upstairs));
	TestTrue(TEXT("another place"), Panel->AddStopToRoom(Vestibule));
	TestFalse(TEXT("not the stop before it"), Panel->AddStopToRoom(Vestibule));
	TestEqual(TEXT("one stop"), Panel->GetStops().Num(), 1);
	TestTrue(TEXT("the list names its building"), Panel->GetStopLabel(0).EndsWith(Vestibule) && Panel->GetStopLabel(0).Contains(TEXT(" · ")));

	// Whatever changes the stops ends a search and clears the route.
	Panel->StartFindRoute(); Panel->StepFindRoute(0.0);
	Panel->AddStopToRoom(Lab);
	TestFalse(TEXT("a new stop ends the search"), Panel->IsFindingRoute());
	TestTrue(TEXT("and there is no route"), Panel->GetRoutePoints().IsEmpty());
	Panel->StartFindRoute(); Panel->StepFindRoute(0.0);
	TestTrue(TEXT("removing a stop"), Panel->RemoveStop(0));
	TestFalse(TEXT("ends the search"), Panel->IsFindingRoute());
	Panel->StartFindRoute(); Panel->StepFindRoute(0.0);
	Panel->ClearAll();
	TestTrue(TEXT("clearing everything ends the search and removes the stops"), !Panel->IsFindingRoute() && Panel->GetStops().IsEmpty());

	// A stop in a room that is deleted goes with it.
	Journey(Lab, { Vestibule }, Upstairs);
	Panel->NotifyRoomDeleted(TEXT("E2-01-OUTSIDE-ENTRY"));
	TestTrue(TEXT("the stop is gone with its room"), Panel->GetStops().IsEmpty() && Panel->HasStart() && Panel->HasDestination());

	World->DestroyWorld(false);
	return true;
}

#endif
