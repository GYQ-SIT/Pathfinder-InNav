#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "DebugPanelWidget.h"
#include "DuplicateToFloorsDialogWidget.h"
#include "InteriorFloorPlanWidget.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "InteriorScreenWidget.h"
#include "NavPlayerController.h"
#include "RoomContextMenuWidget.h"
#include "UiTestFixture.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Input/Events.h"
#include "Misc/AutomationTest.h"

// PAT-200: duplicating rooms onto other floors, and the menu and dialog that do it.

struct FDuplicateTestAccess
{
	static void Bind(UInteriorFloorPlanWidget* Plan, FNavigationSearchFixture& F, ANavPlayerController* PC)
	{
		Plan->InteriorGen = F.Interior; Plan->Generator = F.Generator; Plan->PlayerController = PC;
	}
	static FReply Down(UInteriorFloorPlanWidget* W, const FGeometry& G, const FPointerEvent& E) { return W->NativeOnMouseButtonDown(G, E); }
	static FReply Up(UInteriorFloorPlanWidget* W, const FGeometry& G, const FPointerEvent& E) { return W->NativeOnMouseButtonUp(G, E); }
	static TArray<FVector2D> Vertices(UInteriorFloorPlanWidget* W, FVector2D Size) { return W->GetVertexHandles(Size); }
};

namespace DuplicateToFloorsTests
{
	void Prepare(FNavigationSearchFixture& F)
	{
		F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
		F.Generator->CreateRectangleBuilding(TEXT("DUP"), TEXT("DUP"), {500,500}, 1000, 1000, 0, 3, 300);
		F.Interior->LoadInteriorForBuilding(TEXT("DUP"));
	}
	TArray<AInteriorRoom*> RoomsOn(FNavigationSearchFixture& F, int32 Level) { return F.Interior->GetRoomsOnFloor(Level); }
	FPointerEvent RightMouse(FVector2D Position, bool bHeld)
	{
		TSet<FKey> Held;
		if (bHeld) Held.Add(EKeys::RightMouseButton);
		return FPointerEvent(0, Position, Position, Held, EKeys::RightMouseButton, 0.f, FModifierKeysState());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuplicateToFloorsTest, "InNavi.Interior.DuplicateToFloors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDuplicateToFloorsTest::RunTest(const FString& Parameters)
{
	using namespace DuplicateToFloorsTests;
	AddExpectedError(TEXT("overlaps existing"), EAutomationExpectedErrorFlags::Contains, 0);
	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	FString Error;
	AInteriorRoom* Lobby = F.Interior->CreateRoomFromFootprint({{450,450},{550,450},{550,550},{450,550}}, TEXT("L1"), TEXT("Lift lobby"), true, false, false, Error);
	if (!TestNotNull(TEXT("a lift lobby"), Lobby)) return false;
	F.Interior->SetRoomType(TEXT("L1"), TEXT("Lift lobby")); F.Interior->SetRoomTags(TEXT("L1"), { TEXT("lobby") });
	TestFalse(TEXT("it has a door"), F.Interior->AddDoorToRoom(TEXT("L1"), 0, 100.f).IsEmpty());
	PC->ToggleSelectMode(); PC->SelectRoom(TEXT("L1"));
	UDebugPanelWidget* Debug = nullptr;
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(PC);
	Debug = Screen->GetDebugPanelForTests(); if (Debug) Debug->BindToController();

	// Onto floors 2 and 3: same place and shape, type, tags and door; names made unique; one Undo step; the floor you are on stays.
	TestEqual(TEXT("two copies are made"), PC->DuplicateSelectionToFloors({ 2, 3 }), 2);
	TestEqual(TEXT("the active floor is still 1"), F.Interior->GetActiveFloor(), 1);
	TestEqual(TEXT("the original stays alone on floor 1"), RoomsOn(F, 1).Num(), 1);
	TArray<FString> Names;
	for (const int32 Level : { 2, 3 })
	{
		const TArray<AInteriorRoom*> Rooms = RoomsOn(F, Level);
		if (!TestEqual(TEXT("one room on the floor"), Rooms.Num(), 1)) return false;
		const AInteriorRoom* Copy = Rooms[0];
		TestTrue(TEXT("same outline"), Copy->GetFootprint().Num() == 4 && Copy->GetFootprint()[0].Equals({450,450}, 1e-6) && Copy->GetFootprint()[2].Equals({550,550}, 1e-6));
		TestEqual(TEXT("same type"), Copy->RoomType, FString(TEXT("Lift lobby")));
		TestEqual(TEXT("same tags"), Copy->RoomTags, TArray<FString>({ TEXT("lobby") }));
		TestEqual(TEXT("same door"), Copy->GetOpenings().Num(), 1);
		TestTrue(TEXT("its own id"), Copy->RoomId != TEXT("L1"));
		TestTrue(TEXT("named after the original"), Copy->RoomName.StartsWith(TEXT("Lift lobby")));
		Names.Add(Copy->RoomName);
	}
	TestTrue(TEXT("the names differ from each other and from the original"), Names[0] != Names[1] && !Names.Contains(TEXT("Lift lobby")));
	TestTrue(TEXT("one Undo takes both copies back"), F.Interior->Undo() && RoomsOn(F, 2).IsEmpty() && RoomsOn(F, 3).IsEmpty() && RoomsOn(F, 1).Num() == 1);

	// A floor with something in the way is skipped, the rest are copied, and the message says which floor.
	F.Interior->SetActiveFloor(2);
	F.Interior->CreateRoomFromFootprint({{500,500},{600,500},{600,600},{500,600}}, TEXT("Block"), TEXT("Block"), true, false, false, Error);
	F.Interior->SetActiveFloor(1);
	PC->SelectRoom(TEXT("L1"));
	TestEqual(TEXT("one of two copies fits"), PC->DuplicateSelectionToFloors({ 2, 3 }), 1);
	TestEqual(TEXT("floor 3 got its copy"), RoomsOn(F, 3).Num(), 1);
	TestEqual(TEXT("floor 2 only has the room that was there"), RoomsOn(F, 2).Num(), 1);
	if (Debug) TestTrue(TEXT("the message names floor 2"), Debug->GetLastErrorTextForTests().Contains(TEXT("floor 2")));
	F.Interior->Undo();

	// The floor the room is on is refused, and so is one the building does not have.
	TestNull(TEXT("not onto its own floor"), F.Interior->DuplicateRoomToFloor(TEXT("L1"), 1, Error));
	TestTrue(TEXT("and it says why"), Error.Contains(TEXT("floor the room is on")));
	TestNull(TEXT("not onto a floor that is not there"), F.Interior->DuplicateRoomToFloor(TEXT("L1"), 9, Error));

	// Several rooms at once, and a wall.
	F.Interior->DeleteRoom(TEXT("Block"));
	F.Interior->CreateRoomFromFootprint({{200,200},{260,200},{260,260},{200,260}}, TEXT("R2"), TEXT("Office"), true, false, false, Error);
	F.Interior->CreateWallFromPoints({{700,100},{700,300}}, TEXT("W1"), Error);
	PC->SetRoomSelection({ TEXT("L1"), TEXT("R2"), TEXT("W1") });
	TestEqual(TEXT("three rooms onto one floor"), PC->DuplicateSelectionToFloors({ 2 }), 3);
	TestEqual(TEXT("three on floor 2"), RoomsOn(F, 2).Num(), 3);
	TestTrue(TEXT("the selection is still the three"), PC->IsMultiSelection() && PC->GetMultiSelectedRoomIds().Num() == 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuplicateDialogTest, "InNavi.UI.DuplicateDialog", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDuplicateDialogTest::RunTest(const FString& Parameters)
{
	using namespace DuplicateToFloorsTests;
	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	FString Error;
	if (!TestNotNull(TEXT("a room"), F.Interior->CreateRoomFromFootprint({{450,450},{550,450},{550,550},{450,550}}, TEXT("L1"), TEXT("Lobby"), true, false, false, Error))) return false;
	PC->SelectRoom(TEXT("L1"));

	UDuplicateToFloorsDialogWidget* Dialog = NewObject<UDuplicateToFloorsDialogWidget>();
	Dialog->Setup({ 1, 2, 3 }, { 1 }, TEXT("Lobby"));
	Dialog->Initialize(); Dialog->SetOwningPlayer(PC); Dialog->TakeWidget();
	auto Box = [Dialog](int32 Level) { return Cast<UCheckBox>(Dialog->GetWidgetFromName(*FString::Printf(TEXT("Chk_Floor_%d"), Level))); };
	auto* Duplicate = Cast<UButton>(Dialog->GetWidgetFromName(TEXT("Btn_Duplicate")));
	auto* Cancel = Cast<UButton>(Dialog->GetWidgetFromName(TEXT("Btn_Cancel")));
	if (!TestNotNull(TEXT("floor 1"), Box(1)) || !TestNotNull(TEXT("floor 2"), Box(2)) || !TestNotNull(TEXT("floor 3"), Box(3)) || !TestNotNull(TEXT("Duplicate"), Duplicate) || !TestNotNull(TEXT("Cancel"), Cancel)) return false;

	// Every floor but the room's own is ticked; its own is shown, off and unticked.
	TestFalse(TEXT("the room's own floor cannot be ticked"), Box(1)->GetIsEnabled());
	TestFalse(TEXT("and is not ticked"), Box(1)->IsChecked());
	TestTrue(TEXT("the other floors are ticked"), Box(2)->IsChecked() && Box(3)->IsChecked());
	TestEqual(TEXT("so floors 2 and 3 are what would be copied to"), Dialog->GetTickedLevels(), TArray<int32>({ 2, 3 }));
	TestTrue(TEXT("Duplicate is on"), Duplicate->GetIsEnabled());

	// Unticking floors changes what is copied; with none ticked Duplicate is off.
	Box(3)->SetIsChecked(false); Box(3)->OnCheckStateChanged.Broadcast(false);
	TestEqual(TEXT("floor 3 unticked"), Dialog->GetTickedLevels(), TArray<int32>({ 2 }));
	Box(2)->SetIsChecked(false); Box(2)->OnCheckStateChanged.Broadcast(false);
	TestFalse(TEXT("with no floor ticked Duplicate is off"), Duplicate->GetIsEnabled());
	Box(2)->SetIsChecked(true); Box(2)->OnCheckStateChanged.Broadcast(true);
	TestTrue(TEXT("and on again"), Duplicate->GetIsEnabled());

	// Duplicate hands the ticked floors to the controller, which copies the selected room onto them.
	TArray<int32> Chosen;
	Dialog->OnConfirmed.AddLambda([&Chosen, PC](const TArray<int32>& Levels) { Chosen = Levels; PC->DuplicateSelectionToFloors(Levels); });
	TestTrue(TEXT("Duplicate is wired up"), Duplicate->OnClicked.IsBound());
	Duplicate->OnClicked.Broadcast();
	TestEqual(TEXT("the dialog hands over the ticked floors"), Chosen, TArray<int32>({ 2 }));
	TestEqual(TEXT("the copy is on floor 2"), RoomsOn(F, 2).Num(), 1);
	TestEqual(TEXT("and not on floor 3, which was unticked"), RoomsOn(F, 3).Num(), 0);

	// The menu has its one row.
	URoomContextMenuWidget* Menu = NewObject<URoomContextMenuWidget>();
	Menu->SetMenuPosition({ 100, 100 });
	Menu->Initialize(); Menu->SetOwningPlayer(PC); Menu->TakeWidget();
	TestNotNull(TEXT("the menu's row"), Cast<UButton>(Menu->GetWidgetFromName(TEXT("Btn_DuplicateToFloors"))));
	TestNotNull(TEXT("and the layer that closes it"), Cast<UButton>(Menu->GetWidgetFromName(TEXT("Btn_MenuCatcher"))));
	return true;
}

// A right-click on a room (the pointer does not move) selects it and opens the menu; a right-drag still pans, on a room or not.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuplicateRightClickTest, "InNavi.EditorUX.RightClickMenu", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDuplicateRightClickTest::RunTest(const FString& Parameters)
{
	using namespace DuplicateToFloorsTests;
	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	FString Error;
	if (!TestNotNull(TEXT("a room"), F.Interior->CreateRoomFromFootprint({{450,450},{550,450},{550,550},{450,550}}, TEXT("L1"), TEXT("Lobby"), true, false, false, Error))) return false;
	if (!TestNotNull(TEXT("another"), F.Interior->CreateRoomFromFootprint({{650,450},{750,450},{750,550},{650,550}}, TEXT("L2"), TEXT("Other"), true, false, false, Error))) return false;
	PC->ToggleSelectMode();
	UInteriorFloorPlanWidget* W = NewObject<UInteriorFloorPlanWidget>(); W->Initialize(); W->SetOwningPlayer(PC); W->TakeWidget();
	FDuplicateTestAccess::Bind(W, F, PC);
	const FGeometry G = FGeometry::MakeRoot(FVector2f(1000,1000), FSlateLayoutTransform());
	auto Centre = [&](const FString& Id) { PC->SelectRoom(Id); const FBox2D B(FDuplicateTestAccess::Vertices(W, G.GetLocalSize())); PC->SelectRoom(FString()); return B.GetCenter(); };
	const FVector2D L1 = Centre(TEXT("L1")), L2 = Centre(TEXT("L2"));

	FDuplicateTestAccess::Down(W, G, RightMouse(L1, true));
	FDuplicateTestAccess::Up(W, G, RightMouse(L1, false));
	TestEqual(TEXT("a right-click selects the room"), PC->GetSelectedRoomId(), FString(TEXT("L1")));

	// A drag with the right button is a pan: nothing is selected by it.
	PC->SelectRoom(FString());
	FDuplicateTestAccess::Down(W, G, RightMouse(L2, true));
	FDuplicateTestAccess::Up(W, G, RightMouse(L2 + FVector2D(80, 60), false));
	TestTrue(TEXT("a right-drag selects nothing"), PC->GetSelectedRoomId().IsEmpty());

	// Right-click on a room that is part of a multiple selection keeps the selection.
	PC->SetRoomSelection({ TEXT("L1"), TEXT("L2") });
	FDuplicateTestAccess::Down(W, G, RightMouse(L2, true));
	FDuplicateTestAccess::Up(W, G, RightMouse(L2, false));
	TestTrue(TEXT("the multiple selection is kept"), PC->IsMultiSelection() && PC->GetMultiSelectedRoomIds().Num() == 2);
	return true;
}

#endif
