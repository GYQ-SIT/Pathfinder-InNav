// Fill out your copyright notice in the Description page of Project Settings.


#include "NavPlayerController.h"
#include "NavigationPanelWidget.h"
#include "InteriorFloorPlanWidget.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Pawn.h"
#include "PolyBuilding.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "InteriorConnectorMarker.h"
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
#include "HAL/FileManager.h"
#include "RuntimeAudioImporterLibrary.h"
#include "Sound/ImportedSoundWave.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"

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
		// Keeps the 3D preview's route/start/destination markers live as the user picks points and
		// finds/clears a route - see RefreshRoute3DVisualization's own comment.
		NavigationPanel->OnStateChanged.AddUniqueDynamic(this, &ANavPlayerController::RefreshRoute3DVisualization);
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
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ANavPlayerController::ZoomIn3DPreview);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ANavPlayerController::ZoomOut3DPreview);
	// Separate from the "SelectBuilding" action above - carries the 3D Edit mode room-drag gesture
	// (see OnLeftMouseDown3DEdit/Up), while OnClick still fires for the plain-click (select/draw/etc.)
	// side of the same button.
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ANavPlayerController::OnLeftMouseDown3DEdit);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ANavPlayerController::OnLeftMouseUp3DEdit);
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
		// 3D preview mode is view-only - InteriorFloorPlanWidget is Collapsed (and so already
		// un-clickable) while it's active, but this fallback path bypasses that widget entirely, so
		// it needs its own explicit guard.
		if (bIsIn3DPreviewMode)
		{
			return;
		}

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
		// In 2D, interior-editing clicks are handled by InteriorFloorPlanWidget; in plain 3D View
		// mode, they're an intentional no-op (see IsIn3DPreviewMode's own comment). Only 3D Edit mode
		// routes them anywhere.
		if (bIsIn3DPreviewMode && bIsIn3DEditMode)
		{
			Handle3DEditClick();
		}
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
	// The yellow "selected building" highlight only makes sense when browsing the exterior building
	// cluster - not while previewing one specific building's interior in 3D, where the same click
	// would otherwise land on that building's own (now visible, collidable) exterior wall.
	if (bIsIn3DPreviewMode)
	{
		return;
	}

	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	APolyBuilding* ClickedBuilding = Hit.bBlockingHit ? Cast<APolyBuilding>(Hit.GetActor()) : nullptr;

	// Clicking empty space, or re-clicking the already-selected building, deselects it - previously
	// this only ever added or switched a selection, with no way to clear it once nothing new was hit.
	if (!ClickedBuilding || ClickedBuilding == SelectedBuilding)
	{
		if (SelectedBuilding)
		{
			SelectedBuilding->SetHighlighted(false);
			SelectedBuilding = nullptr;
			OnBuildingSelected.Broadcast(FString());
		}
		return;
	}

	if (SelectedBuilding)
	{
		SelectedBuilding->SetHighlighted(false);
	}

	ClickedBuilding->SetHighlighted(true);
	SelectedBuilding = ClickedBuilding;
	OnBuildingSelected.Broadcast(ClickedBuilding->BuildingId);
	// Only exterior building selection gets this sound - deselecting (above) and every other kind of
	// selection in the app (rooms/doors/connectors in the interior editor) don't go through here.
	PlayBuildingSelectSound();

	UE_LOG(LogTemp, Warning, TEXT("Selected building: %s"), *ClickedBuilding->BuildingName);
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
	OnEditModeChanged.Broadcast();

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
	OnEditModeChanged.Broadcast();

	UE_LOG(LogTemp, Warning, TEXT("Draw wall mode ON (floor %d): click to add points, Enter to finish, Esc to cancel"), InteriorGen->GetActiveFloor());
}

void ANavPlayerController::ExitDrawRoomMode()
{
	if (bDrawRoomMode)
	{
		CancelRoom();
		bDrawRoomMode = false;
		OnDrawRoomModeChanged.Broadcast(false);
		OnEditModeChanged.Broadcast();
	}
	if (bDrawWallMode)
	{
		CancelWall();
		bDrawWallMode = false;
		OnDrawWallModeChanged.Broadcast(false);
		OnEditModeChanged.Broadcast();
	}

	UE_LOG(LogTemp, Warning, TEXT("Draw room/wall mode OFF"));
}

void ANavPlayerController::ExitSelectMode()
{
	// A 3D room drag needs select mode to have started it (see OnLeftMouseDown3DEdit) - if select
	// mode is turned off mid-drag (self-toggle, Esc, or another mode taking over), finish the drag
	// the same way a mouse-up would, rather than leaving it stuck with SetIgnoreLookInput(true) never
	// balanced back to false. No-op if no drag is in progress.
	OnLeftMouseUp3DEdit();

	if (bSelectMode)
	{
		bSelectMode = false;
		OnSelectModeChanged.Broadcast(false);
		OnEditModeChanged.Broadcast();
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
	OnEditModeChanged.Broadcast();

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
		OnEditModeChanged.Broadcast();
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
	OnEditModeChanged.Broadcast();
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
		OnEditModeChanged.Broadcast();
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
	OnEditModeChanged.Broadcast();
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
		OnEditModeChanged.Broadcast();
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
	OnEditModeChanged.Broadcast();
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
	// Tracked (rather than an early-return once SelectedRoomId itself is unchanged) so
	// RefreshSelectionHighlight still fires when this call's only real effect was clearing a
	// previously-selected door/connector - e.g. deselecting by clicking empty space while a connector,
	// not a room, was selected (SelectedRoomId was already empty on both sides of that call).
	bool bChanged = false;

	if (!SelectedDoorId.IsEmpty())
	{
		SelectedDoorRoomId.Empty();
		SelectedDoorId.Empty();
		OnSelectedDoorChanged.Broadcast(FString(), FString());
		bChanged = true;
	}
	if (!SelectedConnectorGroupId.IsEmpty())
	{
		SelectedConnectorGroupId.Empty();
		OnSelectedConnectorChanged.Broadcast(FString());
		bChanged = true;
	}

	if (SelectedRoomId != RoomId)
	{
		SelectedRoomId = RoomId;
		OnSelectedRoomChanged.Broadcast(SelectedRoomId);
		bChanged = true;
	}

	if (bChanged)
	{
		RefreshSelectionHighlight();
	}
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
	RefreshSelectionHighlight();
}

void ANavPlayerController::SelectConnector(const FString& GroupId)
{
	// See SelectRoom's own comment on why this is tracked rather than an early return.
	bool bChanged = false;

	if (!SelectedRoomId.IsEmpty())
	{
		SelectedRoomId.Empty();
		OnSelectedRoomChanged.Broadcast(FString());
		bChanged = true;
	}
	if (!SelectedDoorId.IsEmpty())
	{
		SelectedDoorRoomId.Empty();
		SelectedDoorId.Empty();
		OnSelectedDoorChanged.Broadcast(FString(), FString());
		bChanged = true;
	}

	if (SelectedConnectorGroupId != GroupId)
	{
		SelectedConnectorGroupId = GroupId;
		OnSelectedConnectorChanged.Broadcast(SelectedConnectorGroupId);
		bChanged = true;
	}

	if (bChanged)
	{
		RefreshSelectionHighlight();
	}
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
		OnInteriorContentChanged.Broadcast();
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
		OnInteriorContentChanged.Broadcast();
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
	// See NudgeSelectedRoomSize's own comment - same View-mode edit-proofing.
	if (bIsIn3DPreviewMode && !bIsIn3DEditMode)
	{
		return false;
	}

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
	OnInteriorContentChanged.Broadcast();
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
	OnInteriorContentChanged.Broadcast();
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

void ANavPlayerController::GetBuildingFootprintBounds(APolyBuilding* Building, FVector2D& OutCenter, float& OutSpan) const
{
	OutCenter = FVector2D::ZeroVector;
	OutSpan = 0.f;
	if (!Building)
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

	OutCenter = (MinPoint + MaxPoint) * 0.5f;
	OutSpan = FMath::Max(MaxPoint.X - MinPoint.X, MaxPoint.Y - MinPoint.Y);
}

void ANavPlayerController::ResetFloorFraming(APolyBuilding* Building)
{
	// Always framed from the whole building's own outer footprint, not the current floor's own rooms -
	// framing tightly around whatever's drawn on just this floor meant a sparsely-drawn floor (or one
	// with a single small room) zoomed in dramatically tighter than its neighbors, which felt jarring
	// switching floor to floor. GetBuildingFootprintBounds doesn't vary with Level at all, so this
	// (deliberately) gives every floor of a given building the same distance/center.
	FVector2D Center;
	float Span;
	GetBuildingFootprintBounds(Building, Center, Span);
	ThreeDPreviewCameraDistance = FMath::Clamp(Span * 1.8f, ThreeDPreviewMinCameraDistance, ThreeDPreviewMaxCameraDistance);
	ThreeDPreviewYaw = ThreeDPreviewDefaultYaw;
	ThreeDPreviewPitch = ThreeDPreviewDefaultPitch;
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

	FVector2D Center;
	float Span;
	GetBuildingFootprintBounds(Building, Center, Span);
	if (Span <= 0.f && Center.IsZero())
	{
		return;
	}

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

void ANavPlayerController::PositionCameraFor3DPreview(APolyBuilding* Building, int32 Level)
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

	FVector2D Center;
	float Span;
	GetBuildingFootprintBounds(Building, Center, Span);
	if (Span <= 0.f && Center.IsZero())
	{
		return;
	}

	// Look at the floor's own vertical middle, not its ceiling - a proper 3D view of the floor
	// itself, unlike PositionCameraForFloor's near-top-down floor-plan pose.
	const float FloorHeight = Building->GetFloorHeight();
	const float TargetZ = (Level - 1) * FloorHeight + FloorHeight * 0.5f;
	const FVector Target(Center.X, Center.Y, TargetZ);

	// Oblique/dimetric angle - ThreeDPreviewYaw/Pitch/CameraDistance are persistent state, set by
	// ResetFloorFraming (and Distance also by ZoomIn/Out3DPreview) - this function just reads whatever
	// they currently are. Free WASD/mouse-look then move the actual pawn away from this pose until the
	// next reset (floor switch, or the ResetCamera3DPreview button).
	const FRotator NewRotation(ThreeDPreviewPitch, ThreeDPreviewYaw, 0.f);
	const FVector NewLocation = Target - NewRotation.Vector() * ThreeDPreviewCameraDistance;

	ControlledPawn->SetActorLocationAndRotation(NewLocation, NewRotation);
	SetControlRotation(NewRotation);

	// Same reasoning as PositionCameraForFloor - hide the viewed floor's own ceiling (and everything
	// above it) so the oblique view down isn't blocked.
	Building->SetVisibleInteriorLevel(Level);
	InteriorBuildingHiddenById = Building->BuildingId;
}

void ANavPlayerController::SetOtherBuildingsHidden(bool bNewHidden)
{
	if (!Generator || !InteriorGen)
	{
		return;
	}

	const FString LoadedId = InteriorGen->GetLoadedBuildingId();
	for (APolyBuilding* Building : Generator->GetAllBuildings())
	{
		if (Building && Building->BuildingId != LoadedId)
		{
			Building->SetActorHiddenInGame(bNewHidden);
			Building->SetActorEnableCollision(!bNewHidden);
		}
	}
}

void ANavPlayerController::ZoomIn3DPreview()
{
	// Dollies the pawn along whatever direction it's CURRENTLY facing, rather than recomputing the
	// whole pose from ThreeDPreviewYaw/Pitch/the framing center (what PositionCameraFor3DPreview does)
	// - free WASD/mouse-look (see SetIn3DPreviewMode) can leave those stale the moment the user moves,
	// so calling PositionCameraFor3DPreview here would snap the camera back to the original framing on
	// every scroll tick instead of just zooming from wherever it actually is.
	if (!bIsIn3DPreviewMode)
	{
		return;
	}
	if (APawn* ControlledPawn = GetPawn())
	{
		ControlledPawn->AddActorWorldOffset(GetControlRotation().Vector() * ThreeDPreviewZoomStep);
	}
}

void ANavPlayerController::ZoomOut3DPreview()
{
	if (!bIsIn3DPreviewMode)
	{
		return;
	}
	if (APawn* ControlledPawn = GetPawn())
	{
		ControlledPawn->AddActorWorldOffset(-GetControlRotation().Vector() * ThreeDPreviewZoomStep);
	}
}

void ANavPlayerController::ResetCamera3DPreview()
{
	if (!bIsIn3DPreviewMode || !Generator || !InteriorGen)
	{
		return;
	}

	if (APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId()))
	{
		const int32 Level = InteriorGen->GetActiveFloor();
		ResetFloorFraming(Building);
		PositionCameraFor3DPreview(Building, Level);
	}
}

void ANavPlayerController::IncreaseMoveSpeed3DPreview()
{
	if (!bIsIn3DPreviewMode)
	{
		return;
	}
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(ControlledPawn->GetMovementComponent()))
		{
			Movement->MaxSpeed = FMath::Clamp(Movement->MaxSpeed + ThreeDPreviewMoveSpeedStep,
				ThreeDPreviewMinMoveSpeed, ThreeDPreviewMaxMoveSpeed);
		}
	}
}

void ANavPlayerController::DecreaseMoveSpeed3DPreview()
{
	if (!bIsIn3DPreviewMode)
	{
		return;
	}
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(ControlledPawn->GetMovementComponent()))
		{
			Movement->MaxSpeed = FMath::Clamp(Movement->MaxSpeed - ThreeDPreviewMoveSpeedStep,
				ThreeDPreviewMinMoveSpeed, ThreeDPreviewMaxMoveSpeed);
		}
	}
}

void ANavPlayerController::SetIn3DPreviewMode(bool bEnable)
{
	if (bEnable == bIsIn3DPreviewMode || !Generator || !InteriorGen)
	{
		return;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return;
	}

	bIsIn3DPreviewMode = bEnable;
	if (bEnable)
	{
		ResetFloorFraming(Building);
		PositionCameraFor3DPreview(Building, InteriorGen->GetActiveFloor());
		SetOtherBuildingsHidden(true);
		// Building itself stays visible - PositionCameraFor3DPreview's own SetVisibleInteriorLevel call
		// (see APolyBuilding::ApplySlabVisibility) now hides every OTHER floor's own wall ring and
		// ceiling/roof cap, leaving only the viewed floor's own exterior wall + floor slab showing.
		// WASD/mouse-look stay enabled (the pawn's own default free-fly, unchanged) rather than being
		// locked to a fixed angle - ResetCamera3DPreview (a UI button) snaps back to the default
		// best-fit pose if the user wanders off, instead of constraining movement in the first place.
	}
	else
	{
		// Finish (or revert) any drag still in progress before leaving 3D preview entirely - same
		// reasoning as ExitSelectMode's own call, otherwise SetIgnoreLookInput(true) from a
		// mid-drag exit would never get balanced back to false.
		OnLeftMouseUp3DEdit();
		SetOtherBuildingsHidden(false);
		RepositionCameraToActiveFloor();
		// Always leave 3D preview back in View mode, not however Edit/View happened to be left - no
		// separate OnEdit3DModeChanged broadcast needed, the View/Edit button is already hidden by the
		// OnPreviewModeChanged broadcast below and just re-syncs to View next time 3D is re-entered.
		bIsIn3DEditMode = false;
	}

	// Shows the route/start/destination markers immediately on entry if one already exists (rather
	// than waiting for the next OnStateChanged), and clears them on the way out - the function itself
	// checks bIsIn3DPreviewMode, so this one call handles both.
	RefreshRoute3DVisualization();

	OnPreviewModeChanged.Broadcast(bIsIn3DPreviewMode);
}

void ANavPlayerController::SetIn3DEditMode(bool bEnable)
{
	if (!bIsIn3DPreviewMode || bEnable == bIsIn3DEditMode)
	{
		return;
	}

	if (!bEnable)
	{
		// Same reasoning as ExitSelectMode/SetIn3DPreviewMode's own call - switching back to View
		// mid-drag must still finish (or revert) it and balance SetIgnoreLookInput.
		OnLeftMouseUp3DEdit();
	}

	bIsIn3DEditMode = bEnable;
	OnEdit3DModeChanged.Broadcast(bIsIn3DEditMode);
}

void ANavPlayerController::RefreshSelectionHighlight()
{
	if (AInteriorRoom* PrevRoom = Cast<AInteriorRoom>(HighlightedSelectionActor))
	{
		PrevRoom->SetHighlighted(false);
	}
	else if (AInteriorConnectorMarker* PrevMarker = Cast<AInteriorConnectorMarker>(HighlightedSelectionActor))
	{
		PrevMarker->SetHighlighted(false);
	}
	HighlightedSelectionActor = nullptr;

	if (!InteriorGen)
	{
		return;
	}

	if (!SelectedConnectorGroupId.IsEmpty())
	{
		const int32 ActiveFloor = InteriorGen->GetActiveFloor();
		for (const FInteriorNavNode& Node : InteriorGen->GetNavNodes())
		{
			if (Node.ConnectorGroupId == SelectedConnectorGroupId && Node.Level == ActiveFloor && Node.MarkerActor)
			{
				Node.MarkerActor->SetHighlighted(true);
				HighlightedSelectionActor = Node.MarkerActor;
				break;
			}
		}
		return;
	}

	// A selected door highlights its owning room - there's no separate 3D visual for "just this door".
	const FString RoomIdToHighlight = !SelectedDoorId.IsEmpty() ? SelectedDoorRoomId : SelectedRoomId;
	if (RoomIdToHighlight.IsEmpty())
	{
		return;
	}

	for (AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
	{
		if (Room && Room->RoomId == RoomIdToHighlight)
		{
			Room->SetHighlighted(true);
			HighlightedSelectionActor = Room;
			break;
		}
	}
}

bool ANavPlayerController::Get3DFloorFootprintPoint(const FHitResult& Hit, FVector2D& OutPoint) const
{
	if (!Generator || !InteriorGen)
	{
		return false;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return false;
	}

	const float Scale = Generator->GetScale();
	if (Scale <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	if (Hit.bBlockingHit)
	{
		OutPoint = FVector2D(Hit.Location.X, Hit.Location.Y) / Scale - Building->BuildingOffset;
		return true;
	}

	// Fallback: the trace missed every collidable mesh (e.g. a click just outside the building
	// outline, or through a gap) - intersect the camera ray with the active floor's own Z-plane
	// instead of silently doing nothing.
	FVector RayOrigin, RayDirection;
	if (!DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return false;
	}

	if (FMath::IsNearlyZero(RayDirection.Z))
	{
		// Ray is parallel to the floor plane - no intersection.
		return false;
	}

	const float PlaneZ = (InteriorGen->GetActiveFloor() - 1) * Building->GetFloorHeight();
	const FVector WorldPoint = FMath::RayPlaneIntersection(RayOrigin, RayDirection, FPlane(FVector(0.f, 0.f, PlaneZ), FVector::UpVector));
	OutPoint = FVector2D(WorldPoint.X, WorldPoint.Y) / Scale - Building->BuildingOffset;
	return true;
}

bool ANavPlayerController::FindNearestRoomEdge3D(AInteriorRoom* Room, const FVector& WorldHit, int32& OutEdgeIndex, float& OutOffsetWorldUnits, float MaxDistanceWorldUnits, bool bCenterOffsetForDoorWidth) const
{
	if (!Room || !Generator || !InteriorGen)
	{
		return false;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return false;
	}

	const TArray<FVector2D>& Footprint = Room->GetFootprint();
	const int32 N = Footprint.Num();
	if (N < 2)
	{
		return false;
	}

	const float Scale = Generator->GetScale();
	TArray<FVector2D> WorldPoints;
	WorldPoints.Reserve(N);
	for (const FVector2D& Point : Footprint)
	{
		WorldPoints.Add((Point + Building->BuildingOffset) * Scale);
	}

	constexpr float DoorWidthWorldUnits = 90.f; // matches AInteriorGenerator::AddDoorToRoom's fixed door width
	const FVector2D HitXY(WorldHit.X, WorldHit.Y);
	const int32 NumEdges = Room->IsClosedLoop() ? N : N - 1;

	float BestDistance = TNumericLimits<float>::Max();
	int32 BestEdge = INDEX_NONE;
	float BestOffset = 0.f;

	for (int32 i = 0; i < NumEdges; i++)
	{
		const FVector2D& A = WorldPoints[i];
		const FVector2D& B = WorldPoints[(i + 1) % N];

		const FVector2D AB = B - A;
		const float LengthSq = AB.SizeSquared();
		const float T = (LengthSq > KINDA_SMALL_NUMBER) ? FMath::Clamp(FVector2D::DotProduct(HitXY - A, AB) / LengthSq, 0.f, 1.f) : 0.f;
		const FVector2D ClosestPoint = A + AB * T;
		const float Distance = FVector2D::Distance(HitXY, ClosestPoint);

		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestEdge = i;
			const float EdgeLengthWorldUnits = FVector2D::Distance(A, B);
			const float RawOffsetWorldUnits = T * EdgeLengthWorldUnits;
			BestOffset = bCenterOffsetForDoorWidth
				? FMath::Clamp(RawOffsetWorldUnits - DoorWidthWorldUnits * 0.5f, 0.f, FMath::Max(0.f, EdgeLengthWorldUnits - DoorWidthWorldUnits))
				: RawOffsetWorldUnits;
		}
	}

	if (BestEdge == INDEX_NONE || (MaxDistanceWorldUnits >= 0.f && BestDistance > MaxDistanceWorldUnits))
	{
		return false;
	}

	OutEdgeIndex = BestEdge;
	OutOffsetWorldUnits = BestOffset;
	return true;
}

const FInteriorOpening* ANavPlayerController::FindDoorAtEdgeOffset(AInteriorRoom* Room, int32 EdgeIndex, float OffsetWorldUnits) const
{
	if (!Room)
	{
		return nullptr;
	}

	for (const FInteriorOpening& Opening : Room->GetOpenings())
	{
		if (Opening.Type == EInteriorOpeningType::Door && Opening.EdgeIndex == EdgeIndex &&
			OffsetWorldUnits >= Opening.Offset && OffsetWorldUnits <= Opening.Offset + Opening.Width)
		{
			return &Opening;
		}
	}

	return nullptr;
}

void ANavPlayerController::Select3DEditTarget(const FHitResult& Hit)
{
	if (!Hit.bBlockingHit)
	{
		SelectRoom(FString());
		return;
	}

	if (AInteriorConnectorMarker* Marker = Cast<AInteriorConnectorMarker>(Hit.GetActor()))
	{
		SelectConnector(Marker->GetOwnerGroupId());
		return;
	}

	if (AInteriorRoom* Room = Cast<AInteriorRoom>(Hit.GetActor()))
	{
		int32 EdgeIndex;
		float OffsetWorldUnits;
		constexpr float DoorPickToleranceWorldUnits = 15.f;
		if (FindNearestRoomEdge3D(Room, Hit.Location, EdgeIndex, OffsetWorldUnits, DoorPickToleranceWorldUnits))
		{
			if (const FInteriorOpening* Door = FindDoorAtEdgeOffset(Room, EdgeIndex, OffsetWorldUnits))
			{
				SelectDoor(Room->RoomId, Door->Id);
				return;
			}
		}
		SelectRoom(Room->RoomId);
		return;
	}

	// Hit the building shell or some other uninteresting actor.
	SelectRoom(FString());
}

void ANavPlayerController::Handle3DEditClick()
{
	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	if (bDrawRoomMode || bDrawWallMode)
	{
		FVector2D FootprintPoint;
		if (Get3DFloorFootprintPoint(Hit, FootprintPoint))
		{
			const int32 PrevPointCount = PendingRoomFootprint.Num();
			AddRoomPointAtFootprintPosition(FootprintPoint);
			if (PendingRoomFootprint.Num() > PrevPointCount)
			{
				DrawPendingRoomPointMarker3D();
			}
		}
		return;
	}

	if (ActivePrefabShape != 0)
	{
		FVector2D FootprintPoint;
		if (Get3DFloorFootprintPoint(Hit, FootprintPoint))
		{
			PlacePrefabRoomAt(FootprintPoint);
		}
		return;
	}

	if (ActiveConnectorType != 0)
	{
		FVector2D FootprintPoint;
		if (Get3DFloorFootprintPoint(Hit, FootprintPoint))
		{
			PlaceConnectorAt(FootprintPoint);
		}
		return;
	}

	if (bAddDoorMode)
	{
		if (AInteriorRoom* Room = Cast<AInteriorRoom>(Hit.GetActor()))
		{
			int32 EdgeIndex;
			float OffsetWorldUnits;
			if (FindNearestRoomEdge3D(Room, Hit.Location, EdgeIndex, OffsetWorldUnits, /*MaxDistanceWorldUnits=*/-1.f, /*bCenterOffsetForDoorWidth=*/true))
			{
				AddDoorToWall(Room->RoomId, EdgeIndex, OffsetWorldUnits);
			}
		}
		return;
	}

	if (bSelectMode)
	{
		Select3DEditTarget(Hit);
	}
}

void ANavPlayerController::DrawPendingRoomPointMarker3D()
{
	if (!Generator || !InteriorGen || PendingRoomFootprint.Num() == 0)
	{
		return;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return;
	}

	const float Scale = Generator->GetScale();
	// A little above the floor slab (see AInteriorRoom::RebuildMesh's own FloorSlabZOffset) so the
	// marker doesn't z-fight with it and stays visible from the 3D preview's oblique angle.
	const float Z = (InteriorGen->GetActiveFloor() - 1) * Building->GetFloorHeight() + 10.f;

	auto ToWorld = [&](const FVector2D& FootprintPoint) -> FVector
	{
		return FVector((FootprintPoint.X + Building->BuildingOffset.X) * Scale, (FootprintPoint.Y + Building->BuildingOffset.Y) * Scale, Z);
	};

	const FVector NewWorld = ToWorld(PendingRoomFootprint.Last());
	if (PendingRoomFootprint.Num() > 1)
	{
		const FVector PrevWorld = ToWorld(PendingRoomFootprint[PendingRoomFootprint.Num() - 2]);
		DrawDebugLine(GetWorld(), PrevWorld, NewWorld, FColor::Green, true, -1.f, 0, 5.f);
	}
	DrawDebugSphere(GetWorld(), NewWorld, 20.f, 8, FColor::Green, true);
}

void ANavPlayerController::RefreshRoute3DVisualization()
{
	FlushPersistentDebugLines(GetWorld());

	if (!bIsIn3DPreviewMode || !Generator || !InteriorGen || !NavigationPanel)
	{
		return;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return;
	}

	const float Scale = Generator->GetScale();
	const int32 ActiveFloor = InteriorGen->GetActiveFloor();
	// A little above the floor slab (see AInteriorRoom::RebuildMesh's own FloorSlabZOffset and
	// DrawPendingRoomPointMarker3D's own Z) so the route doesn't z-fight with it.
	const float Z = (ActiveFloor - 1) * Building->GetFloorHeight() + 15.f;

	auto ToWorld = [&](const FVector2D& FootprintPoint) -> FVector
	{
		return FVector((FootprintPoint.X + Building->BuildingOffset.X) * Scale, (FootprintPoint.Y + Building->BuildingOffset.Y) * Scale, Z);
	};

	// Mirrors InteriorFloorPlanWidget::NativePaint's own route-drawing exactly: a segment only draws
	// if BOTH endpoints are on the active floor (a floor-crossing segment, e.g. through an elevator,
	// simply isn't drawn on either floor - same as 2D).
	const TArray<FNavigationRoutePoint>& Route = NavigationPanel->GetRoutePoints();
	for (int32 i = 1; i < Route.Num(); i++)
	{
		if (Route[i - 1].Floor != ActiveFloor || Route[i].Floor != ActiveFloor)
		{
			continue;
		}
		// FColor(255, 140, 0) - orange, matching the 2D route line's own FLinearColor(1.f, 0.55f, 0.f, 1.f).
		DrawDebugLine(GetWorld(), ToWorld(Route[i - 1].Position), ToWorld(Route[i].Position), FColor(255, 140, 0), true, -1.f, 0, 8.f);
	}

	auto DrawEndpoint = [&](const FNavigationRoutePoint& Point, const FColor& Color)
	{
		if (Point.Floor != ActiveFloor)
		{
			return;
		}
		DrawDebugSphere(GetWorld(), ToWorld(Point.Position), 30.f, 12, Color, true);
	};
	if (NavigationPanel->HasStart())
	{
		DrawEndpoint(NavigationPanel->GetStart(), FColor::Green);
	}
	if (NavigationPanel->HasDestination())
	{
		DrawEndpoint(NavigationPanel->GetDestination(), FColor::Red);
	}
}

void ANavPlayerController::OnLeftMouseDown3DEdit()
{
	if (!bIsIn3DPreviewMode || !bIsIn3DEditMode || !bSelectMode || !InteriorGen)
	{
		return;
	}

	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	AInteriorRoom* Room = Hit.bBlockingHit ? Cast<AInteriorRoom>(Hit.GetActor()) : nullptr;
	if (!Room)
	{
		return;
	}

	FVector2D StartFootprintPosition;
	if (!Get3DFloorFootprintPoint(Hit, StartFootprintPosition))
	{
		return;
	}

	// The free-fly pawn's own mouse-look (see SetIn3DPreviewMode's own comment on why it's left
	// enabled) turns the camera on any mouse movement, not just while a dedicated look button is held
	// - without suppressing it here, dragging a room (which moves the mouse while holding the same
	// LeftMouseButton) would also spin the camera underneath the drag. Balanced by the matching
	// SetIgnoreLookInput(false) in OnLeftMouseUp3DEdit below.
	SetIgnoreLookInput(true);

	bIsDragging3DRoom = true;
	Dragging3DRoomId = Room->RoomId;
	DragStart3DRoomFootprint = Room->GetFootprint();
	DragStart3DFootprintPosition = StartFootprintPosition;
}

void ANavPlayerController::OnLeftMouseUp3DEdit()
{
	if (!bIsDragging3DRoom)
	{
		return;
	}

	bIsDragging3DRoom = false;
	SetIgnoreLookInput(false);
	CommitRoomMove(Dragging3DRoomId, DragStart3DRoomFootprint);
	Dragging3DRoomId.Empty();
	DragStart3DRoomFootprint.Reset();
}

void ANavPlayerController::RepositionCameraToActiveFloor()
{
	if (!InteriorGen || !Generator)
	{
		return;
	}

	if (APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId()))
	{
		if (bIsIn3DPreviewMode)
		{
			// Floor switch: reset back to the building's own default distance/angle (see
			// ResetFloorFraming) rather than keeping whatever free-fly/zoom the user left the camera at
			// - PositionCameraFor3DPreview's own Level then aims it at that floor's own vertical middle
			// and hides its ceiling, same framing horizontally, just looking at a different floor.
			ResetFloorFraming(Building);
			PositionCameraFor3DPreview(Building, InteriorGen->GetActiveFloor());
		}
		else
		{
			PositionCameraForFloor(Building, InteriorGen->GetActiveFloor());
		}
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
	const float ViewDistance = FMath::Max(Span * 1.2f, 2000.f);

	// Pull the camera back-and-up along the pitch's own forward vector (same "Target -
	// Rotation.Vector() * Distance" pattern PositionCameraForFloor/PositionCameraFor3DPreview use) so
	// the -50 deg pitch actually looks DOWN AT Center - placing it directly above Center with the same
	// rotation (the old code) points the view down-and-forward from directly overhead, past the
	// buildings entirely, which reads as "not tilted down" even though the pitch value is right.
	const FVector Target(Center.X, Center.Y, 0.f);
	const FRotator NewRotation(-50.f, 0.f, 0.f);
	const FVector NewLocation = Target - NewRotation.Vector() * ViewDistance;

	ControlledPawn->SetActorLocationAndRotation(NewLocation, NewRotation);
	SetControlRotation(NewRotation);
}

void ANavPlayerController::RestoreExteriorCameraView()
{
	// Leaving interior editing entirely (any Back path) needs to unwind 3D preview mode first if it
	// was left active - otherwise SetOtherBuildingsHidden(true)'s hide never gets undone (every other
	// building stays hidden even back on the exterior cluster view) and SetIgnoreMoveInput/
	// SetIgnoreLookInput stay latched forever (the PlayerController outlives this screen, so a later
	// reload would come up with the camera still locked). Must run before InteriorGen->
	// ClearLoadedInterior() below, since SetIn3DPreviewMode(false) still needs to look up the
	// currently-loaded building.
	if (bIsIn3DPreviewMode)
	{
		SetIn3DPreviewMode(false);
	}

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

	// Reset EVERY building's VisibleInteriorLevel, not just InteriorBuildingHiddenById (whichever one
	// was interior-edited most recently) - switching which building is being edited mid-session (the
	// building switcher, or entering/leaving 3D preview for a different building) can leave an EARLIER
	// building's VisibleInteriorLevel stuck non-zero too, since only the latest one ever got tracked.
	// Harmless to call on a building that's already fully visible.
	if (Generator)
	{
		for (APolyBuilding* Building : Generator->GetAllBuildings())
		{
			if (Building)
			{
				Building->SetVisibleInteriorLevel(0);
			}
		}
	}
	InteriorBuildingHiddenById.Empty();

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
		OnProjectSaved.Broadcast();
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
		OnProjectSaved.Broadcast();
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

	// Switching straight to a different building's interior (e.g. via the building switcher) while
	// still in 3D preview mode for the PREVIOUS building would otherwise leave that previous building
	// hidden forever (SetOtherBuildingsHidden's hide never gets undone) and move/look input still
	// ignored - unwind it first, same reasoning as RestoreExteriorCameraView. Must run before
	// InteriorGen->LoadInteriorForBuilding below overwrites which building is "loaded".
	if (bIsIn3DPreviewMode)
	{
		SetIn3DPreviewMode(false);
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
	// A selected connector's marker is a separate actor per floor (see
	// AInteriorGenerator::SetActiveFloor's own per-floor hide/show) - re-sync which one is tinted so
	// the 3D Edit mode highlight follows the connector onto whichever of its floors is now visible,
	// instead of staying stuck on the floor it was originally selected on.
	RefreshSelectionHighlight();
	// The route/start/destination markers are per-floor too (see RefreshRoute3DVisualization's own
	// active-floor filtering) - redraw for whichever floor is now active.
	RefreshRoute3DVisualization();
	OnActiveFloorChanged.Broadcast();
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
		OnActiveFloorChanged.Broadcast();
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
		OnActiveFloorChanged.Broadcast();
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

TArray<FString> ANavPlayerController::GetRoomIdsOnFloor(int32 Level) const
{
	TArray<FString> RoomIds;
	if (!InteriorGen)
	{
		return RoomIds;
	}

	RoomIds.Reserve(InteriorGen->GetRoomsOnFloor(Level).Num());
	for (const AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(Level))
	{
		if (Room)
		{
			RoomIds.Add(Room->RoomId);
		}
	}
	return RoomIds;
}

FString ANavPlayerController::GetRoomName(const FString& RoomId) const
{
	return InteriorGen ? InteriorGen->GetRoomName(RoomId) : FString();
}

bool ANavPlayerController::IsRoomClosedLoop(const FString& RoomId) const
{
	return InteriorGen && InteriorGen->IsRoomClosedLoop(RoomId);
}

void ANavPlayerController::SetRoomHidden(const FString& RoomId, bool bNewHidden)
{
	if (bNewHidden)
	{
		HiddenRoomIds.Add(RoomId);
	}
	else
	{
		HiddenRoomIds.Remove(RoomId);
	}
}

void ANavPlayerController::SetPathNodeHidden(const FString& NodeId, bool bNewHidden)
{
	if (bNewHidden)
	{
		HiddenPathNodeIds.Add(NodeId);
	}
	else
	{
		HiddenPathNodeIds.Remove(NodeId);
	}
}

void ANavPlayerController::SetIONodeHidden(const FString& NodeId, bool bNewHidden)
{
	if (bNewHidden)
	{
		HiddenIONodeIds.Add(NodeId);
	}
	else
	{
		HiddenIONodeIds.Remove(NodeId);
	}
}

FString ANavPlayerController::GetSelectedBuildingId() const
{
	return SelectedBuilding ? SelectedBuilding->BuildingId : FString();
}

TArray<FString> ANavPlayerController::GetAllBuildingNames() const
{
	TArray<FString> Names;
	if (!Generator)
	{
		return Names;
	}

	for (const FString& Id : Generator->GetAllBuildingIds())
	{
		Names.Add(Generator->GetBuildingName(Id));
	}
	return Names;
}

FString ANavPlayerController::FindBuildingIdByName(const FString& Name) const
{
	return Generator ? Generator->FindBuildingIdByName(Name) : FString();
}

FString ANavPlayerController::GetLoadedBuildingName() const
{
	if (!InteriorGen || !Generator)
	{
		return FString();
	}
	return Generator->GetBuildingName(InteriorGen->GetLoadedBuildingId());
}

bool ANavPlayerController::SetLoadedBuildingName(const FString& NewName)
{
	if (!InteriorGen || !Generator)
	{
		return false;
	}

	const FString LoadedId = InteriorGen->GetLoadedBuildingId();
	if (LoadedId.IsEmpty())
	{
		return false;
	}

	const bool bSuccess = Generator->SetBuildingName(LoadedId, NewName);
	if (bSuccess)
	{
		bHasUnsavedChanges = true;
	}
	else
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not rename '%s' to '%s' - that name is already in use."), *LoadedId, *NewName));
	}
	return bSuccess;
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
	// 3D View mode is genuinely edit-proof, not just "safe because the 2D widget that would trigger
	// this is hidden" - a room/connector selected before entering 3D preview would otherwise still
	// respond to a stray =/- press with nothing on screen to show it happened.
	if (bIsIn3DPreviewMode && !bIsIn3DEditMode)
	{
		return;
	}

	// Grow/Shrink work on whichever kind of thing is currently selected - a connector (elevator/
	// stairs/escalator) has no width/depth of its own, just a uniform SizeScale multiplier, so this
	// branches off to a completely different code path (Delta's 20-unit convention below doesn't
	// apply there at all - ConnectorScaleStep is its own, separate step size).
	if (!SelectedConnectorGroupId.IsEmpty() && InteriorGen)
	{
		const float ScaleDelta = (Delta > 0.f) ? ConnectorScaleStep : -ConnectorScaleStep;
		if (InteriorGen->ScaleConnectorGroup(SelectedConnectorGroupId, ScaleDelta))
		{
			bHasUnsavedChanges = true;
		}
		return;
	}

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

	if (ActiveAudioPlaybacks.Num() > 0)
	{
		PruneFinishedAudioPlaybacks();
	}

	if (bIsDragging3DRoom)
	{
		FHitResult Hit;
		GetHitResultUnderCursor(ECC_Visibility, false, Hit);

		FVector2D CurrentFootprintPosition;
		if (Get3DFloorFootprintPoint(Hit, CurrentFootprintPosition))
		{
			const FVector2D Delta = CurrentFootprintPosition - DragStart3DFootprintPosition;

			TArray<FVector2D> NewFootprint;
			NewFootprint.Reserve(DragStart3DRoomFootprint.Num());
			for (const FVector2D& Point : DragStart3DRoomFootprint)
			{
				NewFootprint.Add(Point + Delta);
			}

			MoveRoomTo(Dragging3DRoomId, NewFootprint);
		}
	}

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
	// See NudgeSelectedRoomSize's own comment - same View-mode edit-proofing.
	if (bIsIn3DPreviewMode && !bIsIn3DEditMode)
	{
		return;
	}

	// Same room-vs-connector branch as NudgeSelectedRoomSize - see its own comment. Unlike room
	// rotation, connector rotation has no deferred/mid-gesture-allowed check (see
	// AInteriorGenerator::RotateConnectorGroup's own comment), so there's no snapshot/commit dance to
	// set up here - BeginRotationGestureIfNeeded/EndRotationGestureIfFinished simply don't do
	// anything when a connector (not a room) is selected.
	if (!SelectedConnectorGroupId.IsEmpty() && InteriorGen)
	{
		if (InteriorGen->RotateConnectorGroup(SelectedConnectorGroupId, DeltaDegrees))
		{
			bHasUnsavedChanges = true;
		}
		return;
	}

	if (SelectedRoomId.IsEmpty() || !InteriorGen)
	{
		return;
	}

	if (InteriorGen->RotateRoom(SelectedRoomId, DeltaDegrees))
	{
		bHasUnsavedChanges = true;
	}
}

void ANavPlayerController::StartRotateCCW()
{
	BeginRotationGestureIfNeeded();
	bRotatingCCW = true;
}

void ANavPlayerController::StopRotateCCW()
{
	bRotatingCCW = false;
	EndRotationGestureIfFinished();
}

void ANavPlayerController::StartRotateCW()
{
	BeginRotationGestureIfNeeded();
	bRotatingCW = true;
}

void ANavPlayerController::StopRotateCW()
{
	bRotatingCW = false;
	EndRotationGestureIfFinished();
}

void ANavPlayerController::BeginRotationGestureIfNeeded()
{
	// Only snapshot at the true start of a gesture (neither key held yet) - if the other rotation
	// key is already down, we're still mid-gesture and the original snapshot must be kept.
	if (bRotatingCCW || bRotatingCW || !InteriorGen)
	{
		return;
	}

	// Connector rotation is fully validated every tick (see RotateConnectorGroup), nothing deferred
	// to check on release, so there's no snapshot to take - and SelectedRoomId is empty whenever a
	// connector is selected anyway (the two are mutually exclusive, see SelectConnector), so the
	// SelectedRoomId.IsEmpty() check below would already no-op here regardless. This explicit branch
	// just makes that reasoning obvious rather than relying on it silently falling through.
	if (!SelectedConnectorGroupId.IsEmpty())
	{
		return;
	}

	if (SelectedRoomId.IsEmpty())
	{
		return;
	}

	for (const AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
	{
		if (Room && Room->RoomId == SelectedRoomId)
		{
			RotationStartFootprint = Room->GetFootprint();
			break;
		}
	}
}

void ANavPlayerController::EndRotationGestureIfFinished()
{
	// Only commit once both keys are up - releasing one while the other is still held just means
	// switching direction mid-gesture, not the end of it.
	if (bRotatingCCW || bRotatingCW || SelectedRoomId.IsEmpty() || !InteriorGen || RotationStartFootprint.Num() == 0)
	{
		return;
	}

	InteriorGen->CommitRoomRotation(SelectedRoomId, RotationStartFootprint);
	RotationStartFootprint.Reset();
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

FString ANavPlayerController::GetAudioFolderPath()
{
	// FPaths::ProjectDir() is relative (something like "../../../InNavi/"), not absolute - fine for
	// FFileHelper::LoadFileToString elsewhere in this file (which resolves relative to the working
	// directory), but PlayAudioFile builds a file:// URL from this, and a relative path there produces
	// an invalid URI (every media player plugin then rejects it as unsupported, not just a "file not
	// found" - confirmed via the Output Log, which showed the literal unresolved "../../../.." in the
	// URL it tried to open). ConvertRelativePathToFull makes this a real absolute path first.
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data") / TEXT("Audio"));
}

TArray<FString> ANavPlayerController::GetAvailableAudioFiles() const
{
	TArray<FString> FoundFiles;
	// Not "FolderPath" - that name collides with a class member somewhere in this class's own
	// inheritance chain (C4458 hides-class-member, a compile error in this project).
	const FString AudioFolder = GetAudioFolderPath();
	IFileManager::Get().FindFiles(FoundFiles, *(AudioFolder / TEXT("*.mp3")), true, false);
	IFileManager::Get().FindFiles(FoundFiles, *(AudioFolder / TEXT("*.wav")), true, false);
	FoundFiles.Sort();
	return FoundFiles;
}

bool ANavPlayerController::PlayAudioFile(const FString& FileName)
{
	if (IsAudioFilePlaying(FileName))
	{
		return false;
	}

	const FString FullPath = GetAudioFolderPath() / FileName;
	if (!FPaths::FileExists(FullPath))
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Audio file '%s' not found."), *FileName));
		return false;
	}

	URuntimeAudioImporterLibrary* Importer = URuntimeAudioImporterLibrary::CreateRuntimeAudioImporter();
	if (!Importer)
	{
		OnActionFailed.Broadcast(TEXT("Could not create the audio importer."));
		return false;
	}
	PendingAudioImports.Add(Importer);

	// Added immediately (SoundWave/AudioComponent still null) so a second click on the same row while
	// it's still decoding is a no-op via IsAudioFilePlaying's own check above, instead of kicking off a
	// duplicate import. The lambda below fills in SoundWave/AudioComponent once decoding finishes.
	FActiveAudioPlayback& Playback = ActiveAudioPlaybacks.AddDefaulted_GetRef();
	Playback.FileName = FileName;

	Importer->OnResultNative.AddLambda([this, FileName](URuntimeAudioImporterLibrary* CompletedImporter, UImportedSoundWave* ImportedSoundWave, ERuntimeImportStatus Status)
	{
		PendingAudioImports.RemoveSingleSwap(CompletedImporter);

		FActiveAudioPlayback* Found = ActiveAudioPlaybacks.FindByPredicate([&FileName](const FActiveAudioPlayback& P) { return P.FileName == FileName; });
		if (!Found)
		{
			// Stopped (StopAllAudio clears ActiveAudioPlaybacks) before the async decode finished -
			// nothing left to attach the result to.
			return;
		}

		if (Status != ERuntimeImportStatus::SuccessfulImport || !ImportedSoundWave)
		{
			OnActionFailed.Broadcast(FString::Printf(TEXT("Could not decode audio file '%s'."), *FileName));
			ActiveAudioPlaybacks.RemoveAll([&FileName](const FActiveAudioPlayback& P) { return P.FileName == FileName; });
			return;
		}

		if (!AudioActor)
		{
			AudioActor = GetWorld()->SpawnActor<AActor>();
		}
		if (!AudioActor)
		{
			OnActionFailed.Broadcast(TEXT("Could not create the audio player actor."));
			ActiveAudioPlaybacks.RemoveAll([&FileName](const FActiveAudioPlayback& P) { return P.FileName == FileName; });
			return;
		}

		UAudioComponent* AudioComponent = NewObject<UAudioComponent>(AudioActor);
		AudioComponent->bAutoActivate = false;
		// AudioActor sits at the world origin (see its own comment) - miles from wherever the camera
		// actually is on this campus-scale map. A spatialized 3D sound would attenuate with distance
		// from the listener and be effectively silent from way out here; this is a UI/global sound
		// (BGM, button click, ...), not something tied to a position in the world, so play it at a flat
		// volume regardless of where AudioActor happens to be.
		AudioComponent->bAllowSpatialization = false;
		AudioComponent->SetSound(ImportedSoundWave);
		AudioComponent->RegisterComponent();
		AudioComponent->Play();

		// TEMPORARY diagnostic logging (no sound was audible on the last test pass despite a clean
		// decode) - prints immediately, then again after a short delay, so the Output Log shows
		// whether IsPlaying() ever actually goes true, and what the component's own volume/sound
		// state looks like. Remove once the root cause is confirmed fixed.
		UE_LOG(LogTemp, Warning, TEXT("[AudioDebug] '%s' right after Play(): AudioComponent->IsPlaying()=%s, VolumeMultiplier=%.2f, Sound=%s, bIsActive=%s, SoundWave->IsPlaying()=%s, Duration=%.2f"),
			*FileName,
			AudioComponent->IsPlaying() ? TEXT("true") : TEXT("false"),
			AudioComponent->VolumeMultiplier,
			AudioComponent->Sound ? *AudioComponent->Sound->GetName() : TEXT("null"),
			AudioComponent->IsActive() ? TEXT("true") : TEXT("false"),
			ImportedSoundWave->IsPlaying(this) ? TEXT("true") : TEXT("false"),
			ImportedSoundWave->GetDuration());
		FTimerHandle DebugTimerHandle;
		TWeakObjectPtr<UAudioComponent> WeakAudioComponent = AudioComponent;
		TWeakObjectPtr<UImportedSoundWave> WeakSoundWave = ImportedSoundWave;
		GetWorldTimerManager().SetTimer(DebugTimerHandle, [WeakAudioComponent, WeakSoundWave, FileName, this]()
		{
			if (WeakAudioComponent.IsValid() && WeakSoundWave.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("[AudioDebug] '%s' 1s later: AudioComponent->IsPlaying()=%s, IsActive()=%s, SoundWave->IsPlaying()=%s"),
					*FileName,
					WeakAudioComponent->IsPlaying() ? TEXT("true") : TEXT("false"),
					WeakAudioComponent->IsActive() ? TEXT("true") : TEXT("false"),
					WeakSoundWave->IsPlaying(this) ? TEXT("true") : TEXT("false"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[AudioDebug] '%s' 1s later: component or sound wave no longer valid"), *FileName);
			}
		}, 1.0f, false);

		Found->SoundWave = ImportedSoundWave;
		Found->AudioComponent = AudioComponent;
	});

	// Auto: determine mp3/wav/etc. from the file extension - GetAvailableAudioFiles only ever lists
	// .mp3/.wav, so there's nothing ambiguous to disambiguate here.
	Importer->ImportAudioFromFile(FullPath, ERuntimeAudioFormat::Auto);
	return true;
}

void ANavPlayerController::StopAllAudio()
{
	for (FActiveAudioPlayback& Playback : ActiveAudioPlaybacks)
	{
		if (Playback.AudioComponent)
		{
			Playback.AudioComponent->Stop();
			Playback.AudioComponent->DestroyComponent();
		}
	}
	ActiveAudioPlaybacks.Reset();

	// Anything still decoding has nothing left to attach to once it finishes (see PlayAudioFile's own
	// OnResultNative lambda, which checks ActiveAudioPlaybacks and no-ops if its entry is gone) - clear
	// this too so those importers aren't kept alive for no reason.
	PendingAudioImports.Reset();
}

bool ANavPlayerController::IsAudioFilePlaying(const FString& FileName) const
{
	for (const FActiveAudioPlayback& Playback : ActiveAudioPlaybacks)
	{
		if (Playback.FileName == FileName)
		{
			return true;
		}
	}
	return false;
}

void ANavPlayerController::PlayButtonClickSound()
{
	if (!bButtonClickSoundEnabled)
	{
		return;
	}

	PlayShortSoundEffect(TEXT("click_button.mp3"));
}

void ANavPlayerController::PlayBuildingSelectSound()
{
	PlayShortSoundEffect(TEXT("select_ui.mp3"));
}

void ANavPlayerController::PlayShortSoundEffect(const TCHAR* FileName)
{
	// Deliberately NOT cached/reused across calls (an earlier version of PlayButtonClickSound was, and
	// it crashed the editor: UImportedSoundWave tracks its own playback read-position internally, so
	// two overlapping plays sharing one instance - e.g. rapid clicking - corrupted that state and hit
	// an ensure failure in SoundWaveProcedural, "generated more bytes than expected"). Each call gets
	// its own freshly-decoded, independent instance instead, same as PlayAudioFile - these files are
	// tiny (a fraction of a second of audio), so decoding fresh every time is not noticeable.
	const FString FullPath = GetAudioFolderPath() / FileName;
	if (!FPaths::FileExists(FullPath))
	{
		return;
	}

	URuntimeAudioImporterLibrary* Importer = URuntimeAudioImporterLibrary::CreateRuntimeAudioImporter();
	if (!Importer)
	{
		return;
	}
	PendingAudioImports.Add(Importer);

	Importer->OnResultNative.AddLambda([this](URuntimeAudioImporterLibrary* CompletedImporter, UImportedSoundWave* ImportedSoundWave, ERuntimeImportStatus Status)
	{
		PendingAudioImports.RemoveSingleSwap(CompletedImporter);
		if (Status == ERuntimeImportStatus::SuccessfulImport && ImportedSoundWave)
		{
			UGameplayStatics::PlaySound2D(this, ImportedSoundWave);
		}
	});
	Importer->ImportAudioFromFile(FullPath, ERuntimeAudioFormat::Auto);
}

void ANavPlayerController::PruneFinishedAudioPlaybacks()
{
	for (int32 i = ActiveAudioPlaybacks.Num() - 1; i >= 0; i--)
	{
		FActiveAudioPlayback& Playback = ActiveAudioPlaybacks[i];
		if (!Playback.AudioComponent || Playback.AudioComponent->IsPlaying())
		{
			// Null AudioComponent means still decoding (see FActiveAudioPlayback's own comment) - not
			// finished, just not started yet.
			continue;
		}

		Playback.AudioComponent->DestroyComponent();
		ActiveAudioPlaybacks.RemoveAt(i);
	}
}
