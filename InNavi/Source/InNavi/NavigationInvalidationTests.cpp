#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "InteriorRoom.h"
#include "PolyBuilding.h"
#include "NavigationInstructions.h"
#include "UObject/Script.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"

namespace NavigationInvalidationTests
{
	void Prepare(FNavigationSearchFixture& Fixture)
	{
		Fixture.Interior->ClearLoadedInterior(); Fixture.Generator->ClearAllBuildings();
		Fixture.Generator->CreateRectangleBuilding(TEXT("Layout"), TEXT("Layout"), { 2000, 1500 }, 4000, 3000, 0, 2, 300);
		Fixture.Interior->LoadInteriorForBuilding(TEXT("Layout"));
	}

	void Prime(FNavigationSearchFixture& Fixture)
	{
		Fixture.Nav->ClearAll();
		Fixture.Nav->ToggleSelectStart(); Fixture.Nav->SelectPoint({ 2500, 1500 }, 1);
		Fixture.Nav->ToggleSelectDestination(); Fixture.Nav->SelectPoint({ 3000, 1500 }, 1);
		FNavigationRouteResult Route; Route.Points = { Fixture.Nav->GetStart(), Fixture.Nav->GetDestination() };
		Fixture.Nav->ApplyRouteResult(Route);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationLayoutEditInvalidationTest, "InNavi.Navigation.Invalidation.LayoutEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationLayoutEditInvalidationTest::RunTest(const FString& Parameters)
{
	using namespace NavigationInvalidationTests;
	FNavigationSearchFixture Fixture; Prepare(Fixture);
	FString Error;
	AInteriorRoom* Room = Fixture.Interior->CreateRoomFromFootprint({ {500,500}, {1500,500}, {1500,1500}, {500,1500} }, TEXT("Room"), TEXT("Room"), true, false, false, Error);
	if (!TestNotNull(TEXT("test room"), Room)) return false;
	auto Check = [&](const TCHAR* Name, TFunctionRef<bool()> Edit)
	{
		Prime(Fixture);
		TestTrue(TEXT("a route is present before editing"), !Fixture.Nav->GetRoutePoints().IsEmpty());
		TestTrue(Name, Edit());
		TestTrue(*FString::Printf(TEXT("%s clears route, legs and guidance"), Name), Fixture.Nav->GetRoutePoints().IsEmpty()
			&& Fixture.Nav->GetRouteLegs().IsEmpty() && Fixture.Nav->GetGuidedPoint() == nullptr);
	};
	FString Door;
	Check(TEXT("add door"), [&] { Door = Fixture.Interior->AddDoorToRoom(TEXT("Room"), 0, 400); return !Door.IsEmpty(); });
	Prime(Fixture); FInteriorOpening ExistingDoor;
	if (Fixture.Interior->GetDoorOpening(TEXT("Room"), Door, ExistingDoor)) Fixture.Interior->MoveDoor(TEXT("Room"), Door, ExistingDoor.Offset);
	Fixture.Interior->RecordHistorySnapshot();
	TestFalse(TEXT("selecting a door or ending an unchanged gesture retains the route"), Fixture.Nav->GetRoutePoints().IsEmpty());
	Check(TEXT("move door preview"), [&] { return Fixture.Interior->MoveDoor(TEXT("Room"), Door, 500); });
	Check(TEXT("delete door"), [&] { return Fixture.Interior->DeleteDoor(TEXT("Room"), Door); });
	Check(TEXT("resize room"), [&] { return Fixture.Interior->ResizeRoom(TEXT("Room"), 900, 900); });
	Check(TEXT("move room preview"), [&] { auto Shape = Room->GetFootprint(); for (auto& P : Shape) P.X += 100; return Fixture.Interior->MoveRoom(TEXT("Room"), Shape); });
	Check(TEXT("rotate room preview"), [&] { return Fixture.Interior->RotateRoom(TEXT("Room"), 5); });
	Check(TEXT("create wall"), [&] { return Fixture.Interior->CreateWallFromPoints({ {2000,2000}, {2300,2200} }, TEXT("Wall"), Error) != nullptr; });
	FString Connector;
	Check(TEXT("place connector"), [&] { Connector = Fixture.Interior->PlaceConnectorNode(EInteriorNavNodeType::Elevator, {3000,2400}, {1,2}); return !Connector.IsEmpty(); });
	Check(TEXT("change connector floors"), [&] { return Fixture.Interior->SetConnectorFloors(Connector, {1}); });
	Check(TEXT("rotate connector preview"), [&] { return Fixture.Interior->RotateConnectorGroup(Connector, 5); });
	Check(TEXT("scale connector"), [&] { return Fixture.Interior->ScaleConnectorGroup(Connector, .1f); });
	Check(TEXT("delete connector"), [&] { return Fixture.Interior->DeleteConnectorGroup(Connector); });
	Check(TEXT("add floor"), [&] { return Fixture.Interior->AddFloor() == 3; });
	Check(TEXT("remove floor"), [&] { return Fixture.Interior->RemoveFloor(3); });
	Fixture.Interior->SetActiveFloor(1);
	FString Entrance;
	Check(TEXT("add entrance"), [&] { Entrance = Fixture.Interior->AddEntranceAtPoint({2000,0}, 20, Error); return !Entrance.IsEmpty(); });
	Check(TEXT("remove entrance"), [&] { return Fixture.Interior->RemoveEntrance(Entrance); });
	Check(TEXT("undo"), [&] { return Fixture.Interior->Undo(); });
	Check(TEXT("redo"), [&] { return Fixture.Interior->Redo(); });
	Check(TEXT("add building"), [&] { return Fixture.Generator->CreateRectangleBuilding(TEXT("Other"), TEXT("Other"), {6000,1500}, 1000, 1000, 0, 2, 300) != nullptr; });
	Check(TEXT("rename building"), [&] { return Fixture.Generator->SetBuildingName(TEXT("Other"), TEXT("Renamed")); });
	Check(TEXT("replace stored interior"), [&] { Fixture.Generator->SetInteriorDataForBuilding(TEXT("Other"), MakeShared<FJsonObject>()); return true; });
	Check(TEXT("change exterior floors"), [&] { Fixture.Generator->FindBuildingById(TEXT("Other"))->SetFloorCount(3); return true; });
	Check(TEXT("delete building"), [&] { FEditorScriptExecutionGuard ScriptGuard; return Fixture.Generator->FindBuildingById(TEXT("Other"))->Destroy(); });
	// Viewing and saving are not edits and must not discard an otherwise current route.
	Prime(Fixture); Fixture.Interior->SyncInteriorToBuildingGenerator(); Fixture.Interior->SetActiveFloor(2);
	Fixture.Interior->LoadInteriorForBuilding(TEXT("Layout"));
	TestFalse(TEXT("saving, viewing floors and reloading a view retain the route"), Fixture.Nav->GetRoutePoints().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationPendingEditInvalidationTest, "InNavi.Navigation.Invalidation.PendingAndEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationPendingEditInvalidationTest::RunTest(const FString& Parameters)
{
	using namespace NavigationInvalidationTests;
	FNavigationSearchFixture Fixture; Prepare(Fixture); Prime(Fixture);
	Fixture.Nav->StartFindRoute();
	FNavigationRouteJob Detached(*Fixture.Generator, *Fixture.Interior, Fixture.Nav->GetStart(), Fixture.Nav->GetDestination());
	Detached.Step(0);
	FString Error;
	AInteriorRoom* Room = Fixture.Interior->CreateRoomFromFootprint({ {500,500}, {1500,500}, {1500,1500}, {500,1500} }, TEXT("Room"), TEXT("Room"), true, false, false, Error);
	if (!TestNotNull(TEXT("test room"), Room)) return false;
	TestTrue(TEXT("an edit cancels the panel job and its previous route"), !Fixture.Nav->IsFindingRoute() && Fixture.Nav->GetRoutePoints().IsEmpty());
	TestTrue(TEXT("a detached job also rejects its old snapshot"), Detached.Step(0) && !Detached.Succeeded() && !Detached.GetResult().Error.IsEmpty());
	Fixture.Nav->SetStartToRoom(TEXT("Room (Floor 1)"));
	auto Shape = Room->GetFootprint(); for (auto& P : Shape) P.X += 100;
	Fixture.Interior->MoveRoom(TEXT("Room"), Shape);
	Fixture.Nav->StartFindRoute();
	TestTrue(TEXT("a moved room endpoint is resolved by ID before replanning"), Fixture.Nav->GetStart().Position.Equals({1100,1000}, .01));
	Fixture.Interior->DeleteRoom(TEXT("Room")); Fixture.Nav->StartFindRoute();
	TestTrue(TEXT("a deleted room endpoint stays visible for correction"), Fixture.Nav->HasStart());
	TestFalse(TEXT("planning cannot silently use another start"), Fixture.Nav->IsFindingRoute());
	TestTrue(TEXT("the unavailable start is explained"), Fixture.Nav->GetDisplayStatusMessage().Contains(TEXT("unavailable")));
	// Once detached from a context its model events must no longer affect the panel.
	Fixture.Nav->SetNavigationContext(nullptr, nullptr);
	FNavigationRouteResult Imported; Imported.Points = { {}, {} }; Fixture.Nav->ApplyRouteResult(Imported);
	Fixture.Interior->AddFloor();
	TestEqual(TEXT("old context delegates were removed"), Fixture.Nav->GetRoutePoints().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationDoorGuidanceIntegrationTest, "InNavi.Navigation.Instructions.DoorCrossingAndDesktop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationDoorGuidanceIntegrationTest::RunTest(const FString& Parameters)
{
	using namespace NavigationInvalidationTests;
	FNavigationSearchFixture Fixture; Prepare(Fixture); FString Error;
	if (!TestNotNull(TEXT("room"), Fixture.Interior->CreateRoomFromFootprint({ {500,500}, {1500,500}, {1500,1500}, {500,1500} }, TEXT("Room"), TEXT("Room"), true, false, false, Error))) return false;
	if (!TestFalse(TEXT("door"), Fixture.Interior->AddDoorToRoom(TEXT("Room"), 2, 500).IsEmpty())) return false;
	Fixture.Nav->SetStartToRoom(TEXT("Room (Floor 1)"));
	Fixture.Nav->ToggleSelectDestination(); Fixture.Nav->SelectPoint({1000,2500}, 1);
	Fixture.Nav->RequestFindRoute();
	const auto& Points = Fixture.Nav->GetRoutePoints();
	if (!TestFalse(TEXT("route found through the doorway"), Points.IsEmpty())) return false;
	TestTrue(TEXT("crossing a door is retained even on a direct visibility edge"), Points.ContainsByPredicate([](const FNavigationRoutePoint& P) { return P.bIsDoorway && FMath::IsNearlyEqual(P.Position.Y, 1500.0, .01); }));
	const auto Shared = NavigationInstructions::Build(Points, [](const FNavigationRoutePoint& P) { return P.WorldPosition; }, [](const FString& Id) { return Id; });
	const auto Desktop = Fixture.Nav->BuildRouteSteps();
	if (!TestEqual(TEXT("desktop combines the initial walk with Start and adds arrival"), Desktop.Num(), Shared.Num() + 1)) return false;
	for (int32 I = 0; I < Shared.Num(); ++I)
	{
		if (I == 0) TestTrue(TEXT("desktop start includes the shared first walking instruction"), Desktop[0].Text.EndsWith(Shared[0].Text));
		else TestEqual(TEXT("desktop uses shared text"), Desktop[I].Text, Shared[I].Text);
		TestEqual(TEXT("desktop retains maneuver indices for focus"), Desktop[I].PointIndex, Shared[I].PointIndex);
	}
	TestTrue(TEXT("guidance includes the door and walking distance"), Shared.ContainsByPredicate([](const FNavigationInstruction& Step) { return Step.Kind == ENavigationInstructionKind::PassDoor && Step.Text.Contains(TEXT(" m")); }));
	const auto* DoorPoint = Points.FindByPredicate([](const FNavigationRoutePoint& P) { return P.bIsDoorway; });
	if (TestNotNull(TEXT("door landmark"), DoorPoint))
	{
		FNavigationRouteResult FromDoor; FromDoor.Points = { *DoorPoint, Points.Last() };
		Fixture.Nav->ApplyRouteResult(FromDoor);
		const auto Focus = Fixture.Nav->GetGuidedFocus();
		TestTrue(TEXT("announcements at the same point still highlight the next walking stretch"), Focus.IsSet() && Focus.LastPointIndex > Focus.FirstPointIndex);
	}
	return true;
}

#endif
