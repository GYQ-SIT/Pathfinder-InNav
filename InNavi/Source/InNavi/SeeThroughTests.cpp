#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "BuildingInfoPanelWidget.h"
#include "EditScreenWidget.h"
#include "MapPanelWidget.h"
#include "PolyBuilding.h"
#include "UiTestFixture.h"

#include "GameFramework/DefaultPawn.h"
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
	Controller->BuildCampusInteriorPreviewForTests();

	// Far away: the whole, solid building, and its rooms out of sight.
	PutCameraAbove(Controller, E2, 100000.f);
	TestNearlyEqual(TEXT("far away the building is opaque"), E2->GetBuildingOpacity(), 1.f, 0.01f);
	TestEqual(TEXT("and none of its rooms show"), Controller->CountShownCampusRoomsForTests(TEXT("E2")), 0);

	// Close: walls and roof clear, the rooms show.
	PutCameraAbove(Controller, E2, 500.f);
	TestNearlyEqual(TEXT("close the walls and roof are clear"), E2->GetBuildingOpacity(), ClearOpacity, 0.01f);
	const int32 Total = Controller->CountShownCampusRoomsForTests(TEXT("E2"));
	TestTrue(TEXT("and its rooms show"), Total > 0);

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

#endif // WITH_DEV_AUTOMATION_TESTS
