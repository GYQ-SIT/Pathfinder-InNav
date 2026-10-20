#if WITH_DEV_AUTOMATION_TESTS

#include "NewProjectDialogWidget.h"
#include "UiTestFixture.h"

#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// New project: the file it writes and what the dialog does with the answer.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNewProjectCreateTest, "InNavi.Project.CreateNew", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNewProjectCreateTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();

	// Never touch the real Data folder.
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("NewProject_") + FGuid::NewGuid().ToString());
	Controller->SetProjectsDirectoryOverrideForTests(Directory);

	FString Error;
	TestTrue(TEXT("a name and a description create a project"), Controller->CreateNewProject(TEXT("  Campus Navigation  "), TEXT("  Demo of the flow  "), Error));
	TestEqual(TEXT("no error is reported"), Error, FString());

	const FString File = Directory / TEXT("Campus Navigation.json");
	TestTrue(TEXT("a blank project file is written, named after the project"), IFileManager::Get().FileExists(*File));
	TestEqual(TEXT("it is the file Ctrl+S saves to"), Controller->GetCurrentProjectFilePath(), File);
	TestFalse(TEXT("a project that was just created has nothing unsaved"), Controller->HasUnsavedChanges());
	TestEqual(TEXT("the project keeps its trimmed name"), Fixture.Generator->GetProjectName(), FString(TEXT("Campus Navigation")));
	TestEqual(TEXT("and its trimmed description"), Fixture.Generator->GetProjectDescription(), FString(TEXT("Demo of the flow")));
	TestEqual(TEXT("it starts without buildings"), Fixture.Generator->GetAllBuildings().Num(), 0);

	FString Json;
	FFileHelper::LoadFileToString(Json, *File);
	TSharedPtr<FJsonObject> Root;
	TestTrue(TEXT("the file is valid JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid());
	if (Root.IsValid())
	{
		TestEqual(TEXT("the name is in the file"), Root->GetStringField(TEXT("projectName")), FString(TEXT("Campus Navigation")));
		TestEqual(TEXT("the description is in the file"), Root->GetStringField(TEXT("description")), FString(TEXT("Demo of the flow")));
		const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
		TestTrue(TEXT("it has a buildings list"), Root->TryGetArrayField(TEXT("buildings"), Buildings));
		TestTrue(TEXT("which is empty"), Buildings && Buildings->IsEmpty());
	}

	// Nothing is ever overwritten.
	const int64 SizeBefore = IFileManager::Get().FileSize(*File);
	TestFalse(TEXT("a second project with the same name is refused"), Controller->CreateNewProject(TEXT("campus navigation"), TEXT("other"), Error));
	TestTrue(TEXT("and the error says so"), Error.Contains(TEXT("already exists")));
	TestEqual(TEXT("the existing file is untouched"), IFileManager::Get().FileSize(*File), SizeBefore);

	TestFalse(TEXT("an empty name is refused"), Controller->CreateNewProject(TEXT("   "), FString(), Error));
	TestEqual(TEXT("with its own message"), Error, FString(TEXT("Enter a project name.")));
	TestFalse(TEXT("a name with nothing usable in it is refused"), Controller->CreateNewProject(TEXT("???"), FString(), Error));
	TestTrue(TEXT("with its own message"), Error.Contains(TEXT("letter or number")));

	// Characters Windows does not allow in a file name are replaced; the project keeps the name as typed.
	TestTrue(TEXT("a name with forbidden characters still works"), Controller->CreateNewProject(TEXT("Block A/B: east?"), FString(), Error));
	const FString Odd = Controller->GetProjectFilePathForName(TEXT("Block A/B: east?"));
	bool bHasForbidden = false;
	for (const TCHAR Character : FPaths::GetCleanFilename(Odd))
	{
		bHasForbidden |= FString(TEXT("\\/:*?\"<>|")).Contains(FString::Chr(Character));
	}
	TestFalse(TEXT("the file name has none of them"), bHasForbidden);
	TestTrue(TEXT("and the file exists in the projects folder"), IFileManager::Get().FileExists(*Odd) && FPaths::GetPath(Odd) == Directory);
	TestEqual(TEXT("the project name is kept as typed"), Fixture.Generator->GetProjectName(), FString(TEXT("Block A/B: east?")));

	// Such a project opens again: no buildings, but not a broken file either - and the name comes back.
	Controller->CreateNewProject(TEXT("Something else"), FString(), Error);
	TestEqual(TEXT("opening the blank project spawns nothing"), Controller->LoadBuildingDataFromPath(File), 0);
	TestEqual(TEXT("but it is accepted as the current project"), Controller->GetCurrentProjectFilePath(), File);
	TestEqual(TEXT("with its name"), Fixture.Generator->GetProjectName(), FString(TEXT("Campus Navigation")));
	TestEqual(TEXT("and its description"), Fixture.Generator->GetProjectDescription(), FString(TEXT("Demo of the flow")));

	// A project saved with buildings keeps its name too.
	Controller->StartNewProject();
	TestEqual(TEXT("a fresh New starts without a name"), Fixture.Generator->GetProjectName(), FString());

	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNewProjectDialogTest, "InNavi.UI.NewProjectDialog", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNewProjectDialogTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();

	UNewProjectDialogWidget* Dialog = NewObject<UNewProjectDialogWidget>();
	Dialog->Initialize();
	Dialog->TakeWidget();
	Dialog->SetOwningPlayer(Controller);
	if (!TestNotNull(TEXT("the name box exists"), Dialog->GetNameBoxForTests()) || !TestNotNull(TEXT("the description box exists"), Dialog->GetDescriptionBoxForTests()))
	{
		return false;
	}
	TestNull(TEXT("there is no floor count field any more"), Dialog->WidgetTree->FindWidget(TEXT("Txt_FloorCount")));

	FString GotName, GotDescription;
	int32 Calls = 0;
	FString Answer = TEXT("A project with that name already exists.");
	Dialog->OnCreate = [&](const FString& Name, const FString& Description) -> FString
	{
		++Calls;
		GotName = Name;
		GotDescription = Description;
		return Answer;
	};
	bool bCancelled = false;
	Dialog->OnCancelled = [&]() { bCancelled = true; };

	Dialog->GetNameBoxForTests()->SetText(FText::FromString(TEXT("Alpha")));
	Dialog->GetDescriptionBoxForTests()->SetText(FText::FromString(TEXT("First line")));
	TestFalse(TEXT("no message before anything was submitted"), Dialog->IsErrorShownForTests());

	Dialog->Submit();
	TestEqual(TEXT("Create hands over the name"), GotName, FString(TEXT("Alpha")));
	TestEqual(TEXT("and the description"), GotDescription, FString(TEXT("First line")));
	TestTrue(TEXT("a refusal is shown under the name"), Dialog->IsErrorShownForTests());
	TestEqual(TEXT("with the reason"), Dialog->GetErrorTextForTests().ToString(), Answer);

	// Editing the name takes the stale message away.
	Dialog->GetNameBoxForTests()->OnTextChanged.Broadcast(FText::FromString(TEXT("Alpha 2")));
	TestFalse(TEXT("editing the name hides the message"), Dialog->IsErrorShownForTests());

	Answer.Reset();
	Dialog->Submit();
	TestEqual(TEXT("a second try calls Create again"), Calls, 2);
	TestFalse(TEXT("and shows nothing when it worked"), Dialog->IsErrorShownForTests());

	Dialog->Cancel();
	TestTrue(TEXT("Cancel tells whoever opened the dialog"), bCancelled);
	return true;
}

// Every project file says which format version wrote it, and older or newer files still open.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectFileVersionTest, "InNavi.Project.FileVersion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectFileVersionTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("FileVersion_") + FGuid::NewGuid().ToString());

	// A file from before versions existed has no "version".
	const FString Legacy = Directory / TEXT("legacy.json");
	FFileHelper::SaveStringToFile(TEXT("{\"scale\":1,\"floorHeight\":300,\"buildings\":[]}"), *Legacy);
	Fixture.Generator->LoadBuildingsFromFile(Legacy);
	TestEqual(TEXT("a file without a version is read as version 0"), Fixture.Generator->GetLoadedFileVersion(), 0);

	// What is saved carries the current version, and reads back as it.
	const FString Saved = Directory / TEXT("saved.json");
	TestTrue(TEXT("the project saves"), Fixture.Generator->SaveBuildingsToFile(Saved));
	FString Json;
	FFileHelper::LoadFileToString(Json, *Saved);
	TSharedPtr<FJsonObject> Root;
	TestTrue(TEXT("the file is valid JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid());
	if (Root.IsValid())
	{
		TestEqual(TEXT("it holds the current version"), (int32)Root->GetNumberField(TEXT("version")), ABuildingGenerator::ProjectFileVersion);
	}
	Fixture.Interior->ClearLoadedInterior();
	Fixture.Generator->ClearAllBuildings();
	TestTrue(TEXT("the saved file loads"), Fixture.Generator->LoadBuildingsFromFile(Saved) > 0);
	TestEqual(TEXT("and reads back as it"), Fixture.Generator->GetLoadedFileVersion(), ABuildingGenerator::ProjectFileVersion);

	// A file from a newer build still opens (it is read as well as it can be).
	const FString Future = Directory / TEXT("future.json");
	FFileHelper::SaveStringToFile(TEXT("{\"version\":99,\"scale\":1,\"buildings\":[]}"), *Future);
	AddExpectedError(TEXT("newer than this build reads"), EAutomationExpectedErrorFlags::Contains, 0);
	Fixture.Generator->LoadBuildingsFromFile(Future);
	TestEqual(TEXT("a newer file reports its own version"), Fixture.Generator->GetLoadedFileVersion(), 99);

	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
