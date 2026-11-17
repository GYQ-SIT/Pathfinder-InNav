#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "BuildingInfoPanelWidget.h"
#include "EditScreenWidget.h"
#include "InteriorRoom.h"
#include "MapPanelWidget.h"
#include "PolyBuilding.h"
#include "UiTestFixture.h"

#include "GameFramework/DefaultPawn.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"

namespace SeeThroughTests
{
	// The opacity of a building that is as clear as it gets (the walls and roof of a revealed building).
	constexpr float ClearOpacity = 0.12f;

	// Puts the camera (the pawn of the controller) this far above the building, and lets the controller work out what that shows.
	void PutCameraAbove(ANavPlayerController* Controller, APolyBuilding* Building, float Above)
	{
		FVector Origin, Extent;
		Building->GetActorBounds(false, Origin, Extent);
		Controller->GetPawn()->SetActorLocation(Origin + FVector(0.f, 0.f, Extent.Z + Above));
		// Changing the setting to what it is already still makes the controller look at the camera again.
		Controller->SetCampusSeeThroughEnabled(Controller->IsCampusSeeThroughEnabled());
	}
}

// While a route is being gone through the camera follows its steps and goes close to the buildings: they stay solid then, and fade with the
// distance again when the route is hidden or cleared.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeeThroughNavigationTest, "InNavi.Building.SeeThrough.NavigationKeepsBuildingsSolid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeeThroughNavigationTest::RunTest(const FString& Parameters)
{
	using namespace SeeThroughTests;
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	ADefaultPawn* Pawn = Fixture.World->SpawnActor<ADefaultPawn>();
	Controller->Possess(Pawn);
	APolyBuilding* E2 = Fixture.Generator->FindBuildingById(TEXT("E2"));
	if (!TestNotNull(TEXT("E2"), E2) || !TestTrue(TEXT("a pawn to be the camera"), Controller->GetPawn() != nullptr))
	{
		return false;
	}
	Controller->BuildCampusInteriorPreviewForTests();
	PutCameraAbove(Controller, E2, 500.f);
	TestNearlyEqual(TEXT("close to the building, with no route, it is clear"), E2->GetBuildingOpacity(), ClearOpacity, 0.01f);
	TestFalse(TEXT("no route is being gone through"), Controller->IsNavigationRouteActive());

	// A route is found: the camera is as close as before, but the building is solid.
	FNavigationRouteResult Route;
	FNavigationRoutePoint A, B;
	A.BuildingId = TEXT("E2"); A.Floor = 1; A.Position = FVector2D(50.f, 50.f);
	B = A; B.Position = FVector2D(80.f, 80.f);
	Route.Points = { A, B };
	Fixture.Nav->ApplyRouteResult(Route);
	TestTrue(TEXT("a route is being gone through"), Controller->IsNavigationRouteActive());
	PutCameraAbove(Controller, E2, 500.f);
	TestNearlyEqual(TEXT("with a route the building stays solid"), E2->GetBuildingOpacity(), 1.f, 0.01f);
	TestEqual(TEXT("and its rooms stay out of sight"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 0);

	// A building the user made transparent is theirs to decide: it stays clear.
	Controller->SetBuildingMadeTransparent(TEXT("E2"), true);
	TestNearlyEqual(TEXT("made transparent it is clear with a route too"), E2->GetBuildingOpacity(), ClearOpacity, 0.01f);
	Controller->SetBuildingMadeTransparent(TEXT("E2"), false);

	// Hidden or cleared: the distance decides again.
	Fixture.Nav->SetRouteGuidanceVisible(false);
	PutCameraAbove(Controller, E2, 500.f);
	TestNearlyEqual(TEXT("with the route hidden the close building is clear"), E2->GetBuildingOpacity(), ClearOpacity, 0.01f);
	Fixture.Nav->SetRouteGuidanceVisible(true);
	PutCameraAbove(Controller, E2, 500.f);
	TestNearlyEqual(TEXT("shown again, it is solid"), E2->GetBuildingOpacity(), 1.f, 0.01f);
	Fixture.Nav->RequestClearRoute();
	PutCameraAbove(Controller, E2, 500.f);
	TestFalse(TEXT("the route is cleared: none is being gone through"), Controller->IsNavigationRouteActive());
	TestNearlyEqual(TEXT("and the close building is clear again"), E2->GetBuildingOpacity(), ClearOpacity, 0.01f);
	return true;
}

// The camera distance reveals a building and its rooms; the switch for every building turns that off; a building made transparent from
// the Building panel stays clear whatever the switch says, and shows only the rooms of the floor chosen.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeeThroughDistanceTest, "InNavi.Building.SeeThrough.Distance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeeThroughDistanceTest::RunTest(const FString& Parameters)
{
	using namespace SeeThroughTests;
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	ADefaultPawn* Pawn = Fixture.World->SpawnActor<ADefaultPawn>();
	Controller->Possess(Pawn);
	APolyBuilding* E2 = Fixture.Generator->FindBuildingById(TEXT("E2"));
	APolyBuilding* E1 = Fixture.Generator->FindBuildingById(TEXT("E1"));
	if (!TestNotNull(TEXT("E2"), E2) || !TestNotNull(TEXT("E1"), E1) || !TestTrue(TEXT("a pawn to be the camera"), Controller->GetPawn() != nullptr))
	{
		return false;
	}
	// Far away: the whole, solid building, and its rooms out of sight.
	PutCameraAbove(Controller, E2, 100000.f);
	Controller->BuildCampusInteriorPreviewForTests();
	TestNearlyEqual(TEXT("far away the building is opaque"), E2->GetBuildingOpacity(), 1.f, 0.01f);
	TestEqual(TEXT("and none of its rooms show"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 0);
	TestEqual(TEXT("unrevealed rooms have no preview actors"), Controller->CountCachedCampusRoomsForTests(TEXT("E2")), 0);

	// Close: walls and roof clear, the rooms show.
	PutCameraAbove(Controller, E2, 500.f);
	TestNearlyEqual(TEXT("close the walls and roof are clear"), E2->GetBuildingOpacity(), ClearOpacity, 0.01f);
	const int32 Total = Controller->CountShownCampusRoomsForTests(TEXT("E2"));
	TestTrue(TEXT("and its rooms show"), Total > 0);
	TestEqual(TEXT("revealing by distance creates the rooms"), Controller->CountCachedCampusRoomsForTests(TEXT("E2")), Total);

	// The switch for every building off: solid again, whatever the distance.
	Controller->SetCampusSeeThroughEnabled(false);
	TestNearlyEqual(TEXT("with see-through off the building is opaque"), E2->GetBuildingOpacity(), 1.f, 0.01f);
	TestEqual(TEXT("and its rooms are out of sight"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 0);

	// Made transparent from the Building panel: that comes first.
	Controller->SetBuildingMadeTransparent(TEXT("E2"), true);
	TestNearlyEqual(TEXT("made transparent it is clear although see-through is off"), E2->GetBuildingOpacity(), ClearOpacity, 0.01f);
	TestEqual(TEXT("and all its rooms show"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), Total);
	TestNearlyEqual(TEXT("another building is not touched by it"), E1->GetBuildingOpacity(), 1.f, 0.01f);

	// The floor to show: every room is on exactly one floor.
	int32 SumOverFloors = 0;
	for (int32 Floor = 1; Floor <= 20; ++Floor)
	{
		Controller->SetBuildingRoomFloor(TEXT("E2"), Floor);
		SumOverFloors += Controller->CountShownCampusRoomsForTests(TEXT("E2"));
	}
	TestEqual(TEXT("the rooms shown for each floor add up to all of them"), SumOverFloors, Total);
	Controller->SetBuildingRoomFloor(TEXT("E2"), 99);
	TestEqual(TEXT("a floor the building does not have shows no room"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 0);
	Controller->SetBuildingRoomFloor(TEXT("E2"), 0);
	TestEqual(TEXT("floor 0 is every floor"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), Total);

	// Not transparent any more: back to what the switch says.
	Controller->SetBuildingMadeTransparent(TEXT("E2"), false);
	TestNearlyEqual(TEXT("no longer made transparent it is opaque again"), E2->GetBuildingOpacity(), 1.f, 0.01f);
	TestEqual(TEXT("and its rooms are out of sight"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 0);

	// The switch on again: the camera is still close.
	Controller->SetCampusSeeThroughEnabled(true);
	TestNearlyEqual(TEXT("with see-through on again the close building is clear"), E2->GetBuildingOpacity(), ClearOpacity, 0.01f);
	TestEqual(TEXT("revealing again reuses the cached rooms"), Controller->CountCachedCampusRoomsForTests(TEXT("E2")), Total);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeeThroughLazyPreviewTest, "InNavi.Building.SeeThrough.LazyPreview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeeThroughLazyPreviewTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	Controller->SetCampusSeeThroughEnabled(false);
	Controller->Possess(Fixture.World->SpawnActor<ADefaultPawn>());
	TestTrue(TEXT("the project loads through the app's load path"),
		Controller->LoadBuildingDataFromPathOrDatabase(FPaths::ProjectDir() / TEXT("Data/buildings.json")) > 0);
	TestFalse(TEXT("loading an opaque campus creates no interior actors"), static_cast<bool>(TActorIterator<AInteriorRoom>(Fixture.World)));
	Controller->SetBuildingRoomFloor(TEXT("E2"), 1);
	TestEqual(TEXT("choosing a floor on an opaque building creates nothing"), Controller->CountCachedCampusRoomsForTests(TEXT("E2")), 0);

	Controller->SetCampusBuildingsVisible(false);
	Controller->SetBuildingMadeTransparent(TEXT("E2"), true);
	TestEqual(TEXT("a hidden building creates no previews even if made transparent"), Controller->CountCachedCampusRoomsForTests(TEXT("E2")), 0);
	Controller->SetCampusBuildingsVisible(true);
	Controller->SetBuildingMadeTransparent(TEXT("E2"), true);
	TestEqual(TEXT("first reveal creates only the selected floor's three rooms"), Controller->CountCachedCampusRoomsForTests(TEXT("E2")), 3);
	TestEqual(TEXT("the selected floor is visible immediately"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 3);
	TestEqual(TEXT("the other authored building is not loaded"), Controller->CountCachedCampusRoomsForTests(TEXT("E6")), 0);

	TArray<TWeakObjectPtr<AInteriorRoom>> FirstFloorRooms;
	for (TActorIterator<AInteriorRoom> It(Fixture.World); It; ++It) FirstFloorRooms.Add(*It);
	Controller->SetBuildingRoomFloor(TEXT("E2"), 2);
	TestEqual(TEXT("switching floors adds just the newly needed room"), Controller->CountCachedCampusRoomsForTests(TEXT("E2")), 4);
	TestEqual(TEXT("only floor two shows"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 1);
	Controller->SetBuildingRoomFloor(TEXT("E2"), 99);
	TestEqual(TEXT("a missing floor creates no rooms"), Controller->CountCachedCampusRoomsForTests(TEXT("E2")), 4);
	TestEqual(TEXT("a missing floor shows no rooms"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 0);
	Controller->SetBuildingRoomFloor(TEXT("E2"), 1);
	TestEqual(TEXT("returning to floor one shows its cached rooms"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 3);
	for (const auto& Room : FirstFloorRooms)
	{
		TestTrue(TEXT("switching floors preserves the original room actors"), Room.IsValid() && !Room->IsHidden());
	}
	Controller->SetBuildingRoomFloor(TEXT("E2"), 0);
	TestEqual(TEXT("all floors shows every room without duplication"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 4);
	TestEqual(TEXT("all floors reuses already created rooms"), Controller->CountCachedCampusRoomsForTests(TEXT("E2")), 4);

	Controller->SetBuildingRoomFloor(TEXT("E6"), 1);
	Controller->SetBuildingMadeTransparent(TEXT("E6"), true);
	TestEqual(TEXT("a second building can be revealed at the same time"), Controller->CountShownCampusRoomsForTests(TEXT("E6")), 4);
	TestEqual(TEXT("the first building stays visible"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 4);
	Controller->SetBuildingRoomFloor(TEXT("E6"), 0);
	TestEqual(TEXT("all floors adds the second building's previously unneeded floor"), Controller->CountShownCampusRoomsForTests(TEXT("E6")), 5);

	Controller->SetBuildingMadeTransparent(TEXT("E2"), false);
	Controller->SetBuildingMadeTransparent(TEXT("E6"), false);
	TestEqual(TEXT("opaque buildings keep their previews cached"), Controller->CountCachedCampusRoomsForTests(TEXT("E2")), 4);
	TestEqual(TEXT("opaque cached rooms are hidden"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 0);
	Controller->BuildCampusInteriorPreviewForTests();
	TestFalse(TEXT("refresh drops stale actors without recreating hidden interiors"), static_cast<bool>(TActorIterator<AInteriorRoom>(Fixture.World)));
	for (const auto& Room : FirstFloorRooms) TestFalse(TEXT("refresh destroys the old preview actors"), Room.IsValid());
	return true;
}

// The two distances keep a gap between them, whichever one is moved.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeeThroughDistancesTest, "InNavi.Building.SeeThrough.Distances", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeeThroughDistancesTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	TestNearlyEqual(TEXT("clears from 80 m"), Controller->GetCampusRevealFarDistance(), 8000.f, 0.1f);
	TestNearlyEqual(TEXT("fully clear within 70 m"), Controller->GetCampusRevealNearDistance(), 7000.f, 0.1f);
	TestTrue(TEXT("see-through is on to start with"), Controller->IsCampusSeeThroughEnabled());

	Controller->SetCampusRevealFarDistance(3000.f);
	TestNearlyEqual(TEXT("the far distance is set"), Controller->GetCampusRevealFarDistance(), 3000.f, 0.1f);
	TestTrue(TEXT("and the near one is pulled in below it"), Controller->GetCampusRevealNearDistance() <= 2900.f + 0.1f);

	Controller->SetCampusRevealNearDistance(5000.f);
	TestNearlyEqual(TEXT("the near distance is set"), Controller->GetCampusRevealNearDistance(), 5000.f, 0.1f);
	TestTrue(TEXT("and the far one is pushed out beyond it"), Controller->GetCampusRevealFarDistance() >= 5100.f - 0.1f);

	Controller->SetCampusRevealFarDistance(0.f);
	TestTrue(TEXT("however small the far distance is asked to be, it stays beyond the near one"), Controller->GetCampusRevealFarDistance() >= Controller->GetCampusRevealNearDistance() + 100.f - 0.1f);
	return true;
}

// The controls: the Building panel's button and floor drop-down, the Map panel's switch and sliders.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeeThroughPanelsTest, "InNavi.Building.SeeThrough.Panels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeeThroughPanelsTest::RunTest(const FString& Parameters)
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

	// The Building panel: a button that says OFF, then ON.
	TestTrue(TEXT("E2 can be selected"), Controller->SelectBuildingById(TEXT("E2")));
	Panel->Refresh();
	TestFalse(TEXT("the button starts off"), Panel->IsTransparentOnForTests());
	TestEqual(TEXT("and says so"), Panel->GetTransparentTextForTests(), FString(TEXT("Transparent OFF")));
	Panel->ClickTransparentForTests();
	TestTrue(TEXT("clicking turns it on"), Panel->IsTransparentOnForTests());
	TestEqual(TEXT("and it says so"), Panel->GetTransparentTextForTests(), FString(TEXT("Transparent ON")));
	TestTrue(TEXT("the building is made transparent"), Controller->IsBuildingMadeTransparent(TEXT("E2")));
	Panel->ClickTransparentForTests();
	TestFalse(TEXT("clicking again turns it off"), Panel->IsTransparentOnForTests());
	TestFalse(TEXT("and the building is no longer made transparent"), Controller->IsBuildingMadeTransparent(TEXT("E2")));
	Panel->ClickTransparentForTests();

	// The floor drop-down: "All floors" and one item for every floor of the building.
	const int32 Floors = FCString::Atoi(*Panel->GetFloorsTextForTests());
	TestTrue(TEXT("E2 has floors"), Floors >= 1);
	TestEqual(TEXT("the drop-down lists all floors and every floor"), Panel->GetRoomFloorOptionCountForTests(), Floors + 1);
	TestEqual(TEXT("it starts on all floors"), Panel->GetRoomFloorTextForTests(), FString(TEXT("All floors")));
	const FString TopFloor = FString::Printf(TEXT("Floor %d"), Floors);
	Panel->SelectRoomFloorForTests(TopFloor);
	TestEqual(TEXT("choosing a floor sets it on the controller"), Controller->GetBuildingRoomFloor(TEXT("E2")), Floors);

	// Each building keeps its own: another one shows the defaults, and E2 shows what it was set to when it comes back.
	TestTrue(TEXT("E1 can be selected"), Controller->SelectBuildingById(TEXT("E1")));
	Panel->RefreshIfChanged();
	TestFalse(TEXT("another building is not made transparent"), Panel->IsTransparentOnForTests());
	TestEqual(TEXT("and shows all floors"), Panel->GetRoomFloorTextForTests(), FString(TEXT("All floors")));
	TestTrue(TEXT("E2 can be selected again"), Controller->SelectBuildingById(TEXT("E2")));
	Panel->RefreshIfChanged();
	TestTrue(TEXT("E2 is still made transparent"), Panel->IsTransparentOnForTests());
	TestEqual(TEXT("and still shows the floor chosen"), Panel->GetRoomFloorTextForTests(), TopFloor);

	// The Map panel: the switch for every building.
	Map->Refresh();
	TestEqual(TEXT("the switch starts on"), Map->GetAllSeeThroughTextForTests(), FString(TEXT("See-through ON")));
	Map->ClickAllSeeThroughForTests();
	TestFalse(TEXT("clicking it turns see-through off"), Controller->IsCampusSeeThroughEnabled());
	TestEqual(TEXT("and it says so"), Map->GetAllSeeThroughTextForTests(), FString(TEXT("See-through OFF")));
	Map->ClickAllSeeThroughForTests();
	TestTrue(TEXT("clicking again turns it on"), Controller->IsCampusSeeThroughEnabled());

	// The two sliders follow the controller, in metres, and keep their gap.
	TestNearlyEqual(TEXT("the far slider starts at 80 m"), Map->GetRevealFarSliderForTests(), 80.f, 0.5f);
	TestNearlyEqual(TEXT("the near slider starts at 70 m"), Map->GetRevealNearSliderForTests(), 70.f, 0.5f);
	Map->MoveRevealFarSliderForTests(30.f);
	TestNearlyEqual(TEXT("moving the far slider sets the far distance"), Controller->GetCampusRevealFarDistance(), 3000.f, 0.1f);
	TestNearlyEqual(TEXT("the near slider is pulled in with it"), Map->GetRevealNearSliderForTests(), 29.f, 0.5f);
	Map->MoveRevealNearSliderForTests(50.f);
	TestNearlyEqual(TEXT("moving the near slider sets the near distance"), Controller->GetCampusRevealNearDistance(), 5000.f, 0.1f);
	TestNearlyEqual(TEXT("the far slider is pushed out with it"), Map->GetRevealFarSliderForTests(), 51.f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeeThroughPreviewLifecycleTest, "InNavi.Building.SeeThrough.PreviewLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeeThroughPreviewLifecycleTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	Controller->Possess(Fixture.World->SpawnActor<ADefaultPawn>());
	Controller->BuildCampusInteriorPreviewForTests();
	Controller->SetBuildingMadeTransparent(TEXT("E2"), true);
	const int32 Before = Controller->CountShownCampusRoomsForTests(TEXT("E2"));
	if (!TestTrue(TEXT("the campus preview shows E2's rooms"), Before > 0)) return false;
	Controller->LoadInteriorForBuilding(TEXT("E2"));
	TestTrue(TEXT("a room is deleted"), Controller->DeleteRoom(TEXT("E2-01-02")));
	const FString Path = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (FGuid::NewGuid().ToString() + TEXT(".json"));
	TestTrue(TEXT("the edited layout saves"), Controller->SaveCurrentProjectAs(Path));
	Controller->RestoreExteriorCameraView();
	TestEqual(TEXT("the exterior shows the saved rooms"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), Before - 1);
	Controller->SetCampusBuildingsVisible(false);
	Controller->SetBuildingMadeTransparent(TEXT("E2"), true);
	TestEqual(TEXT("hiding the shell hides its preview rooms"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 0);
	Controller->StartNewProject();
	TestFalse(TEXT("New clears preview actors"), static_cast<bool>(TActorIterator<AInteriorRoom>(Fixture.World)));
	IFileManager::Get().Delete(*Path);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
