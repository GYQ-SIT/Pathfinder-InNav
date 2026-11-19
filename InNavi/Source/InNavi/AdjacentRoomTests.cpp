#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "DebugPanelWidget.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "InteriorScreenWidget.h"
#include "NavPlayerController.h"
#include "UiTestFixture.h"

#include "Misc/AutomationTest.h"

// PAT-194: rooms beside each other share a wall without false overlap errors, and a room dropped on another goes to the nearest spot beside it.

namespace AdjacentRoomTests
{
	void Prepare(FNavigationSearchFixture& F)
	{
		F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
		F.Generator->CreateRectangleBuilding(TEXT("ADJ"), TEXT("ADJ"), {500,500}, 1000, 1000, 0, 1, 300);
		F.Interior->LoadInteriorForBuilding(TEXT("ADJ"));
	}
	TArray<FVector2D> Square(double X, double Y, double Size) { return { {X, Y}, {X + Size, Y}, {X + Size, Y + Size}, {X, Y + Size} }; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdjacentRoomsTest, "InNavi.Interior.AdjacentRooms", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAdjacentRoomsTest::RunTest(const FString& Parameters)
{
	using namespace AdjacentRoomTests;
	AddExpectedError(TEXT("overlaps"), EAutomationExpectedErrorFlags::Contains, 0);
	FNavigationSearchFixture F; Prepare(F);
	FString Error;
	const double Tolerance = 5.0 / F.Generator->GetScale();
	if (!TestNotNull(TEXT("a room"), F.Interior->CreateRoomFromFootprint(Square(400, 400, 100), TEXT("A"), TEXT("A"), true, false, false, Error))) return false;

	// A neighbour that shares the wall exactly, one that stands a hair off it, and one whose wall is a hair crooked: all fine.
	TestNotNull(TEXT("sharing the wall exactly"), F.Interior->CreateRoomFromFootprint(Square(500, 400, 100), TEXT("B"), TEXT("B"), true, false, false, Error));
	const double Hair = Tolerance * 0.4;
	TestNotNull(TEXT("a hair off the wall"), F.Interior->CreateRoomFromFootprint(Square(400 - 100 - Hair, 400, 100), TEXT("C"), TEXT("C"), true, false, false, Error));
	TestNotNull(TEXT("a hair crooked"), F.Interior->CreateRoomFromFootprint({ {400, 500 + Hair * 0.5}, {500, 500 - Hair * 0.5}, {500, 600}, {400, 600} }, TEXT("D"), TEXT("D"), true, false, false, Error));
	TestEqual(TEXT("four separate rooms"), F.Interior->GetRoomsOnFloor(1).Num(), 4);

	// Really on top of a neighbour is still refused.
	TestNull(TEXT("half on top of A is refused"), F.Interior->CreateRoomFromFootprint(Square(450, 400, 100), TEXT("E"), TEXT("E"), true, false, false, Error));
	TestNull(TEXT("exactly on top of A is refused"), F.Interior->CreateRoomFromFootprint(Square(400, 400, 100), TEXT("F"), TEXT("F"), true, false, false, Error));

	// The nearest spot beside: 30 units onto A from the right goes 70 to the right... no: the shortest way out is the 30 back.
	TArray<FVector2D> Footprint = Square(500 + 700 - 30, 400, 100);
	F.Interior->DeleteRoom(TEXT("B")); F.Interior->DeleteRoom(TEXT("C")); F.Interior->DeleteRoom(TEXT("D"));
	Footprint = Square(500 - 30, 400, 100); // overlaps A (400..500) by 30 on its right side
	FString Beside;
	if (!TestTrue(TEXT("it can be nudged"), F.Interior->NudgeFootprintClear(Footprint, true, FString(), &Beside))) return false;
	TestEqual(TEXT("beside A"), Beside, FString(TEXT("A")));
	const FBox2D Moved(Footprint);
	TestTrue(TEXT("it moved 30 to the right, no more"), FMath::IsNearlyEqual(Moved.Min.X, 500.0, 0.01) && FMath::IsNearlyEqual(Moved.Min.Y, 400.0, 0.01));
	TestNotNull(TEXT("and there it fits"), F.Interior->CreateRoomFromFootprint(Footprint, TEXT("G"), TEXT("G"), true, false, false, Error));
	F.Interior->DeleteRoom(TEXT("G"));

	// A clear footprint is left as it is; one with nowhere to go (the building is full) is left alone and reported.
	TArray<FVector2D> Clear = Square(700, 700, 50);
	const TArray<FVector2D> ClearBefore = Clear;
	TestTrue(TEXT("a clear footprint is fine"), F.Interior->NudgeFootprintClear(Clear, true, FString()));
	TestTrue(TEXT("and unchanged"), Clear == ClearBefore);
	TArray<FVector2D> Huge = Square(-2000, -2000, 5000);
	const TArray<FVector2D> HugeBefore = Huge;
	TestFalse(TEXT("a shape that cannot fit anywhere is not moved"), F.Interior->NudgeFootprintClear(Huge, true, FString()));
	TestTrue(TEXT("and stays as it was"), Huge == HugeBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdjacentDropTest, "InNavi.Interior.AdjacentDrop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAdjacentDropTest::RunTest(const FString& Parameters)
{
	using namespace AdjacentRoomTests;
	AddExpectedError(TEXT("overlap"), EAutomationExpectedErrorFlags::Contains, 0);
	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	FString Error;
	if (!TestNotNull(TEXT("room A"), F.Interior->CreateRoomFromFootprint(Square(400, 400, 100), TEXT("A"), TEXT("A"), true, false, false, Error))) return false;
	if (!TestNotNull(TEXT("room B"), F.Interior->CreateRoomFromFootprint(Square(700, 400, 100), TEXT("B"), TEXT("B"), true, false, false, Error))) return false;
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(PC);
	UDebugPanelWidget* Debug = Screen->GetDebugPanelForTests(); if (Debug) Debug->BindToController();

	// B is dragged onto A, 30 units over it, and let go: it goes to the spot beside A (touching), with a message, instead of back.
	const TArray<FVector2D> Start = F.Interior->GetRoomById(TEXT("B"))->GetFootprint();
	TestTrue(TEXT("the drag moves it over A"), PC->MoveRoomTo(TEXT("B"), Square(470, 400, 100)));
	TestTrue(TEXT("let go with nudging, it is accepted"), PC->CommitRoomMove(TEXT("B"), Start, /*bNudgeToFreeSpot=*/true));
	const FBox2D Now(F.Interior->GetRoomById(TEXT("B"))->GetFootprint());
	TestTrue(TEXT("it stands against A's right wall"), FMath::IsNearlyEqual(Now.Min.X, 500.0, 0.01) && FMath::IsNearlyEqual(Now.Min.Y, 400.0, 0.01));
	if (Debug) TestTrue(TEXT("and the message says what it was moved beside"), Debug->GetLastErrorTextForTests().Contains(TEXT("beside 'A'")));

	// Without nudging, as before, it goes back where it was.
	const TArray<FVector2D> Here = F.Interior->GetRoomById(TEXT("B"))->GetFootprint();
	TestTrue(TEXT("dragged over A again"), PC->MoveRoomTo(TEXT("B"), Square(460, 400, 100)));
	TestFalse(TEXT("without nudging it is refused"), PC->CommitRoomMove(TEXT("B"), Here));
	TestTrue(TEXT("and put back"), F.Interior->GetRoomById(TEXT("B"))->GetFootprint() == Here);

	// A prefab room dropped on A is placed beside it instead of failing.
	PC->TogglePrefabRectangleMode();
	const int32 Before = F.Interior->GetRoomsOnFloor(1).Num();
	TestNotNull(TEXT("a rectangle dropped on A is placed"), PC->PlacePrefabRoomAt(FVector2D(450, 450)));
	TestEqual(TEXT("as a third room"), F.Interior->GetRoomsOnFloor(1).Num(), Before + 1);
	return true;
}

// PAT-195: a wall may be drawn inside a room as a partition; one that crosses the room's own wall is still refused.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInnerWallsTest, "InNavi.Interior.InnerWalls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInnerWallsTest::RunTest(const FString& Parameters)
{
	using namespace AdjacentRoomTests;
	AddExpectedError(TEXT("overlaps"), EAutomationExpectedErrorFlags::Contains, 0);
	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	if (PC->IsSnapEnabled()) PC->ToggleSnapEnabled();
	FString Error;
	if (!TestNotNull(TEXT("a room"), F.Interior->CreateRoomFromFootprint(Square(100, 100, 200), TEXT("R"), TEXT("R"), true, false, false, Error))) return false;

	// Drawn through the tool: the first click may be inside the room, and the wall is made.
	PC->ToggleDrawWallMode();
	PC->AddRoomPointAtFootprintPosition({150, 200});
	TestEqual(TEXT("the first point inside a room is accepted"), PC->GetPendingRoomFootprint().Num(), 1);
	PC->AddRoomPointAtFootprintPosition({250, 200});
	TestEqual(TEXT("so is the second"), PC->GetPendingRoomFootprint().Num(), 2);
	TestNotNull(TEXT("the wall is finished inside the room"), PC->FinishWall());

	// Free-standing, wall to wall, bent, and corner to corner: all partitions.
	TestNotNull(TEXT("from one wall of the room to the opposite one"), F.Interior->CreateWallFromPoints({{100,150},{300,150}}, TEXT("W2"), Error));
	TestNotNull(TEXT("an L inside the room"), F.Interior->CreateWallFromPoints({{110,110},{110,140},{140,140}}, TEXT("W3"), Error));
	TestNotNull(TEXT("corner to corner"), F.Interior->CreateWallFromPoints({{100,250},{300,290}}, TEXT("W4"), Error));
	TestEqual(TEXT("the room and four walls"), F.Interior->GetRoomsOnFloor(1).Num(), 5);
	TestTrue(TEXT("the room is still a room"), F.Interior->IsRoomClosedLoop(TEXT("R")));

	// A partition is saved and comes back.
	TestNotNull(TEXT("the partition is a wall of its own"), F.Interior->GetRoomById(TEXT("W2")));
	F.Generator->SetInteriorDataForBuilding(TEXT("ADJ"), F.Interior->CaptureInteriorSnapshot());
	F.Interior->LoadInteriorForBuilding(TEXT("ADJ"));
	TestEqual(TEXT("saved and loaded again: all five"), F.Interior->GetRoomsOnFloor(1).Num(), 5);

	// What is still refused: through the room's wall, out of the room, or across another wall.
	TestNull(TEXT("a wall out through the room's wall"), F.Interior->CreateWallFromPoints({{200,200},{400,200}}, TEXT("Out"), Error));
	TestNull(TEXT("a wall that starts outside and crosses in"), F.Interior->CreateWallFromPoints({{50,220},{150,220}}, TEXT("In"), Error));
	TestNull(TEXT("a wall across another wall"), F.Interior->CreateWallFromPoints({{200,110},{200,190}}, TEXT("Cross"), Error));
	TestNotNull(TEXT("a wall that stops at another wall is fine"), F.Interior->CreateWallFromPoints({{210,110},{210,150}}, TEXT("Stop"), Error));

	// A room drawn around a partition is as good as the other way round: both are the same plan.
	F.Interior->DeleteRoom(TEXT("R"));
	TestNotNull(TEXT("a room may be placed round walls that are already there"), F.Interior->CreateRoomFromFootprint(Square(100, 100, 200), TEXT("R2"), TEXT("R2"), true, false, false, Error));
	return true;
}

#endif
