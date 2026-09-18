#pragma once

#include "CoreMinimal.h"
#include "NavigationTypes.h"

// Declares the InNavi navigation system class
class FInNaviNavigationSystem
{
public:
    FInNaviNavigationSystem();

    void Reset();

    void AddNode(const FNavigationNode& Node);

    void AddConnection(const FNavigationConnection& Connection);

    bool HasNode(const FString& NodeId) const;

private:
    TMap<FString, FNavigationNode> Nodes;
    TArray<FNavigationConnection> Connections;
};