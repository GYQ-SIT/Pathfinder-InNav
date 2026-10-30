#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "InteriorBuildingPreviewPivot.h"
#include "InteriorRoom.h"
#include "InteriorConnectorMarker.h"
#include "InteriorScreenWidget.h"
#include "NavigationRouteVisualization.h"
#include "PolyBuilding.h"
#include "Components/Button.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/DefaultPawn.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/AutomationTest.h"

namespace InteriorInspectionTests
{
	struct FFixture : FNavigationSearchFixture
	{
		ANavPlayerController* Controller = MakeController();
		APolyBuilding* Building = nullptr;
		AStaticMeshActor* Ground = nullptr;
		FFixture(int32 Floors = 3)
		{
			Controller->Possess(World->SpawnActor<ADefaultPawn>());
			Building = Generator->SpawnBuildingFromData(TEXT("PREVIEW"), TEXT("Preview"),
				{ {0,0}, {100,0}, {100,80}, {0,80} }, Floors, 300.f, FVector2D(2500,4000));
			Controller->LoadInteriorForBuilding(TEXT("PREVIEW"));
			for (int32 Level = 1; Level <= Floors; ++Level)
			{
				Interior->SetActiveFloor(Level);
				FString Error;
				Interior->CreateRoomFromFootprint({ {10,10}, {35,10}, {35,35}, {10,35} },
					FString::Printf(TEXT("ROOM%d"), Level), TEXT("Room"), true, false, false, Error);
			}
			TArray<int32> Levels = Interior->GetFloorLevels();
			Interior->PlaceConnectorNode(EInteriorNavNodeType::Elevator, FVector2D(70,50), Levels);
			Controller->SetActiveFloor(1);
			Controller->SetIn3DPreviewMode(true);
			Ground = World->SpawnActor<AStaticMeshActor>();
			Ground->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/MapTemplates/SM_Template_Map_Floor.SM_Template_Map_Floor")));
		}
		FString InteriorJson() const
		{
			FString Json;
			FJsonSerializer::Serialize(Interior->CaptureInteriorSnapshot(), TJsonWriterFactory<>::Create(&Json));
			return Json;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteriorInspectionRotationTest, "InNavi.Interior.Inspection.PivotAndRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteriorInspectionRotationTest::RunTest(const FString& Parameters)
{
	InteriorInspectionTests::FFixture F;
	F.Controller->SetIn3DEditMode(true);
	const FString Before = F.InteriorJson();
	const bool bDirtyBefore = F.Controller->HasUnsavedChanges();
	const FTransform CameraBefore = F.Controller->GetPawn()->GetActorTransform();
	const FRotator ControlBefore = F.Controller->GetControlRotation();
	const FTransform BuildingBefore = F.Building->GetActorTransform();
	AInteriorRoom* Room = F.Interior->GetRoomsOnFloor(2)[0];
	const FTransform RoomBefore = Room->GetActorTransform();
	F.Controller->SetInteriorInspectionEnabled(true);
	AInteriorBuildingPreviewPivot* Pivot = F.Controller->GetInspectionPivot();
	if (!TestNotNull(TEXT("inspection pivot created"), Pivot)) return false;
	const FVector Anchor(25500,40400,0);
	TestTrue(TEXT("at first the pivot is the centre of the lowest floor, with the building's non-zero offset"), Pivot->GetActorLocation().Equals(Anchor));
	TestEqual(TEXT("the first floor is kept"), F.Controller->GetActiveFloor(), 1);
	TestFalse(TEXT("editing suspended"), F.Controller->IsIn3DEditMode());
	F.Controller->RotateInspectionBy(95, 35);
	const FQuat Rotated = Pivot->GetActorQuat();
	const FVector Point(25300,40250,450);
	const FVector Expected = Anchor + Rotated.RotateVector(Point - Anchor);
	TestTrue(TEXT("model rotation is around its bottom, not the world origin"), F.Building->GetActorTransform().TransformPosition(Point).Equals(Expected, 0.01));
	TestTrue(TEXT("rooms rotate with the shell"), Room->GetActorTransform().TransformPosition(Point).Equals(Expected, 0.01));
	for (const FInteriorNavNode& Node : F.Interior->GetNavNodes())
	{
		if (Node.MarkerActor) TestTrue(TEXT("every connector segment rotates with the shell"), Node.MarkerActor->GetActorTransform().TransformPosition(Point).Equals(Expected, 0.01));
	}
	F.Controller->SetActiveFloor(3);
	TestTrue(TEXT("floor change preserves the rotation"), Pivot->GetActorQuat().Equals(Rotated));
	TestTrue(TEXT("floor change never moves the pivot"), Pivot->GetActorLocation().Equals(Anchor));
	F.Controller->SetActiveFloor(1);
	TestEqual(TEXT("the first floor can be chosen again during inspection"), F.Controller->GetActiveFloor(), 1);

	// A pan slides the model across the screen and leaves the turn and the camera alone; the model then turns about its own (moved) centre.
	const FVector Shown = F.Building->GetActorTransform().TransformPosition(Point);
	const FTransform CameraBeforePan = F.Controller->GetPawn()->GetActorTransform();
	F.Controller->PanInspectionBy(FVector2D(100, -40));
	const FVector Moved = F.Building->GetActorTransform().TransformPosition(Point) - Shown;
	TestFalse(TEXT("the model has moved"), Moved.IsNearlyZero(1.0));
	TestTrue(TEXT("by the same distance as its centre"), (Pivot->GetActorLocation() - Anchor).Equals(Moved, 0.05));
	TestTrue(TEXT("the rooms went with it"), Room->GetActorTransform().TransformPosition(Point).Equals(Shown + Moved, 0.05));
	const FVector RightOfView = FRotationMatrix(FRotator(-25.f, -45.f, 0.f)).GetScaledAxis(EAxis::Y);
	const FVector UpOfView = FRotationMatrix(FRotator(-25.f, -45.f, 0.f)).GetScaledAxis(EAxis::Z);
	TestTrue(TEXT("to the right and up on the screen, as dragged, and not towards or away from the camera"),
		(Moved | RightOfView) > 0.0 && (Moved | UpOfView) > 0.0 && FMath::Abs(Moved | FRotationMatrix(FRotator(-25.f, -45.f, 0.f)).GetScaledAxis(EAxis::X)) < 0.05);
	TestTrue(TEXT("the turn is the same"), Pivot->GetActorQuat().Equals(Rotated));
	TestTrue(TEXT("and the camera has not moved"), F.Controller->GetPawn()->GetActorTransform().Equals(CameraBeforePan));
	F.Controller->RotateInspectionBy(30, 0);
	const FQuat Then = Pivot->GetActorQuat();
	TestTrue(TEXT("it turns about its own centre, where that is now"), F.Building->GetActorTransform().TransformPosition(Point)
		.Equals(Pivot->GetActorLocation() + (Then * Rotated.Inverse()).RotateVector(Shown + Moved - Pivot->GetActorLocation()), 0.05));
	TestTrue(TEXT("the camera looks at where the model started"), Pivot->GetDefaultPivot().Equals(Anchor));
	for (int32 Pan = 0; Pan < 200; ++Pan) F.Controller->PanInspectionBy(FVector2D(400, 0));
	TestTrue(TEXT("a pan stops before the model is lost"), (Pivot->GetActorLocation() - Anchor).Size() <= Pivot->GetSweepRadius() * 2.5f + 1.f);

	// The tilt has a limit: the model can be looked at from above, never from underneath or upside down.
	F.Controller->RotateInspectionBy(0, 500);
	const double MostTipped = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Pivot->GetActorQuat().RotateVector(FVector::UpVector) | FVector::UpVector, -1.0, 1.0)));
	TestTrue(TEXT("the top tips toward the viewer by at most 60 degrees"), FMath::IsNearlyEqual(MostTipped, 60.0, 0.1));
	F.Controller->RotateInspectionBy(0, -1000);
	const double LeastTipped = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Pivot->GetActorQuat().RotateVector(FVector::UpVector) | FVector::UpVector, -1.0, 1.0)));
	TestTrue(TEXT("and away by at most 15"), FMath::IsNearlyEqual(LeastTipped, 15.0, 0.1));
	F.Controller->RotateInspectionBy(1000, 0);
	TestTrue(TEXT("turning it round does not change the tilt"), FMath::IsNearlyEqual(Pivot->GetTilt(), -15.f, 0.01f));
	F.Controller->ResetCamera3DPreview();
	TestTrue(TEXT("Reset returns the model as it was entered"), F.Building->GetActorTransform().Equals(BuildingBefore) && Room->GetActorTransform().Equals(RoomBefore));
	TestTrue(TEXT("and the centre to its start"), Pivot->GetActorLocation().Equals(Anchor));
	F.Controller->PanInspectionBy(FVector2D(30, 30));
	F.Controller->RotateInspectionBy(20, 10);
	TestEqual(TEXT("inspection never edits serialized data"), F.InteriorJson(), Before);
	TestEqual(TEXT("inspection never dirties the project"), F.Controller->HasUnsavedChanges(), bDirtyBefore);
	F.Controller->SetInteriorInspectionEnabled(false);
	F.Controller->SetInteriorInspectionEnabled(false);
	TestTrue(TEXT("building transform restored"), F.Building->GetActorTransform().Equals(BuildingBefore));
	TestTrue(TEXT("room transform restored"), Room->GetActorTransform().Equals(RoomBefore));
	TestNull(TEXT("temporary attachment removed"), Room->GetAttachParentActor());
	TestTrue(TEXT("camera pose restored exactly"), F.Controller->GetPawn()->GetActorTransform().Equals(CameraBefore));
	TestTrue(TEXT("control rotation restored"), F.Controller->GetControlRotation().Equals(ControlBefore));
	TestEqual(TEXT("original floor restored"), F.Controller->GetActiveFloor(), 1);
	TestTrue(TEXT("original Edit mode restored"), F.Controller->IsIn3DEditMode());
	TestNull(TEXT("no model actor is left"), F.Controller->GetInspectionPivot());
	TestFalse(TEXT("move input restored"), F.Controller->IsMoveInputIgnored());
	TestFalse(TEXT("look input restored"), F.Controller->IsLookInputIgnored());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteriorInspectionVisibilityTest, "InNavi.Interior.Inspection.VisibilityCollisionAndReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteriorInspectionVisibilityTest::RunTest(const FString& Parameters)
{
	InteriorInspectionTests::FFixture F;
	APolyBuilding* Other = F.Generator->FindBuildingById(TEXT("E1"));
	const bool bOtherWasHidden = Other->IsHidden();
	F.Controller->SetInteriorInspectionEnabled(true);
	UProceduralMeshComponent* Mesh = F.Building->FindComponentByClass<UProceduralMeshComponent>();
	TestFalse(TEXT("bottom cap hidden"), Mesh->GetProcMeshSection(0)->bSectionVisible);
	TestFalse(TEXT("hidden bottom cap has no collision"), Mesh->GetProcMeshSection(0)->bEnableCollision);
	for (int32 Level : F.Interior->GetFloorLevels())
	{
		for (AInteriorRoom* Room : F.Interior->GetRoomsOnFloor(Level))
		{
			TestEqual(TEXT("only active floor rooms visible"), !Room->IsHidden(), Level == 1);
			TestEqual(TEXT("hidden room collision matches visibility"), Room->GetActorEnableCollision(), Level == 1);
		}
		TestEqual(TEXT("only active floor outer walls visible"), Mesh->GetProcMeshSection(F.Building->GetFloorCount() + Level)->bSectionVisible, Level == 1);
	}
	// The first floor stands on a slab of its own now that the bottom platform is gone; the floors above keep theirs.
	const int32 FirstFloorSlab = F.Building->GetInspectionFloorSectionIndex();
	TestTrue(TEXT("the first floor has a slab of its own"), Mesh->GetProcMeshSection(FirstFloorSlab) && Mesh->GetProcMeshSection(FirstFloorSlab)->bSectionVisible);
	TestTrue(TEXT("which can be stood on"), Mesh->GetProcMeshSection(FirstFloorSlab)->bEnableCollision);
	TestFalse(TEXT("the slab at the base of the second floor is not shown with the first"), Mesh->GetProcMeshSection(2)->bSectionVisible);
	TestFalse(TEXT("and the bottom platform is still hidden"), Mesh->GetProcMeshSection(0)->bSectionVisible);
	F.Controller->SetActiveFloor(2);
	TestTrue(TEXT("the second floor keeps its own slab"), Mesh->GetProcMeshSection(2)->bSectionVisible);
	TestFalse(TEXT("and the first floor's slab goes"), Mesh->GetProcMeshSection(FirstFloorSlab)->bSectionVisible);
	F.Controller->SetActiveFloor(1);
	TestTrue(TEXT("scene ground hidden"), F.Ground->IsHidden());
	TestFalse(TEXT("scene ground collision disabled"), F.Ground->GetActorEnableCollision());
	const FTransform GroundBefore = F.Ground->GetActorTransform();
	F.Generator->UpdateGroundToVisible();
	TestTrue(TEXT("ground fitting suspended while the model is transformed"), F.Ground->GetActorTransform().Equals(GroundBefore));
	Other->SetActorHiddenInGame(false);
	F.Controller->GetInspectionPivot()->ApplyIsolation();
	TestTrue(TEXT("isolation survives an external visibility refresh"), Other->IsHidden());
	TestEqual(TEXT("add floor blocked"), F.Controller->AddFloor(), -1);
	TestFalse(TEXT("delete floor blocked"), F.Controller->RemoveFloor(2));
	TestFalse(TEXT("delete room blocked"), F.Controller->DeleteRoom(TEXT("ROOM2")));
	TestFalse(TEXT("undo blocked"), F.Controller->UndoInteriorEdit());
	TestFalse(TEXT("redo blocked"), F.Controller->RedoInteriorEdit());
	F.Controller->SetIn3DEditMode(true);
	TestFalse(TEXT("cannot enter Edit during inspection"), F.Controller->IsIn3DEditMode());
	F.Controller->StartRotateCCW(); F.Controller->StopRotateCCW();
	F.Controller->SetInteriorInspectionEnabled(false);
	TestTrue(TEXT("bottom cap restored"), Mesh->GetProcMeshSection(0)->bSectionVisible);
	TestTrue(TEXT("bottom cap collision restored"), Mesh->GetProcMeshSection(0)->bEnableCollision);
	TestTrue(TEXT("the first floor's own slab is gone"), Mesh->GetNumSections() <= FirstFloorSlab || !Mesh->GetProcMeshSection(FirstFloorSlab) || Mesh->GetProcMeshSection(FirstFloorSlab)->ProcVertexBuffer.IsEmpty());
	TestFalse(TEXT("ground fitting resumed"), F.Generator->IsGroundFitSuspended());
	TestFalse(TEXT("ground visibility restored"), F.Ground->IsHidden());
	TestEqual(TEXT("neighbour's previous visibility restored"), Other->IsHidden(), bOtherWasHidden);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteriorInspectionRouteTest, "InNavi.Interior.Inspection.RouteDisplayTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteriorInspectionRouteTest::RunTest(const FString& Parameters)
{
	InteriorInspectionTests::FFixture F;
	F.Controller->SetInteriorInspectionEnabled(true);
	F.Controller->SetActiveFloor(2);
	F.Controller->RotateInspectionBy(70, 30);
	// The way a route is shown follows the model wherever it has been moved to and however it has been turned.
	F.Controller->PanInspectionBy(FVector2D(60, 25));
	F.Controller->RotateInspectionBy(25, -10);
	const FTransform Display = F.Controller->GetInspectionPivot()->GetDisplayTransform();
	FNavigation3DView Original;
	Original.Segments.Add({ FVector(2600,4200,315), FVector(2900,4200,315), false });
	Original.Labels.Add({ FVector(2700,4200,370), TEXT("Start"), FColor::Green });
	Original.Stops.Add({ FVector(2800,4200,315), 1, FColor::White });
	Original.Start = Original.Segments[0].From;
	Original.Destination = Original.Segments[0].To;
	Original.Here = FVector(2750,4200,315);
	const FNavigation3DView Rotated = UNavigationRouteVisualization::TransformView(Original, Display);
	TestTrue(TEXT("route segment starts follow the model"), Rotated.Segments[0].From.Equals(Display.TransformPosition(Original.Segments[0].From)));
	TestTrue(TEXT("route segment ends follow the model"), Rotated.Segments[0].To.Equals(Display.TransformPosition(Original.Segments[0].To)));
	TestTrue(TEXT("labels follow the model"), Rotated.Labels[0].Position.Equals(Display.TransformPosition(Original.Labels[0].Position)));
	TestTrue(TEXT("stop markers follow the model"), Rotated.Stops[0].Position.Equals(Display.TransformPosition(Original.Stops[0].Position)));
	TestTrue(TEXT("start marker follows the model"), Rotated.Start->Equals(Display.TransformPosition(Original.Start.GetValue())));
	TestTrue(TEXT("destination marker follows the model"), Rotated.Destination->Equals(Display.TransformPosition(Original.Destination.GetValue())));
	TestTrue(TEXT("guidance marker follows the model"), Rotated.Here->Equals(Display.TransformPosition(Original.Here.GetValue())));
	TestTrue(TEXT("original planner coordinates retained"), Original.Start->Equals(FVector(2600,4200,315)));
	FNavigationWorldRoutePoint A, B;
	A.BuildingId = B.BuildingId = TEXT("PREVIEW");
	A.Floor = B.Floor = 2;
	A.WorldPosition = FVector(25300,40250,300);
	B.WorldPosition = FVector(25500,40250,300);
	TestTrue(TEXT("route import accepted"), F.Nav->ApplyWorldRouteResult(TEXT("PREVIEW"), { A, B }));
	F.Controller->RotateInspectionBy(0, 0); // Refresh through the same path as a viewport drag.
	UNavigationRouteVisualization* Visualization = Cast<UNavigationRouteVisualization>(
		FindFProperty<FObjectPropertyBase>(F.Controller->GetClass(), TEXT("RouteVisualization"))->GetObjectPropertyValue_InContainer(F.Controller));
	if (TestTrue(TEXT("active floor route is drawn"), Visualization && !Visualization->BatchedLines.IsEmpty()))
	{
		TestTrue(TEXT("actual line-batch drawing follows the grouped model"), Visualization->BatchedLines[0].Start.Equals(Display.TransformPosition(A.WorldPosition + FVector(0,0,15)), 0.01));
	}
	TestTrue(TEXT("planner still owns untransformed route coordinates"), F.Nav->GetRoutePoints()[0].WorldPosition.Equals(A.WorldPosition));
	F.Controller->SetActiveFloor(3);
	TestTrue(TEXT("a route on a hidden floor disappears"), Visualization->BatchedLines.IsEmpty());
	F.Controller->ResetCamera3DPreview();
	TestTrue(TEXT("reset returns model to original orientation"), F.Controller->GetInspectionPivot()->GetActorQuat().IsIdentity());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteriorInspectionExitTest, "InNavi.Interior.Inspection.ExitPathsAndInputBalance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteriorInspectionExitTest::RunTest(const FString& Parameters)
{
	InteriorInspectionTests::FFixture F;
	F.Controller->SetIgnoreLookInput(true);
	F.Controller->SetIgnoreMoveInput(true);
	const FTransform Original = F.Building->GetActorTransform();
	F.Controller->SetInteriorInspectionEnabled(true);
	F.Controller->RotateInspectionBy(60, 45);
	F.Controller->SetIn3DPreviewMode(false);
	TestFalse(TEXT("2D transition disables inspection"), F.Controller->IsInteriorInspectionEnabled());
	TestTrue(TEXT("2D transition restores model"), F.Building->GetActorTransform().Equals(Original));
	TestTrue(TEXT("external move suppression retained"), F.Controller->IsMoveInputIgnored());
	TestTrue(TEXT("external look suppression retained"), F.Controller->IsLookInputIgnored());
	F.Controller->SetIgnoreMoveInput(false);
	F.Controller->SetIgnoreLookInput(false);
	F.Controller->SetIn3DPreviewMode(true);
	F.Controller->SetInteriorInspectionEnabled(true);
	F.Controller->RotateInspectionBy(40, 20);
	F.Controller->LoadInteriorForBuilding(TEXT("E2"));
	TestFalse(TEXT("building switch disables inspection"), F.Controller->IsInteriorInspectionEnabled());
	TestTrue(TEXT("building switch restores old shell"), F.Building->GetActorTransform().Equals(Original));
	F.Controller->SetIn3DPreviewMode(true);
	F.Controller->SetInteriorInspectionEnabled(true);
	F.Controller->RestoreExteriorCameraView();
	TestFalse(TEXT("Back disables inspection"), F.Controller->IsInteriorInspectionEnabled());
	TestFalse(TEXT("Back restores input"), F.Controller->IsMoveInputIgnored() || F.Controller->IsLookInputIgnored());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteriorInspectionSingleFloorTest, "InNavi.Interior.Inspection.SingleFloorAndButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteriorInspectionSingleFloorTest::RunTest(const FString& Parameters)
{
	InteriorInspectionTests::FFixture F(1);
	TestTrue(TEXT("one storey can be inspected: its floor is kept"), F.Controller->CanInspectBuilding());
	F.Controller->SetInteriorInspectionEnabled(true);
	TestTrue(TEXT("the mode is entered"), F.Controller->IsInteriorInspectionEnabled());
	TestNotNull(TEXT("with a pivot"), F.Controller->GetInspectionPivot());
	TestEqual(TEXT("showing the only floor"), F.Controller->GetActiveFloor(), 1);
	F.Controller->SetInteriorInspectionEnabled(false);
	TestFalse(TEXT("and left again"), F.Controller->IsInteriorInspectionEnabled());
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>();
	Screen->Initialize(); Screen->SetOwningPlayer(F.Controller); Screen->TakeWidget();
	bool bEnabled = false;
	Screen->ProcessEvent(Screen->FindFunctionChecked(TEXT("HandleInspectionModeChanged")), &bEnabled);
	UButton* Button = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_InspectionToggle")));
	if (!TestNotNull(TEXT("inspection toggle exists"), Button)) return false;
	TestTrue(TEXT("toggle enabled for a one-storey building"), Button->GetIsEnabled());
	// Isolate, Orbit and Grab are one group: Orbit and Grab are the two ways of turning the model, one on and the other off.
	UButton* Orbit = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_InspectionOrbit")));
	UButton* Grab = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_InspectionGrab")));
	if (!TestNotNull(TEXT("Orbit exists"), Orbit) || !TestNotNull(TEXT("Grab exists"), Grab)) return false;
	TestEqual(TEXT("Grab, the model following the mouse, is on to begin with"), F.Controller->GetInspectionRotateStyle(), EInspectionRotateStyle::Grab);
	TestEqual(TEXT("both are shown in 3D"), Orbit->GetVisibility() == ESlateVisibility::Visible && Grab->GetVisibility() == ESlateVisibility::Visible, true);
	TestFalse(TEXT("neither can be used before Isolate is on"), Orbit->GetIsEnabled() || Grab->GetIsEnabled());
	F.Controller->SetInteriorInspectionEnabled(true);
	bEnabled = true;
	Screen->ProcessEvent(Screen->FindFunctionChecked(TEXT("HandleInspectionModeChanged")), &bEnabled);
	TestTrue(TEXT("both can be used once it is on"), Orbit->GetIsEnabled() && Grab->GetIsEnabled());
	Orbit->OnClicked.Broadcast();
	TestEqual(TEXT("Orbit turns Grab off"), F.Controller->GetInspectionRotateStyle(), EInspectionRotateStyle::Orbit);
	Grab->OnClicked.Broadcast();
	TestEqual(TEXT("and Grab turns Orbit off"), F.Controller->GetInspectionRotateStyle(), EInspectionRotateStyle::Grab);
	// Between Colours and View in the switcher bar, framed as one.
	UWidget* Colours = Screen->FindScreenWidget(TEXT("Btn_RoomColours"));
	UWidget* View = Screen->FindScreenWidget(TEXT("Btn_Edit3DModeToggle"));
	UWidget* Frame = Screen->FindScreenWidget(TEXT("InspectionFrame"));
	if (TestTrue(TEXT("Colours, View and the group exist"), Colours && View && Frame) && Colours->GetParent() && Colours->GetParent() == Frame->GetParent() && View->GetParent() == Frame->GetParent())
	{
		const UPanelWidget* Row = Frame->GetParent();
		TestEqual(TEXT("the group is right after Colours"), Row->GetChildIndex(Frame), Row->GetChildIndex(Colours) + 1);
		TestEqual(TEXT("and right before View"), Row->GetChildIndex(View), Row->GetChildIndex(Frame) + 1);
	}
	TestTrue(TEXT("Isolate is inside the group"), Button->GetParent() && Button->GetParent()->GetParent() == Frame);
	F.Controller->SetInteriorInspectionEnabled(false);
	bEnabled = false;
	TestEqual(TEXT("toggle shown in 3D"), Button->GetVisibility(), ESlateVisibility::Visible);
	F.Controller->SetIn3DPreviewMode(false);
	Screen->ProcessEvent(Screen->FindFunctionChecked(TEXT("HandleInspectionModeChanged")), &bEnabled);
	TestEqual(TEXT("toggle hidden in 2D"), Button->GetVisibility(), ESlateVisibility::Collapsed);
	return true;
}

#endif
