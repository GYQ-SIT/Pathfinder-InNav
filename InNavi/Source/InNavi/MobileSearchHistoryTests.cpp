#if WITH_DEV_AUTOMATION_TESTS

#include "MobileSearchHistory.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

// The recent searches of the full-screen search: what is kept, in which order, how many, and that they come back from a file.

namespace
{
	FString TempHistoryFile()
	{
		return FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("SearchHistory_") + FGuid::NewGuid().ToString() + TEXT(".json"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSearchHistoryKeysTest, "InNavi.Mobile.SearchHistory.Keys", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSearchHistoryKeysTest::RunTest(const FString& Parameters)
{
	FMobileSearchHistory History;
	TestEqual(TEXT("nothing at first"), History.Chosen().Num() + History.Queries().Num(), 0);

	TestTrue(TEXT("a building is remembered"), History.AddChosen(FMobileSearchHistory::BuildingKey(TEXT("E2"))));
	History.AddChosen(FMobileSearchHistory::PlaceKey(TEXT("E2-01-101")));
	History.AddChosen(FMobileSearchHistory::BuildingKey(TEXT("W2")));
	TestEqual(TEXT("newest first"), FString::Join(History.Chosen(), TEXT(",")), FString(TEXT("building:W2,place:E2-01-101,building:E2")));
	History.AddChosen(FMobileSearchHistory::BuildingKey(TEXT("E2")));
	TestEqual(TEXT("one chosen again goes to the top, once"), FString::Join(History.Chosen(), TEXT(",")), FString(TEXT("building:E2,building:W2,place:E2-01-101")));

	TestFalse(TEXT("an empty key is nothing"), History.AddChosen(FString()));
	TestFalse(TEXT("so is a key of no known kind"), History.AddChosen(TEXT("site:sit")));
	TestFalse(TEXT("and a key with no id"), History.AddChosen(TEXT("place:")));
	TestEqual(TEXT("none of them was kept"), History.Chosen().Num(), 3);

	FString Kind, Id;
	TestTrue(TEXT("a key is taken apart"), FMobileSearchHistory::ParseKey(TEXT("place:E2-01-101"), Kind, Id));
	TestTrue(TEXT("into its kind and its id"), Kind == TEXT("place") && Id == TEXT("E2-01-101"));
	TestTrue(TEXT("an id may hold a colon"), FMobileSearchHistory::ParseKey(TEXT("place:a:b"), Kind, Id) && Id == TEXT("a:b"));

	for (int32 Index = 0; Index < 12; ++Index)
	{
		History.AddChosen(FMobileSearchHistory::PlaceKey(FString::Printf(TEXT("P%d"), Index)));
	}
	TestEqual(TEXT("only the last eight are kept"), History.Chosen().Num(), FMobileSearchHistory::MaxEntries);
	TestEqual(TEXT("with the newest on top"), History.Chosen()[0], FString(TEXT("place:P11")));
	TestEqual(TEXT("and the oldest of them last"), History.Chosen().Last(), FString(TEXT("place:P4")));

	TestTrue(TEXT("clearing the chosen"), History.ClearChosen());
	TestEqual(TEXT("empties them"), History.Chosen().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSearchHistoryQueriesTest, "InNavi.Mobile.SearchHistory.Queries", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSearchHistoryQueriesTest::RunTest(const FString& Parameters)
{
	FMobileSearchHistory History;
	TestTrue(TEXT("words are remembered"), History.AddQuery(TEXT("punggol")));
	History.AddQuery(TEXT("  mall  "));
	TestEqual(TEXT("trimmed, newest first"), FString::Join(History.Queries(), TEXT("|")), FString(TEXT("mall|punggol")));
	History.AddQuery(TEXT("PUNGGOL"));
	TestEqual(TEXT("the same words in other letters are one entry, the newest spelling"), FString::Join(History.Queries(), TEXT("|")), FString(TEXT("PUNGGOL|mall")));
	TestFalse(TEXT("nothing typed is nothing"), History.AddQuery(TEXT("   ")));
	TestEqual(TEXT("and was not kept"), History.Queries().Num(), 2);

	const FString Long = FString::ChrN(200, TEXT('a'));
	History.AddQuery(Long);
	TestEqual(TEXT("a long query is cut"), History.Queries()[0].Len(), FMobileSearchHistory::MaxQueryLength);

	for (int32 Index = 0; Index < 12; ++Index)
	{
		History.AddQuery(FString::Printf(TEXT("q%d"), Index));
	}
	TestEqual(TEXT("only the last eight are kept"), History.Queries().Num(), FMobileSearchHistory::MaxEntries);

	History.AddChosen(FMobileSearchHistory::PlaceKey(TEXT("x")));
	TestTrue(TEXT("clearing the queries"), History.ClearQueries());
	TestTrue(TEXT("leaves the chosen alone"), History.Queries().IsEmpty() && History.Chosen().Num() == 1);
	History.AddQuery(TEXT("again"));
	TestTrue(TEXT("clearing everything"), History.ClearAll());
	TestTrue(TEXT("empties both"), History.Queries().IsEmpty() && History.Chosen().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSearchHistoryFileTest, "InNavi.Mobile.SearchHistory.File", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSearchHistoryFileTest::RunTest(const FString& Parameters)
{
	FMobileSearchHistory Without;
	Without.AddQuery(TEXT("a"));
	TestFalse(TEXT("without a file nothing is written"), Without.Save());

	const FString Path = TempHistoryFile();
	{
		FMobileSearchHistory Kept;
		Kept.UseFile(Path);
		TestTrue(TEXT("a file that is not there is empty lists"), Kept.Chosen().IsEmpty() && Kept.Queries().IsEmpty());
		Kept.AddChosen(FMobileSearchHistory::BuildingKey(TEXT("E2")));
		Kept.AddChosen(FMobileSearchHistory::PlaceKey(TEXT("E2-01-101")));
		Kept.AddQuery(TEXT("punggol"));
		Kept.AddQuery(TEXT("mall"));
	}
	{
		FMobileSearchHistory Next;
		Next.UseFile(Path);
		TestEqual(TEXT("the chosen come back in order"), FString::Join(Next.Chosen(), TEXT(",")), FString(TEXT("place:E2-01-101,building:E2")));
		TestEqual(TEXT("so do the queries"), FString::Join(Next.Queries(), TEXT(",")), FString(TEXT("mall,punggol")));
		Next.ClearAll();
	}
	{
		FMobileSearchHistory Cleared;
		Cleared.UseFile(Path);
		TestTrue(TEXT("a clear is written too"), Cleared.Chosen().IsEmpty() && Cleared.Queries().IsEmpty());
	}

	// A damaged file, and a file with entries of no kind we know, are read as far as they make sense and never crash.
	FFileHelper::SaveStringToFile(TEXT("{ not json"), *Path);
	FMobileSearchHistory Damaged;
	Damaged.UseFile(Path);
	TestTrue(TEXT("a damaged file is empty lists"), Damaged.Chosen().IsEmpty() && Damaged.Queries().IsEmpty());
	FFileHelper::SaveStringToFile(TEXT("{\"version\":1,\"chosen\":[\"building:E2\",\"junk\",7,\"building:E2\"],\"queries\":[\"x\",\"X\",\"\"]}"), *Path);
	FMobileSearchHistory Odd;
	Odd.UseFile(Path);
	TestEqual(TEXT("only the good, different keys are read"), FString::Join(Odd.Chosen(), TEXT(",")), FString(TEXT("building:E2")));
	TestEqual(TEXT("and one of each spelling of a query"), FString::Join(Odd.Queries(), TEXT(",")), FString(TEXT("x")));
	IFileManager::Get().Delete(*Path);
	return true;
}

#endif
