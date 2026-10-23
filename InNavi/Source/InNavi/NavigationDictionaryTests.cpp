#include "NavigationDictionary.h"
#include "NavigationSystem.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationDictionarySearchTest,
    "InNavi.Navigation.Dictionary.Search",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavigationDictionarySearchTest::RunTest(const FString& Parameters)
{
    FNavigationDestinationEntry Restroom;
    Restroom.RoomId = TEXT("B1-02-03");
    Restroom.Name = TEXT("Restroom A");
    Restroom.Category = TEXT("Restroom");
    Restroom.Aliases = { TEXT("Toilet"), TEXT("Bathroom"), TEXT("WC") };
    Restroom.FloorLevel = 2;

    FNavigationDestinationEntry Office;
    Office.RoomId = TEXT("B1-01-01");
    Office.Name = TEXT("Main Office");
    Office.Category = TEXT("Office");
    Office.FloorLevel = 1;

    FNavigationDictionary Dictionary;
    Dictionary.SetEntries({ Restroom, Office });
    TestEqual(TEXT("All destinations"), Dictionary.Search(TEXT("")).Num(), 2);
    TestEqual(TEXT("Case-insensitive alias"), Dictionary.Search(TEXT("toILEt")).Num(), 1);
    TestEqual(TEXT("Multi-word search"), Dictionary.Search(TEXT("restroom a")).Num(), 1);
    TestEqual(TEXT("Category search"), Dictionary.Search(TEXT("office")).Num(), 1);
    TestEqual(TEXT("Floor filter"), Dictionary.Search(TEXT(""), 2).Num(), 1);
    TestEqual(TEXT("No matches"), Dictionary.Search(TEXT("cafeteria")).Num(), 0);

    FNavigationDestinationEntry Result;
    TestTrue(TEXT("Lookup by room ID"), Dictionary.FindByRoomId(TEXT("B1-02-03"), Result));
    TestEqual(TEXT("Correct floor"), Result.FloorLevel, 2);
    TestFalse(TEXT("Missing room ID"), Dictionary.FindByRoomId(TEXT("missing"), Result));

    FInNaviNavigationSystem Navigation;
    FString NodeId;
    TestFalse(TEXT("No graph node yet"), Dictionary.ResolveDirectGraphNode(Restroom.RoomId, Navigation, NodeId));
    FNavigationNode Node;
    Node.Id = Restroom.RoomId;
    Node.Level = 2;
    Node.bWalkable = true;
    Navigation.AddNode(Node);
    TestTrue(TEXT("Direct graph node mapping"), Dictionary.ResolveDirectGraphNode(Restroom.RoomId, Navigation, NodeId));
    TestEqual(TEXT("Mapped node ID"), NodeId, Restroom.RoomId);
    return true;
}
#endif
