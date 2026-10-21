#if WITH_DEV_AUTOMATION_TESTS

#include "FloorLayout.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "NavPlayerController.h"
#include "NavigationRoutePlanner.h"
#include "PolyBuilding.h"
#include "RoomTypes.h"
#include "UiTestFixture.h"

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Algo/Reverse.h"

// The layout of one floor (rooms along the outer wall, a core beside each door), by itself and put into real buildings.

namespace FloorLayoutTests
{
	TArray<FVector2D> Rectangle(double Width, double Height)
	{
		return { FVector2D(0, 0), FVector2D(Width, 0), FVector2D(Width, Height), FVector2D(0, Height) };
	}

	bool Inside(const FVector2D& P, const TArray<FVector2D>& Poly)
	{
		bool bInside = false;
		for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
		{
			if ((Poly[i].Y > P.Y) != (Poly[j].Y > P.Y) && P.X < (Poly[j].X - Poly[i].X) * (P.Y - Poly[i].Y) / (Poly[j].Y - Poly[i].Y) + Poly[i].X)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	// Whether two segments cross each other properly (not merely touch).
	bool SegmentsCross(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
	{
		auto Side = [](const FVector2D& P, const FVector2D& Q, const FVector2D& R) { return (Q.X - P.X) * (R.Y - P.Y) - (Q.Y - P.Y) * (R.X - P.X); };
		const double D1 = Side(A, B, C), D2 = Side(A, B, D), D3 = Side(C, D, A), D4 = Side(C, D, B);
		return ((D1 > 0) != (D2 > 0)) && ((D3 > 0) != (D4 > 0)) && D1 != 0 && D2 != 0 && D3 != 0 && D4 != 0;
	}

	// Two polygons (an L is fine) overlap when a side of one crosses a side of the other or one lies inside the other. Rooms are a
	// third of a metre apart, so touching does not come up.
	bool Overlap(const TArray<FVector2D>& A, const TArray<FVector2D>& B)
	{
		for (int32 i = 0; i < A.Num(); ++i)
		{
			for (int32 j = 0; j < B.Num(); ++j)
			{
				if (SegmentsCross(A[i], A[(i + 1) % A.Num()], B[j], B[(j + 1) % B.Num()])) return true;
			}
		}
		return Inside(A[0], B) || Inside(B[0], A);
	}

	FVector2D FootprintCentre(const TArray<FVector2D>& Footprint)
	{
		FVector2D Sum = FVector2D::ZeroVector;
		for (const FVector2D& P : Footprint) Sum += P;
		return Sum / Footprint.Num();
	}

	double DistanceToOutline(const FVector2D& P, const TArray<FVector2D>& Outline)
	{
		double Best = 1e30;
		for (int32 i = 0; i < Outline.Num(); ++i)
		{
			const FVector2D A = Outline[i], Edge = Outline[(i + 1) % Outline.Num()] - A;
			const double T = FMath::Clamp(FVector2D::DotProduct(P - A, Edge) / FMath::Max(Edge.SizeSquared(), 1e-9), 0.0, 1.0);
			Best = FMath::Min(Best, FVector2D::Distance(P, A + Edge * T));
		}
		return Best;
	}

	FFloorLayoutParams Params(const TArray<FVector2D>& Outline, int32 Seed = 1, double UnitsPerMetre = 10.0)
	{
		FFloorLayoutParams P;
		P.Seed = Seed;
		P.UnitsPerMetre = UnitsPerMetre;
		P.Outline = Outline;
		return P;
	}

	// A door in the middle of an edge.
	FFloorDoor DoorAt(const TArray<FVector2D>& Outline, int32 Edge, double Fraction, double UnitsPerMetre = 10.0)
	{
		FFloorDoor D;
		D.EdgeIndex = Edge;
		D.MiddleAlong = FVector2D::Distance(Outline[Edge], Outline[(Edge + 1) % Outline.Num()]) * Fraction;
		D.Width = 2.4 * UnitsPerMetre;
		return D;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorLayoutShapeTest, "InNavi.Interior.Floor.Layout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFloorLayoutShapeTest::RunTest(const FString& Parameters)
{
	using namespace FloorLayoutTests;
	const TArray<FVector2D> Outline = Rectangle(490, 200);
	FString Error;

	// A floor with no doors still gets one core.
	FFloorLayout NoDoors;
	if (!TestTrue(TEXT("a floor with no doors gets a layout"), FloorLayout::Generate(Params(Outline), NoDoors, Error))) return false;
	TestEqual(TEXT("with one core"), NoDoors.CoreCentres.Num(), 1);
	TestEqual(TEXT("of two lifts and a stair"), NoDoors.Connectors.Num(), 3);
	int32 Lifts = 0, Stairs = 0;
	for (const FFloorConnector& C : NoDoors.Connectors)
	{
		Lifts += C.Type == EInteriorNavNodeType::Elevator ? 1 : 0;
		Stairs += C.Type == EInteriorNavNodeType::Stairs ? 1 : 0;
	}
	TestTrue(TEXT("two lifts, one stair"), Lifts == 2 && Stairs == 1);
	int32 Toilets = 0;
	for (const FFloorRoom& Room : NoDoors.Rooms) Toilets += Room.Type == TEXT("Toilet") ? 1 : 0;
	TestEqual(TEXT("and one toilet beside it"), Toilets, 1);
	TestTrue(*FString::Printf(TEXT("rooms line the walls (%d)"), NoDoors.Rooms.Num()), NoDoors.Rooms.Num() >= 12);

	// Doors: one core each, apart from each other, and the way through the rooms to each door stays open.
	FFloorLayoutParams WithDoors = Params(Outline);
	WithDoors.Doors = { DoorAt(Outline, 0, 0.3), DoorAt(Outline, 2, 0.7), DoorAt(Outline, 3, 0.5) };
	FFloorLayout Three;
	TestTrue(TEXT("a floor with three doors gets a layout"), FloorLayout::Generate(WithDoors, Three, Error));
	TestEqual(TEXT("a core for each door"), Three.CoreCentres.Num(), 3);
	TestEqual(TEXT("each of three connectors"), Three.Connectors.Num(), 9);
	for (int32 i = 0; i < Three.CoreCentres.Num(); ++i)
	{
		for (int32 j = i + 1; j < Three.CoreCentres.Num(); ++j)
		{
			TestTrue(TEXT("cores are apart"), FVector2D::Distance(Three.CoreCentres[i], Three.CoreCentres[j]) >= 139.0);
		}
	}
	int32 ThreeToilets = 0;
	for (const FFloorRoom& Room : Three.Rooms) ThreeToilets += Room.Type == TEXT("Toilet") ? 1 : 0;
	TestTrue(*FString::Printf(TEXT("one toilet a core, never more than three (%d)"), ThreeToilets), ThreeToilets >= 1 && ThreeToilets <= 3);
	for (const FFloorDoor& Door : WithDoors.Doors)
	{
		const FVector2D A = Outline[Door.EdgeIndex], B = Outline[(Door.EdgeIndex + 1) % Outline.Num()];
		const FVector2D Direction = (B - A).GetSafeNormal();
		const FVector2D Inward(-Direction.Y, Direction.X); // counter-clockwise outline
		const FVector2D Middle = A + Direction * Door.MiddleAlong;
		for (double Depth : { 15.0, 30.0, 45.0 })
		{
			for (const FFloorRoom& Room : Three.Rooms)
			{
				TestFalse(TEXT("no room in the way of a door"), Inside(Middle + Inward * Depth, Room.Footprint));
			}
		}
	}

	// Rooms: inside, against the wall, apart, with a clear way out of each room's door, in kinds from the list.
	for (int32 i = 0; i < Three.Rooms.Num(); ++i)
	{
		const FFloorRoom& Room = Three.Rooms[i];
		TestTrue(TEXT("the type is on the list"), RoomTypes::All().Contains(Room.Type));
		double Nearest = 1e30;
		for (const FVector2D& Corner : Room.Footprint)
		{
			TestTrue(TEXT("every corner is inside the building"), Inside(Corner, Outline));
			Nearest = FMath::Min(Nearest, DistanceToOutline(Corner, Outline));
		}
		TestTrue(TEXT("every room is against an outer wall"), Nearest < 6.0);
		for (int32 j = i + 1; j < Three.Rooms.Num(); ++j)
		{
			TestFalse(TEXT("rooms never overlap"), Overlap(Room.Footprint, Three.Rooms[j].Footprint));
		}
		const FVector2D A = Room.Footprint[Room.DoorEdgeIndex], B = Room.Footprint[(Room.DoorEdgeIndex + 1) % Room.Footprint.Num()];
		const FVector2D Edge = (B - A).GetSafeNormal();
		const FVector2D Out(Edge.Y, -Edge.X); // footprints run counter-clockwise, so the room is to the left and out is to the right
		const FVector2D Middle = (A + B) * 0.5;
		const FVector2D Front = Middle + Out * 10.0;
		bool bBlocked = !Inside(Front, Outline);
		for (int32 j = 0; j < Three.Rooms.Num(); ++j) bBlocked = bBlocked || (j != i && Inside(Front, Three.Rooms[j].Footprint));
		TestFalse(TEXT("the room's door opens onto open floor"), bBlocked);
	}

	// Some of the wall is left out.
	double RoomArea = 0.0;
	for (const FFloorRoom& Room : Three.Rooms)
	{
		const TArray<FVector2D>& F = Room.Footprint;
		RoomArea += FVector2D::Distance(F[0], F[1]) * FVector2D::Distance(F[1], F[2]);
	}
	TestTrue(TEXT("the rooms do not fill the floor"), RoomArea < 490.0 * 200.0 * 0.6);

	// The same seed gives the same floor, another seed another.
	FFloorLayout Again, Other;
	FloorLayout::Generate(WithDoors, Again, Error);
	FFloorLayoutParams OtherSeed = WithDoors;
	OtherSeed.Seed = 2;
	FloorLayout::Generate(OtherSeed, Other, Error);
	bool bSame = Again.Rooms.Num() == Three.Rooms.Num(), bDifferent = Other.Rooms.Num() != Three.Rooms.Num();
	for (int32 i = 0; bSame && i < Three.Rooms.Num(); ++i) bSame = Again.Rooms[i].Footprint == Three.Rooms[i].Footprint && Again.Rooms[i].Type == Three.Rooms[i].Type;
	for (int32 i = 0; !bDifferent && i < FMath::Min(Other.Rooms.Num(), Three.Rooms.Num()); ++i) bDifferent = Other.Rooms[i].Footprint != Three.Rooms[i].Footprint;
	TestTrue(TEXT("the same seed gives the same floor"), bSame);
	TestTrue(TEXT("another seed another"), bDifferent);

	// Some rooms have a corner cut away (an L), with the door on the longer front; and rooms of one kind sit together.
	int32 LRooms = 0, Pairs = 0, SamePairs = 0;
	for (int32 Seed = 1; Seed <= 20; ++Seed)
	{
		FFloorLayoutParams Many = WithDoors;
		Many.Seed = Seed;
		FFloorLayout Layout;
		if (!FloorLayout::Generate(Many, Layout, Error)) continue;
		for (int32 i = 0; i < Layout.Rooms.Num(); ++i)
		{
			const FFloorRoom& Room = Layout.Rooms[i];
			if (Room.Footprint.Num() == 6)
			{
				++LRooms;
				const int32 Count = Room.Footprint.Num();
				TestTrue(TEXT("an L's door is in its recess, wide enough for a door"), FVector2D::Distance(Room.Footprint[Room.DoorEdgeIndex], Room.Footprint[(Room.DoorEdgeIndex + 1) % Count]) >= 17.0);
				for (const FVector2D& Corner : Room.Footprint) TestTrue(TEXT("every corner of an L is inside the building"), Inside(Corner, Outline));
				for (int32 j = i + 1; j < Layout.Rooms.Num(); ++j) TestFalse(TEXT("an L overlaps nothing"), Overlap(Room.Footprint, Layout.Rooms[j].Footprint));
			}
			if (i > 0 && Room.Type != TEXT("Toilet") && Layout.Rooms[i - 1].Type != TEXT("Toilet"))
			{
				++Pairs;
				SamePairs += Room.Type == Layout.Rooms[i - 1].Type ? 1 : 0;
			}
		}
	}
	TestTrue(*FString::Printf(TEXT("some rooms are L-shaped (%d)"), LRooms), LRooms >= 5);
	TestTrue(*FString::Printf(TEXT("neighbours are often the same kind (%d of %d)"), SamePairs, Pairs), Pairs > 0 && SamePairs * 100 >= Pairs * 40);

	// A big building gets fewer, larger rooms.
	FFloorLayout Big;
	TestTrue(TEXT("a campus-sized building gets a layout"), FloorLayout::Generate(Params(Rectangle(175, 64), 3, 1.0), Big, Error));
	TestTrue(TEXT("with no more rooms than the cap"), Big.Rooms.Num() <= 24);

	// Turned and notched buildings keep every room inside.
	TArray<FVector2D> Turned;
	for (const FVector2D& P : Outline) Turned.Add(P.GetRotated(33.0));
	TArray<FVector2D> LShape = { FVector2D(0, 0), FVector2D(300, 0), FVector2D(300, 120), FVector2D(160, 120), FVector2D(160, 260), FVector2D(0, 260) };
	TArray<FVector2D> Clockwise = Outline;
	Algo::Reverse(Clockwise);
	for (const TArray<FVector2D>* Shape : { &Turned, &LShape, &Clockwise })
	{
		FFloorLayout ShapeLayout;
		TestTrue(TEXT("a turned, L-shaped or clockwise building gets a layout"), FloorLayout::Generate(Params(*Shape, 5), ShapeLayout, Error));
		TestTrue(*FString::Printf(TEXT("with rooms (%d, %d corners)"), ShapeLayout.Rooms.Num(), Shape->Num()), ShapeLayout.Rooms.Num() > 5);
		for (const FFloorRoom& Room : ShapeLayout.Rooms)
		{
			for (const FVector2D& Corner : Room.Footprint) TestTrue(TEXT("every corner is inside the outline"), Inside(Corner, *Shape));
		}
	}

	FFloorLayout Tiny;
	TestFalse(TEXT("a closet-sized building is refused"), FloorLayout::Generate(Params(Rectangle(20, 20)), Tiny, Error));
	TestFalse(TEXT("with a reason"), Error.IsEmpty());
	return true;
}

// Draws each sample building's floor (and a few made-up outlines) to Saved/AutomationTemp/FloorPreview, to look at.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorLayoutPreviewTest, "InNavi.Interior.Floor.Preview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFloorLayoutPreviewTest::RunTest(const FString& Parameters)
{
	using namespace FloorLayoutTests;
	FNavigationSearchFixture Fixture;
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / TEXT("FloorPreview");

	auto Draw = [&](const FString& Name, const TArray<FVector2D>& Outline, double UnitsPerMetre)
	{
		FFloorLayoutParams LayoutParams = Params(Outline, 3, UnitsPerMetre);
		const int32 Edges = Outline.Num();
		LayoutParams.Doors = { DoorAt(Outline, 0, 0.5, UnitsPerMetre) };
		if (Name.StartsWith(TEXT("Rect")) || Name == TEXT("LShape")) LayoutParams.Doors.Add(DoorAt(Outline, Edges / 2, 0.5, UnitsPerMetre));
		FFloorLayout Layout;
		FString Error;
		const bool bOk = FloorLayout::Generate(LayoutParams, Layout, Error);
		auto Points = [](const TArray<FVector2D>& Poly)
		{
			FString Out;
			for (const FVector2D& P : Poly) Out += FString::Printf(TEXT("%.1f,%.1f "), P.X, P.Y);
			return Out;
		};
		FVector2D Min(1e9, 1e9);
		for (const FVector2D& P : Outline) Min = FVector2D(FMath::Min(Min.X, P.X), FMath::Min(Min.Y, P.Y));
		const FVector2D Shift = -Min;
		FString Svg = TEXT("<svg xmlns='http://www.w3.org/2000/svg' viewBox='-20 -20 560 460' width='560' height='460'><rect x='-20' y='-20' width='560' height='460' fill='white'/>");
		TArray<FVector2D> Shifted;
		for (const FVector2D& P : Outline) Shifted.Add(P + Shift);
		Svg += FString::Printf(TEXT("<polygon points='%s' fill='#f4f4f4' stroke='black' stroke-width='2'/>"), *Points(Shifted));
		for (const FFloorRoom& Room : Layout.Rooms)
		{
			TArray<FVector2D> Moved;
			for (const FVector2D& P : Room.Footprint) Moved.Add(P + Shift);
			const FColor Color = RoomTypes::Color(Room.Type).ToFColor(true);
			Svg += FString::Printf(TEXT("<polygon points='%s' fill='rgb(%d,%d,%d)' fill-opacity='0.6' stroke='#333' stroke-width='0.6'/>"), *Points(Moved), Color.R, Color.G, Color.B);
		}
		for (const FFloorConnector& Connector : Layout.Connectors)
		{
			const FVector2D C = Connector.Position + Shift;
			Svg += FString::Printf(TEXT("<rect x='%.1f' y='%.1f' width='9' height='9' fill='%s' stroke='black'/>"), C.X - 4.5, C.Y - 4.5, Connector.Type == EInteriorNavNodeType::Elevator ? TEXT("blue") : TEXT("navy"));
		}
		for (const FFloorDoor& Door : LayoutParams.Doors)
		{
			const FVector2D A = Outline[Door.EdgeIndex], B = Outline[(Door.EdgeIndex + 1) % Edges];
			const FVector2D P = A + (B - A).GetSafeNormal() * Door.MiddleAlong + Shift;
			Svg += FString::Printf(TEXT("<circle cx='%.1f' cy='%.1f' r='5' fill='green'/>"), P.X, P.Y);
		}
		Svg += TEXT("</svg>");
		FFileHelper::SaveStringToFile(Svg, *(Directory / (Name + TEXT(".svg"))));
		AddInfo(FString::Printf(TEXT("%s (%.1f units/m): %s %d rooms %s"), *Name, UnitsPerMetre, bOk ? TEXT("ok") : TEXT("refused"), Layout.Rooms.Num(), *Error));
	};

	for (const APolyBuilding* Building : Fixture.Generator->GetAllBuildings())
	{
		Draw(Building->BuildingId, Building->GetFootprint(), 100.0 / Fixture.Generator->GetScale());
	}
	Draw(TEXT("Rect49x20"), Rectangle(490, 200), 10.0);
	Draw(TEXT("Rect30x30"), Rectangle(300, 300), 10.0);
	Draw(TEXT("LShape"), { FVector2D(0, 0), FVector2D(300, 0), FVector2D(300, 120), FVector2D(160, 120), FVector2D(160, 260), FVector2D(0, 260) }, 10.0);
	return true;
}


// Putting a layout into a real floor: rooms with doors, a core of lifts and a stair (or the building's own, extended), one undo step.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorGenerateIntoBuildingTest, "InNavi.Interior.Floor.Generate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFloorGenerateIntoBuildingTest::RunTest(const FString& Parameters)
{
	using namespace FloorLayoutTests;
	FNavigationSearchFixture Fixture;
	AInteriorGenerator* Interior = Fixture.Interior;

	// E2 already has lifts and stairs: a new top floor gets rooms and those, and no second core.
	TSet<FString> GroupsBefore, GroupsAfter;
	for (const FInteriorNavNode& Node : Interior->GetNavNodes()) GroupsBefore.Add(Node.ConnectorGroupId);
	const int32 Level = Interior->AddFloor();
	Interior->SetActiveFloor(Level);
	FFloorGenerationResult Result;
	if (!TestTrue(*FString::Printf(TEXT("an empty floor can be generated (%s)"), *Result.Error), Interior->GenerateFloor(11, Result))) return false;
	TestTrue(*FString::Printf(TEXT("with rooms (%d, %d skipped)"), Result.RoomsCreated, Result.RoomsSkipped), Result.RoomsCreated >= 6);
	TestEqual(TEXT("each with a door"), Result.DoorsCreated, Result.RoomsCreated);
	TestEqual(TEXT("no new core, the building has one"), Result.ConnectorsPlaced, 0);
	TestTrue(TEXT("its lifts and stairs now reach the floor"), Result.ConnectorsReused >= 1);
	for (const FInteriorNavNode& Node : Interior->GetNavNodes()) GroupsAfter.Add(Node.ConnectorGroupId);
	TestTrue(TEXT("and no lift or stair is new"), GroupsAfter.Num() == GroupsBefore.Num());
	const TArray<AInteriorRoom*>& Rooms = Interior->GetRoomsOnFloor(Level);
	TestEqual(TEXT("the rooms are on the floor"), Rooms.Num(), Result.RoomsCreated);
	int32 Toilets = 0;
	for (const AInteriorRoom* Room : Rooms)
	{
		TestTrue(TEXT("tagged as generated"), Room->RoomTags.Contains(TEXT("generated")));
		TestTrue(TEXT("a type from the list"), RoomTypes::All().Contains(Room->RoomType));
		Toilets += Room->RoomType == TEXT("Toilet") ? 1 : 0;
		FInteriorOpening Door;
		bool bHasCorridorDoor = false;
		for (const FInteriorOpening& Opening : Room->GetOpenings())
		{
			bHasCorridorDoor = bHasCorridorDoor || Opening.ConnectsTo == TEXT("corridor");
		}
		TestTrue(TEXT("whose door leads to the corridor"), bHasCorridorDoor);
	}
	TestTrue(*FString::Printf(TEXT("one to three toilets (%d)"), Toilets), Toilets >= 1 && Toilets <= 3);

	// A floor that has rooms is left alone.
	FFloorGenerationResult Again;
	TestFalse(TEXT("a floor with rooms is refused"), Interior->GenerateFloor(12, Again));
	TestTrue(TEXT("saying why"), Again.Error.Contains(TEXT("already")));
	TestEqual(TEXT("and nothing changed"), Interior->GetRoomsOnFloor(Level).Num(), Result.RoomsCreated);

	// One undo takes the whole floor back.
	TestTrue(TEXT("undo works"), Interior->Undo());
	TestEqual(TEXT("and one step removes every room"), Interior->GetRoomsOnFloor(Level).Num(), 0);
	return true;
}

// A building with nothing in it gets a core of its own at its door.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorGenerateCoreTest, "InNavi.Interior.Floor.Core", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFloorGenerateCoreTest::RunTest(const FString& Parameters)
{
	using namespace FloorLayoutTests;
	FNavigationSearchFixture Fixture;
	AInteriorGenerator* Interior = Fixture.Interior;

	// E2 is the one with a wide floor; empty it of lifts and stairs by taking a building that has none loaded.
	FString BlankId;
	for (const APolyBuilding* Building : Fixture.Generator->GetAllBuildings())
	{
		Interior->LoadInteriorForBuilding(Building->BuildingId);
		if (Interior->GetNavNodes().IsEmpty() && Building->GetFootprint().Num() >= 4)
		{
			BlankId = Building->BuildingId;
			if (BlankId != TEXT("E2")) break;
		}
	}
	if (!TestFalse(TEXT("the example project has a building without lifts"), BlankId.IsEmpty())) return false;
	Interior->LoadInteriorForBuilding(BlankId);
	int32 Level = Interior->GetActiveFloor();
	if (!Interior->GetRoomsOnFloor(Level).IsEmpty() || Level < 1) Level = Interior->AddFloor();
	Interior->SetActiveFloor(Level);

	FFloorGenerationResult Result;
	if (!TestTrue(*FString::Printf(TEXT("%s: a floor with no doors can be generated (%s)"), *BlankId, *Result.Error), Interior->GenerateFloor(5, Result))) return false;
	TestEqual(TEXT("with a core of three"), Result.ConnectorsPlaced, 3);
	int32 Lifts = 0, Stairs = 0;
	for (const FInteriorNavNode& Node : Interior->GetNavNodes())
	{
		Lifts += Node.Type == EInteriorNavNodeType::Elevator ? 1 : 0;
		Stairs += Node.Type == EInteriorNavNodeType::Stairs ? 1 : 0;
	}
	TestTrue(TEXT("two lifts and a stair"), Lifts >= 2 && Stairs >= 1);
	TestTrue(TEXT("rooms around"), Result.RoomsCreated >= 1);

	// Another floor of the same building reuses that core.
	const int32 Upper = Interior->AddFloor();
	Interior->SetActiveFloor(Upper);
	FFloorGenerationResult Second;
	if (TestTrue(*FString::Printf(TEXT("the next floor can be generated (%s)"), *Second.Error), Interior->GenerateFloor(6, Second)))
	{
		TestEqual(TEXT("without a second core"), Second.ConnectorsPlaced, 0);
		TestTrue(TEXT("the lifts reach it"), Second.ConnectorsReused >= 1);
	}
	return true;
}


// The room colours in the 2D plan can be switched off; the button follows the controller.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomColoursToggleTest, "InNavi.Interior.RoomColours.Toggle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoomColoursToggleTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	TestTrue(TEXT("the colours start on"), Controller->AreRoomColoursVisible());
	Controller->ToggleRoomColours();
	TestFalse(TEXT("one click turns them off"), Controller->AreRoomColoursVisible());
	Controller->ToggleRoomColours();
	TestTrue(TEXT("and another back on"), Controller->AreRoomColoursVisible());

	// The floor slabs in 3D follow: the type's colour, grey when off, and the type's own colour after the type changes.
	Fixture.Interior->SetActiveFloor(1);
	const TArray<AInteriorRoom*>& Rooms = Fixture.Interior->GetRoomsOnFloor(1);
	AInteriorRoom* Room = nullptr;
	for (AInteriorRoom* Candidate : Rooms)
	{
		Room = Room ? Room : (Candidate && Candidate->IsClosedLoop() ? Candidate : nullptr);
	}
	if (!TestNotNull(TEXT("the ground floor has a room"), Room)) return false;
	Fixture.Interior->SetRoomType(Room->RoomId, TEXT("Lab"));
	TestTrue(TEXT("the floor takes its type's colour"), Room->GetFloorTint().Equals(RoomTypes::Color(TEXT("Lab")), 0.001f));
	Controller->ToggleRoomColours();
	TestFalse(TEXT("switching the colours off greys it"), Room->GetFloorTint().Equals(RoomTypes::Color(TEXT("Lab")), 0.001f));
	Fixture.Interior->SetRoomType(Room->RoomId, TEXT("Office"));
	TestFalse(TEXT("and a new type stays grey while they are off"), Room->GetFloorTint().Equals(RoomTypes::Color(TEXT("Office")), 0.001f));
	Controller->ToggleRoomColours();
	TestTrue(TEXT("switching them on again shows the new type"), Room->GetFloorTint().Equals(RoomTypes::Color(TEXT("Office")), 0.001f));
	Room->SetHighlighted(true);
	TestFalse(TEXT("a selected room shows the selection, not its type"), Room->GetFloorTint().Equals(RoomTypes::Color(TEXT("Office")), 0.001f));
	Room->SetHighlighted(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
