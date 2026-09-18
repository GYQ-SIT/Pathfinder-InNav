// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InteriorTypes.generated.h"

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
};
