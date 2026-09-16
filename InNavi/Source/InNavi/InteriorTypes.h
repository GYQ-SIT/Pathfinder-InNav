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

	// Door only: the room (or "corridor"/"exterior") id on the other side, for the navigation graph.
	UPROPERTY(BlueprintReadOnly)
	FString ConnectsTo;
};

// A navigation graph node parsed from a floor's interior data. Not yet turned into an actor -
// representation is pending alignment with the navigation (NavMesh/NavLinks) team.
USTRUCT(BlueprintType)
struct FInteriorNavNode
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString Id;

	// "pathNode" | "elevator" | "stairs"
	UPROPERTY(BlueprintReadOnly)
	FString Type;

	// Which floor this node was placed on (the floor whose "navNodes" array it came from) - needed
	// to write it back into the right floor when serializing the interior back to JSON.
	UPROPERTY(BlueprintReadOnly)
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly)
	FVector2D Position = FVector2D::ZeroVector;

	// elevator/stairs only: which floor levels this node connects.
	UPROPERTY(BlueprintReadOnly)
	TArray<int32> ConnectsFloors;
};
