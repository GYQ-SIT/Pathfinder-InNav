#pragma once

#include "CoreMinimal.h"

enum class ENavigationNodeType : uint8
{
    Room,
    Door,
    Connector
};

enum class ENavigationRouteStatus : uint8
{
    NotReady,
    Success,
    InvalidStart,
    InvalidDestination,
    NoRoute,
    AlreadyAtDestination
};

struct FNavigationNode
{
    FString Id;

    int32 Level = 0;

    FVector2D Position = FVector2D::ZeroVector;

    ENavigationNodeType Type = ENavigationNodeType::Room;

    FString ConnectorGroupId;

    bool bWalkable = true;
};

struct FNavigationConnection
{
    FString FromNodeId;

    FString ToNodeId;

    float Cost = 1.0f;

    bool bCrossesFloor = false;

    bool bBidirectional = true;
};

struct FNavigationRouteRequest
{
    FString StartNodeId;

    FString DestinationNodeId;
};

struct FNavigationGraphRouteResult
{
    ENavigationRouteStatus Status =
        ENavigationRouteStatus::NotReady;

    TArray<FString> NodeIds;

    TArray<FVector2D> Positions;

    float TotalCost = 0.0f;

    FString Message;

    bool IsSuccess() const
    {
        return Status == ENavigationRouteStatus::Success ||
            Status == ENavigationRouteStatus::AlreadyAtDestination;
    }
};

struct FNavigationPositionCheckResult
{
    bool bIsValid = false;

    FString NearestNodeId;

    float DistanceToNearestNode = 0.0f;

    FString Message;
};