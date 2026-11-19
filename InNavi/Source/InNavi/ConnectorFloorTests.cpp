#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorScreenWidget.h"
#include "InteriorTypes.h"
#include "LayoutValidation.h"
#include "NavPlayerController.h"
#include "NavigationRoutePlanner.h"
#include "PolyBuilding.h"
#include "UiTestFixture.h"

#include "DebugPanelWidget.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

// A lift, a stair or an escalator is one connector (a group) with a node on each floor it reaches, and each floor's node stands where it was
// put, turned and sized as that floor needs. Moving, rotating and resizing change the active floor's node alone; Shift changes every floor of the
// connector.

namespace ConnectorFloorTests
{
	struct FConnectorScene
	{
		FNavigationSearchFixture Fixture;
		double Units = 1.0; // footprint units in a metre
		FString Group;
		AInteriorGenerator* Interior = nullptr;

		FConnectorScene()
		{
			Interior = Fixture.Interior;
			Interior->ClearLoadedInterior();
			Fixture.Generator->ClearAllBuildings();
			Units = 100.0 / Fixture.Generator->GetScale();
			const TArray<FVector2D> Outline = { FVector2D(0, 0), FVector2D(60 * Units, 0), FVector2D(60 * Units, 30 * Units), FVector2D(0, 30 * Units) };
			Fixture.Generator->SpawnBuildingFromData(TEXT("CF"), TEXT("Connectors"), Outline, 3, 300.f, FVector2D(5000.0, 5000.0));
			Interior->LoadInteriorForBuilding(TEXT("CF"));
			// A lift that reaches all three floors.
			Group = Interior->PlaceConnectorNode(EInteriorNavNodeType::Elevator, FVector2D(30 * Units, 15 * Units), TArray<int32>{ 1, 2, 3 });
		}

		const FInteriorNavNode* Node(int32 Level) const
		{
			for (const FInteriorNavNode& N : Interior->GetNavNodes()) if (N.ConnectorGroupId == Group && N.Level == Level) return &N;
			return nullptr;
		}
		FVector2D At(double X, double Y) const { return FVector2D(X * Units, Y * Units); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectorFloorMoveTest, "InNavi.Interior.ConnectorFloors.Move", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConnectorFloorMoveTest::RunTest(const FString& Parameters)
{
	using namespace ConnectorFloorTests;
	FConnectorScene S;
	if (!TestFalse(TEXT("a lift is placed"), S.Group.IsEmpty())) return false;
	TestEqual(TEXT("it has a node on every floor"), S.Interior->GetConnectorPositions(S.Group).Num(), 3);

	// One floor: the node of floor 2 moves 5 m, the others stay.
	TMap<int32, FVector2D> Second;
	Second.Add(2, S.At(35, 15));
	TestTrue(TEXT("the node of floor 2 moves"), S.Interior->SetConnectorPositions(S.Group, Second));
	TestTrue(TEXT("floor 2 has it 5 m on"), S.Node(2)->Position.Equals(S.At(35, 15), 0.001));
	TestTrue(TEXT("floor 1 did not move"), S.Node(1)->Position.Equals(S.At(30, 15), 0.001));
	TestTrue(TEXT("nor floor 3"), S.Node(3)->Position.Equals(S.At(30, 15), 0.001));
	TestEqual(TEXT("it is still one connector"), S.Node(1)->ConnectorGroupId, S.Node(2)->ConnectorGroupId);
	TestTrue(TEXT("that still reaches the three floors"), S.Node(2)->ConnectsFloors == TArray<int32>({ 1, 2, 3 }));

	// A spot outside the building, or on another connector, is refused and nothing moves.
	TMap<int32, FVector2D> Outside;
	Outside.Add(2, S.At(-5, 15));
	TestFalse(TEXT("a spot outside the outline is refused"), S.Interior->SetConnectorPositions(S.Group, Outside));
	TestTrue(TEXT("and floor 2 stays"), S.Node(2)->Position.Equals(S.At(35, 15), 0.001));
	S.Interior->SetActiveFloor(2);
	const FString Other = S.Interior->PlaceConnectorNode(EInteriorNavNodeType::Elevator, S.At(50, 15), TArray<int32>{ 2 });
	TestFalse(TEXT("a second lift on floor 2"), Other.IsEmpty());
	TMap<int32, FVector2D> OnOther;
	OnOther.Add(2, S.At(50, 15));
	TestFalse(TEXT("moving onto it is refused"), S.Interior->SetConnectorPositions(S.Group, OnOther));

	// Every floor together, by the same step from where each is now: all or nothing.
	TMap<int32, FVector2D> All = S.Interior->GetConnectorPositions(S.Group);
	for (TPair<int32, FVector2D>& Pair : All) Pair.Value += S.At(0, 4);
	TestTrue(TEXT("all three move 4 m"), S.Interior->SetConnectorPositions(S.Group, All));
	TestTrue(TEXT("floor 1"), S.Node(1)->Position.Equals(S.At(30, 19), 0.001));
	TestTrue(TEXT("floor 2 keeps its 5 m"), S.Node(2)->Position.Equals(S.At(35, 19), 0.001));
	TestTrue(TEXT("floor 3"), S.Node(3)->Position.Equals(S.At(30, 19), 0.001));
	for (TPair<int32, FVector2D>& Pair : All) Pair.Value += S.At(0, 40);
	TestFalse(TEXT("a step that takes one floor out of the building moves none"), S.Interior->SetConnectorPositions(S.Group, All));
	TestTrue(TEXT("floor 1 stayed"), S.Node(1)->Position.Equals(S.At(30, 19), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectorFloorRotateScaleTest, "InNavi.Interior.ConnectorFloors.RotateAndScale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConnectorFloorRotateScaleTest::RunTest(const FString& Parameters)
{
	using namespace ConnectorFloorTests;
	FConnectorScene S;
	if (!TestFalse(TEXT("a lift is placed"), S.Group.IsEmpty())) return false;

	// Rotate floor 2 alone, then every floor from where each is.
	TestTrue(TEXT("floor 2 turns 90 degrees"), S.Interior->RotateConnectorGroup(S.Group, 90.f, 2));
	TestTrue(TEXT("floor 2 is turned"), FMath::IsNearlyEqual(S.Node(2)->RotationDegrees, 90.f));
	TestTrue(TEXT("floor 1 is not"), FMath::IsNearlyEqual(S.Node(1)->RotationDegrees, 0.f));
	TestTrue(TEXT("nor floor 3"), FMath::IsNearlyEqual(S.Node(3)->RotationDegrees, 0.f));
	TestTrue(TEXT("every floor turns 45 degrees"), S.Interior->RotateConnectorGroup(S.Group, 45.f));
	TestTrue(TEXT("floor 1 is at 45"), FMath::IsNearlyEqual(S.Node(1)->RotationDegrees, 45.f));
	TestTrue(TEXT("floor 2 is at 135: it turned from where it was"), FMath::IsNearlyEqual(S.Node(2)->RotationDegrees, 135.f));

	// Resize floor 3 alone, then every floor.
	const float Step = 0.25f;
	const float Before = S.Node(1)->SizeScale;
	TestTrue(TEXT("floor 3 grows"), S.Interior->ScaleConnectorGroup(S.Group, Step, 3));
	TestTrue(TEXT("floor 3 is bigger"), FMath::IsNearlyEqual(S.Node(3)->SizeScale, Before + Step));
	TestTrue(TEXT("floor 1 is not"), FMath::IsNearlyEqual(S.Node(1)->SizeScale, Before));
	TestTrue(TEXT("every floor grows"), S.Interior->ScaleConnectorGroup(S.Group, Step));
	TestTrue(TEXT("floor 1 grew"), FMath::IsNearlyEqual(S.Node(1)->SizeScale, Before + Step));
	TestTrue(TEXT("floor 3 grew from its own size"), FMath::IsNearlyEqual(S.Node(3)->SizeScale, Before + 2 * Step));
	return true;
}

// The controller moves the selected connector as a drag does (the positions at the start plus how far the mouse went): the active floor's node, or
// every floor with Shift; one history step per drag, so Undo takes a drag back; and the positions survive saving and loading (the floors of a
// connector are told by their group, not by where they stand).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectorFloorControllerTest, "InNavi.Interior.ConnectorFloors.ControllerAndSave", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConnectorFloorControllerTest::RunTest(const FString& Parameters)
{
	using namespace ConnectorFloorTests;
	FConnectorScene S;
	if (!TestFalse(TEXT("a lift is placed"), S.Group.IsEmpty())) return false;
	ANavPlayerController* Controller = S.Fixture.MakeController();
	// The controller opens the building from the project's saved interior.
	S.Fixture.Generator->SetInteriorDataForBuilding(TEXT("CF"), S.Interior->CaptureInteriorSnapshot());
	Controller->LoadInteriorForBuilding(TEXT("CF"));
	S.Group = S.Interior->GetNavNodes().IsEmpty() ? FString() : S.Interior->GetNavNodes()[0].ConnectorGroupId;
	if (!TestFalse(TEXT("the lift is there after opening the building through the controller"), S.Group.IsEmpty())) return false;
	Controller->SelectConnector(S.Group);
	Controller->SetActiveFloor(2);

	// A drag without Shift: the positions are those of the active floor's node alone.
	const FVector2D Start1 = S.Node(1)->Position;
	const FVector2D Start2 = S.Node(2)->Position;
	TMap<int32, FVector2D> Grabbed = Controller->GetSelectedConnectorPositions(false);
	TestEqual(TEXT("one floor is grabbed"), Grabbed.Num(), 1);
	TestTrue(TEXT("the one on floor 2"), Grabbed.Contains(2));
	for (TPair<int32, FVector2D>& Pair : Grabbed) Pair.Value += S.At(3, 2);
	TestTrue(TEXT("the drag moves it"), Controller->MoveSelectedConnectorTo(Grabbed));
	TestTrue(TEXT("floor 2 moved 3 m and 2 m"), S.Node(2)->Position.Equals(Start2 + S.At(3, 2), 0.001));
	TestTrue(TEXT("floor 1 did not"), S.Node(1)->Position.Equals(Start1, 0.001));
	TestTrue(TEXT("the project is marked as changed"), Controller->HasUnsavedChanges());
	S.Interior->RecordHistorySnapshot();

	// With Shift: every floor is grabbed, and each moves from where it is.
	TMap<int32, FVector2D> GrabbedAll = Controller->GetSelectedConnectorPositions(true);
	TestEqual(TEXT("three floors are grabbed"), GrabbedAll.Num(), 3);
	const FVector2D BeforeShift2 = S.Node(2)->Position;
	for (TPair<int32, FVector2D>& Pair : GrabbedAll) Pair.Value += S.At(-4, 0);
	TestTrue(TEXT("the drag moves them all"), Controller->MoveSelectedConnectorTo(GrabbedAll));
	TestTrue(TEXT("floor 1 moved 4 m"), S.Node(1)->Position.Equals(Start1 + S.At(-4, 0), 0.001));
	TestTrue(TEXT("floor 2 moved 4 m from its own place"), S.Node(2)->Position.Equals(BeforeShift2 + S.At(-4, 0), 0.001));
	S.Interior->RecordHistorySnapshot();

	// A drop where it cannot go moves nothing.
	TMap<int32, FVector2D> Outside = Controller->GetSelectedConnectorPositions(false);
	for (TPair<int32, FVector2D>& Pair : Outside) Pair.Value = S.At(-20, 15);
	TestFalse(TEXT("outside the building is refused"), Controller->MoveSelectedConnectorTo(Outside));

	// Undo takes the last drag back.
	const FVector2D AfterShift1 = S.Node(1)->Position;
	TestTrue(TEXT("Undo works"), S.Interior->Undo());
	TestTrue(TEXT("floor 1 is back where it was before the Shift drag"), S.Node(1)->Position.Equals(Start1, 0.001));
	TestFalse(TEXT("which is not where it ended"), S.Node(1)->Position.Equals(AfterShift1, 0.001));
	TestTrue(TEXT("floor 2 keeps the first drag"), S.Node(2)->Position.Equals(Start2 + S.At(3, 2), 0.001));

	// Saved and opened again: still one connector on three floors, each where it stood.
	const FVector2D Saved1 = S.Node(1)->Position;
	const FVector2D Saved2 = S.Node(2)->Position;
	TestFalse(TEXT("floor 1 and floor 2 are not in the same place"), Saved1.Equals(Saved2, 0.001));
	S.Fixture.Generator->SetInteriorDataForBuilding(TEXT("CF"), S.Interior->CaptureInteriorSnapshot());
	S.Interior->LoadInteriorForBuilding(TEXT("CF"));
	TestEqual(TEXT("three nodes"), S.Interior->GetNavNodes().Num(), 3);
	TSet<FString> Groups;
	for (const FInteriorNavNode& N : S.Interior->GetNavNodes()) Groups.Add(N.ConnectorGroupId);
	TestEqual(TEXT("in one group"), Groups.Num(), 1);
	S.Group = Groups.Num() == 1 ? *Groups.CreateConstIterator() : FString();
	if (S.Node(1) && S.Node(2))
	{
		TestTrue(TEXT("floor 1 where it was"), S.Node(1)->Position.Equals(Saved1, 0.001));
		TestTrue(TEXT("floor 2 where it was"), S.Node(2)->Position.Equals(Saved2, 0.001));
	}
	return true;
}

// PAT-199: the connector panel has a "Select all floors" button. One click ticks every floor box the building uses (a basement included, boxes
// beyond the building's floors stay hidden and unticked); the floors only change when Save changes is pressed, and floors can be unticked first.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectorSelectAllFloorsTest, "InNavi.Interior.ConnectorFloors.SelectAllFloors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConnectorSelectAllFloorsTest::RunTest(const FString& Parameters)
{
	using namespace ConnectorFloorTests;
	FConnectorScene S;
	if (!TestFalse(TEXT("a lift is placed"), S.Group.IsEmpty())) return false;
	// The lift reaches floors 1 and 2 only; the building also gets a basement, so it has B1, 1, 2, 3.
	TestTrue(TEXT("the lift is cut down to two floors"), S.Interior->SetConnectorFloors(S.Group, { 1, 2 }));
	ANavPlayerController* Controller = S.Fixture.MakeController();
	S.Fixture.Generator->SetInteriorDataForBuilding(TEXT("CF"), S.Interior->CaptureInteriorSnapshot());
	Controller->LoadInteriorForBuilding(TEXT("CF"));
	if (!TestNotEqual(TEXT("a basement is added"), S.Interior->AddFloorBelow(), 0)) return false;
	S.Group = S.Interior->GetNavNodes().IsEmpty() ? FString() : S.Interior->GetNavNodes()[0].ConnectorGroupId;
	if (!TestFalse(TEXT("the lift is there"), S.Group.IsEmpty())) return false;

	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(Controller);
	FScriptDelegate Selected; Selected.BindUFunction(Screen, TEXT("HandleSelectedConnectorChanged")); Controller->OnSelectedConnectorChanged.AddUnique(Selected);
	Controller->SelectConnector(S.Group);

	auto Box = [Screen](int32 Index) { return Cast<UCheckBox>(Screen->FindScreenWidget(*FString::Printf(TEXT("Chk_Floor%d"), Index))); };
	auto* SelectAll = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_SelectAllFloors")));
	auto* Save = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_ApplyConnector")));
	if (!TestNotNull(TEXT("select all button"), SelectAll) || !TestNotNull(TEXT("save button"), Save)) return false;
	for (int32 Index = 1; Index <= 5; ++Index) if (!TestNotNull(TEXT("floor box"), Box(Index))) return false;

	// Only the building's four floors are listed: the fifth entry (its box and its number) is hidden, the fourth is shown.
	TestEqual(TEXT("the fifth entry is hidden with its number"), Box(5)->GetParent()->GetVisibility(), ESlateVisibility::Collapsed);
	TestNotEqual(TEXT("the fourth entry is shown"), Box(4)->GetParent()->GetVisibility(), ESlateVisibility::Collapsed);

	// Boxes are in building order: B1, 1, 2, 3.
	TestFalse(TEXT("B1 is not reached yet"), Box(1)->IsChecked());
	TestTrue(TEXT("floor 1 is reached"), Box(2)->IsChecked());
	TestTrue(TEXT("floor 2 is reached"), Box(3)->IsChecked());
	TestFalse(TEXT("floor 3 is not reached yet"), Box(4)->IsChecked());

	SelectAll->OnClicked.Broadcast();
	for (int32 Index = 1; Index <= 4; ++Index) TestTrue(*FString::Printf(TEXT("box %d is ticked"), Index), Box(Index)->IsChecked());
	TestFalse(TEXT("a box beyond the building stays unticked"), Box(5)->IsChecked());
	TestFalse(TEXT("and stays hidden"), Box(5)->GetVisibility() != ESlateVisibility::Collapsed);
	TestEqual(TEXT("nothing changed in the building yet"), Controller->GetSelectedConnectorFloors().Num(), 2);

	// A second click, with every floor ticked, clears them all (a toggle); a third ticks them again.
	SelectAll->OnClicked.Broadcast();
	for (int32 Index = 1; Index <= 4; ++Index) TestFalse(*FString::Printf(TEXT("box %d is cleared"), Index), Box(Index)->IsChecked());
	TestFalse(TEXT("the hidden box is still unticked"), Box(5)->IsChecked());
	SelectAll->OnClicked.Broadcast();
	for (int32 Index = 1; Index <= 4; ++Index) TestTrue(*FString::Printf(TEXT("box %d is ticked again"), Index), Box(Index)->IsChecked());

	// Unticking one floor and saving keeps just that one out.
	Box(3)->SetIsChecked(false);
	Save->OnClicked.Broadcast();
	TArray<int32> Floors = Controller->GetSelectedConnectorFloors();
	Floors.Sort();
	TestEqual(TEXT("saved floors are B1, 1 and 3"), Floors, TArray<int32>({ -1, 1, 3 }));

	// And Select all floors then Save reaches every floor.
	SelectAll->OnClicked.Broadcast();
	Save->OnClicked.Broadcast();
	Floors = Controller->GetSelectedConnectorFloors();
	Floors.Sort();
	TestEqual(TEXT("saved floors are all four"), Floors, TArray<int32>({ -1, 1, 2, 3 }));
	Controller->OnSelectedConnectorChanged.Remove(Selected);
	return true;
}

// PAT-206: when a connector cannot go somewhere, the message names the floor and what is in the way there (the blocked floor is often not the one
// on screen), for placing it, for adding floors to it and for dropping it after a drag.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectorBlockedReasonTest, "InNavi.Interior.ConnectorFloors.BlockedReason", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConnectorBlockedReasonTest::RunTest(const FString& Parameters)
{
	using namespace ConnectorFloorTests;
	// The refusals are logged as errors on purpose; this test provokes them.
	AddExpectedError(TEXT("Connector placement:"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("SetConnectorFloors:"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("PlaceConnectorNode:"), EAutomationExpectedErrorFlags::Contains, 0);
	FConnectorScene S;
	if (!TestFalse(TEXT("a lift is placed"), S.Group.IsEmpty())) return false;
	TestTrue(TEXT("the lift reaches floors 1 and 2 only"), S.Interior->SetConnectorFloors(S.Group, { 1, 2 }));

	// A room on floor 3 whose left wall runs through the lift's column.
	S.Interior->SetActiveFloor(3);
	FString Error;
	if (!TestNotNull(TEXT("a room on floor 3"), S.Interior->CreateRoomFromFootprint({ S.At(30, 5), S.At(40, 5), S.At(40, 25), S.At(30, 25) }, TEXT("R3"), TEXT("Server room"), true, false, false, Error))) return false;
	S.Interior->SetActiveFloor(1);

	// Adding floor 3 is refused, and the reason says which floor and which room.
	S.Interior->ConsumeConnectorFailure();
	TestFalse(TEXT("floor 3 cannot be added"), S.Interior->SetConnectorFloors(S.Group, { 1, 2, 3 }));
	const FString AddReason = S.Interior->ConsumeConnectorFailure();
	TestTrue(TEXT("it names floor 3"), AddReason.StartsWith(TEXT("Floor 3:")));
	TestTrue(TEXT("and the room whose wall is in the way"), AddReason.Contains(TEXT("Server room")));
	TestTrue(TEXT("the reason is used up once read"), S.Interior->ConsumeConnectorFailure().IsEmpty());

	// A new lift that reaches every floor fails on floor 3 as well, though floors 1 and 2 are clear at that spot.
	TestTrue(TEXT("a new lift cannot be placed through the wall"), S.Interior->PlaceConnectorNode(EInteriorNavNodeType::Elevator, S.At(30, 8), TArray<int32>{ 1, 2, 3 }).IsEmpty());
	const FString PlaceReason = S.Interior->ConsumeConnectorFailure();
	TestTrue(TEXT("placing names floor 3 too"), PlaceReason.StartsWith(TEXT("Floor 3:")) && PlaceReason.Contains(TEXT("Server room")));

	// A yes leaves no reason behind.
	TestTrue(TEXT("a lift on floors 1 and 2 can be placed there"), !S.Interior->PlaceConnectorNode(EInteriorNavNodeType::Elevator, S.At(30, 8), TArray<int32>{ 1, 2 }).IsEmpty());
	TestTrue(TEXT("with no reason"), S.Interior->ConsumeConnectorFailure().IsEmpty());

	// What the user sees: the message of the controller, in the debug panel.
	ANavPlayerController* Controller = S.Fixture.MakeController();
	S.Fixture.Generator->SetInteriorDataForBuilding(TEXT("CF"), S.Interior->CaptureInteriorSnapshot());
	Controller->LoadInteriorForBuilding(TEXT("CF"));
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(Controller);
	UDebugPanelWidget* Panel = Screen->GetDebugPanelForTests();
	if (!TestNotNull(TEXT("debug panel"), Panel)) return false;
	Panel->BindToController();
	FString Group;
	for (const FInteriorNavNode& Node : S.Interior->GetNavNodes()) if (Node.Level == 1 && Node.Position.Equals(S.At(30, 15), 0.001)) Group = Node.ConnectorGroupId;
	if (!TestFalse(TEXT("the first lift is back after opening the building"), Group.IsEmpty())) return false;
	Controller->SelectConnector(Group);
	TestFalse(TEXT("the controller cannot add floor 3"), Controller->SetSelectedConnectorFloors({ 1, 2, 3 }));
	TestTrue(TEXT("the message names floor 3"), Panel->GetLastErrorTextForTests().Contains(TEXT("Floor 3:")));
	TestTrue(TEXT("and the room"), Panel->GetLastErrorTextForTests().Contains(TEXT("Server room")));

	// Dropping a dragged connector outside the building: refused, and the report says it stays and why.
	Controller->SetActiveFloor(1);
	TMap<int32, FVector2D> Outside = Controller->GetSelectedConnectorPositions(false);
	for (TPair<int32, FVector2D>& Pair : Outside) Pair.Value = S.At(-20, 15);
	TestFalse(TEXT("outside the building is refused"), Controller->MoveSelectedConnectorTo(Outside));
	Controller->ReportConnectorMoveRefused();
	const FString DropText = Panel->GetLastErrorTextForTests();
	TestTrue(TEXT("the report says it stays where it was"), DropText.Contains(TEXT("stays where it was")));
	TestTrue(TEXT("and names the floor and the outer wall"), DropText.Contains(TEXT("Floor 1:")) && DropText.Contains(TEXT("outer wall")));
	return true;
}

// PAT-204: a connector placed inside a closed room with no door is placed all the same, with a warning that nobody can reach it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectorEnclosedWarningTest, "InNavi.Interior.ConnectorFloors.EnclosedWarning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConnectorEnclosedWarningTest::RunTest(const FString& Parameters)
{
	using namespace ConnectorFloorTests;
	FConnectorScene S;
	FString Error;
	AInteriorRoom* Vault = S.Interior->CreateRoomFromFootprint({ S.At(10, 5), S.At(20, 5), S.At(20, 15), S.At(10, 15) }, TEXT("V1"), TEXT("Vault"), true, false, false, Error);
	if (!TestNotNull(TEXT("a room without a door"), Vault)) return false;
	TestTrue(TEXT("a spot in it is enclosed"), S.Interior->FindEnclosingRoomWithoutDoor(S.At(15, 10), 1) == Vault);
	TestNull(TEXT("a spot outside it is not"), S.Interior->FindEnclosingRoomWithoutDoor(S.At(40, 10), 1));
	TestNull(TEXT("the same spot on another floor is not"), S.Interior->FindEnclosingRoomWithoutDoor(S.At(15, 10), 2));

	// The user's side: placing a lift in it still works, and says why nobody can get to it.
	ANavPlayerController* Controller = S.Fixture.MakeController();
	S.Fixture.Generator->SetInteriorDataForBuilding(TEXT("CF"), S.Interior->CaptureInteriorSnapshot());
	Controller->LoadInteriorForBuilding(TEXT("CF"));
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(Controller);
	UDebugPanelWidget* Panel = Screen->GetDebugPanelForTests();
	if (!TestNotNull(TEXT("debug panel"), Panel)) return false;
	Panel->BindToController();
	Controller->ToggleElevatorMode();
	TestFalse(TEXT("the lift is placed"), Controller->PlaceConnectorAt(S.At(15, 10)).IsEmpty());
	TestTrue(TEXT("and the message names the room"), Panel->GetLastErrorTextForTests().Contains(TEXT("Vault")));
	TestTrue(TEXT("and says it has no door"), Panel->GetLastErrorTextForTests().Contains(TEXT("no door")));

	// With a door the room is open and the lift needs no warning.
	S.Interior->AddDoorToRoom(TEXT("V1"), 0, 100.f);
	TestNull(TEXT("a room with a door is not enclosed"), S.Interior->FindEnclosingRoomWithoutDoor(S.At(15, 10), 1));
	return true;
}

// A route takes a lift whose floors do not stand in one column, and the validator no longer calls that inconsistent.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectorFloorRouteTest, "InNavi.Interior.ConnectorFloors.RouteAndValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConnectorFloorRouteTest::RunTest(const FString& Parameters)
{
	using namespace ConnectorFloorTests;
	FConnectorScene S;
	if (!TestFalse(TEXT("a lift is placed"), S.Group.IsEmpty())) return false;
	TMap<int32, FVector2D> Moved;
	Moved.Add(2, S.At(40, 15));
	Moved.Add(3, S.At(20, 10));
	TestTrue(TEXT("floors 2 and 3 are moved away from floor 1"), S.Interior->SetConnectorPositions(S.Group, Moved));
	S.Fixture.Generator->SetInteriorDataForBuilding(TEXT("CF"), S.Interior->CaptureInteriorSnapshot());

	FNavigationRoutePoint From, To;
	From.BuildingId = TEXT("CF"); From.Floor = 1; From.Position = S.At(5, 5);
	To.BuildingId = TEXT("CF"); To.Floor = 3; To.Position = S.At(55, 25);
	FNavigationRouteResult Route;
	if (TestTrue(TEXT("a route from floor 1 to floor 3 over the lift"), FNavigationRoutePlanner::FindCampusRoute(*S.Fixture.Generator, *S.Interior, From, To, Route)))
	{
		TestEqual(TEXT("it ends on floor 3"), Route.Points.Last().Floor, 3);
		// The lift is entered where it stands on floor 1 and left where it stands on floor 3: two different places.
		const FVector2D Floor1Lift = S.At(30, 15), Floor3Lift = S.At(20, 10);
		TestTrue(TEXT("it enters the lift on floor 1"), Route.Points.ContainsByPredicate([&](const FNavigationRoutePoint& P) { return P.Floor == 1 && P.ConnectorType == EInteriorNavNodeType::Elevator && P.Position.Equals(Floor1Lift, 0.01); }));
		TestTrue(TEXT("and leaves it on floor 3, where that floor's node is"), Route.Points.ContainsByPredicate([&](const FNavigationRoutePoint& P) { return P.Floor == 3 && P.ConnectorType == EInteriorNavNodeType::Elevator && P.Position.Equals(Floor3Lift, 0.01); }));
	}

	const FString Path = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("connector-floors-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
	if (TestTrue(TEXT("the project is saved"), S.Fixture.Generator->SaveBuildingsToFile(Path)))
	{
		const LayoutValidation::FReport Report = LayoutValidation::ValidateFile(Path);
		const bool bInconsistent = Report.Issues.ContainsByPredicate([](const LayoutValidation::FIssue& I) { return I.Rule == TEXT("CONNECTOR_GROUP_INCONSISTENT"); });
		TestFalse(TEXT("floors of a lift in different places are not an inconsistency"), bInconsistent);
	}
	IFileManager::Get().Delete(*Path);
	return true;
}

#endif
