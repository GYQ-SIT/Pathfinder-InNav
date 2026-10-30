#if WITH_DEV_AUTOMATION_TESTS

#include "NavigationInstructions.h"
#include "Misc/AutomationTest.h"

namespace NavigationInstructionTests
{
	FNavigationRoutePoint Point(double X, double Y, int32 Floor = 1, const FString& Building = TEXT("A"))
	{
		FNavigationRoutePoint P; P.Position = { X, Y }; P.Floor = Floor; P.BuildingId = Building;
		P.WorldPosition = FVector(X, Y, FMath::Max(0, Floor - 1) * 300); P.bHasWorldPosition = true; return P;
	}

	TArray<FNavigationInstruction> Instructions(const TArray<FNavigationRoutePoint>& Points, int32 First = 0, int32 Last = INDEX_NONE)
	{
		return NavigationInstructions::Build(Points, [](const FNavigationRoutePoint& P) { return P.WorldPosition; },
			[](const FString& Id) { return TEXT("Building ") + Id; }, First, Last);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationSharedWalkingInstructionsTest, "InNavi.Navigation.Instructions.Walking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationSharedWalkingInstructionsTest::RunTest(const FString& Parameters)
{
	using namespace NavigationInstructionTests;
	TArray<FNavigationRoutePoint> Points = { Point(0, 0), Point(100, 0), Point(200, 0), Point(200, 0), Point(300, 0), Point(300, -200), Point(500, -200) };
	const auto Steps = Instructions(Points);
	if (!TestEqual(TEXT("collinear and duplicate vertices collapse, two bends remain"), Steps.Num(), 3)) return false;
	TestTrue(TEXT("initial straight covers the original vertices"), Steps[0].PointIndex == 0 && Steps[0].LastPointIndex == 4 && FMath::IsNearlyEqual(Steps[0].Metres, 3.0));
	TestTrue(TEXT("east then up the map is left"), Steps[1].Kind == ENavigationInstructionKind::TurnLeft && Steps[1].PointIndex == 4);
	TestTrue(TEXT("north then east is right"), Steps[2].Kind == ENavigationInstructionKind::TurnRight && Steps[2].PointIndex == 5);
	TestTrue(TEXT("directions contain walking distance"), Steps[1].Text.Contains(TEXT("2 m")));
	double Total = 0; for (const auto& Step : Steps) Total += Step.Metres;
	TestTrue(TEXT("grouping preserves complete physical distance"), FMath::IsNearlyEqual(Total, 7.0));
	const auto Back = Instructions({ Point(0, 0), Point(100, 0), Point(0, 0) });
	TestTrue(TEXT("a reversal is an explicit U-turn"), Back.Num() == 2 && Back[1].Kind == ENavigationInstructionKind::TurnAround);
	TestTrue(TEXT("an empty or invalid range has no instructions"), Instructions({}).IsEmpty() && Instructions(Points, 4, 1).IsEmpty());
	const auto Leg = Instructions(Points, 4, 6);
	TestTrue(TEXT("a trip leg starts fresh and retains global indices"), Leg.Num() == 2 && Leg[0].Kind == ENavigationInstructionKind::Straight && Leg[0].PointIndex == 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationSharedTransitionInstructionsTest, "InNavi.Navigation.Instructions.Transitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationSharedTransitionInstructionsTest::RunTest(const FString& Parameters)
{
	using namespace NavigationInstructionTests;
	TArray<FNavigationRoutePoint> Points = { Point(0, 0), Point(100, 0), Point(200, 0), Point(200, 0, 2), Point(200, 0, 3), Point(400, 0, 3) };
	Points[1].bIsDoorway = true;
	for (int32 I = 2; I <= 4; ++I) { Points[I].ConnectorType = EInteriorNavNodeType::Elevator; Points[I].ConnectorId = TEXT("Lift"); }
	const auto Steps = Instructions(Points);
	if (!TestEqual(TEXT("a door, one lift ride and walks are separate instructions"), Steps.Num(), 4)) return false;
	TestTrue(TEXT("door is announced"), Steps[1].Kind == ENavigationInstructionKind::PassDoor && Steps[1].Text.Contains(TEXT("door")));
	TestTrue(TEXT("one lift ride covers floors 1 to 3 and six metres"), Steps[2].Kind == ENavigationInstructionKind::TakeLift
		&& Steps[2].LastPointIndex == 4 && Steps[2].Text.Contains(TEXT("Floor 1 to Floor 3")) && FMath::IsNearlyEqual(Steps[2].Metres, 6.0));
	TestTrue(TEXT("walking after the lift starts without a fabricated turn"), Steps[3].Kind == ENavigationInstructionKind::Straight);
	const auto Unknown = Instructions({ Point(0, 0), Point(0, 0, 2) });
	TestTrue(TEXT("imported floor changes without connector metadata do not invent an escalator"), Unknown.Num() == 1 && Unknown[0].Kind == ENavigationInstructionKind::ChangeFloor && Unknown[0].Text.Contains(TEXT("floor connection")));
	const auto Link = Instructions({ Point(0, 0), Point(50, 0, 1, TEXT("B")), Point(50, 200, 1, TEXT("B")) });
	TestTrue(TEXT("link announces leaving, entering and walking"), Link.Num() == 3 && Link[0].Kind == ENavigationInstructionKind::ExitBuilding
		&& Link[1].Kind == ENavigationInstructionKind::EnterBuilding && Link[2].Kind == ENavigationInstructionKind::Straight);
	const auto Outdoor = Instructions({ Point(0, 0, 0), Point(100, 0, 0, TEXT("B")), Point(100, 0, 1, TEXT("B")), Point(100, 200, 1, TEXT("B")) });
	TestTrue(TEXT("an outdoor coordinate-owner change does not become a link door"), Outdoor.Num() == 3
		&& Outdoor[0].Kind == ENavigationInstructionKind::Straight && Outdoor[0].Text.Contains(TEXT("outdoor"))
		&& Outdoor[1].Kind == ENavigationInstructionKind::EnterBuilding && Outdoor[2].Kind == ENavigationInstructionKind::Straight);
	return true;
}

#endif
