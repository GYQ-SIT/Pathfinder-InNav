#include "NavPlayerController.h"

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "InteriorConnectorMarker.h"
#include "PolygonGeometryUtils.h"
#include "ProceduralMeshComponent.h"
#include "NavigationPanelWidget.h"
#include "NavigationRouteVisualization.h"
#include "PolyBuilding.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

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

namespace
{
	FString FullRouteFloorKey(const FString& BuildingId, int32 Floor)
	{
		return BuildingId + TEXT("|") + FString::FromInt(Floor);
	}

	// The floors of the overview are this many floor heights apart, whatever their real distance.
	constexpr float FullRouteFloorSpacingFactor = 2.5f;
}

void ANavPlayerController::BuildFullRouteFloorShifts()
{
	FullRouteFloorShifts.Reset();
	if (!NavigationPanel || !Generator) return;
	TMap<FString, TSet<int32>> Used;
	for (FNavigationRoutePoint Point : NavigationPanel->GetRoutePoints())
	{
		ResolveFullRoutePoint(Point);
		if (Point.Floor != 0) Used.FindOrAdd(Point.BuildingId).Add(Point.Floor);
	}
	for (const TPair<FString, TSet<int32>>& Entry : Used)
	{
		const APolyBuilding* Building = Generator->FindBuildingById(Entry.Key);
		if (!Building) continue;
		TArray<int32> Floors = Entry.Value.Array();
		Floors.Sort([&](int32 A, int32 B) { return Building->GetLevelBaseZ(A) < Building->GetLevelBaseZ(B); });
		const float FloorHeight = FMath::Max(Building->GetLevelBaseZ(2) - Building->GetLevelBaseZ(1), 100.f);
		const float Spacing = FloorHeight * FullRouteFloorSpacingFactor;
		// The lowest floor stays where it is, the others are laid out above it, evenly.
		const float FirstBase = Building->GetLevelBaseZ(Floors[0]);
		for (int32 Rank = 0; Rank < Floors.Num(); ++Rank)
		{
			FullRouteFloorShifts.Add(FullRouteFloorKey(Entry.Key, Floors[Rank]), FirstBase + Rank * Spacing - Building->GetLevelBaseZ(Floors[Rank]));
		}
	}
}

float ANavPlayerController::FullRouteFloorShift(const FString& BuildingId, int32 Floor) const
{
	const float* Shift = FullRouteFloorShifts.Find(FullRouteFloorKey(BuildingId, Floor));
	return Shift ? *Shift : 0.f;
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
	const FBox* ExteriorBounds, float* OutFov, float PitchDegrees)
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
	const FRotator Rotation(PitchDegrees, -45.f, 0.f);
	return FTransform(Rotation, Bounds.GetCenter() - Rotation.Vector() * Distance);
}

void ANavPlayerController::FrameFullRouteView()
{
	if (!bFullRouteViewEnabled || !GetPawn() || !NavigationPanel) return;
	FBox Bounds(ForceInit);
	for (FNavigationRoutePoint Point : NavigationPanel->GetRoutePoints())
	{
		if (!ResolveFullRoutePoint(Point)) continue;
		Bounds += Point.WorldPosition + FVector(0.f, 0.f, FullRouteFloorShift(Point.BuildingId, Point.Floor));
	}
	if (!Bounds.IsValid) return;
	int32 Width = 0, Height = 0;
	GetViewportSize(Width, Height);
	FullRouteViewportSize = FIntPoint(Width, Height);
	const float Aspect = Width > 0 && Height > 0 ? float(Width) / Height : 16.f / 9.f;
	float Fov = FullRouteSavedFov;
	// The buildings the route has nothing to do with are solid, so the camera must be neither inside one nor looking at the route through
	// one: it looks down more steeply until it does not.
	TArray<FBox> Solids;
	if (Generator)
	{
		for (const APolyBuilding* Building : Generator->GetAllBuildings())
		{
			if (!IsValid(Building) || Building->IsHidden()) continue;
			bool bOfRoute = false;
			for (const TPair<FString, float>& Shift : FullRouteFloorShifts) bOfRoute |= Shift.Key.StartsWith(Building->BuildingId + TEXT("|"));
			if (!bOfRoute) Solids.Add(Building->GetComponentsBoundingBox(true));
		}
	}
	FTransform Camera = FullRouteCameraTransform(Bounds, FullRouteSavedFov, Aspect, nullptr, &Fov);
	for (const float Pitch : { -45.f, -60.f, -75.f, -89.f })
	{
		const FVector Eye = Camera.GetLocation();
		const FVector Target = Bounds.GetCenter();
		const bool bBlocked = Solids.ContainsByPredicate([&](const FBox& Solid)
		{
			FVector HitLocation, HitNormal; float HitTime;
			return Solid.IsInside(Eye) || FMath::LineExtentBoxIntersection(Solid, Eye, Target, FVector::ZeroVector, HitLocation, HitNormal, HitTime);
		});
		if (!bBlocked) break;
		Camera = FullRouteCameraTransform(Bounds, FullRouteSavedFov, Aspect, nullptr, &Fov, Pitch);
	}
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
				if (!Cast<APolyBuilding>(Actor)) Actor->SetActorLocation(State.Location);
				if (APolyBuilding* Building = Cast<APolyBuilding>(Actor))
				{
					Building->SetVisibleInteriorLevel(State.InteriorLevel);
					Building->SetBuildingOpacity(State.Opacity);
				}
			}
		}
		FullRouteSavedActors.Empty();
		FullRouteFloorShifts.Reset();
		for (const TWeakObjectPtr<AActor>& Plate : FullRouteFloorPlates)
		{
			if (Plate.IsValid()) Plate->Destroy();
		}
		FullRouteFloorPlates.Reset();
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
	BuildFullRouteFloorShifts();
	const FString OpenBuilding = InteriorGen ? InteriorGen->GetLoadedBuildingId() : FString();
	// Only the building that is open has its rooms in the world. The rooms of the other buildings of the route are made the way the campus
	// x-ray makes them (from the stored data, read-only), for the floors the route uses; they are put back like the rest on leaving.
	TSet<FString> PulledBuildings;
	for (const TPair<FString, float>& Shift : FullRouteFloorShifts)
	{
		int32 Bar = INDEX_NONE;
		Shift.Key.FindLastChar(TEXT('|'), Bar);
		const FString BuildingId = Shift.Key.Left(Bar);
		const int32 Floor = FCString::Atoi(*Shift.Key.Mid(Bar + 1));
		PulledBuildings.Add(BuildingId);
		if (BuildingId != OpenBuilding && Generator)
		{
			EnsureCampusInteriorPreview(Generator->FindBuildingById(BuildingId), Floor);
		}
	}
	// The building each preview room belongs to; the previews of the open building are not wanted (its rooms are there).
	TMap<const AActor*, FString> PreviewRoomBuilding;
	TSet<const AActor*> UnwantedPreviewRooms;
	for (const TPair<FString, FCampusPreviewRooms>& Preview : CampusPreview)
	{
		for (const TWeakObjectPtr<AInteriorRoom>& Room : Preview.Value.Rooms)
		{
			if (!Room.IsValid()) continue;
			if (Preview.Key == OpenBuilding) UnwantedPreviewRooms.Add(Room.Get());
			else PreviewRoomBuilding.Add(Room.Get(), Preview.Key);
		}
	}
	// The floor each lift segment belongs to (the marker itself does not know).
	TMap<const AActor*, int32> MarkerFloor;
	if (InteriorGen)
	{
		for (const FInteriorNavNode& Node : InteriorGen->GetNavNodes())
		{
			if (Node.MarkerActor) MarkerFloor.Add(Node.MarkerActor, Node.Level);
		}
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		APolyBuilding* Building = Cast<APolyBuilding>(*It);
		if (!Building && !It->IsA<AInteriorRoom>() && !It->IsA<AInteriorConnectorMarker>()) continue;
		FFullRouteActorState State;
		State.Actor = *It;
		State.bHidden = It->IsHidden();
		State.bCollision = It->GetActorEnableCollision();
		State.Location = It->GetActorLocation();
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
			// The rooms and the lifts of the building that is open (when the route is in it) stay, but only those of the floors the route goes
			// through, and each floor is moved to where the overview lays it; the other buildings are faded rather than hidden, to keep where
			// the route is, and the building of the route itself is not drawn (its floors are not where they are, so its shell would not fit).
			bool bShown = Building != nullptr;
			if (!Building && !UnwantedPreviewRooms.Contains(Actor))
			{
				FString OwnerId = OpenBuilding;
				int32 Floor = INDEX_NONE;
				if (const AInteriorRoom* Room = Cast<AInteriorRoom>(Actor))
				{
					Floor = Room->FloorLevel;
					if (const FString* Previewed = PreviewRoomBuilding.Find(Actor)) OwnerId = *Previewed;
				}
				else if (const int32* Found = MarkerFloor.Find(Actor)) Floor = *Found;
				if (Floor != INDEX_NONE && FullRouteFloorShifts.Contains(FullRouteFloorKey(OwnerId, Floor)))
				{
					bShown = true;
					Actor->SetActorLocation(State.Location + FVector(0.f, 0.f, FullRouteFloorShift(OwnerId, Floor)));
				}
			}
			Actor->SetActorHiddenInGame(!bShown);
			Actor->SetActorEnableCollision(false);
			if (Building)
			{
				Building->SetVisibleInteriorLevel(0);
				// The shell of a building whose floors are pulled apart is not drawn (they are not where they are). Every other building stays
				// as it is, solid: the see-through look is only for the floors of the route.
				Building->SetBuildingOpacity(PulledBuildings.Contains(Building->BuildingId) ? 0.f : 1.f);
			}
		}
	}
	// A plate under each floor of the route, in the shape of its building.
	UMaterialInterface* PlateBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ImageAssets/Materials/M_BuildingTransparent.M_BuildingTransparent"));
	for (const TPair<FString, float>& Shift : FullRouteFloorShifts)
	{
		int32 Bar = INDEX_NONE;
		Shift.Key.FindLastChar(TEXT('|'), Bar);
		const APolyBuilding* PlateBuilding = Generator ? Generator->FindBuildingById(Shift.Key.Left(Bar)) : nullptr;
		const TArray<FVector2D>* Footprint = PlateBuilding ? &PlateBuilding->GetFootprint() : nullptr;
		if (!PlateBuilding || !Footprint || Footprint->Num() < 3 || !PlateBase || !GetWorld()) continue;
		const int32 Floor = FCString::Atoi(*Shift.Key.Mid(Bar + 1));
		const float Z = PlateBuilding->GetLevelBaseZ(Floor) + Shift.Value - 1.f;
		TArray<FVector2D> Outline;
		for (const FVector2D& Corner : *Footprint) Outline.Add((Corner + PlateBuilding->BuildingOffset) * Generator->GetScale());
		TArray<int32> Triangles;
		TriangulatePolygon(Outline, Triangles);
		if (Triangles.Num() < 3) continue;
		TArray<FVector> Vertices;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		for (const FVector2D& Point : Outline)
		{
			Vertices.Add(FVector(Point, Z));
			Normals.Add(FVector::UpVector);
			UVs.Add(FVector2D::ZeroVector);
		}
		// Both ways round: the plate is seen from above and, from the floor below, from underneath.
		TArray<int32> Both = Triangles;
		for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
		{
			Both.Add(Triangles[Index]); Both.Add(Triangles[Index + 2]); Both.Add(Triangles[Index + 1]);
		}
		AActor* Plate = GetWorld()->SpawnActor<AActor>();
		if (!Plate) continue;
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Plate);
		Plate->SetRootComponent(Mesh);
		Mesh->RegisterComponent();
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->CreateMeshSection_LinearColor(0, Vertices, Both, Normals, UVs, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(PlateBase, Plate);
		// The colour the floors of the rooms have unless they have a type, about 70% see-through.
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 0.55f, 0.58f));
		Material->SetScalarParameterValue(TEXT("Opacity"), 0.3f);
		Mesh->SetMaterial(0, Material);
		FullRouteFloorPlates.Add(Plate);
	}
	// The lifts, stairs and escalators of the other buildings of the route (the open one has its own): their markers are made from the stored
	// data for the floors the route uses, and go with the plates.
	for (const TPair<FString, float>& Shift : FullRouteFloorShifts)
	{
		int32 Bar = INDEX_NONE;
		Shift.Key.FindLastChar(TEXT('|'), Bar);
		const FString BuildingId = Shift.Key.Left(Bar);
		const int32 Floor = FCString::Atoi(*Shift.Key.Mid(Bar + 1));
		const APolyBuilding* Other = BuildingId != OpenBuilding && Generator ? Generator->FindBuildingById(BuildingId) : nullptr;
		const TSharedPtr<FJsonObject> InteriorData = Other ? Generator->GetInteriorDataForBuilding(BuildingId) : nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Levels = nullptr;
		if (!Other || !GetWorld() || !InteriorData.IsValid() || !InteriorData->TryGetArrayField(TEXT("floors"), Levels)) continue;
		for (const TSharedPtr<FJsonValue>& LevelValue : *Levels)
		{
			const TSharedPtr<FJsonObject> LevelObject = LevelValue.IsValid() ? LevelValue->AsObject() : nullptr;
			int32 Level = 1;
			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			if (!LevelObject.IsValid() || !LevelObject->TryGetNumberField(TEXT("level"), Level) || Level != Floor
				|| !LevelObject->TryGetArrayField(TEXT("navNodes"), Nodes)) continue;
			for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
			{
				const TSharedPtr<FJsonObject> Node = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
				FString TypeName;
				const TArray<TSharedPtr<FJsonValue>>* Position = nullptr;
				if (!Node.IsValid() || !Node->TryGetStringField(TEXT("type"), TypeName) || !Node->TryGetArrayField(TEXT("position"), Position) || Position->Num() < 2) continue;
				const EInteriorNavNodeType Type = TypeName == TEXT("elevator") ? EInteriorNavNodeType::Elevator
					: TypeName == TEXT("stairs") ? EInteriorNavNodeType::Stairs
					: TypeName == TEXT("escalator") ? EInteriorNavNodeType::Escalator : EInteriorNavNodeType::PathNode;
				if (Type == EInteriorNavNodeType::PathNode) continue;
				double Rotation = 0.0, SizeScale = 1.0;
				Node->TryGetNumberField(TEXT("rotationDegrees"), Rotation);
				Node->TryGetNumberField(TEXT("sizeScale"), SizeScale);
				AInteriorConnectorMarker* Marker = GetWorld()->SpawnActor<AInteriorConnectorMarker>(FVector::ZeroVector, FRotator::ZeroRotator);
				if (!Marker) continue;
				const FVector2D Centre = (FVector2D((*Position)[0]->AsNumber(), (*Position)[1]->AsNumber()) + Other->BuildingOffset) * Generator->GetScale();
				Marker->BuildMarker(Type, Centre, Other->GetLevelBaseZ(Floor) + Shift.Value, Other->GetFloorHeight(), float(Rotation), float(SizeScale));
				Marker->SetActorEnableCollision(false);
				FullRouteFloorPlates.Add(Marker);
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
	for (FNavigationRoutePoint& Point : Points)
	{
		ResolveFullRoutePoint(Point);
		Point.WorldPosition.Z += FullRouteFloorShift(Point.BuildingId, Point.Floor);
	}
	TArray<FNavigationStop> Stops = NavigationPanel->GetStops();
	for (FNavigationStop& Stop : Stops)
	{
		ResolveFullRoutePoint(Stop.Point);
		Stop.Point.WorldPosition.Z += FullRouteFloorShift(Stop.Point.BuildingId, Stop.Point.Floor);
	}
	// The route's exact first/last points take precedence over room-centre endpoint selections.
	FNavigation3DView View = UNavigationRouteVisualization::BuildView(Points, &Points[0], &Points.Last(),
		FVector2D::ZeroVector, 1.f, 1.f, 0, true, FString(), &Stops, &NavigationPanel->GetRouteLegs(), nullptr, true);
	// No texts in the overview: from this far they pile up on each other and hide the route (the steps are in the panel).
	View.Labels.Reset();
	// Seen from far the lines and the balls have to be bigger: they grow with the size of the route.
	FBox Bounds(ForceInit);
	for (const FNavigationRoutePoint& Point : Points) Bounds += Point.WorldPosition;
	RouteVisualization->SetDisplayScale(Bounds.IsValid ? FMath::Clamp(Bounds.GetExtent().Size() / 2500.f, 1.f, 8.f) : 1.f);
	RouteVisualization->ShowView(View);
	RouteVisualization->SetDisplayScale(1.f);
	const float LineScale = FMath::Clamp(Bounds.IsValid ? Bounds.GetExtent().Size() / 2500.f : 1.f, 1.f, 8.f);

	// The floors the route goes through, each outlined at its level, so it is seen which floor each part of the route is on.
	TSet<TPair<FString, int32>> Floors;
	for (const FNavigationRoutePoint& Point : Points)
	{
		if (Point.Floor != 0) Floors.Add(TPair<FString, int32>(Point.BuildingId, Point.Floor));
	}
	for (const TPair<FString, int32>& Floor : Floors)
	{
		const APolyBuilding* Building = Generator ? Generator->FindBuildingById(Floor.Key) : nullptr;
		const TArray<FVector2D>& Footprint = Building ? Building->GetFootprint() : TArray<FVector2D>();
		if (!Building || Footprint.Num() < 3) continue;
		const FLinearColor OutlineColor(0.45f, 0.72f, 1.f, 1.f);
		const float Z = Building->GetLevelBaseZ(Floor.Value) + FullRouteFloorShift(Floor.Key, Floor.Value) + 6.f;
		auto World = [&](int32 Index) { return FVector((Footprint[Index % Footprint.Num()] + Building->BuildingOffset) * Generator->GetScale(), Z); };
		for (int32 Index = 0; Index < Footprint.Num(); ++Index)
		{
			RouteVisualization->DrawLine(World(Index), World(Index + 1), OutlineColor, SDPG_Foreground, 3.f * LineScale, -1.f);
		}
	}
}
