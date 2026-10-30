#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "InteriorEntrance.h"
#include "InteriorGenerator.h"
#include "NavPlayerController.h"
#include "PolyBuilding.h"
#include "ProjectLibrary.h"
#include "UiTestFixture.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "SQLiteDatabase.h"

// Smaller faults found in a review of the code: deleting a floor, copying a project with a long name, pasting a building, and the
// precision of the map database's coordinates.

namespace MinorFixTests
{
	TArray<int32> SortedLevels(const TArray<FInteriorNavNode>& Nodes)
	{
		TArray<int32> Levels;
		for (const FInteriorNavNode& Node : Nodes)
		{
			Levels.Add(Node.Level);
		}
		Levels.Sort();
		return Levels;
	}

	TArray<int32> SortedLevels(const TArray<FInteriorEntrance>& Entrances)
	{
		TArray<int32> Levels;
		for (const FInteriorEntrance& Entrance : Entrances)
		{
			Levels.Add(Entrance.Level);
		}
		Levels.Sort();
		return Levels;
	}

	bool AllReachOnly(const TArray<FInteriorNavNode>& Nodes, const TArray<int32>& Floors)
	{
		for (const FInteriorNavNode& Node : Nodes)
		{
			TArray<int32> Reached = Node.ConnectsFloors;
			Reached.Sort();
			if (Reached != Floors)
			{
				return false;
			}
		}
		return true;
	}
}

// Deleting a floor takes the lifts, stairs and doors in the outer wall of that floor with it, and renumbers the lifts and stairs of the floors
// above. (A main door is only ever on the ground floor; the doors of the floors above are link doors to a neighbouring building, which
// this test has no neighbour for, so their renumbering is not tested here.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinorFixRemoveFloorTest, "InNavi.Interior.RemoveFloor.TakesConnectorsAndDoorsAlong", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMinorFixRemoveFloorTest::RunTest(const FString& Parameters)
{
	using namespace MinorFixTests;
	FNavigationSearchFixture Fixture;
	const double Units = 100.0 / Fixture.Generator->GetScale();
	const FVector2D Offset(9000.0, 9000.0);
	const TArray<FVector2D> Outline = { FVector2D(0, 0), FVector2D(60 * Units, 0), FVector2D(60 * Units, 30 * Units), FVector2D(0, 30 * Units) };
	const FString Id = TEXT("RM-FLOOR");
	Fixture.Generator->SpawnBuildingFromData(Id, TEXT("Remove floor"), Outline, 3, 300.f, Offset);
	AInteriorGenerator* Interior = Fixture.Interior;
	Interior->LoadInteriorForBuilding(Id);
	while (Interior->GetFloorLevels().Num() < 3 && Interior->AddFloor() >= 1)
	{
	}
	if (!TestEqual(TEXT("the building has three floors"), Interior->GetFloorLevels().Num(), 3))
	{
		return false;
	}

	// A lift that reaches all three floors, and two doors in the outer wall of the first floor.
	TestFalse(TEXT("a lift is placed"), Interior->PlaceConnectorNode(EInteriorNavNodeType::Elevator, FVector2D(30 * Units, 15 * Units), TArray<int32>{ 1, 2, 3 }).IsEmpty());
	Interior->SetActiveFloor(1);
	for (const double Along : { 20.0, 40.0 })
	{
		FString Error;
		FString Entrance = Interior->AddEntranceAtPoint(FVector2D(Along * Units, 0.0), 15.f, Error);
		if (Entrance.IsEmpty())
		{
			Entrance = Interior->AddEntranceAtPoint(FVector2D(Offset.X + Along * Units, Offset.Y), 15.f, Error);
		}
		TestFalse(*FString::Printf(TEXT("a door in the outer wall is added (%s)"), *Error), Entrance.IsEmpty());
	}
	TestTrue(TEXT("the lift is on every floor"), SortedLevels(Interior->GetNavNodes()) == TArray<int32>({ 1, 2, 3 }));
	TestTrue(TEXT("the doors are on the first floor"), SortedLevels(Interior->GetEntrances()) == TArray<int32>({ 1, 1 }));

	TestTrue(TEXT("the middle floor is deleted"), Interior->RemoveFloor(2));
	TestTrue(TEXT("the lift is on the two floors that are left, the upper one renumbered"), SortedLevels(Interior->GetNavNodes()) == TArray<int32>({ 1, 2 }));
	TestTrue(TEXT("and it reaches those two floors by their new numbers"), AllReachOnly(Interior->GetNavNodes(), { 1, 2 }));
	TestTrue(TEXT("the doors of the first floor are still there"), SortedLevels(Interior->GetEntrances()) == TArray<int32>({ 1, 1 }));

	TestTrue(TEXT("the first floor is deleted"), Interior->RemoveFloor(1));
	TestTrue(TEXT("the lift of the first floor is gone, the other one is on the first floor now"), SortedLevels(Interior->GetNavNodes()) == TArray<int32>({ 1 }));
	TestTrue(TEXT("and it reaches only that floor"), AllReachOnly(Interior->GetNavNodes(), { 1 }));
	TestEqual(TEXT("the doors of the first floor are gone with it"), Interior->GetEntrances().Num(), 0);
	return true;
}

// A project with a long name can be copied more than once: every copy has a file of its own, and the original is not touched.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinorFixLongCopyNameTest, "InNavi.Project.Library.CopiesOfALongName", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMinorFixLongCopyNameTest::RunTest(const FString& Parameters)
{
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("LongName_") + FGuid::NewGuid().ToString());
	const FString LongName = FString::ChrN(100, TEXT('n'));
	const FString SourcePath = ProjectLibrary::FilePathForName(Folder, LongName);
	const FString Text = FString::Printf(TEXT("{ \"version\": 1, \"projectName\": \"%s\", \"buildings\": [] }"), *LongName);
	FFileHelper::SaveStringToFile(Text, *SourcePath);

	const TArray<FProjectLibraryEntry> Entries = ProjectLibrary::Scan(Folder);
	if (!TestEqual(TEXT("the project is found"), Entries.Num(), 1))
	{
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
		return false;
	}
	FString First, Second, Error;
	TestTrue(TEXT("the project is copied"), ProjectLibrary::Duplicate(Entries[0], Folder, First, Error));
	TestTrue(TEXT("and copied again"), ProjectLibrary::Duplicate(Entries[0], Folder, Second, Error));
	TestNotEqual(TEXT("the first copy is not the original"), First, Entries[0].FilePath);
	TestNotEqual(TEXT("the second copy is not the original"), Second, Entries[0].FilePath);
	TestNotEqual(TEXT("the two copies are different files"), First, Second);
	TestTrue(TEXT("both exist"), IFileManager::Get().FileExists(*First) && IFileManager::Get().FileExists(*Second));
	FString After;
	FFileHelper::LoadFileToString(After, *Entries[0].FilePath);
	TestEqual(TEXT("the original is as it was"), After, Text);
	TestEqual(TEXT("the folder has the original and the two copies"), ProjectLibrary::Scan(Folder).Num(), 3);
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

// A pasted building has its centre under the cursor also when the building stands at an offset.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinorFixPasteTest, "InNavi.Building.Paste.CentredOnTheCursor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMinorFixPasteTest::RunTest(const FString& Parameters)
{
	const TArray<FVector2D> Copied = { FVector2D(0, 0), FVector2D(100, 0), FVector2D(100, 50), FVector2D(0, 50) };
	const float Scale = 100.f;
	const FVector2D Cursor(123456.0, 78900.0);
	for (const FVector2D& Offset : { FVector2D(0, 0), FVector2D(500, 300), FVector2D(-70, 1200) })
	{
		const TArray<FVector2D> Pasted = ANavPlayerController::PastedFootprint(Copied, Offset, Scale, Cursor);
		if (!TestEqual(TEXT("the outline keeps its points"), Pasted.Num(), Copied.Num()))
		{
			return false;
		}
		FVector2D Center = FVector2D::ZeroVector;
		for (const FVector2D& Point : Pasted)
		{
			Center += Point;
		}
		Center /= Pasted.Num();
		const FVector2D OnTheGround = (Center + Offset) * Scale;
		TestTrue(*FString::Printf(TEXT("with an offset of (%.0f, %.0f) the centre is where the cursor is"), Offset.X, Offset.Y), OnTheGround.Equals(Cursor, 0.01));
		TestTrue(TEXT("and the shape is the same"), (Pasted[1] - Pasted[0]).Equals(FVector2D(100, 0), 0.001) && (Pasted[3] - Pasted[0]).Equals(FVector2D(0, 50), 0.001));
	}
	TestEqual(TEXT("nothing copied gives nothing"), ANavPlayerController::PastedFootprint(TArray<FVector2D>(), FVector2D::ZeroVector, Scale, Cursor).Num(), 0);
	return true;
}

// The campus read from the map database is where the database says, to the centimetre: a longitude kept in a float is only good to 0.4 m.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinorFixDatabasePrecisionTest, "InNavi.Building.Database.KeepsItsPrecision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMinorFixDatabasePrecisionTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ABuildingGenerator* Generator = Fixture.Generator;
	const FString Path = FPaths::ProjectDir() / TEXT("Data/map_extended.db");
	if (!TestTrue(TEXT("the campus database is there"), IFileManager::Get().FileExists(*Path)))
	{
		return false;
	}
	Fixture.Interior->ClearLoadedInterior();
	Generator->ClearAllBuildings();
	Generator->ClearAllRoadsAndPOIs();
	Generator->ClearAllContextBuildings();
	if (!TestTrue(TEXT("the campus is loaded"), Generator->LoadBuildingsFromDatabase(Path) > 0))
	{
		return false;
	}

	FSQLiteDatabase Database;
	if (!TestTrue(TEXT("the database opens"), Database.Open(*Path, ESQLiteDatabaseOpenMode::ReadOnly)))
	{
		return false;
	}
	// The point the loader projects around: the middle of the nodes of the university buildings.
	double RefLat = 0.0, RefLon = 0.0;
	Database.Execute(TEXT("SELECT AVG(bn.lat) AS lat, AVG(bn.lon) AS lon FROM buildings b JOIN building_nodes bn ON b.building_id = bn.building_id WHERE b.building_type = 'university'"),
		[&RefLat, &RefLon](const FSQLitePreparedStatement& Statement) -> ESQLitePreparedStatementExecuteRowResult
		{
			Statement.GetColumnValueByName(TEXT("lat"), RefLat);
			Statement.GetColumnValueByName(TEXT("lon"), RefLon);
			return ESQLitePreparedStatementExecuteRowResult::Continue;
		});
	constexpr double MetersPerDegreeLat = 111320.0;
	const double MetersPerDegreeLon = MetersPerDegreeLat * FMath::Cos(FMath::DegreesToRadians(RefLat));

	double Worst = 0.0;
	int32 Compared = 0;
	for (const APolyBuilding* Building : Generator->GetFilteredBuildings())
	{
		TArray<FVector2D> Nodes; // (lat, lon) in the order of the database
		Database.Execute(*FString::Printf(TEXT("SELECT lat, lon FROM building_nodes WHERE building_id = %lld ORDER BY sequence"), FCString::Atoi64(*Building->BuildingId.Mid(4))),
			[&Nodes](const FSQLitePreparedStatement& Statement) -> ESQLitePreparedStatementExecuteRowResult
			{
				double Lat = 0.0, Lon = 0.0;
				Statement.GetColumnValueByName(TEXT("lat"), Lat);
				Statement.GetColumnValueByName(TEXT("lon"), Lon);
				Nodes.Add(FVector2D(Lat, Lon));
				return ESQLitePreparedStatementExecuteRowResult::Continue;
			});
		const TArray<FVector2D>& Outline = Building->GetFootprint();
		for (int32 Index = 0; Index < Outline.Num() && Index < Nodes.Num(); ++Index)
		{
			const FVector2D Expected((Nodes[Index].Y - RefLon) * MetersPerDegreeLon, (Nodes[Index].X - RefLat) * MetersPerDegreeLat);
			Worst = FMath::Max(Worst, FVector2D::Distance(Outline[Index], Expected));
			++Compared;
		}
	}
	Database.Close();
	TestTrue(TEXT("many corners of the campus buildings were compared"), Compared > 100);
	TestTrue(*FString::Printf(TEXT("no corner is more than a centimetre from where the database puts it (the worst is %.4f m)"), Worst), Worst < 0.01);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
