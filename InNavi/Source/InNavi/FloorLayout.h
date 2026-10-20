#pragma once

#include "CoreMinimal.h"
#include "InteriorTypes.h"

// Makes up the rooms and the lifts-and-stairs of one floor from its outline and its doors, nothing else - pure geometry, no actors, so it can
// be tested by itself. AInteriorGenerator::GenerateFloor turns the result into real rooms with the editor's own creation functions, which
// keep their own validation.
//
// How a floor is laid out (after how real buildings look):
//  - Rooms hug the outer wall: along each edge of the outline a row of rooms, as deep as the building allows while leaving a walkable
//    middle. What is left in the middle (and in front of every door) is the corridor: no walls, just open floor.
//  - Every door in the outer wall gets an open passage through the row of rooms, so it opens onto the corridor.
//  - A "core" - two lifts, a stair, and a toilet beside it - stands against the wall beside each door (one core per door, at least
//    one). The row of rooms leaves the core's stretch of wall free.
//  - Rooms of one kind sit together, a few rooms are left out, and corners are sometimes left open.
//
// All coordinates are the building's footprint units (the same space as its outline and as every room footprint).

struct FFloorDoor
{
	// The outline edge the door is on (edge i = Outline[i] to Outline[i + 1]), where its middle is along that edge from the edge's start,
	// and how wide it is.
	int32 EdgeIndex = 0;
	double MiddleAlong = 0.0;
	double Width = 0.0;
};

struct FFloorRoom
{
	// One of RoomTypes::All().
	FString Type;
	// Counter-clockwise.
	TArray<FVector2D> Footprint;
	// Which edge of the footprint faces the corridor and where along it the door goes (0 = its start, 1 = its end).
	int32 DoorEdgeIndex = 0;
	float DoorFraction = 0.5f;
};

struct FFloorConnector
{
	EInteriorNavNodeType Type = EInteriorNavNodeType::Elevator;
	FVector2D Position = FVector2D::ZeroVector;
	float RotationDegrees = 0.f;
};

struct FFloorLayoutParams
{
	// The same seed always gives the same layout.
	int32 Seed = 0;
	// How many footprint units make a metre (100 / the project's scale), so rooms come out a believable size.
	double UnitsPerMetre = 10.0;
	TArray<FVector2D> Outline;
	// The doors in the outer wall on this floor. One core is made for each, or one if there are none.
	TArray<FFloorDoor> Doors;
	// Where lifts, stairs and escalators already stand (their footprints, 4 corners each, and their centres): rooms keep clear of them.
	TArray<TArray<FVector2D>> ConnectorFootprints;
	TArray<FVector2D> ConnectorCentres;
	// False when the building already has lifts and stairs: they are used as they are and no new ones are made. Toilets are placed
	// beside them all the same.
	bool bMakeCore = true;
	// A big building gets fewer, larger rooms so that no floor holds more than this many (a floor with very many walls makes route-finding slow).
	int32 MaxRooms = 24;
};

struct FFloorLayout
{
	TArray<FFloorRoom> Rooms;
	// The new core's lifts and stair (empty when the building already had them).
	TArray<FFloorConnector> Connectors;
	// Where each core is: the middle of its stretch of wall.
	TArray<FVector2D> CoreCentres;
};

namespace FloorLayout
{
	// False, with OutError saying why, if the outline is too small to hold a row of rooms and a core.
	INNAVI_API bool Generate(const FFloorLayoutParams& Params, FFloorLayout& OutLayout, FString& OutError);
}
