#pragma once

#include "CoreMinimal.h"

// Defines the data structures used by the navigation system
enum class ENavigationNodeType : uint8
{
    Room,
    Door,
    Connector
};

struct FNavigationNode
{
    FString Id;
    int32 Level = 0;
    FVector2D Position = FVector2D::ZeroVector;

    ENavigationNodeType Type = ENavigationNodeType::Room;

    // Used for elevators, stairs, and escalators
    FString ConnectorGroupId;
};

struct FNavigationConnection
{
    FString FromNodeId;
    FString ToNodeId;

    float Cost = 1.0f;
    bool bCrossesFloor = false;
};