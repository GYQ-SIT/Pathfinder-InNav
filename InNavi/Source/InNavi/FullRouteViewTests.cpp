#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "NavigationDockPanelWidget.h"
#include "NavigationRouteVisualization.h"
#include "InteriorRoom.h"
#include "InteriorConnectorMarker.h"
#include "EngineUtils.h"
#include "PolyBuilding.h"
#include "Components/Button.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/DefaultPawn.h"
#include "Misc/AutomationTest.h"

namespace FullRouteViewTests
{
	struct FFixture : FNavigationSearchFixture
	{
		ANavPlayerController* Controller = MakeController();
		APolyBuilding* Building = nullptr;
		FFixture()
		{
			Controller->Possess(World->SpawnActor<ADefaultPawn>());
			Building = Generator->SpawnBuildingFromData(TEXT("ROUTE"), TEXT("Route building"),
				{ {-500,-400}, {500,-400}, {500,400}, {-500,400} }, 30, 300.f, FVector2D(2500,4000));
			Controller->LoadInteriorForBuilding(TEXT("ROUTE"));
			FString Error;
			Interior->CreateRoomFromFootprint({ {10,10}, {35,10}, {35,35}, {10,35} }, TEXT("ROOM"), TEXT("Room"), true, false, false, Error);
			FScriptDelegate Changed;
			Changed.BindUFunction(Controller, TEXT("RefreshRoute3DVisualization"));
			Nav->OnStateChanged.AddUnique(Changed);
			// AActor::ProcessEvent dispatches dynamic delegates only after world initialization.
			World->bActorsInitialized = true;
			Controller->PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
		}
		void MakeRoute()
		{
			FNavigationRouteResult Result;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				FNavigationRoutePoint Point;
				Point.BuildingId = TEXT("ROUTE");
				Point.Position = FVector2D(10 + Index * 20, 20);
				Point.Floor = Index < 2 ? 1 : 3;
				Result.Points.Add(Point);
			}
			Nav->ApplyRouteResult(Result);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFullRouteRestoreTest, "InNavi.Navigation.FullRoute.Restore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFullRouteRestoreTest::RunTest(const FString& Parameters)
{
	FullRouteViewTests::FFixture F;
	TestFalse(TEXT("no route cannot enter overview"), F.Controller->CanShowFullRouteView());
	F.Controller->SetFullRouteViewEnabled(true);
	TestFalse(TEXT("empty route is harmless"), F.Controller->IsFullRouteViewEnabled());
	F.MakeRoute();
	for (bool bStartIn3D : { false, true })
	{
		F.Controller->SetIn3DPreviewMode(bStartIn3D);
		F.Controller->SetIn3DEditMode(bStartIn3D);
		F.Building->SetBuildingOpacity(1.f);
		const FTransform Camera = F.Controller->GetPawn()->GetActorTransform();
		const FRotator Control = F.Controller->GetControlRotation();
		const int32 Level = F.Building->GetVisibleInteriorLevel();
		AInteriorRoom* Room = F.Interior->GetRoomsOnFloor(1)[0];
		const bool bRoomHidden = Room->IsHidden();
		const bool bRoomCollision = Room->GetActorEnableCollision();
		const bool bDirty = F.Controller->HasUnsavedChanges();
		F.Controller->SetFullRouteViewEnabled(true);
		TestTrue(TEXT("overview entered from either 2D or 3D"), F.Controller->IsFullRouteViewEnabled());
		TestTrue(TEXT("interior world visible"), F.Controller->IsIn3DPreviewMode());
		TestFalse(TEXT("read-only"), F.Controller->IsIn3DEditMode());
		TestEqual(TEXT("whole building visible"), F.Building->GetVisibleInteriorLevel(), 0);
		TestTrue(TEXT("the building of the route is not drawn (its floors are pulled apart)"), F.Building->GetBuildingOpacity() < 0.05f);
		// The rooms stay, drawn see-through, so the route is seen against them (they used to be hidden).
		TestFalse(TEXT("rooms stay visible"), Room->IsHidden());
		TestFalse(TEXT("camera moved outside"), F.Controller->GetPawn()->GetActorTransform().Equals(Camera));
		// The camera frames the route, not the whole building, so it may be inside the see-through shell (collision is off).
		TestTrue(TEXT("camera targets the whole route across its floors"),
			F.Controller->GetControlRotation().Vector().Equals((FVector(25400,40200,375) - F.Controller->GetPawn()->GetActorLocation()).GetSafeNormal(), 0.01));
		F.Controller->SetFullRouteViewEnabled(false);
		TestEqual(TEXT("previous dimension restored"), F.Controller->IsIn3DPreviewMode(), bStartIn3D);
		TestEqual(TEXT("previous editing mode restored"), F.Controller->IsIn3DEditMode(), bStartIn3D);
		TestTrue(TEXT("exact camera restored"), F.Controller->GetPawn()->GetActorTransform().Equals(Camera));
		TestTrue(TEXT("control rotation restored"), F.Controller->GetControlRotation().Equals(Control));
		TestEqual(TEXT("temporary camera zoom unlocked again"), F.Controller->PlayerCameraManager->GetLockedFOV(), 0.f);
		TestEqual(TEXT("cutaway restored"), F.Building->GetVisibleInteriorLevel(), Level);
		TestEqual(TEXT("opacity restored"), F.Building->GetBuildingOpacity(), 1.f);
		TestEqual(TEXT("room visibility restored"), Room->IsHidden(), bRoomHidden);
		TestEqual(TEXT("room collision restored"), Room->GetActorEnableCollision(), bRoomCollision);
		TestEqual(TEXT("does not dirty project"), F.Controller->HasUnsavedChanges(), bDirty);
		TestEqual(TEXT("all route points retained"), F.Nav->GetRoutePoints().Num(), 4);
	}
	F.Controller->SetFullRouteViewEnabled(true);
	F.Nav->RequestClearRoute();
	TestFalse(TEXT("clear route restores presentation immediately"), F.Controller->IsFullRouteViewEnabled());
	F.MakeRoute();
	F.Controller->SetFullRouteViewEnabled(true);
	F.Controller->SetActiveFloor(3);
	TestFalse(TEXT("floor change exits overview"), F.Controller->IsFullRouteViewEnabled());
	TestEqual(TEXT("requested floor is selected"), F.Controller->GetActiveFloor(), 3);
	F.Controller->SetFullRouteViewEnabled(true);
	F.Nav->SetRouteGuidanceVisible(false);
	TestFalse(TEXT("hiding route exits overview"), F.Controller->IsFullRouteViewEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFullRouteFloorsTest, "InNavi.Navigation.FullRoute.FloorsPulledApart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFullRouteFloorsTest::RunTest(const FString& Parameters)
{
	FullRouteViewTests::FFixture F;
	// A room on each of the floors 1 (the fixture's own), 2 and 3 (the route goes through 1 and 3 only).
	TArray<AInteriorRoom*> Rooms{ F.Interior->GetRoomsOnFloor(1)[0] };
	for (int32 Floor = 2; Floor <= 3; ++Floor)
	{
		F.Controller->SetActiveFloor(Floor);
		FString Error;
		Rooms.Add(F.Interior->CreateRoomFromFootprint({ {10,10}, {35,10}, {35,35}, {10,35} }, FString::Printf(TEXT("R%d"), Floor), TEXT("Room"), true, false, false, Error));
	}
	if (!TestTrue(TEXT("rooms made"), Rooms.Num() == 3 && Rooms[0] && Rooms[1] && Rooms[2])) return false;
	F.MakeRoute();
	const FVector Floor1 = Rooms[0]->GetActorLocation();
	const FVector Floor3 = Rooms[2]->GetActorLocation();
	const bool bFloor2Hidden = Rooms[1]->IsHidden();
	const bool bFloor3Hidden = Rooms[2]->IsHidden();
	F.Controller->SetFullRouteViewEnabled(true);
	TestTrue(TEXT("overview on"), F.Controller->IsFullRouteViewEnabled());
	TestFalse(TEXT("a floor of the route is shown"), Rooms[0]->IsHidden());
	TestFalse(TEXT("the other floor of the route is shown"), Rooms[2]->IsHidden());
	TestTrue(TEXT("a floor the route does not use is hidden"), Rooms[1]->IsHidden());
	TestEqual(TEXT("the lowest floor stays where it is"), F.Controller->FullRouteFloorShift(TEXT("ROUTE"), 1), 0.f, 0.01f);
	TestTrue(TEXT("the floors are pulled apart"), F.Controller->FullRouteFloorShift(TEXT("ROUTE"), 3) > 0.f);
	TestEqual(TEXT("the upper room moved up by the shift"), float(Rooms[2]->GetActorLocation().Z - Floor3.Z), F.Controller->FullRouteFloorShift(TEXT("ROUTE"), 3), 0.01f);
	TestTrue(TEXT("the floors are pulled apart"), Rooms[2]->GetActorLocation().Z - Rooms[0]->GetActorLocation().Z > 0.f);
	TestEqual(TEXT("the lower room did not move"), Rooms[0]->GetActorLocation().Z, Floor1.Z);
	F.Controller->SetFullRouteViewEnabled(false);
	TestTrue(TEXT("the rooms are back where they were"), Rooms[0]->GetActorLocation().Equals(Floor1) && Rooms[2]->GetActorLocation().Equals(Floor3));
	TestEqual(TEXT("the visibility of the unused floor is back"), Rooms[1]->IsHidden(), bFloor2Hidden);
	TestEqual(TEXT("the visibility of the route floor is back"), Rooms[2]->IsHidden(), bFloor3Hidden);
	return true;
}

// A route that goes on into a building that is not open: its floors are pulled apart too, with its rooms (made for the overview from its stored
// data) and its lifts, and all of it is gone again when the overview ends.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFullRouteOtherBuildingTest, "InNavi.Navigation.FullRoute.OtherBuilding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFullRouteOtherBuildingTest::RunTest(const FString& Parameters)
{
	FullRouteViewTests::FFixture F;
	F.Generator->SpawnBuildingFromData(TEXT("OTHER"), TEXT("Other building"),
		{ {-500,-400}, {500,-400}, {500,400}, {-500,400} }, 30, 300.f, FVector2D(9000,4000));
	F.Controller->LoadInteriorForBuilding(TEXT("OTHER"));
	for (int32 Floor = 1; Floor <= 3; ++Floor)
	{
		F.Controller->SetActiveFloor(Floor);
		FString Error;
		if (!TestNotNull(TEXT("room in the other building"), F.Interior->CreateRoomFromFootprint({ {10,10}, {35,10}, {35,35}, {10,35} },
			FString::Printf(TEXT("O%d"), Floor), TEXT("Room"), true, false, false, Error))) return false;
	}
	if (!TestFalse(TEXT("a lift in the other building"), F.Interior->PlaceConnectorNode(EInteriorNavNodeType::Elevator, FVector2D(100,100), { 1, 2, 3 }).IsEmpty())) return false;
	F.Generator->SetInteriorDataForBuilding(TEXT("OTHER"), F.Interior->CaptureInteriorSnapshot());
	F.Controller->LoadInteriorForBuilding(TEXT("ROUTE"));

	FNavigationRouteResult Result;
	for (const TPair<const TCHAR*, int32>& Place : { TPair<const TCHAR*, int32>(TEXT("ROUTE"), 1), TPair<const TCHAR*, int32>(TEXT("ROUTE"), 3),
		TPair<const TCHAR*, int32>(TEXT("OTHER"), 1), TPair<const TCHAR*, int32>(TEXT("OTHER"), 3) })
	{
		FNavigationRoutePoint Point;
		Point.BuildingId = Place.Key;
		Point.Position = FVector2D(10, 20);
		Point.Floor = Place.Value;
		Result.Points.Add(Point);
	}
	F.Nav->ApplyRouteResult(Result);
	auto CountLifts = [&]() { int32 Count = 0; for (TActorIterator<AInteriorConnectorMarker> It(F.World); It; ++It) ++Count; return Count; };
	const int32 LiftsBefore = CountLifts();

	F.Controller->SetFullRouteViewEnabled(true);
	if (!TestTrue(TEXT("overview on"), F.Controller->IsFullRouteViewEnabled())) return false;
	TestTrue(TEXT("the floors of the other building are pulled apart too"), F.Controller->FullRouteFloorShift(TEXT("OTHER"), 3) > 0.f);
	TestEqual(TEXT("its rooms are shown for the floors of the route only"), F.Controller->CountShownCampusRoomsForTests(TEXT("OTHER")), 2);
	TestEqual(TEXT("and its lift on those two floors"), CountLifts() - LiftsBefore, 2);

	F.Controller->SetFullRouteViewEnabled(false);
	TestEqual(TEXT("its rooms are hidden again"), F.Controller->CountShownCampusRoomsForTests(TEXT("OTHER")), 0);
	TestEqual(TEXT("its lifts of the overview are gone"), CountLifts(), LiftsBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFullRoutePanelTest, "InNavi.Navigation.FullRoute.PanelAndCampus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFullRoutePanelTest::RunTest(const FString& Parameters)
{
	FullRouteViewTests::FFixture F;
	UNavigationDockPanelWidget* Panel = NewObject<UNavigationDockPanelWidget>();
	Panel->Initialize(); Panel->SetOwningPlayer(F.Controller); Panel->TakeWidget();
	UButton* Button = Cast<UButton>(Panel->FindContentWidget(TEXT("Btn_NavFullRouteView")));
	if (!TestNotNull(TEXT("shared navigation panel has overview button"), Button)) return false;
	Panel->RefreshDisplay();
	TestFalse(TEXT("button disabled without route"), Button->GetIsEnabled());
	F.MakeRoute();
	for (bool bOutdoor : { false, true })
	{
		F.Nav->SetOutdoorMode(bOutdoor);
		Panel->RefreshDisplay();
		TestTrue(TEXT("button usable on both screens"), Button->GetIsEnabled());
		const FTransform Before = F.Controller->GetPawn()->GetActorTransform();
		Button->OnClicked.Broadcast();
		TestTrue(TEXT("button enters overview"), F.Controller->IsFullRouteViewEnabled());
		TestTrue(TEXT("building shown even when previously filtered/hidden"), !F.Building->IsHidden());
		Button->OnClicked.Broadcast();
		TestFalse(TEXT("button returns"), F.Controller->IsFullRouteViewEnabled());
		TestTrue(TEXT("campus/interior camera restored"), F.Controller->GetPawn()->GetActorTransform().Equals(Before));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFullRouteGeometryTest, "InNavi.Navigation.FullRoute.GeometryAndFraming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFullRouteGeometryTest::RunTest(const FString& Parameters)
{
	TArray<FNavigationRoutePoint> Points;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		FNavigationRoutePoint Point;
		Point.Floor = Index < 2 ? -1 : (Index < 4 ? 3 : 0);
		Point.BuildingId = Index < 4 ? TEXT("A") : TEXT("B");
		Point.bHasWorldPosition = true;
		Point.WorldPosition = FVector(10000 + Index * 100, -5000, Index < 2 ? -300 : (Index < 4 ? 600 : 0));
		Points.Add(Point);
	}
	const FNavigationRouteFocus Focus{0, 1};
	const FNavigation3DView View = UNavigationRouteVisualization::BuildView(Points, &Points[0], &Points.Last(),
		FVector2D::ZeroVector, 1, 300, 1, true, TEXT("A"), nullptr, nullptr, &Focus, true);
	TestTrue(TEXT("foreground cues show through walls and ground"), View.bForeground);
	TestTrue(TEXT("both endpoints included across floors/buildings"), View.Start.IsSet() && View.Destination.IsSet());
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		TestTrue(TEXT("every route edge retained including vertical transitions"), View.Segments.ContainsByPredicate([&](const FNavigationDisplaySegment& Segment)
		{
			return Segment.From.Equals(Points[Index - 1].WorldPosition + FVector(0,0,15))
				&& Segment.To.Equals(Points[Index].WorldPosition + FVector(0,0,15)) && !Segment.bDimmed;
		}));
	}
	const FBox Bounds(FVector(10000,-5000,-300), FVector(16000,-3000,4500));
	for (float Aspect : { 4.f/3.f, 16.f/9.f, 32.f/9.f, 0.6f })
	{
		// A small route inside a much larger building must remain readable from outside its shell.
		const FBox Exterior(Bounds.Min - FVector(20000), Bounds.Max + FVector(20000));
		float Fov = 0.f;
		const FTransform Camera = ANavPlayerController::FullRouteCameraTransform(Bounds, 90, Aspect, &Exterior, &Fov);
		TestFalse(TEXT("camera outside whole building bounds"), Bounds.IsInside(Camera.GetLocation()));
		TestFalse(TEXT("camera also outside much larger exterior"), Exterior.IsInside(Camera.GetLocation()));
		TestTrue(TEXT("short route zooms in for legibility"), Fov < 60.f && Fov > 0.f);
		const double TanHalfFov = FMath::Tan(FMath::DegreesToRadians(Fov * 0.5));
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector P = Camera.InverseTransformPosition(FVector(Corner & 1 ? Bounds.Max.X : Bounds.Min.X,
				Corner & 2 ? Bounds.Max.Y : Bounds.Min.Y, Corner & 4 ? Bounds.Max.Z : Bounds.Min.Z));
			TestTrue(TEXT("all corners fit horizontal and vertical frustum with margin"),
				P.X > 0 && FMath::Abs(P.Y/P.X) < 0.9 * TanHalfFov && FMath::Abs(P.Z/P.X) < 0.9 * TanHalfFov / Aspect);
		}
	}
	return true;
}

#endif
