#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "InteriorDrawing.h"
#include "NavigationSearchUtils.h"
#include "Components/ComboBoxString.h"
#include "InteriorFloorPlanWidget.h"
#include "InteriorScreenWidget.h"
#include "InteriorRoom.h"
#include "DebugPanelWidget.h"
#include "NavCameraPawn.h"
#include "NavGameMode.h"
#include "NavigationDockPanelWidget.h"
#include "PolyBuilding.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Input/Events.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"

struct FEditorInteractionTestAccess
{
	static void SetupInput(ANavPlayerController* PC) { PC->SetupInputComponent(); }
	static void Bind(UInteriorFloorPlanWidget* Plan, FNavigationSearchFixture& F, ANavPlayerController* PC)
	{
		Plan->InteriorGen = F.Interior; Plan->Generator = F.Generator; Plan->PlayerController = PC;
	}
	static FReply Down(UInteriorFloorPlanWidget* W, const FGeometry& G, const FPointerEvent& E) { return W->NativeOnMouseButtonDown(G,E); }
	static FReply Move(UInteriorFloorPlanWidget* W, const FGeometry& G, const FPointerEvent& E) { return W->NativeOnMouseMove(G,E); }
	static FReply Up(UInteriorFloorPlanWidget* W, const FGeometry& G, const FPointerEvent& E) { return W->NativeOnMouseButtonUp(G,E); }
	static TArray<FVector2D> Handles(UInteriorFloorPlanWidget* W, FVector2D Size) { return W->GetResizeHandles(Size); }
	static TArray<FVector2D> Preview(UInteriorFloorPlanWidget* W) { return W->GetPrefabPreview(); }
	static void ResizePreview(const UInteriorFloorPlanWidget* W, FVector2D& Position, FVector2D& Size) { Position = W->ResizePreviewPosition; Size = W->ResizePreviewSize; }
	static TArray<FVector2D> Vertices(UInteriorFloorPlanWidget* W, FVector2D Size) { return W->GetVertexHandles(Size); }
	static FReply SearchKey(UNavigationPlaceCombo* Combo, FKey Key)
	{
		return Combo->HandleSearchKey(FGeometry(), FKeyEvent(Key,FModifierKeysState(),0,false,0,0));
	}
	static bool HasChoiceLabel(UNavigationPlaceCombo* Combo)
	{
		return Combo->ComboBoxContent.IsValid() && Combo->ComboBoxContent->GetChildren()->GetChildAt(0) != SNullWidget::NullWidget;
	}
};

namespace EditorInteractionTests
{
	void Prepare(FNavigationSearchFixture& F)
	{
		F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
		F.Generator->CreateRectangleBuilding(TEXT("UX"), TEXT("UX"), {500,500}, 1000, 1000, 0, 2, 300);
		F.Interior->LoadInteriorForBuilding(TEXT("UX"));
	}
	FPointerEvent Mouse(FVector2D Position, bool bHeld)
	{
		TSet<FKey> Held;
		if (bHeld) Held.Add(EKeys::LeftMouseButton);
		return FPointerEvent(0, Position, Position, Held, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
	}
	// The same, with Shift and/or Ctrl held.
	FPointerEvent MouseWith(FVector2D Position, bool bHeld, bool bShift, bool bCtrl)
	{
		TSet<FKey> Held;
		if (bHeld) Held.Add(EKeys::LeftMouseButton);
		return FPointerEvent(0, Position, Position, Held, EKeys::LeftMouseButton, 0.f, FModifierKeysState(bShift, false, bCtrl, false, false, false, false, false, false));
	}
	UInteriorFloorPlanWidget* Plan(FNavigationSearchFixture& F, ANavPlayerController* PC)
	{
		UInteriorFloorPlanWidget* W = NewObject<UInteriorFloorPlanWidget>();
		W->Initialize(); W->SetOwningPlayer(PC); W->TakeWidget();
		FEditorInteractionTestAccess::Bind(W,F,PC);
		return W;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorWallSelfOverlapTest, "InNavi.EditorUX.WallSelfOverlap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorWallSelfOverlapTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F; EditorInteractionTests::Prepare(F);
	FString Error;
	const TArray<TArray<FVector2D>> Bad = {
		{{100,100},{200,200},{100,200},{200,100}}, // crossing
		{{100,100},{200,100},{150,100}}, // adjacent backtracking
		{{100,100},{200,100},{50,100}}, // backtracking past the start
		{{100,100},{100,100}}, // zero-length
		{{100,100},{200,100},{200,200},{100,200},{200,100}}, // non-adjacent endpoint touch
		{{100,100},{300,100},{300,200},{150,200},{150,100},{250,100}} // collinear overlap
	};
	for (const auto& Points : Bad)
	{
		TestNull(TEXT("invalid wall is rejected"), F.Interior->CreateWallFromPoints(Points, FString(), Error));
		TestTrue(TEXT("the reason identifies self-overlap"), Error.Contains(TEXT("itself")));
		TestTrue(TEXT("rejection creates no wall"), F.Interior->GetRoomsOnFloor(1).IsEmpty());
	}
	AInteriorRoom* Straight = F.Interior->CreateWallFromPoints({{100,100},{150,100},{200,100}}, TEXT("Straight"), Error);
	TestNotNull(TEXT("collinear continuation is allowed"), Straight);
	F.Interior->DeleteRoom(TEXT("Straight"));
	AInteriorRoom* Bend = F.Interior->CreateWallFromPoints({{100,100},{200,100},{200,200}}, TEXT("Bend"), Error);
	TestNotNull(TEXT("a normal bend is allowed"), Bend);
	AddExpectedError(TEXT("wall overlaps existing"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("crossing an existing wall is still rejected"), F.Interior->CreateWallFromPoints({{150,50},{150,150}}, TEXT("Other"), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorDrawingFailureTest, "InNavi.EditorUX.DrawingFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorDrawingFailureTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F; EditorInteractionTests::Prepare(F);
	ANavPlayerController* PC = F.MakeController(); if (PC->IsSnapEnabled()) PC->ToggleSnapEnabled();
	UDebugPanelWidget* Debug = NewObject<UDebugPanelWidget>(); Debug->Initialize(); Debug->SetOwningPlayer(PC); Debug->TakeWidget(); Debug->BindToController();
	PC->ToggleDrawRoomMode();
	for (const FVector2D& Point : TArray<FVector2D>{{100,100},{200,200},{100,200},{200,100}}) PC->AddRoomPointAtFootprintPosition(Point);
	TestTrue(TEXT("room draft is cleared automatically"), PC->GetPendingRoomFootprint().IsEmpty());
	TestTrue(TEXT("debug explains the failure"), Debug->GetLastErrorTextForTests().Contains(TEXT("cross")) || Debug->GetLastErrorTextForTests().Contains(TEXT("intersect")));
	TestTrue(TEXT("room error identifies a room outline"), Debug->GetLastErrorTextForTests().Contains(TEXT("room outline")));
	PC->ToggleDrawWallMode();
	for (const FVector2D& Point : TArray<FVector2D>{{100,100},{200,100},{150,100}}) PC->AddRoomPointAtFootprintPosition(Point);
	TestTrue(TEXT("wall draft is cleared automatically"), PC->GetPendingRoomFootprint().IsEmpty());
	TestTrue(TEXT("debug explains wall self-overlap"), Debug->GetLastErrorTextForTests().Contains(TEXT("itself")));
	TestTrue(TEXT("wall error still identifies a wall"), Debug->GetLastErrorTextForTests().Contains(TEXT("wall")));
	PC->AddRoomPointAtFootprintPosition({100,100});
	PC->AddRoomPointAtFootprintPosition({-100,-100});
	TestEqual(TEXT("stray click keeps the draft"), PC->GetPendingRoomFootprint().Num(), 1);
	PC->AddRoomPointAtFootprintPosition({200,100});
	TestNotNull(TEXT("a new valid wall can be drawn immediately"), PC->FinishWall());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorPrefabDragTest, "InNavi.EditorUX.PrefabDrag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorPrefabDragTest::RunTest(const FString& Parameters)
{
	using namespace EditorInteractionTests;
	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	UInteriorFloorPlanWidget* W = Plan(F,PC);
	const FGeometry G = FGeometry::MakeRoot(FVector2f(1000,1000), FSlateLayoutTransform());
	for (int32 Shape = 1; Shape <= 3; ++Shape)
	{
		if (Shape == 1) PC->TogglePrefabRectangleMode();
		else if (Shape == 2) PC->TogglePrefabTriangleMode();
		else PC->TogglePrefabCircleMode();
		const FVector2D Start(300,300), End(450,430);
		FEditorInteractionTestAccess::Down(W,G,Mouse(Start,true));
		TestTrue(TEXT("mouse down does not create an object"), F.Interior->GetRoomsOnFloor(1).IsEmpty());
		FEditorInteractionTestAccess::Move(W,G,Mouse(End,true));
		TestFalse(TEXT("drag has a live preview"), FEditorInteractionTestAccess::Preview(W).IsEmpty());
		FEditorInteractionTestAccess::Up(W,G,Mouse(End,false));
		FVector2D Position, Size;
		TestTrue(TEXT("release creates and selects a room"), PC->GetSelectedRoomBounds(Position,Size));
		const FVector2D Expected = W->LocalPositionToFootprintPoint(End,G.GetLocalSize()) - W->LocalPositionToFootprintPoint(Start,G.GetLocalSize());
		TestTrue(TEXT("drag sets exact footprint bounds for every shape"), Size.Equals(Expected,0.01));
		F.Interior->DeleteRoom(PC->GetSelectedRoomId()); PC->SelectRoom(FString());
		const FVector2D Click(500,500);
		FEditorInteractionTestAccess::Down(W,G,Mouse(Click,true));
		FEditorInteractionTestAccess::Up(W,G,Mouse(Click,false));
		TestTrue(TEXT("click-only placement still works"), PC->GetSelectedRoomBounds(Position,Size));
		TestTrue(TEXT("click uses the shape's default size"), Size.Equals(FBox2D(PC->BuildPrefabRoomFootprint({0,0})).GetSize(),0.01));
		F.Interior->DeleteRoom(PC->GetSelectedRoomId()); PC->SelectRoom(FString());
	}
	FEditorInteractionTestAccess::Down(W,G,Mouse({300,300},true));
	PC->TogglePrefabCircleMode();
	FEditorInteractionTestAccess::Up(W,G,Mouse({450,450},false));
	TestTrue(TEXT("cancelled prefab gesture creates nothing"), F.Interior->GetRoomsOnFloor(1).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorResizeTest, "InNavi.EditorUX.Resize", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorResizeTest::RunTest(const FString& Parameters)
{
	using namespace EditorInteractionTests;
	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	FString Error;
	AInteriorRoom* Room = F.Interior->CreateRoomFromFootprint({{450,450},{550,450},{550,550},{450,550}}, TEXT("Size"), TEXT("Size"), true,false,false,Error);
	if (!TestNotNull(TEXT("room created"), Room)) return false;
	PC->ToggleSelectMode(); PC->SelectRoom(TEXT("Size")); PC->SetRoomResizeStepMetres(0.25f);
	FVector2D Position, Before, After; PC->GetSelectedRoomBounds(Position,Before);
	PC->GrowSelectedRoom(); PC->GetSelectedRoomBounds(Position,After);
	const double Step = 25.0 / PC->GetBuildingScale();
	TestTrue(TEXT("configured growth uses physical metres"), After.Equals(Before + FVector2D(Step,Step),0.01));
	PC->ShrinkSelectedRoom(); PC->GetSelectedRoomBounds(Position,After);
	TestTrue(TEXT("shrink reverses growth"), After.Equals(Before,0.01));
	UInteriorFloorPlanWidget* W = Plan(F,PC);
	const FGeometry G = FGeometry::MakeRoot(FVector2f(1000,1000), FSlateLayoutTransform());
	const TArray<FVector2D> Handles = FEditorInteractionTestAccess::Handles(W,G.GetLocalSize());
	if (!TestEqual(TEXT("eight resize handles"), Handles.Num(),8)) return false;
	FEditorInteractionTestAccess::Down(W,G,Mouse(Handles[4],true));
	FEditorInteractionTestAccess::Move(W,G,Mouse(Handles[4]+FVector2D(20,15),true));
	FEditorInteractionTestAccess::Move(W,G,Mouse(Handles[4]+FVector2D(30,25),true));
	FEditorInteractionTestAccess::Up(W,G,Mouse(Handles[4]+FVector2D(30,25),false));
	PC->GetSelectedRoomBounds(Position,After);
	TestTrue(TEXT("drag resized both axes"), After.X > Before.X && After.Y > Before.Y);
	F.Interior->Undo();
	const AInteriorRoom* Restored = F.Interior->GetRoomById(TEXT("Size"));
	TestTrue(TEXT("one undo reverses the entire drag"), Restored && FBox2D(Restored->GetFootprint()).GetSize().Equals(Before,0.01));
	F.Interior->Redo(); PC->SelectRoom(TEXT("Size"));
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(PC);
	FScriptDelegate Selected; Selected.BindUFunction(Screen,TEXT("HandleSelectedRoomChanged")); PC->OnSelectedRoomChanged.AddUnique(Selected);
	PC->SelectRoom(FString()); PC->SelectRoom(TEXT("Size"));
	auto* Width = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Txt_RoomWidth")));
	auto* LengthField = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Txt_RoomLength")));
	auto* Save = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_ApplyRoom")));
	if (!TestNotNull(TEXT("width field"),Width) || !TestNotNull(TEXT("depth field"),LengthField) || !TestNotNull(TEXT("save"),Save)) return false;
	TestFalse(TEXT("width is editable"), Width->GetIsReadOnly());
	Width->SetText(FText::FromString(TEXT("8.5"))); Width->OnTextChanged.Broadcast(Width->GetText());
	LengthField->SetText(FText::FromString(TEXT("6.25"))); LengthField->OnTextChanged.Broadcast(LengthField->GetText());
	Save->OnClicked.Broadcast(); PC->GetSelectedRoomBounds(Position,After);
	TestTrue(TEXT("typed dimensions are applied in metres"), After.Equals(FVector2D(850,625)/PC->GetBuildingScale(),0.01));
	Width->SetText(FText::FromString(TEXT("-2"))); Width->OnTextChanged.Broadcast(Width->GetText());
	Save->OnClicked.Broadcast(); PC->GetSelectedRoomBounds(Position,Before);
	TestTrue(TEXT("invalid size preserves geometry"), Before.Equals(After,0.01));
	PC->OnSelectedRoomChanged.Remove(Selected);
	return true;
}

// PAT-190: dragging a resize handle never shows the room outside the building. The preview stops at the wall, a corner handle slides along it, and
// letting go applies exactly what was shown (no "Unable to resize" after the fact).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorResizeStopsAtWallTest, "InNavi.EditorUX.ResizeStopsAtWall", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorResizeStopsAtWallTest::RunTest(const FString& Parameters)
{
	using namespace EditorInteractionTests;
	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	FString Error;
	AInteriorRoom* Room = F.Interior->CreateRoomFromFootprint({{450,450},{550,450},{550,550},{450,550}}, TEXT("Wall"), TEXT("Wall"), true,false,false,Error);
	if (!TestNotNull(TEXT("room created"), Room)) return false;
	PC->ToggleSelectMode(); PC->SelectRoom(TEXT("Wall"));

	// The check the drag relies on, against the real outline of the building.
	const APolyBuilding* Building = F.Generator->FindBuildingById(TEXT("UX"));
	if (!TestNotNull(TEXT("building"), Building)) return false;
	const FBox2D Walls(Building->GetFootprint());
	TestTrue(TEXT("a size inside the building fits"), F.Interior->DoesResizeStayInsideBuilding(TEXT("Wall"), {450,450}, {100,100}));
	TestFalse(TEXT("a size past the wall does not"), F.Interior->DoesResizeStayInsideBuilding(TEXT("Wall"), {450,450}, {Walls.GetSize().X * 2.0, 100}));
	TestFalse(TEXT("an unknown room does not"), F.Interior->DoesResizeStayInsideBuilding(TEXT("Nope"), {400,400}, {200,200}));

	FVector2D Position, Start; PC->GetSelectedRoomBounds(Position, Start);
	UInteriorFloorPlanWidget* W = Plan(F, PC);
	const FGeometry G = FGeometry::MakeRoot(FVector2f(1000,1000), FSlateLayoutTransform());
	const TArray<FVector2D> Handles = FEditorInteractionTestAccess::Handles(W, G.GetLocalSize());
	if (!TestEqual(TEXT("eight resize handles"), Handles.Num(), 8)) return false;

	// Bottom-right corner, pulled far past the building in both directions.
	FEditorInteractionTestAccess::Down(W, G, Mouse(Handles[4], true));
	FEditorInteractionTestAccess::Move(W, G, Mouse(Handles[4] + FVector2D(4000,4000), true));
	FVector2D PreviewPosition, PreviewSize;
	FEditorInteractionTestAccess::ResizePreview(W, PreviewPosition, PreviewSize);
	TestTrue(TEXT("the preview grew"), PreviewSize.X > Start.X + 1.0 && PreviewSize.Y > Start.Y + 1.0);
	TestTrue(TEXT("the preview is inside the building"), F.Interior->DoesResizeStayInsideBuilding(TEXT("Wall"), PreviewPosition, PreviewSize));
	TestTrue(TEXT("it stopped at the far wall on X"), FMath::IsNearlyEqual(PreviewPosition.X + PreviewSize.X, Walls.Max.X, Walls.GetSize().X * 0.01));
	TestTrue(TEXT("and on Y"), FMath::IsNearlyEqual(PreviewPosition.Y + PreviewSize.Y, Walls.Max.Y, Walls.GetSize().Y * 0.01));
	TestTrue(TEXT("the top-left corner did not move"), PreviewPosition.Equals(FVector2D(450,450), 0.01));

	// Pulled past the right wall only: X stops at the wall, Y follows the pointer (the corner slides along the wall).
	FEditorInteractionTestAccess::Move(W, G, Mouse(Handles[4] + FVector2D(4000,20), true));
	FEditorInteractionTestAccess::ResizePreview(W, PreviewPosition, PreviewSize);
	TestTrue(TEXT("X stays at the wall"), FMath::IsNearlyEqual(PreviewPosition.X + PreviewSize.X, Walls.Max.X, Walls.GetSize().X * 0.01));
	TestTrue(TEXT("Y is not stuck at the wall"), PreviewPosition.Y + PreviewSize.Y < Walls.Max.Y - Walls.GetSize().Y * 0.05);

	// Letting go applies the shown size.
	FEditorInteractionTestAccess::Move(W, G, Mouse(Handles[4] + FVector2D(4000,4000), true));
	FEditorInteractionTestAccess::ResizePreview(W, PreviewPosition, PreviewSize);
	FEditorInteractionTestAccess::Up(W, G, Mouse(Handles[4] + FVector2D(4000,4000), false));
	FVector2D After; PC->GetSelectedRoomBounds(Position, After);
	TestTrue(TEXT("the room has the size that was shown"), After.Equals(PreviewSize, 0.05));
	const AInteriorRoom* Resized = F.Interior->GetRoomById(TEXT("Wall"));
	TestTrue(TEXT("and is still inside the building"), Resized && F.Interior->DoesResizeStayInsideBuilding(TEXT("Wall"), Position, After));
	return true;
}

// PAT-191, drawing: Shift keeps a drawn line straight and a dragged shape square (circle) or equilateral (triangle), and Ctrl+Z while drawing takes
// back the last point instead of an older edit.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorDrawingControlsTest, "InNavi.EditorUX.DrawingControls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorDrawingControlsTest::RunTest(const FString& Parameters)
{
	using namespace EditorInteractionTests;

	// The rules themselves.
	TestTrue(TEXT("axis lock goes along X when the pointer went further in X"), InteriorDrawing::AxisLocked({10,10}, {50,14}).Equals({50,10}, 1e-9));
	TestTrue(TEXT("axis lock goes along Y when it went further in Y"), InteriorDrawing::AxisLocked({10,10}, {12,60}).Equals({10,60}, 1e-9));
	TestTrue(TEXT("a square keeps the longer pull"), InteriorDrawing::ProportionalCorner({0,0}, {100,40}, 1.0).Equals({100,100}, 1e-9));
	TestTrue(TEXT("and the direction of the drag"), InteriorDrawing::ProportionalCorner({0,0}, {-30,10}, 1.0).Equals({-30,30}, 1e-9));
	TestTrue(TEXT("a triangle's proportion"), InteriorDrawing::ProportionalCorner({0,0}, {100,10}, 0.866).Equals({100,86.6}, 1e-6));

	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	if (PC->IsSnapEnabled()) PC->ToggleSnapEnabled();
	UInteriorFloorPlanWidget* W = Plan(F, PC);
	const FGeometry G = FGeometry::MakeRoot(FVector2f(1000,1000), FSlateLayoutTransform());

	// Shift while clicking out a room: the second point is straight along X from the first.
	PC->ToggleDrawRoomMode();
	const FVector2D First(300,300);
	FEditorInteractionTestAccess::Down(W, G, MouseWith(First, true, false, false));
	FEditorInteractionTestAccess::Up(W, G, MouseWith(First, false, false, false));
	if (!TestEqual(TEXT("one point is placed"), PC->GetPendingRoomFootprint().Num(), 1)) return false;
	FEditorInteractionTestAccess::Down(W, G, MouseWith({500,330}, true, true, false));
	if (!TestEqual(TEXT("two points"), PC->GetPendingRoomFootprint().Num(), 2)) return false;
	TestTrue(TEXT("with Shift the second point has the first point's Y"), FMath::IsNearlyEqual(PC->GetPendingRoomFootprint()[1].Y, PC->GetPendingRoomFootprint()[0].Y, 1e-6));
	TestFalse(TEXT("and it did go somewhere else along X"), FMath::IsNearlyEqual(PC->GetPendingRoomFootprint()[1].X, PC->GetPendingRoomFootprint()[0].X, 1.0));
	FEditorInteractionTestAccess::Down(W, G, MouseWith({350,500}, true, false, false));
	if (!TestEqual(TEXT("three points"), PC->GetPendingRoomFootprint().Num(), 3)) return false;
	TestFalse(TEXT("without Shift the point stays where it was clicked"), FMath::IsNearlyEqual(PC->GetPendingRoomFootprint()[2].X, PC->GetPendingRoomFootprint()[1].X, 1.0));

	// Ctrl+Z while drawing: the last point goes, one at a time, and only then does Undo reach the older edits.
	PC->CancelRoom();
	FString Error;
	AInteriorRoom* Kept = F.Interior->CreateRoomFromFootprint({{100,100},{150,100},{150,150},{100,150}}, TEXT("Keep"), TEXT("Keep"), true, false, false, Error);
	if (!TestNotNull(TEXT("an older edit to keep"), Kept)) return false;
	TestTrue(TEXT("still drawing"), PC->IsInDrawRoomMode());
	PC->AddRoomPointAtFootprintPosition({300,300});
	PC->AddRoomPointAtFootprintPosition({400,300});
	PC->AddRoomPointAtFootprintPosition({400,400});
	TestEqual(TEXT("three points"), PC->GetPendingRoomFootprint().Num(), 3);
	TestTrue(TEXT("Undo takes back a point"), PC->UndoInteriorEdit());
	TestEqual(TEXT("two points left"), PC->GetPendingRoomFootprint().Num(), 2);
	TestTrue(TEXT("and the older edit is untouched"), F.Interior->GetRoomById(TEXT("Keep")) != nullptr);
	TestTrue(TEXT("again"), PC->UndoInteriorEdit());
	TestTrue(TEXT("and again"), PC->UndoInteriorEdit());
	TestTrue(TEXT("no points left"), PC->GetPendingRoomFootprint().IsEmpty());
	TestTrue(TEXT("the older edit is still there"), F.Interior->GetRoomById(TEXT("Keep")) != nullptr);
	TestTrue(TEXT("with nothing drawn Undo goes back to the older edits"), PC->UndoInteriorEdit());
	TestNull(TEXT("and takes the room back"), F.Interior->GetRoomById(TEXT("Keep")));
	PC->ToggleDrawRoomMode();

	// Shift while dragging a shape: a square, a circle that is as high as wide, an equilateral triangle.
	for (int32 Shape = 1; Shape <= 3; ++Shape)
	{
		if (Shape == 1) PC->TogglePrefabRectangleMode();
		else if (Shape == 2) PC->TogglePrefabTriangleMode();
		else PC->TogglePrefabCircleMode();
		const FVector2D Start(600,600), End(780,640);
		FEditorInteractionTestAccess::Down(W, G, MouseWith(Start, true, true, false));
		FEditorInteractionTestAccess::Move(W, G, MouseWith(End, true, true, false));
		const TArray<FVector2D> Preview = FEditorInteractionTestAccess::Preview(W);
		if (!TestFalse(TEXT("the shape has a preview"), Preview.IsEmpty())) return false;
		const FVector2D Size = FBox2D(Preview).GetSize();
		const double Expected = Shape == 2 ? PC->GetPrefabShiftAspect() : 1.0;
		TestTrue(*FString::Printf(TEXT("shape %d keeps its proportion with Shift"), Shape), FMath::IsNearlyEqual(Size.Y / Size.X, Expected, 0.01));
		FEditorInteractionTestAccess::Up(W, G, MouseWith(End, false, true, false));
		FVector2D Position, PlacedSize;
		if (TestTrue(TEXT("the shape is placed"), PC->GetSelectedRoomBounds(Position, PlacedSize)))
		{
			TestTrue(*FString::Printf(TEXT("shape %d is placed with that proportion"), Shape), FMath::IsNearlyEqual(PlacedSize.Y / PlacedSize.X, Expected, 0.01));
			F.Interior->DeleteRoom(PC->GetSelectedRoomId()); PC->SelectRoom(FString());
		}
		// Without Shift the box is whatever the drag was.
		FEditorInteractionTestAccess::Down(W, G, MouseWith(Start, true, false, false));
		FEditorInteractionTestAccess::Move(W, G, MouseWith(End, true, false, false));
		const FVector2D Free = FBox2D(FEditorInteractionTestAccess::Preview(W)).GetSize();
		TestFalse(*FString::Printf(TEXT("shape %d is free without Shift"), Shape), FMath::IsNearlyEqual(Free.Y / Free.X, Expected, 0.01));
		FEditorInteractionTestAccess::Up(W, G, MouseWith(End, false, false, false));
		if (PC->GetSelectedRoomBounds(Position, PlacedSize)) { F.Interior->DeleteRoom(PC->GetSelectedRoomId()); PC->SelectRoom(FString()); }
	}
	return true;
}

// PAT-191, corners: a corner of a room or wall moves on its own, without drawing the room again. A corner under a resize handle needs Ctrl (a plain
// drag there resizes the room as before); the others move with a plain drag. It never goes where drawing the room would have been refused.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorRoomCornersTest, "InNavi.EditorUX.RoomCorners", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorRoomCornersTest::RunTest(const FString& Parameters)
{
	using namespace EditorInteractionTests;
	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	if (PC->IsSnapEnabled()) PC->ToggleSnapEnabled();
	FString Error;
	const APolyBuilding* Building = F.Generator->FindBuildingById(TEXT("UX"));
	if (!TestNotNull(TEXT("building"), Building)) return false;
	const FBox2D Walls(Building->GetFootprint());

	// The rules, on a rectangle.
	if (!TestNotNull(TEXT("a room"), F.Interior->CreateRoomFromFootprint({{450,450},{550,450},{550,550},{450,550}}, TEXT("Quad"), TEXT("Quad"), true, false, false, Error))) return false;
	if (!TestNotNull(TEXT("a neighbour"), F.Interior->CreateRoomFromFootprint({{600,450},{700,450},{700,550},{600,550}}, TEXT("Next"), TEXT("Next"), true, false, false, Error))) return false;
	TestTrue(TEXT("a corner can move inside the building"), F.Interior->CanMoveRoomVertex(TEXT("Quad"), 2, {570,560}));
	TestFalse(TEXT("not outside the building"), F.Interior->CanMoveRoomVertex(TEXT("Quad"), 2, {Walls.Max.X + 500, Walls.Max.Y + 500}));
	TestFalse(TEXT("not into the neighbouring room"), F.Interior->CanMoveRoomVertex(TEXT("Quad"), 1, {650,450}));
	TestFalse(TEXT("not so the outline crosses itself"), F.Interior->CanMoveRoomVertex(TEXT("Quad"), 0, {600,500}));
	TestFalse(TEXT("not onto another corner"), F.Interior->CanMoveRoomVertex(TEXT("Quad"), 0, {550,450}));
	TestFalse(TEXT("an unknown corner"), F.Interior->CanMoveRoomVertex(TEXT("Quad"), 9, {500,500}));
	TestTrue(TEXT("the move itself"), F.Interior->MoveRoomVertex(TEXT("Quad"), 2, {570,560}));
	TestTrue(TEXT("changes just that corner"), F.Interior->GetRoomById(TEXT("Quad"))->GetFootprint()[2].Equals({570,560}, 1e-6) && F.Interior->GetRoomById(TEXT("Quad"))->GetFootprint()[0].Equals({450,450}, 1e-6));
	TestTrue(TEXT("and is one Undo step"), F.Interior->Undo() && F.Interior->GetRoomById(TEXT("Quad"))->GetFootprint()[2].Equals({550,550}, 1e-6));
	F.Interior->DeleteRoom(TEXT("Next"));
	F.Interior->DeleteRoom(TEXT("Quad"));

	// An L-shaped room: its inner corner is under no resize handle.
	if (!TestNotNull(TEXT("an L-shaped room"), F.Interior->CreateRoomFromFootprint({{450,450},{650,450},{650,500},{500,500},{500,650},{450,650}}, TEXT("L"), TEXT("L"), true, false, false, Error))) return false;
	PC->ToggleSelectMode(); PC->SelectRoom(TEXT("L"));
	UInteriorFloorPlanWidget* W = Plan(F, PC);
	const FGeometry G = FGeometry::MakeRoot(FVector2f(1000,1000), FSlateLayoutTransform());
	const TArray<FVector2D> Dots = FEditorInteractionTestAccess::Vertices(W, G.GetLocalSize());
	if (!TestEqual(TEXT("a dot for each of the six corners"), Dots.Num(), 6)) return false;
	auto Corner = [&](int32 Index) { return F.Interior->GetRoomById(TEXT("L"))->GetFootprint()[Index]; };
	const FVector2D Inner = Corner(3), Outer = Corner(1), Far = Corner(0);

	// A plain drag on the inner corner moves that corner alone, and one Undo takes it back.
	FEditorInteractionTestAccess::Down(W, G, MouseWith(Dots[3], true, false, false));
	FEditorInteractionTestAccess::Move(W, G, MouseWith(Dots[3] + FVector2D(20,15), true, false, false));
	FEditorInteractionTestAccess::Up(W, G, MouseWith(Dots[3] + FVector2D(20,15), false, false, false));
	TestFalse(TEXT("the inner corner moved"), Corner(3).Equals(Inner, 0.5));
	TestTrue(TEXT("the others did not"), Corner(1).Equals(Outer, 1e-6) && Corner(0).Equals(Far, 1e-6));
	TestTrue(TEXT("the project is marked as changed"), PC->HasUnsavedChanges());
	TestTrue(TEXT("one Undo takes the corner back"), F.Interior->Undo() && Corner(3).Equals(Inner, 1e-6));
	PC->SelectRoom(TEXT("L"));

	// A plain drag on a corner under a resize handle still resizes the whole room (every corner follows); Ctrl moves just that corner.
	const FVector2D Before3 = Corner(3);
	FEditorInteractionTestAccess::Down(W, G, MouseWith(Dots[1], true, false, false));
	FEditorInteractionTestAccess::Move(W, G, MouseWith(Dots[1] + FVector2D(-40,0), true, false, false));
	FEditorInteractionTestAccess::Up(W, G, MouseWith(Dots[1] + FVector2D(-40,0), false, false, false));
	TestFalse(TEXT("a plain drag on a bounding-box corner resizes: the inner corner follows"), Corner(3).Equals(Before3, 0.5));
	F.Interior->Undo(); PC->SelectRoom(TEXT("L"));
	const TArray<FVector2D> Dots2 = FEditorInteractionTestAccess::Vertices(W, G.GetLocalSize());
	const FVector2D Inner2 = Corner(3), Outer2 = Corner(1);
	FEditorInteractionTestAccess::Down(W, G, MouseWith(Dots2[1], true, false, true));
	FEditorInteractionTestAccess::Move(W, G, MouseWith(Dots2[1] + FVector2D(-40,0), true, false, true));
	FEditorInteractionTestAccess::Up(W, G, MouseWith(Dots2[1] + FVector2D(-40,0), false, false, true));
	TestTrue(TEXT("Ctrl moves just that corner"), Corner(3).Equals(Inner2, 1e-6) && !Corner(1).Equals(Outer2, 0.5));

	// Dragged far outside the building, the corner stops at the wall.
	const TArray<FVector2D> Dots3 = FEditorInteractionTestAccess::Vertices(W, G.GetLocalSize());
	FEditorInteractionTestAccess::Down(W, G, MouseWith(Dots3[3], true, false, false));
	FEditorInteractionTestAccess::Move(W, G, MouseWith(Dots3[3] + FVector2D(6000,6000), true, false, false));
	FEditorInteractionTestAccess::Up(W, G, MouseWith(Dots3[3] + FVector2D(6000,6000), false, false, false));
	const FVector2D Stopped = Corner(3);
	TestTrue(TEXT("the corner is still inside the building"), Stopped.X <= Walls.Max.X + 0.01 && Stopped.Y <= Walls.Max.Y + 0.01);
	TestTrue(TEXT("and moved toward the wall as far as it could"), Stopped.X > Inner2.X + 1.0 || Stopped.Y > Inner2.Y + 1.0);
	return true;
}

// PAT-198: the first save of a room without tags fills them from its name and type; later saves and tags the user typed are left alone.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorAutoTagsTest, "InNavi.EditorUX.AutoTags", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorAutoTagsTest::RunTest(const FString& Parameters)
{
	using namespace EditorInteractionTests;
	TestEqual(TEXT("the words of the name, then the type"), NavigationSearch::TagsFromNameAndType(TEXT("Male Toilet L2"), TEXT("Toilet")), TArray<FString>({ TEXT("male"), TEXT("toilet"), TEXT("l2") }));
	TestEqual(TEXT("the type Other adds nothing, and a word of digits only is no tag"), NavigationSearch::TagsFromNameAndType(TEXT("Lab 3"), TEXT("Other")), TArray<FString>({ TEXT("lab") }));
	TestEqual(TEXT("a two-word type is one tag"), NavigationSearch::TagsFromNameAndType(TEXT("Level 2 Lobby"), TEXT("Lift lobby")), TArray<FString>({ TEXT("level"), TEXT("lobby"), TEXT("lift lobby") }));
	TestTrue(TEXT("a database id as the name gives no tags of its own"), NavigationSearch::TagsFromNameAndType(TEXT("SQL-1161271361-01-02"), TEXT("Other")).IsEmpty());
	TestEqual(TEXT("only the type is used then"), NavigationSearch::TagsFromNameAndType(TEXT("SQL-1161271361-01-02"), TEXT("Toilet")), TArray<FString>({ TEXT("toilet") }));
	TestTrue(TEXT("nothing to derive from nothing"), NavigationSearch::TagsFromNameAndType(FString(), TEXT("Other")).IsEmpty());

	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	FString Error;
	if (!TestNotNull(TEXT("room A"), F.Interior->CreateRoomFromFootprint({{100,100},{150,100},{150,150},{100,150}}, TEXT("A"), TEXT("Room A"), true, false, false, Error))) return false;
	if (!TestNotNull(TEXT("room B"), F.Interior->CreateRoomFromFootprint({{200,100},{250,100},{250,150},{200,150}}, TEXT("B"), TEXT("Room B"), true, false, false, Error))) return false;
	PC->ToggleSelectMode();
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(PC);
	FScriptDelegate Selected; Selected.BindUFunction(Screen, TEXT("HandleSelectedRoomChanged")); PC->OnSelectedRoomChanged.AddUnique(Selected);
	auto* Name = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Txt_RoomName")));
	auto* Tags = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Txt_RoomTags")));
	auto* Type = Cast<UComboBoxString>(Screen->FindScreenWidget(TEXT("Cmb_RoomType")));
	auto* Save = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_ApplyRoom")));
	if (!TestNotNull(TEXT("name field"), Name) || !TestNotNull(TEXT("tags field"), Tags) || !TestNotNull(TEXT("type"), Type) || !TestNotNull(TEXT("save"), Save)) return false;

	// Room A: first save, no tags: they are filled and the box shows them.
	PC->SelectRoom(TEXT("A"));
	Name->SetText(FText::FromString(TEXT("Male Toilet L2"))); Type->SetSelectedOption(TEXT("Toilet"));
	Save->OnClicked.Broadcast();
	TestEqual(TEXT("tags were filled from the name and type"), PC->GetSelectedRoomTags(), TArray<FString>({ TEXT("male"), TEXT("toilet"), TEXT("l2") }));
	TestEqual(TEXT("and the box shows them"), Tags->GetText().ToString(), FString(TEXT("male, toilet, l2")));

	// A later save does not fill them again: cleared tags stay cleared.
	Tags->SetText(FText::GetEmpty()); Name->SetText(FText::FromString(TEXT("Female Toilet L2")));
	Save->OnClicked.Broadcast();
	TestTrue(TEXT("a second save leaves the tags the user cleared"), PC->GetSelectedRoomTags().IsEmpty());

	// Room B: tags the user typed are kept as typed.
	PC->SelectRoom(TEXT("B"));
	Name->SetText(FText::FromString(TEXT("Seminar room"))); Type->SetSelectedOption(TEXT("Classroom")); Tags->SetText(FText::FromString(TEXT("projector")));
	Save->OnClicked.Broadcast();
	TestEqual(TEXT("typed tags are not touched"), PC->GetSelectedRoomTags(), TArray<FString>({ TEXT("projector") }));
	PC->OnSelectedRoomChanged.Remove(Selected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorCameraControlsTest,"InNavi.EditorUX.CameraControls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorCameraControlsTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F; ANavPlayerController* PC = F.MakeController();
	ANavCameraPawn* Pawn = F.World->SpawnActor<ANavCameraPawn>(); PC->Possess(Pawn);
	UInputComponent* Input = NewObject<UInputComponent>(Pawn); Pawn->SetupPlayerInputComponent(Input);
	TestEqual(TEXT("game mode uses the shortcut-safe camera"), GetDefault<ANavGameMode>()->DefaultPawnClass.Get(), ANavCameraPawn::StaticClass());
	bool bVertical = false;
	for (const auto& Binding : Input->AxisBindings)
	{
		TestTrue(TEXT("default Ctrl-descent axis is unbound"), Binding.AxisName != TEXT("DefaultPawn_MoveUp"));
		bVertical |= Binding.AxisName == TEXT("InNaviCamera_MoveUp");
	}
	TestTrue(TEXT("replacement vertical axis is bound"),bVertical);
	bool bDescend = false;
	for (const auto& Mapping : UPlayerInput::GetEngineDefinedAxisMappings())
	{
		if (Mapping.AxisName != TEXT("InNaviCamera_MoveUp")) continue;
		TestTrue(TEXT("Ctrl is absent from vertical mappings"), Mapping.Key != EKeys::LeftControl && Mapping.Key != EKeys::RightControl);
		bDescend |= Mapping.Key == EKeys::C && Mapping.Scale < 0;
	}
	TestTrue(TEXT("C descends"), bDescend);
	PC->PlayerInput = NewObject<UPlayerInput>(PC);
	PC->PlayerInput->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::LeftControl, IE_Pressed, 1.f, false, FPlatformTime::Cycles64()));
	TArray<UInputComponent*> Stack;
	PC->PlayerInput->ProcessInputStack(Stack, 1.f/60.f, false);
	TestTrue(TEXT("Ctrl press is processed"), PC->IsInputKeyDown(EKeys::LeftControl));
	Pawn->ConsumeMovementInputVector();
	Pawn->MoveUp_World(-1); Pawn->MoveForward(1); Pawn->MoveRight(1);
	TestTrue(TEXT("Ctrl shortcuts cannot move the camera"), Pawn->ConsumeMovementInputVector().IsNearlyZero());
	PC->PlayerInput->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::LeftControl, IE_Released, 0.f, false, FPlatformTime::Cycles64()));
	PC->PlayerInput->ProcessInputStack(Stack, 1.f/60.f, false);
	Pawn->MoveUp_World(-1);
	TestTrue(TEXT("normal descent works"), Pawn->ConsumeMovementInputVector().Z < 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorDeleteKeyTest, "InNavi.EditorUX.DeleteKey", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorDeleteKeyTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F; EditorInteractionTests::Prepare(F); ANavPlayerController* PC = F.MakeController();
	FEditorInteractionTestAccess::SetupInput(PC);
	FString Error;
	AInteriorRoom* Room = F.Interior->CreateRoomFromFootprint({{200,200},{300,200},{300,300},{200,300}}, TEXT("DeleteMe"), TEXT("DeleteMe"), true,false,false,Error);
	if (!TestNotNull(TEXT("room"),Room)) return false;
	PC->ToggleSelectMode(); PC->SelectRoom(TEXT("DeleteMe"));
	bool bBound = false;
	for (const auto& Binding : PC->InputComponent->KeyBindings)
	{
		TestTrue(TEXT("Backspace is reserved for text editing"), Binding.Chord.Key != EKeys::BackSpace);
		if (Binding.Chord.Key == EKeys::Delete && Binding.KeyEvent == IE_Pressed)
		{
			bBound = true; Binding.KeyDelegate.Execute(EKeys::Delete);
		}
	}
	TestTrue(TEXT("Delete key has a binding"), bBound);
	TestNull(TEXT("Delete binding removes the selected room"), F.Interior->GetRoomById(TEXT("DeleteMe")));
	TestTrue(TEXT("deleted room can be restored by undo"), F.Interior->Undo());
	TestNotNull(TEXT("undo restored the room"), F.Interior->GetRoomById(TEXT("DeleteMe")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorSearchKeyboardTest, "InNavi.EditorUX.SearchKeyboard", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorSearchKeyboardTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F; ANavPlayerController* PC = F.MakeController();
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(PC);
	Screen->ProcessEvent(Screen->FindFunctionChecked(TEXT("HandleNavigationStateChanged")),nullptr);
	FScriptDelegate Changed; Changed.BindUFunction(Screen,TEXT("HandleNavigationStateChanged"));
	F.Nav->OnStateChanged.AddUnique(Changed);
	auto* Destination = Cast<UNavigationPlaceCombo>(Screen->FindScreenWidget(TEXT("Cbx_NavPlace1")));
	auto* Start = Cast<UNavigationPlaceCombo>(Screen->FindScreenWidget(TEXT("Cbx_NavPlace0")));
	if (!TestNotNull(TEXT("destination search"),Destination) || !TestNotNull(TEXT("start search"),Start)) return false;
	Destination->TakeWidget(); Start->TakeWidget();
	Destination->SetSearchText(FString());
	if (!TestTrue(TEXT("there are choices"),Destination->GetFilteredOptionCount() >= 2)) return false;
	const int32 StartChoices = Start->GetFilteredOptionCount();
	FEditorInteractionTestAccess::SearchKey(Destination,EKeys::Down);
	TestFalse(TEXT("arrow navigation does not change the route"),F.Nav->HasDestination());
	FEditorInteractionTestAccess::SearchKey(Destination,EKeys::Down);
	const FString Expected = Destination->GetFilteredOption(1);
	FEditorInteractionTestAccess::SearchKey(Destination,EKeys::Enter);
	TestTrue(TEXT("Enter commits a destination"),F.Nav->HasDestination());
	TestEqual(TEXT("Enter commits the highlighted result"),F.Nav->GetPlaceText(1),Expected);
	TestTrue(TEXT("the selected index belongs to the combo's options"),Destination->GetSelectedIndex() != INDEX_NONE);
	TestTrue(TEXT("the selected destination label is visible"),FEditorInteractionTestAccess::HasChoiceLabel(Destination));
	Destination->SetSearchText(TEXT("missing-place-493257"));
	TestEqual(TEXT("no matches"),Destination->GetFilteredOptionCount(),0);
	FEditorInteractionTestAccess::SearchKey(Destination,EKeys::Enter);
	TestEqual(TEXT("Enter on no matches keeps the previous destination"),F.Nav->GetPlaceText(1),Expected);
	TestEqual(TEXT("the Start search was not changed"),Start->GetFilteredOptionCount(),StartChoices);
	Destination->SetSearchText(FString());
	TestTrue(TEXT("the query can be cleared after no matches"),Destination->GetFilteredOptionCount() >= 2);
	F.Nav->ClearAll();
	Destination->SetSearchText(Expected);
	FEditorInteractionTestAccess::SearchKey(Destination,EKeys::Enter);
	TestTrue(TEXT("the same destination can be selected after clearing"),F.Nav->HasDestination());
	TestTrue(TEXT("its label is still visible after refilling the options"),FEditorInteractionTestAccess::HasChoiceLabel(Destination));
	F.Nav->OnStateChanged.Remove(Changed);
	return true;
}

// Turning the 2D plan is only a way of looking at it: a click must land on the same footprint point it is drawn over at every turn, a drag must still move
// the content with the mouse, and a tall building laid across the screen must fill the widget.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorTurnPlanMappingTest, "InNavi.EditorUX.TurnPlan.Mapping", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorTurnPlanMappingTest::RunTest(const FString& Parameters)
{
	using namespace EditorInteractionTests;

	// A quarter turn to the right on the screen (x right, y down): right goes to down, down to left, left to up, up to right.
	auto Turned = [](FVector2D Vector, int32 Turns) { return UInteriorFloorPlanWidget::RotateQuarterTurns(Vector, Turns); };
	TestTrue(TEXT("right turns to down"), Turned(FVector2D(1, 0), 1).Equals(FVector2D(0, 1), 1e-6));
	TestTrue(TEXT("down turns to left"), Turned(FVector2D(0, 1), 1).Equals(FVector2D(-1, 0), 1e-6));
	TestTrue(TEXT("two quarters turn it round"), Turned(FVector2D(2, 3), 2).Equals(FVector2D(-2, -3), 1e-6));
	TestTrue(TEXT("three quarters are one to the left"), Turned(FVector2D(1, 0), 3).Equals(Turned(FVector2D(1, 0), -1), 1e-6));
	TestTrue(TEXT("four quarters are none"), Turned(FVector2D(2, 3), 4).Equals(FVector2D(2, 3), 1e-6));
	TestTrue(TEXT("and any whole number of them counts"), Turned(FVector2D(2, 3), -5).Equals(Turned(FVector2D(2, 3), 3), 1e-6));

	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	UInteriorFloorPlanWidget* W = Plan(F, PC);
	const FVector2D Size(1000, 1000);
	const FVector2D Centre = Size * 0.5;
	const FVector2D Right(120, 0), Down(0, 80);

	TestEqual(TEXT("not turned to begin with"), W->GetViewQuarterTurns(), 0);
	const FVector2D CentrePoint0 = W->LocalPositionToFootprintPoint(Centre, Size);
	const FVector2D RightStep0 = W->LocalPositionToFootprintPoint(Centre + Right, Size) - CentrePoint0;
	const FVector2D DownStep0 = W->LocalPositionToFootprintPoint(Centre + Down, Size) - CentrePoint0;
	TestTrue(TEXT("to the right on the screen is to the right in the plan, down is down"), RightStep0.X > 1.0 && FMath::IsNearlyZero(RightStep0.Y, 0.001) && DownStep0.Y > 1.0 && FMath::IsNearlyZero(DownStep0.X, 0.001));

	for (int32 Turn = 1; Turn <= 4; ++Turn)
	{
		W->TurnViewBy(1);
		TestEqual(TEXT("the turns count up and wrap round"), W->GetViewQuarterTurns(), Turn % 4);
		// The middle of the widget stays the middle of the plan, whatever the turn.
		const FVector2D CentrePoint = W->LocalPositionToFootprintPoint(Centre, Size);
		TestTrue(TEXT("the middle stays where it was"), CentrePoint.Equals(CentrePoint0, 0.01));
		// The screen's own right and down are the plan's directions turned back: the plan is turned to the right, so what is to the right of the middle on the
		// screen was above it in the plan.
		const FVector2D RightStep = W->LocalPositionToFootprintPoint(Centre + Right, Size) - CentrePoint;
		const FVector2D DownStep = W->LocalPositionToFootprintPoint(Centre + Down, Size) - CentrePoint;
		TestTrue(TEXT("a point to the right of the middle is the plan's direction turned back"), RightStep.Equals(UInteriorFloorPlanWidget::RotateQuarterTurns(RightStep0, -Turn), 0.01));
		TestTrue(TEXT("and so is a point below it"), DownStep.Equals(UInteriorFloorPlanWidget::RotateQuarterTurns(DownStep0, -Turn), 0.01));
		// Lengths are never changed by a turn.
		TestTrue(TEXT("a turn does not change the scale"), FMath::IsNearlyEqual(RightStep.Size(), RightStep0.Size(), 0.01));
	}
	W->TurnViewBy(-1);
	TestEqual(TEXT("one to the left from none is three to the right"), W->GetViewQuarterTurns(), 3);
	W->TurnViewBy(1);
	TestEqual(TEXT("and one to the right takes it back"), W->GetViewQuarterTurns(), 0);

	// The resize handles are drawn through the same conversion as the plan: at every turn, a click on a handle is on the corner of the room it belongs to.
	FString Error;
	if (AInteriorRoom* Room = F.Interior->CreateRoomFromFootprint({{450,450},{550,450},{550,550},{450,550}}, TEXT("Turn"), TEXT("Turn"), true,false,false,Error))
	{
		PC->ToggleSelectMode(); PC->SelectRoom(TEXT("Turn"));
		FVector2D RoomPosition, RoomSize;
		PC->GetSelectedRoomBounds(RoomPosition, RoomSize);
		for (int32 Turn = 0; Turn < 4; ++Turn)
		{
			const TArray<FVector2D> Handles = FEditorInteractionTestAccess::Handles(W, Size);
			if (!TestEqual(TEXT("eight handles at every turn"), Handles.Num(), 8)) break;
			bool bAllOnTheRoom = true;
			for (const FVector2D& Handle : Handles)
			{
				const FVector2D Point = W->LocalPositionToFootprintPoint(Handle, Size);
				bAllOnTheRoom &= Point.X > RoomPosition.X - 0.5 && Point.X < RoomPosition.X + RoomSize.X + 0.5 && Point.Y > RoomPosition.Y - 0.5 && Point.Y < RoomPosition.Y + RoomSize.Y + 0.5;
			}
			TestTrue(*FString::Printf(TEXT("every handle is on the room it belongs to (turn %d)"), Turn), bAllOnTheRoom);
			W->TurnViewBy(1);
		}
		F.Interior->DeleteRoom(TEXT("Turn")); PC->SelectRoom(FString());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorTurnPlanFitTest, "InNavi.EditorUX.TurnPlan.TallBuildingFillsTheWidget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorTurnPlanFitTest::RunTest(const FString& Parameters)
{
	using namespace EditorInteractionTests;
	FNavigationSearchFixture F;
	F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
	// Tall and narrow (200 wide, 1000 deep): in a wide widget it is small, laid across the screen it is much larger.
	F.Generator->CreateRectangleBuilding(TEXT("TALL"), TEXT("TALL"), {0, 0}, 200, 1000, 0, 2, 300);
	F.Interior->LoadInteriorForBuilding(TEXT("TALL"));
	// Floor 1 frames the whole campus area around the building; another floor frames the building's own outline, which is what is measured here.
	F.Interior->SetActiveFloor(2);
	ANavPlayerController* PC = F.MakeController();
	UInteriorFloorPlanWidget* W = Plan(F, PC);
	const FVector2D Size(1000, 400);
	const FVector2D Centre = Size * 0.5;
	auto WorldPerHundredPixels = [&]() { return (W->LocalPositionToFootprintPoint(Centre + FVector2D(100, 0), Size) - W->LocalPositionToFootprintPoint(Centre, Size)).Size(); };

	const double Upright = WorldPerHundredPixels();
	W->TurnViewBy(1);
	const double Laid = WorldPerHundredPixels();
	TestTrue(TEXT("laid across the screen the plan is drawn larger (fewer plan units per pixel)"), Laid < Upright * 0.6);
	W->TurnViewBy(1);
	TestTrue(TEXT("turned round it is as big as upright"), FMath::IsNearlyEqual(WorldPerHundredPixels(), Upright, Upright * 0.001));
	W->TurnViewBy(1);
	TestTrue(TEXT("and three quarters is as big as one"), FMath::IsNearlyEqual(WorldPerHundredPixels(), Laid, Laid * 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorTurnPlanButtonsTest, "InNavi.EditorUX.TurnPlan.Buttons", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditorTurnPlanButtonsTest::RunTest(const FString& Parameters)
{
	using namespace EditorInteractionTests;
	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(PC);
	UButton* Left = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_TurnPlanLeft")));
	UButton* Right = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_TurnPlanRight")));
	UWidget* Colours = Screen->FindScreenWidget(TEXT("Btn_RoomColours"));
	UInteriorFloorPlanWidget* PlanWidget = Cast<UInteriorFloorPlanWidget>(Screen->FindScreenWidget(TEXT("FloorPlanWidget")));
	if (!TestNotNull(TEXT("Turn left"), Left) || !TestNotNull(TEXT("Turn right"), Right) || !TestNotNull(TEXT("Colours"), Colours) || !TestNotNull(TEXT("the plan"), PlanWidget)) return false;

	// Right after Colours, left then right.
	const UPanelWidget* Row = Colours->GetParent();
	if (TestTrue(TEXT("they are in the same bar as Colours"), Row && Left->GetParent() == Row && Right->GetParent() == Row))
	{
		TestEqual(TEXT("Turn left is right after Colours"), Row->GetChildIndex(Left), Row->GetChildIndex(Colours) + 1);
		TestEqual(TEXT("and Turn right right after it"), Row->GetChildIndex(Right), Row->GetChildIndex(Left) + 1);
	}

	// Shown in 2D, gone in 3D, and back.
	TestEqual(TEXT("shown in 2D"), Left->GetVisibility() == ESlateVisibility::Visible && Right->GetVisibility() == ESlateVisibility::Visible, true);
	bool bPreview = true;
	Screen->ProcessEvent(Screen->FindFunctionChecked(TEXT("HandlePreviewModeChanged")), &bPreview);
	TestEqual(TEXT("hidden in 3D"), Left->GetVisibility() == ESlateVisibility::Collapsed && Right->GetVisibility() == ESlateVisibility::Collapsed, true);
	bPreview = false;
	Screen->ProcessEvent(Screen->FindFunctionChecked(TEXT("HandlePreviewModeChanged")), &bPreview);
	TestEqual(TEXT("and back in 2D"), Left->GetVisibility() == ESlateVisibility::Visible && Right->GetVisibility() == ESlateVisibility::Visible, true);

	// They turn the plan, a quarter at a time, and nothing else.
	const int32 RoomsBefore = F.Interior->GetRoomsOnFloor(1).Num();
	Right->OnClicked.Broadcast();
	TestEqual(TEXT("Turn right: one quarter to the right"), PlanWidget->GetViewQuarterTurns(), 1);
	Right->OnClicked.Broadcast();
	TestEqual(TEXT("again: two"), PlanWidget->GetViewQuarterTurns(), 2);
	Left->OnClicked.Broadcast();
	TestEqual(TEXT("Turn left takes one back"), PlanWidget->GetViewQuarterTurns(), 1);
	Left->OnClicked.Broadcast(); Left->OnClicked.Broadcast();
	TestEqual(TEXT("and from none, left is three to the right"), PlanWidget->GetViewQuarterTurns(), 3);
	TestEqual(TEXT("the layout is not touched"), F.Interior->GetRoomsOnFloor(1).Num(), RoomsBefore);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
