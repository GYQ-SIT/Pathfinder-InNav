#pragma once

#include "CoreMinimal.h"
#include "NavigationRoutePlanner.h"

enum class ENavigationInstructionKind : uint8
{
	Straight, TurnLeft, TurnRight, TurnAround, PassDoor,
	TakeLift, TakeStairs, TakeEscalator, ChangeFloor, ExitBuilding, EnterBuilding
};

// An instruction covers original route points, so grouping directions never changes the drawn path.
struct FNavigationInstruction
{
	ENavigationInstructionKind Kind = ENavigationInstructionKind::Straight;
	FString Text;
	int32 PointIndex = INDEX_NONE;
	int32 LastPointIndex = INDEX_NONE;
	double Metres = 0.0;
};

namespace NavigationInstructions
{
	// Footprint maps draw y downwards. Both clients use this convention for left/right.
	INNAVI_API bool IsLeftTurn(const FVector2D& Incoming, const FVector2D& Outgoing);
	INNAVI_API TArray<FNavigationInstruction> Build(const TArray<FNavigationRoutePoint>& Points,
		TFunctionRef<FVector(const FNavigationRoutePoint&)> WorldPosition,
		TFunctionRef<FString(const FString&)> BuildingName,
		int32 FirstPoint = 0, int32 LastPoint = INDEX_NONE);
}
