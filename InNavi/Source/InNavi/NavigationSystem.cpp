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
    FNavigationRouteResult& OutResult) const
{
    OutResult = FNavigationRouteResult();

    if (!IsReady())
    {
        OutResult.Status =
            ENavigationRouteStatus::NotReady;

        OutResult.Message = TEXT(
            "Navigation system has no nodes.");

        return false;
    }

    if (!Nodes.Contains(Request.StartNodeId))
    {
        OutResult.Status =
            ENavigationRouteStatus::InvalidStart;

        OutResult.Message = TEXT(
            "Start node does not exist.");

        return false;
    }

    if (!Nodes.Contains(Request.DestinationNodeId))
    {
        OutResult.Status =
            ENavigationRouteStatus::InvalidDestination;

        OutResult.Message = TEXT(
            "Destination node does not exist.");

        return false;
    }

    if (Request.StartNodeId ==
        Request.DestinationNodeId)
    {
        const FNavigationNode& Node =
            Nodes[Request.StartNodeId];

        OutResult.Status =
            ENavigationRouteStatus::AlreadyAtDestination;

        OutResult.NodeIds.Add(Node.Id);
        OutResult.Positions.Add(Node.Position);

        OutResult.Message =
            TEXT("Start and destination are the same.");

        return true;
    }

    TQueue<FString> PendingNodes;

    TSet<FString> VisitedNodes;

    TMap<FString, FString> PreviousNode;

    TMap<FString, float> CostToNode;

    PendingNodes.Enqueue(Request.StartNodeId);

    VisitedNodes.Add(Request.StartNodeId);

    CostToNode.Add(
        Request.StartNodeId,
        0.0f);

    while (!PendingNodes.IsEmpty())
    {
        FString CurrentNodeId;

        PendingNodes.Dequeue(CurrentNodeId);

        if (CurrentNodeId ==
            Request.DestinationNodeId)
        {
            break;
        }

        for (const FNavigationConnection& Connection :
            Connections)
        {
            FString NeighbourNodeId;

            bool bConnectionCanBeUsed = false;

            if (Connection.FromNodeId ==
                CurrentNodeId)
            {
                NeighbourNodeId =
                    Connection.ToNodeId;

                bConnectionCanBeUsed = true;
            }
            else if (
                Connection.bBidirectional &&
                Connection.ToNodeId ==
                CurrentNodeId)
            {
                NeighbourNodeId =
                    Connection.FromNodeId;

                bConnectionCanBeUsed = true;
            }

            if (!bConnectionCanBeUsed)
            {
                continue;
            }

            if (!Nodes.Contains(NeighbourNodeId))
            {
                continue;
            }

            const FNavigationNode& NeighbourNode =
                Nodes[NeighbourNodeId];

            if (!NeighbourNode.bWalkable)
            {
                continue;
            }

            if (VisitedNodes.Contains(
                NeighbourNodeId))
            {
                continue;
            }

            VisitedNodes.Add(NeighbourNodeId);

            PreviousNode.Add(
                NeighbourNodeId,
                CurrentNodeId);

            const float* CurrentCost =
                CostToNode.Find(CurrentNodeId);

            const float NewCost =
                (CurrentCost ? *CurrentCost : 0.0f) +
                FMath::Max(Connection.Cost, 0.0f);

            CostToNode.Add(
                NeighbourNodeId,
                NewCost);

            PendingNodes.Enqueue(
                NeighbourNodeId);
        }
    }

    if (!VisitedNodes.Contains(
        Request.DestinationNodeId))
    {
        OutResult.Status =
            ENavigationRouteStatus::NoRoute;

        OutResult.Message = TEXT(
            "No route exists between the selected nodes.");

        return false;
    }

    FString CurrentNodeId =
        Request.DestinationNodeId;

    while (true)
    {
        OutResult.NodeIds.Insert(
            CurrentNodeId,
            0);

        if (CurrentNodeId ==
            Request.StartNodeId)
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

            OutResult.Message = TEXT(
                "Route reconstruction failed.");

            return false;
        }

        CurrentNodeId = *PreviousId;
    }

    for (const FString& NodeId :
        OutResult.NodeIds)
    {
        const FNavigationNode* Node =
            Nodes.Find(NodeId);

        if (Node)
        {
            OutResult.Positions.Add(
                Node->Position);
        }
    }

    const float* TotalCost =
        CostToNode.Find(
            Request.DestinationNodeId);

    OutResult.TotalCost =
        TotalCost ? *TotalCost : 0.0f;

    OutResult.Status =
        ENavigationRouteStatus::Success;

    OutResult.Message = FString::Printf(
        TEXT("Route found with %d nodes."),
        OutResult.NodeIds.Num());

    return true;
}