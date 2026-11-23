// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteriorTypes.h"
#include "InteriorConnectorMarker.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
struct FConnectorPart;

// The 3D shape of one floor-segment of an elevator, a staircase or an escalator: the lift is a glass shaft with its steel frame, a landing door
// and a floor indicator; the stairs have treads, risers, side stringers, balusters and handrails; the escalator has a truss, a run of steps,
// glass balustrades with skirts and moving handrails. Each is made of several parts (frame, glass, treads ...), each with its own material.
// (The 2D floor plan draws these as icons, see UInteriorFloorPlanWidget.)
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

	// Builds this floor segment's geometry for Type, centered at WorldCenter (XY),
	// spanning FloorBaseZ to FloorBaseZ + FloorHeight, then rotated (about WorldCenter,
	// RotationDegrees, positive = counter-clockwise) and uniformly scaled (SizeScale, footprint only
	// - FloorHeight is never scaled). Stays inside GetConnectorFootprintCorners (InteriorTypes.h), so the mesh and the
	// placement/overlap checks never disagree about where the connector actually is, and its top lies a little under the slab above
	// (see SlabClearance in the .cpp). No-op (empty mesh) for PathNode - callers shouldn't spawn a marker for one at all, see
	// AInteriorGenerator::SpawnConnectorMarker. Can be called again to rebuild: the earlier parts are cleared first.
	void BuildMarker(EInteriorNavNodeType Type, const FVector2D& WorldCenter, float FloorBaseZ, float FloorHeight, float RotationDegrees, float SizeScale);

	// Tints the whole marker light green (3D Edit mode's selection indicator) when true, back to the parts' own colours when
	// false. Mirrors APolyBuilding::SetHighlighted / AInteriorRoom::SetHighlighted.
	void SetHighlighted(bool bHighlighted);

	// Which connector group (FInteriorNavNode::ConnectorGroupId, same id RotateConnectorGroup/
	// ScaleConnectorGroup/SelectConnector use) this floor-segment marker belongs to - set by
	// AInteriorGenerator::SpawnConnectorMarker/RebuildConnectorMarker, since a hit on this actor only
	// gives the actor itself, not the FInteriorNavNode it was built from.
	void SetOwnerGroupId(const FString& InGroupId) { OwnerGroupId = InGroupId; }
	const FString& GetOwnerGroupId() const { return OwnerGroupId; }

private:
	// Each Build* function builds its parts in LOCAL space first (centered on the origin, unrotated, unscaled - the connector's "rise"
	// direction is +X, its width Y) and CommitParts applies WorldCenter/RotationDegrees/SizeScale at the very end.
	void BuildElevatorShaft(const FVector2D& WorldCenter, float FloorBaseZ, float FloorHeight, float RotationDegrees, float SizeScale);
	void BuildStairs(const FVector2D& WorldCenter, float FloorBaseZ, float FloorHeight, float RotationDegrees, float SizeScale);
	void BuildEscalator(const FVector2D& WorldCenter, float FloorBaseZ, float FloorHeight, float RotationDegrees, float SizeScale);
	// Makes one mesh section and one material per non-empty part.
	void CommitParts(TArray<FConnectorPart>& Parts, const FVector2D& WorldCenter, float RotationDegrees, float SizeScale);

	UPROPERTY()
	UProceduralMeshComponent* MeshComp;

	// One material per part (the glass ones see-through), and the colour each had, which SetHighlighted(false) gives back.
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PartMaterials;
	TArray<FLinearColor> PartColors;

	FString OwnerGroupId;
};
