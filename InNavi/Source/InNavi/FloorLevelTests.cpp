#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "LayoutValidation.h"
#include "PolyBuilding.h"
#include "UiTestFixture.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Floor levels: 1 is the ground floor, the basements are -1 (B1), -2 ..., and there is no level 0. A building may start at any level (a bridge
// that only exists on the third floor, a car park under the ground).

namespace FloorLevelTests
{
	TArray<FVector2D> Outline()
	{
		return { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 300), FVector2D(0, 300) };
	}

	FString TemporaryPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("floor-levels-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
	}

	TSharedPtr<FJsonObject> ReadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		}
		return Root;
	}

	TSharedPtr<FJsonObject> FindBuilding(const TSharedPtr<FJsonObject>& Project, const FString& Id)
	{
		const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
		if (Project.IsValid() && Project->TryGetArrayField(TEXT("buildings"), Buildings))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Buildings)
			{
				const TSharedPtr<FJsonObject> Building = Value->AsObject();
				if (Building.IsValid() && Building->GetStringField(TEXT("id")) == Id) return Building;
			}
		}
		return nullptr;
	}

	// The lowest and highest height of the wall of one level, as built.
	bool WallHeights(const APolyBuilding& Building, int32 Level, float& OutLow, float& OutHigh)
	{
		const TArray<FVector> Vertices = Building.GetWallVerticesForTests(Level);
		if (Vertices.IsEmpty()) return false;
		OutLow = OutHigh = Vertices[0].Z;
		for (const FVector& Vertex : Vertices)
		{
			OutLow = FMath::Min(OutLow, float(Vertex.Z));
			OutHigh = FMath::Max(OutHigh, float(Vertex.Z));
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorLevelNumberingTest, "InNavi.Levels.Numbering", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFloorLevelNumberingTest::RunTest(const FString& Parameters)
{
	using namespace LayoutValidation;
	TestTrue(TEXT("the ground floor is a level"), IsValidLevel(1));
	TestTrue(TEXT("so is B1"), IsValidLevel(-1));
	TestFalse(TEXT("there is no level 0"), IsValidLevel(0));
	TestTrue(TEXT("the deepest basement is a level"), IsValidLevel(-MaxBasementFloors));
	TestFalse(TEXT("one deeper is not"), IsValidLevel(-MaxBasementFloors - 1));
	TestTrue(TEXT("the highest floor is a level"), IsValidLevel(MaxBuildingFloors));
	TestFalse(TEXT("one higher is not"), IsValidLevel(MaxBuildingFloors + 1));

	TestEqual(TEXT("the ground floor starts at the ground"), LevelIndex(1), 0);
	TestEqual(TEXT("the second floor is one floor up"), LevelIndex(2), 1);
	TestEqual(TEXT("B1 is one floor down"), LevelIndex(-1), -1);
	TestEqual(TEXT("B3 is three down"), LevelIndex(-3), -3);
	for (const int32 Level : {-20, -3, -1, 1, 2, 9, 200})
	{
		TestEqual(*FString::Printf(TEXT("level %d goes to its height and back"), Level), LevelFromIndex(LevelIndex(Level)), Level);
	}

	TestEqual(TEXT("one floor up from B1 is the ground floor, not 0"), LevelAfter(-1, 1), 1);
	TestEqual(TEXT("one floor down from the ground floor is B1"), LevelAfter(1, -1), -1);
	TestEqual(TEXT("two down from the second floor is B1"), LevelAfter(2, -2), -1);
	TestEqual(TEXT("three up from B2 is the second floor"), LevelAfter(-2, 3), 2);
	TestEqual(TEXT("no steps stays"), LevelAfter(-2, 0), -2);

	TestEqual(TEXT("B2 up to the second floor is four floors"), LevelSpan(-2, 2), 4);
	TestEqual(TEXT("one level is one floor"), LevelSpan(3, 3), 1);
	TestEqual(TEXT("B3 to B1 is three"), LevelSpan(-3, -1), 3);
	TestEqual(TEXT("an upside down range is none"), LevelSpan(3, 1), 0);

	TestEqual(TEXT("B1 is called B1"), LevelLabel(-1), FString(TEXT("B1")));
	TestEqual(TEXT("B12"), LevelLabel(-12), FString(TEXT("B12")));
	TestEqual(TEXT("the ground floor is 1"), LevelLabel(1), FString(TEXT("1")));
	TestEqual(TEXT("the third is 3"), LevelLabel(3), FString(TEXT("3")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingLevelRangeTest, "InNavi.Levels.Building", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingLevelRangeTest::RunTest(const FString& Parameters)
{
	using namespace FloorLevelTests;
	FNavigationSearchFixture Fixture;
	ABuildingGenerator* Generator = Fixture.Generator;
	Fixture.Interior->ClearLoadedInterior();
	Generator->ClearAllBuildings();

	// An ordinary building starts on the ground floor.
	APolyBuilding* Plain = Generator->SpawnBuildingFromData(TEXT("L-PLAIN"), TEXT("Plain"), Outline(), 3, 300.f, FVector2D(0.0, 0.0));
	if (!TestNotNull(TEXT("an ordinary building"), Plain)) return false;
	TestEqual(TEXT("it starts at level 1"), Plain->GetFirstLevel(), 1);
	TestEqual(TEXT("and ends at level 3"), Plain->GetLastLevel(), 3);
	TestEqual(TEXT("its levels"), Plain->GetLevels(), TArray<int32>({1, 2, 3}));
	TestEqual(TEXT("it is drawn from the ground"), Plain->GetBaseZ(), 0.f);
	TestEqual(TEXT("up to its height"), Plain->GetTopZ(), 900.f);

	// A building with basements: B2, B1 and the ground floor. There is no level 0 between B1 and 1.
	APolyBuilding* Deep = Generator->SpawnBuildingFromData(TEXT("L-DEEP"), TEXT("Deep"), Outline(), 3, 300.f, FVector2D(2000.0, 0.0), -2);
	if (!TestNotNull(TEXT("a building with basements"), Deep)) return false;
	TestEqual(TEXT("its levels run B2, B1, 1"), Deep->GetLevels(), TArray<int32>({-2, -1, 1}));
	TestEqual(TEXT("the last level is the ground floor"), Deep->GetLastLevel(), 1);
	TestTrue(TEXT("it has B1"), Deep->HasLevel(-1));
	TestFalse(TEXT("it has no level 0"), Deep->HasLevel(0));
	TestFalse(TEXT("it has no second floor"), Deep->HasLevel(2));
	TestEqual(TEXT("B2 is its lowest floor"), Deep->GetLevelPosition(-2), 1);
	TestEqual(TEXT("the ground floor is its third"), Deep->GetLevelPosition(1), 3);
	TestEqual(TEXT("a level it does not have is position 0"), Deep->GetLevelPosition(2), 0);
	TestEqual(TEXT("it starts two floors below the ground"), Deep->GetBaseZ(), -600.f);
	TestEqual(TEXT("and ends at the ground floor's ceiling"), Deep->GetTopZ(), 300.f);
	TestEqual(TEXT("the floor of B1"), Deep->GetLevelBaseZ(-1), -300.f);
	TestEqual(TEXT("the floor of the ground floor"), Deep->GetLevelBaseZ(1), 0.f);

	float Low = 0.f, High = 0.f;
	if (TestTrue(TEXT("the wall of B2 is built"), WallHeights(*Deep, -2, Low, High)))
	{
		TestEqual(TEXT("the wall of B2 starts at -600"), Low, -600.f);
		TestTrue(TEXT("and ends at -300 (a little under it while see-through)"), High <= -300.f && High > -310.f);
	}
	if (TestTrue(TEXT("the wall of B1 is built"), WallHeights(*Deep, -1, Low, High)))
	{
		TestEqual(TEXT("the wall of B1 starts at -300"), Low, -300.f);
		TestEqual(TEXT("and ends at the ground"), High, 0.f);
	}
	TestFalse(TEXT("there is no wall for level 0"), WallHeights(*Deep, 0, Low, High));

	// A building that only exists on the third floor (a bridge): one floor, 600 up.
	APolyBuilding* Bridge = Generator->SpawnBuildingFromData(TEXT("L-BRIDGE"), TEXT("Bridge"), Outline(), 1, 300.f, FVector2D(4000.0, 0.0), 3);
	if (!TestNotNull(TEXT("a building that starts on the third floor"), Bridge)) return false;
	TestEqual(TEXT("its only level is 3"), Bridge->GetLevels(), TArray<int32>({3}));
	TestFalse(TEXT("it has no ground floor"), Bridge->HasLevel(1));
	TestEqual(TEXT("it floats 600 up"), Bridge->GetBaseZ(), 600.f);
	TestEqual(TEXT("and is 300 high"), Bridge->GetTopZ(), 900.f);
	if (TestTrue(TEXT("the wall of level 3 is built"), WallHeights(*Bridge, 3, Low, High)))
	{
		TestEqual(TEXT("the wall starts at 600"), Low, 600.f);
		TestTrue(TEXT("and ends at 900 (a little under it while see-through)"), High <= 900.f && High > 890.f);
	}

	// The range can be changed: B1 to the second floor, then a range upside down is the one floor.
	int32 Changes = 0;
	Bridge->OnFloorCountChanged.AddLambda([&Changes](APolyBuilding*) { ++Changes; });
	Bridge->SetLevelRange(-1, 2);
	TestEqual(TEXT("B1, 1, 2"), Bridge->GetLevels(), TArray<int32>({-1, 1, 2}));
	TestEqual(TEXT("the change was announced once"), Changes, 1);
	Bridge->SetLevelRange(-1, 2);
	TestEqual(TEXT("the same range again changes nothing"), Changes, 1);
	Bridge->SetLevelRange(3, 1);
	TestEqual(TEXT("an upside down range is the one floor, at its first level"), Bridge->GetLevels(), TArray<int32>({3}));
	Bridge->SetLevelRange(0, 0);
	TestEqual(TEXT("level 0 becomes the ground floor"), Bridge->GetLevels(), TArray<int32>({1}));
	Bridge->SetLevelRange(-500, 2);
	TestEqual(TEXT("a basement deeper than allowed stops at the deepest"), Bridge->GetFirstLevel(), -LayoutValidation::MaxBasementFloors);
	Bridge->SetFloorCount(2);
	TestEqual(TEXT("changing only the count keeps the first level"), Bridge->GetFirstLevel(), -LayoutValidation::MaxBasementFloors);
	TestEqual(TEXT("and the count"), Bridge->GetFloorCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingLevelSaveLoadTest, "InNavi.Levels.SaveAndLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingLevelSaveLoadTest::RunTest(const FString& Parameters)
{
	using namespace FloorLevelTests;
	FNavigationSearchFixture Fixture;
	ABuildingGenerator* Generator = Fixture.Generator;
	Fixture.Interior->ClearLoadedInterior();
	Generator->ClearAllBuildings();
	Generator->SpawnBuildingFromData(TEXT("S-PLAIN"), TEXT("Plain"), Outline(), 2, 300.f, FVector2D(0.0, 0.0));
	Generator->SpawnBuildingFromData(TEXT("S-DEEP"), TEXT("Deep"), Outline(), 3, 300.f, FVector2D(2000.0, 0.0), -2);
	Generator->SpawnBuildingFromData(TEXT("S-BRIDGE"), TEXT("Bridge"), Outline(), 1, 300.f, FVector2D(4000.0, 0.0), 3);

	const FString Path = TemporaryPath();
	if (!TestTrue(TEXT("the project is saved"), Generator->SaveBuildingsToFile(Path))) return false;
	const TSharedPtr<FJsonObject> Saved = ReadJson(Path);
	const TSharedPtr<FJsonObject> SavedPlain = FindBuilding(Saved, TEXT("S-PLAIN"));
	const TSharedPtr<FJsonObject> SavedDeep = FindBuilding(Saved, TEXT("S-DEEP"));
	const TSharedPtr<FJsonObject> SavedBridge = FindBuilding(Saved, TEXT("S-BRIDGE"));
	if (!TestNotNull(TEXT("all three are in the file"), SavedPlain.Get()) || !TestNotNull(TEXT("deep"), SavedDeep.Get()) || !TestNotNull(TEXT("bridge"), SavedBridge.Get()))
	{
		return false;
	}
	TestFalse(TEXT("an ordinary building has no first level in its file, as before basements"), SavedPlain->HasField(TEXT("firstLevel")));
	TestEqual(TEXT("the basement building writes its first level"), int32(SavedDeep->GetNumberField(TEXT("firstLevel"))), -2);
	TestEqual(TEXT("the bridge writes level 3"), int32(SavedBridge->GetNumberField(TEXT("firstLevel"))), 3);

	Generator->ClearAllBuildings();
	TestEqual(TEXT("all three are loaded"), Generator->LoadBuildingsFromFile(Path), 3);
	const APolyBuilding* Plain = Generator->FindBuildingById(TEXT("S-PLAIN"));
	const APolyBuilding* Deep = Generator->FindBuildingById(TEXT("S-DEEP"));
	const APolyBuilding* Bridge = Generator->FindBuildingById(TEXT("S-BRIDGE"));
	if (!TestNotNull(TEXT("plain"), Plain) || !TestNotNull(TEXT("deep"), Deep) || !TestNotNull(TEXT("bridge"), Bridge))
	{
		IFileManager::Get().Delete(*Path);
		return false;
	}
	TestEqual(TEXT("the ordinary building still starts at 1"), Plain->GetFirstLevel(), 1);
	TestEqual(TEXT("the basements come back"), Deep->GetLevels(), TArray<int32>({-2, -1, 1}));
	TestEqual(TEXT("the bridge comes back at level 3"), Bridge->GetLevels(), TArray<int32>({3}));

	// A file with a first level that does not exist is not loaded at all, as with a floor count that is wrong: not 0, not deeper than the basements
	// allow, not a fraction.
	for (const double Bad : {0.0, -21.0, 201.0, 1.5})
	{
		SavedBridge->SetStringField(TEXT("id"), TEXT("S-BAD"));
		SavedBridge->SetNumberField(TEXT("firstLevel"), Bad);
		FString Text;
		FJsonSerializer::Serialize(Saved.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
		const FString BadPath = TemporaryPath();
		FFileHelper::SaveStringToFile(Text, *BadPath);
		Generator->ClearAllBuildings();
		TestEqual(*FString::Printf(TEXT("a first level of %g stops the file from loading"), Bad), Generator->LoadBuildingsFromFile(BadPath), 0);
		TestNull(*FString::Printf(TEXT("and the bad one is not there (%g)"), Bad), Generator->FindBuildingById(TEXT("S-BAD")));
		IFileManager::Get().Delete(*BadPath);
	}
	IFileManager::Get().Delete(*Path);
	return true;
}

namespace FloorLevelTests
{
	AInteriorRoom* MakeRoom(AInteriorGenerator& Interior, const TCHAR* Name, float Left = 100.f)
	{
		FString Error;
		return Interior.CreateRoomFromFootprintWithTags({{Left, 100}, {Left + 200, 100}, {Left + 200, 300}, {Left, 300}}, FString(), Name, true, false, false, Error, {});
	}
}

// The interior editor follows the building's levels: a basement floor is its own floor with its own height, a building that starts on the third
// floor opens on it, the entry floor is the one nearest the ground, and floors keep their numbers.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteriorLevelsTest, "InNavi.Levels.Interior", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteriorLevelsTest::RunTest(const FString& Parameters)
{
	using namespace FloorLevelTests;
	FNavigationSearchFixture Fixture;
	AInteriorGenerator* Interior = Fixture.Interior;
	Interior->ClearLoadedInterior();
	Fixture.Generator->ClearAllBuildings();
	const TArray<FVector2D> Big = {{0, 0}, {1600, 0}, {1600, 1000}, {0, 1000}};

	// A building with two basements and a ground floor opens on the ground floor, with a floor for every level.
	Fixture.Generator->SpawnBuildingFromData(TEXT("I-DEEP"), TEXT("Deep"), Big, 3, 300.f, FVector2D(0.0, 0.0), -2);
	Interior->LoadInteriorForBuilding(TEXT("I-DEEP"));
	TestEqual(TEXT("a floor for every level, with no 0"), Interior->GetFloorLevels(), TArray<int32>({-2, -1, 1}));
	TestEqual(TEXT("it opens on the ground floor"), Interior->GetActiveFloor(), 1);
	TestEqual(TEXT("the floor it opens on is the ground floor"), Interior->GetOpeningLevel(), 1);
	Interior->SetActiveFloor(-1);
	TestEqual(TEXT("B1 can be the active floor"), Interior->GetActiveFloor(), -1);
	Interior->SetActiveFloor(0);
	TestTrue(TEXT("0 is not a floor: the nearest floor is used"), Interior->GetActiveFloor() == 1 || Interior->GetActiveFloor() == -1);
	Interior->SetActiveFloor(2);
	TestEqual(TEXT("a floor the building does not have gives the nearest one"), Interior->GetActiveFloor(), 1);

	// A room on B1 is drawn one floor under the ground, and its id says B01.
	Interior->SetActiveFloor(-1);
	AInteriorRoom* Basement = MakeRoom(*Interior, TEXT("Car park"));
	if (TestNotNull(TEXT("a room on B1"), Basement))
	{
		TestEqual(TEXT("it is on level -1"), Basement->FloorLevel, -1);
		TestTrue(TEXT("its walls start one floor under the ground"), FMath::IsNearlyEqual(Basement->GetComponentsBoundingBox().Min.Z, -300.0, 1.0));
		TestTrue(TEXT("its id has B01 in it, not -1"), Basement->RoomId.Contains(TEXT("-B01-")));
	}
	TestEqual(TEXT("the room is on the floor B1"), Interior->GetRoomsOnFloor(-1).Num(), 1);

	// The range is changed from either end; the floors that stay keep their numbers, and Undo brings it all back.
	Interior->SetActiveFloor(1);
	AInteriorRoom* Ground = MakeRoom(*Interior, TEXT("Lobby"), 600.f);
	TestNotNull(TEXT("a room on the ground floor"), Ground);
	TestEqual(TEXT("adding a floor above gives level 2"), Interior->AddFloor(), 2);
	TestEqual(TEXT("and below gives B3"), Interior->AddFloorBelow(), -3);
	TestEqual(TEXT("the floors are B3 to 2"), Interior->GetFloorLevels(), TArray<int32>({-3, -2, -1, 1, 2}));
	TestTrue(TEXT("the lowest floor can be removed"), Interior->RemoveFloor(-3));
	TestFalse(TEXT("a floor in the middle cannot"), Interior->RemoveFloor(-1));
	TestEqual(TEXT("so the floors are B2 to 2"), Interior->GetFloorLevels(), TArray<int32>({-2, -1, 1, 2}));
	TestTrue(TEXT("the range is set: B1 to 1"), Interior->SetLevelRange(-1, 1));
	TestEqual(TEXT("B2 and 2 are gone"), Interior->GetFloorLevels(), TArray<int32>({-1, 1}));
	TestEqual(TEXT("the room on B1 stays"), Interior->GetRoomsOnFloor(-1).Num(), 1);
	TestEqual(TEXT("and the one on the ground floor"), Interior->GetRoomsOnFloor(1).Num(), 1);
	TestTrue(TEXT("a range with the last floor below the first is the one floor"), Interior->SetLevelRange(1, -1));
	TestEqual(TEXT("which is the first"), Interior->GetFloorLevels(), TArray<int32>({1}));
	TestEqual(TEXT("the room on B1 went with its floor"), Interior->GetRoomsOnFloor(-1).Num(), 0);
	TestFalse(TEXT("0 is not a floor"), Interior->SetLevelRange(0, 2));
	TestFalse(TEXT("nor is a floor too deep"), Interior->SetLevelRange(-LayoutValidation::MaxBasementFloors - 1, 2));
	TestTrue(TEXT("Undo brings the floors back"), Interior->Undo());
	const APolyBuilding* Deep = Fixture.Generator->FindBuildingById(TEXT("I-DEEP"));
	TestEqual(TEXT("the building has B1 and 1 again"), Deep->GetLevels(), TArray<int32>({-1, 1}));
	TestEqual(TEXT("with the room on B1"), Interior->GetRoomsOnFloor(-1).Num(), 1);

	// A building that only exists on the third floor opens on it: the way in is on that floor, at its height.
	Fixture.Generator->SpawnBuildingFromData(TEXT("I-BRIDGE"), TEXT("Bridge"), Big, 1, 300.f, FVector2D(20000.0, 0.0), 3);
	Interior->LoadInteriorForBuilding(TEXT("I-BRIDGE"));
	TestEqual(TEXT("its only floor is 3"), Interior->GetFloorLevels(), TArray<int32>({3}));
	TestEqual(TEXT("it opens on it"), Interior->GetActiveFloor(), 3);
	TestEqual(TEXT("the floor it opens on is the third"), Interior->GetOpeningLevel(), 3);
	// A walkway has no way out of its own: the main door is on the ground floor only, and this building has none.
	FString DoorError;
	TestTrue(TEXT("no main door on the third floor"), Interior->AddEntranceAtPoint(FVector2D(800.0, 0.0), 15.f, DoorError).IsEmpty());
	TestTrue(TEXT("the reason is the ground floor"), DoorError.Contains(TEXT("ground floor")));
	AInteriorRoom* Deck = MakeRoom(*Interior, TEXT("Walkway"));
	if (TestNotNull(TEXT("a room on the bridge"), Deck))
	{
		TestEqual(TEXT("it is on level 3"), Deck->FloorLevel, 3);
		TestTrue(TEXT("and starts 600 up"), FMath::IsNearlyEqual(Deck->GetComponentsBoundingBox().Min.Z, 600.0, 1.0));
	}
	return true;
}

// A basement floor survives saving and loading, and a project from before basements (floors with a level and a shell without a first level)
// opens as it always did: the shell grows to cover its floors.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteriorLevelsSaveLoadTest, "InNavi.Levels.InteriorSaveAndLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteriorLevelsSaveLoadTest::RunTest(const FString& Parameters)
{
	using namespace FloorLevelTests;
	FNavigationSearchFixture Fixture;
	AInteriorGenerator* Interior = Fixture.Interior;
	Interior->ClearLoadedInterior();
	Fixture.Generator->ClearAllBuildings();
	const TArray<FVector2D> Big = {{0, 0}, {1600, 0}, {1600, 1000}, {0, 1000}};
	Fixture.Generator->SpawnBuildingFromData(TEXT("V-DEEP"), TEXT("Deep"), Big, 3, 300.f, FVector2D(0.0, 0.0), -2);
	Interior->LoadInteriorForBuilding(TEXT("V-DEEP"));
	Interior->SetActiveFloor(-2);
	TestNotNull(TEXT("a room on B2"), MakeRoom(*Interior, TEXT("Plant room")));
	Interior->SetActiveFloor(1);
	TestNotNull(TEXT("a room on the ground floor"), MakeRoom(*Interior, TEXT("Entrance hall")));
	Fixture.Generator->SetInteriorDataForBuilding(TEXT("V-DEEP"), Interior->CaptureInteriorSnapshot());

	const FString Path = TemporaryPath();
	if (!TestTrue(TEXT("the project is saved"), Fixture.Generator->SaveBuildingsToFile(Path))) return false;
	Interior->ClearLoadedInterior();
	Fixture.Generator->ClearAllBuildings();
	TestEqual(TEXT("and loads"), Fixture.Generator->LoadBuildingsFromFile(Path), 1);
	Interior->LoadInteriorForBuilding(TEXT("V-DEEP"));
	TestEqual(TEXT("the floors come back as they were"), Interior->GetFloorLevels(), TArray<int32>({-2, -1, 1}));
	TestEqual(TEXT("with the room on B2"), Interior->GetRoomsOnFloor(-2).Num(), 1);
	TestEqual(TEXT("and the one on the ground floor"), Interior->GetRoomsOnFloor(1).Num(), 1);
	IFileManager::Get().Delete(*Path);

	// A file from before basements: a shell of one floor and an interior with a floor on level 3. The shell grows to cover it, as it always did.
	const FString OldPath = TemporaryPath();
	FFileHelper::SaveStringToFile(TEXT(R"({"buildings":[{"id":"V-OLD","name":"Old","floorCount":1,"footprint":[[0,0],[1000,0],[1000,1000],[0,1000]],"interior":{"floors":[{"level":3}]}}]})"), *OldPath);
	Interior->ClearLoadedInterior();
	Fixture.Generator->ClearAllBuildings();
	TestEqual(TEXT("the old file loads"), Fixture.Generator->LoadBuildingsFromFile(OldPath), 1);
	Interior->LoadInteriorForBuilding(TEXT("V-OLD"));
	const APolyBuilding* Old = Fixture.Generator->FindBuildingById(TEXT("V-OLD"));
	if (TestNotNull(TEXT("the building"), Old))
	{
		TestEqual(TEXT("its shell now covers level 3, from the ground floor"), Old->GetLevels(), TArray<int32>({1, 2, 3}));
	}
	TestEqual(TEXT("only the authored floor has an entry"), Interior->GetFloorLevels(), TArray<int32>({3}));

	// A file with a basement floor in the interior but no first level on the building: the shell goes down to cover it.
	const FString BasementPath = TemporaryPath();
	FFileHelper::SaveStringToFile(TEXT(R"({"buildings":[{"id":"V-BASE","name":"Base","floorCount":2,"footprint":[[0,0],[1000,0],[1000,1000],[0,1000]],"interior":{"floors":[{"level":-1},{"level":1}]}}]})"), *BasementPath);
	Interior->ClearLoadedInterior();
	Fixture.Generator->ClearAllBuildings();
	TestEqual(TEXT("the file with a basement floor loads"), Fixture.Generator->LoadBuildingsFromFile(BasementPath), 1);
	Interior->LoadInteriorForBuilding(TEXT("V-BASE"));
	const APolyBuilding* Base = Fixture.Generator->FindBuildingById(TEXT("V-BASE"));
	if (TestNotNull(TEXT("the building"), Base))
	{
		TestTrue(TEXT("its shell covers B1 and the ground floor"), Base->HasLevel(-1) && Base->HasLevel(1));
	}
	TestEqual(TEXT("both floors are there"), Interior->GetFloorLevels(), TArray<int32>({-1, 1}));
	IFileManager::Get().Delete(*OldPath);
	IFileManager::Get().Delete(*BasementPath);
	return true;
}

#endif
