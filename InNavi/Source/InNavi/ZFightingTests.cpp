#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "InteriorConnectorMarker.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "PolyBuilding.h"
#include "UiTestFixture.h"

#include "Misc/AutomationTest.h"

// Z-fighting: faces in the very same plane flicker. Walls that lie on the same line (a room on the building's outer wall, rooms that share a wall) get
// a level each that differs from their neighbours', and the top of a stair or a lift stays under the slab above.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZFightingWallsTest, "InNavi.Geometry.ZFightingWalls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FZFightingWallsTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F;
	F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
	APolyBuilding* Building = F.Generator->CreateRectangleBuilding(TEXT("ZFW"), TEXT("ZFW"), {500,500}, 1000, 1000, 0, 2, 300);
	if (!TestNotNull(TEXT("a building"), Building)) return false;
	F.Interior->LoadInteriorForBuilding(TEXT("ZFW"));
	const FBox2D Outline(Building->GetFootprint());
	FString Error;

	// A room on the building's outer wall, one that shares a wall with it, a third beside that, and one far away.
	AInteriorRoom* OnOuterWall = F.Interior->CreateRoomFromFootprint({{Outline.Min.X, Outline.Min.Y}, {Outline.Min.X + 200, Outline.Min.Y}, {Outline.Min.X + 200, Outline.Min.Y + 200}, {Outline.Min.X, Outline.Min.Y + 200}},
		TEXT("A"), TEXT("A"), true, false, false, Error);
	AInteriorRoom* Beside = F.Interior->CreateRoomFromFootprint({{Outline.Min.X + 200, Outline.Min.Y}, {Outline.Min.X + 400, Outline.Min.Y}, {Outline.Min.X + 400, Outline.Min.Y + 200}, {Outline.Min.X + 200, Outline.Min.Y + 200}},
		TEXT("B"), TEXT("B"), true, false, false, Error);
	AInteriorRoom* Next = F.Interior->CreateRoomFromFootprint({{Outline.Min.X + 400, Outline.Min.Y}, {Outline.Min.X + 600, Outline.Min.Y}, {Outline.Min.X + 600, Outline.Min.Y + 200}, {Outline.Min.X + 400, Outline.Min.Y + 200}},
		TEXT("C"), TEXT("C"), true, false, false, Error);
	if (!TestNotNull(TEXT("room A"), OnOuterWall) || !TestNotNull(TEXT("room B"), Beside) || !TestNotNull(TEXT("room C"), Next)) return false;

	TestTrue(TEXT("a room is never at level 0, which the building's outer wall keeps"), OnOuterWall->GetWallBiasLevel() >= 1 && Beside->GetWallBiasLevel() >= 1 && Next->GetWallBiasLevel() >= 1);
	TestNotEqual(TEXT("rooms that share a wall get different levels (A, B)"), OnOuterWall->GetWallBiasLevel(), Beside->GetWallBiasLevel());
	TestNotEqual(TEXT("(B, C)"), Beside->GetWallBiasLevel(), Next->GetWallBiasLevel());
	TestTrue(TEXT("levels stay within the steps there are"), Next->GetWallBiasLevel() <= AInteriorRoom::WallBiasLevels);

	// The walls really are a little bigger, by that room's own step.
	const FBox Box = OnOuterWall->GetComponentsBoundingBox(true);
	const float Expected = 280.f + AInteriorRoom::WallBiasHeightStep * OnOuterWall->GetWallBiasLevel();
	TestTrue(TEXT("the walls are taller by the room's step"), FMath::IsNearlyEqual(static_cast<float>(Box.Max.Z), Expected, 0.5f));

	// Rebuilding does not change the level of a room whose level is still free.
	const int32 Before = Beside->GetWallBiasLevel();
	Beside->SetFloorBaseZ(0.f);
	TestEqual(TEXT("a rebuilt room keeps its level"), Beside->GetWallBiasLevel(), Before);

	// Rooms that are not next to each other may share a level; rooms on another floor never count.
	AInteriorRoom* Far = F.Interior->CreateRoomFromFootprint({{Outline.Max.X - 300, Outline.Max.Y - 300}, {Outline.Max.X - 100, Outline.Max.Y - 300}, {Outline.Max.X - 100, Outline.Max.Y - 100}, {Outline.Max.X - 300, Outline.Max.Y - 100}},
		TEXT("F"), TEXT("F"), true, false, false, Error);
	TestNotNull(TEXT("a room far away"), Far);
	TestTrue(TEXT("starts at the first free level"), Far && Far->GetWallBiasLevel() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZFightingConnectorsTest, "InNavi.Geometry.ZFightingConnectors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FZFightingConnectorsTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F;
	F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
	APolyBuilding* Building = F.Generator->CreateRectangleBuilding(TEXT("ZFC"), TEXT("ZFC"), {500,500}, 1000, 1000, 0, 3, 300);
	if (!TestNotNull(TEXT("a building"), Building)) return false;
	F.Interior->LoadInteriorForBuilding(TEXT("ZFC"));
	const FVector2D Middle = FBox2D(Building->GetFootprint()).GetCenter();
	F.Interior->ConsumeConnectorFailure();
	const FString Stairs = F.Interior->PlaceConnectorNode(EInteriorNavNodeType::Stairs, Middle, { 1, 2 });
	const FString StairsWhy = F.Interior->ConsumeConnectorFailure();
	const FString Lift = F.Interior->PlaceConnectorNode(EInteriorNavNodeType::Elevator, Middle + FVector2D(FBox2D(Building->GetFootprint()).GetSize().X * 0.25, 0.0), { 1, 2, 3 });
	const FString LiftWhy = F.Interior->ConsumeConnectorFailure();
	if (!TestFalse(*FString::Printf(TEXT("stairs placed (%s)"), *StairsWhy), Stairs.IsEmpty())) return false;
	if (!TestFalse(*FString::Printf(TEXT("lift placed (%s) in a building of levels %d"), *LiftWhy, F.Interior->GetFloorLevels().Num()), Lift.IsEmpty())) return false;

	// The floor cap above a floor lies at (level) * floor height; a connector's top must stay under it, in no plane of it.
	int32 Checked = 0;
	for (const FInteriorNavNode& Node : F.Interior->GetNavNodes())
	{
		if (!Node.MarkerActor || Node.Type == EInteriorNavNodeType::PathNode) continue;
		const float SlabAbove = Node.Level * 300.f;
		const float Top = static_cast<float>(Node.MarkerActor->GetComponentsBoundingBox(true).Max.Z);
		TestTrue(*FString::Printf(TEXT("the top of the connector on floor %d is under the slab above it (%.2f < %.2f)"), Node.Level, Top, SlabAbove), Top < SlabAbove - 0.5f);
		TestTrue(*FString::Printf(TEXT("but not by much (%.2f)"), Top), Top > SlabAbove - 10.f);
		++Checked;
	}
	TestTrue(TEXT("every connector on every floor was checked"), Checked >= 4);
	return true;
}

// Two buildings that overlap on the map and are as tall as each other: their roofs and outer walls must not share a plane.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZFightingBuildingsTest, "InNavi.Geometry.ZFightingBuildings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FZFightingBuildingsTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F;
	F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
	APolyBuilding* First = F.Generator->CreateRectangleBuilding(TEXT("ZB1"), TEXT("ZB1"), {500,500}, 1000, 1000, 0, 2, 300);
	APolyBuilding* Second = F.Generator->CreateRectangleBuilding(TEXT("ZB2"), TEXT("ZB2"), {700,600}, 1000, 600, 0, 2, 300);
	APolyBuilding* Third = F.Generator->CreateRectangleBuilding(TEXT("ZB3"), TEXT("ZB3"), {700,600}, 1000, 600, 0, 2, 300);
	APolyBuilding* Far = F.Generator->CreateRectangleBuilding(TEXT("ZB4"), TEXT("ZB4"), {9000,9000}, 500, 500, 0, 2, 300);
	if (!TestNotNull(TEXT("buildings"), First) || !TestNotNull(TEXT("second"), Second) || !TestNotNull(TEXT("third"), Third) || !TestNotNull(TEXT("far"), Far)) return false;

	TestEqual(TEXT("the first building is as it always was"), First->GetZFightLevel(), 0);
	TestNotEqual(TEXT("an overlapping building takes another level"), Second->GetZFightLevel(), First->GetZFightLevel());
	TestTrue(TEXT("and a third on top of both takes a third"), Third->GetZFightLevel() != First->GetZFightLevel() && Third->GetZFightLevel() != Second->GetZFightLevel());
	TestEqual(TEXT("a building on its own keeps level 0"), Far->GetZFightLevel(), 0);

	// The roof is higher by the building's step: the tops of the two no longer lie in one plane.
	const float FirstTop = static_cast<float>(First->GetComponentsBoundingBox(true).Max.Z);
	const float SecondTop = static_cast<float>(Second->GetComponentsBoundingBox(true).Max.Z);
	TestTrue(TEXT("the roofs are at different heights"), FMath::Abs(SecondTop - FirstTop) >= APolyBuilding::ZFightRoofStep - 0.5f);
	TestTrue(TEXT("the first one is not raised"), FMath::IsNearlyEqual(FirstTop, 600.f, 0.5f));

	// Rebuilding (a floor added) keeps the level while it is still free.
	const int32 Before = Second->GetZFightLevel();
	Second->SetFloorCount(3);
	TestEqual(TEXT("a rebuilt building keeps its level"), Second->GetZFightLevel(), Before);
	return true;
}

#endif
