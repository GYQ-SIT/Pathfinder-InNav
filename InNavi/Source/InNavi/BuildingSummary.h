#pragma once

#include "CoreMinimal.h"

// What the Building panel shows about one building (see ABuildingGenerator::GetBuildingSummary).
struct FBuildingSummary
{
	FString Id;
	FString Name;
	int32 Floors = 0;
	int32 FirstLevel = 1;    // the level of the lowest floor (1 = the ground floor, -1 = B1); the floors go up from it
	int32 Rooms = 0;         // rooms on all floors, from the interior data
	int32 VerticalLinks = 0; // elevators, stairs and escalators (a link across several floors counts once)
	FString Type;            // one value from BuildingTypes::All()
	TArray<FVector2D> Footprint;
};
