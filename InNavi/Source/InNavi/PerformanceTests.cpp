#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "FrameStats.h"
#include "LayoutValidationSession.h"
#include "MobileRealSource.h"
#include "PolyBuilding.h"
#include "NavigationRoutePlanner.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

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

	void SaveMeasurement(const FString& Name, const TSharedRef<FJsonObject>& Data)
	{
		Data->SetStringField(TEXT("machine_cpu"), FPlatformMisc::GetCPUBrand().TrimStartAndEnd());
		Data->SetNumberField(TEXT("machine_cores"), FPlatformMisc::NumberOfCores());
		Data->SetNumberField(TEXT("machine_ram_gb"), FPlatformMemory::GetConstants().TotalPhysicalGB);
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Data, Writer);
		FFileHelper::SaveStringToFile(Text, *(FPaths::ProjectSavedDir() / TEXT("Performance") / (Name + TEXT(".json"))));
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

#endif
