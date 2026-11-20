#if WITH_DEV_AUTOMATION_TESTS

#include "ProjectCardWidget.h"
#include "ProjectLibrary.h"
#include "ProjectFile.h"
#include "ProjectLibraryWidget.h"
#include "StartScreenWidget.h"
#include "UiTestFixture.h"

#include "Components/Button.h"
#include "ImageUtils.h"
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
	WriteLibraryFile(Folder / TEXT("E2_Filled.json"), TEXT("{\"buildings\":[{}]}"), Now - FTimespan::FromDays(2));
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
		TestEqual(TEXT("then the older ones in order"), Entries[1].FileName, FString(TEXT("E2_Filled.json")));
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
	if (const FProjectLibraryEntry* Sample = Find(Entries, TEXT("E2_Filled.json")))
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
	WriteLibraryFile(Folder / TEXT("E2_Filled.json"), TEXT("{\"buildings\":[{}]}"), Now);

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
	TestTrue(TEXT("the shipped sample can be duplicated"), ProjectLibrary::Duplicate(Load(TEXT("E2_Filled.json")), Folder, NewPath, Error));

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
	TestFalse(TEXT("the shipped sample is not renamed"), ProjectLibrary::Rename(Load(TEXT("E2_Filled.json")), TEXT("Mine"), Folder, NewPath, Error));
	TestTrue(TEXT("it is still there"), IFileManager::Get().FileExists(*(Folder / TEXT("E2_Filled.json"))));
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
	WriteLibraryFile(Folder / TEXT("E2_Filled.json"), TEXT("{\"buildings\":[{}]}"), Now);

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

	TestFalse(TEXT("the shipped sample cannot be deleted"), ProjectLibrary::SoftDelete(Load(TEXT("E2_Filled.json")), Folder, DeletedPath, Error));
	TestTrue(TEXT("with the reason"), Error.Contains(TEXT("sample")));
	TestTrue(TEXT("and it is still there"), IFileManager::Get().FileExists(*(Folder / TEXT("E2_Filled.json"))));

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
	WriteLibraryFile(Folder / TEXT("E2_Filled.json"), TEXT("{\"buildings\":[{}]}"), Now - FTimespan::FromDays(2));

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

// The same screens have no project: the keyboard shortcuts do nothing under them (Ctrl+S would save an empty scene, Ctrl+V paste into nothing),
// and a screen that is torn down never unblocks the one that replaced it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStartScreensBlockShortcutsTest, "InNavi.UI.StartScreensBlockKeyShortcuts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStartScreensBlockShortcutsTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	TestFalse(TEXT("shortcuts work to begin with"), Controller->AreKeyShortcutsBlocked());

	UStartScreenWidget* Start = NewObject<UStartScreenWidget>();
	Start->Initialize();
	const TSharedRef<SWidget> StartSlate = Start->TakeWidget();
	Start->SetOwningPlayer(Controller);
	Start->BlockClicksToTheScene();
	TestTrue(TEXT("the start screen blocks them"), Controller->AreKeyShortcutsBlocked());
	TestFalse(TEXT("a key press does not reach the shortcuts"), Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::P, IE_Pressed, 1.f)));
	TestFalse(TEXT("nor a held Ctrl+S"), Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::S, IE_Repeat, 1.f)));

	UProjectLibraryWidget* Library = NewObject<UProjectLibraryWidget>();
	Library->Initialize();
	const TSharedRef<SWidget> LibrarySlate = Library->TakeWidget();
	Library->SetOwningPlayer(Controller);
	Library->BlockClicksToTheScene();
	Controller->UnblockKeyShortcuts(Start);
	TestTrue(TEXT("the start screen going away does not unblock the library"), Controller->AreKeyShortcutsBlocked());
	Controller->UnblockKeyShortcuts(Library);
	TestFalse(TEXT("the library going away gives the shortcuts back"), Controller->AreKeyShortcutsBlocked());
	return true;
}

namespace ProjectLibraryTests
{
	// A picture of three vertical bands: the left 40% red, the next 20% green, the rest blue.
	void WriteBandsPicture(const FString& Path, int32 Width, int32 Height)
	{
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				Pixels[Y * Width + X] = X < Width * 4 / 10 ? FColor(255, 0, 0, 255) : (X < Width * 6 / 10 ? FColor(0, 255, 0, 255) : FColor(0, 0, 255, 255));
			}
		}
		FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), Width, Height));
	}

	void WriteSolidPicture(const FString& Path, int32 Width, int32 Height, const FColor& Colour)
	{
		TArray<FColor> Pixels;
		Pixels.Init(Colour, Width * Height);
		FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), Width, Height));
	}

	FColor PixelOf(const FString& Path, int32 X, int32 Y, int32& OutWidth, int32& OutHeight)
	{
		FImage Image;
		if (!FImageUtils::LoadImage(*Path, Image)) return FColor::Black;
		Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		OutWidth = Image.SizeX;
		OutHeight = Image.SizeY;
		return Image.AsBGRA8()[static_cast<int64>(Y) * Image.SizeX + X];
	}
}

// The cover picture of a project card: cut to the card's proportions from the middle of the picture, kept as a PNG of its own in Data/Covers (the project file is
// not touched), replaced, removed, and carried along when the project is duplicated, renamed or deleted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectLibraryCoverTest, "InNavi.Project.Library.Cover", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectLibraryCoverTest::RunTest(const FString& Parameters)
{
	using namespace ProjectLibraryTests;
	const FString Folder = MakeFolder();
	IFileManager::Get().MakeDirectory(*Folder, true);
	const FDateTime Now = FDateTime(2026, 10, 20, 12, 0, 0);
	const FString ProjectText = TEXT("{\"projectName\":\"Alpha\",\"buildings\":[]}");
	WriteLibraryFile(Folder / TEXT("alpha.json"), ProjectText, Now);
	WriteBandsPicture(Folder / TEXT("source.png"), 1000, 300);
	FString Error;

	TArray<FProjectLibraryEntry> Entries = ProjectLibrary::Scan(Folder);
	if (!TestEqual(TEXT("the project is listed"), Entries.Num(), 1)) return false;
	TestTrue(TEXT("a project starts without a cover"), Entries[0].CoverPath.IsEmpty());
	TestEqual(TEXT("its cover would be kept in Data/Covers under the file name"), ProjectLibrary::CoverPathFor(Folder, TEXT("alpha.json")), Folder / TEXT("Covers") / TEXT("alpha.json.png"));

	// A wide picture: the card's proportions are cut from its middle (the red and the blue edges are cut away unevenly, the green is in the middle).
	TestTrue(TEXT("a picture becomes the cover"), ProjectLibrary::SetCover(Entries[0], Folder / TEXT("source.png"), Folder, Error));
	TestTrue(TEXT("no error"), Error.IsEmpty());
	const FString CoverPath = ProjectLibrary::CoverPathFor(Folder, TEXT("alpha.json"));
	int32 W = 0, H = 0;
	const FColor Left = PixelOf(CoverPath, 10, ProjectLibrary::CoverHeight / 2, W, H);
	const FColor Middle = PixelOf(CoverPath, ProjectLibrary::CoverWidth / 2, ProjectLibrary::CoverHeight / 2, W, H);
	const FColor Right = PixelOf(CoverPath, ProjectLibrary::CoverWidth - 10, ProjectLibrary::CoverHeight / 2, W, H);
	TestTrue(TEXT("the cover has the size of the card's picture, twice over"), W == ProjectLibrary::CoverWidth && H == ProjectLibrary::CoverHeight);
	TestTrue(TEXT("its left is the red of the picture"), Left.R > 200 && Left.G < 60 && Left.B < 60);
	TestTrue(TEXT("its middle is the green"), Middle.G > 200 && Middle.R < 60 && Middle.B < 60);
	TestTrue(TEXT("its right is the blue"), Right.B > 200 && Right.R < 60 && Right.G < 60);
	TestTrue(TEXT("the picture that was chosen is left as it is"), IFileManager::Get().FileExists(*(Folder / TEXT("source.png"))));
	FString ProjectAfter;
	FFileHelper::LoadFileToString(ProjectAfter, *(Folder / TEXT("alpha.json")));
	TestEqual(TEXT("and so is the project file"), ProjectAfter, ProjectText);

	Entries = ProjectLibrary::Scan(Folder);
	TestEqual(TEXT("the library finds the cover"), Entries[0].CoverPath, CoverPath);

	// A small picture is scaled up; a new one replaces the old cover.
	WriteSolidPicture(Folder / TEXT("small.png"), 40, 30, FColor(250, 250, 250, 255));
	TestTrue(TEXT("a small picture works too"), ProjectLibrary::SetCover(Entries[0], Folder / TEXT("small.png"), Folder, Error));
	const FColor Replaced = PixelOf(CoverPath, 300, 100, W, H);
	TestTrue(TEXT("and replaces the old cover"), Replaced.R > 240 && Replaced.G > 240 && Replaced.B > 240 && W == ProjectLibrary::CoverWidth);
	TestFalse(TEXT("no half-written file is left behind"), IFileManager::Get().FileExists(*(Folder / TEXT("Covers") / TEXT("alpha.json.new.png"))));

	// What is refused, with the reason, and the old cover stays.
	WriteLibraryFile(Folder / TEXT("notes.txt"), TEXT("hello"), Now);
	WriteLibraryFile(Folder / TEXT("broken.png"), TEXT("this is not a picture"), Now);
	TestFalse(TEXT("a file that is not a picture is refused"), ProjectLibrary::SetCover(Entries[0], Folder / TEXT("notes.txt"), Folder, Error));
	TestFalse(TEXT("and says why"), Error.IsEmpty());
	TestFalse(TEXT("a broken picture is refused"), ProjectLibrary::SetCover(Entries[0], Folder / TEXT("broken.png"), Folder, Error));
	TestFalse(TEXT("so is a missing one"), ProjectLibrary::SetCover(Entries[0], Folder / TEXT("nothing.png"), Folder, Error));
	TestTrue(TEXT("the old cover is still there"), IFileManager::Get().FileExists(*CoverPath));

	// A sample or a database can have a cover too: it is a file of its own.
	FProjectLibraryEntry Sample;
	Sample.FileName = TEXT("map_extended.db");
	Sample.bIsSample = true;
	Sample.bIsDatabase = true;
	TestTrue(TEXT("a database can have a cover"), ProjectLibrary::SetCover(Sample, Folder / TEXT("source.png"), Folder, Error));
	TestTrue(TEXT("kept under its own name"), IFileManager::Get().FileExists(*ProjectLibrary::CoverPathFor(Folder, TEXT("map_extended.db"))));

	// Taking it away.
	TestTrue(TEXT("a cover can be removed"), ProjectLibrary::RemoveCover(Entries[0], Folder, Error));
	TestFalse(TEXT("the file is gone"), IFileManager::Get().FileExists(*CoverPath));
	TestTrue(TEXT("removing it again is no error"), ProjectLibrary::RemoveCover(Entries[0], Folder, Error));
	TestTrue(TEXT("the library shows the drawing again"), ProjectLibrary::Scan(Folder).FilterByPredicate([](const FProjectLibraryEntry& E) { return E.FileName == TEXT("alpha.json"); })[0].CoverPath.IsEmpty());

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

// The cover goes with the project: a copy has the same one, a renamed project keeps it, a deleted one is put away with it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectLibraryCoverFollowsTest, "InNavi.Project.Library.CoverFollowsTheProject", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectLibraryCoverFollowsTest::RunTest(const FString& Parameters)
{
	using namespace ProjectLibraryTests;
	const FString Folder = MakeFolder();
	IFileManager::Get().MakeDirectory(*Folder, true);
	WriteLibraryFile(Folder / TEXT("alpha.json"), TEXT("{\"projectName\":\"Alpha\",\"buildings\":[]}"), FDateTime(2026, 10, 20, 12, 0, 0));
	WriteSolidPicture(Folder / TEXT("source.png"), 800, 400, FColor(10, 200, 30, 255));
	FString Error, NewPath, DeletedPath;
	auto AlphaEntry = [&Folder]() { return ProjectLibrary::Scan(Folder).FilterByPredicate([](const FProjectLibraryEntry& E) { return E.FileName == TEXT("alpha.json"); })[0]; };
	TestTrue(TEXT("a cover is set"), ProjectLibrary::SetCover(AlphaEntry(), Folder / TEXT("source.png"), Folder, Error));

	TestTrue(TEXT("the project is duplicated"), ProjectLibrary::Duplicate(AlphaEntry(), Folder, NewPath, Error));
	TestTrue(TEXT("the copy has the cover too"), IFileManager::Get().FileExists(*ProjectLibrary::CoverPathFor(Folder, FPaths::GetCleanFilename(NewPath))));
	TestTrue(TEXT("and the original keeps its own"), IFileManager::Get().FileExists(*ProjectLibrary::CoverPathFor(Folder, TEXT("alpha.json"))));

	TestTrue(TEXT("the project is renamed"), ProjectLibrary::Rename(AlphaEntry(), TEXT("Beta"), Folder, NewPath, Error));
	TestTrue(TEXT("the cover has the new file name"), IFileManager::Get().FileExists(*ProjectLibrary::CoverPathFor(Folder, TEXT("Beta.json"))));
	TestFalse(TEXT("and is not left under the old one"), IFileManager::Get().FileExists(*ProjectLibrary::CoverPathFor(Folder, TEXT("alpha.json"))));

	const FProjectLibraryEntry Beta = ProjectLibrary::Scan(Folder).FilterByPredicate([](const FProjectLibraryEntry& E) { return E.FileName == TEXT("Beta.json"); })[0];
	TestFalse(TEXT("the library finds it under the new name"), Beta.CoverPath.IsEmpty());
	TestTrue(TEXT("the project is deleted"), ProjectLibrary::SoftDelete(Beta, Folder, DeletedPath, Error));
	TestFalse(TEXT("its cover is no longer in the covers"), IFileManager::Get().FileExists(*ProjectLibrary::CoverPathFor(Folder, TEXT("Beta.json"))));
	TestTrue(TEXT("it is in the Deleted folder beside the project"), IFileManager::Get().FileExists(*(DeletedPath + TEXT(".png"))));

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

// The picture area of a card: a button that asks for a cover; with a cover it shows the picture and a cross to take it away; the hint shows over it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCardCoverTest, "InNavi.UI.ProjectLibrary.Cover", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectCardCoverTest::RunTest(const FString& Parameters)
{
	using namespace ProjectLibraryTests;
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	const FString Folder = MakeFolder();
	IFileManager::Get().MakeDirectory(*Folder, true);
	Controller->SetProjectsDirectoryOverrideForTests(Folder);
	const FDateTime Now = FDateTime(2026, 10, 20, 12, 0, 0);
	WriteLibraryFile(Folder / TEXT("alpha.json"), TEXT("{\"projectName\":\"Alpha\",\"buildings\":[{}]}"), Now);
	WriteLibraryFile(Folder / TEXT("beta.json"), TEXT("{\"projectName\":\"Beta\",\"buildings\":[{}]}"), Now - FTimespan::FromDays(1));
	WriteSolidPicture(Folder / TEXT("source.png"), 800, 400, FColor(10, 200, 30, 255));
	FString Error;
	const FProjectLibraryEntry Alpha = ProjectLibrary::Scan(Folder).FilterByPredicate([](const FProjectLibraryEntry& E) { return E.FileName == TEXT("alpha.json"); })[0];
	ProjectLibrary::SetCover(Alpha, Folder / TEXT("source.png"), Folder, Error);

	UProjectLibraryWidget* Library = NewObject<UProjectLibraryWidget>();
	Library->Initialize();
	const TSharedRef<SWidget> LibrarySlate = Library->TakeWidget();
	Library->SetOwningPlayer(Controller);
	Library->Refresh();
	auto CardOf = [&Library](const TCHAR* FileName) -> UProjectCardWidget*
	{
		for (UProjectCardWidget* Card : Library->GetCardsForTests()) if (Card->GetEntry().FileName == FileName) return Card;
		return nullptr;
	};
	UProjectCardWidget* WithCover = CardOf(TEXT("alpha.json"));
	UProjectCardWidget* Without = CardOf(TEXT("beta.json"));
	if (!TestNotNull(TEXT("the card with a cover"), WithCover) || !TestNotNull(TEXT("the card without"), Without)) return false;

	TestTrue(TEXT("a project with a cover shows it"), WithCover->ShowsCoverForTests());
	TestFalse(TEXT("instead of the drawing"), WithCover->ShowsDrawingForTests());
	TestNotNull(TEXT("with a cross to remove it"), WithCover->GetRemoveCoverButtonForTests());
	TestFalse(TEXT("one without shows the drawing"), Without->ShowsCoverForTests());
	TestTrue(TEXT("(the drawing)"), Without->ShowsDrawingForTests());
	TestNull(TEXT("and has nothing to remove"), Without->GetRemoveCoverButtonForTests());
	TestNotNull(TEXT("every card has the button over its picture"), Without->GetPictureButtonForTests());

	// The hint over the picture while the pointer is on it.
	TestFalse(TEXT("no hint until the pointer is on the picture"), Without->ShowsHoverHintForTests());
	Without->HoverPictureForTests(true);
	TestTrue(TEXT("the hint shows over it"), Without->ShowsHoverHintForTests());
	Without->HoverPictureForTests(false);
	TestFalse(TEXT("and goes when the pointer leaves"), Without->ShowsHoverHintForTests());

	// A click on the picture asks for a cover for that project.
	FString AskedFor;
	Without->OnChangeCover = [&AskedFor](const FProjectLibraryEntry& Entry) { AskedFor = Entry.FileName; };
	Without->GetPictureButtonForTests()->OnClicked.Broadcast();
	TestEqual(TEXT("a click on the picture asks for a cover for its project"), AskedFor, FString(TEXT("beta.json")));

	// What the screen does with the answer.
	Library->SetCoverFromFile(Without->GetEntry(), Folder / TEXT("source.png"));
	TestTrue(TEXT("a picture chosen for Beta is kept"), IFileManager::Get().FileExists(*ProjectLibrary::CoverPathFor(Folder, TEXT("beta.json"))));
	TestTrue(TEXT("and its card shows it"), CardOf(TEXT("beta.json")) && CardOf(TEXT("beta.json"))->ShowsCoverForTests());
	Library->SetCoverFromFile(Without->GetEntry(), Folder / TEXT("nothing.png"));
	TestFalse(TEXT("a picture that cannot be read says so on the screen"), Library->GetMessageTextForTests().IsEmpty());
	Library->RemoveCoverOf(Alpha);
	TestFalse(TEXT("the cross takes Alpha's cover away"), IFileManager::Get().FileExists(*ProjectLibrary::CoverPathFor(Folder, TEXT("alpha.json"))));
	TestTrue(TEXT("and its card shows the drawing again"), CardOf(TEXT("alpha.json")) && CardOf(TEXT("alpha.json"))->ShowsDrawingForTests());

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
