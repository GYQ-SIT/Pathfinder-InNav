#include "NavigationSystem.h"

// Implements the functions declared in NavigationSystem.h
FInNaviNavigationSystem::FInNaviNavigationSystem()
{
}

void FInNaviNavigationSystem::Reset()
{
    Nodes.Empty();
    Connections.Empty();
}

void FInNaviNavigationSystem::AddNode(
    const FNavigationNode& Node)
{
    Nodes.Add(Node.Id, Node);
}

void FInNaviNavigationSystem::AddConnection(
    const FNavigationConnection& Connection)
{
    Connections.Add(Connection);
}

bool FInNaviNavigationSystem::HasNode(
    const FString& NodeId) const
{
    return Nodes.Contains(NodeId);
}