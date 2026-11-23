#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "InteriorFloorPlanWidget.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "DebugPanelWidget.h"
#include "InteriorScreenWidget.h"
#include "NavPlayerController.h"
#include "UiTestFixture.h"

#include "Engine/Texture2D.h"
#include "Input/Events.h"
#include "Misc/AutomationTest.h"

// PAT-202: the transform gizmos (W move, E turn, R resize) on the 2D plan.

struct FGizmoTestAccess
{
	static void Bind(UInteriorFloorPlanWidget* Plan, FNavigationSearchFixture& F, ANavPlayerController* PC)
	{
		Plan->InteriorGen = F.Interior; Plan->Generator = F.Generator; Plan->PlayerController = PC;
	}
	static FReply Down(UInteriorFloorPlanWidget* W, const FGeometry& G, const FPointerEvent& E) { return W->NativeOnMouseButtonDown(G, E); }
	static FReply Move(UInteriorFloorPlanWidget* W, const FGeometry& G, const FPointerEvent& E) { return W->NativeOnMouseMove(G, E); }
	static FReply Up(UInteriorFloorPlanWidget* W, const FGeometry& G, const FPointerEvent& E) { return W->NativeOnMouseButtonUp(G, E); }
	static FReply Wheel(UInteriorFloorPlanWidget* W, const FGeometry& G, const FPointerEvent& E) { return W->NativeOnMouseWheel(G, E); }
	static void SetReferenceTexture(UInteriorFloorPlanWidget* W, const FString& Path)
	{
		W->ReferenceImageTexture = UTexture2D::CreateTransient(16, 16);
		W->ReferenceImagePath = Path;
	}
	static TArray<FVector2D> ResizeHandles(UInteriorFloorPlanWidget* W, FVector2D Size) { return W->GetResizeHandles(Size); }
	static TArray<FVector2D> VertexHandles(UInteriorFloorPlanWidget* W, FVector2D Size) { return W->GetVertexHandles(Size); }
	static UInteriorFloorPlanWidget::FGizmoLayout Layout(UInteriorFloorPlanWidget* W, FVector2D Size) { return W->ComputeGizmoLayout(Size); }
	// The 3D connector drag: its state is set up here, because the pointer ray that drives it needs a real viewport.
	static void Begin3DConnectorDrag(ANavPlayerController* PC) { PC->DragStart3DConnectorPositions = PC->GetSelectedConnectorPositions(false); PC->bIsDragging3DConnector = !PC->DragStart3DConnectorPositions.IsEmpty(); }
	static void Note3DConnectorMove(ANavPlayerController* PC, bool bMoved) { PC->b3DConnectorMoved |= bMoved; PC->b3DConnectorRefused = !bMoved; }
	static bool Dragging3DConnector(ANavPlayerController* PC) { return PC->bIsDragging3DConnector; }
	static void Release3D(ANavPlayerController* PC) { PC->OnLeftMouseUp3DEdit(); }
};

namespace TransformGizmoTests
{
	void Prepare(FNavigationSearchFixture& F)
	{
		F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
		F.Generator->CreateRectangleBuilding(TEXT("GZ"), TEXT("GZ"), {500,500}, 1000, 1000, 0, 1, 300);
		F.Interior->LoadInteriorForBuilding(TEXT("GZ"));
	}
	FPointerEvent Mouse(FVector2D Position, bool bHeld)
	{
		TSet<FKey> Held;
		if (bHeld) Held.Add(EKeys::LeftMouseButton);
		return FPointerEvent(0, Position, Position, Held, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
	}
	FBox2D BoundsOf(FNavigationSearchFixture& F, const FString& Id) { return FBox2D(F.Interior->GetRoomById(Id)->GetFootprint()); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransformGizmoTest, "InNavi.EditorUX.TransformGizmo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTransformGizmoTest::RunTest(const FString& Parameters)
{
	using namespace TransformGizmoTests;
	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	if (PC->IsSnapEnabled()) PC->ToggleSnapEnabled();
	FString Error;
	if (!TestNotNull(TEXT("a room"), F.Interior->CreateRoomFromFootprint({{400,400},{500,400},{500,450},{400,450}}, TEXT("R"), TEXT("R"), true, false, false, Error))) return false;
	if (!TestNotNull(TEXT("another"), F.Interior->CreateRoomFromFootprint({{700,400},{760,400},{760,450},{700,450}}, TEXT("S"), TEXT("S"), true, false, false, Error))) return false;
	UInteriorFloorPlanWidget* W = NewObject<UInteriorFloorPlanWidget>(); W->Initialize(); W->SetOwningPlayer(PC); W->TakeWidget();
	FGizmoTestAccess::Bind(W, F, PC);
	const FGeometry G = FGeometry::MakeRoot(FVector2f(1000,1000), FSlateLayoutTransform());
	const FVector2D Size = G.GetLocalSize();

	// When there is a gizmo: Select mode, one room (or wall, or connector) selected, a mode chosen.
	PC->SetTransformGizmo(ETransformGizmo::Translate);
	PC->SelectRoom(TEXT("R"));
	TestFalse(TEXT("not outside Select mode"), PC->IsTransformGizmoShown());
	PC->ToggleSelectMode();
	TestTrue(TEXT("in Select mode with a room selected, it is shown"), PC->IsTransformGizmoShown());
	PC->SetRoomSelection({ TEXT("R"), TEXT("S") });
	TestFalse(TEXT("not for several rooms"), PC->IsTransformGizmoShown());
	PC->SelectRoom(TEXT("R"));
	PC->ToggleTransformGizmo(ETransformGizmo::Translate);
	TestEqual(TEXT("the same key again switches it off"), PC->GetTransformGizmo(), ETransformGizmo::None);
	TestFalse(TEXT("and then there is none"), PC->IsTransformGizmoShown());
	TestFalse(TEXT("the plain handles are back"), FGizmoTestAccess::ResizeHandles(W, Size).IsEmpty());

	// While a gizmo is on, the plain resize handles and corner dots are not drawn.
	PC->SetTransformGizmo(ETransformGizmo::Translate);
	TestTrue(TEXT("no resize handles under the gizmo"), FGizmoTestAccess::ResizeHandles(W, Size).IsEmpty());
	TestTrue(TEXT("no corner dots either"), FGizmoTestAccess::VertexHandles(W, Size).IsEmpty());

	// Translate: an arrow moves along its axis only, the middle moves freely.
	auto Layout = [&]() { return FGizmoTestAccess::Layout(W, Size); };
	auto L = Layout();
	if (!TestTrue(TEXT("the layout is there"), L.bValid && L.bRoom)) return false;
	FBox2D Before = BoundsOf(F, TEXT("R"));
	FVector2D Grab = L.Center + L.DirX * 40.0;
	FGizmoTestAccess::Down(W, G, Mouse(Grab, true));
	FGizmoTestAccess::Move(W, G, Mouse(Grab + L.DirX * 30.0 + L.DirY * 25.0, true));
	FGizmoTestAccess::Up(W, G, Mouse(Grab + L.DirX * 30.0 + L.DirY * 25.0, false));
	FBox2D After = BoundsOf(F, TEXT("R"));
	TestTrue(TEXT("the X arrow moved the room along X"), !FMath::IsNearlyEqual(After.Min.X, Before.Min.X, 0.5));
	TestTrue(TEXT("and not at all along Y"), FMath::IsNearlyEqual(After.Min.Y, Before.Min.Y, 1e-6));
	TestTrue(TEXT("the size did not change"), After.GetSize().Equals(Before.GetSize(), 1e-6));
	L = Layout();
	Before = After;
	Grab = L.Center + L.DirY * 40.0;
	FGizmoTestAccess::Down(W, G, Mouse(Grab, true));
	FGizmoTestAccess::Move(W, G, Mouse(Grab + L.DirX * 30.0 + L.DirY * 25.0, true));
	FGizmoTestAccess::Up(W, G, Mouse(Grab + L.DirX * 30.0 + L.DirY * 25.0, false));
	After = BoundsOf(F, TEXT("R"));
	TestTrue(TEXT("the Y arrow moved it along Y only"), !FMath::IsNearlyEqual(After.Min.Y, Before.Min.Y, 0.5) && FMath::IsNearlyEqual(After.Min.X, Before.Min.X, 1e-6));
	L = Layout();
	Before = After;
	FGizmoTestAccess::Down(W, G, Mouse(L.Center, true));
	FGizmoTestAccess::Move(W, G, Mouse(L.Center + L.DirX * 30.0 + L.DirY * 25.0, true));
	FGizmoTestAccess::Up(W, G, Mouse(L.Center + L.DirX * 30.0 + L.DirY * 25.0, false));
	After = BoundsOf(F, TEXT("R"));
	TestTrue(TEXT("the middle square moves it freely"), !FMath::IsNearlyEqual(After.Min.X, Before.Min.X, 0.5) && !FMath::IsNearlyEqual(After.Min.Y, Before.Min.Y, 0.5));

	// Rotate: the ring turns the room by the angle the pointer goes round it; one Undo takes the whole turn back.
	PC->SetTransformGizmo(ETransformGizmo::Rotate);
	const TArray<FVector2D> OriginalFootprint = F.Interior->GetRoomById(TEXT("R"))->GetFootprint();
	const FBox2D BeforeTurn = BoundsOf(F, TEXT("R"));
	L = Layout();
	FGizmoTestAccess::Down(W, G, Mouse(L.Center + L.DirX * L.RingRadius, true));
	for (int32 Step = 1; Step <= 6; ++Step)
	{
		const double Angle = FMath::DegreesToRadians(15.0 * Step);
		FGizmoTestAccess::Move(W, G, Mouse(L.Center + (L.DirX * FMath::Cos(Angle) + L.DirY * FMath::Sin(Angle)) * L.RingRadius, true));
	}
	FGizmoTestAccess::Up(W, G, Mouse(L.Center + L.DirY * L.RingRadius, false));
	const FBox2D Turned = BoundsOf(F, TEXT("R"));
	TestTrue(TEXT("a quarter turn swaps the room's width and length"), FMath::IsNearlyEqual(Turned.GetSize().X, BeforeTurn.GetSize().Y, 1.0) && FMath::IsNearlyEqual(Turned.GetSize().Y, BeforeTurn.GetSize().X, 1.0));
	TestTrue(TEXT("about its own centre"), Turned.GetCenter().Equals(BeforeTurn.GetCenter(), 1.0));
	TestTrue(TEXT("one Undo takes the whole turn back"), F.Interior->Undo() && FBox2D(F.Interior->GetRoomById(TEXT("R"))->GetFootprint()).GetSize().Equals(BeforeTurn.GetSize(), 0.01));
	PC->SelectRoom(TEXT("R"));

	// Scale: the X handle widens, the Y handle lengthens, the corner keeps the proportions.
	PC->SetTransformGizmo(ETransformGizmo::Scale);
	L = Layout();
	Before = BoundsOf(F, TEXT("R"));
	FGizmoTestAccess::Down(W, G, Mouse(L.ScaleX, true));
	FGizmoTestAccess::Move(W, G, Mouse(L.ScaleX + L.DirX * 40.0, true));
	FGizmoTestAccess::Up(W, G, Mouse(L.ScaleX + L.DirX * 40.0, false));
	After = BoundsOf(F, TEXT("R"));
	TestTrue(TEXT("the X handle made it wider"), After.GetSize().X > Before.GetSize().X + 10.0);
	TestTrue(TEXT("and not longer"), FMath::IsNearlyEqual(After.GetSize().Y, Before.GetSize().Y, 1e-6));
	L = Layout();
	Before = After;
	FGizmoTestAccess::Down(W, G, Mouse(L.ScaleY, true));
	FGizmoTestAccess::Move(W, G, Mouse(L.ScaleY + L.DirY * 40.0, true));
	FGizmoTestAccess::Up(W, G, Mouse(L.ScaleY + L.DirY * 40.0, false));
	After = BoundsOf(F, TEXT("R"));
	TestTrue(TEXT("the Y handle made it longer"), After.GetSize().Y > Before.GetSize().Y + 10.0 && FMath::IsNearlyEqual(After.GetSize().X, Before.GetSize().X, 1e-6));
	L = Layout();
	Before = After;
	const double Aspect = Before.GetSize().Y / Before.GetSize().X;
	FGizmoTestAccess::Down(W, G, Mouse(L.ScaleUniform, true));
	FGizmoTestAccess::Move(W, G, Mouse(L.ScaleUniform + (L.DirX + L.DirY) * 30.0, true));
	FGizmoTestAccess::Up(W, G, Mouse(L.ScaleUniform + (L.DirX + L.DirY) * 30.0, false));
	After = BoundsOf(F, TEXT("R"));
	TestTrue(TEXT("the corner handle made it bigger"), After.GetSize().X > Before.GetSize().X + 5.0);
	TestTrue(TEXT("with the same proportions"), FMath::IsNearlyEqual(After.GetSize().Y / After.GetSize().X, Aspect, 0.02));

	// A lift has a gizmo too: it moves, turns and grows as a whole.
	PC->SelectRoom(FString());
	const FString Group = F.Interior->PlaceConnectorNode(EInteriorNavNodeType::Elevator, {200, 700}, TArray<int32>{ 1 });
	if (!TestFalse(TEXT("a lift"), Group.IsEmpty())) return false;
	PC->SelectConnector(Group);
	TestTrue(TEXT("shown for a connector"), PC->IsTransformGizmoShown());
	auto NodeOf = [&]() -> const FInteriorNavNode* { for (const FInteriorNavNode& N : F.Interior->GetNavNodes()) if (N.ConnectorGroupId == Group) return &N; return nullptr; };
	PC->SetTransformGizmo(ETransformGizmo::Translate);
	L = Layout();
	if (!TestTrue(TEXT("a layout for the lift"), L.bValid && !L.bRoom)) return false;
	const FVector2D LiftBefore = NodeOf()->Position;
	Grab = L.Center + L.DirX * 40.0;
	FGizmoTestAccess::Down(W, G, Mouse(Grab, true));
	FGizmoTestAccess::Move(W, G, Mouse(Grab + L.DirX * 30.0 + L.DirY * 20.0, true));
	FGizmoTestAccess::Up(W, G, Mouse(Grab + L.DirX * 30.0 + L.DirY * 20.0, false));
	TestTrue(TEXT("the lift moved along X only"), !FMath::IsNearlyEqual(NodeOf()->Position.X, LiftBefore.X, 0.5) && FMath::IsNearlyEqual(NodeOf()->Position.Y, LiftBefore.Y, 1e-6));
	PC->SetTransformGizmo(ETransformGizmo::Scale);
	L = Layout();
	const float ScaleBefore = NodeOf()->SizeScale;
	FGizmoTestAccess::Down(W, G, Mouse(L.ScaleUniform, true));
	FGizmoTestAccess::Move(W, G, Mouse(L.ScaleUniform + (L.ScaleUniform - L.Center).GetSafeNormal() * 40.0, true));
	FGizmoTestAccess::Up(W, G, Mouse(L.ScaleUniform + (L.ScaleUniform - L.Center).GetSafeNormal() * 40.0, false));
	TestTrue(TEXT("the lift grew"), NodeOf()->SizeScale > ScaleBefore + 0.01f);
	PC->SetTransformGizmo(ETransformGizmo::Rotate);
	L = Layout();
	const float TurnBefore = NodeOf()->RotationDegrees;
	FGizmoTestAccess::Down(W, G, Mouse(L.Center + L.DirX * L.RingRadius, true));
	for (int32 Step = 1; Step <= 3; ++Step)
	{
		const double Angle = FMath::DegreesToRadians(15.0 * Step);
		FGizmoTestAccess::Move(W, G, Mouse(L.Center + (L.DirX * FMath::Cos(Angle) + L.DirY * FMath::Sin(Angle)) * L.RingRadius, true));
	}
	FGizmoTestAccess::Up(W, G, Mouse(L.Center + L.DirY * L.RingRadius, false));
	TestFalse(TEXT("the lift turned"), FMath::IsNearlyEqual(NodeOf()->RotationDegrees, TurnBefore, 1.f));

	// Nothing for a door or an outer-wall door.
	PC->SelectRoom(TEXT("S"));
	TestTrue(TEXT("a room again"), PC->IsTransformGizmoShown());
	PC->SelectRoom(FString());
	TestFalse(TEXT("no selection, no gizmo"), PC->IsTransformGizmoShown());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnector3DDragTest, "InNavi.EditorUX.Connector3DDrag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConnector3DDragTest::RunTest(const FString& Parameters)
{
	using namespace TransformGizmoTests;
	FNavigationSearchFixture F; Prepare(F); ANavPlayerController* PC = F.MakeController();
	const FString Group = F.Interior->PlaceConnectorNode(EInteriorNavNodeType::Elevator, {200, 700}, TArray<int32>{ 1 });
	if (!TestFalse(TEXT("a lift"), Group.IsEmpty())) return false;
	auto Position = [&]() { for (const FInteriorNavNode& N : F.Interior->GetNavNodes()) if (N.ConnectorGroupId == Group) return N.Position; return FVector2D::ZeroVector; };
	PC->SelectConnector(Group);
	const FVector2D Start = Position();

	// The drag as the 3D view does it: each tick asks for the grab positions plus how far the pointer went; one history step when it is let go.
	FGizmoTestAccess::Begin3DConnectorDrag(PC);
	if (!TestTrue(TEXT("the drag has begun"), FGizmoTestAccess::Dragging3DConnector(PC))) return false;
	TMap<int32, FVector2D> Target = PC->GetSelectedConnectorPositions(false);
	for (TPair<int32, FVector2D>& Pair : Target) Pair.Value += FVector2D(40, 25);
	const bool bMoved = PC->MoveSelectedConnectorTo(Target);
	FGizmoTestAccess::Note3DConnectorMove(PC, bMoved);
	TestTrue(TEXT("the lift followed"), bMoved && Position().Equals(Start + FVector2D(40, 25), 0.001));
	FGizmoTestAccess::Release3D(PC);
	TestFalse(TEXT("let go: the drag is over"), FGizmoTestAccess::Dragging3DConnector(PC));
	TestTrue(TEXT("one Undo takes the whole drag back"), F.Interior->Undo() && Position().Equals(Start, 0.001));
	F.Interior->Redo();

	// Dropped where it cannot go: it stays at the last good place and the reason is told once, on release.
	PC->SelectConnector(Group);
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(PC);
	UDebugPanelWidget* Debug = Screen->GetDebugPanelForTests(); if (Debug) Debug->BindToController();
	FGizmoTestAccess::Begin3DConnectorDrag(PC);
	TMap<int32, FVector2D> Outside = PC->GetSelectedConnectorPositions(false);
	for (TPair<int32, FVector2D>& Pair : Outside) Pair.Value = FVector2D(-500, 700);
	FGizmoTestAccess::Note3DConnectorMove(PC, PC->MoveSelectedConnectorTo(Outside));
	FGizmoTestAccess::Release3D(PC);
	if (Debug) TestTrue(TEXT("the message says it stays and why"), Debug->GetLastErrorTextForTests().Contains(TEXT("stays where it was")) && Debug->GetLastErrorTextForTests().Contains(TEXT("outer wall")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReferenceImageMouseTest, "InNavi.EditorUX.ReferenceImageMouse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReferenceImageMouseTest::RunTest(const FString& Parameters)
{
	using namespace TransformGizmoTests;
	FNavigationSearchFixture Fixture;
	Prepare(Fixture);
	ANavPlayerController* PC = Fixture.MakeController();
	PC->SetActiveFloor(1);
	UInteriorFloorPlanWidget* Plan = NewObject<UInteriorFloorPlanWidget>();
	Plan->Initialize();
	Plan->SetOwningPlayer(PC);
	const TSharedRef<SWidget> SlatePlan = Plan->TakeWidget();
	FGizmoTestAccess::Bind(Plan, Fixture, PC);

	const FBox2D Frame = PC->GetReferenceImageFrame();
	FInteriorReferenceImage Image;
	Image.Path = TEXT("reference.png");
	Image.Center = Frame.GetCenter();
	Image.Width = static_cast<float>(Frame.GetSize().X);
	Image.Opacity = 0.8f;
	Image.bLocked = false;
	if (!TestTrue(TEXT("an unlocked image is set"), PC->SetActiveReferenceImage(Image))) return false;
	FGizmoTestAccess::SetReferenceTexture(Plan, Image.Path);
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(1000, 1000), FSlateLayoutTransform());
	const FVector2D Size = Geometry.GetLocalSize();
	const FVector2D Middle = Size * 0.5;
	const FVector2D Moved = Middle + FVector2D(80.0, 60.0);

	TestTrue(TEXT("an unlocked image captures a drag"), FGizmoTestAccess::Down(Plan, Geometry, Mouse(Middle, true)).GetMouseCaptor().IsValid());
	FGizmoTestAccess::Move(Plan, Geometry, Mouse(Moved, true));
	FInteriorReferenceImage Current;
	PC->GetActiveReferenceImage(Current);
	TestTrue(TEXT("drag preview does not commit early"), Current == Image);
	FGizmoTestAccess::Up(Plan, Geometry, Mouse(Moved, false));
	PC->GetActiveReferenceImage(Current);
	TestTrue(TEXT("release moves the image to the pointer"),
		Current.Center.Equals(Plan->LocalPositionToFootprintPoint(Moved, Size), 0.01));
	TestTrue(TEXT("one undo reverses the entire drag"), PC->UndoInteriorEdit());
	PC->GetActiveReferenceImage(Current);
	TestTrue(TEXT("the old image is restored"), Current == Image);

	FGizmoTestAccess::Wheel(Plan, Geometry, FPointerEvent(0, Middle, Middle, {}, FKey(), 1.f, FModifierKeysState()));
	PC->GetActiveReferenceImage(Current);
	TestTrue(TEXT("wheel scales an unlocked image"), FMath::IsNearlyEqual(Current.Width, Image.Width * 1.1f, 0.01f));

	Plan->SetReferenceCropMode(true);
	FGizmoTestAccess::Down(Plan, Geometry, Mouse(Middle - FVector2D(120, 100), true));
	FGizmoTestAccess::Move(Plan, Geometry, Mouse(Middle + FVector2D(120, 100), true));
	FGizmoTestAccess::Up(Plan, Geometry, Mouse(Middle + FVector2D(120, 100), false));
	PC->GetActiveReferenceImage(Current);
	TestTrue(TEXT("cropping stores a smaller UV rectangle and exits crop mode"),
		Current.Crop.GetArea() > 0.0 && Current.Crop.GetArea() < 1.0 && !Plan->IsReferenceCropMode());
	TestTrue(TEXT("crop does not change saved opacity"), FMath::IsNearlyEqual(Current.Opacity, Image.Opacity));
	return true;
}

#endif
