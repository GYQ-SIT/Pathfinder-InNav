#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "EditScreenWidget.h"
#include "NavPlayerController.h"
#include "PolyBuilding.h"
#include "UiTestFixture.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "SQLiteDatabase.h"

namespace RobustnessTests
{
	FString TemporaryFile(const TCHAR* Extension)
	{
		return FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("robustness-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + Extension);
	}

	FString WriteTemporaryFile(const FString& Text, const TCHAR* Extension)
	{
		const FString Path = TemporaryFile(Extension);
		FFileHelper::SaveStringToFile(Text, *Path);
		return Path;
	}

	// How many floors the stored interior data of a building has, or -1 when it has none.
	int32 StoredFloorCount(ABuildingGenerator* Generator, const FString& BuildingId)
	{
		const TSharedPtr<FJsonObject> Interior = Generator->GetInteriorDataForBuilding(BuildingId);
		const TArray<TSharedPtr<FJsonValue>>* Floors = nullptr;
		return Interior.IsValid() && Interior->TryGetArrayField(TEXT("floors"), Floors) ? Floors->Num() : -1;
	}
}

// "Save and new" with a save that does not happen (the file cannot be written, as when "Save as" is cancelled) keeps the project open.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRobustnessFailedSaveTest, "InNavi.Project.Robustness.FailedSaveKeepsTheProject", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRobustnessFailedSaveTest::RunTest(const FString& Parameters)
{
	using namespace RobustnessTests;
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	const FString Path = TemporaryFile(TEXT(".json"));
	if (!TestTrue(TEXT("a project file is written"), Fixture.Generator->SaveBuildingsToFile(Path)))
	{
		return false;
	}
	TestTrue(TEXT("it is opened"), Controller->LoadBuildingDataFromPath(Path) > 0);
	const int32 Buildings = Fixture.Generator->GetBuildingCount();
	Controller->LoadInteriorForBuilding(TEXT("E2"));
	TestTrue(TEXT("a building is renamed"), Controller->SetLoadedBuildingName(TEXT("Renamed for the test")));
	TestTrue(TEXT("so the project has unsaved changes"), Controller->HasUnsavedChanges());

	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	Files.SetReadOnly(*Path, true);
	// The write that cannot be done is logged as an error, once.
	AddExpectedError(TEXT("SaveBuildingsToFile: could not write"), EAutomationExpectedErrorFlags::Contains, 1);
	UEditScreenWidget* Exterior = NewObject<UEditScreenWidget>();
	Exterior->Initialize(); Exterior->TakeWidget(); Exterior->SetOwningPlayer(Controller);
	Exterior->ProcessEvent(Exterior->FindFunctionChecked(TEXT("HandleFileNewSave")), nullptr);
	TestEqual(TEXT("a save that failed does not start a new project: the buildings are still there"), Fixture.Generator->GetBuildingCount(), Buildings);
	TestTrue(TEXT("the changes are still marked as unsaved"), Controller->HasUnsavedChanges());
	TestEqual(TEXT("and it is still the same file"), Controller->GetCurrentProjectFilePath(), Path);

	Files.SetReadOnly(*Path, false);
	Exterior->ProcessEvent(Exterior->FindFunctionChecked(TEXT("HandleFileNewSave")), nullptr);
	TestEqual(TEXT("once the file can be written, Save and new saves and starts the new project"), Fixture.Generator->GetBuildingCount(), 0);
	Files.DeleteFile(*Path);
	return true;
}

// A file or database that cannot be loaded is refused before anything is cleared: what is open stays open, with its save path.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRobustnessBadFilesTest, "InNavi.Project.Robustness.BadFilesKeepWhatIsOpen", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRobustnessBadFilesTest::RunTest(const FString& Parameters)
{
	using namespace RobustnessTests;
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	const FString Good = TemporaryFile(TEXT(".json"));
	if (!TestTrue(TEXT("a project file is written"), Fixture.Generator->SaveBuildingsToFile(Good)) || !TestTrue(TEXT("it is opened"), Controller->LoadBuildingDataFromPath(Good) > 0))
	{
		return false;
	}
	const int32 Buildings = Fixture.Generator->GetBuildingCount();
	APolyBuilding* OpenBuilding = Fixture.Generator->FindBuildingById(TEXT("E2"));
	Controller->LoadInteriorForBuilding(TEXT("E2"));
	TestTrue(TEXT("there is an unsaved floor in the live interior"), Controller->AddFloor() > 0);
	const int32 ActiveFloor = Controller->GetActiveFloor();

	struct FBadFile
	{
		const TCHAR* What;
		const TCHAR* Text;
	};
	const FBadFile BadFiles[] = {
		{ TEXT("a file that is not JSON"), TEXT("{ this is not json") },
		{ TEXT("a file without a list of buildings"), TEXT("{ \"version\": 1 }") },
		{ TEXT("a file whose buildings have no outline"), TEXT("{ \"version\": 1, \"buildings\": [ { \"id\": \"X\", \"name\": \"No outline\" } ] }") },
		{ TEXT("a file with an empty building ID"), TEXT(R"({"buildings":[{"id":"","name":"Broken","footprint":[[0,0],[1000,0],[1000,1000],[0,1000]]}]})") },
		{ TEXT("a file with one good and one bad building"), TEXT(R"({"buildings":[{"id":"Other","name":"Other","footprint":[[0,0],[1000,0],[1000,1000],[0,1000]]},{"id":"Broken","name":"Broken"}]})") }
	};
	AddExpectedError(TEXT("SpawnBuildingFromData: building id cannot be empty"), EAutomationExpectedErrorFlags::Contains, 1);
	for (const FBadFile& Bad : BadFiles)
	{
		const FString Path = WriteTemporaryFile(Bad.Text, TEXT(".json"));
		TestEqual(*FString::Printf(TEXT("%s loads nothing"), Bad.What), Controller->LoadBuildingDataFromPath(Path), 0);
		TestEqual(*FString::Printf(TEXT("%s leaves the buildings that were open"), Bad.What), Fixture.Generator->GetBuildingCount(), Buildings);
		TestEqual(*FString::Printf(TEXT("%s leaves the file to save to"), Bad.What), Controller->GetCurrentProjectFilePath(), Good);
		IFileManager::Get().Delete(*Path);
	}
	TestEqual(TEXT("a file that is not there loads nothing"), Controller->LoadBuildingDataFromPath(TemporaryFile(TEXT(".json"))), 0);
	TestEqual(TEXT("and leaves the buildings"), Fixture.Generator->GetBuildingCount(), Buildings);

	const FString NotADatabase = WriteTemporaryFile(TEXT("this is not a database"), TEXT(".db"));
	TestEqual(TEXT("a file that is not a map database loads nothing"), Controller->LoadBuildingDataFromDatabase(NotADatabase), 0);
	TestEqual(TEXT("and leaves the buildings"), Fixture.Generator->GetBuildingCount(), Buildings);
	TestEqual(TEXT("and the file to save to"), Controller->GetCurrentProjectFilePath(), Good);
	IFileManager::Get().Delete(*NotADatabase);

	const FString MissingNodes = TemporaryFile(TEXT(".db"));
	FSQLiteDatabase Database;
	if (TestTrue(TEXT("a database without building nodes can be created"), Database.Open(*MissingNodes, ESQLiteDatabaseOpenMode::ReadWriteCreate)))
	{
		TestTrue(TEXT("the database contains a campus building"),
			Database.Execute(TEXT("CREATE TABLE buildings (building_id INTEGER, building_type TEXT)")) &&
			Database.Execute(TEXT("INSERT INTO buildings VALUES (1, 'university')")));
		Database.Close();
		TestFalse(TEXT("the malformed database fails to load"), Controller->TryLoadBuildingDataFromPathOrDatabase(MissingNodes));
	}
	IFileManager::Get().Delete(*MissingNodes);
	TestEqual(TEXT("the original building actor is intact"), Fixture.Generator->FindBuildingById(TEXT("E2")), OpenBuilding);
	TestEqual(TEXT("the original interior is still open"), Controller->GetLoadedBuildingId(), FString(TEXT("E2")));
	TestEqual(TEXT("the unsaved floor is still active"), Controller->GetActiveFloor(), ActiveFloor);
	TestTrue(TEXT("the project is still marked as unsaved"), Controller->HasUnsavedChanges());
	TestEqual(TEXT("the original save path is intact"), Controller->GetCurrentProjectFilePath(), Good);
	TestTrue(TEXT("Ctrl+S can still save the original project"), Controller->SaveCurrentProject());
	FString Saved;
	TSharedPtr<FJsonObject> SavedRoot;
	if (TestTrue(TEXT("the saved file is readable"), FFileHelper::LoadFileToString(Saved, *Good) &&
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Saved), SavedRoot)))
	{
		TestEqual(TEXT("Ctrl+S saved the original buildings"), SavedRoot->GetArrayField(TEXT("buildings")).Num(), Buildings);
	}

	// A new project, with no buildings yet, is a good file.
	const FString Empty = WriteTemporaryFile(TEXT("{ \"version\": 1, \"buildings\": [] }"), TEXT(".json"));
	TestTrue(TEXT("an empty project is opened successfully"), Controller->TryLoadBuildingDataFromPathOrDatabase(Empty));
	TestEqual(TEXT("a project without buildings opens, empty"), Fixture.Generator->GetBuildingCount(), 0);
	TestEqual(TEXT("and is the file to save to"), Controller->GetCurrentProjectFilePath(), Empty);
	IFileManager::Get().Delete(*Empty);
	IFileManager::Get().Delete(*Good);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRobustnessSparseFloorTest, "InNavi.Project.Robustness.SparseFloorsPreserveShell", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRobustnessSparseFloorTest::RunTest(const FString& Parameters)
{
	using namespace RobustnessTests;
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	const FString Path = WriteTemporaryFile(TEXT(R"({"buildings":[{"id":"Sparse","name":"Sparse","floorCount":4,"footprint":[[0,0],[1000,0],[1000,1000],[0,1000]],"interior":{"floors":[{"level":1},{"level":3}]}}]})"), TEXT(".json"));
	if (!TestEqual(TEXT("the sparse project opens"), Controller->LoadBuildingDataFromPath(Path), 1))
	{
		IFileManager::Get().Delete(*Path);
		return false;
	}
	APolyBuilding* Building = Fixture.Generator->FindBuildingById(TEXT("Sparse"));
	Controller->LoadInteriorForBuilding(TEXT("Sparse"));
	TestEqual(TEXT("opening the interior keeps all four shell floors"), Building->GetFloorCount(), 4);
	const TArray<int32> AuthoredLevels{1, 3};
	TestTrue(TEXT("the authored levels retain their numbers"), Fixture.Interior->GetFloorLevels() == AuthoredLevels);
	TestEqual(TEXT("a new floor goes above the shell"), Controller->AddFloor(), 5);
	TestTrue(TEXT("undo succeeds"), Controller->UndoInteriorEdit());
	TestEqual(TEXT("undo restores the previous shell height"), Building->GetFloorCount(), 4);
	TestTrue(TEXT("redo succeeds"), Controller->RedoInteriorEdit());
	TestEqual(TEXT("redo restores the added shell floor"), Building->GetFloorCount(), 5);
	TestTrue(TEXT("the added floor can be removed"), Controller->RemoveFloor(5));
	TestEqual(TEXT("removing one floor leaves the other four"), Building->GetFloorCount(), 4);
	TestTrue(TEXT("the project saves"), Controller->SaveCurrentProject());
	TestEqual(TEXT("the saved project reloads"), Controller->LoadBuildingDataFromPath(Path), 1);
	Building = Fixture.Generator->FindBuildingById(TEXT("Sparse"));
	if (TestNotNull(TEXT("the saved building exists"), Building))
	{
		Controller->LoadInteriorForBuilding(TEXT("Sparse"));
		TestEqual(TEXT("the shell survives saving and reopening"), Building->GetFloorCount(), 4);
		TestTrue(TEXT("the saved interior retains levels 1 and 3"), Fixture.Interior->GetFloorLevels() == AuthoredLevels);
	}
	IFileManager::Get().Delete(*Path);
	return true;
}

// Opening a map database must not leave the JSON project that was open before as the file Ctrl+S writes to.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRobustnessDatabaseSavePathTest, "InNavi.Project.Robustness.DatabaseDoesNotKeepTheOldSavePath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRobustnessDatabaseSavePathTest::RunTest(const FString& Parameters)
{
	using namespace RobustnessTests;
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	const FString Database = FPaths::ProjectDir() / TEXT("Data/map_extended.db");
	if (!TestTrue(TEXT("the campus database is there"), IFileManager::Get().FileExists(*Database)))
	{
		return false;
	}
	const FString Path = TemporaryFile(TEXT(".json"));
	if (!TestTrue(TEXT("a project file is written"), Fixture.Generator->SaveBuildingsToFile(Path)) || !TestTrue(TEXT("it is opened"), Controller->LoadBuildingDataFromPath(Path) > 0))
	{
		return false;
	}
	TestEqual(TEXT("an opened project is the file to save to"), Controller->GetCurrentProjectFilePath(), Path);
	TestTrue(TEXT("the campus database is opened"), Controller->LoadBuildingDataFromDatabase(Database) > 0);
	TestTrue(TEXT("a database is not a file to save to, and the project opened before no longer is"), Controller->GetCurrentProjectFilePath().IsEmpty());
	IFileManager::Get().Delete(*Path);
	return true;
}

// Discard and switch to another building: the edits that were discarded must not come back from the live interior.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRobustnessDiscardTest, "InNavi.Project.Robustness.DiscardedEditsStayDiscarded", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRobustnessDiscardTest::RunTest(const FString& Parameters)
{
	using namespace RobustnessTests;
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	if (!TestTrue(TEXT("E2 is opened"), Controller->LoadInteriorForBuilding(TEXT("E2")) >= 0))
	{
		return false;
	}
	APolyBuilding* Building = Fixture.Generator->FindBuildingById(TEXT("E2"));
	if (!TestNotNull(TEXT("E2"), Building))
	{
		return false;
	}
	const int32 Floors = Building->GetFloorCount();
	const int32 StoredBefore = StoredFloorCount(Fixture.Generator, TEXT("E2"));
	TestTrue(TEXT("the stored interior has floors"), StoredBefore > 0);

	TestTrue(TEXT("a floor is added"), Controller->AddFloor() >= 1);
	TestEqual(TEXT("the building has one floor more"), Building->GetFloorCount(), Floors + 1);
	TestTrue(TEXT("and has unsaved changes"), Controller->HasUnsavedChanges());

	Controller->DiscardUnsavedChanges();
	TestEqual(TEXT("Discard puts the floors back"), Building->GetFloorCount(), Floors);
	TestTrue(TEXT("another building is opened"), Controller->LoadInteriorForBuildingAfterDiscard(TEXT("E1")) >= 0);
	TestEqual(TEXT("the floors are still the ones before the edit"), Building->GetFloorCount(), Floors);
	TestEqual(TEXT("and so is the stored interior: the live copy was not written over it"), StoredFloorCount(Fixture.Generator, TEXT("E2")), StoredBefore);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
