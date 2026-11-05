#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "ConfirmDialogWidget.h"
#include "EditScreenWidget.h"
#include "InteriorRoom.h"
#include "NavPlayerController.h"
#include "PolyBuilding.h"
#include "UiTestFixture.h"
#include "WavFileLoader.h"

#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/Archive.h"
#include "SQLiteDatabase.h"
#include "UObject/Script.h"

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
	// Exercise replacement on a local filesystem, independently of workspace sync tools.
	const FString Path = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), TEXT("InNavi-save-"), TEXT(".json"));
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
	if (!TestTrue(TEXT("the test file is made read-only"), Files.SetReadOnly(*Path, true)))
	{
		Files.DeleteFile(*Path);
		return false;
	}
	// The write that cannot be done is logged as an error, once.
	AddExpectedError(TEXT("SaveBuildingsToFile: could not write"), EAutomationExpectedErrorFlags::Contains, 1);
	UEditScreenWidget* Exterior = NewObject<UEditScreenWidget>();
	Exterior->Initialize(); Exterior->TakeWidget(); Exterior->SetOwningPlayer(Controller);
	Exterior->ProcessEvent(Exterior->FindFunctionChecked(TEXT("HandleFileNewSave")), nullptr);
	TestEqual(TEXT("a save that failed does not start a new project: the buildings are still there"), Fixture.Generator->GetBuildingCount(), Buildings);
	TestTrue(TEXT("the changes are still marked as unsaved"), Controller->HasUnsavedChanges());
	TestEqual(TEXT("and it is still the same file"), Controller->GetCurrentProjectFilePath(), Path);

	TestTrue(TEXT("the test file is writable again"), Files.SetReadOnly(*Path, false));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRobustnessDiscardExteriorTest, "InNavi.Project.Robustness.DiscardExteriorEdits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRobustnessDiscardExteriorTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	Controller->RestoreExteriorCameraView();
	const FString Before = Fixture.Generator->GetBuildingType(TEXT("E2"));
	const FString Changed = Before == TEXT("Commercial") ? TEXT("University") : TEXT("Commercial");
	TestTrue(TEXT("an existing building can be selected"), Controller->SelectBuildingById(TEXT("E2")));
	TestTrue(TEXT("its type can be changed"), Controller->SetSelectedBuildingType(Changed));
	TestNotNull(TEXT("a new building can be drawn"), Controller->CreateRectangleBuilding(TEXT("New"), TEXT("New"), FVector2D::ZeroVector, 400, 400, 0, 2, 300));
	TestTrue(TEXT("the new building can be selected"), Controller->SelectBuildingById(TEXT("New")));
	TestTrue(TEXT("the changes mark the project dirty"), Controller->HasUnsavedChanges());
	Controller->DiscardUnsavedChanges();
	TestEqual(TEXT("the original type is restored"), Fixture.Generator->GetBuildingType(TEXT("E2")), Before);
	TestNull(TEXT("the unsaved building is removed"), Fixture.Generator->FindBuildingById(TEXT("New")));
	TestTrue(TEXT("the discarded building's selection is cleared"), Controller->GetSelectedBuildingId().IsEmpty());
	TestFalse(TEXT("discard leaves the project clean"), Controller->HasUnsavedChanges());

	TestNotNull(TEXT("the removed ID can be used again"), Controller->CreateRectangleBuilding(TEXT("New"), TEXT("New"), FVector2D::ZeroVector, 400, 400, 0, 2, 300));
	const FString Path = RobustnessTests::TemporaryFile(TEXT(".json"));
	TestTrue(TEXT("the new building can be saved"), Controller->SaveCurrentProjectAs(Path));
	Controller->DiscardUnsavedChanges();
	TestNotNull(TEXT("the saved building survives discard"), Fixture.Generator->FindBuildingById(TEXT("New")));
	IFileManager::Get().Delete(*Path);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectWindowCloseTest, "InNavi.Project.Robustness.WindowClose", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectWindowCloseTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	FEditorScriptExecutionGuard ScriptGuard;
	auto* Controller = Fixture.MakeController();
	auto* Viewport = NewObject<UGameViewportClient>(GEngine);
	Controller->BindGameWindowCloseGuard(Viewport);
	TestTrue(TEXT("a clean project allows the window to close"), Viewport->WindowCloseRequested());
	const FString Path = RobustnessTests::TemporaryFile(TEXT(".json"));
	TestTrue(TEXT("the project is saved to a known path"), Controller->SaveCurrentProjectAs(Path));
	TestNotNull(TEXT("an unsaved building is created"), Controller->CreateRectangleBuilding(
		TEXT("Close"), TEXT("Close"), {0, 0}, 400, 400, 0, 1, 300));
	TestFalse(TEXT("the viewport vetoes closing a dirty project"), Viewport->WindowCloseRequested());
	if (auto* Dialog = Controller->WindowCloseDialog.Get(); TestNotNull(TEXT("the save prompt is created"), Dialog))
	{
		TestFalse(TEXT("a second close request is also vetoed"), Viewport->WindowCloseRequested());
		TestTrue(TEXT("only one prompt is created"), Dialog == Controller->WindowCloseDialog.Get());
		Dialog->OnCancel.Broadcast();
		TestTrue(TEXT("Cancel keeps the changes"), Controller->HasUnsavedChanges());
	}
	TestFalse(TEXT("the prompt can be reopened"), Viewport->WindowCloseRequested());
#if PLATFORM_WINDOWS
	if (auto* Dialog = Controller->WindowCloseDialog.Get())
	{
		auto& Platform = FPlatformFileManager::Get().GetPlatformFile();
		TUniquePtr<IFileHandle> Locked(Platform.OpenRead(*Path));
		if (TestTrue(TEXT("the project can be locked against replacement"), Locked.IsValid()))
		{
			AddExpectedError(TEXT("SaveBuildingsToFile: could not write"), EAutomationExpectedErrorFlags::Contains, 1);
			Dialog->OnSaveAndLeave.Broadcast();
			TestTrue(TEXT("a failed save keeps the project dirty"), Controller->HasUnsavedChanges());
		}
	}
	TestFalse(TEXT("closing can be retried after a failed save"), Viewport->WindowCloseRequested());
#endif
	if (auto* Dialog = Controller->WindowCloseDialog.Get()) Dialog->OnSaveAndLeave.Broadcast();
	TestFalse(TEXT("Save and Exit saves the changes"), Controller->HasUnsavedChanges());
	TestTrue(TEXT("the saved project allows closing"), Viewport->WindowCloseRequested());

	TestNotNull(TEXT("another unsaved building is created"), Controller->CreateRectangleBuilding(
		TEXT("Discard"), TEXT("Discard"), {0, 0}, 400, 400, 0, 1, 300));
	TestFalse(TEXT("the new edit is guarded"), Viewport->WindowCloseRequested());
	if (auto* Dialog = Controller->WindowCloseDialog.Get()) Dialog->OnDiscardAndLeave.Broadcast();
	TestNull(TEXT("Discard and Exit drops the edit"), Fixture.Generator->FindBuildingById(TEXT("Discard")));
	TestTrue(TEXT("discarded edits allow closing"), Viewport->WindowCloseRequested());
	Controller->UnbindGameWindowCloseGuard();
	TestFalse(TEXT("teardown clears the viewport delegate"), Viewport->OnWindowCloseRequested().IsBound());
	IFileManager::Get().Delete(*Path);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectNewerSaveTest, "InNavi.Project.Robustness.PreserveNewerFormat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectNewerSaveTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	auto* Controller = Fixture.MakeController();
	const FString Original = TEXT("{\"version\":99,\"buildings\":[],\"futureData\":{\"name\":\"keep me\"}}");
	const FString Path = RobustnessTests::WriteTemporaryFile(Original, TEXT(".json"));
	const FString Copy = RobustnessTests::TemporaryFile(TEXT(".json"));
	TestTrue(TEXT("the newer file still opens"), Controller->TryLoadBuildingDataFromPathOrDatabase(Path));
	TestEqual(TEXT("the newer version is retained while editing"), Fixture.Generator->GetLoadedFileVersion(), 99);
	TestFalse(TEXT("the editor refuses the original as a Save As destination"), Controller->SaveCurrentProjectAs(Path));
	TestTrue(TEXT("a separate compatible copy can be saved"), Controller->SaveCurrentProjectAs(Copy));
	TestEqual(TEXT("the copy is now the current save path"), Controller->GetCurrentProjectFilePath(), Copy);
	TestTrue(TEXT("Ctrl+S can save the copy without asking for a path"), Controller->SaveCurrentProject());
	TestFalse(TEXT("any explicit save over the newer file is still refused"), Fixture.Generator->SaveBuildingsToFile(Path));
	FString Text;
	TestTrue(TEXT("the original can still be read"), FFileHelper::LoadFileToString(Text, *Path));
	TestEqual(TEXT("its unknown data and version survived"), Text, Original);
	IFileManager::Get().Delete(*Path);
	IFileManager::Get().Delete(*Copy);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectInvalidScaleTest, "InNavi.Project.Robustness.InvalidScale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectInvalidScaleTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	auto* Controller = Fixture.MakeController();
	const FString ActivePath = RobustnessTests::TemporaryFile(TEXT(".json"));
	TestTrue(TEXT("the current project has a save path"), Controller->SaveCurrentProjectAs(ActivePath));
	TestTrue(TEXT("the current project has an unsaved change"), Controller->AddFloor() > 0);
	auto* Existing = Fixture.Generator->FindBuildingById(TEXT("E2"));
	for (const TCHAR* Scale : {TEXT("0"), TEXT("-1"), TEXT("\"0\""), TEXT("1e40")})
	{
		const FString Text = FString::Printf(TEXT("{\"scale\":%s,\"buildings\":[{\"id\":\"B\",\"name\":\"B\","
			"\"footprint\":[[0,0],[400,0],[400,400],[0,400]]}]}"), Scale);
		const FString Path = RobustnessTests::WriteTemporaryFile(Text, TEXT(".json"));
		TestFalse(TEXT("the invalid scale is refused before replacing the project"), Controller->TryLoadBuildingDataFromPathOrDatabase(Path));
		TestTrue(TEXT("the current building still exists"), Fixture.Generator->FindBuildingById(TEXT("E2")) == Existing);
		TestTrue(TEXT("the unsaved edit remains"), Controller->HasUnsavedChanges());
		TestEqual(TEXT("the save path remains the original"), Controller->GetCurrentProjectFilePath(), ActivePath);
		IFileManager::Get().Delete(*Path);
	}
	IFileManager::Get().Delete(*ActivePath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectStaleSaveTest, "InNavi.Project.Robustness.StaleSave", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectStaleSaveTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture First;
	FNavigationSearchFixture Second;
	auto* Writer = First.MakeController();
	auto* Stale = Second.MakeController();
	const FString Path = RobustnessTests::TemporaryFile(TEXT(".json"));
	const FString Copy = RobustnessTests::TemporaryFile(TEXT(".json"));
	TestTrue(TEXT("both sessions open the same project"), Writer->SaveCurrentProjectAs(Path) && Stale->LoadBuildingDataFromPath(Path) > 0);
	First.Generator->SetProjectInfo(TEXT("First edit"), FString());
	Second.Generator->SetProjectInfo(TEXT("Second edit"), FString());
	TestTrue(TEXT("the first writer can save"), Writer->SaveCurrentProject());
	TestFalse(TEXT("a stale write is refused"), Stale->SaveCurrentProject());
	FString Text;
	TestTrue(TEXT("the current file remains readable"), FFileHelper::LoadFileToString(Text, *Path));
	TestTrue(TEXT("the first writer's changes survive"), Text.Contains(TEXT("First edit")));
	TestTrue(TEXT("the second writer can save to a new path"), Stale->SaveCurrentProjectAs(Copy));
	TestTrue(TEXT("saving again updates the new path normally"), Stale->SaveCurrentProject());
	TestTrue(TEXT("the new copy contains the second edit"), FFileHelper::LoadFileToString(Text, *Copy) && Text.Contains(TEXT("Second edit")));
	TestTrue(TEXT("an external deletion can remove the saved copy"), IFileManager::Get().Delete(*Copy));
	TestFalse(TEXT("saving does not silently undo an external deletion"), Stale->SaveCurrentProject());
	IFileManager::Get().Delete(*Path);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectInvalidShellTest, "InNavi.Project.Robustness.InvalidShell", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectInvalidShellTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	auto* Controller = Fixture.MakeController();
	auto* Previous = Fixture.Generator->FindBuildingById(TEXT("E2"));
	for (const FString& Building : {
		FString(TEXT("{\"floorHeight\":0,\"footprint\":[[0,0],[400,0],[400,400],[0,400]],\"id\":\"B\",\"name\":\"B\"}")),
		FString(TEXT("{\"footprint\":[[0,0],[400,0],[800,0]],\"id\":\"B\",\"name\":\"B\"}"))})
	{
		const FString Path = RobustnessTests::WriteTemporaryFile(TEXT("{\"buildings\":[") + Building + TEXT("]}"), TEXT(".json"));
		TestFalse(TEXT("an unusable shell is refused"), Controller->TryLoadBuildingDataFromPathOrDatabase(Path));
		TestTrue(TEXT("the existing project is kept"), Fixture.Generator->FindBuildingById(TEXT("E2")) == Previous);
		IFileManager::Get().Delete(*Path);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectInvalidInteriorImportTest, "InNavi.Project.Robustness.InvalidInteriorImport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectInvalidInteriorImportTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	auto* Controller = Fixture.MakeController();
	auto* Previous = Fixture.Generator->FindBuildingById(TEXT("E2"));
	const FString Path = RobustnessTests::WriteTemporaryFile(TEXT(R"({
		"buildings":[{"id":"B","name":"B","footprint":[[0,0],[1000,0],[1000,1000],[0,1000]],
		"interior":{"floors":[{"level":1,"rooms":[{"id":"CannotDrop","name":"Room without geometry"}]}]}}]
	})"), TEXT(".json"));
	TestFalse(TEXT("a missing room footprint fails import"), Controller->TryLoadBuildingDataFromPathOrDatabase(Path));
	TestTrue(TEXT("the original project stays open"), Fixture.Generator->FindBuildingById(TEXT("E2")) == Previous);
	IFileManager::Get().Delete(*Path);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectRemoveLinkedBuildingTest, "InNavi.Project.Robustness.RemoveLinkedBuilding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectRemoveLinkedBuildingTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	auto* Controller = Fixture.MakeController();
	Controller->StartNewProject();
	const TArray<FVector2D> Outline = {{0,0},{1000,0},{1000,1000},{0,1000}};
	Fixture.Generator->SpawnBuildingFromData(TEXT("Source"), TEXT("Source"), Outline, 1, 300, {0,0});
	auto* Target = Fixture.Generator->SpawnBuildingFromData(TEXT("Target"), TEXT("Target"), Outline, 1, 300, {1000,0});
	if (!TestNotNull(TEXT("the linked building exists"), Target)) return false;
	TSharedPtr<FJsonObject> Data;
	const FString Json = TEXT(R"({"floors":[{"level":1}],"entrances":[
		{"id":"Main","kind":"main","level":1,"edge":0,"offset":100,"width":120},
		{"id":"Link","kind":"link","connectsTo":"Target","level":1,"edge":1,"offset":100,"width":120}
	]})");
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Data);
	Fixture.Generator->SetInteriorDataForBuilding(TEXT("Source"), Data);
	Fixture.Interior->LoadInteriorForBuilding(TEXT("Source"));
	TestEqual(TEXT("both authored entrances load"), Fixture.Interior->GetEntrances().Num(), 2);
	FString Error;
	TestNotNull(TEXT("a room edit creates an undo step"), Fixture.Interior->CreateRoomFromFootprint(
		{{300,300},{500,300},{500,500},{300,500}}, TEXT("Room"), TEXT("Room"), true, false, false, Error));
	TestTrue(TEXT("the target can be removed"), Fixture.Generator->RemoveBuilding(*Target));
	TestEqual(TEXT("the stored link is removed"), Fixture.Generator->GetStoredEntrances(TEXT("Source")).Num(), 1);
	TestEqual(TEXT("the open interior keeps only the main entrance"), Fixture.Interior->GetEntrances().Num(), 1);
	Fixture.Interior->SyncInteriorToBuildingGenerator();
	TestEqual(TEXT("writing back the open interior cannot resurrect the link"), Fixture.Generator->GetStoredEntrances(TEXT("Source")).Num(), 1);
	Fixture.Generator->SetInteriorDataForBuilding(TEXT("Source"), Data);
	TestEqual(TEXT("restoring an old snapshot cannot resurrect the link"), Fixture.Generator->GetStoredEntrances(TEXT("Source")).Num(), 1);
	TestNotNull(TEXT("the deleted ID is available again"), Fixture.Generator->SpawnBuildingFromData(TEXT("Target"), TEXT("New target"), Outline, 1, 300, {1000,0}));
	TestTrue(TEXT("undo still works after the deletion"), Fixture.Interior->Undo());
	TestEqual(TEXT("undo cannot link to the replacement building"), Fixture.Interior->GetEntrances().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSaveDuringRotationTest, "InNavi.Project.Robustness.SaveDuringRotation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectSaveDuringRotationTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	auto* Controller = Fixture.MakeController();
	Controller->StartNewProject();
	Fixture.Generator->SpawnBuildingFromData(TEXT("Rotation"), TEXT("Rotation"), {{0,0},{2200,0},{2200,1600},{0,1600}}, 2, 300, {0,0});
	Fixture.Interior->LoadInteriorForBuilding(TEXT("Rotation"));
	const TArray<FVector2D> Original = {{100,100},{450,100},{450,450},{100,450}};
	const TArray<FVector2D> Preview = {{700,500},{1100,500},{1100,750},{700,750}};
	FString Error;
	auto* Room = Fixture.Interior->CreateRoomFromFootprint(Original, TEXT("Rotated"), TEXT("Rotated"), true, false, false, Error);
	if (!TestNotNull(TEXT("the room exists"), Room)) return false;
	if (!TestFalse(TEXT("the lift is placed"), Fixture.Interior->PlaceConnectorNode(EInteriorNavNodeType::Elevator, {900,500}, {1,2}).IsEmpty())) return false;
	Controller->SelectRoom(Room->RoomId);
	Controller->StartRotateCW();
	TestTrue(TEXT("the temporary room crosses the connector"), Controller->MoveRoomTo(Room->RoomId, Preview));
	const FString Path = RobustnessTests::TemporaryFile(TEXT(".json"));
	TestTrue(TEXT("save succeeds"), Controller->SaveCurrentProjectAs(Path));
	TestTrue(TEXT("save reverts the blocked rotation"), Room->GetFootprint() == Original);
	Controller->StopRotateCW();
	Controller->LoadBuildingDataFromPath(Path);
	Fixture.Interior->LoadInteriorForBuilding(TEXT("Rotation"));
	if (const auto* Loaded = Fixture.Interior->GetRoomById(TEXT("Rotated")); TestNotNull(TEXT("saved room reloads"), Loaded))
		TestTrue(TEXT("the valid footprint was saved"), Loaded->GetFootprint() == Original);
	IFileManager::Get().Delete(*Path);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectGeometryEditTest, "InNavi.Project.Robustness.GeometryEdits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectGeometryEditTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	Fixture.MakeController()->StartNewProject();
	Fixture.Generator->SpawnBuildingFromData(TEXT("Notch"), TEXT("Notch"),
		{{0,0},{1000,0},{1000,1000},{700,1000},{700,300},{300,300},{300,1000},{0,1000}}, 1, 300, {0,0});
	Fixture.Interior->LoadInteriorForBuilding(TEXT("Notch"));
	FString Error;
	AddExpectedError(TEXT("footprint extends outside"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("a room cannot span a concave notch"), Fixture.Interior->CreateRoomFromFootprint(
		{{150,600},{850,600},{850,800},{150,800}}, TEXT("Bridge"), TEXT("Bridge"), true, false, false, Error));
	TestNull(TEXT("a wall cannot span the notch"), Fixture.Interior->CreateWallFromPoints({{150,600},{850,600}}, TEXT("BridgeWall"), Error));
	const TArray<FVector2D> Left = {{100,100},{200,100},{200,200},{100,200}};
	TestNotNull(TEXT("a valid room can be created"), Fixture.Interior->CreateRoomFromFootprint(Left, TEXT("Left"), TEXT("Left"), true, false, false, Error));
	TestNotNull(TEXT("its neighbour can be created"), Fixture.Interior->CreateRoomFromFootprint(
		{{220,100},{290,100},{290,200},{220,200}}, TEXT("Right"), TEXT("Right"), true, false, false, Error));
	AddExpectedError(TEXT("wall overlaps existing room"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("a wall cannot cut through two opposite corners"), Fixture.Interior->CreateWallFromPoints({Left[0], Left[2]}, TEXT("Diagonal"), Error));
	TestFalse(TEXT("a resize cannot overlap a neighbouring room"), Fixture.Interior->ResizeRoom(TEXT("Left"), 180, 100));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSaveDuringDragTest, "InNavi.Project.Robustness.SaveDuringDrag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectSaveDuringDragTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	auto* Controller = Fixture.MakeController();
	Controller->StartNewProject();
	Fixture.Generator->SpawnBuildingFromData(TEXT("Drag"), TEXT("Drag"), {{0,0},{1500,0},{1500,1000},{0,1000}}, 1, 300, {0,0});
	Fixture.Interior->LoadInteriorForBuilding(TEXT("Drag"));
	const TArray<FVector2D> Original = {{100,100}, {450,100}, {450,450}, {100,450}};
	const TArray<FVector2D> Obstacle = {{700,100}, {1050,100}, {1050,450}, {700,450}};
	const TArray<FVector2D> Adjacent = {{350,100}, {700,100}, {700,450}, {350,450}};
	FString Error;
	auto* Room = Fixture.Interior->CreateRoomFromFootprint(Original, TEXT("Moved"), TEXT("Moved"), true, false, false, Error);
	if (!TestNotNull(TEXT("the room to move exists"), Room)) return false;
	TestNotNull(TEXT("the obstacle exists"), Fixture.Interior->CreateRoomFromFootprint(Obstacle, TEXT("Other"), TEXT("Other"), true, false, false, Error));
	TestTrue(TEXT("a room can share an edge with its neighbour"), Controller->MoveRoomTo(Room->RoomId, Adjacent) && Controller->CommitRoomMove(Room->RoomId, Original));
	TestTrue(TEXT("the room returns to its original position"), Controller->MoveRoomTo(Room->RoomId, Original) && Controller->CommitRoomMove(Room->RoomId, Adjacent));
	Controller->bIsDragging3DRoom = true;
	Controller->Dragging3DRoom = Room;
	Controller->Dragging3DRoomId = Room->RoomId;
	Controller->DragStart3DRoomFootprint = Original;
	Controller->SetIgnoreLookInput(true);
	TestTrue(TEXT("the overlapping position is permitted as a preview"), Controller->MoveRoomTo(Room->RoomId, Obstacle));
	const FString Path = RobustnessTests::TemporaryFile(TEXT(".json"));
	TestTrue(TEXT("save finalises the drag"), Controller->SaveCurrentProjectAs(Path));
	TestFalse(TEXT("the drag has finished"), Controller->bIsDragging3DRoom);
	TestTrue(TEXT("the invalid position was reverted before saving"), Room->GetFootprint() == Original);
	Controller->LoadBuildingDataFromPath(Path);
	Fixture.Interior->LoadInteriorForBuilding(TEXT("Drag"));
	if (auto* Loaded = Fixture.Interior->GetRoomById(TEXT("Moved")); TestNotNull(TEXT("the saved room reloads"), Loaded))
		TestTrue(TEXT("the file contains the valid position"), Loaded->GetFootprint() == Original);
	IFileManager::Get().Delete(*Path);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectWavBoundsTest, "InNavi.Audio.WavBounds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectWavBoundsTest::RunTest(const FString& Parameters)
{
	const FString Sample = FPaths::ProjectDir() / TEXT("Data/Audio/select_ui.wav");
	TestNotNull(TEXT("a shipped WAV is still accepted"), LoadWavFileAsSoundWave(Sample, GetTransientPackage()));
	const FString Oversized = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), TEXT("InNavi-audio-"), TEXT(".wav"));
	TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*Oversized));
	if (!TestTrue(TEXT("the test file can be opened"), Writer.IsValid())) return false;
	Writer->Seek(64 * 1024 * 1024);
	uint8 Byte = 0;
	Writer->Serialize(&Byte, 1);
	Writer.Reset();
	AddExpectedError(TEXT("no larger than 64 MiB"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("oversized audio is rejected before decoding"), LoadWavFileAsSoundWave(Oversized, GetTransientPackage()));
	IFileManager::Get().Delete(*Oversized);

	const TArray<uint8> EmptyWave = {
		'R','I','F','F', 36,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0,
		1,0, 1,0, 0x40,0x1F,0,0, 0x80,0x3E,0,0, 2,0, 16,0, 'd','a','t','a', 0,0,0,0
	};
	const FString Empty = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), TEXT("InNavi-empty-audio-"), TEXT(".wav"));
	if (TestTrue(TEXT("an empty WAV can be written"), FFileHelper::SaveArrayToFile(EmptyWave, *Empty)))
	{
		AddExpectedError(TEXT("has no audio data"), EAutomationExpectedErrorFlags::Contains, 1);
		TestNull(TEXT("an empty data chunk at the end is handled safely"), LoadWavFileAsSoundWave(Empty, GetTransientPackage()));
	}
	IFileManager::Get().Delete(*Empty);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
