#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "InteriorEntrance.h"
#include "InteriorRoom.h"
#include "PolyBuilding.h"
#include "NavigationRouteVisualization.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

namespace NavigationEntranceRoutingTests
{
	TSharedPtr<FJsonObject> InteriorData(const TArray<FInteriorEntrance>& Doors, bool bLift = false)
	{
		auto Data = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Floors;
		for (int32 Level = 1; Level <= 2; ++Level)
		{
			auto Floor = MakeShared<FJsonObject>();
			Floor->SetNumberField(TEXT("level"), Level);
			Floor->SetNumberField(TEXT("wallThickness"), 20);
			Floor->SetArrayField(TEXT("rooms"), {});
			TArray<TSharedPtr<FJsonValue>> Nodes;
			if (bLift)
			{
				auto Node = MakeShared<FJsonObject>();
				Node->SetStringField(TEXT("id"), FString::Printf(TEXT("Lift-%d"), Level));
				Node->SetStringField(TEXT("type"), TEXT("elevator"));
				Node->SetStringField(TEXT("connectorGroupId"), TEXT("Lift"));
				Node->SetArrayField(TEXT("position"), { MakeShared<FJsonValueNumber>(50), MakeShared<FJsonValueNumber>(50) });
				Node->SetArrayField(TEXT("connectsFloors"), { MakeShared<FJsonValueNumber>(1), MakeShared<FJsonValueNumber>(2) });
				Nodes.Add(MakeShared<FJsonValueObject>(Node));
			}
			Floor->SetArrayField(TEXT("navNodes"), Nodes);
			Floors.Add(MakeShared<FJsonValueObject>(Floor));
		}
		Data->SetArrayField(TEXT("floors"), Floors);
		TArray<TSharedPtr<FJsonValue>> Entrances;
		for (const auto& Door : Doors) Entrances.Add(MakeShared<FJsonValueObject>(InteriorEntrances::ToJson(Door)));
		Data->SetArrayField(TEXT("entrances"), Entrances);
		return Data;
	}

	FInteriorEntrance Door(const FString& Id, EInteriorEntranceKind Kind, int32 Edge, int32 Floor = 1, const FString& Target = FString())
	{
		FInteriorEntrance Result;
		Result.Id = Id; Result.Kind = Kind; Result.Level = Floor; Result.EdgeIndex = Edge;
		Result.Offset = 380; Result.Width = 240; Result.ConnectsTo = Target;
		return Result;
	}

	void Prepare(FNavigationSearchFixture& Fixture, const TArray<FInteriorEntrance>& A, const TArray<FInteriorEntrance>& B, bool bLift = false)
	{
		Fixture.Interior->ClearLoadedInterior();
		Fixture.Generator->ClearAllBuildings();
		// New project clears scale to 1. Establish the centimetre conversion used by these
		// footprints and doorway widths through the real project loader.
		const FString SettingsPath = FPaths::ProjectSavedDir() / (TEXT("RouteScale-") + FGuid::NewGuid().ToString() + TEXT(".json"));
		FFileHelper::SaveStringToFile(TEXT("{\"scale\":10,\"buildings\":[]}"), *SettingsPath);
		Fixture.Generator->LoadBuildingsFromFile(SettingsPath);
		IFileManager::Get().Delete(*SettingsPath);
		const TArray<FVector2D> Shape = { {0,0}, {100,0}, {100,100}, {0,100} };
		Fixture.Generator->SpawnBuildingFromData(TEXT("A"), TEXT("Alpha"), Shape, 2, 300, FVector2D::ZeroVector);
		Fixture.Generator->SpawnBuildingFromData(TEXT("B"), TEXT("Beta"), Shape, 2, 300, FVector2D(110,0));
		Fixture.Generator->SetInteriorDataForBuilding(TEXT("A"), InteriorData(A, bLift));
		Fixture.Generator->SetInteriorDataForBuilding(TEXT("B"), InteriorData(B, bLift));
		Fixture.Interior->LoadInteriorForBuilding(TEXT("A"));
	}

	FNavigationRoutePoint Point(const FString& Building, int32 Floor = 1, FVector2D Position = FVector2D(50,50))
	{
		FNavigationRoutePoint Result;
		Result.BuildingId = Building; Result.Floor = Floor; Result.Position = Position;
		return Result;
	}

	bool HasBuildingTransition(const FNavigationRouteResult& Route)
	{
		for (int32 I = 1; I < Route.Points.Num(); ++I)
			if (Route.Points[I-1].BuildingId != Route.Points[I].BuildingId && Route.Points[I].Floor > 0 && Route.Points[I-1].Floor > 0) return true;
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationEntranceMainTest, "InNavi.Navigation.Entrances.MainAndOutdoor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationEntranceMainTest::RunTest(const FString& Parameters)
{
	using namespace NavigationEntranceRoutingTests;
	FNavigationSearchFixture Fixture;
	Prepare(Fixture, { Door(TEXT("A-Main"), EInteriorEntranceKind::Main, 3) }, { Door(TEXT("B-Main"), EInteriorEntranceKind::Main, 1) });
	FNavigationRouteResult Route;
	const uint64 Revision = Fixture.Interior->GetRoomSearchRevision();
	TestTrue(TEXT("New main entrance connects corridor to outside"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior,
		Point(TEXT("A")), Point(TEXT("A"), 0, FVector2D(-20,50)), Route));
	TestTrue(TEXT("Route uses authored main entrance"), Route.Points.ContainsByPredicate([](const auto& P) { return P.ConnectorId.Contains(TEXT("Main:A-Main")); }));
	TestTrue(TEXT("Outdoor route reverses through the main entrance"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior,
		Point(TEXT("A"), 0, FVector2D(-20,50)), Point(TEXT("A")), Route));
	TestTrue(TEXT("Main entrances allow travel between separated buildings via outside"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior,
		Point(TEXT("A")), Point(TEXT("B")), Route));
	TestTrue(TEXT("Campus route contains an outdoor segment"), Route.Points.ContainsByPredicate([](const auto& P) { return P.Floor == 0; }));
	TestFalse(TEXT("Main doors do not act as building link doors"), HasBuildingTransition(Route));
	for (const auto& P : Route.Points) TestTrue(TEXT("Route points have building identity and exact world coordinates"), !P.BuildingId.IsEmpty() && P.bHasWorldPosition);
	TestEqual(TEXT("Routing does not switch the loaded building"), Fixture.Interior->GetLoadedBuildingId(), FString(TEXT("A")));
	TestEqual(TEXT("Routing does not mutate the live interior"), Fixture.Interior->GetRoomSearchRevision(), Revision);
	// Only A has a door after this replacement, so B must remain unreachable.
	Fixture.Generator->SetInteriorDataForBuilding(TEXT("B"), InteriorData({}));
	TestFalse(TEXT("A missing entrance cannot be bypassed through a building outline"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior,
		Point(TEXT("A")), Point(TEXT("B")), Route));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationEntranceLinkTest, "InNavi.Navigation.Entrances.LinksAndFloors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationEntranceLinkTest::RunTest(const FString& Parameters)
{
	using namespace NavigationEntranceRoutingTests;
	FNavigationSearchFixture Fixture;
	Prepare(Fixture, { Door(TEXT("A-Link"), EInteriorEntranceKind::Link, 1, 2, TEXT("B")) }, {}, true);
	FNavigationRouteResult Route;
	TestTrue(TEXT("A saved link connects its mirror upstairs"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior,
		Point(TEXT("A"), 2), Point(TEXT("B"), 2), Route));
	TestTrue(TEXT("Route crosses a building link"), HasBuildingTransition(Route));
	TestFalse(TEXT("Upstairs link does not force an outdoor detour"), Route.Points.ContainsByPredicate([](const auto& P) { return P.Floor == 0; }));
	TestTrue(TEXT("Mirrored link works in reverse"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior,
		Point(TEXT("B"), 2), Point(TEXT("A"), 2), Route));
	TestTrue(TEXT("Route combines lifts in unopened buildings and an upstairs link"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior,
		Point(TEXT("A"), 1, FVector2D(40,50)), Point(TEXT("B"), 1, FVector2D(60,50)), Route));
	TestTrue(TEXT("Lift metadata survives"), Route.Points.ContainsByPredicate([](const auto& P) { return P.ConnectorType == EInteriorNavNodeType::Elevator; }));
	// A link is not permission to cross an unrelated solid room wall in an unopened interior.
	auto Blocked = InteriorData({}, true);
	auto Sealed = MakeShared<FJsonObject>();
	Sealed->SetStringField(TEXT("id"), TEXT("Sealed")); Sealed->SetStringField(TEXT("name"), TEXT("Sealed"));
	TArray<TSharedPtr<FJsonValue>> Outline;
	for (const FVector2D& P : TArray<FVector2D>{ {0,0}, {100,0}, {100,100}, {0,100} })
		Outline.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y) }));
	Sealed->SetArrayField(TEXT("footprint"), Outline);
	Blocked->GetArrayField(TEXT("floors"))[1]->AsObject()->SetArrayField(TEXT("rooms"), { MakeShared<FJsonValueObject>(Sealed) });
	Fixture.Generator->SetInteriorDataForBuilding(TEXT("B"), Blocked);
	TestFalse(TEXT("A sealed room blocks the link's indoor approach"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior,
		Point(TEXT("A"), 2), Point(TEXT("B"), 2), Route));
	Fixture.Generator->SetInteriorDataForBuilding(TEXT("B"), InteriorData({}, true));
	Fixture.Generator->FindBuildingById(TEXT("B"))->BuildFromFootprint({ {0,0}, {100,0}, {100,100}, {0,100} }, 10, FVector2D(110,0), 2, 500);
	TestFalse(TEXT("Same floor numbers at different world heights cannot form a link"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior,
		Point(TEXT("A"), 2), Point(TEXT("B"), 2), Route));
	Prepare(Fixture, { Door(TEXT("Dangling"), EInteriorEntranceKind::Link, 1, 1, TEXT("Missing")) }, {});
	TestFalse(TEXT("Dangling link returns a useful failure"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior,
		Point(TEXT("A")), Point(TEXT("B")), Route));
	TestFalse(TEXT("Unreachable result reports an error"), Route.Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationEntranceStateTest, "InNavi.Navigation.Entrances.SelectionAndDisplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationEntranceStateTest::RunTest(const FString& Parameters)
{
	using namespace NavigationEntranceRoutingTests;
	FNavigationSearchFixture Fixture;
	Prepare(Fixture, { Door(TEXT("A-Link"), EInteriorEntranceKind::Link, 1, 1, TEXT("B")) }, {});
	ANavPlayerController* PC = Fixture.MakeController();
	PC->OnInteriorContentChanged.AddUniqueDynamic(Fixture.Nav, &UNavigationPanelWidget::NotifyInteriorChanged);
	Fixture.Nav->ToggleSelectStart(); Fixture.Nav->SelectPoint(FVector2D(50,50), 1);
	TestTrue(TEXT("Start is selected in A"), Fixture.Nav->HasStart());
	PC->LoadInteriorForBuilding(TEXT("B"));
	TestTrue(TEXT("Switching buildings retains start"), Fixture.Nav->HasStart() && Fixture.Nav->GetStart().BuildingId == TEXT("A"));
	Fixture.Nav->ToggleSelectDestination(); Fixture.Nav->SelectPoint(FVector2D(50,50), 1);
	TestTrue(TEXT("Find Route permits endpoints in different buildings"), Fixture.Nav->CanFindRoute());
	Fixture.Nav->RequestFindRoute();
	TestFalse(TEXT("Actual button handler computes cross-building route"), Fixture.Nav->GetRoutePoints().IsEmpty());
	TestTrue(TEXT("Directions name both buildings and the link"), Fixture.Nav->BuildRouteDetails().Contains(TEXT("from Alpha to Beta")));
	const auto& Points = Fixture.Nav->GetRoutePoints();
	auto View = UNavigationRouteVisualization::BuildView(Points, &Fixture.Nav->GetStart(), &Fixture.Nav->GetDestination(),
		FVector2D(110,0), 10, 300, 1, true, TEXT("B"));
	TestFalse(TEXT("Start marker is hidden in a different building on the same floor"), View.Start.IsSet());
	TestTrue(TEXT("Destination marker uses its owning building's world position"), View.Destination.IsSet() && View.Destination.GetValue().Equals(FVector(1600,500,15)));
	TestTrue(TEXT("Local link label names other building"), View.Labels.ContainsByPredicate([](const auto& Label) { return Label.Text.Contains(TEXT("from A")); }));
	TestFalse(TEXT("2D visibility does not project A into B's local coordinates"), UNavigationRouteVisualization::IsPointVisible(Fixture.Nav->GetStart(), TEXT("B"), 1));
	PC->LoadInteriorForBuilding(TEXT("A"));
	TestFalse(TEXT("Viewing the previous building retains the route"), Fixture.Nav->GetRoutePoints().IsEmpty());
	PC->SelectEntrance(TEXT("A-Link"));
	TestTrue(TEXT("The actual delete command removes the link"), PC->DeleteSelection());
	TestTrue(TEXT("Entrance edits hide stale guidance and retain endpoints"), Fixture.Nav->GetRoutePoints().IsEmpty() && Fixture.Nav->CanFindRoute());
	Fixture.Nav->RequestFindRoute();
	TestTrue(TEXT("Removed link makes the journey unreachable"), Fixture.Nav->DidLastFindRouteFail());
	TestTrue(TEXT("Entrance removal can be undone"), PC->UndoInteriorEdit());
	Fixture.Nav->RequestFindRoute();
	TestFalse(TEXT("Undo restores the cross-building route"), Fixture.Nav->GetRoutePoints().IsEmpty());
	Fixture.Nav->SetRouteGuidanceVisible(false);
	PC->LoadInteriorForBuilding(TEXT("B"));
	TestFalse(TEXT("Switching buildings retains route visibility preference"), Fixture.Nav->IsRouteGuidanceVisible());
	Fixture.Nav->RequestClearRoute();
	TestFalse(TEXT("Clear removes cross-building state"), Fixture.Nav->HasStart() || Fixture.Nav->HasDestination());
	FNavigationWorldRoutePoint ImportedA, ImportedB;
	ImportedA.BuildingId = TEXT("A"); ImportedA.WorldPosition = FVector(500,500,0);
	ImportedB.BuildingId = TEXT("B"); ImportedB.WorldPosition = FVector(1600,500,0);
	TestTrue(TEXT("World-route adapter accepts building identity per point"), Fixture.Nav->ApplyWorldRouteResult(TEXT("B"), { ImportedA, ImportedB }));
	TestTrue(TEXT("World-route adapter uses each building's own offset"), Fixture.Nav->GetStart().Position.Equals(FVector2D(50,50))
		&& Fixture.Nav->GetDestination().Position.Equals(FVector2D(50,50)) && Fixture.Nav->GetStart().BuildingId == TEXT("A"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationEntrancePersistenceTest, "InNavi.Navigation.Entrances.PersistenceAndLegacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationEntrancePersistenceTest::RunTest(const FString& Parameters)
{
	using namespace NavigationEntranceRoutingTests;
	FNavigationSearchFixture Fixture;
	Prepare(Fixture, { Door(TEXT("A-Link"), EInteriorEntranceKind::Link, 1, 1, TEXT("B")) }, {});
	Fixture.Interior->SyncInteriorToBuildingGenerator();
	const FString Path = FPaths::ProjectSavedDir() / (TEXT("EntranceRoutes-") + FGuid::NewGuid().ToString() + TEXT(".json"));
	TestTrue(TEXT("Entrances save with the project"), Fixture.Generator->SaveBuildingsToFile(Path));
	Fixture.Interior->ClearLoadedInterior(); Fixture.Generator->ClearAllBuildings();
	TestEqual(TEXT("Both buildings reload"), Fixture.Generator->LoadBuildingsFromFile(Path), 2);
	Fixture.Interior->LoadInteriorForBuilding(TEXT("B"));
	IFileManager::Get().Delete(*Path);
	FNavigationRouteResult Route;
	TestTrue(TEXT("Saved one-sided link routes through a regenerated mirror"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior,
		Point(TEXT("B")), Point(TEXT("A")), Route));

	// An old file has no entrances array and stores its exterior exit on a room door.
	auto Legacy = InteriorData({});
	Legacy->RemoveField(TEXT("entrances"));
	auto Room = MakeShared<FJsonObject>();
	Room->SetStringField(TEXT("id"), TEXT("Legacy-Room")); Room->SetStringField(TEXT("name"), TEXT("Legacy Room"));
	TArray<TSharedPtr<FJsonValue>> Footprint;
	for (const FVector2D& P : TArray<FVector2D>{ {0,20}, {30,20}, {30,80}, {0,80} })
		Footprint.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y) }));
	Room->SetArrayField(TEXT("footprint"), Footprint);
	auto Opening = MakeShared<FJsonObject>();
	Opening->SetStringField(TEXT("id"), TEXT("Old-Exit")); Opening->SetStringField(TEXT("type"), TEXT("door"));
	Opening->SetStringField(TEXT("connectsTo"), TEXT("outside")); Opening->SetNumberField(TEXT("edgeIndex"), 3);
	Opening->SetNumberField(TEXT("offset"), 180); Opening->SetNumberField(TEXT("width"), 240);
	Room->SetArrayField(TEXT("openings"), { MakeShared<FJsonValueObject>(Opening) });
	Legacy->GetArrayField(TEXT("floors"))[0]->AsObject()->SetArrayField(TEXT("rooms"), { MakeShared<FJsonValueObject>(Room) });
	Fixture.Generator->SetInteriorDataForBuilding(TEXT("A"), Legacy);
	Fixture.Interior->LoadInteriorForBuilding(TEXT("A"));
	const AInteriorRoom* Loaded = Fixture.Interior->GetRoomById(TEXT("Legacy-Room"));
	if (!TestNotNull(TEXT("Legacy room loads"), Loaded)) return false;
	TestTrue(TEXT("Loading preserves legacy exterior destination"), Loaded->GetOpenings()[0].ConnectsTo == TEXT("outside"));
	TestTrue(TEXT("Legacy room exit still routes outdoors without new entrances"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior,
		Point(TEXT("A"), 1, FVector2D(15,50)), Point(TEXT("A"), 0, FVector2D(-20,50)), Route));
	Fixture.Interior->SyncInteriorToBuildingGenerator();
	Fixture.Interior->LoadInteriorForBuilding(TEXT("B"));
	TestTrue(TEXT("Legacy room exit also works when its building is unopened"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior,
		Point(TEXT("A"), 1, FVector2D(15,50)), Point(TEXT("A"), 0, FVector2D(-20,50)), Route));
	return true;
}

// Finding a route in slices gives the route found all at once, and the panel's search can be stopped in the ways a person can.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationSlicedRouteTest, "InNavi.Navigation.SlicedRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationSlicedRouteTest::RunTest(const FString& Parameters)
{
	using namespace NavigationEntranceRoutingTests;
	FNavigationSearchFixture Fixture;
	Prepare(Fixture, { Door(TEXT("A-Link"), EInteriorEntranceKind::Link, 1, 1, TEXT("B")) }, {});
	ANavPlayerController* PC = Fixture.MakeController();
	PC->OnInteriorContentChanged.AddUniqueDynamic(Fixture.Nav, &UNavigationPanelWidget::NotifyInteriorChanged);
	UNavigationPanelWidget* Nav = Fixture.Nav;
	Nav->ToggleSelectStart(); Nav->SelectPoint(FVector2D(50,50), 1);
	PC->LoadInteriorForBuilding(TEXT("B"));
	Nav->ToggleSelectDestination(); Nav->SelectPoint(FVector2D(50,50), 1);
	if (!TestTrue(TEXT("both ends are set"), Nav->CanFindRoute())) return false;

	FNavigationRouteResult AllAtOnce;
	const bool bFound = FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior, Nav->GetStart(), Nav->GetDestination(), AllAtOnce);
	TestTrue(TEXT("the route exists"), bFound);

	FNavigationRouteJob Job(*Fixture.Generator, *Fixture.Interior, Nav->GetStart(), Nav->GetDestination());
	int32 Steps = 0; float Last = 0.f;
	while (!Job.Step(0.0) && Steps < 100000)
	{
		++Steps; const float Now = Job.GetProgress();
		TestTrue(TEXT("progress only goes forward, up to 1"), Now >= Last && Now <= 1.f); Last = Now;
		TestFalse(TEXT("says what it is doing"), Job.GetPhase().IsEmpty());
	}
	TestTrue(TEXT("it took more than one slice"), Steps > 1);
	TestEqual(TEXT("the same outcome as all at once"), Job.Succeeded(), bFound);
	TestEqual(TEXT("the same number of points"), Job.GetResult().Points.Num(), AllAtOnce.Points.Num());
	bool bSamePoints = Job.GetResult().Points.Num() == AllAtOnce.Points.Num();
	for (int32 I = 0; bSamePoints && I < AllAtOnce.Points.Num(); ++I)
	{
		const auto& A = Job.GetResult().Points[I]; const auto& B = AllAtOnce.Points[I];
		bSamePoints = A.BuildingId == B.BuildingId && A.Floor == B.Floor && A.ConnectorId == B.ConnectorId && A.Position.Equals(B.Position);
	}
	TestTrue(TEXT("the same points"), bSamePoints);

	// The panel's search ends with the route applied as RequestFindRoute applies it.
	Nav->StartFindRoute();
	TestTrue(TEXT("the search is running"), Nav->IsFindingRoute());
	TestTrue(TEXT("and says so"), Nav->GetDisplayStatusMessage().Contains(TEXT("Finding route")));
	while (!Nav->StepFindRoute(0.0)) {}
	TestFalse(TEXT("finished"), Nav->IsFindingRoute());
	TestEqual(TEXT("the route is shown"), Nav->GetRoutePoints().Num(), AllAtOnce.Points.Num());
	TestFalse(TEXT("not marked as failed"), Nav->DidLastFindRouteFail());

	// An edit of the interior stops it, and the old route goes as it does after any edit.
	Nav->StartFindRoute(); Nav->StepFindRoute(0.0);
	Nav->NotifyInteriorChanged();
	TestFalse(TEXT("an interior edit stops the search"), Nav->IsFindingRoute());
	TestTrue(TEXT("the route of before the edit is gone"), Nav->GetRoutePoints().IsEmpty());

	// Cancel stops it with a message and no route.
	Nav->StartFindRoute(); Nav->StepFindRoute(0.0);
	Nav->CancelFindRoute();
	TestFalse(TEXT("cancelled"), Nav->IsFindingRoute());
	TestTrue(TEXT("says so"), Nav->GetDisplayStatusMessage().Contains(TEXT("cancelled")));
	TestTrue(TEXT("and shows no route"), Nav->GetRoutePoints().IsEmpty());
	TestTrue(TEXT("stepping a stopped search is harmless"), Nav->StepFindRoute(0.0));

	// Clearing stops it too.
	Nav->StartFindRoute(); Nav->StepFindRoute(0.0);
	Nav->RequestClearRoute();
	TestFalse(TEXT("clearing stops the search"), Nav->IsFindingRoute());
	TestTrue(TEXT("and nothing is shown"), Nav->GetRoutePoints().IsEmpty() && !Nav->HasStart());
	return true;
}

#endif
