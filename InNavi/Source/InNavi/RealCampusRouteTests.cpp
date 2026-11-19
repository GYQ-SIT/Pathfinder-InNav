#if WITH_DEV_AUTOMATION_TESTS

#include "MobileRealSource.h"
#include "NavigationRoutePlanner.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

// The first route in the real project (Data/E2_Filled.json: 29 buildings, a ten-floor interior of 214 rooms): how long it takes when the graph has to be
// made, when it is in a file, and when it is in memory. The numbers are written to Saved/Performance/real-campus-first-route.json next to the others.
// The limits are loose guards (a slow machine, a loaded CI runner), not the budget: the budget is in docs/performance.md.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRealCampusFirstRouteTest, "InNavi.Performance.RealCampusFirstRoute", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRealCampusFirstRouteTest::RunTest(const FString& Parameters)
{
	const FString Project = FPaths::ProjectDir() / TEXT("Data") / TEXT("E2_Filled.json");
	if (!IFileManager::Get().FileExists(*Project))
	{
		AddInfo(TEXT("Data/E2_Filled.json is not there: the real project is not measured."));
		return true;
	}
	FMobileRealSource Source(Project);
	const auto Sites = Source.ListSites();
	if (!TestFalse(TEXT("the real project has a site"), Sites.IsEmpty())) return false;
	const auto From = Source.SearchPlaces(Sites[0].Id, TEXT("Lectorial 6"), FMobilePlaceFilter());
	const auto To = Source.SearchPlaces(Sites[0].Id, TEXT("E2-08-14"), FMobilePlaceFilter());
	if (!TestFalse(TEXT("a room to start from"), From.IsEmpty()) || !TestFalse(TEXT("a room on the eighth floor"), To.IsEmpty())) return false;
	FMobileRouteRequest Request;
	Request.FromPlaceId = From[0].Id;
	Request.ToPlaceId = To[0].Id;

	const FString Directory = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT
	{
		FNavigationRoutePlanner::SetGraphFiles(FString(), FString());
		IFileManager::Get().DeleteDirectory(*Directory, false, true);
	};
	const auto Time = [&](FMobileRoute& Route)
	{
		const double Started = FPlatformTime::Seconds();
		Route = Source.FindRoute(Request);
		return (FPlatformTime::Seconds() - Started) * 1000.0;
	};

	// The graph has to be made (and is written to a file).
	FNavigationRoutePlanner::SetGraphFiles(Directory, FString());
	FNavigationRoutePlanner::ClearGraphCache();
	FMobileRoute Built, FromFile, InMemory;
	const double BuiltMs = Time(Built);
	FNavigationRoutePlanner::WaitForGraphFiles();
	TestTrue(TEXT("the first route is found"), Built.bFound);
	TestTrue(TEXT("its graph was made"), FNavigationRoutePlanner::GetGraphCacheMisses() >= 1);

	// A new start of the app: nothing in memory, the graph in a file.
	FNavigationRoutePlanner::ClearGraphCache();
	const double FileMs = Time(FromFile);
	TestTrue(TEXT("the route from the file is found"), FromFile.bFound);
	TestTrue(TEXT("it read the file and made no graph"), FNavigationRoutePlanner::GetGraphFileReads() >= 1 && FNavigationRoutePlanner::GetGraphCacheMisses() == 0);
	TestTrue(TEXT("and is the same route"), FromFile.Steps.Num() == Built.Steps.Num() && FMath::IsNearlyEqual(FromFile.Metres, Built.Metres, 0.001));

	// The next route in the same run.
	const double MemoryMs = Time(InMemory);
	TestTrue(TEXT("the route from memory is found"), InMemory.bFound);

	const FString Json = FString::Printf(TEXT("{\n  \"graph_made_ms\": %.1f,\n  \"graph_from_file_ms\": %.1f,\n  \"graph_in_memory_ms\": %.1f\n}\n"), BuiltMs, FileMs, MemoryMs);
	FFileHelper::SaveStringToFile(Json, *(FPaths::ProjectSavedDir() / TEXT("Performance") / TEXT("real-campus-first-route.json")));
	AddInfo(FString::Printf(TEXT("first route, real project: graph made %.0f ms, from a file %.0f ms, in memory %.0f ms"), BuiltMs, FileMs, MemoryMs));
	TestTrue(TEXT("making the graph stays within 20 s"), BuiltMs < 20000.0);
	TestTrue(TEXT("reading it from a file is under 3 s"), FileMs < 3000.0);
	return true;
}

#endif
