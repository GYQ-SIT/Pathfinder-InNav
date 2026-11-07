#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "InteriorFloorPlanWidget.h"
#include "InteriorScreenWidget.h"
#include "InteriorRoom.h"
#include "AudioPanelWidget.h"
#include "EditScreenWidget.h"
#include "NavigationDockPanelWidget.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/AudioComponent.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Misc/AutomationTest.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/HittestGrid.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SBoxPanel.h"

struct FEditorFeedbackTestAccess
{
	static void Bind(UInteriorFloorPlanWidget* W, FNavigationSearchFixture& F, ANavPlayerController* PC) { W->InteriorGen=F.Interior; W->Generator=F.Generator; W->PlayerController=PC; }
	static auto Handles(UInteriorFloorPlanWidget* W) { return W->GetResizeHandles({1000,1000}); }
	static FPointerEvent Mouse(FVector2D P, bool bDown) { TSet<FKey> Keys; if (bDown) Keys.Add(EKeys::LeftMouseButton); return FPointerEvent(0,P,P,Keys,EKeys::LeftMouseButton,0,FModifierKeysState()); }
	static FGeometry Geometry() { return FGeometry::MakeRoot(FVector2f(1000,1000),FSlateLayoutTransform()); }
	static void Down(UInteriorFloorPlanWidget* W,FVector2D P) { W->NativeOnMouseButtonDown(Geometry(),Mouse(P,true)); }
	static void Move(UInteriorFloorPlanWidget* W,FVector2D P) { W->NativeOnMouseMove(Geometry(),Mouse(P,true)); }
	static void Up(UInteriorFloorPlanWidget* W,FVector2D P) { W->NativeOnMouseButtonUp(Geometry(),Mouse(P,false)); }
	static void Open(UNavigationPlaceCombo* C) { C->MyComboBox->SetIsOpen(true); }
	static void Close(UNavigationPlaceCombo* C) { C->MyComboBox->SetIsOpen(false); }
	static bool IsOpen(UNavigationPlaceCombo* C) { return C->MyComboBox->IsOpen(); }
	static auto Results(UNavigationPlaceCombo* C) { return C->SearchResults; }
	static auto First(UNavigationPlaceCombo* C) { return C->FilteredOptions[0]; }
	static FString PopupQuery(UNavigationPlaceCombo* C) { return C->SearchField->GetText().ToString(); }
	static AActor* AudioOwner(ANavPlayerController* PC) { return PC->AudioActor; }
	static TArray<FVector2f> PaintPreview(UInteriorFloorPlanWidget* W)
	{
		// Call the real paint path even under -nullrhi, so CI covers preview draw-element generation.
		auto Window = SNew(SWindow).ClientSize(FVector2D(1000,1000));
		FHittestGrid HitTest;
		FPaintArgs Args(&Window.Get(), HitTest, FVector2D::ZeroVector, 0.0, 1.f/60.f);
		FSlateWindowElementList Elements(Window);
		W->NativePaint(Args, Geometry(), FSlateRect(0,0,1000,1000), Elements, 0, FWidgetStyle(), true);
		for (const auto& Line : Elements.GetUncachedDrawElements().Get<(uint8)EElementType::ET_Line>())
			if (Line.GetLayer() == 9) return Line.GetPoints();
		return {};
	}
};

namespace EditorFeedbackTests
{
	void Prepare(FNavigationSearchFixture& F)
	{
		F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
		F.Generator->CreateRectangleBuilding(TEXT("Feedback"),TEXT("Feedback"),{500,500},1000,1000,0,2,300);
		F.Interior->LoadInteriorForBuilding(TEXT("Feedback"));
	}
	UInteriorScreenWidget* Screen(ANavPlayerController* PC)
	{
		auto* W=NewObject<UInteriorScreenWidget>(); W->Initialize(); W->TakeWidget(); W->SetOwningPlayer(PC);
		W->ProcessEvent(W->FindFunctionChecked(TEXT("HandleNavigationStateChanged")),nullptr); return W;
	}
	UInteriorFloorPlanWidget* Plan(FNavigationSearchFixture& F,ANavPlayerController* PC)
	{
		auto* W=NewObject<UInteriorFloorPlanWidget>(); W->Initialize(); W->SetOwningPlayer(PC); W->TakeWidget(); FEditorFeedbackTestAccess::Bind(W,F,PC); return W;
	}
	void Type(UEditableTextBox* Box,const TCHAR* Value) { Box->SetText(FText::FromString(Value)); Box->OnTextChanged.Broadcast(Box->GetText()); }
	void SlateFrames() { for (int32 I=0; I<3; ++I) { FSlateApplication::Get().Tick(); FPlatformProcess::Sleep(0.02f); } }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorFeedbackPreviewPaint,"InNavi.EditorUX.Feedback.PreviewNativePaint",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEditorFeedbackPreviewPaint::RunTest(const FString& Parameters)
{
	using namespace EditorFeedbackTests;
	for (int32 Shape=1; Shape<=3; ++Shape)
	{
		FNavigationSearchFixture F; Prepare(F); auto* PC=F.MakeController(); auto* W=Plan(F,PC);
		if (Shape==1) PC->TogglePrefabRectangleMode();
		else if (Shape==2) PC->TogglePrefabTriangleMode();
		else PC->TogglePrefabCircleMode();
		FEditorFeedbackTestAccess::Down(W,{400,400});
		for (bool bDragged : {false,true})
		{
			if (bDragged) FEditorFeedbackTestAccess::Move(W,{550,550});
			const auto Painted=FEditorFeedbackTestAccess::PaintPreview(W);
			const int32 Expected=PC->BuildPrefabRoomFootprint({500,500}).Num()+1;
			TestEqual(TEXT("NativePaint emits the complete prefab outline on click and drag"),Painted.Num(),Expected);
			if (Painted.Num()>1) TestTrue(TEXT("painted prefab outline is closed"),Painted[0].Equals(Painted.Last()));
			TestTrue(TEXT("painting a prefab preview does not place a room"),F.Interior->GetRoomsOnFloor(1).IsEmpty());
		}
	}
	for (bool bClosed : {true,false})
	{
		FNavigationSearchFixture F; Prepare(F); auto* PC=F.MakeController(); auto* W=Plan(F,PC); FString Error;
		const TArray<FVector2D> Points=bClosed?TArray<FVector2D>{{400,400},{600,400},{600,600},{400,600}}:TArray<FVector2D>{{400,500},{600,500}};
		auto* Room=bClosed?F.Interior->CreateRoomFromFootprint(Points,TEXT("Paint"),TEXT("Paint"),true,false,false,Error):F.Interior->CreateWallFromPoints(Points,TEXT("Paint"),Error);
		if (!TestNotNull(TEXT("resize target"),Room)) return false;
		PC->ToggleSelectMode(); PC->SelectRoom(TEXT("Paint"));
		const auto Handles=FEditorFeedbackTestAccess::Handles(W);
		if (!TestTrue(TEXT("resize handles exist"),!Handles.IsEmpty())) return false;
		FEditorFeedbackTestAccess::Down(W,Handles.Last());
		const auto Initial=FEditorFeedbackTestAccess::PaintPreview(W);
		FEditorFeedbackTestAccess::Move(W,Handles.Last()+FVector2D(-20,20));
		const auto Painted=FEditorFeedbackTestAccess::PaintPreview(W);
		TestEqual(TEXT("NativePaint emits the resized outline"),Painted.Num(),Points.Num()+(bClosed?1:0));
		if (Painted.Num()>1) TestEqual(TEXT("only room resize previews close their outline"),Painted[0].Equals(Painted.Last()),bClosed);
		TestTrue(TEXT("paint reflects the changed preview size"),Initial!=Painted);
		TestTrue(TEXT("painting resize previews leaves room geometry unchanged"),Room->GetFootprint()==Points);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorFeedbackSizeCommit,"InNavi.EditorUX.Feedback.SizeCommitAndStraightWalls",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEditorFeedbackSizeCommit::RunTest(const FString& Parameters)
{
	using namespace EditorFeedbackTests;
	for (bool bVertical : {false,true})
	{
		FNavigationSearchFixture F; Prepare(F); auto* PC=F.MakeController(); FString Error;
		const TArray<FVector2D> Points=bVertical?TArray<FVector2D>{{500,400},{500,600}}:TArray<FVector2D>{{400,500},{600,500}};
		if (!TestNotNull(TEXT("straight wall"),F.Interior->CreateWallFromPoints(Points,TEXT("Wall"),Error))) return false;
		auto* ScreenWidget=Screen(PC); FScriptDelegate Selected; Selected.BindUFunction(ScreenWidget,TEXT("HandleSelectedRoomChanged")); PC->OnSelectedRoomChanged.AddUnique(Selected);
		PC->ToggleSelectMode(); PC->SelectRoom(TEXT("Wall"));
		auto* Width=Cast<UEditableTextBox>(ScreenWidget->FindScreenWidget(TEXT("Txt_RoomWidth")));
		auto* LengthField=Cast<UEditableTextBox>(ScreenWidget->FindScreenWidget(TEXT("Txt_RoomLength")));
		if (!Width||!LengthField) return false;
		auto* Length=bVertical?LengthField:Width; auto* Zero=bVertical?Width:LengthField;
		TestTrue(TEXT("zero dimension stays read-only"),Zero->GetIsReadOnly());
		Type(Length,TEXT("3")); Length->OnTextCommitted.Broadcast(Length->GetText(),ETextCommit::OnEnter);
		FVector2D Pos,Size; PC->GetSelectedRoomBounds(Pos,Size);
		TestTrue(TEXT("Enter applies wall length and keeps the zero dimension"),Size.Equals(bVertical?FVector2D(0,300):FVector2D(300,0),0.01));
		Type(Length,TEXT("2foo")); Length->OnTextCommitted.Broadcast(Length->GetText(),ETextCommit::OnUserMovedFocus);
		FVector2D After; PC->GetSelectedRoomBounds(Pos,After); TestTrue(TEXT("trailing text is rejected"),After.Equals(Size,0.01));
		Type(Length,TEXT("0.1")); Length->OnTextCommitted.Broadcast(Length->GetText(),ETextCommit::OnEnter);
		PC->GetSelectedRoomBounds(Pos,After); TestTrue(TEXT("below-minimum dimensions are rejected"),After.Equals(Size,0.01));
		Type(Length,TEXT("2.5")); Length->OnTextCommitted.Broadcast(Length->GetText(),ETextCommit::OnUserMovedFocus);
		PC->GetSelectedRoomBounds(Pos,After); TestTrue(TEXT("leaving the field applies length"),FMath::IsNearlyEqual(After.GetMax(),250.0,0.01));
		PC->SetRoomResizeStepMetres(100.f); PC->ShrinkSelectedRoom(); PC->GetSelectedRoomBounds(Pos,After);
		TestTrue(TEXT("shrink stops at half a metre"),FMath::IsNearlyEqual(After.GetMax(),50.0,0.01));
		TestEqual(TEXT("straight wall has endpoint handles"),FEditorFeedbackTestAccess::Handles(Plan(F,PC)).Num(),2);
		PC->OnSelectedRoomChanged.Remove(Selected);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorFeedbackDimensionRecovery,"InNavi.EditorUX.Feedback.DimensionRecoveryAndPrecision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEditorFeedbackDimensionRecovery::RunTest(const FString& Parameters)
{
	using namespace EditorFeedbackTests;
	FNavigationSearchFixture F; Prepare(F); auto* PC=F.MakeController(); FString Error;
	if (!TestNotNull(TEXT("room"),F.Interior->CreateRoomFromFootprint({{400,400},{800.123456789,400},{800.123456789,699.123456789},{400,699.123456789}},TEXT("Fields"),TEXT("Original"),true,false,false,Error))) return false;
	auto* W=Screen(PC); FScriptDelegate Selected; Selected.BindUFunction(W,TEXT("HandleSelectedRoomChanged")); PC->OnSelectedRoomChanged.AddUnique(Selected);
	PC->ToggleSelectMode(); PC->SelectRoom(TEXT("Fields"));
	auto* Width=Cast<UEditableTextBox>(W->FindScreenWidget(TEXT("Txt_RoomWidth")));
	auto* Length=Cast<UEditableTextBox>(W->FindScreenWidget(TEXT("Txt_RoomLength")));
	auto* Label=Cast<UTextBlock>(W->FindScreenWidget(TEXT("Txt_RoomLengthLabel")));
	auto* Name=Cast<UEditableTextBox>(W->FindScreenWidget(TEXT("Txt_RoomName")));
	auto* Tags=Cast<UEditableTextBox>(W->FindScreenWidget(TEXT("Txt_RoomTags")));
	auto* TypeBox=Cast<UComboBoxString>(W->FindScreenWidget(TEXT("Cmb_RoomType")));
	auto* Save=Cast<UButton>(W->FindScreenWidget(TEXT("Btn_ApplyRoom")));
	if (!Width||!Length||!Label||!Name||!Tags||!TypeBox||!Save) return false;
	TestEqual(TEXT("vertical dimension is labelled length"),Label->GetText().ToString(),FString(TEXT("LENGTH (m)")));
	TestFalse(TEXT("Enter retains width focus for corrections"),Width->GetClearKeyboardFocusOnCommit());
	TestFalse(TEXT("Enter retains length focus for corrections"),Length->GetClearKeyboardFocusOnCommit());
	const FString OriginalWidth=Width->GetText().ToString(), OriginalLength=Length->GetText().ToString();
	const TArray<FVector2D> Original=F.Interior->GetRoomById(TEXT("Fields"))->GetFootprint();
	for (auto* Box : {Width,Length})
	{
		Type(Box,TEXT("ad")); Box->OnTextCommitted.Broadcast(Box->GetText(),ETextCommit::OnEnter);
		TestEqual(TEXT("Enter retains invalid text for correction"),Box->GetText().ToString(),FString(TEXT("ad")));
		Box->OnTextCommitted.Broadcast(Box->GetText(),ETextCommit::OnUserMovedFocus);
		PC->SelectRoom(TEXT("Fields"));
		TestEqual(TEXT("failed blur restores width on the same room"),Width->GetText().ToString(),OriginalWidth);
		TestEqual(TEXT("failed blur restores length on the same room"),Length->GetText().ToString(),OriginalLength);
		TestTrue(TEXT("invalid commit leaves geometry unchanged"),F.Interior->GetRoomById(TEXT("Fields"))->GetFootprint()==Original);
	}
	// A failing size must not swallow the other properties from the same Save click.
	Type(Width,TEXT("ad")); Name->SetText(FText::FromString(TEXT("Renamed"))); Tags->SetText(FText::FromString(TEXT("follow-up, precision")));
	const FString NewType=TypeBox->GetOptionAtIndex(1); TypeBox->SetSelectedOption(NewType); Save->OnClicked.Broadcast();
	TestEqual(TEXT("name saves despite invalid size"),PC->GetSelectedRoomName(),FString(TEXT("Renamed")));
	TestTrue(TEXT("tags save despite invalid size"),PC->GetSelectedRoomTags().Contains(TEXT("precision")));
	TestEqual(TEXT("type saves despite invalid size"),PC->GetSelectedRoomType(),NewType);
	FVector2D Position, Before, After; PC->GetSelectedRoomBounds(Position,Before);
	TestTrue(TEXT("the displayed length has less precision than geometry"),FMath::Abs(FCString::Atod(*OriginalLength)*100.0-Before.Y)>1e-8);
	Type(Width,TEXT("4.5")); Width->OnTextCommitted.Broadcast(Width->GetText(),ETextCommit::OnEnter); PC->GetSelectedRoomBounds(Position,After);
	TestTrue(TEXT("width-only commit preserves exact length"),FMath::IsNearlyEqual(After.Y,Before.Y,1e-8));
	Before=After; Type(Length,TEXT("3.5")); Length->OnTextCommitted.Broadcast(Length->GetText(),ETextCommit::OnUserMovedFocus); PC->GetSelectedRoomBounds(Position,After);
	TestTrue(TEXT("length-only commit preserves exact width"),FMath::IsNearlyEqual(After.X,Before.X,1e-8));
	// A valid resize also triggers refreshes: read all metadata before attempting it.
	Type(Width,TEXT("4.75")); Name->SetText(FText::FromString(TEXT("Resize and rename"))); Tags->SetText(FText::FromString(TEXT("together"))); Save->OnClicked.Broadcast();
	TestEqual(TEXT("name survives successful resize refresh"),PC->GetSelectedRoomName(),FString(TEXT("Resize and rename")));
	TestTrue(TEXT("tags survive successful resize refresh"),PC->GetSelectedRoomTags().Contains(TEXT("together")));
	PC->OnSelectedRoomChanged.Remove(Selected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorFeedbackDimensionFocus,"InNavi.EditorUX.Rendered.DimensionFieldFocus",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEditorFeedbackDimensionFocus::RunTest(const FString& Parameters)
{
	if (FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))) { AddWarning(TEXT("Run with rendering enabled to check native field focus events.")); return true; }
	using namespace EditorFeedbackTests;
	FNavigationSearchFixture F; Prepare(F); auto* PC=F.MakeController(); auto* W=Screen(PC); FString Error;
	if (!TestNotNull(TEXT("room"),F.Interior->CreateRoomFromFootprint({{400,400},{600,400},{600,600},{400,600}},TEXT("Focus"),TEXT("Focus"),true,false,false,Error))) return false;
	FScriptDelegate Selected; Selected.BindUFunction(W,TEXT("HandleSelectedRoomChanged")); PC->OnSelectedRoomChanged.AddUnique(Selected);
	PC->ToggleSelectMode(); PC->SelectRoom(TEXT("Focus"));
	auto* Width=Cast<UEditableTextBox>(W->FindScreenWidget(TEXT("Txt_RoomWidth")));
	auto* Length=Cast<UEditableTextBox>(W->FindScreenWidget(TEXT("Txt_RoomLength")));
	if (!Width||!Length) return false;
	auto Host=SNew(SWindow).Title(FText::FromString(TEXT("Dimension field verification"))).ClientSize(FVector2D(600,200))
		[SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Width->TakeWidget()]+SVerticalBox::Slot().AutoHeight()[Length->TakeWidget()]];
	FSlateApplication::Get().AddWindow(Host,true); SlateFrames();
	for (auto* Box : {Width,Length})
	{
		const FString RealSize=Box->GetText().ToString();
		FSlateApplication::Get().SetKeyboardFocus(Box->TakeWidget(),EFocusCause::Mouse); SlateFrames(); Type(Box,TEXT("ad"));
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0)); SlateFrames();
		TestEqual(TEXT("native Enter keeps invalid text"),Box->GetText().ToString(),FString(TEXT("ad")));
		auto* Other=Box==Width?Length:Width;
		FSlateApplication::Get().SetKeyboardFocus(Other->TakeWidget(),EFocusCause::Mouse); SlateFrames();
		TestEqual(TEXT("native focus loss restores real size"),Box->GetText().ToString(),RealSize);
	}
	FSlateApplication::Get().RequestDestroyWindow(Host); SlateFrames();
	PC->OnSelectedRoomChanged.Remove(Selected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorFeedbackDrawing,"InNavi.EditorUX.Feedback.IncrementalDrawingAndLoops",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEditorFeedbackDrawing::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F; EditorFeedbackTests::Prepare(F); auto* PC=F.MakeController(); if (PC->IsSnapEnabled()) PC->ToggleSnapEnabled();
	PC->ToggleDrawWallMode(); PC->AddRoomPointAtFootprintPosition({100,100}); PC->AddRoomPointAtFootprintPosition({200,200});
	PC->AddRoomPointAtFootprintPosition({-500,-500}); TestEqual(TEXT("stray click preserves both points"),PC->GetPendingRoomFootprint().Num(),2);
	PC->AddRoomPointAtFootprintPosition({100,200}); PC->AddRoomPointAtFootprintPosition({200,100});
	TestTrue(TEXT("crossing is rejected as soon as its new point is clicked"),PC->GetPendingRoomFootprint().IsEmpty());
	for (FVector2D P:TArray<FVector2D>{{100,100},{200,100},{200,200},{100,200},{100,100}}) PC->AddRoomPointAtFootprintPosition(P);
	TestEqual(TEXT("closing endpoint is accepted"),PC->GetPendingRoomFootprint().Num(),5);
	TestNotNull(TEXT("closed wall loop can be finished"),PC->FinishWall());
	PC->AddRoomPointAtFootprintPosition({150,50}); PC->AddRoomPointAtFootprintPosition({150,150});
	TestTrue(TEXT("existing-object overlap is caught before Finish"),PC->GetPendingRoomFootprint().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorFeedbackAnchoredResize,"InNavi.EditorUX.Feedback.AnchoredResizeAndPreviewCost",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEditorFeedbackAnchoredResize::RunTest(const FString& Parameters)
{
	using namespace EditorFeedbackTests;
	for (int32 Index=0; Index<8; ++Index)
	{
		FNavigationSearchFixture F; Prepare(F); auto* PC=F.MakeController(); FString Error;
		TArray<FVector2D> Outline;
		for (int32 I=0; I<128; ++I) { const double Angle=2.0*PI*I/128; Outline.Add(FVector2D(500,500)+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*150); }
		if (!F.Interior->CreateRoomFromFootprint(Outline,TEXT("Large"),TEXT("Large"),true,false,false,Error)) return false;
		PC->ToggleSelectMode(); PC->SelectRoom(TEXT("Large")); auto* W=Plan(F,PC); const auto H=FEditorFeedbackTestAccess::Handles(W);
		if (H.Num()!=8) return false;
		const FVector2D Direction=(H[Index]-(H[0]+H[4])*0.5).GetSafeNormal(); const FVector2D End=H[Index]+Direction*30;
		const FBox2D Before(Outline); FEditorFeedbackTestAccess::Down(W,H[Index]); const double Start=FPlatformTime::Seconds();
		for (int32 I=0; I<2000; ++I) FEditorFeedbackTestAccess::Move(W,H[Index]+Direction*(I%30));
		AddInfo(FString::Printf(TEXT("Handle %d, 128-point room, 2000 preview moves: %.2f ms"),Index,(FPlatformTime::Seconds()-Start)*1000));
		TestTrue(TEXT("preview moves do not rebuild or mutate room geometry"),F.Interior->GetRoomById(TEXT("Large"))->GetFootprint()==Outline);
		FEditorFeedbackTestAccess::Up(W,End); const FBox2D After(F.Interior->GetRoomById(TEXT("Large"))->GetFootprint());
		TestTrue(TEXT("opposite horizontal side stays fixed"),Direction.X<0?FMath::IsNearlyEqual(After.Max.X,Before.Max.X,0.01):FMath::IsNearlyEqual(After.Min.X,Before.Min.X,0.01));
		TestTrue(TEXT("opposite vertical side stays fixed"),Direction.Y<0?FMath::IsNearlyEqual(After.Max.Y,Before.Max.Y,0.01):FMath::IsNearlyEqual(After.Min.Y,Before.Min.Y,0.01));
		F.Interior->Undo(); TestTrue(TEXT("one undo restores the entire resize"),F.Interior->GetRoomById(TEXT("Large"))->GetFootprint()==Outline);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorFeedbackSearch,"InNavi.EditorUX.Feedback.SearchRefreshAndNumbering",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEditorFeedbackSearch::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F; auto* PC=F.MakeController(); auto* W=EditorFeedbackTests::Screen(PC);
	auto* C=Cast<UNavigationPlaceCombo>(W->FindScreenWidget(TEXT("Cbx_NavPlace1")));
	if (!C) return false; C->TakeWidget(); C->SetSearchText(TEXT("E2"));
	W->ProcessEvent(W->FindFunctionChecked(TEXT("HandleNavigationStateChanged")),nullptr);
	TestEqual(TEXT("refresh preserves the search query"),C->GetSearchText(),FString(TEXT("E2")));
	const auto Options=F.Nav->GetSelectableRooms(); if (Options.Num()<3) return false;
	TestTrue(TEXT("start"),F.Nav->SetStartToRoom(Options[0].GetDisplayLabel()));
	TestTrue(TEXT("destination"),F.Nav->SetDestinationToRoom(Options[1].GetDisplayLabel()));
	TestTrue(TEXT("new place appends"),F.Nav->AddPlaceToRoom(Options[2].GetDisplayLabel()));
	W->ProcessEvent(W->FindFunctionChecked(TEXT("HandleNavigationStateChanged")),nullptr);
	auto* One=Cast<UTextBlock>(W->FindScreenWidget(TEXT("Txt_NavPlaceRole1")));
	auto* Two=Cast<UTextBlock>(W->FindScreenWidget(TEXT("Txt_NavPlaceRole2")));
	TestTrue(TEXT("previous destination row is numbered 1"),One&&One->GetText().ToString()==TEXT("1"));
	TestTrue(TEXT("new destination row is numbered 2"),Two&&Two->GetText().ToString()==TEXT("2"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorFeedbackAudio,"InNavi.EditorUX.Feedback.ErrorSoundMuteState",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEditorFeedbackAudio::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F; auto* PC=F.MakeController(); auto* W=EditorFeedbackTests::Screen(PC); auto* Panel=W->GetAudioPanelForTests();
	if (!Panel) return false; PC->SetAllUiSoundsEnabled(false); Panel->Refresh();
	auto* Error=Cast<UCheckBox>(Panel->FindContentWidget(TEXT("Chk_AudioErrorSound")));
	auto* Mute=Cast<UCheckBox>(Panel->FindContentWidget(TEXT("Chk_AudioMuteAll")));
	if (!Error||!Mute) return false; TestTrue(TEXT("Mute All includes error sound"),Mute->IsChecked()&&!Error->IsChecked());
	Error->OnCheckStateChanged.Broadcast(true);
	TestTrue(TEXT("error can be enabled independently"),PC->IsActionFailureSoundEnabled()&&!PC->IsButtonClickSoundEnabled()&&!PC->AreAllUiSoundsMuted()&&!Mute->IsChecked());
	auto* Exterior=NewObject<UEditScreenWidget>(); Exterior->Initialize(); Exterior->TakeWidget(); Exterior->SetOwningPlayer(PC);
	auto* Other=Exterior->GetAudioPanelForTests(); if (!Other) return false; Other->Refresh();
	auto* OtherError=Cast<UCheckBox>(Other->FindContentWidget(TEXT("Chk_AudioErrorSound")));
	TestTrue(TEXT("error setting survives switching screens"),OtherError&&OtherError->IsChecked());
	TestFalse(TEXT("CDO has no failure-sound binding"),GetDefault<ANavPlayerController>()->OnActionFailed.IsBound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorFeedbackLiveAudio,"InNavi.EditorUX.Live.ErrorAudio",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FEditorFeedbackLiveAudio::RunTest(const FString& Parameters)
{
	UWorld* World=nullptr;
	for (const FWorldContext& C:GEngine->GetWorldContexts()) if (C.WorldType==EWorldType::Game&&C.World()) { World=C.World(); break; }
	auto* PC=World?Cast<ANavPlayerController>(World->GetFirstPlayerController()):nullptr;
	if (!TestNotNull(TEXT("live controller"),PC)) return false;
	TestTrue(TEXT("BeginPlay binds the actual controller"),PC->OnActionFailed.GetAllObjects().Contains(PC));
	PC->SetAllUiSoundsEnabled(false);
	PC->OnActionFailed.Broadcast(TEXT("Muted error probe"),EActionFailureSeverity::Warning);
	PC->SetActionFailureSoundEnabled(true);
	PC->OnActionFailed.Broadcast(TEXT("Error sound probe"),EActionFailureSeverity::Warning);
	auto* Owner=FEditorFeedbackTestAccess::AudioOwner(PC);
	if (!TestNotNull(TEXT("enabled error event starts audio"),Owner)) return false;
	TArray<UAudioComponent*> Audio; Owner->GetComponents(Audio);
	TestTrue(TEXT("error audio is playing"),!Audio.IsEmpty()&&Audio.Last()->IsPlaying());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorFeedbackLiveSearch,"InNavi.EditorUX.Rendered.SearchPopupRefreshAndMouse",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEditorFeedbackLiveSearch::RunTest(const FString& Parameters)
{
	if (FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))) { AddWarning(TEXT("Run this test with rendering enabled to check native mouse input.")); return true; }
	using namespace EditorFeedbackTests;
	for (bool bHighlight : {false,true})
	{
		FNavigationSearchFixture F; auto* PC=F.MakeController(); auto* W=Screen(PC);
		auto* C=Cast<UNavigationPlaceCombo>(W->FindScreenWidget(TEXT("Cbx_NavPlace1"))); if (!C) return false;
		auto ComboWidget=C->TakeWidget();
		auto Host=SNew(SWindow).Title(FText::FromString(TEXT("Navigation search verification"))).ClientSize(FVector2D(600,400))[ComboWidget];
		FSlateApplication::Get().AddWindow(Host,true); FEditorFeedbackTestAccess::Open(C); SlateFrames();
		const auto Options=F.Nav->GetSelectableRooms(); if (Options.IsEmpty()) return false;
		const FString Query=Options[0].RoomId; C->SetSearchText(Query);
		F.Interior->SetRoomName(Options[0].RoomId,TEXT("Renamed during search"));
		W->ProcessEvent(W->FindFunctionChecked(TEXT("HandleNavigationStateChanged")),nullptr); SlateFrames();
		TestTrue(TEXT("refresh keeps the menu open"),FEditorFeedbackTestAccess::IsOpen(C));
		TestEqual(TEXT("refresh keeps the visible query"),FEditorFeedbackTestAccess::PopupQuery(C),Query);
		// Exercise the actual result rows with native Slate input, including a preselected row.
		auto List=FEditorFeedbackTestAccess::Results(C); C->SetSearchText(Query);
		if (!TestTrue(TEXT("matching result exists"),C->GetFilteredOptionCount()>0)) return false;
		FEditorFeedbackTestAccess::Close(C); Host->SetContent(List.ToSharedRef()); SlateFrames(); auto Item=FEditorFeedbackTestAccess::First(C);
		if (bHighlight) { List->SetSelection(Item,ESelectInfo::OnNavigation); SlateFrames(); }
		auto Row=List->WidgetFromItem(Item);
		if (!TestTrue(TEXT("rendered result exists"),Row.IsValid())) return false;
		const auto G=Row->AsWidget()->GetCachedGeometry(); const FVector2D At=G.LocalToAbsolute(G.GetLocalSize()*0.5);
		const FPointerEvent Down(FSlateApplication::CursorPointerIndex,At,At,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
		const FPointerEvent Up(FSlateApplication::CursorPointerIndex,At,At,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState());
		FSlateApplication::Get().ProcessMouseButtonDownEvent(Host->GetNativeWindow(),Down); FSlateApplication::Get().ProcessMouseButtonUpEvent(Up);
		TestTrue(bHighlight?TEXT("click commits a keyboard-highlighted result"):TEXT("click commits an unhighlighted result"),F.Nav->HasDestination());
		if (!F.Nav->HasDestination()) AddInfo(FString::Printf(TEXT("Navigation status after click: %s"),*F.Nav->GetDisplayStatusMessage()));
		FSlateApplication::Get().RequestDestroyWindow(Host); SlateFrames();
	}
	return true;
}

#endif
