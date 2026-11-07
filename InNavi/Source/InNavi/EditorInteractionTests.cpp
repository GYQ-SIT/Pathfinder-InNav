#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "InteriorFloorPlanWidget.h"
#include "InteriorScreenWidget.h"
#include "InteriorRoom.h"
#include "DebugPanelWidget.h"
#include "NavCameraPawn.h"
#include "NavGameMode.h"
#include "NavigationDockPanelWidget.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorCameraControlsTest, "InNavi.EditorUX.CameraControls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
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

#endif // WITH_DEV_AUTOMATION_TESTS
