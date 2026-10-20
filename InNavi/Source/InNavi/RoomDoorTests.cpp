#if WITH_DEV_AUTOMATION_TESTS

#include "InteriorRoom.h"
#include "UiTestFixture.h"

#include "Misc/AutomationTest.h"

// A door in a room's wall leads to the corridor (the walkable space of the building outside its rooms) or to another room. The way out
// of the building is a main door in the outer wall, so a room door is never offered "Outside".

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomDoorTargetsTest, "InNavi.Interior.RoomDoor.Targets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoomDoorTargetsTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	Fixture.Interior->SetActiveFloor(1);
	const TArray<AInteriorRoom*>& Rooms = Fixture.Interior->GetRoomsOnFloor(1);
	if (!TestFalse(TEXT("the ground floor has rooms"), Rooms.IsEmpty())) return false;
	const FString RoomId = Rooms[0]->RoomId;

	// Edge 0 has no door in the example project's first room.
	const FString DoorId = Fixture.Interior->AddDoorToRoom(RoomId, 0, 5.f);
	if (!TestFalse(TEXT("a door can be added to a room"), DoorId.IsEmpty())) return false;
	FInteriorOpening Opening;
	TestTrue(TEXT("it can be read back"), Fixture.Interior->GetDoorOpening(RoomId, DoorId, Opening));
	TestEqual(TEXT("a new door leads to the corridor"), Opening.ConnectsTo, FString(TEXT("corridor")));

	Controller->SelectDoor(RoomId, DoorId);
	const TArray<FString> Targets = Controller->GetConnectableTargetIds();
	TestTrue(TEXT("the corridor is offered"), Targets.Contains(TEXT("corridor")));
	TestFalse(TEXT("and Outside is not, even on the ground floor"), Targets.Contains(TEXT("Outside")));
	TestTrue(TEXT("other rooms are"), Targets.Num() > 1);
	TestEqual(TEXT("and it shows as the corridor"), Controller->ResolveConnectsToDisplayName(Opening.ConnectsTo), FString(TEXT("corridor")));
	TestEqual(TEXT("a door with nothing set (an older project) shows as the corridor too"), Controller->ResolveConnectsToDisplayName(FString()), FString(TEXT("corridor")));

	// A door an older project already set to outside keeps showing it.
	Fixture.Interior->SetDoorConnectsTo(RoomId, DoorId, TEXT("outside"));
	Controller->SelectDoor(FString(), FString());
	Controller->SelectDoor(RoomId, DoorId);
	TestTrue(TEXT("a door already set to outside still lists it"), Controller->GetConnectableTargetIds().Contains(TEXT("Outside")));
	return true;
}

// Loading an older project turns a room door that led outside into one that leads to the corridor; the way out is a main door now.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomDoorLegacyOutsideTest, "InNavi.Interior.RoomDoor.LegacyOutside", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoomDoorLegacyOutsideTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	// The example project's E2 vestibule has a door whose file says "outside".
	FInteriorOpening Opening;
	TestTrue(TEXT("the vestibule door exists"), Fixture.Interior->GetDoorOpening(TEXT("E2-01-OUTSIDE-ENTRY"), TEXT("E2-01-OUTSIDE-ENTRY-D02"), Opening));
	TestEqual(TEXT("and loaded as leading to the corridor, not outside"), Opening.ConnectsTo, FString(TEXT("corridor")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
