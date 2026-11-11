#if WITH_DEV_AUTOMATION_TESTS

#include "DesktopRenderingSubsystem.h"
#include "HistorySnapshotText.h"
#include "UiTestFixture.h"
#include "StartScreenWidget.h"
#include "ProjectLibraryWidget.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesktopRenderingPresetTest, "InNavi.Desktop.Rendering.PresetRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesktopRenderingPresetTest::RunTest(const FString& Parameters)
{
	auto* Settings = NewObject<UDesktopRenderingSubsystem>(NewObject<UGameInstance>(GEngine));
	Settings->bAvailable = true;
	Settings->SetLightweight(true, false);
	TestTrue(TEXT("lightweight is active"), Settings->IsLightweight());
	TestEqual(TEXT("custom depth selection remains enabled"), IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"))->GetInt(), 3);
	TestEqual(TEXT("Lumen GI is disabled"), IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod"))->GetInt(), 0);
	TestEqual(TEXT("screen-space reflections remain available"), IConsoleManager::Get().FindConsoleVariable(TEXT("r.ReflectionMethod"))->GetInt(), 2);
	const auto Originals = Settings->OriginalSettings;
	Settings->SetLightweight(false, false);
	for (const auto& Setting : Originals)
	{
		TestEqual(TEXT("standard restores each original rendering value"), IConsoleManager::Get().FindConsoleVariable(*Setting.Key)->GetInt(), Setting.Value);
	}
	Settings->SetLightweight(true, false);
	Settings->Deinitialize();
	for (const auto& Setting : Originals)
	{
		TestEqual(TEXT("teardown restores the original values too"), IConsoleManager::Get().FindConsoleVariable(*Setting.Key)->GetInt(), Setting.Value);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesktopRenderingScreenTest, "InNavi.Desktop.Rendering.OpaqueScreenTransitions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesktopRenderingScreenTest::RunTest(const FString& Parameters)
{
	auto* Settings = NewObject<UDesktopRenderingSubsystem>(NewObject<UGameInstance>(GEngine));
	Settings->bAvailable = true;
	auto* Viewport = NewObject<UGameViewportClient>(GEngine);
	auto* Start = NewObject<UStartScreenWidget>();
	auto* Library = NewObject<UProjectLibraryWidget>();
	Viewport->bDisableWorldRendering = false;
	Settings->UpdateOpaqueScreen(Start, true, Viewport);
	TestTrue(TEXT("start screen suspends the covered world"), Viewport->bDisableWorldRendering);
	Settings->UpdateOpaqueScreen(Library, true, Viewport);
	Settings->UpdateOpaqueScreen(Start, false, Viewport);
	TestTrue(TEXT("overlapping transition to library stays suspended"), Viewport->bDisableWorldRendering);
	Settings->UpdateOpaqueScreen(Library, false, Viewport);
	TestFalse(TEXT("opening the editor restores rendering"), Viewport->bDisableWorldRendering);
	Viewport->bDisableWorldRendering = true;
	Settings->UpdateOpaqueScreen(Start, true, Viewport);
	Settings->UpdateOpaqueScreen(Start, false, Viewport);
	TestTrue(TEXT("a previously disabled viewport stays disabled"), Viewport->bDisableWorldRendering);
	Viewport->bDisableWorldRendering = false;
	Settings->UpdateOpaqueScreen(Start, true, Viewport);
	Settings->Deinitialize();
	TestFalse(TEXT("teardown releases the pause"), Viewport->bDisableWorldRendering);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHistorySnapshotTextTest, "InNavi.Interior.History.LosslessTextStorage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHistorySnapshotTextTest::RunTest(const FString& Parameters)
{
	FString Original;
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		Original += FString::Printf(TEXT("{\"id\":\"room-%d\",\"name\":\"\u6559\u5ba4 \U0001F680\",\"footprint\":[[0,0],[20,0],[20,20],[0,20]]}\n"), Index);
	}
	FHistorySnapshotText Stored;
	Stored.Set(Original);
	FString Restored;
	TestTrue(TEXT("large text decompresses"), Stored.Read(Restored));
	TestEqual(TEXT("all Unicode and coordinates survive exactly"), Restored, Original);
	TestTrue(TEXT("history storage is at least halved for layout-shaped text"), Stored.GetStoredBytes() < Stored.GetUncompressedBytes() / 2);
	Stored.Set(TEXT("small \u6559\u5ba4"));
	TestTrue(TEXT("replacement clears the old compressed payload"), Stored.Equals(TEXT("small \u6559\u5ba4")));
	Stored.Set(TEXT(""));
	TestTrue(TEXT("empty text round-trips too"), Stored.Read(Restored) && Restored.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompressedInteriorHistoryTest, "InNavi.Interior.History.CompressedUndoRedo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCompressedInteriorHistoryTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	const FString Id(TEXT("E2-01-01"));
	const FString Original = Fixture.Interior->GetRoomName(Id);
	const int32 Before = Fixture.Interior->InteriorHistory.Num();
	for (int32 Index = 0; Index < 40; ++Index)
	{
		TestTrue(TEXT("rename succeeds"), Fixture.Interior->SetRoomName(Id, FString::Printf(TEXT("Room %d \u6559\u5ba4 \U0001F680"), Index)));
	}
	TestEqual(TEXT("every undo step is retained"), Fixture.Interior->InteriorHistory.Num(), Before + 40);
	Fixture.Interior->RecordHistorySnapshot();
	TestEqual(TEXT("duplicate state is still ignored"), Fixture.Interior->InteriorHistory.Num(), Before + 40);
	uint64 Stored = 0, Uncompressed = 0;
	for (const auto& Snapshot : Fixture.Interior->InteriorHistory)
	{
		Stored += Snapshot.Json.GetStoredBytes();
		Uncompressed += Snapshot.Json.GetUncompressedBytes();
	}
	TestTrue(TEXT("actual interior history uses less than half the text storage"), Stored < Uncompressed / 2);
	for (int32 Index = 38; Index >= -1; --Index)
	{
		TestTrue(TEXT("every edit can be undone"), Fixture.Interior->Undo());
		TestEqual(TEXT("undo restores the exact previous label"), Fixture.Interior->GetRoomName(Id), Index < 0 ? Original : FString::Printf(TEXT("Room %d \u6559\u5ba4 \U0001F680"), Index));
	}
	for (int32 Index = 0; Index < 40; ++Index)
	{
		TestTrue(TEXT("every edit can be redone"), Fixture.Interior->Redo());
		TestEqual(TEXT("redo restores the exact label"), Fixture.Interior->GetRoomName(Id), FString::Printf(TEXT("Room %d \u6559\u5ba4 \U0001F680"), Index));
	}
	TestTrue(TEXT("undo before branching"), Fixture.Interior->Undo());
	TestTrue(TEXT("a new branch can be edited"), Fixture.Interior->SetRoomName(Id, TEXT("New branch")));
	TestFalse(TEXT("new edits discard the old redo branch"), Fixture.Interior->Redo());
	AddInfo(FString::Printf(TEXT("History text storage: %llu bytes instead of %llu bytes, no undo steps removed."), Stored, Uncompressed));
	return true;
}

#endif
