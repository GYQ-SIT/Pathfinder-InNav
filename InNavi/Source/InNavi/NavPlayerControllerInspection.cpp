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
		IsValid(Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId())) && InteriorGen->GetFloorLevels().Num() > 1;
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
		bInspectionDragging = false;
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
	const TArray<int32> Levels = InteriorGen->GetFloorLevels();
	if (InspectionSavedFloor == Levels[0]) SetActiveFloor(Levels[1]);
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
		// A sphere about the bottom pivot contains the whole model at every allowed tilt.
		InspectionDefaultDistance = InspectionPivot->GetSweepRadius() * 1.3 / FMath::Sin(FMath::Max(HalfAngle, 0.1));
		InspectionCameraDistance = InspectionDefaultDistance;
	}
	InspectionCameraDistance = FMath::Clamp(InspectionCameraDistance * ZoomFactor,
		InspectionPivot->GetSweepRadius() * 1.05f, InspectionDefaultDistance * 5.f);
	const FRotator Rotation(-25.f, -45.f, 0.f);
	GetPawn()->SetActorLocationAndRotation(InspectionPivot->GetActorLocation() - Rotation.Vector() * InspectionCameraDistance, Rotation);
	SetControlRotation(Rotation);
}

void ANavPlayerController::RotateInspectionBy(float YawDegrees, float TiltDegrees)
{
	if (!bInteriorInspectionEnabled || !IsValid(InspectionPivot)) return;
	InspectionPivot->RotateBy(YawDegrees, TiltDegrees);
	RefreshRoute3DVisualization();
}

void ANavPlayerController::BeginInspectionDrag()
{
	if (IsPointerOverUI() || (NavigationPanel && NavigationPanel->IsPointerOverPanel())) return;
	float X = 0.f, Y = 0.f;
	if (GetMousePosition(X, Y))
	{
		InspectionLastPointer = FVector2D(X, Y);
		bInspectionDragging = true;
	}
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
	if (!bInspectionDragging) return;
	float X = 0.f, Y = 0.f;
	if (!IsInputKeyDown(EKeys::LeftMouseButton) || !GetMousePosition(X, Y) ||
		(FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsActive()))
	{
		bInspectionDragging = false;
		return;
	}
	const FVector2D Pointer(X, Y);
	const FVector2D Delta = Pointer - InspectionLastPointer;
	InspectionLastPointer = Pointer;
	if (!IsPointerOverUI() && !Delta.IsNearlyZero()) RotateInspectionBy(Delta.X * 0.35f, -Delta.Y * 0.35f);
}

void ANavPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetInteriorInspectionEnabled(false);
	Super::EndPlay(EndPlayReason);
}
