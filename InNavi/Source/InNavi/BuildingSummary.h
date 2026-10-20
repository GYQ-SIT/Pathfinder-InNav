#pragma once

#include "CoreMinimal.h"

// What the Building panel shows about one building (see ABuildingGenerator::GetBuildingSummary).
struct FBuildingSummary
{
	FString Id;
	FString Name;
	int32 Floors = 0;
	int32 Rooms = 0;         // rooms on all floors, from the interior data
	int32 VerticalLinks = 0; // elevators, stairs and escalators (a link across several floors counts once)
	TArray<FVector2D> Footprint;
};
