#include "NavigationTestActor.h"
#include "NavigationSystem.h"

ANavigationTestActor::ANavigationTestActor()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ANavigationTestActor::BeginPlay()
{
    Super::BeginPlay();

    RunNavigationTest();
}

void ANavigationTestActor::RunNavigationTest()
{
    FInNaviNavigationSystem NavigationSystem;

    auto AddNode =
        [&NavigationSystem](
            const TCHAR* Id,
            int32 Level,
            const FVector2D& Position)
        {
            FNavigationNode Node;

            Node.Id = Id;
            Node.Level = Level;
            Node.Position = Position;
            Node.bWalkable = true;

            NavigationSystem.AddNode(Node);
        };

    auto AddConnection =
        [&NavigationSystem](
            const TCHAR* From,
            const TCHAR* To,
            bool bCrossesFloor)
        {
            FNavigationConnection Connection;

            Connection.FromNodeId = From;
            Connection.ToNodeId = To;
            Connection.Cost = 1.0f;
            Connection.bCrossesFloor = bCrossesFloor;
            Connection.bBidirectional = true;

            NavigationSystem.AddConnection(Connection);
        };

    AddNode(
        TEXT("Room_F1"),
        1,
        FVector2D(0.0f, 0.0f));

    AddNode(
        TEXT("Door_F1"),
        1,
        FVector2D(100.0f, 0.0f));

    AddNode(
        TEXT("Elevator_F1"),
        1,
        FVector2D(200.0f, 0.0f));

    AddNode(
        TEXT("Elevator_F2"),
        2,
        FVector2D(200.0f, 0.0f));

    AddNode(
        TEXT("Door_F2"),
        2,
        FVector2D(300.0f, 0.0f));

    AddNode(
        TEXT("Room_F2"),
        2,
        FVector2D(400.0f, 0.0f));

    AddConnection(
        TEXT("Room_F1"),
        TEXT("Door_F1"),
        false);

    AddConnection(
        TEXT("Door_F1"),
        TEXT("Elevator_F1"),
        false);

    AddConnection(
        TEXT("Elevator_F1"),
        TEXT("Elevator_F2"),
        true);

    AddConnection(
        TEXT("Elevator_F2"),
        TEXT("Door_F2"),
        false);

    AddConnection(
        TEXT("Door_F2"),
        TEXT("Room_F2"),
        false);

    FNavigationRouteRequest Request;

    Request.StartNodeId = TEXT("Room_F1");
    Request.DestinationNodeId = TEXT("Room_F2");

    FNavigationRouteResult Result;

    const bool bRouteFound =
        NavigationSystem.FindRoute(
            Request,
            Result);

    if (!bRouteFound)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[T04 Test] Route failed: %s"),
            *Result.Message);

        return;
    }

    FString RouteText;

    for (int32 Index = 0;
        Index < Result.NodeIds.Num();
        Index++)
    {
        if (Index > 0)
        {
            RouteText += TEXT(" -> ");
        }

        RouteText += Result.NodeIds[Index];
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[T04 Test] Route found: %s"),
        *RouteText);

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[T04 Test] Total cost: %.2f"),
        Result.TotalCost);

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[T04 Test] Message: %s"),
        *Result.Message);
}