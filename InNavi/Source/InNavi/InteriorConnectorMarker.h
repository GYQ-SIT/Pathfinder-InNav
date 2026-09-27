// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteriorTypes.h"
#include "InteriorConnectorMarker.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

// Placeholder 3D geometry for one floor-segment of an elevator/stairs/escalator connector node -
// the first 3D representation these have (the 2D floor plan already draws them as icons, see
// UInteriorFloorPlanWidget, but the 3D preview mode - ANavPlayerController::SetIn3DPreviewMode -
// had nothing at all to show for them). Basic primitive shapes, not real art, per the "占位"
// (placeholder) scope of this pass.
//
// One marker per FInteriorNavNode (see FInteriorNavNode::MarkerActor, and
// AInteriorGenerator::SpawnConnectorMarker which owns the spawn/destroy lifecycle) - a multi-floor
// connector already gets one FInteriorNavNode per floor it reaches, so its markers naturally stack
// into one continuous shape spanning those floors without this class needing to know about the
// group as a whole.
//
// Spawned at the world origin with world-space positions baked directly into the mesh's own
// vertices, same convention as AInteriorRoom/APolyBuilding, rather than using the actor's own
// transform.
UCLASS()
class INNAVI_API AInteriorConnectorMarker : public AActor
{
	GENERATED_BODY()

public:
	AInteriorConnectorMarker();

	// Builds this floor segment's placeholder geometry for Type, centered at WorldCenter (XY),
	// spanning FloorBaseZ to FloorBaseZ + FloorHeight, then rotated (about WorldCenter,
	// RotationDegrees, positive = counter-clockwise) and uniformly scaled (SizeScale, footprint only
	// - FloorHeight is never scaled). Matches GetConnectorFootprintCorners (InteriorTypes.h) exactly,
	// so the mesh and the placement/overlap checks never disagree about where the connector actually
	// is. No-op (empty mesh) for PathNode - callers shouldn't spawn a marker for one at all, see
	// AInteriorGenerator::SpawnConnectorMarker.
	void BuildMarker(EInteriorNavNodeType Type, const FVector2D& WorldCenter, float FloorBaseZ, float FloorHeight, float RotationDegrees, float SizeScale);

	// Tints the marker to a light green (3D Edit mode's selection indicator) when true, back to this
	// connector's own normal tint (set by BuildMarker, per-type) when false. Mirrors
	// APolyBuilding::SetHighlighted / AInteriorRoom::SetHighlighted.
	void SetHighlighted(bool bHighlighted);

	// Which connector group (FInteriorNavNode::ConnectorGroupId, same id RotateConnectorGroup/
	// ScaleConnectorGroup/SelectConnector use) this floor-segment marker belongs to - set by
	// AInteriorGenerator::SpawnConnectorMarker/RebuildConnectorMarker, since a hit on this actor only
	// gives the actor itself, not the FInteriorNavNode it was built from.
	void SetOwnerGroupId(const FString& InGroupId) { OwnerGroupId = InGroupId; }
	const FString& GetOwnerGroupId() const { return OwnerGroupId; }

private:
	// Both Build* functions build their geometry in LOCAL space first (centered on the origin,
	// unrotated, unscaled - the same fixed +X "rise" direction as before) and only apply
	// WorldCenter/RotationDegrees/SizeScale at the very end (see the TransformFootprintVertices
	// helper in the .cpp) - keeps the actual box-building logic simple regardless of orientation.
	void BuildElevatorShaft(const FVector2D& WorldCenter, float FloorBaseZ, float FloorHeight, float RotationDegrees, float SizeScale);
	// bIsEscalator only changes the tint, so stairs/escalator stay visually distinguishable despite
	// sharing the exact same step shape.
	void BuildStairsSteps(const FVector2D& WorldCenter, float FloorBaseZ, float FloorHeight, float RotationDegrees, float SizeScale, bool bIsEscalator);

	UPROPERTY()
	UProceduralMeshComponent* MeshComp;

	UPROPERTY()
	UMaterialInstanceDynamic* DynMaterial;

	// This connector type's own tint (set by BuildElevatorShaft/BuildStairsSteps) - restored by
	// SetHighlighted(false) instead of a single shared constant, since elevator/stairs/escalator each
	// have a different normal color.
	FLinearColor NormalColor = FLinearColor::White;

	FString OwnerGroupId;
};
