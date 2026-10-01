// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InteriorTypes.generated.h"

class AInteriorConnectorMarker;

UENUM(BlueprintType)
enum class EInteriorOpeningType : uint8
{
	Door,
	Window,
	Void
};

UENUM(BlueprintType)
enum class EInteriorNavNodeType : uint8
{
	PathNode,
	Elevator,
	Stairs,
	Escalator
};

// Used by ANavPlayerController::OnActionFailed. Warning: the action was rejected before anything
// changed (validation failed, a precondition wasn't met, the user cancelled a dialog, ...) -
// nothing was lost. Critical: existing data was already cleared/discarded before the failure was
// detected (e.g. Open/Load found no buildings in the new file/database, after the previous project
// was already cleared), or the user's edits may look saved when they aren't (a save silently
// failed) - the user could lose work if they don't notice.
UENUM(BlueprintType)
enum class EActionFailureSeverity : uint8
{
	Warning,
	Critical
};

// One opening (door/window/void) cut into a specific edge of a room's wall loop.
USTRUCT(BlueprintType)
struct FInteriorOpening
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString Id;

	UPROPERTY(BlueprintReadOnly)
	EInteriorOpeningType Type = EInteriorOpeningType::Door;

	// Which edge of the room footprint this opening is on (edge i = Footprint[i] to Footprint[i+1]).
	UPROPERTY(BlueprintReadOnly)
	int32 EdgeIndex = 0;

	// Distance along the edge, from its start point, to the start of the opening (world units).
	UPROPERTY(BlueprintReadOnly)
	float Offset = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float Width = 0.f;

	// Window only: height of the window opening and how far off the floor it starts.
	UPROPERTY(BlueprintReadOnly)
	float Height = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float SillHeight = 0.f;

	// Door only, for the navigation graph: InputId is the room this door leads into (the room that
	// owns this opening - not auto-filled from that, kept as its own explicit field in case a door
	// ever needs to record something other than its owning room, e.g. once shared/graph-based walls
	// exist), and ConnectsTo ("output") is the room/corridor/exterior id on the other side. Both are
	// left blank when the "add door" tool first creates a door - filled in later via a proper UI
	// form, not automatically.
	UPROPERTY(BlueprintReadOnly)
	FString InputId;

	UPROPERTY(BlueprintReadOnly)
	FString ConnectsTo;
};

// A navigation graph node - a point placed anywhere inside a building's interior (including
// inside a room - it has no shape/walls/door of its own, unlike a room or a door). Elevator/
// Stairs/Escalator span multiple floors: placing one creates a separate FInteriorNavNode per
// floor it connects to (each written into that floor's own "navNodes" array, matching the JSON
// schema, which is per-floor), all sharing the same ConnectorGroupId so they can be found and
// kept in sync as "the same physical connector" - see AInteriorGenerator::PlaceConnectorNode/
// SetConnectorFloors. PathNode is always a single, standalone, single-floor node.
USTRUCT(BlueprintType)
struct FInteriorNavNode
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString Id;

	UPROPERTY(BlueprintReadOnly)
	EInteriorNavNodeType Type = EInteriorNavNodeType::PathNode;

	// Which floor this node was placed on (the floor whose "navNodes" array it came from) - needed
	// to write it back into the right floor when serializing the interior back to JSON.
	UPROPERTY(BlueprintReadOnly)
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly)
	FVector2D Position = FVector2D::ZeroVector;

	// Elevator/Stairs/Escalator only: which floor levels this connector leads to. An elevator
	// defaults to every floor in the building; stairs/escalator default to just the immediately
	// adjacent floor(s) (one at the top/bottom of the building, since there's only one side) - both
	// are freely user-editable afterward, including down to a single floor (e.g. a staircase that
	// dead-ends partway up the building). Empty for PathNode.
	UPROPERTY(BlueprintReadOnly)
	TArray<int32> ConnectsFloors;

	// Elevator/Stairs/Escalator only: shared by every per-floor FInteriorNavNode instance that
	// represents this same physical connector, so they can be explicitly identified as one group
	// rather than inferred from matching position - see the class comment above. Empty for PathNode.
	UPROPERTY(BlueprintReadOnly)
	FString ConnectorGroupId;

	// Elevator/Stairs/Escalator only: this connector's own orientation/size, on top of
	// GetConnectorFootprintHalfSize's fixed base shape (see GetConnectorFootprintCorners, which
	// combines the two). Kept in sync across every floor instance sharing ConnectorGroupId by
	// AInteriorGenerator::RotateConnectorGroup/ScaleConnectorGroup, the same way Position/
	// ConnectsFloors already are (see SetConnectorFloors) - a multi-floor connector is one physical
	// object, so every floor's copy of it should look the same. Degrees, positive = counter-
	// clockwise, matching RotateRoom's convention. 0/1 (no-op) for PathNode.
	UPROPERTY(BlueprintReadOnly)
	float RotationDegrees = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float SizeScale = 1.f;

	// The 3D preview's placeholder geometry for this node (see AInteriorConnectorMarker) - spawned/
	// destroyed alongside this struct entry by AInteriorGenerator::SpawnConnectorMarker and the
	// various NavNodes-removal sites. Null for PathNode (no 3D marker built for those). Purely a
	// runtime visual, not serialized to JSON - rebuilt fresh every load.
	UPROPERTY()
	AInteriorConnectorMarker* MarkerActor = nullptr;
};

// Fixed placeholder footprint half-size (world units, at RotationDegrees=0/SizeScale=1) for each
// connector type's 3D marker geometry (see AInteriorConnectorMarker) - shared with
// GetConnectorFootprintCorners and AInteriorGenerator's overlap checks so all three can never
// disagree about how much space a connector actually occupies. X = half-depth (along the
// connector's own local +X, i.e. its "forward"/rise direction before RotationDegrees is applied),
// Y = half-width. Zero for PathNode, which has no 3D footprint to check.
inline FVector2D GetConnectorFootprintHalfSize(EInteriorNavNodeType Type)
{
	switch (Type)
	{
	case EInteriorNavNodeType::Elevator:  return FVector2D(100.f, 100.f); // 200x200
	case EInteriorNavNodeType::Stairs:
	case EInteriorNavNodeType::Escalator: return FVector2D(125.f, 75.f); // 250 deep x 150 wide
	default:                              return FVector2D::ZeroVector; // PathNode
	}
}

// The connector's actual 4 footprint corners (world units, CCW order), given its base shape (see
// GetConnectorFootprintHalfSize), RotationDegrees (about its own center, positive = counter-
// clockwise - same convention AInteriorGenerator::RotateRoom already uses) and SizeScale (uniform
// multiplier on the base half-size). Returned RELATIVE TO THE CONNECTOR'S OWN CENTER, not yet
// translated to a position - callers add their own center afterward, in whichever space they're
// working in (AInteriorConnectorMarker in world space for the 3D mesh, AInteriorGenerator in
// pre-scale/building-local space for placement/overlap checks - see CanPlaceConnectorOnFloor's own
// comment on that conversion). Empty for PathNode.
inline void GetConnectorFootprintCorners(EInteriorNavNodeType Type, float RotationDegrees, float SizeScale, TArray<FVector2D>& OutCorners)
{
	OutCorners.Reset();

	const FVector2D HalfSize = GetConnectorFootprintHalfSize(Type) * FMath::Max(SizeScale, KINDA_SMALL_NUMBER);
	if (HalfSize.IsNearlyZero())
	{
		return;
	}

	const FVector2D LocalCorners[4] =
	{
		FVector2D(-HalfSize.X, -HalfSize.Y),
		FVector2D( HalfSize.X, -HalfSize.Y),
		FVector2D( HalfSize.X,  HalfSize.Y),
		FVector2D(-HalfSize.X,  HalfSize.Y),
	};

	const float Radians = FMath::DegreesToRadians(RotationDegrees);
	const float CosA = FMath::Cos(Radians);
	const float SinA = FMath::Sin(Radians);

	OutCorners.Reserve(4);
	for (const FVector2D& Local : LocalCorners)
	{
		OutCorners.Add(FVector2D(Local.X * CosA - Local.Y * SinA, Local.X * SinA + Local.Y * CosA));
	}
}
