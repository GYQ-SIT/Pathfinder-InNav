#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "InteriorRoom.h"
#include "Misc/AutomationTest.h"

// Two rooms side by side in a turned row - their edges on one line, a gap between them - are not overlapping, whatever the angle. The
// overlap check used to read some of them as crossing, because float noise tipped the sign of a cross product of nearly collinear edges.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomOverlapTurnedRowTest, "InNavi.Interior.RoomOverlap.TurnedRow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoomOverlapTurnedRowTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	Fixture.Interior->SetActiveFloor(3);
	// E2's own lifts and stairs sit on this floor; a row that would wall one in is refused for its own reason, which is not what is tested.
	AddExpectedError(TEXT("would wall in"), EAutomationExpectedErrorFlags::Contains, 0);

	int32 Rows = 0, WronglyRefused = 0;
	for (int32 Angle = 3; Angle < 90; Angle += 3)
	{
		for (int32 Shift = 0; Shift < 4; ++Shift)
		{
			const FVector2D U(FMath::Cos(FMath::DegreesToRadians(static_cast<double>(Angle))), FMath::Sin(FMath::DegreesToRadians(static_cast<double>(Angle))));
			const FVector2D V(-U.Y, U.X);
			const FVector2D Origin(300.0 + Shift * 7.3, 50.0 + Shift * 2.9);
			auto Room = [&](double From, double To)
			{
				return TArray<FVector2D>{ Origin + U * From, Origin + U * To, Origin + U * To + V * 12.0, Origin + U * From + V * 12.0 };
			};

			FString Error;
			AInteriorRoom* First = Fixture.Interior->CreateRoomFromFootprint(Room(0, 30), FString(), FString(), true, false, false, Error);
			if (!First)
			{
				continue;
			}
			// A 0.3 m gap (3 footprint units in the example project).
			AInteriorRoom* Second = Fixture.Interior->CreateRoomFromFootprint(Room(33, 63), FString(), FString(), true, false, false, Error);
			if (Second || Error.Contains(TEXT("overlaps")))
			{
				++Rows;
				WronglyRefused += Second ? 0 : 1;
			}
			Fixture.Interior->DeleteRoom(First->RoomId);
			if (Second)
			{
				Fixture.Interior->DeleteRoom(Second->RoomId);
			}
		}
	}
	TestTrue(TEXT("enough turned rows fit in the building to mean something"), Rows > 40);
	TestEqual(TEXT("no row of rooms with a gap between them is refused as overlapping"), WronglyRefused, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
