#pragma once

#include "CoreMinimal.h"

// The colours of the three kinds of door on the 2D floor plan, which the Properties panel's tag for the selected door reuses so the two
// match: a door in a room's wall, a main door (the way out) in the outer wall, and a link door (to the building next door).
namespace InteriorDoorColors
{
	inline FLinearColor RoomDoor() { return FLinearColor(0.f, 0.85f, 1.f, 1.f); }
	inline FLinearColor MainDoor() { return FLinearColor(0.18f, 0.8f, 0.45f, 1.f); }
	inline FLinearColor LinkDoor() { return FLinearColor(0.26f, 0.59f, 1.f, 1.f); }
}
