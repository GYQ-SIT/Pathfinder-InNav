// Fill out your copyright notice in the Description page of Project Settings.


#include "NavPlayerController.h"
#include "NavigationPanelWidget.h"
#include "InteriorFloorPlanWidget.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Pawn.h"
#include "PolyBuilding.h"
#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/UObjectIterator.h"

// ---- Helper: is Point inside the (possibly concave) polygon Poly? (even-odd ray-casting test) ----
static bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Poly)
{
	bool bInside = false;
	const int32 N = Poly.Num();
	for (int32 i = 0, j = N - 1; i < N; j = i++)
	{
		const FVector2D& Pi = Poly[i];
		const FVector2D& Pj = Poly[j];
		if (((Pi.Y > Point.Y) != (Pj.Y > Point.Y)) &&
			(Point.X < (Pj.X - Pi.X) * (Point.Y - Pi.Y) / (Pj.Y - Pi.Y) + Pi.X))
		{
			bInside = !bInside;
		}
	}
	return bInside;
}

ANavPlayerController::ANavPlayerController()
{
	SelectedBuilding = nullptr;
	Generator = nullptr;
	InteriorGen = nullptr;
}

void ANavPlayerController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	NavigationPanel = CreateWidget<UNavigationPanelWidget>(this, UNavigationPanelWidget::StaticClass());
	if (NavigationPanel)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Navigation] Navigation panel created successfully"));
		NavigationPanel->AddToViewport(100);
		NavigationPanel->SetVisibility(ESlateVisibility::Collapsed);
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
	}

	for (TActorIterator<ABuildingGenerator> It(GetWorld()); It; ++It)
	{
		Generator = *It;
		break;
	}

	for (TActorIterator<AInteriorGenerator> It(GetWorld()); It; ++It)
	{
		InteriorGen = *It;
		break;
	}
	if (NavigationPanel)
	{
		NavigationPanel->SetNavigationContext(Generator, InteriorGen);
	}
	UE_LOG(LogTemp, Warning, TEXT("[Navigation] BeginPlay references: Generator=%s Interior=%s"),
		Generator ? TEXT("valid") : TEXT("NULL"),
		InteriorGen ? TEXT("valid") : TEXT("NULL"));
}

bool ANavPlayerController::IsSelectingNavigationPoint() const
{
	return NavigationPanel && NavigationPanel->IsSelectingPoint();
}

void ANavPlayerController::SelectNavigationPoint(FVector2D FootprintPosition, int32 Floor)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[Navigation] Controller forwarding point: position=(%.2f, %.2f), floor=%d, panel=%s"),
		FootprintPosition.X, FootprintPosition.Y, Floor,
		NavigationPanel ? TEXT("valid") : TEXT("NULL"));

	if (NavigationPanel)
	{
		NavigationPanel->SelectPoint(FootprintPosition, Floor);
	}
}

void ANavPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindAction("SelectBuilding", IE_Pressed, this, &ANavPlayerController::OnClick);
	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ANavPlayerController::ToggleBuildMode);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ANavPlayerController::ToggleShapeTestMode);
	InputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &ANavPlayerController::CycleInteriorFloorUp);
	InputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &ANavPlayerController::CycleInteriorFloorDown);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ANavPlayerController::AddInteriorFloor);
	InputComponent->BindKey(EKeys::X, IE_Pressed, this, &ANavPlayerController::RemoveActiveInteriorFloor);
	InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ANavPlayerController::ToggleDrawRoomMode);
	InputComponent->BindKey(EKeys::L, IE_Pressed, this, &ANavPlayerController::ToggleDrawWallMode);
	InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &ANavPlayerController::ToggleSelectMode);
	// Not "D" - DefaultPawn's free-fly movement already uses WASD, so "D" would double up as
	// "strafe right" every time this mode gets toggled.
	InputComponent->BindKey(EKeys::K, IE_Pressed, this, &ANavPlayerController::ToggleAddDoorMode);
	InputComponent->BindKey(EKeys::BackSpace, IE_Pressed, this, &ANavPlayerController::DeleteSelectionKeyHandler);
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ANavPlayerController::TogglePrefabRectangleMode);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ANavPlayerController::TogglePrefabTriangleMode);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ANavPlayerController::TogglePrefabCircleMode);
	InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ANavPlayerController::ToggleElevatorMode);
	InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &ANavPlayerController::ToggleStairsMode);
	InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &ANavPlayerController::ToggleEscalatorMode);
	InputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &ANavPlayerController::GrowSelectedRoom);
	InputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &ANavPlayerController::ShrinkSelectedRoom);
	InputComponent->BindKey(EKeys::Nine, IE_Pressed, this, &ANavPlayerController::StartRotateCCW);
	InputComponent->BindKey(EKeys::Nine, IE_Released, this, &ANavPlayerController::StopRotateCCW);
	InputComponent->BindKey(EKeys::Zero, IE_Pressed, this, &ANavPlayerController::StartRotateCW);
	InputComponent->BindKey(EKeys::Zero, IE_Released, this, &ANavPlayerController::StopRotateCW);
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ANavPlayerController::OnConfirmPressed);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ANavPlayerController::OnCancelPressed);
	InputComponent->BindKey(EKeys::N, IE_Pressed, this, &ANavPlayerController::ToggleSnapEnabled);
	InputComponent->BindKey(FInputChord(EKeys::S, /*Shift*/false, /*Ctrl*/true, /*Alt*/false, /*Cmd*/false), IE_Pressed, this, &ANavPlayerController::SaveCurrentProjectKeyHandler);
}

void ANavPlayerController::OnClick()
{
	UE_LOG(LogTemp, Warning, TEXT("[Navigation] PlayerController OnClick: selecting=%s build=%s"),
		IsSelectingNavigationPoint() ? TEXT("true") : TEXT("false"),
		bBuildMode ? TEXT("true") : TEXT("false"));

	// Ignore clicks that happen on the navigation panel buttons.
	if (NavigationPanel &&
		NavigationPanel->IsPointerOverPanel())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Navigation] PlayerController OnClick ignored: pointer is over navigation panel"));
		return;
	}

	// The 2D floor-plan widget normally handles navigation-point clicks. Keep
	// the original 3D deprojection as a fallback because GameAndUI input can
	// route the same mouse click to the PlayerController without delivering it
	// to the custom floor-plan widget.
	if (IsSelectingNavigationPoint())
	{
		// The floor plan is a 2D UMG widget, so navigation clicks must be converted
		// using that widget's cached screen geometry. The old fallback deprojected
		// the click through the 3D camera, which made the result depend on the camera
		// and caused most visible floor clicks to be rejected as outside the building.
		const FVector2D CursorScreenPosition = FSlateApplication::Get().GetCursorPos();
		for (TObjectIterator<UInteriorFloorPlanWidget> It; It; ++It)
		{
			UInteriorFloorPlanWidget* FloorPlan = *It;
			if (!FloorPlan || FloorPlan->GetWorld() != GetWorld() || !FloorPlan->IsVisible())
			{
				continue;
			}

			const FGeometry& FloorGeometry = FloorPlan->GetCachedGeometry();
			if (!FloorGeometry.IsUnderLocation(CursorScreenPosition))
			{
				continue;
			}

			if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Navigation] 2D controller fallback rejected: interior is not loaded"));
				return;
			}

			const int32 Floor = InteriorGen->GetActiveFloor();
			if (Floor < 1)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Navigation] 2D controller fallback rejected: active floor=%d"), Floor);
				return;
			}

			const FVector2D LocalPosition =
				FloorGeometry.AbsoluteToLocal(CursorScreenPosition);
			const FVector2D FootprintPosition =
				FloorPlan->LocalPositionToFootprintPoint(
					LocalPosition,
					FloorGeometry.GetLocalSize());

			UE_LOG(LogTemp, Warning,
				TEXT("[Navigation] 2D click routed by controller: local=(%.2f, %.2f) footprint=(%.2f, %.2f), floor=%d"),
				LocalPosition.X, LocalPosition.Y,
				FootprintPosition.X, FootprintPosition.Y, Floor);

			SelectNavigationPoint(FootprintPosition, Floor);
			return;
		}

		// Do not fall back to the 3D camera for a 2D floor-plan click. If the
		// widget cannot be found or the cursor is outside it, report the problem
		// instead of producing a misleading random footprint position.
		UE_LOG(LogTemp, Error,
			TEXT("[Navigation] Navigation click did not hit a visible 2D floor-plan widget; 3D fallback skipped"));
		return;

	}

	// Build mode uses the mouse click to add a building footprint point.
	if (bBuildMode)
	{
		AddFootprintPointUnderCursor();
	}
	else if (
		bDrawRoomMode ||
		bDrawWallMode ||
		bSelectMode ||
		bAddDoorMode ||
		ActivePrefabShape != 0 ||
		ActiveConnectorType != 0)
	{
		// Interior-editing clicks are handled by
		// InteriorFloorPlanWidget.
		// Therefore, do nothing here.
	}
	else
	{
		// If no special mode is active,
		// select a building in the 3D view.
		SelectBuildingUnderCursor();
	}
}

void ANavPlayerController::SelectBuildingUnderCursor()
{
	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	if (Hit.bBlockingHit)
	{
		APolyBuilding* ClickedBuilding = Cast<APolyBuilding>(Hit.GetActor());
		if (ClickedBuilding)
		{
			if (SelectedBuilding && SelectedBuilding != ClickedBuilding)
			{
				SelectedBuilding->SetHighlighted(false);
			}

			ClickedBuilding->SetHighlighted(true);
			SelectedBuilding = ClickedBuilding;
			OnBuildingSelected.Broadcast(ClickedBuilding->BuildingId);

			UE_LOG(LogTemp, Warning, TEXT("Selected building: %s"), *ClickedBuilding->BuildingName);
		}
	}
}

void ANavPlayerController::ToggleBuildMode()
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter build mode: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("Cannot enter build mode: no BuildingGenerator found in the level."));
		return;
	}

	if (bShapeTestMode)
	{
		// Modes are mutually exclusive - only one should interpret Enter/Esc at a time.
		bShapeTestMode = false;
		OnShapeModeChanged.Broadcast(false);
	}
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitAddDoorMode();
	ExitConnectorMode();

	bBuildMode = !bBuildMode;
	OnBuildModeChanged.Broadcast(bBuildMode);

	if (bBuildMode)
	{
		UE_LOG(LogTemp, Warning, TEXT("Build mode ON: click to add footprint points, Enter to finish, Esc to cancel"));
	}
	else
	{
		CancelBuilding();
		UE_LOG(LogTemp, Warning, TEXT("Build mode OFF"));
	}
}

void ANavPlayerController::ToggleShapeTestMode()
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter shape test mode: no BuildingGenerator found in the level"));
		return;
	}

	if (bBuildMode)
	{
		// Modes are mutually exclusive - only one should interpret Enter/Esc at a time.
		CancelBuilding();
		bBuildMode = false;
		OnBuildModeChanged.Broadcast(false);
	}
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitAddDoorMode();
	ExitConnectorMode();

	bShapeTestMode = !bShapeTestMode;
	OnShapeModeChanged.Broadcast(bShapeTestMode);

	if (bShapeTestMode)
	{
		UE_LOG(LogTemp, Warning, TEXT("Shape test mode ON: edit Data/test_shape_params.json (\"shape\": \"rectangle\" or \"polygon\"), then Enter to generate, Esc to exit"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Shape test mode OFF"));
	}
}

void ANavPlayerController::ToggleDrawRoomMode()
{
	if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter draw room mode: no building interior currently loaded"));
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try drawing a room."));
		return;
	}

	if (bDrawRoomMode)
	{
		ExitDrawRoomMode();
		return;
	}

	if (bBuildMode)
	{
		// Modes are mutually exclusive - only one should interpret Enter/Esc at a time.
		CancelBuilding();
		bBuildMode = false;
		OnBuildModeChanged.Broadcast(false);
	}
	if (bShapeTestMode)
	{
		bShapeTestMode = false;
		OnShapeModeChanged.Broadcast(false);
	}
	if (bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitAddDoorMode();
	ExitConnectorMode();

	bDrawRoomMode = true;
	OnDrawRoomModeChanged.Broadcast(true);

	UE_LOG(LogTemp, Warning, TEXT("Draw room mode ON (floor %d): click to add footprint points, Enter to finish, Esc to cancel"), InteriorGen->GetActiveFloor());
}

void ANavPlayerController::ToggleDrawWallMode()
{
	if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter draw wall mode: no building interior currently loaded"));
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try drawing a wall."));
		return;
	}

	if (bDrawWallMode)
	{
		ExitDrawRoomMode();
		return;
	}

	if (bBuildMode)
	{
		// Modes are mutually exclusive - only one should interpret Enter/Esc at a time.
		CancelBuilding();
		bBuildMode = false;
		OnBuildModeChanged.Broadcast(false);
	}
	if (bShapeTestMode)
	{
		bShapeTestMode = false;
		OnShapeModeChanged.Broadcast(false);
	}
	if (bDrawRoomMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitAddDoorMode();
	ExitConnectorMode();

	bDrawWallMode = true;
	OnDrawWallModeChanged.Broadcast(true);

	UE_LOG(LogTemp, Warning, TEXT("Draw wall mode ON (floor %d): click to add points, Enter to finish, Esc to cancel"), InteriorGen->GetActiveFloor());
}

void ANavPlayerController::ExitDrawRoomMode()
{
	if (bDrawRoomMode)
	{
		CancelRoom();
		bDrawRoomMode = false;
		OnDrawRoomModeChanged.Broadcast(false);
	}
	if (bDrawWallMode)
	{
		CancelWall();
		bDrawWallMode = false;
		OnDrawWallModeChanged.Broadcast(false);
	}

	UE_LOG(LogTemp, Warning, TEXT("Draw room/wall mode OFF"));
}

void ANavPlayerController::ExitSelectMode()
{
	if (bSelectMode)
	{
		bSelectMode = false;
		OnSelectModeChanged.Broadcast(false);
		UE_LOG(LogTemp, Warning, TEXT("Select mode OFF"));
	}
}

void ANavPlayerController::ToggleSelectMode()
{
	if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter select mode: no building interior currently loaded"));
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try selecting a room/wall."));
		return;
	}

	if (bSelectMode)
	{
		ExitSelectMode();
		return;
	}

	if (bBuildMode)
	{
		// Modes are mutually exclusive - only one should interpret Enter/Esc at a time.
		CancelBuilding();
		bBuildMode = false;
		OnBuildModeChanged.Broadcast(false);
	}
	if (bShapeTestMode)
	{
		bShapeTestMode = false;
		OnShapeModeChanged.Broadcast(false);
	}
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitPrefabRoomMode();
	ExitAddDoorMode();
	ExitConnectorMode();

	bSelectMode = true;
	OnSelectModeChanged.Broadcast(true);

	UE_LOG(LogTemp, Warning, TEXT("Select mode ON (floor %d): click a room/wall on the 2D canvas to select it (Backspace deletes, =/- resizes), Esc/Q to exit"), InteriorGen->GetActiveFloor());
}

void ANavPlayerController::ToggleSnapEnabled()
{
	bSnapEnabled = !bSnapEnabled;
	UE_LOG(LogTemp, Warning, TEXT("Snapping %s"), bSnapEnabled ? TEXT("ON") : TEXT("OFF"));
}

void ANavPlayerController::ExitPrefabRoomMode()
{
	if (ActivePrefabShape != 0)
	{
		ActivePrefabShape = 0;
		UE_LOG(LogTemp, Warning, TEXT("Prefab room placement OFF"));
	}
}

void ANavPlayerController::TogglePrefabRoomMode(int32 ShapeId)
{
	if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot place a prefab room: no building interior currently loaded"));
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try placing a room."));
		return;
	}

	if (ActivePrefabShape == ShapeId)
	{
		ExitPrefabRoomMode();
		return;
	}

	if (bBuildMode)
	{
		// Modes are mutually exclusive - only one should interpret Enter/Esc at a time.
		CancelBuilding();
		bBuildMode = false;
		OnBuildModeChanged.Broadcast(false);
	}
	if (bShapeTestMode)
	{
		bShapeTestMode = false;
		OnShapeModeChanged.Broadcast(false);
	}
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitAddDoorMode();
	ExitConnectorMode();

	ActivePrefabShape = ShapeId;
	UE_LOG(LogTemp, Warning, TEXT("Prefab room placement ON (shape %d): click the 2D canvas to place, press the same key again to stop"), ShapeId);
}

void ANavPlayerController::TogglePrefabRectangleMode()
{
	TogglePrefabRoomMode(1);
}

void ANavPlayerController::TogglePrefabTriangleMode()
{
	TogglePrefabRoomMode(2);
}

void ANavPlayerController::TogglePrefabCircleMode()
{
	TogglePrefabRoomMode(3);
}

void ANavPlayerController::ExitConnectorMode()
{
	if (ActiveConnectorType != 0)
	{
		ActiveConnectorType = 0;
		UE_LOG(LogTemp, Warning, TEXT("Connector placement OFF"));
	}
}

void ANavPlayerController::ToggleConnectorMode(EInteriorNavNodeType Type)
{
	if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot place a connector: no building interior currently loaded"));
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try placing an elevator/stairs/escalator."));
		return;
	}

	// Matches ActiveConnectorType's convention: 0 = none, otherwise the int32 value of the
	// EInteriorNavNodeType being placed (Elevator=1/Stairs=2/Escalator=3 - PathNode=0 is never
	// placed this way, so it never collides with "none").
	const int32 RequestedType = static_cast<int32>(Type);
	if (ActiveConnectorType == RequestedType)
	{
		ExitConnectorMode();
		return;
	}

	if (bBuildMode)
	{
		// Modes are mutually exclusive - only one should interpret Enter/Esc at a time.
		CancelBuilding();
		bBuildMode = false;
		OnBuildModeChanged.Broadcast(false);
	}
	if (bShapeTestMode)
	{
		bShapeTestMode = false;
		OnShapeModeChanged.Broadcast(false);
	}
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitAddDoorMode();

	ActiveConnectorType = RequestedType;
	UE_LOG(LogTemp, Warning, TEXT("Connector placement ON (type %d): click the 2D canvas to place, press the same key again to stop"), RequestedType);
}

void ANavPlayerController::ToggleElevatorMode()
{
	ToggleConnectorMode(EInteriorNavNodeType::Elevator);
}

void ANavPlayerController::ToggleStairsMode()
{
	ToggleConnectorMode(EInteriorNavNodeType::Stairs);
}

void ANavPlayerController::ToggleEscalatorMode()
{
	ToggleConnectorMode(EInteriorNavNodeType::Escalator);
}

void ANavPlayerController::ExitAddDoorMode()
{
	if (bAddDoorMode)
	{
		bAddDoorMode = false;
		UE_LOG(LogTemp, Warning, TEXT("Add-door mode OFF"));
	}
}

void ANavPlayerController::ToggleAddDoorMode()
{
	if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter add-door mode: no building interior currently loaded"));
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try adding a door."));
		return;
	}

	if (bAddDoorMode)
	{
		ExitAddDoorMode();
		return;
	}

	if (bBuildMode)
	{
		// Modes are mutually exclusive - only one should interpret Enter/Esc at a time.
		CancelBuilding();
		bBuildMode = false;
		OnBuildModeChanged.Broadcast(false);
	}
	if (bShapeTestMode)
	{
		bShapeTestMode = false;
		OnShapeModeChanged.Broadcast(false);
	}
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitConnectorMode();

	bAddDoorMode = true;
	UE_LOG(LogTemp, Warning, TEXT("Add-door mode ON (floor %d): click a wall edge on the 2D canvas to add a door, Esc/K to exit"), InteriorGen->GetActiveFloor());
}

FString ANavPlayerController::AddDoorToWall(const FString& RoomId, int32 EdgeIndex, float OffsetWorldUnits)
{
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."));
		return FString();
	}

	const FString NewDoorId = InteriorGen->AddDoorToRoom(RoomId, EdgeIndex, OffsetWorldUnits);
	if (NewDoorId.IsEmpty())
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not add a door to '%s'."), *RoomId));
	}
	else
	{
		bHasUnsavedChanges = true;
		// Selected immediately, like PlacePrefabRoomAt does for rooms, so it can be deleted right
		// away with Backspace if it's wrong.
		SelectDoor(RoomId, NewDoorId);
	}
	return NewDoorId;
}

void ANavPlayerController::OnConfirmPressed()
{
	if (bBuildMode)
	{
		FinishBuilding();
	}
	else if (bShapeTestMode)
	{
		ConfirmShapeTest();
	}
	else if (bDrawRoomMode)
	{
		FinishRoom();
	}
	else if (bDrawWallMode)
	{
		FinishWall();
	}
}

void ANavPlayerController::OnCancelPressed()
{
	if (NavigationPanel && NavigationPanel->GetSelectionMode() != ENavigationSelectionMode::None)
	{
		NavigationPanel->CancelSelection();
		return;
	}

	if (bBuildMode)
	{
		CancelBuilding();
	}
	else if (bShapeTestMode)
	{
		bShapeTestMode = false;
		OnShapeModeChanged.Broadcast(false);
		UE_LOG(LogTemp, Warning, TEXT("Shape test mode OFF"));
	}
	else if (bDrawRoomMode)
	{
		CancelRoom();
	}
	else if (bDrawWallMode)
	{
		CancelWall();
	}
	else if (bSelectMode)
	{
		ExitSelectMode();
	}
	else if (ActivePrefabShape != 0)
	{
		ExitPrefabRoomMode();
	}
	else if (bAddDoorMode)
	{
		ExitAddDoorMode();
	}
	else if (ActiveConnectorType != 0)
	{
		ExitConnectorMode();
	}
}

void ANavPlayerController::AddFootprintPointUnderCursor()
{
	if (!Generator)
	{
		return;
	}

	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	if (!Hit.bBlockingHit)
	{
		return;
	}

	if (Cast<APolyBuilding>(Hit.GetActor()))
	{
		UE_LOG(LogTemp, Warning, TEXT("Click on the ground to add a footprint point, not on another building"));
		OnActionFailed.Broadcast(TEXT("Click on the ground, not on another building."));
		return;
	}

	// SpawnBuildingFromData multiplies the footprint by Generator's Scale, but Hit.Location is
	// already in final world-space units, so divide it back out before storing the point.
	const float Scale = Generator->GetScale();
	const FVector2D NewPoint(Hit.Location.X / Scale, Hit.Location.Y / Scale);

	if (PendingFootprint.Num() > 0)
	{
		// Prev is stored unscaled (see above) - scale it back up to world space to draw it correctly.
		const FVector2D& Prev = PendingFootprint.Last();
		const FVector PrevWorld(Prev.X * Scale, Prev.Y * Scale, Hit.Location.Z);
		DrawDebugLine(GetWorld(), PrevWorld, Hit.Location, FColor::Green, true, -1.f, 0, 5.f);
	}
	DrawDebugSphere(GetWorld(), Hit.Location, 20.f, 8, FColor::Green, true);

	PendingFootprint.Add(NewPoint);
	OnFootprintPointCountChanged.Broadcast(PendingFootprint.Num());

	UE_LOG(LogTemp, Warning, TEXT("Footprint point %d added: (%.1f, %.1f)"), PendingFootprint.Num(), NewPoint.X, NewPoint.Y);
}

void ANavPlayerController::FinishBuilding(const FString& CustomId, const FString& CustomName)
{
	if (!bBuildMode)
	{
		return;
	}

	if (PendingFootprint.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("Need at least 3 points to finish a building (have %d)"), PendingFootprint.Num());
		OnActionFailed.Broadcast(FString::Printf(TEXT("Need at least 3 points to finish a building (have %d)."), PendingFootprint.Num()));
		return;
	}

	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("No BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."));
		return;
	}

	// Empty CustomId/CustomName means "let the user skip naming it" - fall back to auto-numbering.
	const bool bUsingAutoId = CustomId.IsEmpty();
	const FString NewId = bUsingAutoId ? FString::Printf(TEXT("NEW_%d"), NextNewBuildingIndex) : CustomId;
	const FString NewName = CustomName.IsEmpty() ? NewId : CustomName;

	APolyBuilding* NewBuilding = Generator->SpawnBuildingFromData(NewId, NewName, PendingFootprint, 5, 300.f, FVector2D::ZeroVector);

	if (!NewBuilding)
	{
		// Spawn was rejected (e.g. self-intersecting outline, or a typed-in id that's already taken)
		// - keep the drawn points and preview so the user can see what's there; Esc still cancels.
		UE_LOG(LogTemp, Warning, TEXT("Could not finish this building - see error above. Press Esc to discard and redraw."));
		OnActionFailed.Broadcast(TEXT("Could not create the building - check that the id isn't already used and the outline doesn't cross itself."));
		return;
	}

	if (bUsingAutoId)
	{
		NextNewBuildingIndex++;
	}
	bHasUnsavedChanges = true;
	FlushPersistentDebugLines(GetWorld());
	PendingFootprint.Reset();
	OnFootprintPointCountChanged.Broadcast(0);
}

void ANavPlayerController::CancelBuilding()
{
	FlushPersistentDebugLines(GetWorld());
	PendingFootprint.Reset();
	OnFootprintPointCountChanged.Broadcast(0);
}

void ANavPlayerController::AddRoomPointAtFootprintPosition(FVector2D FootprintPoint)
{
	if (!bDrawRoomMode && !bDrawWallMode)
	{
		return;
	}

	if (InteriorGen && Generator)
	{
		if (APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId()))
		{
			if (!IsPointInPolygon(FootprintPoint, Building->GetFootprint()))
			{
				UE_LOG(LogTemp, Warning, TEXT("Click inside the building's outline to add a point, not outside it"));
				OnActionFailed.Broadcast(TEXT("Click inside the building's outline, not outside it."));
				return;
			}
		}
	}

	// Vertex snapping: if this click landed close to an existing room/wall's corner, snap to it
	// exactly instead of the raw click position - same tolerance as dragging a room (see
	// UInteriorFloorPlanWidget), applied after the building-outline check above so snapping can't
	// rescue a click that's genuinely outside the building.
	if (InteriorGen && bSnapEnabled)
	{
		constexpr float SnapToleranceFootprintUnits = 8.f;
		FVector2D SnapOffset;
		if (InteriorGen->FindSnapOffset(InteriorGen->GetActiveFloor(), { FootprintPoint }, FString(), SnapToleranceFootprintUnits, SnapOffset))
		{
			FootprintPoint += SnapOffset;
		}
	}

	PendingRoomFootprint.Add(FootprintPoint);
	OnRoomPointCountChanged.Broadcast(PendingRoomFootprint.Num());

	UE_LOG(LogTemp, Warning, TEXT("Room point %d added: (%.1f, %.1f)"), PendingRoomFootprint.Num(), FootprintPoint.X, FootprintPoint.Y);
}

void ANavPlayerController::SelectRoom(const FString& RoomId)
{
	if (!SelectedDoorId.IsEmpty())
	{
		SelectedDoorRoomId.Empty();
		SelectedDoorId.Empty();
		OnSelectedDoorChanged.Broadcast(FString(), FString());
	}
	if (!SelectedConnectorGroupId.IsEmpty())
	{
		SelectedConnectorGroupId.Empty();
		OnSelectedConnectorChanged.Broadcast(FString());
	}

	if (SelectedRoomId == RoomId)
	{
		return;
	}

	SelectedRoomId = RoomId;
	OnSelectedRoomChanged.Broadcast(SelectedRoomId);
}

void ANavPlayerController::SelectDoor(const FString& RoomId, const FString& DoorId)
{
	if (!SelectedRoomId.IsEmpty())
	{
		SelectedRoomId.Empty();
		OnSelectedRoomChanged.Broadcast(FString());
	}
	if (!SelectedConnectorGroupId.IsEmpty())
	{
		SelectedConnectorGroupId.Empty();
		OnSelectedConnectorChanged.Broadcast(FString());
	}

	SelectedDoorRoomId = RoomId;
	SelectedDoorId = DoorId;
	OnSelectedDoorChanged.Broadcast(SelectedDoorRoomId, SelectedDoorId);
}

void ANavPlayerController::SelectConnector(const FString& GroupId)
{
	if (!SelectedRoomId.IsEmpty())
	{
		SelectedRoomId.Empty();
		OnSelectedRoomChanged.Broadcast(FString());
	}
	if (!SelectedDoorId.IsEmpty())
	{
		SelectedDoorRoomId.Empty();
		SelectedDoorId.Empty();
		OnSelectedDoorChanged.Broadcast(FString(), FString());
	}

	if (SelectedConnectorGroupId == GroupId)
	{
		return;
	}

	SelectedConnectorGroupId = GroupId;
	OnSelectedConnectorChanged.Broadcast(SelectedConnectorGroupId);
}

bool ANavPlayerController::SetSelectedRoomName(const FString& NewName)
{
	if (SelectedRoomId.IsEmpty() || !InteriorGen)
	{
		return false;
	}

	const bool bSuccess = InteriorGen->SetRoomName(SelectedRoomId, NewName);
	if (bSuccess)
	{
		bHasUnsavedChanges = true;
	}
	else
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not rename '%s' to '%s' - that name is already in use."), *SelectedRoomId, *NewName));
	}
	return bSuccess;
}

FString ANavPlayerController::GetSelectedRoomName() const
{
	if (SelectedRoomId.IsEmpty() || !InteriorGen)
	{
		return FString();
	}

	return InteriorGen->GetRoomName(SelectedRoomId);
}

bool ANavPlayerController::IsSelectedRoomClosedLoop() const
{
	if (SelectedRoomId.IsEmpty() || !InteriorGen)
	{
		return false;
	}

	return InteriorGen->IsRoomClosedLoop(SelectedRoomId);
}

bool ANavPlayerController::GetSelectedDoorOpening(FInteriorOpening& OutOpening)
{
	if (SelectedDoorId.IsEmpty() || !InteriorGen)
	{
		return false;
	}

	return InteriorGen->GetDoorOpening(SelectedDoorRoomId, SelectedDoorId, OutOpening);
}

bool ANavPlayerController::SetSelectedDoorConnectsTo(const FString& NewConnectsToLabel)
{
	if (SelectedDoorId.IsEmpty() || !InteriorGen)
	{
		return false;
	}

	// NewConnectsToLabel is whatever the dropdown showed (see GetConnectableTargetIds) - a room's
	// current name, or "corridor". Resolve it back to the stable room id that actually gets stored,
	// so a later rename doesn't silently break this door's connection.
	FString ResolvedConnectsTo = NewConnectsToLabel;
	if (NewConnectsToLabel != TEXT("corridor"))
	{
		bool bResolved = false;
		for (const AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
		{
			if (Room && Room->RoomName == NewConnectsToLabel)
			{
				ResolvedConnectsTo = Room->RoomId;
				bResolved = true;
				break;
			}
		}
		if (!bResolved)
		{
			OnActionFailed.Broadcast(FString::Printf(TEXT("Could not find a room named '%s'."), *NewConnectsToLabel));
			return false;
		}
	}

	const bool bSuccess = InteriorGen->SetDoorConnectsTo(SelectedDoorRoomId, SelectedDoorId, ResolvedConnectsTo);
	if (bSuccess)
	{
		bHasUnsavedChanges = true;
	}
	else
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not update door '%s'."), *SelectedDoorId));
	}
	return bSuccess;
}

TArray<FString> ANavPlayerController::GetConnectableTargetIds() const
{
	TArray<FString> Result;
	if (SelectedDoorId.IsEmpty() || !InteriorGen)
	{
		return Result;
	}

	Result.Add(TEXT("corridor"));

	for (const AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
	{
		if (Room && Room->IsClosedLoop() && Room->RoomId != SelectedDoorRoomId)
		{
			// Shown (and picked from) by name, not raw id - the id is what actually gets stored (see
			// SetSelectedDoorConnectsTo), so a later rename doesn't silently break this door's
			// connection. RoomName is never actually empty (see CreateRoomFromFootprint), it falls
			// back to the room's own id if never given a real name.
			Result.Add(Room->RoomName);
		}
	}

	return Result;
}

FString ANavPlayerController::ResolveConnectsToDisplayName(const FString& ConnectsToId) const
{
	if (ConnectsToId.IsEmpty() || ConnectsToId == TEXT("corridor") || !InteriorGen)
	{
		return ConnectsToId;
	}

	const FString RoomName = InteriorGen->GetRoomName(ConnectsToId);
	// Falls back to the raw id if the room can't be found (e.g. it was since deleted) - better to
	// show a stale-but-visible id than silently blank out the field.
	return RoomName.IsEmpty() ? ConnectsToId : RoomName;
}

bool ANavPlayerController::DeleteRoom(const FString& RoomId)
{
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."));
		return false;
	}

	const bool bSuccess = InteriorGen->DeleteRoom(RoomId);
	if (!bSuccess)
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not delete '%s' - it may no longer exist."), *RoomId));
	}
	else
	{
		bHasUnsavedChanges = true;
		if (SelectedRoomId == RoomId)
		{
			SelectRoom(FString());
		}
		if (SelectedDoorRoomId == RoomId)
		{
			// The room (and every door on it) is gone - the selected door reference is now stale.
			SelectedDoorRoomId.Empty();
			SelectedDoorId.Empty();
			OnSelectedDoorChanged.Broadcast(FString(), FString());
		}
	}
	return bSuccess;
}

bool ANavPlayerController::DeleteDoor(const FString& RoomId, const FString& DoorId)
{
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."));
		return false;
	}

	const bool bSuccess = InteriorGen->DeleteDoor(RoomId, DoorId);
	if (!bSuccess)
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not delete door '%s'."), *DoorId));
	}
	else
	{
		bHasUnsavedChanges = true;
		if (SelectedDoorRoomId == RoomId && SelectedDoorId == DoorId)
		{
			SelectedDoorRoomId.Empty();
			SelectedDoorId.Empty();
			OnSelectedDoorChanged.Broadcast(FString(), FString());
		}
	}
	return bSuccess;
}

bool ANavPlayerController::MoveDoor(const FString& RoomId, const FString& DoorId, float NewOffsetWorldUnits)
{
	if (!InteriorGen)
	{
		return false;
	}

	const bool bSuccess = InteriorGen->MoveDoor(RoomId, DoorId, NewOffsetWorldUnits);
	if (bSuccess)
	{
		bHasUnsavedChanges = true;
	}
	return bSuccess;
}

bool ANavPlayerController::DeleteSelection()
{
	if (!SelectedDoorId.IsEmpty())
	{
		return DeleteDoor(SelectedDoorRoomId, SelectedDoorId);
	}

	if (!SelectedConnectorGroupId.IsEmpty())
	{
		return DeleteSelectedConnector();
	}

	if (SelectedRoomId.IsEmpty())
	{
		return false;
	}

	return DeleteRoom(SelectedRoomId);
}

bool ANavPlayerController::DeleteSelectedConnector()
{
	if (SelectedConnectorGroupId.IsEmpty() || !InteriorGen)
	{
		return false;
	}

	const bool bSuccess = InteriorGen->DeleteConnectorGroup(SelectedConnectorGroupId);
	if (bSuccess)
	{
		bHasUnsavedChanges = true;
		SelectedConnectorGroupId.Empty();
		OnSelectedConnectorChanged.Broadcast(FString());
	}
	else
	{
		OnActionFailed.Broadcast(TEXT("Could not delete that connector."));
	}
	return bSuccess;
}

EInteriorNavNodeType ANavPlayerController::GetSelectedConnectorType() const
{
	if (!SelectedConnectorGroupId.IsEmpty() && InteriorGen)
	{
		for (const FInteriorNavNode& Node : InteriorGen->GetNavNodes())
		{
			if (Node.ConnectorGroupId == SelectedConnectorGroupId)
			{
				return Node.Type;
			}
		}
	}

	return EInteriorNavNodeType::PathNode;
}

TArray<int32> ANavPlayerController::GetSelectedConnectorFloors() const
{
	TArray<int32> Result;
	if (!SelectedConnectorGroupId.IsEmpty() && InteriorGen)
	{
		for (const FInteriorNavNode& Node : InteriorGen->GetNavNodes())
		{
			if (Node.ConnectorGroupId == SelectedConnectorGroupId)
			{
				return Node.ConnectsFloors;
			}
		}
	}

	return Result;
}

bool ANavPlayerController::SetSelectedConnectorFloors(const TArray<int32>& NewFloors)
{
	if (SelectedConnectorGroupId.IsEmpty() || !InteriorGen)
	{
		return false;
	}

	const bool bSuccess = InteriorGen->SetConnectorFloors(SelectedConnectorGroupId, NewFloors);
	if (bSuccess)
	{
		bHasUnsavedChanges = true;
	}
	else
	{
		OnActionFailed.Broadcast(TEXT("Could not update that connector's floors - check each newly-added floor exists, is inside the building, and isn't blocked by a wall."));
	}
	return bSuccess;
}

void ANavPlayerController::DeleteSelectionKeyHandler()
{
	DeleteSelection();
}

bool ANavPlayerController::ResizeRoom(const FString& RoomId, float NewWidth, float NewDepth)
{
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."));
		return false;
	}

	const bool bSuccess = InteriorGen->ResizeRoom(RoomId, NewWidth, NewDepth);
	if (!bSuccess)
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not resize '%s'."), *RoomId));
	}
	else
	{
		bHasUnsavedChanges = true;
	}
	return bSuccess;
}

bool ANavPlayerController::MoveRoomTo(const FString& RoomId, const TArray<FVector2D>& NewFootprint)
{
	if (!InteriorGen)
	{
		return false;
	}

	const bool bSuccess = InteriorGen->MoveRoom(RoomId, NewFootprint);
	if (bSuccess)
	{
		bHasUnsavedChanges = true;
	}
	return bSuccess;
}

bool ANavPlayerController::CommitRoomMove(const FString& RoomId, const TArray<FVector2D>& FallbackFootprint)
{
	if (!InteriorGen)
	{
		return false;
	}

	const bool bSuccess = InteriorGen->CommitRoomMove(RoomId, FallbackFootprint);
	if (!bSuccess)
	{
		OnActionFailed.Broadcast(TEXT("Can't place a room on top of another room/wall - moved it back."));
	}
	bHasUnsavedChanges = true;
	return bSuccess;
}

AInteriorRoom* ANavPlayerController::PlacePrefabRoomAt(FVector2D FootprintCenter)
{
	if (ActivePrefabShape == 0)
	{
		return nullptr;
	}

	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."));
		return nullptr;
	}

	if (Generator)
	{
		if (APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId()))
		{
			if (!IsPointInPolygon(FootprintCenter, Building->GetFootprint()))
			{
				UE_LOG(LogTemp, Warning, TEXT("Click inside the building's outline to place a room, not outside it"));
				OnActionFailed.Broadcast(TEXT("Click inside the building's outline, not outside it."));
				return nullptr;
			}
		}
	}

	// Default sizes are deliberately small - ResizeRoom (below) is how they grow from here.
	TArray<FVector2D> Footprint;
	switch (ActivePrefabShape)
	{
	case 1: // Rectangle: 60 x 45, axis-aligned.
	{
		constexpr float HalfWidth = 30.f;
		constexpr float HalfDepth = 22.5f;
		Footprint.Add(FootprintCenter + FVector2D(-HalfWidth, -HalfDepth));
		Footprint.Add(FootprintCenter + FVector2D(HalfWidth, -HalfDepth));
		Footprint.Add(FootprintCenter + FVector2D(HalfWidth, HalfDepth));
		Footprint.Add(FootprintCenter + FVector2D(-HalfWidth, HalfDepth));
		break;
	}
	case 2: // Triangle: equilateral, radius 30, pointing up.
	{
		constexpr float Radius = 30.f;
		constexpr int32 Sides = 3;
		for (int32 i = 0; i < Sides; i++)
		{
			const float AngleDegrees = -90.f + (360.f * i) / Sides;
			const float AngleRad = FMath::DegreesToRadians(AngleDegrees);
			Footprint.Add(FootprintCenter + FVector2D(Radius * FMath::Cos(AngleRad), Radius * FMath::Sin(AngleRad)));
		}
		break;
	}
	case 3: // Circle: approximated as a 24-sided regular polygon, radius 30.
	{
		constexpr float Radius = 30.f;
		constexpr int32 Sides = 24;
		for (int32 i = 0; i < Sides; i++)
		{
			const float AngleRad = FMath::DegreesToRadians((360.f * i) / Sides);
			Footprint.Add(FootprintCenter + FVector2D(Radius * FMath::Cos(AngleRad), Radius * FMath::Sin(AngleRad)));
		}
		break;
	}
	default:
		return nullptr;
	}

	AInteriorRoom* NewRoom = InteriorGen->CreateRoomFromFootprint(Footprint, FString(), FString(), true, false);
	if (!NewRoom)
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not place the prefab room - it may overlap an existing one."));
		OnActionFailed.Broadcast(TEXT("Could not place the room there - check that it doesn't overlap an existing one."));
		return nullptr;
	}

	bHasUnsavedChanges = true;
	// Selecting it immediately means =/- (or a future resize UI) can act on it right away without
	// an extra click.
	SelectRoom(NewRoom->RoomId);
	return NewRoom;
}

FString ANavPlayerController::PlaceConnectorAt(FVector2D FootprintPosition)
{
	if (ActiveConnectorType == 0)
	{
		return FString();
	}

	if (!InteriorGen || !Generator)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator/BuildingGenerator placed in the level."));
		return FString();
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try placing an elevator/stairs/escalator."));
		return FString();
	}

	const EInteriorNavNodeType Type = static_cast<EInteriorNavNodeType>(ActiveConnectorType);
	const int32 FloorCount = Building->GetFloorCount();
	const int32 ActiveFloorLevel = InteriorGen->GetActiveFloor();

	// An elevator defaults to every floor (real elevators go direct to any floor); stairs/escalator
	// default to just the immediately adjacent floor(s) - one side only at the top/bottom of the
	// building. Either default is freely editable afterward (not built yet - see the connector
	// properties panel, still to come).
	TArray<int32> ConnectsToFloors;
	if (Type == EInteriorNavNodeType::Elevator)
	{
		for (int32 Level = 1; Level <= FloorCount; Level++)
		{
			ConnectsToFloors.Add(Level);
		}
	}
	else
	{
		if (ActiveFloorLevel - 1 >= 1)
		{
			ConnectsToFloors.Add(ActiveFloorLevel - 1);
		}
		ConnectsToFloors.Add(ActiveFloorLevel);
		if (ActiveFloorLevel + 1 <= FloorCount)
		{
			ConnectsToFloors.Add(ActiveFloorLevel + 1);
		}
	}

	const FString GroupId = InteriorGen->PlaceConnectorNode(Type, FootprintPosition, ConnectsToFloors);
	if (GroupId.IsEmpty())
	{
		OnActionFailed.Broadcast(TEXT("Could not place a connector there - check it's inside the building and not on top of a wall."));
		return FString();
	}

	bHasUnsavedChanges = true;
	return GroupId;
}

AInteriorRoom* ANavPlayerController::FinishRoom(const FString& RoomId, const FString& RoomName, bool bIsPublic, bool bRequiresKey)
{
	if (!bDrawRoomMode)
	{
		return nullptr;
	}

	if (PendingRoomFootprint.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("Need at least 3 points to finish a room (have %d)"), PendingRoomFootprint.Num());
		OnActionFailed.Broadcast(FString::Printf(TEXT("Need at least 3 points to finish a room (have %d)."), PendingRoomFootprint.Num()));
		return nullptr;
	}

	if (!InteriorGen)
	{
		UE_LOG(LogTemp, Error, TEXT("No InteriorGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator found in the level."));
		return nullptr;
	}

	AInteriorRoom* NewRoom = InteriorGen->CreateRoomFromFootprint(PendingRoomFootprint, RoomId, RoomName, bIsPublic, bRequiresKey);

	if (!NewRoom)
	{
		// Spawn was rejected (e.g. self-intersecting outline, or a typed-in id that's already
		// taken) - keep the drawn points and preview so the user can see what's there; Esc cancels.
		UE_LOG(LogTemp, Warning, TEXT("Could not finish this room - see error above. Press Esc to discard and redraw."));
		OnActionFailed.Broadcast(TEXT("Could not create the room - check that it stays inside the building, doesn't overlap another room/wall, doesn't cross itself, and that the id isn't already used."));
		return nullptr;
	}

	bHasUnsavedChanges = true;
	FlushPersistentDebugLines(GetWorld());
	PendingRoomFootprint.Reset();
	OnRoomPointCountChanged.Broadcast(0);
	return NewRoom;
}

void ANavPlayerController::CancelRoom()
{
	FlushPersistentDebugLines(GetWorld());
	PendingRoomFootprint.Reset();
	OnRoomPointCountChanged.Broadcast(0);
}

AInteriorRoom* ANavPlayerController::FinishWall(const FString& WallId)
{
	if (!bDrawWallMode)
	{
		return nullptr;
	}

	if (PendingRoomFootprint.Num() < 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("Need at least 2 points to finish a wall (have %d)"), PendingRoomFootprint.Num());
		OnActionFailed.Broadcast(FString::Printf(TEXT("Need at least 2 points to finish a wall (have %d)."), PendingRoomFootprint.Num()));
		return nullptr;
	}

	if (!InteriorGen)
	{
		UE_LOG(LogTemp, Error, TEXT("No InteriorGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator found in the level."));
		return nullptr;
	}

	AInteriorRoom* NewWall = InteriorGen->CreateWallFromPoints(PendingRoomFootprint, WallId);

	if (!NewWall)
	{
		// Spawn was rejected (e.g. a typed-in id that's already taken) - keep the drawn points and
		// preview so the user can see what's there; Esc cancels.
		UE_LOG(LogTemp, Warning, TEXT("Could not finish this wall - see error above. Press Esc to discard and redraw."));
		OnActionFailed.Broadcast(TEXT("Could not create the wall - check that the id isn't already used."));
		return nullptr;
	}

	bHasUnsavedChanges = true;
	FlushPersistentDebugLines(GetWorld());
	PendingRoomFootprint.Reset();
	OnRoomPointCountChanged.Broadcast(0);
	return NewWall;
}

void ANavPlayerController::CancelWall()
{
	FlushPersistentDebugLines(GetWorld());
	PendingRoomFootprint.Reset();
	OnRoomPointCountChanged.Broadcast(0);
}

APolyBuilding* ANavPlayerController::CreateRectangleBuilding(const FString& Id, const FString& Name, FVector2D Position, float Width, float Depth, float RotationDegrees, int32 FloorCount, float FloorHeight)
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRectangleBuilding: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."));
		return nullptr;
	}

	APolyBuilding* Result = Generator->CreateRectangleBuilding(Id, Name, Position, Width, Depth, RotationDegrees, FloorCount, FloorHeight);
	if (!Result)
	{
		OnActionFailed.Broadcast(TEXT("Could not create the building - check that the id isn't already used and the dimensions/floor count are valid."));
	}
	else
	{
		bHasUnsavedChanges = true;
	}
	return Result;
}

APolyBuilding* ANavPlayerController::CreateRegularPolygonBuilding(const FString& Id, const FString& Name, FVector2D Position, int32 Sides, float Radius, float RotationDegrees, int32 FloorCount, float FloorHeight)
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRegularPolygonBuilding: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."));
		return nullptr;
	}

	APolyBuilding* Result = Generator->CreateRegularPolygonBuilding(Id, Name, Position, Sides, Radius, RotationDegrees, FloorCount, FloorHeight);
	if (!Result)
	{
		OnActionFailed.Broadcast(TEXT("Could not create the building - check that the id isn't already used and the sides/radius/floor count are valid."));
	}
	else
	{
		bHasUnsavedChanges = true;
	}
	return Result;
}

FString ANavPlayerController::BrowseForBuildingDataFile()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		UE_LOG(LogTemp, Error, TEXT("BrowseForBuildingDataFile: desktop platform not available"));
		OnActionFailed.Broadcast(TEXT("Could not open the file picker on this system."));
		return FString();
	}

	const void* ParentWindowHandle = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (ActiveWindow.IsValid() && ActiveWindow->GetNativeWindow().IsValid())
		{
			ParentWindowHandle = ActiveWindow->GetNativeWindow()->GetOSWindowHandle();
		}
	}

	TArray<FString> OutFiles;
	const bool bPicked = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		TEXT("Import building data"),
		TEXT(""),
		TEXT(""),
		TEXT("JSON files (*.json)|*.json"),
		EFileDialogFlags::None,
		OutFiles);

	if (!bPicked || OutFiles.Num() == 0)
	{
		// User cancelled the dialog - not a failure, nothing to report.
		return FString();
	}

	return OutFiles[0];
}

int32 ANavPlayerController::LoadBuildingDataFromPath(const FString& FilePath)
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadBuildingDataFromPath: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."));
		return 0;
	}

	if (FilePath.IsEmpty())
	{
		OnActionFailed.Broadcast(TEXT("No file selected - click Browse first."));
		return 0;
	}
	if (NavigationPanel)
	{
		NavigationPanel->ClearAll();
		NavigationPanel->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (InteriorGen)
	{
		InteriorGen->ClearLoadedInterior();
	}
	Generator->ClearAllBuildings();

	const int32 Count = Generator->LoadBuildingsFromFile(FilePath);
	UE_LOG(LogTemp, Warning, TEXT("LoadBuildingDataFromPath: loaded %d buildings from %s"), Count, *FilePath);
	if (Count == 0)
	{
		OnActionFailed.Broadcast(TEXT("No buildings were loaded from that file - check that it matches the expected JSON format."));
	}
	else
	{
		// This file is now what Ctrl+S saves back to.
		CurrentProjectFilePath = FilePath;
		bHasUnsavedChanges = false;
		FocusCameraOnAllBuildings();
	}
	return Count;
}

void ANavPlayerController::StartNewProject()
{
	if (NavigationPanel)
	{
		NavigationPanel->ClearAll();
		NavigationPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (InteriorGen)
	{
		InteriorGen->ClearLoadedInterior();
	}
	if (Generator)
	{
		Generator->ClearAllBuildings();
	}

	CurrentProjectFilePath.Empty();
	bHasUnsavedChanges = false;
	NextNewBuildingIndex = 1;

	// Defensively reset any in-progress mode/selection state too, in case New is ever reachable
	// mid-edit rather than only from the start screen.
	bBuildMode = false;
	PendingFootprint.Reset();
	bDrawRoomMode = false;
	bDrawWallMode = false;
	bSelectMode = false;
	ActivePrefabShape = 0;
	bAddDoorMode = false;
	SelectedRoomId.Empty();
	SelectedDoorRoomId.Empty();
	SelectedDoorId.Empty();
	PendingRoomFootprint.Reset();
	SelectedBuilding = nullptr;

	FocusCameraOnAllBuildings();

	UE_LOG(LogTemp, Warning, TEXT("Started a new, blank project"));
}

void ANavPlayerController::FocusCameraOnBuilding(APolyBuilding* Building)
{
	if (!Building)
	{
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	if (!bHasSavedCameraView)
	{
		SavedCameraLocation = ControlledPawn->GetActorLocation();
		SavedCameraRotation = GetControlRotation();
		bHasSavedCameraView = true;
	}

	const int32 Level = InteriorGen ? InteriorGen->GetActiveFloor() : 1;
	PositionCameraForFloor(Building, Level);
}

void ANavPlayerController::PositionCameraForFloor(APolyBuilding* Building, int32 Level)
{
	if (!Building)
	{
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	const float Scale = Generator ? Generator->GetScale() : 1.f;
	const TArray<FVector2D>& Footprint = Building->GetFootprint();
	if (Footprint.Num() == 0)
	{
		return;
	}

	FVector2D MinPoint(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D MaxPoint(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	for (const FVector2D& Point : Footprint)
	{
		const FVector2D WorldPoint = (Point + Building->BuildingOffset) * Scale;
		MinPoint.X = FMath::Min(MinPoint.X, WorldPoint.X);
		MinPoint.Y = FMath::Min(MinPoint.Y, WorldPoint.Y);
		MaxPoint.X = FMath::Max(MaxPoint.X, WorldPoint.X);
		MaxPoint.Y = FMath::Max(MaxPoint.Y, WorldPoint.Y);
	}

	const FVector2D Center = (MinPoint + MaxPoint) * 0.5f;
	const float Span = FMath::Max(MaxPoint.X - MinPoint.X, MaxPoint.Y - MinPoint.Y);

	// Just above the selected floor's own ceiling (where its now-hidden slab would be), not above
	// the whole building - keeps a consistent close-up framing no matter how many floors sit above
	// whichever one is currently being edited.
	const float FloorCeilingZ = Level * Building->GetFloorHeight();
	const float ViewHeight = FloorCeilingZ + FMath::Max(Span * 1.5f, 500.f) + 1000.f;

	const FVector NewLocation(Center.X, Center.Y, ViewHeight);
	// Not perfectly vertical (-90) on purpose: dead-straight-down makes vertical walls render
	// edge-on (effectively invisible, since they'd have zero apparent width) - a slight tilt keeps
	// them visible as a readable floor plan while still reading as "top-down".
	const FRotator NewRotation(-80.f, 0.f, 0.f);

	ControlledPawn->SetActorLocationAndRotation(NewLocation, NewRotation);
	SetControlRotation(NewRotation);

	// Only that floor's own ceiling (or the roof, if it's the top floor) needs to hide - the camera
	// now sits just above it, so nothing further up blocks the view.
	Building->SetVisibleInteriorLevel(Level);
	InteriorBuildingHiddenById = Building->BuildingId;
}

void ANavPlayerController::RepositionCameraToActiveFloor()
{
	if (!InteriorGen || !Generator)
	{
		return;
	}

	if (APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId()))
	{
		PositionCameraForFloor(Building, InteriorGen->GetActiveFloor());
	}
}

void ANavPlayerController::FocusCameraOnAllBuildings()
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !Generator)
	{
		return;
	}

	const TArray<APolyBuilding*> Buildings = Generator->GetAllBuildings();
	if (Buildings.Num() == 0)
	{
		// Nothing to frame yet (e.g. a brand new blank project) - just pull the camera up above the
		// origin instead of leaving it wherever PlayerStart put it.
		const FVector DefaultLocation(0.f, 0.f, 3000.f);
		const FRotator DefaultRotation(-50.f, 0.f, 0.f);
		ControlledPawn->SetActorLocationAndRotation(DefaultLocation, DefaultRotation);
		SetControlRotation(DefaultRotation);
		return;
	}

	const float Scale = Generator->GetScale();
	FVector2D MinPoint(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D MaxPoint(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	for (const APolyBuilding* Building : Buildings)
	{
		if (!Building)
		{
			continue;
		}
		for (const FVector2D& Point : Building->GetFootprint())
		{
			const FVector2D WorldPoint = (Point + Building->BuildingOffset) * Scale;
			MinPoint.X = FMath::Min(MinPoint.X, WorldPoint.X);
			MinPoint.Y = FMath::Min(MinPoint.Y, WorldPoint.Y);
			MaxPoint.X = FMath::Max(MaxPoint.X, WorldPoint.X);
			MaxPoint.Y = FMath::Max(MaxPoint.Y, WorldPoint.Y);
		}
	}

	const FVector2D Center = (MinPoint + MaxPoint) * 0.5f;
	const float Span = FMath::Max(MaxPoint.X - MinPoint.X, MaxPoint.Y - MinPoint.Y);
	const float ViewHeight = FMath::Max(Span * 1.2f, 2000.f);

	const FVector NewLocation(Center.X, Center.Y, ViewHeight);
	const FRotator NewRotation(-50.f, 0.f, 0.f);

	ControlledPawn->SetActorLocationAndRotation(NewLocation, NewRotation);
	SetControlRotation(NewRotation);
}

void ANavPlayerController::RestoreExteriorCameraView()
{
	if (NavigationPanel)
	{
		NavigationPanel->ClearAll();
		NavigationPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	// Leaving interior editing entirely - cancel any in-progress draw room/wall/delete/prefab mode
	// so it doesn't keep intercepting clicks/Enter/Esc on whatever screen comes next.
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitAddDoorMode();
	ExitConnectorMode();

	if (!InteriorBuildingHiddenById.IsEmpty() && Generator)
	{
		if (APolyBuilding* HiddenBuilding = Generator->FindBuildingById(InteriorBuildingHiddenById))
		{
			HiddenBuilding->SetVisibleInteriorLevel(0);
		}
		InteriorBuildingHiddenById.Empty();
	}

	// Leaving interior editing entirely - destroy whatever rooms were spawned for it (safe: the
	// data itself lives in BuildingGenerator's InteriorDataById, not in these actors, so nothing is
	// lost; LoadInteriorForBuilding respawns them fresh from that data next time this building's
	// interior is opened). Clear the selection too - it would otherwise point at an actor that no
	// longer exists.
	if (InteriorGen)
	{
		InteriorGen->ClearLoadedInterior();
	}
	SelectRoom(FString());

	if (!bHasSavedCameraView)
	{
		return;
	}

	if (APawn* ControlledPawn = GetPawn())
	{
		ControlledPawn->SetActorLocationAndRotation(SavedCameraLocation, SavedCameraRotation);
		SetControlRotation(SavedCameraRotation);
	}

	bHasSavedCameraView = false;
}

bool ANavPlayerController::SaveCurrentProject()
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveCurrentProject: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."));
		return false;
	}

	if (CurrentProjectFilePath.IsEmpty())
	{
		// No file yet (e.g. a brand-new session) - ask where to save, same dialog Open uses.
		const FString ChosenPath = BrowseForSaveLocation();
		if (ChosenPath.IsEmpty())
		{
			// User cancelled the dialog - not a failure, nothing to report.
			return false;
		}
		return SaveCurrentProjectAs(ChosenPath);
	}

	if (InteriorGen)
	{
		InteriorGen->SyncInteriorToBuildingGenerator();
	}

	const bool bSuccess = Generator->SaveBuildingsToFile(CurrentProjectFilePath);
	if (bSuccess)
	{
		bHasUnsavedChanges = false;
		UE_LOG(LogTemp, Warning, TEXT("Saved to %s"), *CurrentProjectFilePath);
	}
	else
	{
		OnActionFailed.Broadcast(TEXT("Could not save - see the Output Log for details."));
	}
	return bSuccess;
}

FString ANavPlayerController::BrowseForSaveLocation()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		UE_LOG(LogTemp, Error, TEXT("BrowseForSaveLocation: desktop platform not available"));
		OnActionFailed.Broadcast(TEXT("Could not open the save dialog on this system."));
		return FString();
	}

	const void* ParentWindowHandle = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (ActiveWindow.IsValid() && ActiveWindow->GetNativeWindow().IsValid())
		{
			ParentWindowHandle = ActiveWindow->GetNativeWindow()->GetOSWindowHandle();
		}
	}

	TArray<FString> OutFiles;
	const bool bPicked = DesktopPlatform->SaveFileDialog(
		ParentWindowHandle,
		TEXT("Save building data"),
		TEXT(""),
		TEXT("NewProject.json"),
		TEXT("JSON files (*.json)|*.json"),
		EFileDialogFlags::None,
		OutFiles);

	if (!bPicked || OutFiles.Num() == 0)
	{
		// User cancelled the dialog - not a failure, nothing to report.
		return FString();
	}

	return OutFiles[0];
}

bool ANavPlayerController::SaveCurrentProjectAs(const FString& FilePath)
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveCurrentProjectAs: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."));
		return false;
	}

	if (FilePath.IsEmpty())
	{
		OnActionFailed.Broadcast(TEXT("No save location chosen."));
		return false;
	}

	if (InteriorGen)
	{
		InteriorGen->SyncInteriorToBuildingGenerator();
	}

	const bool bSuccess = Generator->SaveBuildingsToFile(FilePath);
	if (bSuccess)
	{
		CurrentProjectFilePath = FilePath;
		bHasUnsavedChanges = false;
		UE_LOG(LogTemp, Warning, TEXT("Saved to %s"), *FilePath);
	}
	else
	{
		OnActionFailed.Broadcast(TEXT("Could not save - see the Output Log for details."));
	}
	return bSuccess;
}

void ANavPlayerController::SaveCurrentProjectKeyHandler()
{
	SaveCurrentProject();
}

// ---- Permanent Interior pass-throughs (see header) ----

int32 ANavPlayerController::LoadInteriorForBuilding(const FString& BuildingId)
{
	if (!InteriorGen)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadInteriorForBuilding: no InteriorGenerator placed in the level"));
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."));
		return -1;
	}

	const int32 Result = InteriorGen->LoadInteriorForBuilding(BuildingId);
	if (Result < 0)
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not load the interior for '%s' - no building with that id exists."), *BuildingId));
	}
	else if (Generator)
	{
		FocusCameraOnBuilding(Generator->FindBuildingById(BuildingId));
		if (NavigationPanel)
		{
			NavigationPanel->ClearAll();
			// The navigation widget fills the viewport and sits above WBP_InteriorScreen.
			// Keep the outer widget out of hit testing so its empty canvas does not block
			// the room-editing controls below it. Its visible children (the navigation
			// panel and buttons) remain hit-testable.
			NavigationPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			UE_LOG(LogTemp, Warning,
				TEXT("[Navigation] Navigation panel made visible after loading interior: building=%s floor=%d"),
				*BuildingId, InteriorGen->GetActiveFloor());
		}
	}
	return Result;
}

void ANavPlayerController::SetActiveFloor(int32 Level)
{
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."));
		return;
	}

	InteriorGen->SetActiveFloor(Level);
	RepositionCameraToActiveFloor();
}

int32 ANavPlayerController::GetActiveFloor() const
{
	return InteriorGen ? InteriorGen->GetActiveFloor() : 0;
}

int32 ANavPlayerController::AddFloor()
{
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."));
		return -1;
	}

	const int32 NewLevel = InteriorGen->AddFloor();
	if (NewLevel < 0)
	{
		OnActionFailed.Broadcast(TEXT("Could not add a floor - load a building's interior first."));
	}
	else
	{
		bHasUnsavedChanges = true;
		RepositionCameraToActiveFloor();
	}
	return NewLevel;
}

bool ANavPlayerController::RemoveFloor(int32 Level)
{
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."));
		return false;
	}

	const bool bSuccess = InteriorGen->RemoveFloor(Level);
	if (!bSuccess)
	{
		OnActionFailed.Broadcast(TEXT("Could not remove that floor - it may not exist, or it's the last remaining floor."));
	}
	else
	{
		bHasUnsavedChanges = true;
		RepositionCameraToActiveFloor();
	}
	return bSuccess;
}

FString ANavPlayerController::GetLoadedBuildingId() const
{
	return InteriorGen ? InteriorGen->GetLoadedBuildingId() : FString();
}

int32 ANavPlayerController::GetLoadedBuildingFloorCount() const
{
	if (!InteriorGen || !Generator)
	{
		return 0;
	}

	const APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	return Building ? Building->GetFloorCount() : 0;
}

FString ANavPlayerController::PeekNextRoomId() const
{
	return InteriorGen ? InteriorGen->PeekNextRoomId() : FString();
}

const TArray<FInteriorNavNode>& ANavPlayerController::GetNavNodes() const
{
	static const TArray<FInteriorNavNode> EmptyNavNodes;
	return InteriorGen ? InteriorGen->GetNavNodes() : EmptyNavNodes;
}

FString ANavPlayerController::GetSelectedBuildingId() const
{
	return SelectedBuilding ? SelectedBuilding->BuildingId : FString();
}

// ---- TEST-ONLY keyboard hooks - route through the pass-throughs above ----

void ANavPlayerController::CycleInteriorFloorUp()
{
	SetActiveFloor(GetActiveFloor() + 1);
	UE_LOG(LogTemp, Warning, TEXT("Active interior floor: %d"), GetActiveFloor());
}

void ANavPlayerController::CycleInteriorFloorDown()
{
	SetActiveFloor(GetActiveFloor() - 1);
	UE_LOG(LogTemp, Warning, TEXT("Active interior floor: %d"), GetActiveFloor());
}

void ANavPlayerController::AddInteriorFloor()
{
	AddFloor();
}

void ANavPlayerController::RemoveActiveInteriorFloor()
{
	RemoveFloor(GetActiveFloor());
}

void ANavPlayerController::GrowSelectedRoom()
{
	NudgeSelectedRoomSize(20.f);
}

void ANavPlayerController::ShrinkSelectedRoom()
{
	NudgeSelectedRoomSize(-20.f);
}

void ANavPlayerController::NudgeSelectedRoomSize(float Delta)
{
	if (SelectedRoomId.IsEmpty() || !InteriorGen)
	{
		return;
	}

	for (AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
	{
		if (!Room || Room->RoomId != SelectedRoomId)
		{
			continue;
		}

		const TArray<FVector2D>& Footprint = Room->GetFootprint();
		if (Footprint.Num() == 0)
		{
			return;
		}

		FVector2D MinPoint(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
		FVector2D MaxPoint(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
		for (const FVector2D& Point : Footprint)
		{
			MinPoint.X = FMath::Min(MinPoint.X, Point.X);
			MinPoint.Y = FMath::Min(MinPoint.Y, Point.Y);
			MaxPoint.X = FMath::Max(MaxPoint.X, Point.X);
			MaxPoint.Y = FMath::Max(MaxPoint.Y, Point.Y);
		}

		const float NewWidth = FMath::Max(10.f, (MaxPoint.X - MinPoint.X) + Delta);
		const float NewDepth = FMath::Max(10.f, (MaxPoint.Y - MinPoint.Y) + Delta);
		ResizeRoom(SelectedRoomId, NewWidth, NewDepth);
		return;
	}
}

void ANavPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bRotatingCCW == bRotatingCW)
	{
		// Neither held, or both held at once (cancel out) - nothing to do.
		return;
	}

	const float Direction = bRotatingCCW ? -1.f : 1.f;
	RotateSelectedRoom(Direction * RotationDegreesPerSecond * DeltaSeconds);
}

void ANavPlayerController::RotateSelectedRoom(float DeltaDegrees)
{
	if (SelectedRoomId.IsEmpty() || !InteriorGen)
	{
		return;
	}

	if (InteriorGen->RotateRoom(SelectedRoomId, DeltaDegrees))
	{
		bHasUnsavedChanges = true;
	}
}

void ANavPlayerController::ConfirmShapeTest()
{
	// TEMPORARY TEST INPUT — stands in for Padre's UI form (shape dropdown + parameter fields)
	// until it exists. Once the UI calls Generator->CreateRectangleBuilding(...) /
	// CreateRegularPolygonBuilding(...) directly, delete this function and the R-key/shape-mode
	// plumbing above. Those Create...Building functions themselves are the permanent part.
	if (!Generator)
	{
		return;
	}

	const FString FilePath = FPaths::ProjectDir() / TEXT("Data") / TEXT("test_shape_params.json");

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("ConfirmShapeTest: could not read %s"), *FilePath);
		return;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("ConfirmShapeTest: failed to parse %s"), *FilePath);
		return;
	}

	FString Shape = TEXT("rectangle");
	RootObject->TryGetStringField(TEXT("shape"), Shape);

	const FString Id = RootObject->GetStringField(TEXT("id"));
	const FString Name = RootObject->GetStringField(TEXT("name"));

	FVector2D Position(0.f, 0.f);
	const TArray<TSharedPtr<FJsonValue>>* PositionArray;
	if (RootObject->TryGetArrayField(TEXT("position"), PositionArray) && PositionArray->Num() >= 2)
	{
		Position.X = (*PositionArray)[0]->AsNumber();
		Position.Y = (*PositionArray)[1]->AsNumber();
	}

	float RotationDegrees = 0.f;
	int32 FloorCount = 5;
	float FloorHeight = 300.f;
	RootObject->TryGetNumberField(TEXT("rotationDegrees"), RotationDegrees);
	RootObject->TryGetNumberField(TEXT("floorCount"), FloorCount);
	RootObject->TryGetNumberField(TEXT("floorHeight"), FloorHeight);

	APolyBuilding* NewBuilding = nullptr;

	if (Shape == TEXT("polygon"))
	{
		int32 Sides = 6;
		float Radius = 200.f;
		RootObject->TryGetNumberField(TEXT("sides"), Sides);
		RootObject->TryGetNumberField(TEXT("radius"), Radius);

		NewBuilding = Generator->CreateRegularPolygonBuilding(Id, Name, Position, Sides, Radius, RotationDegrees, FloorCount, FloorHeight);
	}
	else
	{
		float Width = 300.f;
		float Depth = 300.f;
		RootObject->TryGetNumberField(TEXT("width"), Width);
		RootObject->TryGetNumberField(TEXT("depth"), Depth);

		NewBuilding = Generator->CreateRectangleBuilding(Id, Name, Position, Width, Depth, RotationDegrees, FloorCount, FloorHeight);
	}

	if (!NewBuilding)
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not create building - see error above. Edit the id in test_shape_params.json and try again."));
	}
}
