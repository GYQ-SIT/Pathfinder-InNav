#pragma once

#include "CoreMinimal.h"
#include "NavigationTypes.h"

class FInNaviNavigationSystem
{
public:
    FInNaviNavigationSystem();

    void Reset();

    void AddNode(
        const FNavigationNode& Node);

    void AddConnection(
        const FNavigationConnection& Connection);

    bool HasNode(
        const FString& NodeId) const;

    bool GetNode(
        const FString& NodeId,
        FNavigationNode& OutNode) const;

    bool FindRoute(
        const FNavigationRouteRequest& Request,
        FNavigationRouteResult& OutResult) const;

    FNavigationPositionCheckResult ValidatePosition(
        const FVector2D& Position,
        int32 Level,
        float MaxDistanceToNode = 200.0f) const;

    bool IsReady() const;

private:
    TMap<FString, FNavigationNode> Nodes;

    TArray<FNavigationConnection> Connections;
};