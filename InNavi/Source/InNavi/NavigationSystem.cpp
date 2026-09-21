#include "NavigationSystem.h"
#include "Containers/Queue.h"

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
    if (Node.Id.IsEmpty())
    {
        return;
    }

    Nodes.Add(Node.Id, Node);
}

void FInNaviNavigationSystem::AddConnection(
    const FNavigationConnection& Connection)
{
    if (Connection.FromNodeId.IsEmpty() ||
        Connection.ToNodeId.IsEmpty())
    {
        return;
    }

    Connections.Add(Connection);
}

bool FInNaviNavigationSystem::HasNode(
    const FString& NodeId) const
{
    return Nodes.Contains(NodeId);
}

bool FInNaviNavigationSystem::GetNode(
    const FString& NodeId,
    FNavigationNode& OutNode) const
{
    const FNavigationNode* FoundNode =
        Nodes.Find(NodeId);

    if (!FoundNode)
    {
        return false;
    }

    OutNode = *FoundNode;
    return true;
}

bool FInNaviNavigationSystem::IsReady() const
{
    return Nodes.Num() > 0;
}

FNavigationPositionCheckResult
FInNaviNavigationSystem::ValidatePosition(
    const FVector2D& Position,
    int32 Level,
    float MaxDistanceToNode) const
{
    FNavigationPositionCheckResult Result;

    float ClosestDistanceSquared =
        MaxDistanceToNode * MaxDistanceToNode;

    for (const TPair<FString, FNavigationNode>& Pair : Nodes)
    {
        const FNavigationNode& Node = Pair.Value;

        if (Node.Level != Level)
        {
            continue;
        }

        if (!Node.bWalkable)
        {
            continue;
        }

        const float DistanceSquared =
            FVector2D::DistSquared(
                Position,
                Node.Position);

        if (DistanceSquared <= ClosestDistanceSquared)
        {
            ClosestDistanceSquared = DistanceSquared;

            Result.bIsValid = true;
            Result.NearestNodeId = Node.Id;

            Result.DistanceToNearestNode =
                FMath::Sqrt(DistanceSquared);
        }
    }

    if (Result.bIsValid)
    {
        Result.Message = FString::Printf(
            TEXT("Position is valid near node '%s'."),
            *Result.NearestNodeId);
    }
    else
    {
        Result.Message = TEXT(
            "Position is not close enough to a valid navigation node.");
    }

    return Result;
}

bool FInNaviNavigationSystem::FindRoute(
    const FNavigationRouteRequest& Request,
    FNavigationGraphRouteResult& OutResult) const
{
    // Clear any previous route result.
    OutResult = FNavigationGraphRouteResult();

    if (!IsReady())
    {
        OutResult.Status = ENavigationRouteStatus::NotReady;
        OutResult.Message =
            TEXT("Navigation system has no nodes.");
        return false;
    }

    const FNavigationNode* StartNode =
        Nodes.Find(Request.StartNodeId);

    if (!StartNode || !StartNode->bWalkable)
    {
        OutResult.Status =
            ENavigationRouteStatus::InvalidStart;
        OutResult.Message =
            TEXT("Start node does not exist or is not walkable.");
        return false;
    }

    const FNavigationNode* DestinationNode =
        Nodes.Find(Request.DestinationNodeId);

    if (!DestinationNode || !DestinationNode->bWalkable)
    {
        OutResult.Status =
            ENavigationRouteStatus::InvalidDestination;
        OutResult.Message =
            TEXT("Destination node does not exist or is not walkable.");
        return false;
    }

    if (Request.StartNodeId == Request.DestinationNodeId)
    {
        OutResult.Status =
            ENavigationRouteStatus::AlreadyAtDestination;

        OutResult.NodeIds.Add(StartNode->Id);
        OutResult.Positions.Add(StartNode->Position);
        OutResult.Message =
            TEXT("Start and destination are the same.");

        return true;
    }

    const float InfiniteCost =
        TNumericLimits<float>::Max();

    TMap<FString, float> BestCost;
    TMap<FString, FString> PreviousNode;
    TSet<FString> VisitedNodes;

    // Give every node an initially unreachable cost.
    for (const TPair<FString, FNavigationNode>& Pair : Nodes)
    {
        BestCost.Add(Pair.Key, InfiniteCost);
    }

    // The route starts with a cost of zero.
    BestCost.Add(Request.StartNodeId, 0.0f);

    while (true)
    {
        FString CurrentNodeId;
        float LowestCost = InfiniteCost;

        // Find the unvisited node with the cheapest known route.
        for (const TPair<FString, float>& Pair : BestCost)
        {
            if (VisitedNodes.Contains(Pair.Key))
            {
                continue;
            }

            if (Pair.Value < LowestCost)
            {
                LowestCost = Pair.Value;
                CurrentNodeId = Pair.Key;
            }
        }

        // No more reachable nodes remain.
        if (CurrentNodeId.IsEmpty())
        {
            break;
        }

        VisitedNodes.Add(CurrentNodeId);

        // The cheapest route has reached the destination.
        if (CurrentNodeId == Request.DestinationNodeId)
        {
            break;
        }

        const float* CurrentCost =
            BestCost.Find(CurrentNodeId);

        if (!CurrentCost)
        {
            continue;
        }

        // Check every connection to find neighbours of this node.
        for (const FNavigationConnection& Connection :
            Connections)
        {
            FString NeighbourNodeId;
            bool bCanUseConnection = false;

            if (Connection.FromNodeId == CurrentNodeId)
            {
                NeighbourNodeId = Connection.ToNodeId;
                bCanUseConnection = true;
            }
            else if (
                Connection.bBidirectional &&
                Connection.ToNodeId == CurrentNodeId)
            {
                NeighbourNodeId = Connection.FromNodeId;
                bCanUseConnection = true;
            }

            if (!bCanUseConnection ||
                VisitedNodes.Contains(NeighbourNodeId))
            {
                continue;
            }

            const FNavigationNode* NeighbourNode =
                Nodes.Find(NeighbourNodeId);

            if (!NeighbourNode || !NeighbourNode->bWalkable)
            {
                continue;
            }

            // Prevent negative connection costs.
            const float ConnectionCost =
                FMath::Max(Connection.Cost, 0.0f);

            const float NewCost =
                *CurrentCost + ConnectionCost;

            float* OldCost =
                BestCost.Find(NeighbourNodeId);

            // Save this route only when it is cheaper.
            if (OldCost && NewCost < *OldCost)
            {
                *OldCost = NewCost;
                PreviousNode.Add(
                    NeighbourNodeId,
                    CurrentNodeId);
            }
        }
    }

    if (!VisitedNodes.Contains(Request.DestinationNodeId))
    {
        OutResult.Status =
            ENavigationRouteStatus::NoRoute;
        OutResult.Message =
            TEXT("No route exists between the selected nodes.");
        return false;
    }

    // Follow the previous-node links backwards.
    // Insert each node at the front to restore start-to-destination order.
    FString CurrentNodeId =
        Request.DestinationNodeId;

    while (true)
    {
        OutResult.NodeIds.Insert(CurrentNodeId, 0);

        if (CurrentNodeId == Request.StartNodeId)
        {
            break;
        }

        const FString* PreviousId =
            PreviousNode.Find(CurrentNodeId);

        if (!PreviousId)
        {
            OutResult.Status =
                ENavigationRouteStatus::NoRoute;
            OutResult.NodeIds.Empty();
            OutResult.Message =
                TEXT("Route reconstruction failed.");
            return false;
        }

        CurrentNodeId = *PreviousId;
    }

    // Convert the node IDs into positions for drawing the route.
    for (const FString& NodeId : OutResult.NodeIds)
    {
        const FNavigationNode* Node =
            Nodes.Find(NodeId);

        if (Node)
        {
            OutResult.Positions.Add(Node->Position);
        }
    }

    const float* TotalCost =
        BestCost.Find(Request.DestinationNodeId);

    OutResult.TotalCost =
        TotalCost ? *TotalCost : 0.0f;

    OutResult.Status =
        ENavigationRouteStatus::Success;

    OutResult.Message = FString::Printf(
        TEXT("Route found with %d nodes."),
        OutResult.NodeIds.Num());

    return true;
}