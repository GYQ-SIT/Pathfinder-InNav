#include "InteriorBuildingPreviewPivot.h"

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "InteriorConnectorMarker.h"
#include "PolyBuilding.h"
#include "ContextBuildingActor.h"
#include "POIMarkerActor.h"
#include "RoadSegmentActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "SceneManagement.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"

AInteriorBuildingPreviewPivot::AInteriorBuildingPreviewPivot()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Pivot")));
	RootComponent->SetMobility(EComponentMobility::Movable);
	PivotGuide = CreateDefaultSubobject<ULineBatchComponent>(TEXT("PivotGuide"));
	PivotGuide->SetupAttachment(RootComponent);
	PivotGuide->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PivotGuide->DefaultLifeTime = -1.f;
	PivotGuide->bCalculateAccurateBounds = true;
	UStaticMeshComponent* Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PivotMarker"));
	Marker->SetupAttachment(RootComponent);
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetCastShadow(false);
	Marker->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	Marker->SetRelativeScale3D(FVector(0.3));
}

bool AInteriorBuildingPreviewPivot::GroupActor(AActor* Actor)
{
	if (!IsValid(Actor) || !Actor->GetRootComponent()) return false;
	USceneComponent* Component = Actor->GetRootComponent();
	FModelState State;
	State.Actor = Actor;
	State.Parent = Component->GetAttachParent();
	State.Socket = Component->GetAttachSocketName();
	State.World = Actor->GetActorTransform();
	State.Relative = Component->GetRelativeTransform();
	State.Mobility = Component->Mobility;
	State.bHidden = Actor->IsHidden();
	State.bCollision = Actor->GetActorEnableCollision();
	ModelStates.Add(State);
	Component->SetMobility(EComponentMobility::Movable);
	return Actor->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
}

bool AInteriorBuildingPreviewPivot::Initialize(APolyBuilding* Building, AInteriorGenerator* Interior, ABuildingGenerator* Generator)
{
	if (!Building || !Interior || !Generator || !ModelStates.IsEmpty()) return false;
	const TArray<int32> Levels = Interior->GetFloorLevels();
	if (Levels.IsEmpty()) return false;
	LowestFloor = Levels[0];
	FBox Bounds(ForceInit);
	for (const FVector2D& Point : Building->GetFootprint())
	{
		const FVector2D XY = (Point + Building->BuildingOffset) * Generator->GetScale();
		Bounds += Building->GetActorTransform().TransformPosition(FVector(XY, (LowestFloor - 1) * Building->GetFloorHeight()));
	}
	if (!Bounds.IsValid) return false;
	DefaultPivot = Bounds.GetCenter();
	SetActorLocation(DefaultPivot);
	OriginalBuildingWorld = Building->GetActorTransform();
	ViewedBuilding = Building;
	OriginalVisibleLevel = Building->GetVisibleInteriorLevel();
	const float Height = (Building->GetFloorCount() - LowestFloor + 1) * Building->GetFloorHeight();
	SweepRadius = FMath::Max(100.f, FMath::Sqrt(FMath::Square(Bounds.GetExtent().Size()) + FMath::Square(Height)));

	bool bGrouped = GroupActor(Building);
	for (int32 Level : Levels)
	{
		for (AInteriorRoom* Room : Interior->GetRoomsOnFloor(Level)) bGrouped &= GroupActor(Room);
	}
	for (const FInteriorNavNode& Node : Interior->GetNavNodes())
	{
		if (Node.MarkerActor) bGrouped &= GroupActor(Node.MarkerActor);
	}
	if (!bGrouped) { Restore(); return false; }

	AActor* Ground = Generator->FindGroundActor();
	ViewedGenerator = Generator;
	bGroundWasSuspended = Generator->IsGroundFitSuspended();
	Generator->SetGroundFitSuspended(true);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == Building || Actor == this) continue;
		if (Actor == Ground || Actor->ActorHasTag(TEXT("InteriorPreviewEnvironment")) || Actor->IsA<APolyBuilding>() ||
			Actor->IsA<AContextBuildingActor>() || Actor->IsA<ARoadSegmentActor>() || Actor->IsA<APOIMarkerActor>())
		{
			SceneStates.Add({ Actor, Actor->IsHidden(), Actor->GetActorEnableCollision() });
		}
	}
	Building->SetInspectionBottomHidden(true);
	ApplyIsolation();
	GuideSize = FMath::Clamp(SweepRadius * 0.012f, 25.f, 100.f);
	DrawGuide();
	if (UStaticMeshComponent* Marker = FindComponentByClass<UStaticMeshComponent>())
	{
		Marker->SetRelativeScale3D(FVector(FMath::Clamp(Bounds.GetExtent().Size() * 0.0004, 0.25, 1.0)));
		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Material, this);
			Tint->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.55f, 0.06f));
			Marker->SetMaterial(0, Tint);
		}
	}
	return true;
}

void AInteriorBuildingPreviewPivot::DrawGuide()
{
	// Foreground guide remains readable even when the viewed floor covers the pivot.
	PivotGuide->Flush();
	const FLinearColor Amber(1.f, 0.55f, 0.06f);
	PivotGuide->DrawSphere(GetActorLocation(), GuideSize, 16, Amber, -1.f, SDPG_Foreground, 2.f);
	for (const FVector& Axis : { FVector::ForwardVector, FVector::RightVector, FVector::UpVector })
	{
		PivotGuide->DrawLine(GetActorLocation() - Axis * GuideSize * 2.f, GetActorLocation() + Axis * GuideSize * 2.f,
			Amber, SDPG_Foreground, 2.f, -1.f);
	}
}

void AInteriorBuildingPreviewPivot::DetachModel()
{
	for (const FModelState& State : ModelStates)
	{
		if (AActor* Actor = State.Actor.Get()) Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
}

void AInteriorBuildingPreviewPivot::AttachModel()
{
	for (const FModelState& State : ModelStates)
	{
		if (AActor* Actor = State.Actor.Get()) Actor->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
	}
}

void AInteriorBuildingPreviewPivot::TranslateBy(const FVector& Delta, float MaxDistance)
{
	if (ModelStates.IsEmpty()) return;
	FVector Offset = GetActorLocation() + Delta - DefaultPivot;
	if (Offset.Size() > MaxDistance) Offset = Offset.GetSafeNormal() * MaxDistance;
	// The model hangs on this actor, so it goes with it.
	SetActorLocation(DefaultPivot + Offset);
	DrawGuide();
}

void AInteriorBuildingPreviewPivot::SetTiltRange(const FVector& Axis, float MinDegrees, float MaxDegrees)
{
	TiltAxis = Axis.GetSafeNormal();
	TiltMin = MinDegrees;
	TiltMax = MaxDegrees;
	RotateBy(0.f, 0.f);
}

void AInteriorBuildingPreviewPivot::ApplyIsolation()
{
	for (const FSceneState& State : SceneStates)
	{
		if (AActor* Actor = State.Actor.Get())
		{
			if (!Actor->IsHidden()) Actor->SetActorHiddenInGame(true);
			if (Actor->GetActorEnableCollision()) Actor->SetActorEnableCollision(false);
		}
	}
}

void AInteriorBuildingPreviewPivot::RotateBy(float YawDegrees, float TiltDegrees)
{
	Yaw = FMath::UnwindDegrees(Yaw + YawDegrees);
	Tilt = FMath::Clamp(Tilt + TiltDegrees, TiltMin, TiltMax);
	// Spin about the vertical axis first, then tip about the axis across the view: the tip is then always up and down on the screen, whatever
	// the spin is.
	SetActorRotation(FQuat(TiltAxis, FMath::DegreesToRadians(Tilt)) * FQuat(FVector::UpVector, FMath::DegreesToRadians(Yaw)));
}

void AInteriorBuildingPreviewPivot::ResetRotation()
{
	Yaw = Tilt = 0.f;
	if (ModelStates.IsEmpty())
	{
		SetActorRotation(FRotator::ZeroRotator);
		return;
	}
	// Back as entered: every actor where it was, the turning point at its start.
	DetachModel();
	for (const FModelState& State : ModelStates)
	{
		if (AActor* Actor = State.Actor.Get()) Actor->SetActorTransform(State.World, false, nullptr, ETeleportType::TeleportPhysics);
	}
	SetActorLocationAndRotation(DefaultPivot, FRotator::ZeroRotator);
	AttachModel();
	DrawGuide();
}

FTransform AInteriorBuildingPreviewPivot::GetDisplayTransform() const
{
	// From a place as the planner has it to where it is shown: the way the building has moved from where it was, whichever point it was
	// turned about.
	const APolyBuilding* Building = ViewedBuilding.Get();
	return Building ? OriginalBuildingWorld.Inverse() * Building->GetActorTransform() : FTransform::Identity;
}

void AInteriorBuildingPreviewPivot::Restore()
{
	// Reverse order also handles a room that was already attached to the shell before entry.
	for (int32 I = ModelStates.Num() - 1; I >= 0; --I)
	{
		const FModelState& State = ModelStates[I];
		AActor* Actor = State.Actor.Get();
		if (!Actor || !Actor->GetRootComponent()) continue;
		Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Actor->SetActorTransform(State.World, false, nullptr, ETeleportType::TeleportPhysics);
		if (USceneComponent* Parent = State.Parent.Get())
		{
			Actor->AttachToComponent(Parent, FAttachmentTransformRules::KeepWorldTransform, State.Socket);
			Actor->GetRootComponent()->SetRelativeTransform(State.Relative);
		}
		Actor->GetRootComponent()->SetMobility(State.Mobility);
		Actor->SetActorHiddenInGame(State.bHidden);
		Actor->SetActorEnableCollision(State.bCollision);
	}
	ModelStates.Reset();
	for (const FSceneState& State : SceneStates)
	{
		if (AActor* Actor = State.Actor.Get())
		{
			Actor->SetActorHiddenInGame(State.bHidden);
			Actor->SetActorEnableCollision(State.bCollision);
		}
	}
	SceneStates.Reset();
	if (APolyBuilding* Building = ViewedBuilding.Get())
	{
		Building->SetInspectionBottomHidden(false);
		Building->SetVisibleInteriorLevel(OriginalVisibleLevel);
	}
	ViewedBuilding.Reset();
	if (ABuildingGenerator* Generator = ViewedGenerator.Get()) Generator->SetGroundFitSuspended(bGroundWasSuspended);
	ViewedGenerator.Reset();
}

void AInteriorBuildingPreviewPivot::Destroyed()
{
	Restore();
	Super::Destroyed();
}

void AInteriorBuildingPreviewPivot::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Restore();
	Super::EndPlay(EndPlayReason);
}
