#include "NavPlayerController.h"

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "InteriorConnectorMarker.h"
#include "NavigationPanelWidget.h"
#include "NavigationRouteVisualization.h"
#include "PolyBuilding.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"

bool ANavPlayerController::ResolveFullRoutePoint(FNavigationRoutePoint& Point) const
{
	const FString Id = Point.BuildingId.IsEmpty() && InteriorGen ? InteriorGen->GetLoadedBuildingId() : Point.BuildingId;
	Point.BuildingId = Id;
	if (Point.bHasWorldPosition) return !Point.WorldPosition.ContainsNaN();
	const APolyBuilding* Building = Generator ? Generator->FindBuildingById(Id) : nullptr;
	if (!Building) return false;
	const FVector2D XY = (Point.Position + Building->BuildingOffset) * Generator->GetScale();
	Point.WorldPosition = FVector(XY.X, XY.Y, Point.Floor == 0 ? 0.f : Building->GetLevelBaseZ(Point.Floor));
	Point.bHasWorldPosition = true;
	Point.BuildingId = Id;
	return !Point.WorldPosition.ContainsNaN();
}

bool ANavPlayerController::CanShowFullRouteView() const
{
	if (!Generator || !NavigationPanel || !GetPawn() || NavigationPanel->IsFindingRoute()
		|| NavigationPanel->GetRoutePoints().IsEmpty()) return false;
	for (FNavigationRoutePoint Point : NavigationPanel->GetRoutePoints())
	{
		if (!ResolveFullRoutePoint(Point)) return false;
	}
	return true;
}

uint32 ANavPlayerController::FullRouteSignature() const
{
	uint32 Signature = 0;
	if (NavigationPanel) for (const FNavigationRoutePoint& Point : NavigationPanel->GetRoutePoints())
	{
		Signature = HashCombine(Signature, GetTypeHash(Point.BuildingId));
		Signature = HashCombine(Signature, GetTypeHash(Point.Floor));
		Signature = HashCombine(Signature, GetTypeHash(Point.Position));
		Signature = HashCombine(Signature, GetTypeHash(Point.WorldPosition));
	}
	return Signature;
}

FTransform ANavPlayerController::FullRouteCameraTransform(const FBox& Bounds, float HorizontalFov, float AspectRatio,
	const FBox* ExteriorBounds, float* OutFov)
{
	AspectRatio = FMath::Max(AspectRatio, 0.1f);
	const float HalfHorizontal = FMath::DegreesToRadians(FMath::Clamp(HorizontalFov, 10.f, 150.f) * 0.5f);
	const float HalfVertical = FMath::Atan(FMath::Tan(HalfHorizontal) / AspectRatio);
	// Reserve room for dock panels, labels and arrows around the bounding sphere.
	const float Radius = FMath::Max(200.f, Bounds.GetExtent().Size() * 1.45f);
	float Distance = Radius / FMath::Sin(FMath::Min(HalfHorizontal, HalfVertical));
	if (ExteriorBounds && ExteriorBounds->IsValid)
	{
		// Leave even a tall or wide building, without making a short route a tiny dot: use optical zoom to keep it readable.
		Distance = FMath::Max(Distance, float(ExteriorBounds->GetExtent().Size()
			+ FVector::Distance(ExteriorBounds->GetCenter(), Bounds.GetCenter()) + 200.f));
	}
	const float HalfAngle = FMath::Asin(FMath::Clamp(Radius / Distance, 0.f, 0.99f));
	if (OutFov) *OutFov = FMath::RadiansToDegrees(2.f * (AspectRatio > 1.f ? FMath::Atan(FMath::Tan(HalfAngle) * AspectRatio) : HalfAngle));
	const FRotator Rotation(-30.f, -45.f, 0.f);
	return FTransform(Rotation, Bounds.GetCenter() - Rotation.Vector() * Distance);
}

void ANavPlayerController::FrameFullRouteView()
{
	if (!bFullRouteViewEnabled || !GetPawn() || !NavigationPanel) return;
	FBox Bounds(ForceInit);
	for (FNavigationRoutePoint Point : NavigationPanel->GetRoutePoints())
	{
		if (!ResolveFullRoutePoint(Point)) continue;
		Bounds += Point.WorldPosition;
	}
	if (!Bounds.IsValid) return;
	int32 Width = 0, Height = 0;
	GetViewportSize(Width, Height);
	FullRouteViewportSize = FIntPoint(Width, Height);
	const float Aspect = Width > 0 && Height > 0 ? float(Width) / Height : 16.f / 9.f;
	float Fov = FullRouteSavedFov;
	const FTransform Camera = FullRouteCameraTransform(Bounds, FullRouteSavedFov, Aspect, nullptr, &Fov);
	if (PlayerCameraManager) PlayerCameraManager->SetFOV(Fov);
	if (UPawnMovementComponent* Movement = GetPawn()->GetMovementComponent()) Movement->StopMovementImmediately();
	GetPawn()->SetActorTransform(Camera);
	SetControlRotation(Camera.Rotator());
}

void ANavPlayerController::SetFullRouteViewEnabled(bool bEnable)
{
	if (bEnable == bFullRouteViewEnabled) return;
	if (!bEnable)
	{
		bFullRouteViewEnabled = false;
		if (PlayerCameraManager)
		{
			if (FullRouteSavedLockedFov > 0.f) PlayerCameraManager->SetFOV(FullRouteSavedLockedFov);
			else PlayerCameraManager->UnlockFOV();
		}
		SetIn3DPreviewMode(bFullRouteSavedPreview);
		SetIn3DEditMode(bFullRouteSavedEdit);
		for (const FFullRouteActorState& State : FullRouteSavedActors)
		{
			if (AActor* Actor = State.Actor.Get())
			{
				Actor->SetActorHiddenInGame(State.bHidden);
				Actor->SetActorEnableCollision(State.bCollision);
				if (AInteriorRoom* Room = Cast<AInteriorRoom>(Actor)) Room->SetOverviewOpacity(-1.f);
				if (APolyBuilding* Building = Cast<APolyBuilding>(Actor))
				{
					Building->SetVisibleInteriorLevel(State.InteriorLevel);
					Building->SetBuildingOpacity(State.Opacity);
				}
			}
		}
		FullRouteSavedActors.Empty();
		if (GetPawn() && GetPawn() == FullRouteSavedPawn.Get())
		{
			if (UPawnMovementComponent* Movement = GetPawn()->GetMovementComponent()) Movement->StopMovementImmediately();
			GetPawn()->SetActorTransform(FullRouteSavedCamera);
			SetControlRotation(FullRouteSavedControl);
		}
		FullRouteSavedPawn.Reset();
		RefreshRoute3DVisualization();
		if (NavigationPanel) NavigationPanel->OnStateChanged.Broadcast();
		return;
	}
	if (!CanShowFullRouteView()) return;
	SetInteriorInspectionEnabled(false);
	FinishRotationGesture();
	ExitAllMouseTools();
	CancelNavigationPicking();
	OnLeftMouseUp3DEdit();
	FullRouteSavedPawn = GetPawn();
	FullRouteSavedCamera = GetPawn()->GetActorTransform();
	FullRouteSavedControl = GetControlRotation();
	FullRouteSavedFov = PlayerCameraManager ? PlayerCameraManager->GetFOVAngle() : 90.f;
	FullRouteSavedLockedFov = PlayerCameraManager ? PlayerCameraManager->GetLockedFOV() : 0.f;
	bFullRouteSavedPreview = bIsIn3DPreviewMode;
	bFullRouteSavedEdit = bIsIn3DEditMode;
	bFullRouteSavedOutdoor = NavigationPanel->IsOutdoorMode();
	FullRouteSavedSignature = FullRouteSignature();
	TSet<FString> RouteBuildings;
	for (FNavigationRoutePoint Point : NavigationPanel->GetRoutePoints())
	{
		ResolveFullRoutePoint(Point);
		RouteBuildings.Add(Point.BuildingId);
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		APolyBuilding* Building = Cast<APolyBuilding>(*It);
		if (!Building && !It->IsA<AInteriorRoom>() && !It->IsA<AInteriorConnectorMarker>()) continue;
		FFullRouteActorState State;
		State.Actor = *It;
		State.bHidden = It->IsHidden();
		State.bCollision = It->GetActorEnableCollision();
		if (Building)
		{
			State.InteriorLevel = Building->GetVisibleInteriorLevel();
			State.Opacity = Building->GetBuildingOpacity() < 0.f ? 1.f : Building->GetBuildingOpacity();
		}
		FullRouteSavedActors.Add(State);
	}
	// The interior screen uses this existing signal to replace its 2D plan with the world view.
	if (!NavigationPanel->IsOutdoorMode()) SetIn3DPreviewMode(true);
	SetIn3DEditMode(false);
	bFullRouteViewEnabled = true;
	for (const FFullRouteActorState& State : FullRouteSavedActors)
	{
		if (AActor* Actor = State.Actor.Get())
		{
			APolyBuilding* Building = Cast<APolyBuilding>(Actor);
			const bool bRouteBuilding = Building && RouteBuildings.Contains(Building->BuildingId);
			// The rooms and the lifts of the building that is open (when the route is in it) stay, so the route is seen against them; the other buildings
			// are faded rather than hidden, to keep where the route is.
			const bool bOpenRouteContent = !Building && InteriorGen && RouteBuildings.Contains(InteriorGen->GetLoadedBuildingId());
			Actor->SetActorHiddenInGame(!(Building || bOpenRouteContent));
			Actor->SetActorEnableCollision(false);
			if (AInteriorRoom* Room = Cast<AInteriorRoom>(Actor)) Room->SetOverviewOpacity(0.3f);
			if (Building)
			{
				Building->SetVisibleInteriorLevel(0);
				Building->SetBuildingOpacity(bRouteBuilding ? 0.16f : 0.05f);
			}
		}
	}
	NavigationPanel->SetRouteGuidanceVisible(true);
	FrameFullRouteView();
	RefreshRoute3DVisualization();
	NavigationPanel->OnStateChanged.Broadcast();
}

void ANavPlayerController::ShowFullRouteVisualization()
{
	TArray<FNavigationRoutePoint> Points = NavigationPanel->GetRoutePoints();
	for (FNavigationRoutePoint& Point : Points) ResolveFullRoutePoint(Point);
	TArray<FNavigationStop> Stops = NavigationPanel->GetStops();
	for (FNavigationStop& Stop : Stops) ResolveFullRoutePoint(Stop.Point);
	// The route's exact first/last points take precedence over room-centre endpoint selections.
	FNavigation3DView View = UNavigationRouteVisualization::BuildView(Points, &Points[0], &Points.Last(),
		FVector2D::ZeroVector, 1.f, 1.f, 0, true, FString(), &Stops, &NavigationPanel->GetRouteLegs(), nullptr, true);
	// No texts in the overview: from this far they pile up on each other and hide the route (the steps are in the panel).
	View.Labels.Reset();
	RouteVisualization->ShowView(View);

	// The floors the route goes through, each outlined at its level, so it is seen which floor each part of the route is on.
	TSet<TPair<FString, int32>> Floors;
	for (const FNavigationRoutePoint& Point : Points)
	{
		if (Point.Floor != 0) Floors.Add(TPair<FString, int32>(Point.BuildingId, Point.Floor));
	}
	const FLinearColor OutlineColor(0.45f, 0.72f, 1.f, 1.f);
	for (const TPair<FString, int32>& Floor : Floors)
	{
		const APolyBuilding* Building = Generator ? Generator->FindBuildingById(Floor.Key) : nullptr;
		const TArray<FVector2D>& Footprint = Building ? Building->GetFootprint() : TArray<FVector2D>();
		if (!Building || Footprint.Num() < 3) continue;
		const float Z = Building->GetLevelBaseZ(Floor.Value) + 6.f;
		auto World = [&](int32 Index) { return FVector((Footprint[Index % Footprint.Num()] + Building->BuildingOffset) * Generator->GetScale(), Z); };
		for (int32 Index = 0; Index < Footprint.Num(); ++Index)
		{
			RouteVisualization->DrawLine(World(Index), World(Index + 1), OutlineColor, SDPG_Foreground, 3.f, -1.f);
		}
	}
}
