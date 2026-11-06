#if WITH_DEV_AUTOMATION_TESTS

#include "ProjectCardWidget.h"
#include "ProjectLibrary.h"
#include "ProjectFile.h"
#include "ProjectLibraryWidget.h"
#include "StartScreenWidget.h"
#include "UiTestFixture.h"

#include "Components/Button.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "SQLiteDatabase.h"

// The project library's file work: what is listed, in what order, and copying / renaming without losing anything.

namespace ProjectLibraryTests
{
	FString MakeFolder()
	{
		return FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("Library_") + FGuid::NewGuid().ToString());
	}

	void WriteLibraryFile(const FString& Path, const FString& Text, FDateTime Modified)
	{
		FFileHelper::SaveStringToFile(Text, *Path);
		IFileManager::Get().SetTimeStamp(*Path, Modified);
	}

	const FProjectLibraryEntry* Find(const TArray<FProjectLibraryEntry>& Entries, const FString& FileName)
	{
		return Entries.FindByPredicate([&FileName](const FProjectLibraryEntry& Entry) { return Entry.FileName == FileName; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectLibraryScanTest, "InNavi.Project.Library.Scan", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectLibraryScanTest::RunTest(const FString& Parameters)
{
	using namespace ProjectLibraryTests;
	const FString Folder = MakeFolder();
	IFileManager::Get().MakeDirectory(*Folder, true);
	const FDateTime Now = FDateTime(2026, 10, 20, 12, 0, 0);

	WriteLibraryFile(Folder / TEXT("plain.json"), TEXT("{\"buildings\":[]}"), Now - FTimespan::FromDays(3));
	WriteLibraryFile(Folder / TEXT("named.json"), TEXT("{\"projectName\":\"Campus North\",\"description\":\"Two blocks\",\"buildings\":[{},{}]}"), Now - FTimespan::FromDays(1));
	WriteLibraryFile(Folder / TEXT("buildings.json"), TEXT("{\"buildings\":[{}]}"), Now - FTimespan::FromDays(2));
	WriteLibraryFile(Folder / TEXT("layout.json"), TEXT("{\"layoutVersion\":3}"), Now);
	WriteLibraryFile(Folder / TEXT("broken.json"), TEXT("{ not json"), Now);
	WriteLibraryFile(Folder / TEXT("notes.txt"), TEXT("hello"), Now);
	WriteLibraryFile(Folder / TEXT("junk.db"), TEXT("this is not a database"), Now);

	// A real database with 3 buildings, 2 of them campus ones.
	{
		FSQLiteDatabase Database;
		if (TestTrue(TEXT("the test database can be created"), Database.Open(*(Folder / TEXT("map.db")), ESQLiteDatabaseOpenMode::ReadWriteCreate)))
		{
			Database.Execute(TEXT("CREATE TABLE buildings (building_id INTEGER, building_type TEXT)"), [](const FSQLitePreparedStatement&) { return ESQLitePreparedStatementExecuteRowResult::Continue; });
			Database.Execute(TEXT("INSERT INTO buildings VALUES (1,'university'),(2,'university'),(3,'house')"), [](const FSQLitePreparedStatement&) { return ESQLitePreparedStatementExecuteRowResult::Continue; });
			Database.Close();
			IFileManager::Get().SetTimeStamp(*(Folder / TEXT("map.db")), Now - FTimespan::FromDays(5));
		}
	}

	const TArray<FProjectLibraryEntry> Entries = ProjectLibrary::Scan(Folder);
	TestEqual(TEXT("only the loadable files are listed (3 projects and 1 database)"), Entries.Num(), 4);
	TestNull(TEXT("a layout file is not a project"), Find(Entries, TEXT("layout.json")));
	TestNull(TEXT("a file that is not JSON is skipped"), Find(Entries, TEXT("broken.json")));
	TestNull(TEXT("a file that is not a database is skipped"), Find(Entries, TEXT("junk.db")));

	if (Entries.Num() == 4)
	{
		TestEqual(TEXT("newest first"), Entries[0].FileName, FString(TEXT("named.json")));
		TestEqual(TEXT("then the older ones in order"), Entries[1].FileName, FString(TEXT("buildings.json")));
		TestEqual(TEXT("..."), Entries[2].FileName, FString(TEXT("plain.json")));
		TestEqual(TEXT("the oldest last"), Entries[3].FileName, FString(TEXT("map.db")));
	}
	if (const FProjectLibraryEntry* Named = Find(Entries, TEXT("named.json")))
	{
		TestEqual(TEXT("the project's own name is shown"), Named->DisplayName, FString(TEXT("Campus North")));
		TestEqual(TEXT("with its description"), Named->Description, FString(TEXT("Two blocks")));
		TestEqual(TEXT("and how many buildings it holds"), Named->BuildingCount, 2);
		TestEqual(TEXT("and when it was last written"), Named->Modified, Now - FTimespan::FromDays(1));
		TestFalse(TEXT("it is not a sample"), Named->bIsSample);
	}
	if (const FProjectLibraryEntry* Plain = Find(Entries, TEXT("plain.json")))
	{
		TestEqual(TEXT("without a name, the file name is shown"), Plain->DisplayName, FString(TEXT("plain")));
		TestEqual(TEXT("an empty project has no buildings"), Plain->BuildingCount, 0);
	}
	if (const FProjectLibraryEntry* Sample = Find(Entries, TEXT("buildings.json")))
	{
		TestTrue(TEXT("the shipped sample is marked"), Sample->bIsSample);
	}
	if (const FProjectLibraryEntry* Database = Find(Entries, TEXT("map.db")))
	{
		TestTrue(TEXT("a database is marked"), Database->bIsDatabase);
		TestEqual(TEXT("it counts the campus buildings"), Database->BuildingCount, 2);
	}

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectLibraryEditTest, "InNavi.Project.Library.DuplicateRename", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectLibraryEditTest::RunTest(const FString& Parameters)
{
	using namespace ProjectLibraryTests;
	const FString Folder = MakeFolder();
	IFileManager::Get().MakeDirectory(*Folder, true);
	const FDateTime Now = FDateTime(2026, 10, 20, 12, 0, 0);
	WriteLibraryFile(Folder / TEXT("a.json"), TEXT("{\"projectName\":\"Alpha\",\"description\":\"First\",\"scale\":10,\"buildings\":[{\"id\":\"X\"}]}"), Now);
	WriteLibraryFile(Folder / TEXT("b.json"), TEXT("{\"projectName\":\"Beta\",\"buildings\":[]}"), Now);
	WriteLibraryFile(Folder / TEXT("buildings.json"), TEXT("{\"buildings\":[{}]}"), Now);

	auto Load = [&Folder](const TCHAR* FileName) { return *ProjectLibraryTests::Find(ProjectLibrary::Scan(Folder), FileName); };
	FString NewPath, Error;

	// Duplicate: a copy with its own name and everything else the same, the original untouched.
	TestTrue(TEXT("a project can be duplicated"), ProjectLibrary::Duplicate(Load(TEXT("a.json")), Folder, NewPath, Error));
	TestEqual(TEXT("the copy is named after it"), FPaths::GetCleanFilename(NewPath), FString(TEXT("Alpha copy.json")));
	const FProjectLibraryEntry Copy = Load(TEXT("Alpha copy.json"));
	TestEqual(TEXT("the copy has the new name inside"), Copy.DisplayName, FString(TEXT("Alpha copy")));
	TestEqual(TEXT("and the same description"), Copy.Description, FString(TEXT("First")));
	TestEqual(TEXT("and the same buildings"), Copy.BuildingCount, 1);
	TestEqual(TEXT("the original keeps its name"), Load(TEXT("a.json")).DisplayName, FString(TEXT("Alpha")));
	TestTrue(TEXT("a second copy gets the next free name"), ProjectLibrary::Duplicate(Load(TEXT("a.json")), Folder, NewPath, Error));
	TestEqual(TEXT("with a number"), FPaths::GetCleanFilename(NewPath), FString(TEXT("Alpha copy 2.json")));
	TestTrue(TEXT("the shipped sample can be duplicated"), ProjectLibrary::Duplicate(Load(TEXT("buildings.json")), Folder, NewPath, Error));

	// Rename: the name inside and the file name change together.
	TestTrue(TEXT("a project can be renamed"), ProjectLibrary::Rename(Load(TEXT("b.json")), TEXT("  Gamma  "), Folder, NewPath, Error));
	TestEqual(TEXT("the file follows the name"), FPaths::GetCleanFilename(NewPath), FString(TEXT("Gamma.json")));
	TestFalse(TEXT("the old file is gone"), IFileManager::Get().FileExists(*(Folder / TEXT("b.json"))));
	TestEqual(TEXT("the new file holds the name"), Load(TEXT("Gamma.json")).DisplayName, FString(TEXT("Gamma")));

	// Refusals leave everything as it was.
	const FProjectLibraryEntry Alpha = Load(TEXT("a.json"));
	TestFalse(TEXT("a name another project has is refused"), ProjectLibrary::Rename(Alpha, TEXT("gamma"), Folder, NewPath, Error));
	TestTrue(TEXT("with the reason"), Error.Contains(TEXT("already exists")));
	TestFalse(TEXT("an empty name is refused"), ProjectLibrary::Rename(Alpha, TEXT("  "), Folder, NewPath, Error));
	TestFalse(TEXT("so is one with nothing usable in it"), ProjectLibrary::Rename(Alpha, TEXT("???"), Folder, NewPath, Error));
	TestFalse(TEXT("the shipped sample is not renamed"), ProjectLibrary::Rename(Load(TEXT("buildings.json")), TEXT("Mine"), Folder, NewPath, Error));
	TestTrue(TEXT("it is still there"), IFileManager::Get().FileExists(*(Folder / TEXT("buildings.json"))));
	TestEqual(TEXT("Alpha is unchanged"), Load(TEXT("a.json")).DisplayName, FString(TEXT("Alpha")));

	// Only the case of the name changes: the same file, not a clash with itself.
	TestTrue(TEXT("changing only the case is allowed"), ProjectLibrary::Rename(Load(TEXT("Gamma.json")), TEXT("GAMMA"), Folder, NewPath, Error));
	TestEqual(TEXT("and shows in the name"), ProjectLibraryTests::Find(ProjectLibrary::Scan(Folder), FPaths::GetCleanFilename(NewPath))->DisplayName, FString(TEXT("GAMMA")));

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

#if PLATFORM_WINDOWS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectFileBlockedReplacementTest, "InNavi.Project.Library.BlockedReplacement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectFileBlockedReplacementTest::RunTest(const FString& Parameters)
{
	const FString Folder = ProjectLibraryTests::MakeFolder();
	const FString Path = Folder / TEXT("Original.json");
	const FString Original = TEXT("{\"projectName\":\"Original\",\"buildings\":[]}");
	auto& Files = IFileManager::Get();
	Files.MakeDirectory(*Folder, true);
	TestTrue(TEXT("the original project was written"), ProjectFile::WriteText(Path, Original));
	const auto Entries = ProjectLibrary::Scan(Folder);
	if (TestEqual(TEXT("the original project is listed"), Entries.Num(), 1))
	{
		auto& Platform = FPlatformFileManager::Get().GetPlatformFile();
		TestTrue(TEXT("the original is made read-only"), Platform.SetReadOnly(*Path, true));
		TestFalse(TEXT("a blocked replacement fails"), ProjectFile::WriteText(Path, TEXT("replacement")));
		FString NewPath, Error;
		TestFalse(TEXT("a rename with a blocked original fails"), ProjectLibrary::Rename(Entries[0], TEXT("New name"), Folder, NewPath, Error));
		TestTrue(TEXT("the error describes the rollback"), Error.Contains(TEXT("rolled back")));
		TestFalse(TEXT("the rename leaves no extra project"), Files.FileExists(*(Folder / TEXT("New name.json"))));
		FString Text;
		TestTrue(TEXT("the original remains readable"), FFileHelper::LoadFileToString(Text, *Path));
		TestEqual(TEXT("the original bytes survive"), Text, Original);
		TArray<FString> FilesInFolder;
		Files.FindFiles(FilesInFolder, *(Folder / TEXT("*")), true, false);
		TestEqual(TEXT("no staging files remain"), FilesInFolder.Num(), 1);
		Platform.SetReadOnly(*Path, false);
		TestTrue(TEXT("a retry replaces the original"), ProjectFile::WriteText(Path, TEXT("replacement")));
		TestTrue(TEXT("the replacement is readable"), FFileHelper::LoadFileToString(Text, *Path));
		TestEqual(TEXT("the replacement is complete"), Text, FString(TEXT("replacement")));
	}
	Files.DeleteDirectory(*Folder, false, true);
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectLibraryDeleteTest, "InNavi.Project.Library.Delete", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectLibraryDeleteTest::RunTest(const FString& Parameters)
{
	using namespace ProjectLibraryTests;
	const FString Folder = MakeFolder();
	IFileManager::Get().MakeDirectory(*Folder, true);
	const FDateTime Now = FDateTime(2026, 10, 20, 12, 0, 0);
	WriteLibraryFile(Folder / TEXT("a.json"), TEXT("{\"projectName\":\"Alpha\",\"buildings\":[{}]}"), Now);
	WriteLibraryFile(Folder / TEXT("buildings.json"), TEXT("{\"buildings\":[{}]}"), Now);

	auto Load = [&Folder](const TCHAR* FileName) { return *ProjectLibraryTests::Find(ProjectLibrary::Scan(Folder), FileName); };
	FString DeletedPath, Error;

	TestTrue(TEXT("a project can be deleted"), ProjectLibrary::SoftDelete(Load(TEXT("a.json")), Folder, DeletedPath, Error));
	TestFalse(TEXT("its file leaves the library folder"), IFileManager::Get().FileExists(*(Folder / TEXT("a.json"))));
	TestTrue(TEXT("and is kept in the Deleted folder"), IFileManager::Get().FileExists(*(Folder / TEXT("Deleted") / TEXT("a.json"))));
	TestEqual(TEXT("the library no longer lists it (and does not list the Deleted folder's files)"), ProjectLibrary::Scan(Folder).Num(), 1);

	// A project with the same file name deleted later does not replace the first one.
	WriteLibraryFile(Folder / TEXT("a.json"), TEXT("{\"projectName\":\"Alpha again\",\"buildings\":[]}"), Now);
	TestTrue(TEXT("a second project with that file name can be deleted too"), ProjectLibrary::SoftDelete(Load(TEXT("a.json")), Folder, DeletedPath, Error));
	TArray<FString> Kept;
	IFileManager::Get().FindFiles(Kept, *(Folder / TEXT("Deleted") / TEXT("*.json")), true, false);
	TestEqual(TEXT("both are kept"), Kept.Num(), 2);

	TestFalse(TEXT("the shipped sample cannot be deleted"), ProjectLibrary::SoftDelete(Load(TEXT("buildings.json")), Folder, DeletedPath, Error));
	TestTrue(TEXT("with the reason"), Error.Contains(TEXT("sample")));
	TestTrue(TEXT("and it is still there"), IFileManager::Get().FileExists(*(Folder / TEXT("buildings.json"))));

	FProjectLibraryEntry Gone;
	Gone.FilePath = Folder / TEXT("not here.json");
	Gone.FileName = TEXT("not here.json");
	TestFalse(TEXT("a file that is already gone is reported"), ProjectLibrary::SoftDelete(Gone, Folder, DeletedPath, Error));

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectLibraryWidgetTest, "InNavi.UI.ProjectLibrary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectLibraryWidgetTest::RunTest(const FString& Parameters)
{
	using namespace ProjectLibraryTests;
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	const FString Folder = MakeFolder();
	IFileManager::Get().MakeDirectory(*Folder, true);
	Controller->SetProjectsDirectoryOverrideForTests(Folder);
	const FDateTime Now = FDateTime(2026, 10, 20, 12, 0, 0);
	WriteLibraryFile(Folder / TEXT("alpha.json"), TEXT("{\"projectName\":\"Alpha Campus\",\"description\":\"Two blocks\",\"buildings\":[{},{}]}"), Now);
	WriteLibraryFile(Folder / TEXT("empty.json"), TEXT("{\"projectName\":\"Blank\",\"buildings\":[]}"), Now - FTimespan::FromDays(1));
	WriteLibraryFile(Folder / TEXT("buildings.json"), TEXT("{\"buildings\":[{}]}"), Now - FTimespan::FromDays(2));

	UProjectLibraryWidget* Library = NewObject<UProjectLibraryWidget>();
	Library->Initialize();
	// Held for the whole test: the cards are built into this tree when they are added, and only while it lives.
	const TSharedRef<SWidget> LibrarySlate = Library->TakeWidget();
	Library->SetOwningPlayer(Controller);
	Library->Refresh();

	TestEqual(TEXT("every project in the folder has a card"), Library->GetShownCardCountForTests(), 3);
	TestEqual(TEXT("the count says so"), Library->GetCountTextForTests().ToString(), FString(TEXT("3 projects")));
	if (Library->GetShownCardCountForTests() != 3)
	{
		return false;
	}
	TestEqual(TEXT("newest first"), Library->GetCardsForTests()[0]->GetEntry().DisplayName, FString(TEXT("Alpha Campus")));

	// The sample file cannot be renamed; an ordinary project can.
	UProjectCardWidget* SampleCard = nullptr;
	UProjectCardWidget* AlphaCard = nullptr;
	for (UProjectCardWidget* Card : Library->GetCardsForTests())
	{
		if (Card->GetEntry().bIsSample)
		{
			SampleCard = Card;
		}
		else if (Card->GetEntry().DisplayName == TEXT("Alpha Campus"))
		{
			AlphaCard = Card;
		}
	}
	if (TestNotNull(TEXT("the sample has a card"), SampleCard) && TestNotNull(TEXT("so does Alpha"), AlphaCard))
	{
		TestFalse(TEXT("the sample cannot be renamed"), SampleCard->GetRenameButtonForTests()->GetIsEnabled());
		TestTrue(TEXT("but can be duplicated"), SampleCard->GetDuplicateButtonForTests()->GetIsEnabled());
		TestTrue(TEXT("an ordinary project can be renamed"), AlphaCard->GetRenameButtonForTests()->GetIsEnabled());
		TestFalse(TEXT("the sample cannot be deleted"), SampleCard->GetDeleteButtonForTests()->GetIsEnabled());
		TestTrue(TEXT("an ordinary project can be deleted"), AlphaCard->GetDeleteButtonForTests()->GetIsEnabled());
	}

	// Search narrows the cards by name or file name, without losing the total.
	Library->SetSearchForTests(TEXT("alp"));
	TestEqual(TEXT("search by name"), Library->GetShownCardCountForTests(), 1);
	TestEqual(TEXT("the count shows the narrowing"), Library->GetCountTextForTests().ToString(), FString(TEXT("1 of 3 projects")));
	Library->SetSearchForTests(TEXT("EMPTY.JSON"));
	TestEqual(TEXT("search by file name, ignoring case"), Library->GetShownCardCountForTests(), 1);
	Library->SetSearchForTests(TEXT("zzz"));
	TestEqual(TEXT("no match, no cards"), Library->GetShownCardCountForTests(), 0);
	Library->SetSearchForTests(FString());
	TestEqual(TEXT("clearing the search brings them all back"), Library->GetShownCardCountForTests(), 3);

	// Duplicate adds a card.
	if (AlphaCard)
	{
		Library->DuplicateProject(AlphaCard->GetEntry());
		TestEqual(TEXT("a duplicate shows up"), Library->GetShownCardCountForTests(), 4);
		TestTrue(TEXT("as a file in the folder"), IFileManager::Get().FileExists(*(Folder / TEXT("Alpha Campus copy.json"))));
	}

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

// The start screen and the project library cover the 3D scene, where the buildings of the project that was open still stand: a click on them
// must not reach it (it selected a building, and played the sound of that). The controller is told the pointer is over the screen everywhere.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStartScreensBlockTheSceneTest, "InNavi.UI.StartScreensBlockClicksToTheScene", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStartScreensBlockTheSceneTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	TestFalse(TEXT("no screen covers the scene to begin with"), Controller->IsPointerOverUI());

	UStartScreenWidget* Start = NewObject<UStartScreenWidget>();
	Start->Initialize();
	const TSharedRef<SWidget> StartSlate = Start->TakeWidget();
	Start->SetOwningPlayer(Controller);
	Start->BlockClicksToTheScene();
	TestTrue(TEXT("the start screen covers the scene: a click on it is not a click on a building"), Controller->IsPointerOverUI());

	UProjectLibraryWidget* Library = NewObject<UProjectLibraryWidget>();
	Library->Initialize();
	const TSharedRef<SWidget> LibrarySlate = Library->TakeWidget();
	Library->SetOwningPlayer(Controller);
	Controller->ClearPointerOverUICallback(Start);
	TestFalse(TEXT("leaving the start screen gives the scene its clicks back"), Controller->IsPointerOverUI());
	Library->BlockClicksToTheScene();
	TestTrue(TEXT("the project library covers it too"), Controller->IsPointerOverUI());
	Controller->ClearPointerOverUICallback(Library);
	TestFalse(TEXT("and gives them back"), Controller->IsPointerOverUI());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
