#if WITH_DEV_AUTOMATION_TESTS

#include "DesktopRenderingSubsystem.h"
#include "HistorySnapshotText.h"
#include "UiTestFixture.h"
#include "StartScreenWidget.h"
#include "ProjectLibraryWidget.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "RenderUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesktopRenderingDefaultsTest, "InNavi.Desktop.Rendering.StandardByDefault", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesktopRenderingDefaultsTest::RunTest(const FString& Parameters)
{
	FConfigFile Defaults;
	Defaults.Read(FPaths::ProjectConfigDir() / TEXT("DefaultGameUserSettings.ini"));
	bool bShippedLightweight = true;
	TestTrue(TEXT("shipped desktop rendering preference exists"), Defaults.GetBool(TEXT("InNavi.DesktopRendering"), TEXT("Lightweight"), bShippedLightweight));
	TestFalse(TEXT("shipped preference explicitly selects Standard"), bShippedLightweight);
	TestFalse(TEXT("fresh installs preserve Standard rendering"), UDesktopRenderingSubsystem::ResolveLightweightPreference(&Defaults, TEXT("")));
	TestFalse(TEXT("missing settings fall back to Standard"), UDesktopRenderingSubsystem::ResolveLightweightPreference(nullptr, TEXT("")));
	FConfigFile Saved;
	TestFalse(TEXT("a missing preference also keeps Standard"), UDesktopRenderingSubsystem::ResolveLightweightPreference(&Saved, TEXT("")));
	Saved.SetBool(TEXT("InNavi.DesktopRendering"), TEXT("Lightweight"), true);
	TestTrue(TEXT("an explicit saved Lightweight choice survives"), UDesktopRenderingSubsystem::ResolveLightweightPreference(&Saved, TEXT("")));
	TestFalse(TEXT("Standard command-line override wins over a saved choice"), UDesktopRenderingSubsystem::ResolveLightweightPreference(&Saved, TEXT("-DesktopRenderPreset=Standard")));
	Saved.SetBool(TEXT("InNavi.DesktopRendering"), TEXT("Lightweight"), false);
	TestTrue(TEXT("Lightweight remains available as an explicit override"), UDesktopRenderingSubsystem::ResolveLightweightPreference(&Saved, TEXT("-DesktopRenderPreset=Lightweight")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesktopRenderingPresetTest, "InNavi.Desktop.Rendering.PresetRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDesktopRenderingPresetTest::RunTest(const FString& Parameters)
{
	auto* Settings = NewObject<UDesktopRenderingSubsystem>(NewObject<UGameInstance>(GEngine));
	Settings->bAvailable = true;
	auto* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable"));
	auto* RayTracing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Enable"));
	if (!TestNotNull(TEXT("VSM runtime switch exists"), Shadows) || !TestNotNull(TEXT("RT runtime switch exists"), RayTracing)) return false;
	TestFalse(TEXT("VSM switch is writable at runtime"), Shadows->TestFlags(ECVF_ReadOnly));
	TestTrue(TEXT("VSM switch is render-thread safe"), Shadows->TestFlags(ECVF_RenderThreadSafe));
	TestFalse(TEXT("RT switch is writable"), RayTracing->TestFlags(ECVF_ReadOnly));
	const int32 OriginalShadows = Shadows->GetInt(), OriginalRayTracing = RayTracing->GetInt();
	const bool bOriginalRTEnabled = IsRayTracingEnabled();
	Settings->SetLightweight(false, false);
	TestTrue(TEXT("Standard startup does not override or capture engine rendering settings"), Settings->OriginalSettings.IsEmpty());
	Settings->SetLightweight(true, false);
	TestTrue(TEXT("lightweight is active"), Settings->IsLightweight());
	TestEqual(TEXT("custom depth selection remains enabled"), IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"))->GetInt(), 3);
	TestEqual(TEXT("Lumen GI is disabled"), IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod"))->GetInt(), 0);
	TestEqual(TEXT("screen-space reflections remain available"), IConsoleManager::Get().FindConsoleVariable(TEXT("r.ReflectionMethod"))->GetInt(), 2);
	TestEqual(TEXT("Lightweight disables VSM at runtime"), Shadows->GetInt(), 0);
	if (IsRayTracingEnableOnDemandSupported())
	{
		TestEqual(TEXT("on-demand RT switch changes to zero"), RayTracing->GetInt(), 0);
		TestFalse(TEXT("engine confirms RT is actually disabled"), IsRayTracingEnabled());
	}
	else
	{
		TestEqual(TEXT("startup-only RT switch is left untouched"), RayTracing->GetInt(), OriginalRayTracing);
		TestEqual(TEXT("startup-only RT state stays unchanged"), IsRayTracingEnabled(), bOriginalRTEnabled);
		TestFalse(TEXT("startup-only RT is excluded from the restoration list"), Settings->OriginalSettings.Contains(TEXT("r.RayTracing.Enable")));
	}
	AddInfo(FString::Printf(TEXT("Runtime renderer check: RT allowed=%d, on-demand=%d, effective RT before=%d, Lightweight=%d; VSM %d -> %d."), IsRayTracingAllowed(), IsRayTracingEnableOnDemandSupported(), bOriginalRTEnabled, IsRayTracingEnabled(), OriginalShadows, Shadows->GetInt()));
	const auto Originals = Settings->OriginalSettings;
	Settings->SetLightweight(false, false);
	TestEqual(TEXT("Standard restores effective RT, not just its console variable"), IsRayTracingEnabled(), bOriginalRTEnabled);
	TestEqual(TEXT("Standard restores VSM"), Shadows->GetInt(), OriginalShadows);
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
