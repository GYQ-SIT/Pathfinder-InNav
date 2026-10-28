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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteriorInspectionRotationTest, "InNavi.Interior.Inspection.FixedBottomPivotAndRestore",
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
	TestTrue(TEXT("pivot includes the building's non-zero offset and is on its bottom slab"), Pivot->GetActorLocation().Equals(Anchor));
	TestEqual(TEXT("lowest floor temporarily skipped"), F.Controller->GetActiveFloor(), 2);
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
	TestTrue(TEXT("floor change never moves the bottom pivot"), Pivot->GetActorLocation().Equals(Anchor));
	F.Controller->SetActiveFloor(1);
	TestEqual(TEXT("lowest floor cannot be selected during inspection"), F.Controller->GetActiveFloor(), 3);
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
			TestEqual(TEXT("only active floor rooms visible"), !Room->IsHidden(), Level == 2);
			TestEqual(TEXT("hidden room collision matches visibility"), Room->GetActorEnableCollision(), Level == 2);
		}
		TestEqual(TEXT("only active floor outer walls visible"), Mesh->GetProcMeshSection(F.Building->GetFloorCount() + Level)->bSectionVisible, Level == 2);
	}
	TestTrue(TEXT("active floor keeps its own slab"), Mesh->GetProcMeshSection(2)->bSectionVisible);
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
	F.Controller->RotateInspectionBy(70, 30);
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
	TestFalse(TEXT("one storey cannot be inspected after hiding its lowest floor"), F.Controller->CanInspectBuilding());
	F.Controller->SetInteriorInspectionEnabled(true);
	TestFalse(TEXT("no mode entered"), F.Controller->IsInteriorInspectionEnabled());
	TestNull(TEXT("no pivot created"), F.Controller->GetInspectionPivot());
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>();
	Screen->Initialize(); Screen->SetOwningPlayer(F.Controller); Screen->TakeWidget();
	bool bEnabled = false;
	Screen->ProcessEvent(Screen->FindFunctionChecked(TEXT("HandleInspectionModeChanged")), &bEnabled);
	UButton* Button = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_InspectionToggle")));
	if (!TestNotNull(TEXT("inspection toggle exists"), Button)) return false;
	TestFalse(TEXT("toggle disabled for one-storey building"), Button->GetIsEnabled());
	TestEqual(TEXT("toggle shown in 3D"), Button->GetVisibility(), ESlateVisibility::Visible);
	F.Controller->SetIn3DPreviewMode(false);
	Screen->ProcessEvent(Screen->FindFunctionChecked(TEXT("HandleInspectionModeChanged")), &bEnabled);
	TestEqual(TEXT("toggle hidden in 2D"), Button->GetVisibility(), ESlateVisibility::Collapsed);
	return true;
}

#endif
