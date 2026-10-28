#include "NavPlayerController.h"

#include "BuildingGenerator.h"
#include "InteriorBuildingPreviewPivot.h"
#include "InteriorGenerator.h"
#include "NavigationPanelWidget.h"
#include "PolyBuilding.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Framework/Application/SlateApplication.h"

bool ANavPlayerController::CanInspectBuilding() const
{
	return bIsIn3DPreviewMode && Generator && InteriorGen && GetPawn() &&
		IsValid(Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId())) && !InteriorGen->GetFloorLevels().IsEmpty();
}

int32 ANavPlayerController::GetInspectionLowestFloor() const
{
	const TArray<int32> Levels = InteriorGen ? InteriorGen->GetFloorLevels() : TArray<int32>();
	return Levels.IsEmpty() ? 0 : Levels[0];
}

void ANavPlayerController::SetInteriorInspectionEnabled(bool bEnable)
{
	if (bEnable == bInteriorInspectionEnabled) return;
	if (!bEnable)
	{
		bInteriorInspectionEnabled = false;
		InspectionDrag = EInspectionDrag::None;
		// Restore floor visibility before applying the exact actor snapshots.
		if (InteriorGen && InteriorGen->GetLoadedBuildingId() == InspectionBuildingId)
		{
			InteriorGen->SetActiveFloor(InspectionSavedFloor);
		}
		if (IsValid(InspectionPivot))
		{
			InspectionPivot->Restore();
			InspectionPivot->Destroy();
		}
		InspectionPivot = nullptr;
		if (APawn* PreviewPawn = InspectionPawn.Get()) PreviewPawn->SetActorTransform(InspectionSavedCamera, false, nullptr, ETeleportType::TeleportPhysics);
		InspectionPawn.Reset();
		SetControlRotation(InspectionSavedControlRotation);
		SetIgnoreMoveInput(false);
		SetIgnoreLookInput(false);
		SetIn3DEditMode(bInspectionSavedEdit);
		RefreshSelectionHighlight();
		RefreshRoute3DVisualization();
		OnActiveFloorChanged.Broadcast();
		OnInspectionModeChanged.Broadcast(false);
		return;
	}
	if (!CanInspectBuilding()) return;
	OnLeftMouseUp3DEdit();
	StopRotateCCW();
	StopRotateCW();
	ExitDrawRoomMode();
	ExitPrefabRoomMode();
	ExitAddDoorMode();
	ExitConnectorMode();
	if (NavigationPanel) NavigationPanel->CancelSelection();
	InspectionSavedFloor = InteriorGen->GetActiveFloor();
	InspectionBuildingId = InteriorGen->GetLoadedBuildingId();
	InspectionSavedCamera = GetPawn()->GetActorTransform();
	InspectionSavedControlRotation = GetControlRotation();
	InspectionPawn = GetPawn();
	bInspectionSavedEdit = bIsIn3DEditMode;
	InspectionPivot = GetWorld()->SpawnActor<AInteriorBuildingPreviewPivot>();
	if (!InspectionPivot || !InspectionPivot->Initialize(Generator->FindBuildingById(InspectionBuildingId), InteriorGen, Generator))
	{
		if (InspectionPivot) InspectionPivot->Destroy();
		InspectionPivot = nullptr;
		return;
	}
	SetIn3DEditMode(false);
	bInteriorInspectionEnabled = true;
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	if (UPawnMovementComponent* Movement = GetPawn()->GetMovementComponent()) Movement->StopMovementImmediately();
	// The tip of the model is about the axis across the view, so it is always up and down on the screen.
	const FVector Backward = -FRotator(0.f, InspectionCameraYaw, 0.f).Vector();
	InspectionPivot->SetTiltRange(FVector::CrossProduct(FVector::UpVector, Backward), InspectionTiltMin, InspectionTiltMax);
	InspectionCameraDistance = 0.f;
	FrameInspectionCamera();
	RefreshRoute3DVisualization();
	OnInspectionModeChanged.Broadcast(true);
}

void ANavPlayerController::FrameInspectionCamera(float ZoomFactor)
{
	if (!IsValid(InspectionPivot) || !GetPawn()) return;
	if (InspectionCameraDistance <= 0.f)
	{
		int32 Width = 0, Height = 0;
		GetViewportSize(Width, Height);
		const double Aspect = Height > 0 ? double(Width) / Height : 16.0 / 9.0;
		const double HalfFov = FMath::DegreesToRadians(PlayerCameraManager ? PlayerCameraManager->GetFOVAngle() * 0.5 : 45.0);
		const double HalfAngle = FMath::Min(HalfFov, FMath::Atan(FMath::Tan(HalfFov) / FMath::Max(Aspect, 0.1)));
		// A sphere about the first pivot contains the whole model at every allowed tilt.
		InspectionDefaultDistance = InspectionPivot->GetSweepRadius() * 1.3 / FMath::Sin(FMath::Max(HalfAngle, 0.1));
		InspectionCameraDistance = InspectionDefaultDistance;
	}
	InspectionCameraDistance = FMath::Clamp(InspectionCameraDistance * ZoomFactor,
		InspectionPivot->GetSweepRadius() * 1.05f, InspectionDefaultDistance * 5.f);
	const FRotator Rotation(InspectionCameraPitch, InspectionCameraYaw, 0.f);
	// The camera looks at where the model started, not at wherever it has been panned to.
	GetPawn()->SetActorLocationAndRotation(InspectionPivot->GetDefaultPivot() - Rotation.Vector() * InspectionCameraDistance, Rotation);
	SetControlRotation(Rotation);
}

void ANavPlayerController::RotateInspectionBy(float YawDegrees, float TiltDegrees)
{
	if (!bInteriorInspectionEnabled || !IsValid(InspectionPivot)) return;
	InspectionPivot->RotateBy(YawDegrees, TiltDegrees);
	RefreshRoute3DVisualization();
}

void ANavPlayerController::PanInspectionBy(const FVector2D& PixelDelta)
{
	if (!bInteriorInspectionEnabled || !IsValid(InspectionPivot)) return;
	// What a pixel is in the world, at the distance of the model: the model follows the cursor.
	int32 Width = 0, Height = 0;
	GetViewportSize(Width, Height);
	const double HalfFov = FMath::DegreesToRadians(PlayerCameraManager ? PlayerCameraManager->GetFOVAngle() * 0.5 : 45.0);
	const double PerPixel = Width > 0 ? 2.0 * InspectionCameraDistance * FMath::Tan(HalfFov) / Width : 1.0;
	const FRotationMatrix View(FRotator(InspectionCameraPitch, InspectionCameraYaw, 0.f));
	const FVector Move = (View.GetScaledAxis(EAxis::Y) * PixelDelta.X - View.GetScaledAxis(EAxis::Z) * PixelDelta.Y) * PerPixel;
	InspectionPivot->TranslateBy(Move, InspectionPivot->GetSweepRadius() * 2.5f);
	RefreshRoute3DVisualization();
}

void ANavPlayerController::BeginInspectionDrag(EInspectionDrag Mode)
{
	if (IsPointerOverUI() || (NavigationPanel && NavigationPanel->IsPointerOverPanel())) return;
	float X = 0.f, Y = 0.f;
	if (GetMousePosition(X, Y))
	{
		InspectionLastPointer = FVector2D(X, Y);
		InspectionDrag = Mode;
	}
}

void ANavPlayerController::OnRightMouseDownInspection()
{
	if (bInteriorInspectionEnabled) BeginInspectionDrag(EInspectionDrag::Rotate);
}

void ANavPlayerController::OnRightMouseUpInspection()
{
	if (InspectionDrag == EInspectionDrag::Rotate) InspectionDrag = EInspectionDrag::None;
}

void ANavPlayerController::TickInteriorInspection()
{
	if (!IsValid(InspectionPivot) || !InspectionPivot->HasBuilding() || !InteriorGen ||
		InteriorGen->GetLoadedBuildingId() != InspectionBuildingId || InspectionPawn.Get() != GetPawn())
	{
		SetInteriorInspectionEnabled(false);
		return;
	}
	InspectionPivot->ApplyIsolation();
	if (InspectionDrag == EInspectionDrag::None) return;
	float X = 0.f, Y = 0.f;
	if (!IsInputKeyDown(InspectionDrag == EInspectionDrag::Pan ? EKeys::LeftMouseButton : EKeys::RightMouseButton) || !GetMousePosition(X, Y) ||
		(FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsActive()))
	{
		InspectionDrag = EInspectionDrag::None;
		return;
	}
	const FVector2D Pointer(X, Y);
	const FVector2D Delta = Pointer - InspectionLastPointer;
	InspectionLastPointer = Pointer;
	if (IsPointerOverUI() || Delta.IsNearlyZero()) return;
	if (InspectionDrag == EInspectionDrag::Pan)
	{
		PanInspectionBy(Delta);
		return;
	}
	// Grab: the model follows the mouse (drag right and its near side goes right; drag up and its top tips away). Orbit is the other way
	// round, as if the camera were moved.
	const float Direction = InspectionRotateStyle == EInspectionRotateStyle::Grab ? -1.f : 1.f;
	RotateInspectionBy(Direction * Delta.X * 0.35f, -Direction * Delta.Y * 0.35f);
}

void ANavPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetInteriorInspectionEnabled(false);
	Super::EndPlay(EndPlayReason);
}
