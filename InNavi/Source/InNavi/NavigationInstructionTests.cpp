#if WITH_DEV_AUTOMATION_TESTS

#include "NavigationInstructions.h"
#include "InteriorEntrance.h"
#include "LayoutValidation.h"
#include "PolyBuilding.h"
#include "UiTestFixture.h"
#include "Misc/AutomationTest.h"

namespace NavigationInstructionTests
{
	FNavigationRoutePoint Point(double X, double Y, int32 Floor = 1, const FString& Building = TEXT("A"))
	{
		FNavigationRoutePoint P; P.Position = { X, Y }; P.Floor = Floor; P.BuildingId = Building;
		P.WorldPosition = FVector(X, Y, LayoutValidation::LevelIndex(Floor) * 300); P.bHasWorldPosition = true; return P;
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

// T3 (PAT-171): more tests for NavigationInstructions, which the navigation panel and the phone share. They use tables of headings, route fixtures
// built the way the planner builds them, a property that has to hold for any route (the instructions cover every point once, and their distances
// add up to the walked distance), and a real route found by the planner in a small campus.

namespace NavigationInstructionTests
{
	// A point on the floor with its connector, as the planner makes one.
	FNavigationRoutePoint OnConnector(double X, double Y, int32 Floor, EInteriorNavNodeType Type, const FString& Id, const FString& Building = TEXT("A"))
	{
		FNavigationRoutePoint P = Point(X, Y, Floor, Building);
		P.ConnectorType = Type; P.ConnectorId = Id;
		return P;
	}

	TArray<FNavigationInstruction> Walk(const TArray<FVector2D>& Path)
	{
		TArray<FNavigationRoutePoint> Points;
		for (const FVector2D& P : Path) Points.Add(Point(P.X, P.Y));
		return Instructions(Points);
	}

	double PathLength(const TArray<FNavigationRoutePoint>& Points)
	{
		double Length = 0;
		for (int32 I = 1; I < Points.Num(); ++I) Length += FVector::Distance(Points[I - 1].WorldPosition, Points[I].WorldPosition) / 100.0;
		return Length;
	}
}

// How sharp a bend has to be before it is a turn, and which way: a table of headings. The path goes east for 10 m and then bends by an angle; the
// map draws y downwards, so a bend towards smaller y is to the left.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationInstructionTurnTableTest, "InNavi.Navigation.Instructions.TurnTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationInstructionTurnTableTest::RunTest(const FString& Parameters)
{
	using namespace NavigationInstructionTests;
	struct FCase { double BendDegrees; ENavigationInstructionKind Expected; const TCHAR* Name; };
	// Positive bends turn towards smaller y: left. 24 degrees is a bend that is not announced (the dot product stays above 0.90), 26 is.
	const FCase Cases[] = {
		{ 0, ENavigationInstructionKind::Straight, TEXT("straight on") },
		{ 24, ENavigationInstructionKind::Straight, TEXT("a gentle 24 degree bend is not announced") },
		{ 26, ENavigationInstructionKind::TurnLeft, TEXT("26 degrees to the left is a turn") },
		{ -26, ENavigationInstructionKind::TurnRight, TEXT("26 degrees to the right is a turn") },
		{ 90, ENavigationInstructionKind::TurnLeft, TEXT("a right angle to the left") },
		{ -90, ENavigationInstructionKind::TurnRight, TEXT("a right angle to the right") },
		{ 150, ENavigationInstructionKind::TurnLeft, TEXT("150 degrees is still a turn, not a U-turn") },
		{ -150, ENavigationInstructionKind::TurnRight, TEXT("and so is -150") },
		{ 165, ENavigationInstructionKind::TurnAround, TEXT("165 degrees is turning around") },
		{ 180, ENavigationInstructionKind::TurnAround, TEXT("walking back is turning around") },
	};
	for (const FCase& Case : Cases)
	{
		const double Radians = FMath::DegreesToRadians(Case.BendDegrees);
		// y points down on the map, so a bend to the left goes to smaller y.
		const FVector2D Second(1000.0 + 1000.0 * FMath::Cos(Radians), -1000.0 * FMath::Sin(Radians));
		const TArray<FNavigationInstruction> Steps = Walk({ FVector2D(0, 0), FVector2D(1000, 0), Second });
		if (!TestTrue(*FString::Printf(TEXT("%s: there are instructions"), Case.Name), !Steps.IsEmpty())) continue;
		TestTrue(*FString::Printf(TEXT("%s: the instruction for the second leg is the kind expected"), Case.Name), Steps.Last().Kind == Case.Expected);
		if (Case.Expected == ENavigationInstructionKind::Straight)
		{
			TestEqual(*FString::Printf(TEXT("%s: a bend that is not announced makes one walk of both legs"), Case.Name), Steps.Num(), 1);
		}
	}
	TestTrue(TEXT("IsLeftTurn follows the map: going east and then up the map (smaller y) is left"), NavigationInstructions::IsLeftTurn(FVector2D(1, 0), FVector2D(0, -1)));
	TestFalse(TEXT("and going east and then down is right"), NavigationInstructions::IsLeftTurn(FVector2D(1, 0), FVector2D(0, 1)));
	return true;
}

// The distance in the words: under half a metre is "less than 1 m", otherwise whole metres, never "0 m".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationInstructionDistanceTextTest, "InNavi.Navigation.Instructions.DistanceText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationInstructionDistanceTextTest::RunTest(const FString& Parameters)
{
	using namespace NavigationInstructionTests;
	struct FCase { double Centimetres; const TCHAR* Expected; };
	const FCase Cases[] = { { 10, TEXT("less than 1 m") }, { 49, TEXT("less than 1 m") }, { 50, TEXT("for 1 m.") }, { 140, TEXT("for 1 m.") },
		{ 150, TEXT("for 2 m.") }, { 1260, TEXT("for 13 m.") }, { 100000, TEXT("for 1000 m.") } };
	for (const FCase& Case : Cases)
	{
		const TArray<FNavigationInstruction> Steps = Walk({ FVector2D(0, 0), FVector2D(Case.Centimetres, 0) });
		if (!TestEqual(*FString::Printf(TEXT("%g cm is one walk"), Case.Centimetres), Steps.Num(), 1)) continue;
		TestTrue(*FString::Printf(TEXT("%g cm is said as %s (%s)"), Case.Centimetres, Case.Expected, *Steps[0].Text), Steps[0].Text.Contains(Case.Expected));
		TestFalse(*FString::Printf(TEXT("%g cm never says 0 m"), Case.Centimetres), Steps[0].Text.Contains(TEXT(" 0 m")));
	}
	return true;
}

// Rides: one ride over several vertices of the same connector is one instruction; a different connector, a change of direction or a change of
// kind starts another. Floors below the ground are named B1.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationInstructionRidesTest, "InNavi.Navigation.Instructions.Rides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationInstructionRidesTest::RunTest(const FString& Parameters)
{
	using namespace NavigationInstructionTests;
	const auto Lift = EInteriorNavNodeType::Elevator;
	const auto Stairs = EInteriorNavNodeType::Stairs;

	// Up through three vertices of one lift: one instruction, from floor 1 to floor 3.
	auto Up = Instructions({ OnConnector(0, 0, 1, Lift, TEXT("L1")), OnConnector(0, 0, 2, Lift, TEXT("L1")), OnConnector(0, 0, 3, Lift, TEXT("L1")) });
	TestTrue(TEXT("one ride up two floors"), Up.Num() == 1 && Up[0].Kind == ENavigationInstructionKind::TakeLift && Up[0].Text.Contains(TEXT("Floor 1 to Floor 3")));

	// Two different lifts one after the other (a change of lift): two instructions.
	auto Change = Instructions({ OnConnector(0, 0, 1, Lift, TEXT("L1")), OnConnector(0, 0, 2, Lift, TEXT("L1")), OnConnector(0, 0, 2, Lift, TEXT("L2")), OnConnector(0, 0, 3, Lift, TEXT("L2")) });
	int32 Rides = 0;
	for (const FNavigationInstruction& Step : Change) Rides += Step.Kind == ENavigationInstructionKind::TakeLift ? 1 : 0;
	TestEqual(TEXT("a change of lift is two rides"), Rides, 2);

	// Up and then straight down on the same lift is two rides, not one ride from 1 to 1.
	auto UpDown = Instructions({ OnConnector(0, 0, 1, Lift, TEXT("L1")), OnConnector(0, 0, 2, Lift, TEXT("L1")), OnConnector(0, 0, 1, Lift, TEXT("L1")) });
	Rides = 0;
	for (const FNavigationInstruction& Step : UpDown) Rides += Step.Kind == ENavigationInstructionKind::TakeLift ? 1 : 0;
	TestEqual(TEXT("up and back down is two rides"), Rides, 2);

	// A lift and then stairs: two kinds, two instructions, each with its own word.
	auto Mixed = Instructions({ OnConnector(0, 0, 1, Lift, TEXT("L1")), OnConnector(0, 0, 2, Lift, TEXT("L1")), OnConnector(300, 0, 2, Stairs, TEXT("S1")), OnConnector(300, 0, 3, Stairs, TEXT("S1")) });
	TestTrue(TEXT("a lift and then stairs says both"), Mixed.ContainsByPredicate([](const FNavigationInstruction& S) { return S.Kind == ENavigationInstructionKind::TakeLift && S.Text.Contains(TEXT("lift")); })
		&& Mixed.ContainsByPredicate([](const FNavigationInstruction& S) { return S.Kind == ENavigationInstructionKind::TakeStairs && S.Text.Contains(TEXT("stairs")); }));

	// The floor under the ground floor is B1, in words and in height: 3 m down.
	auto Down = Instructions({ OnConnector(0, 0, 1, Lift, TEXT("L1")), OnConnector(0, 0, -1, Lift, TEXT("L1")) });
	if (TestEqual(TEXT("one ride down to B1"), Down.Num(), 1))
	{
		TestTrue(TEXT("it says Floor 1 to Floor B1"), Down[0].Text.Contains(TEXT("Floor 1 to Floor B1")));
		TestTrue(TEXT("and it is 3 m, one floor, not 6"), FMath::IsNearlyEqual(Down[0].Metres, 3.0, 0.01));
	}
	auto FromBasement = Instructions({ OnConnector(0, 0, -2, Lift, TEXT("L1")), OnConnector(0, 0, -1, Lift, TEXT("L1")), OnConnector(0, 0, 1, Lift, TEXT("L1")), OnConnector(0, 0, 2, Lift, TEXT("L1")) });
	TestTrue(TEXT("B2 to 2 is one ride of three floor heights (9 m) that names both ends"), FromBasement.Num() == 1 && FromBasement[0].Text.Contains(TEXT("Floor B2 to Floor 2")) && FMath::IsNearlyEqual(FromBasement[0].Metres, 9.0, 0.01));

	// A link door on a basement floor, and entering a building from outside onto B1, say B1 too.
	auto Link = Instructions({ Point(0, 0, -1, TEXT("A")), Point(50, 0, -1, TEXT("B")) });
	TestTrue(TEXT("a link door on B1 says B1"), Link.Num() == 2 && Link[0].Text.Contains(TEXT("Floor B1")) && Link[1].Text.Contains(TEXT("Floor B1")));
	return true;
}

// For any route, the instructions cover the route points once, one after the other and with nothing left out, and the distances they carry add up
// to the length of the route. Tried on routes made from a seed: walks with bends, doorways, rides and a building change.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationInstructionCoverageTest, "InNavi.Navigation.Instructions.Coverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationInstructionCoverageTest::RunTest(const FString& Parameters)
{
	using namespace NavigationInstructionTests;
	for (int32 Seed = 1; Seed <= 40; ++Seed)
	{
		FRandomStream Random(Seed);
		TArray<FNavigationRoutePoint> Points;
		int32 Floor = Random.RandRange(-1, 3) == 0 ? 1 : Random.RandRange(-1, 3);
		if (Floor == 0) Floor = 1;
		FString Building = TEXT("A");
		FVector2D At(0, 0);
		Points.Add(Point(At.X, At.Y, Floor, Building));
		const int32 Length = Random.RandRange(3, 14);
		for (int32 I = 0; I < Length; ++I)
		{
			const int32 Kind = Random.RandRange(0, 9);
			if (Kind <= 5)
			{
				At += FVector2D(Random.FRandRange(-600.f, 600.f), Random.FRandRange(-600.f, 600.f));
				Points.Add(Point(At.X, At.Y, Floor, Building));
				if (Random.RandRange(0, 4) == 0) Points.Last().bIsDoorway = true;
			}
			else if (Kind <= 8)
			{
				const int32 Next = LayoutValidation::LevelAfter(Floor, Random.RandRange(0, 1) ? 1 : -1);
				Points.Add(OnConnector(At.X, At.Y, Floor, EInteriorNavNodeType::Elevator, TEXT("L")));
				Floor = Next;
				Points.Add(OnConnector(At.X, At.Y, Floor, EInteriorNavNodeType::Elevator, TEXT("L")));
			}
			else
			{
				Building = Building == TEXT("A") ? TEXT("B") : TEXT("A");
				Points.Add(Point(At.X, At.Y, Floor, Building));
			}
		}
		const TArray<FNavigationInstruction> Steps = Instructions(Points);
		if (Steps.IsEmpty())
		{
			TestTrue(*FString::Printf(TEXT("seed %d: no instructions only when nothing was walked"), Seed), PathLength(Points) < 0.01);
			continue;
		}
		bool bOrdered = true, bInRange = true;
		double Total = 0;
		for (int32 I = 0; I < Steps.Num(); ++I)
		{
			bInRange &= Points.IsValidIndex(Steps[I].PointIndex) && Points.IsValidIndex(Steps[I].LastPointIndex) && Steps[I].PointIndex <= Steps[I].LastPointIndex;
			if (I > 0) bOrdered &= Steps[I].PointIndex >= Steps[I - 1].PointIndex && Steps[I].PointIndex <= Steps[I - 1].LastPointIndex + 1;
			Total += Steps[I].Metres;
			bInRange &= !Steps[I].Text.IsEmpty();
		}
		TestTrue(*FString::Printf(TEXT("seed %d: every instruction has words and points inside the route"), Seed), bInRange);
		TestTrue(*FString::Printf(TEXT("seed %d: the instructions follow each other along the route"), Seed), bOrdered);
		TestTrue(*FString::Printf(TEXT("seed %d: the distances add up to the length of the route (%.2f of %.2f m)"), Seed, Total, PathLength(Points)),
			FMath::IsNearlyEqual(Total, PathLength(Points), 0.05));
	}
	return true;
}

// The module through the planner: a route found in a small campus (a lift down to B1, then a link door to the next building) read as
// instructions, with the names of the buildings coming from the callback.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationInstructionPlannerRouteTest, "InNavi.Navigation.Instructions.PlannerRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNavigationInstructionPlannerRouteTest::RunTest(const FString& Parameters)
{
	using namespace NavigationInstructionTests;
	FNavigationSearchFixture Fixture;
	Fixture.Interior->ClearLoadedInterior();
	Fixture.Generator->ClearAllBuildings();
	const TArray<FVector2D> Shape = {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
	// Building A has B1 and the ground floor with a lift; building B is next to it (east) with a link door on B1.
	Fixture.Generator->SpawnBuildingFromData(TEXT("I-A"), TEXT("Alpha"), Shape, 2, 300, {0, 0}, -1);
	Fixture.Generator->SpawnBuildingFromData(TEXT("I-B"), TEXT("Beta"), Shape, 1, 300, {1000, 0}, -1);

	auto FloorData = [](int32 Level, bool bLift)
	{
		auto Floor = MakeShared<FJsonObject>();
		Floor->SetNumberField(TEXT("level"), Level);
		Floor->SetNumberField(TEXT("wallThickness"), 20);
		Floor->SetArrayField(TEXT("rooms"), {});
		TArray<TSharedPtr<FJsonValue>> Nodes;
		if (bLift)
		{
			auto Node = MakeShared<FJsonObject>();
			Node->SetStringField(TEXT("id"), FString::Printf(TEXT("Lift-%d"), Level));
			Node->SetStringField(TEXT("type"), TEXT("elevator"));
			Node->SetStringField(TEXT("connectorGroupId"), TEXT("Lift"));
			Node->SetArrayField(TEXT("position"), { MakeShared<FJsonValueNumber>(500), MakeShared<FJsonValueNumber>(500) });
			Node->SetArrayField(TEXT("connectsFloors"), { MakeShared<FJsonValueNumber>(-1), MakeShared<FJsonValueNumber>(1) });
			Nodes.Add(MakeShared<FJsonValueObject>(Node));
		}
		Floor->SetArrayField(TEXT("navNodes"), Nodes);
		return MakeShared<FJsonValueObject>(Floor);
	};
	FInteriorEntrance Link;
	Link.Id = TEXT("I-A-LINK"); Link.Kind = EInteriorEntranceKind::Link; Link.Level = -1; Link.EdgeIndex = 1; Link.Offset = 875; Link.Width = 100; Link.ConnectsTo = TEXT("I-B");
	auto DataA = MakeShared<FJsonObject>();
	DataA->SetArrayField(TEXT("floors"), { FloorData(-1, true), FloorData(1, true) });
	DataA->SetArrayField(TEXT("entrances"), { MakeShared<FJsonValueObject>(InteriorEntrances::ToJson(Link)) });
	auto DataB = MakeShared<FJsonObject>();
	DataB->SetArrayField(TEXT("floors"), { FloorData(-1, false) });
	DataB->SetArrayField(TEXT("entrances"), {});
	Fixture.Generator->SetInteriorDataForBuilding(TEXT("I-A"), DataA);
	Fixture.Generator->SetInteriorDataForBuilding(TEXT("I-B"), DataB);
	Fixture.Interior->LoadInteriorForBuilding(TEXT("I-A"));

	FNavigationRoutePoint From, To;
	From.BuildingId = TEXT("I-A"); From.Floor = 1; From.Position = {100, 100};
	To.BuildingId = TEXT("I-B"); To.Floor = -1; To.Position = {100, 900};
	FNavigationRouteResult Route;
	if (!TestTrue(TEXT("a route from A's ground floor to B's B1"), FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior, From, To, Route))) return false;

	TArray<FString> NamesAsked;
	const TArray<FNavigationInstruction> Steps = NavigationInstructions::Build(Route.Points, [](const FNavigationRoutePoint& P) { return P.WorldPosition; },
		[&NamesAsked](const FString& Id) { NamesAsked.Add(Id); return Id == TEXT("I-A") ? FString(TEXT("Alpha")) : Id == TEXT("I-B") ? FString(TEXT("Beta")) : Id; });
	auto Find = [&Steps](ENavigationInstructionKind Kind) -> const FNavigationInstruction* { return Steps.FindByPredicate([Kind](const FNavigationInstruction& S) { return S.Kind == Kind; }); };

	const FNavigationInstruction* Ride = Find(ENavigationInstructionKind::TakeLift);
	if (TestNotNull(TEXT("the route takes the lift"), Ride))
	{
		TestTrue(TEXT("down from floor 1 to B1 in Alpha"), Ride->Text.Contains(TEXT("Floor 1 to Floor B1")) && Ride->Text.Contains(TEXT("Alpha")));
	}
	const FNavigationInstruction* Leave = Find(ENavigationInstructionKind::ExitBuilding);
	const FNavigationInstruction* Enter = Find(ENavigationInstructionKind::EnterBuilding);
	if (TestNotNull(TEXT("it leaves A by the link door"), Leave) && TestNotNull(TEXT("and enters B"), Enter))
	{
		TestTrue(TEXT("naming both buildings and B1"), Leave->Text.Contains(TEXT("Alpha")) && Leave->Text.Contains(TEXT("Beta")) && Leave->Text.Contains(TEXT("B1")));
		TestTrue(TEXT("the lift comes before the door"), Ride && Ride->PointIndex < Leave->PointIndex);
		TestTrue(TEXT("and the door before the walk in B"), Enter->PointIndex > Leave->PointIndex);
	}
	TestTrue(TEXT("the building names were asked through the callback"), NamesAsked.Contains(TEXT("I-A")) && NamesAsked.Contains(TEXT("I-B")));
	double Total = 0;
	for (const FNavigationInstruction& Step : Steps) Total += Step.Metres;
	TestTrue(TEXT("the distances add up to the length of the route"), FMath::IsNearlyEqual(Total, PathLength(Route.Points), 0.05));
	return true;
}

#endif
