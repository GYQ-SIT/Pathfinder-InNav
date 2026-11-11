#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "FrameStats.h"
#include "LayoutValidationSession.h"
#include "MobileRealSource.h"
#include "EditScreenWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "PolyBuilding.h"
#include "NavigationRoutePlanner.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/GarbageCollection.h"

// The numbers behind docs/performance.md. Each test measures one thing at the size of the real data, writes what it measured to
// Saved/Performance/<name>.json and into the test log, and fails only when a budget of the document is broken. The budgets are loose
// guards against a slowdown (this runs on whichever machine runs CI), not the figures: the figures are what is written out.
namespace PerformanceTests
{
	struct FSamples
	{
		TArray<double> Milliseconds;
		void Add(double Seconds) { Milliseconds.Add(Seconds * 1000.0); }
		double Percentile(double Fraction) const
		{
			if (Milliseconds.IsEmpty()) return 0.0;
			TArray<double> Sorted = Milliseconds;
			Sorted.Sort();
			return Sorted[FMath::Clamp(FMath::CeilToInt(Fraction * Sorted.Num()) - 1, 0, Sorted.Num() - 1)];
		}
		double Max() const { return Percentile(1.0); }
	};

	TSharedRef<FJsonObject> Describe(const FSamples& Samples)
	{
		auto Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Samples.Milliseconds.Num());
		Result->SetNumberField(TEXT("p50_ms"), Samples.Percentile(0.5));
		Result->SetNumberField(TEXT("p95_ms"), Samples.Percentile(0.95));
		Result->SetNumberField(TEXT("max_ms"), Samples.Max());
		return Result;
	}

	bool SaveMeasurement(const FString& Name, const TSharedRef<FJsonObject>& Data)
	{
		Data->SetStringField(TEXT("machine_cpu"), FPlatformMisc::GetCPUBrand().TrimStartAndEnd());
		Data->SetNumberField(TEXT("machine_cores"), FPlatformMisc::NumberOfCores());
		Data->SetNumberField(TEXT("machine_ram_gb"), FPlatformMemory::GetConstants().TotalPhysicalGB);
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Performance");
		return IFileManager::Get().MakeDirectory(*Directory, true) && FJsonSerializer::Serialize(Data, Writer)
			&& FFileHelper::SaveStringToFile(Text, *(Directory / (Name + TEXT(".json"))));
	}

	// The rooms of the open building that a route can start or end in.
	TArray<UNavigationPanelWidget::FNavigationRoomOption> UsableRooms(const FNavigationSearchFixture& Fixture, const FString& BuildingId)
	{
		const APolyBuilding* Building = Fixture.Generator->FindBuildingById(BuildingId);
		TArray<UNavigationPanelWidget::FNavigationRoomOption> Rooms;
		for (const auto& Option : Fixture.Nav->GetSelectableRooms())
		{
			FString Error;
			if (Building && FNavigationRoutePlanner::IsValidPoint(*Building, *Fixture.Interior, Fixture.Generator->GetScale(), Option.Floor, Option.Centroid, Error))
			{
				Rooms.Add(Option);
			}
		}
		return Rooms;
	}

	// A campus-sized building (175 x 64 m, ten floors, the largest the editor makes) with a generated layout on every floor. Returns the
	// seconds the ten floors took to generate; the building is left open.
	double BuildTenFloors(FNavigationSearchFixture& Fixture, const FString& Id, FAutomationTestBase& Test)
	{
		const double Units = 100.0 / Fixture.Generator->GetScale();
		const FVector2D Offset(9000.0, 9000.0);
		const TArray<FVector2D> Outline = { FVector2D(0, 0), FVector2D(175 * Units, 0), FVector2D(175 * Units, 64 * Units), FVector2D(0, 64 * Units) };
		Fixture.Generator->SpawnBuildingFromData(Id, TEXT("Perf"), Outline, 10, 400.f, Offset);
		Fixture.Interior->LoadInteriorForBuilding(Id);
		if (Fixture.Interior->GetActiveFloor() < 1) Fixture.Interior->AddFloor();
		FString Error;
		Fixture.Interior->SetActiveFloor(1);
		Fixture.Interior->AddEntranceAtPoint(FVector2D(Offset.X + 87 * Units, Offset.Y), 15.f, Error);
		double Seconds = 0.0;
		for (int32 Level = 1; Level <= 10; ++Level)
		{
			Fixture.Interior->SetActiveFloor(Level);
			FFloorGenerationResult Generated;
			const double Start = FPlatformTime::Seconds();
			Test.TestTrue(*FString::Printf(TEXT("floor %d generates (%s)"), Level, *Generated.Error), Fixture.Interior->GenerateFloor(Level, Generated));
			Seconds += FPlatformTime::Seconds() - Start;
		}
		// What was generated is kept as the building's data, as a save does, so the interior can be opened again.
		Fixture.Generator->SetInteriorDataForBuilding(Id, Fixture.Interior->CaptureInteriorSnapshot());
		Fixture.Nav->SetNavigationContext(Fixture.Generator, Fixture.Interior);
		return Seconds;
	}
}

// Loading the real campus from the database: 436 buildings, 1,359 roads and 465 points of interest.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerformanceCampusLoadTest, "InNavi.Performance.CampusLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerformanceCampusLoadTest::RunTest(const FString& Parameters)
{
	using namespace PerformanceTests;
	FNavigationSearchFixture Fixture;
	const FString Database = FPaths::ProjectDir() / TEXT("Data/map_extended.db");
	if (!TestTrue(TEXT("the campus database is there"), IFileManager::Get().FileExists(*Database))) return false;
	FSamples Buildings, Roads, Context;
	int32 BuildingCount = 0, RoadAndPoiCount = 0, ContextCount = 0;
	for (int32 Run = 0; Run < 5; ++Run)
	{
		Fixture.Interior->ClearLoadedInterior();
		Fixture.Generator->ClearAllBuildings();
		Fixture.Generator->ClearAllRoadsAndPOIs();
		Fixture.Generator->ClearAllContextBuildings();
		double Start = FPlatformTime::Seconds();
		BuildingCount = Fixture.Generator->LoadBuildingsFromDatabase(Database);
		Buildings.Add(FPlatformTime::Seconds() - Start);
		Start = FPlatformTime::Seconds();
		RoadAndPoiCount = Fixture.Generator->LoadRoadsAndPOIsFromDatabase(Database);
		Roads.Add(FPlatformTime::Seconds() - Start);
		Start = FPlatformTime::Seconds();
		ContextCount = Fixture.Generator->LoadContextBuildingsFromDatabase(Database);
		Context.Add(FPlatformTime::Seconds() - Start);
	}
	TestTrue(TEXT("campus buildings are loaded"), BuildingCount > 0);
	TestTrue(TEXT("roads and points of interest are loaded"), RoadAndPoiCount > 1000);
	TestTrue(TEXT("context buildings are loaded"), ContextCount > 100);
	const double TotalP95 = Buildings.Percentile(0.95) + Roads.Percentile(0.95) + Context.Percentile(0.95);
	AddInfo(FString::Printf(TEXT("Campus from the database (%d campus buildings, %d roads and POIs, %d context buildings), 5 runs: buildings p50 %.0f ms, roads and POIs p50 %.0f ms, context p50 %.0f ms; whole load p95 %.0f ms"),
		BuildingCount, RoadAndPoiCount, ContextCount, Buildings.Percentile(0.5), Roads.Percentile(0.5), Context.Percentile(0.5), TotalP95));
	TestTrue(TEXT("the whole campus loads in under 10 s"), TotalP95 < 10000.0);
	auto Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("campus_buildings"), BuildingCount);
	Data->SetNumberField(TEXT("roads_and_pois"), RoadAndPoiCount);
	Data->SetNumberField(TEXT("context_buildings"), ContextCount);
	Data->SetObjectField(TEXT("buildings_load"), Describe(Buildings));
	Data->SetObjectField(TEXT("roads_and_pois_load"), Describe(Roads));
	Data->SetObjectField(TEXT("context_load"), Describe(Context));
	Data->SetNumberField(TEXT("whole_load_p95_ms"), TotalP95);
	SaveMeasurement(TEXT("campus-load"), Data);
	return true;
}

// Opening a building's interior, and routes inside it, across its floors: a campus-sized building (175 x 64 m, ten floors) with a
// generated layout on every floor, which is the largest the editor makes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerformanceInteriorTest, "InNavi.Performance.InteriorRoutes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerformanceInteriorTest::RunTest(const FString& Parameters)
{
	using namespace PerformanceTests;
	FNavigationSearchFixture Fixture;
	const FString Id = TEXT("PERF");
	const double Generating = BuildTenFloors(Fixture, Id, *this);
	FSamples Opening;
	for (int32 Run = 0; Run < 5; ++Run)
	{
		Fixture.Interior->ClearLoadedInterior();
		const double Start = FPlatformTime::Seconds();
		Fixture.Interior->LoadInteriorForBuilding(Id);
		Opening.Add(FPlatformTime::Seconds() - Start);
	}
	Fixture.Nav->SetNavigationContext(Fixture.Generator, Fixture.Interior);
	const auto Rooms = UsableRooms(Fixture, Id);
	AddInfo(FString::Printf(TEXT("%d rooms in the ten-floor building, %d of them can be used as a route point"), Fixture.Nav->GetSelectableRooms().Num(), Rooms.Num()));
	if (!TestTrue(TEXT("the building has rooms to route between"), Rooms.Num() >= 20)) return false;
	const APolyBuilding* Building = Fixture.Generator->FindBuildingById(Id);

	// Pairs spread over the floors, always the same ones. The first route builds the graph of the building; the next ones find it kept.
	FNavigationRoutePlanner::ClearGraphCache();
	FSamples Routes, Warm;
	double FirstRoute = 0.0;
	int32 Found = 0, CrossFloor = 0, Pairs = 0;
	const int32 Count = Rooms.Num();
	for (int32 Index = 0; Index < Count && Pairs < 12; ++Index)
	{
		const auto& From = Rooms[Index];
		const auto& To = Rooms[(Index * 7 + 3) % Count];
		if (From.RoomId == To.RoomId) continue;
		++Pairs;
		FNavigationRouteResult Route;
		const double Start = FPlatformTime::Seconds();
		const bool bFound = FNavigationRoutePlanner::FindRoute(*Building, *Fixture.Interior, Fixture.Generator->GetScale(), From.Floor, From.Centroid, To.Floor, To.Centroid, Route);
		const double Took = FPlatformTime::Seconds() - Start;
		Routes.Add(Took);
		if (Pairs == 1) FirstRoute = Took; else Warm.Add(Took);
		Found += bFound ? 1 : 0;
		CrossFloor += (bFound && From.Floor != To.Floor) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("Interior of 10 floors, %d usable rooms: generating the ten floors %.0f ms; opening it, 5 runs: p50 %.0f ms, p95 %.0f ms. Route inside it, %d pairs (%d found, %d across floors): the first, which builds the graph, %.0f ms; the next %d (graph kept, %d found in the cache): p50 %.0f ms, p95 %.0f ms, max %.0f ms"),
		Count, Generating * 1000.0, Opening.Percentile(0.5), Opening.Percentile(0.95), Pairs, Found, CrossFloor, FirstRoute * 1000.0, Warm.Milliseconds.Num(), FNavigationRoutePlanner::GetGraphCacheHits(), Warm.Percentile(0.5), Warm.Percentile(0.95), Warm.Max()));
	TestTrue(TEXT("most of the pairs have a route"), Found * 10 >= Pairs * 8);
	TestTrue(TEXT("opening an interior takes under 5 s"), Opening.Percentile(0.95) < 5000.0);
	// A guard against a large slowdown, not the budget (15 s, docs/performance.md): the laptop that runs the CI needs 21.8 s for it, the
	// desktop it was first measured on 6 s, so the limit is set for the slower of the two.
	TestTrue(TEXT("the first route in the largest building, which builds the graph, takes under 45 s"), FirstRoute < 45.0);
	TestEqual(TEXT("the other routes found the graph kept"), FNavigationRoutePlanner::GetGraphCacheHits(), Warm.Milliseconds.Num());
	TestTrue(TEXT("and have a p95 under 1.5 s"), Warm.Percentile(0.95) < 1500.0);
	auto Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("usable_rooms"), Count);
	Data->SetNumberField(TEXT("generate_ten_floors_ms"), Generating * 1000.0);
	Data->SetNumberField(TEXT("pairs"), Pairs);
	Data->SetNumberField(TEXT("found"), Found);
	Data->SetNumberField(TEXT("across_floors"), CrossFloor);
	Data->SetObjectField(TEXT("open_interior"), Describe(Opening));
	Data->SetObjectField(TEXT("route"), Describe(Routes));
	Data->SetNumberField(TEXT("first_route_ms"), FirstRoute * 1000.0);
	Data->SetObjectField(TEXT("route_graph_kept"), Describe(Warm));
	SaveMeasurement(TEXT("interior-routes"), Data);
	return true;
}

// Routes between three buildings, over their link doors and the open ground between them (docs/test-data).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerformanceCampusRouteTest, "InNavi.Performance.CampusRoutes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerformanceCampusRouteTest::RunTest(const FString& Parameters)
{
	using namespace PerformanceTests;
	FNavigationSearchFixture Fixture;
	const FString Project = FPaths::ProjectDir() / TEXT("../docs/test-data/entrance-test-2-with-doors.json");
	Fixture.Interior->ClearLoadedInterior();
	Fixture.Generator->ClearAllBuildings();
	if (!TestTrue(TEXT("the three-building project loads"), Fixture.Generator->LoadBuildingsFromFile(Project) == 3)) return false;
	TMap<FString, TArray<FNavigationRoutePoint>> PointsOf;
	for (const TCHAR* Id : { TEXT("A"), TEXT("B"), TEXT("C") })
	{
		Fixture.Interior->LoadInteriorForBuilding(Id);
		Fixture.Nav->SetNavigationContext(Fixture.Generator, Fixture.Interior);
		for (const auto& Room : UsableRooms(Fixture, Id))
		{
			FNavigationRoutePoint Point;
			Point.BuildingId = Id; Point.Floor = Room.Floor; Point.Position = Room.Centroid;
			PointsOf.FindOrAdd(Id).Add(Point);
		}
	}
	if (!TestTrue(TEXT("each building has rooms"), PointsOf.Num() == 3)) return false;
	FSamples Routes;
	int32 Pairs = 0, Found = 0;
	const TCHAR* Order[] = { TEXT("A"), TEXT("B"), TEXT("C") };
	for (int32 Index = 0; Index < 30; ++Index)
	{
		const TArray<FNavigationRoutePoint>& FromRooms = PointsOf[Order[Index % 3]];
		const TArray<FNavigationRoutePoint>& ToRooms = PointsOf[Order[(Index + 1 + (Index / 3) % 2) % 3]];
		const FNavigationRoutePoint& From = FromRooms[(Index * 5) % FromRooms.Num()];
		const FNavigationRoutePoint& To = ToRooms[(Index * 3 + 1) % ToRooms.Num()];
		FNavigationRouteResult Route;
		const double Start = FPlatformTime::Seconds();
		const bool bFound = FNavigationRoutePlanner::FindCampusRoute(*Fixture.Generator, *Fixture.Interior, From, To, Route);
		Routes.Add(FPlatformTime::Seconds() - Start);
		++Pairs;
		Found += bFound ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("Route between buildings (3 buildings, %d pairs, %d found): p50 %.0f ms, p95 %.0f ms, max %.0f ms"), Pairs, Found, Routes.Percentile(0.5), Routes.Percentile(0.95), Routes.Max()));
	TestTrue(TEXT("most of the pairs have a route"), Found * 10 >= Pairs * 8);
	TestTrue(TEXT("a route between buildings has a p95 under 5 s"), Routes.Percentile(0.95) < 5000.0);
	auto Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("buildings"), 3);
	Data->SetNumberField(TEXT("pairs"), Pairs);
	Data->SetNumberField(TEXT("found"), Found);
	Data->SetObjectField(TEXT("route"), Describe(Routes));
	SaveMeasurement(TEXT("campus-routes"), Data);
	return true;
}

// What the phone app asks of its data interface (IMobileNavSource), the nearest thing this product has to an API: opening a project,
// searching its places and asking for a route. These are the "response times" of the product.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerformancePhoneDataTest, "InNavi.Performance.PhoneData", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerformancePhoneDataTest::RunTest(const FString& Parameters)
{
	using namespace PerformanceTests;
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../docs/test-data/entrance-test-2-with-doors.json"));
	FSamples Opening, Searching, Routing;
	TSharedPtr<FMobileRealSource> Source;
	for (int32 Run = 0; Run < 5; ++Run)
	{
		const double Start = FPlatformTime::Seconds();
		Source = MakeShared<FMobileRealSource>(Project);
		Opening.Add(FPlatformTime::Seconds() - Start);
	}
	const TArray<FMobileSite> Sites = Source->ListSites();
	if (!TestTrue(TEXT("the project is a site"), !Sites.IsEmpty())) return false;
	const FString Site = Sites[0].Id;
	TArray<FMobilePlace> Places;
	for (const TCHAR* Query : { TEXT(""), TEXT("a"), TEXT("lab"), TEXT("1"), TEXT("room") })
	{
		const double Start = FPlatformTime::Seconds();
		const TArray<FMobilePlace> Found = Source->SearchPlaces(Site, Query, FMobilePlaceFilter());
		Searching.Add(FPlatformTime::Seconds() - Start);
		if (Places.IsEmpty()) Places = Found;
	}
	if (!TestTrue(TEXT("the site has places"), Places.Num() >= 10)) return false;
	int32 Pairs = 0, Found = 0;
	for (int32 Index = 0; Index < Places.Num() && Pairs < 40; ++Index)
	{
		const FMobilePlace& From = Places[Index];
		const FMobilePlace& To = Places[(Index * 7 + 3) % Places.Num()];
		if (From.Id == To.Id) continue;
		FMobileRouteRequest Request;
		Request.FromPlaceId = From.Id;
		Request.ToPlaceId = To.Id;
		const double Start = FPlatformTime::Seconds();
		const FMobileRoute Route = Source->FindRoute(Request);
		Routing.Add(FPlatformTime::Seconds() - Start);
		++Pairs;
		Found += Route.bFound ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("Phone data interface (%d places): open a project p50 %.0f ms, p95 %.0f ms; search p50 %.1f ms, p95 %.1f ms; route (%d pairs, %d found) p50 %.0f ms, p95 %.0f ms, max %.0f ms"),
		Places.Num(), Opening.Percentile(0.5), Opening.Percentile(0.95), Searching.Percentile(0.5), Searching.Percentile(0.95), Pairs, Found, Routing.Percentile(0.5), Routing.Percentile(0.95), Routing.Max()));
	TestTrue(TEXT("some routes are found"), Found > 0);
	TestTrue(TEXT("opening a project takes under 5 s"), Opening.Percentile(0.95) < 5000.0);
	TestTrue(TEXT("a search takes under 500 ms"), Searching.Percentile(0.95) < 500.0);
	TestTrue(TEXT("a route takes under 5 s (p95)"), Routing.Percentile(0.95) < 5000.0);
	auto Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("places"), Places.Num());
	Data->SetNumberField(TEXT("pairs"), Pairs);
	Data->SetNumberField(TEXT("found"), Found);
	Data->SetObjectField(TEXT("open_project"), Describe(Opening));
	Data->SetObjectField(TEXT("search"), Describe(Searching));
	Data->SetObjectField(TEXT("route"), Describe(Routing));
	SaveMeasurement(TEXT("phone-data"), Data);
	return true;
}

// The layout validator over a whole building and over every building of a project.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerformanceValidationTest, "InNavi.Performance.Validation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerformanceValidationTest::RunTest(const FString& Parameters)
{
	using namespace PerformanceTests;
	FNavigationSearchFixture Fixture;
	BuildTenFloors(Fixture, TEXT("PERF"), *this);
	ULayoutValidationSession* Session = NewObject<ULayoutValidationSession>();
	Session->SetContext(Fixture.Generator, Fixture.Interior, TEXT("performance"));
	FSamples Building, Project;
	for (int32 Run = 0; Run < 2; ++Run)
	{
		double Start = FPlatformTime::Seconds();
		Session->Validate(TEXT("PERF"));
		Building.Add(FPlatformTime::Seconds() - Start);
		Start = FPlatformTime::Seconds();
		Session->Validate();
		Project.Add(FPlatformTime::Seconds() - Start);
	}
	AddInfo(FString::Printf(TEXT("Layout validator, 2 runs: one building (ten floors, generated) p50 %.0f ms, p95 %.0f ms; whole project (%d buildings) p50 %.0f ms, p95 %.0f ms"),
		Building.Percentile(0.5), Building.Percentile(0.95), Fixture.Generator->GetAllBuildings().Num(), Project.Percentile(0.5), Project.Percentile(0.95)));
	TestTrue(TEXT("the validator has a report"), Session->HasReport());
	// A guard, as for the first route: the first run builds the graph (22 s on the CI laptop, 6 s on the desktop).
	TestTrue(TEXT("a building is validated in under 45 s"), Building.Percentile(0.95) < 45000.0);
	TestTrue(TEXT("a project is validated in under 30 s"), Project.Percentile(0.95) < 30000.0);
	auto Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("project_buildings"), Fixture.Generator->GetAllBuildings().Num());
	Data->SetObjectField(TEXT("one_building"), Describe(Building));
	Data->SetObjectField(TEXT("whole_project"), Describe(Project));
	SaveMeasurement(TEXT("validation"), Data);
	return true;
}

// CPU process memory at repeatable checkpoints of a desktop data workload. This includes the editor/test harness;
// use scripts/measure-memory.ps1 against a packaged desktop process for the rendered application's footprint.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerformanceMemoryUsageTest, "InNavi.Performance.MemoryUsage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerformanceMemoryUsageTest::RunTest(const FString& Parameters)
{
	using namespace PerformanceTests;
	constexpr double BytesPerMiB = 1024.0 * 1024.0;
	double MaxGrowthMiB = 0.0;
	const bool bHasBudget = FParse::Value(FCommandLine::Get(), TEXT("MemoryMaxGrowthMiB="), MaxGrowthMiB);
	if (bHasBudget && !TestTrue(TEXT("MemoryMaxGrowthMiB must be finite and positive"), FMath::IsFinite(MaxGrowthMiB) && MaxGrowthMiB > 0.0)) return false;

	FNavigationRoutePlanner::ClearGraphCache();
	CollectGarbage(RF_NoFlags, true);
	const FPlatformMemoryStats Baseline = FPlatformMemory::GetStats();
	const double Start = FPlatformTime::Seconds();
	TArray<TSharedPtr<FJsonValue>> Snapshots;
	uint64 MaxSampledResident = 0, MaxSampledCommitted = 0;
	auto Sample = [&](const TCHAR* Phase)
	{
		const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
		TestTrue(TEXT("process memory counters are available"), Stats.UsedPhysical > 0 && Stats.UsedVirtual > 0);
		TestTrue(TEXT("process lifetime peaks cover the current counters"), Stats.PeakUsedPhysical >= Stats.UsedPhysical && Stats.PeakUsedVirtual >= Stats.UsedVirtual);
		const int64 ResidentDelta = (int64)Stats.UsedPhysical - (int64)Baseline.UsedPhysical;
		const int64 CommittedDelta = (int64)Stats.UsedVirtual - (int64)Baseline.UsedVirtual;
		auto Snapshot = MakeShared<FJsonObject>();
		Snapshot->SetStringField(TEXT("phase"), Phase);
		Snapshot->SetNumberField(TEXT("elapsed_seconds"), FPlatformTime::Seconds() - Start);
		Snapshot->SetNumberField(TEXT("resident_bytes"), Stats.UsedPhysical);
		Snapshot->SetNumberField(TEXT("committed_bytes"), Stats.UsedVirtual);
		Snapshot->SetNumberField(TEXT("process_lifetime_peak_resident_bytes"), Stats.PeakUsedPhysical);
		Snapshot->SetNumberField(TEXT("process_lifetime_peak_committed_bytes"), Stats.PeakUsedVirtual);
		Snapshot->SetNumberField(TEXT("resident_delta_from_baseline_bytes"), ResidentDelta);
		Snapshot->SetNumberField(TEXT("committed_delta_from_baseline_bytes"), CommittedDelta);
		Snapshots.Add(MakeShared<FJsonValueObject>(Snapshot));
		MaxSampledResident = FMath::Max(MaxSampledResident, Stats.UsedPhysical);
		MaxSampledCommitted = FMath::Max(MaxSampledCommitted, Stats.UsedVirtual);
		AddInfo(FString::Printf(TEXT("Memory [%s]: resident %.2f MiB (%+.2f), committed %.2f MiB (%+.2f from baseline)"),
			Phase, Stats.UsedPhysical / BytesPerMiB, ResidentDelta / BytesPerMiB, Stats.UsedVirtual / BytesPerMiB, CommittedDelta / BytesPerMiB));
	};
	Sample(TEXT("baseline_after_gc"));
	auto Data = MakeShared<FJsonObject>();
	{
		FNavigationSearchFixture Fixture;
		ON_SCOPE_EXIT { FNavigationRoutePlanner::ClearGraphCache(); };
		Fixture.Interior->ClearLoadedInterior();
		Fixture.Generator->ClearAllBuildings();
		const FString Database = FPaths::ProjectDir() / TEXT("Data/map_extended.db");
		if (!TestTrue(TEXT("the campus database is there"), IFileManager::Get().FileExists(*Database))) return false;
		const int32 Buildings = Fixture.Generator->LoadBuildingsFromDatabase(Database);
		const int32 Roads = Fixture.Generator->LoadRoadsAndPOIsFromDatabase(Database);
		const int32 Context = Fixture.Generator->LoadContextBuildingsFromDatabase(Database);
		if (!TestTrue(TEXT("the real campus workload loaded"), Buildings > 0 && Roads > 1000 && Context > 100)) return false;
		Data->SetNumberField(TEXT("campus_buildings"), Buildings);
		Data->SetNumberField(TEXT("roads_and_pois"), Roads);
		Data->SetNumberField(TEXT("context_buildings"), Context);
		Sample(TEXT("campus_loaded"));

		const FString Id(TEXT("MEMORY"));
		BuildTenFloors(Fixture, Id, *this);
		Sample(TEXT("ten_floor_interior_generated"));
		const auto Rooms = UsableRooms(Fixture, Id);
		Data->SetNumberField(TEXT("usable_rooms"), Rooms.Num());
		if (!TestTrue(TEXT("the interior has enough rooms for the route workload"), Rooms.Num() >= 20)) return false;
		const APolyBuilding* Building = Fixture.Generator->FindBuildingById(Id);
		FNavigationRoutePlanner::ClearGraphCache();
		int32 Found = 0;
		for (int32 Index = 0; Index < 12; ++Index)
		{
			const auto& From = Rooms[Index];
			const auto& To = Rooms[(Index * 7 + 3) % Rooms.Num()];
			FNavigationRouteResult Route;
			Found += FNavigationRoutePlanner::FindRoute(*Building, *Fixture.Interior, Fixture.Generator->GetScale(),
				From.Floor, From.Centroid, To.Floor, To.Centroid, Route) ? 1 : 0;
			Sample(Index == 0 ? TEXT("first_route_graph_cached") : TEXT("warm_route"));
		}
		Data->SetNumberField(TEXT("routes_attempted"), 12);
		Data->SetNumberField(TEXT("routes_found"), Found);
		TestTrue(TEXT("most route pairs have a route"), Found >= 10);
		TestEqual(TEXT("warm routes reuse the graph"), FNavigationRoutePlanner::GetGraphCacheHits(), 11);
	}
	// Only collect after the fixture has released its raw UObject pointers. Allocators may retain freed pages;
	// a nonzero cleanup delta is a measurement, not proof of a leak.
	CollectGarbage(RF_NoFlags, true);
	Sample(TEXT("workload_destroyed_cache_cleared_after_gc"));
	const double GrowthMiB = FMath::Max(0.0, ((double)MaxSampledCommitted - Baseline.UsedVirtual) / BytesPerMiB);
	Data->SetNumberField(TEXT("schema_version"), 1);
	Data->SetStringField(TEXT("measured_at_utc"), FDateTime::UtcNow().ToIso8601());
	Data->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Data->SetNumberField(TEXT("process_id"), FPlatformProcess::GetCurrentProcessId());
	Data->SetBoolField(TEXT("includes_editor_overhead"), GIsEditor);
	Data->SetBoolField(TEXT("null_rhi"), FParse::Param(FCommandLine::Get(), TEXT("nullrhi")));
	Data->SetStringField(TEXT("scope"), TEXT("Desktop data workload; process CPU memory including the test harness; excludes dedicated GPU memory. Peaks are process-lifetime counters, sampled maxima are checkpoint maxima."));
	Data->SetArrayField(TEXT("snapshots"), Snapshots);
	Data->SetNumberField(TEXT("max_sampled_resident_bytes"), MaxSampledResident);
	Data->SetNumberField(TEXT("max_sampled_committed_bytes"), MaxSampledCommitted);
	Data->SetNumberField(TEXT("max_sampled_committed_growth_mib"), GrowthMiB);
	if (bHasBudget) Data->SetNumberField(TEXT("max_committed_growth_budget_mib"), MaxGrowthMiB);
	TestTrue(TEXT("memory measurements are saved"), SaveMeasurement(TEXT("memory-usage"), Data));
	if (bHasBudget) TestTrue(TEXT("sampled committed growth stays within MemoryMaxGrowthMiB"), GrowthMiB <= MaxGrowthMiB);
	AddInfo(FString::Printf(TEXT("Memory: max sampled resident %.2f MiB, committed %.2f MiB, committed growth %.2f MiB. Report: Saved/Performance/memory-usage.json"),
		MaxSampledResident / BytesPerMiB, MaxSampledCommitted / BytesPerMiB, GrowthMiB));
	return true;
}

// The frame figures of the Debug panel: the average, the 1 % low and the slowest frame, over the last ten seconds.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerformanceFrameStatsTest, "InNavi.Performance.FrameStats", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerformanceFrameStatsTest::RunTest(const FString& Parameters)
{
	FFrameStats Empty;
	TestEqual(TEXT("no frames, no rate"), Empty.AverageFps(), 0.0);

	FFrameStats Steady;
	for (int32 Frame = 0; Frame < 600; ++Frame) Steady.AddFrame(1.0 / 60.0);
	TestTrue(TEXT("a steady 60 fps averages 60"), FMath::IsNearlyEqual(Steady.AverageFps(), 60.0, 0.01));
	TestTrue(TEXT("and its slowest frame is 60 as well"), FMath::IsNearlyEqual(Steady.MinFps(), 60.0, 0.01));

	// One slow frame in a hundred: it is the minimum, and the 1 % low sits on it only when it is that large a part of the frames.
	FFrameStats Hitch;
	for (int32 Frame = 0; Frame < 99; ++Frame) Hitch.AddFrame(1.0 / 100.0);
	Hitch.AddFrame(1.0 / 20.0);
	TestTrue(TEXT("the slowest frame is the hitch"), FMath::IsNearlyEqual(Hitch.MinFps(), 20.0, 0.01));
	TestTrue(TEXT("the average is frames over time, not the mean of the rates"), FMath::IsNearlyEqual(Hitch.AverageFps(), 100.0 / 1.04, 0.01));
	TestTrue(TEXT("the 1 % low of 100 frames is its slowest one"), FMath::IsNearlyEqual(Hitch.OnePercentLowFps(), 20.0, 0.01));

	// Only the last ten seconds count.
	FFrameStats Long;
	for (int32 Frame = 0; Frame < 600; ++Frame) Long.AddFrame(1.0 / 10.0);
	for (int32 Frame = 0; Frame < 1200; ++Frame) Long.AddFrame(1.0 / 120.0);
	TestTrue(TEXT("old slow frames have left the window"), FMath::IsNearlyEqual(Long.AverageFps(), 120.0, 0.5) && Long.MinFps() > 100.0);
	TestTrue(TEXT("and the text says what it shows"), Long.ToText().Contains(TEXT("avg")) && Long.ToText().Contains(TEXT("1% low")));
	return true;
}

// Opt-in rendered-app comparison: run alone with -game, never as an editor fixture or with -nullrhi.
namespace PerformanceTests
{
	class FCampusPreviewMemoryCapture : public IAutomationLatentCommand
	{
	public:
		FCampusPreviewMemoryCapture(FAutomationTestBase* InTest, const FString& InProject, const FString& InBuilding,
			const FString& InVariant, const FString& InOutput)
			: Test(InTest), Project(InProject), BuildingId(InBuilding), Variant(InVariant), Output(InOutput) {}

		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAction) return false;
			if (!Controller.IsValid())
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::Game && Context.World())
					{
						Controller = Cast<ANavPlayerController>(Context.World()->GetFirstPlayerController());
						if (Controller.IsValid()) break;
					}
				}
				if (!Controller.IsValid() || GIsEditor || FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
				{
					Test->AddError(TEXT("Campus preview capture requires a rendered -game desktop process."));
					return true;
				}
				for (TActorIterator<ABuildingGenerator> It(Controller->GetWorld()); It; ++It) { Generator = *It; break; }
				if (!Generator.IsValid()) { Test->AddError(TEXT("No app building generator.")); return true; }
				TArray<UUserWidget*> Widgets;
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Controller.Get(), Widgets, UUserWidget::StaticClass(), true);
				for (UUserWidget* Widget : Widgets) Widget->RemoveFromParent();
				UEditScreenWidget* Screen = CreateWidget<UEditScreenWidget>(Controller.Get(), UEditScreenWidget::StaticClass());
				if (!Screen) { Test->AddError(TEXT("Cannot create exterior screen.")); return true; }
				Screen->AddToViewport();
				Controller->SetCampusSeeThroughEnabled(false);
				Controller->StartNewProject();
				CollectGarbage(RF_NoFlags, true);
				Data->SetStringField(TEXT("started_at_utc"), FDateTime::UtcNow().ToIso8601());
				Data->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
				Data->SetStringField(TEXT("cpu"), FPlatformMisc::GetCPUBrand().TrimStartAndEnd());
				Data->SetNumberField(TEXT("visible_physical_memory_bytes"), FPlatformMemory::GetConstants().TotalPhysical);
				Data->SetNumberField(TEXT("process_id"), FPlatformProcess::GetCurrentProcessId());
				Data->SetStringField(TEXT("project_file"), Project);
				Data->SetStringField(TEXT("building_id"), BuildingId);
				Data->SetStringField(TEXT("variant"), Variant);
				const FIntPoint Viewport = GEngine->GameViewport->Viewport->GetSizeXY();
				Data->SetNumberField(TEXT("viewport_width"), Viewport.X);
				Data->SetNumberField(TEXT("viewport_height"), Viewport.Y);
				Data->SetBoolField(TEXT("includes_editor_runtime"), true);
				Data->SetBoolField(TEXT("null_rhi"), false);
				Data->SetNumberField(TEXT("settle_seconds_per_phase"), 5);
				Data->SetNumberField(TEXT("sample_interval_seconds"), 0.5);
				Data->SetNumberField(TEXT("samples_per_phase"), 10);
				Data->SetStringField(TEXT("scope"), TEXT("Rendered UnrealEditor.exe -game Development, 1280x720; Windows process working set and private committed bytes, not dedicated GPU memory. One explicit GC before empty baseline; normal rendering/engine GC continues during capture."));
				NextAction = Now + 5.0;
				return false;
			}

			static const TCHAR* Names[] = { TEXT("empty_baseline"), TEXT("campus_opaque"), TEXT("selected_floor_1"), TEXT("selected_building_all_floors") };
			auto Sample = MakeShared<FJsonObject>();
			const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
			Sample->SetStringField(TEXT("phase"), Names[Phase]);
			Sample->SetStringField(TEXT("measured_at_utc"), FDateTime::UtcNow().ToIso8601());
			Sample->SetNumberField(TEXT("resident_bytes"), Stats.UsedPhysical);
			Sample->SetNumberField(TEXT("private_committed_bytes"), Stats.UsedVirtual);
			int32 Cached = 0, Shown = 0;
			for (APolyBuilding* Building : Generator->GetAllBuildings())
			{
				Cached += Controller->CountCachedCampusRoomsForTests(Building->BuildingId);
				Shown += Controller->CountShownCampusRoomsForTests(Building->BuildingId);
			}
			Sample->SetNumberField(TEXT("cached_preview_rooms"), Cached);
			Sample->SetNumberField(TEXT("shown_preview_rooms"), Shown);
			Samples.Add(MakeShared<FJsonValueObject>(Sample));
			if (++PhaseSamples < 10) { NextAction = Now + 0.5; return false; }
			Test->AddInfo(FString::Printf(TEXT("Preview memory %s: cached %d, shown %d, resident %.2f MiB, private %.2f MiB"),
				Names[Phase], Cached, Shown, Stats.UsedPhysical / 1048576.0, Stats.UsedVirtual / 1048576.0));
			PhaseSamples = 0;
			if (++Phase == 4)
			{
				Data->SetNumberField(TEXT("schema_version"), 1);
				Data->SetArrayField(TEXT("samples"), Samples);
				FString Json;
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
				Test->TestTrue(TEXT("preview memory capture saved"), FJsonSerializer::Serialize(Data, TJsonWriterFactory<>::Create(&Json))
					&& FFileHelper::SaveStringToFile(Json, *Output));
				return true;
			}
			if (Phase == 1)
			{
				if (!Test->TestTrue(TEXT("benchmark project loaded"), Controller->LoadBuildingDataFromPathOrDatabase(Project) > 0)) return true;
				if (!Test->TestNotNull(TEXT("benchmark building exists"), Generator->FindBuildingById(BuildingId))) return true;
				int32 AuthoredRooms = 0;
				for (APolyBuilding* Building : Generator->GetAllBuildings())
				{
					const auto Interior = Generator->GetInteriorDataForBuilding(Building->BuildingId);
					const TArray<TSharedPtr<FJsonValue>>* Floors = nullptr;
					if (!Interior || !Interior->TryGetArrayField(TEXT("floors"), Floors)) continue;
					for (const auto& Floor : *Floors)
					{
						const TArray<TSharedPtr<FJsonValue>>* Rooms = nullptr;
						if (Floor->AsObject()->TryGetArrayField(TEXT("rooms"), Rooms)) AuthoredRooms += Rooms->Num();
					}
				}
				Data->SetNumberField(TEXT("authored_rooms"), AuthoredRooms);
				Data->SetNumberField(TEXT("editable_buildings"), Generator->GetAllBuildings().Num());
				int32 Created = 0;
				for (APolyBuilding* Building : Generator->GetAllBuildings()) Created += Controller->CountCachedCampusRoomsForTests(Building->BuildingId);
				Test->TestEqual(TEXT("opaque preview count matches the compiled variant"), Created, Variant == TEXT("eager") ? AuthoredRooms : 0);
			}
			else if (Phase == 2)
			{
				Controller->SetBuildingRoomFloor(BuildingId, 1);
				Controller->SetBuildingMadeTransparent(BuildingId, true);
			}
			else Controller->SetBuildingRoomFloor(BuildingId, 0);
			NextAction = FPlatformTime::Seconds() + 5.0;
			return false;
		}
	private:
		FAutomationTestBase* Test;
		FString Project, BuildingId, Variant, Output;
		TWeakObjectPtr<ANavPlayerController> Controller;
		TWeakObjectPtr<ABuildingGenerator> Generator;
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Samples;
		double NextAction = 0.0;
		int32 Phase = 0, PhaseSamples = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerformanceCampusPreviewMemoryTest, "InNavi.Performance.CampusPreviewMemory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPerformanceCampusPreviewMemoryTest::RunTest(const FString& Parameters)
{
	FString PreparedProject;
	if (FParse::Value(FCommandLine::Get(), TEXT("PreviewMemoryPrepare="), PreparedProject))
	{
		FNavigationSearchFixture Fixture;
		Fixture.Interior->ClearLoadedInterior();
		Fixture.Generator->ClearAllBuildings();
		PerformanceTests::BuildTenFloors(Fixture, TEXT("MEMORY"), *this);
		TestTrue(TEXT("generated benchmark project saved"), Fixture.Generator->SaveBuildingsToFile(PreparedProject));
		return true;
	}
	FString Project, Building, Variant, Output;
	if (!FParse::Value(FCommandLine::Get(), TEXT("PreviewMemoryProject="), Project))
	{
		AddInfo(TEXT("Opt-in rendered-app capture: supply PreviewMemoryProject, PreviewMemoryBuilding, PreviewMemoryVariant and PreviewMemoryOutput; see docs/performance.md."));
		return true;
	}
	if (!FParse::Value(FCommandLine::Get(), TEXT("PreviewMemoryBuilding="), Building)
		|| !FParse::Value(FCommandLine::Get(), TEXT("PreviewMemoryVariant="), Variant)
		|| !FParse::Value(FCommandLine::Get(), TEXT("PreviewMemoryOutput="), Output)
		|| (Variant != TEXT("eager") && Variant != TEXT("lazy")))
	{
		AddError(TEXT("Missing/invalid preview memory capture parameters."));
		return false;
	}
	AddCommand(new PerformanceTests::FCampusPreviewMemoryCapture(this, Project, Building, Variant, Output));
	return true;
}

#endif
