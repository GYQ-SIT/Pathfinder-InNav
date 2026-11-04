#if WITH_DEV_AUTOMATION_TESTS

#include "MobileRealSource.h"
#include "MobileNavSourceContract.h"
#include "MobileAppWidget.h"
#include "MobileExplorePageWidget.h"
#include "MobileSearchPageWidget.h"
#include "MobileMapWidget.h"
#include "MobilePlacePageWidget.h"
#include "MobileRoutePageWidget.h"
#include "InteriorRoom.h"
#include "LayoutValidationSession.h"
#include "LayoutValidation.h"
#include "UiTestFixture.h"
#include "Components/Button.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace MobileRealTests
{
	struct FProjectFiles
	{
		FString Directory = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
		TSharedPtr<FJsonObject> Root;
		FProjectFiles()
		{
			IFileManager::Get().MakeDirectory(*Directory, true);
			FString Text;
			FFileHelper::LoadFileToString(Text, *(FPaths::ProjectPluginsDir() / TEXT("InNaviLayoutValidator/Examples/valid-layout.json")));
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
			Root->SetStringField(TEXT("projectName"), TEXT("Authored campus"));
		}
		~FProjectFiles() { IFileManager::Get().DeleteDirectory(*Directory, false, true); }
		FString Write(const FString& Name = TEXT("campus.json"))
		{
			FString Text; FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
			const FString Path = Directory / Name; FFileHelper::SaveStringToFile(Text, *Path); return Path;
		}
		TSharedPtr<FJsonObject> Building() { return Root->GetArrayField(TEXT("buildings"))[0]->AsObject(); }
		TSharedPtr<FJsonObject> Interior() { return Building()->GetObjectField(TEXT("interior")); }
		TSharedPtr<FJsonObject> Floor(int32 Index) { return Interior()->GetArrayField(TEXT("floors"))[Index]->AsObject(); }
		TSharedPtr<FJsonObject> Room(int32 Index) { return Floor(Index)->GetArrayField(TEXT("rooms"))[0]->AsObject(); }
		TSharedPtr<FJsonObject> Clone(const TSharedPtr<FJsonObject>& Json)
		{
			FString Text; FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
			TSharedPtr<FJsonObject> Result; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result); return Result;
		}
		void AddSecondBuilding(bool bLink)
		{
			auto Other = Clone(Building()); Other->SetStringField(TEXT("id"), TEXT("B")); Other->SetStringField(TEXT("name"), TEXT("Building B"));
			Other->SetArrayField(TEXT("offset"), {MakeShared<FJsonValueNumber>(1050), MakeShared<FJsonValueNumber>(0)});
			auto Buildings = Root->GetArrayField(TEXT("buildings")); Buildings.Add(MakeShared<FJsonValueObject>(Other)); Root->SetArrayField(TEXT("buildings"), Buildings);
			if (bLink)
			{
				auto Link = MakeShared<FJsonObject>(); Link->SetStringField(TEXT("id"), TEXT("AtoB")); Link->SetStringField(TEXT("kind"), TEXT("link"));
				Link->SetStringField(TEXT("connectsTo"), TEXT("B")); Link->SetNumberField(TEXT("level"), 1); Link->SetNumberField(TEXT("edge"), 1);
				Link->SetNumberField(TEXT("offset"), 500); Link->SetNumberField(TEXT("width"), 120);
				auto Doors = Interior()->GetArrayField(TEXT("entrances")); Doors.Add(MakeShared<FJsonValueObject>(Link)); Interior()->SetArrayField(TEXT("entrances"), Doors);
			}
		}
	};
	TArray<FMobilePlace> Places(FMobileRealSource& Source) { return Source.SearchPlaces(Source.ListSites()[0].Id, FString(), FMobilePlaceFilter()); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRealContractTest, "InNavi.Mobile.Source.RealContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRealContractTest::RunTest(const FString& Parameters)
{
	MobileRealTests::FProjectFiles Files; const auto Path = Files.Write();
	FString Before; FFileHelper::LoadFileToString(Before, *Path);
	FMobileRealSource Source(Path); MobileNavSourceTests::CheckContract(*this, Source);
	TestTrue(TEXT("real data carries no mock note"), Source.GetDataNote().IsEmpty());
	FString After; FFileHelper::LoadFileToString(After, *Path); TestEqual(TEXT("source never writes its project"), After, Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRealIndexTest, "InNavi.Mobile.Source.RealIndexAndIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRealIndexTest::RunTest(const FString& Parameters)
{
	MobileRealTests::FProjectFiles Files; Files.AddSecondBuilding(false);
	Files.Room(1)->SetStringField(TEXT("id"), TEXT("R1")); // room ids are scoped to a floor
	Files.Room(0)->SetStringField(TEXT("roomType"), TEXT("Classroom"));
	Files.Room(0)->SetArrayField(TEXT("tags"), {MakeShared<FJsonValueString>(TEXT("quiet"))});
	Files.Write(); Files.Write(TEXT("another.json"));
	FMobileRealSource Source(Files.Directory); const auto Sites = Source.ListSites();
	if (!TestEqual(TEXT("both projects are indexed"), Sites.Num(), 2)) return false;
	TSet<FString> Ids;
	for (const auto& Site : Sites) for (const auto& Place : Source.SearchPlaces(Site.Id, FString(), FMobilePlaceFilter()))
	{
		TestFalse(TEXT("room ids do not collide across sites, buildings or floors"), Ids.Contains(Place.Id)); Ids.Add(Place.Id);
		FMobileLevelMap Map; TestTrue(TEXT("map is found by globally unique building id"), Source.GetLevelMap(Place.BuildingId, Place.Level, Map));
		TestTrue(TEXT("maps contain the real room ids"), Map.Rooms.ContainsByPredicate([&](const auto& R) { return R.PlaceId == Place.Id; }));
	}
	TestEqual(TEXT("four actual places per project"), Ids.Num(), 8);
	const auto Hits = Source.SearchPlaces(Sites[0].Id, TEXT("quiet classroom"), FMobilePlaceFilter());
	TestEqual(TEXT("every query word matches room metadata"), Hits.Num(), 1);
	FMobilePlaceFilter Filter; Filter.BuildingId = Source.ListBuildings(Sites[0].Id)[1].Id; Filter.Level = 2;
	TestEqual(TEXT("building and level filters use real scopes"), Source.SearchPlaces(Sites[0].Id, FString(), Filter).Num(), 1);
	const auto Campus = Source.GetCampusMap(Sites[0].Id);
	TestTrue(TEXT("campus geometry includes the building offset"), Campus.Buildings[1].Outline[0].Equals({1050,0}) && Campus.Buildings[1].Offset.Equals({1050,0}));
	FMobileRealSource Reloaded(Files.Directory);
	TestEqual(TEXT("saved-place identities survive reopening"), Reloaded.SearchPlaces(Sites[0].Id, FString(), FMobilePlaceFilter())[0].Id,
		Source.SearchPlaces(Sites[0].Id, FString(), FMobilePlaceFilter())[0].Id);
	FMobileRouteRequest AcrossSites; AcrossSites.FromPlaceId = Source.SearchPlaces(Sites[0].Id, FString(), FMobilePlaceFilter())[0].Id;
	AcrossSites.ToPlaceId = Source.SearchPlaces(Sites[1].Id, FString(), FMobilePlaceFilter())[0].Id;
	TestFalse(TEXT("different projects cannot be joined by coincident raw ids"), Source.FindRoute(AcrossSites).bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRealRouteTest, "InNavi.Mobile.Source.RealRoutesAndPreferences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRealRouteTest::RunTest(const FString& Parameters)
{
	MobileRealTests::FProjectFiles Files;
	for (const FString& Type : {FString(TEXT("elevator")), FString(TEXT("stairs")), FString(TEXT("escalator"))})
	{
		for (int32 I = 0; I < 2; ++I) Files.Floor(I)->GetArrayField(TEXT("navNodes"))[0]->AsObject()->SetStringField(TEXT("type"), Type);
		FMobileRealSource Source(Files.Write()); const auto Places = MobileRealTests::Places(Source);
		FMobileRouteRequest Request; Request.FromPlaceId = Places[0].Id; Request.ToPlaceId = Places[1].Id;
		const auto Route = Source.FindRoute(Request);
		if (!TestTrue(TEXT("real room-to-room route is found"), Route.bFound)) continue;
		const auto Kind = Type == TEXT("elevator") ? EMobileStepKind::TakeLift : Type == TEXT("stairs") ? EMobileStepKind::TakeStairs : EMobileStepKind::TakeEscalator;
		TestTrue(TEXT("instructions name the connector actually used"), Route.Steps.ContainsByPredicate([&](const auto& S) { return S.Kind == Kind; }));
		const auto* Vertical = Route.Steps.FindByPredicate([&](const auto& S) { return S.Kind == Kind; });
		TestTrue(TEXT("vertical distance is actual metres, not a graph search penalty"), Vertical && FMath::IsNearlyEqual(Vertical->Metres, 3.0, .001));
		double Sum = 0; for (const auto& Step : Route.Steps) Sum += Step.Metres;
		TestTrue(TEXT("step distances account for the complete physical route"), FMath::IsNearlyEqual(Sum, Route.Metres, .001));
		Request.Preferences.bAllowLift = false; Request.Preferences.bAllowStairs = false; Request.Preferences.bAllowEscalator = false;
		const auto Blocked = Source.FindRoute(Request);
		TestTrue(TEXT("forbidden connectors are removed before pathfinding"), !Blocked.bFound && !Blocked.Error.IsEmpty());
		Request.Preferences = FMobileRoutePreferences::LiftOnly();
		TestEqual(TEXT("lift only cannot silently use stairs or escalators"), Source.FindRoute(Request).bFound, Type == TEXT("elevator"));
	}
	Files.Floor(0)->GetArrayField(TEXT("navNodes"))[0]->AsObject()->SetStringField(TEXT("type"), TEXT("elevator"));
	Files.Floor(1)->GetArrayField(TEXT("navNodes"))[0]->AsObject()->SetStringField(TEXT("type"), TEXT("elevator"));
	Files.Room(1)->SetArrayField(TEXT("openings"), {});
	FMobileRealSource Sealed(Files.Write()); const auto Places = MobileRealTests::Places(Sealed);
	FMobileRouteRequest Request; Request.FromPlaceId = Places[0].Id; Request.ToPlaceId = Places[1].Id;
	TestFalse(TEXT("a sealed room has no invented route"), Sealed.FindRoute(Request).bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRealNarrowRoomTest, "InNavi.Mobile.Source.NarrowRoom", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRealNarrowRoomTest::RunTest(const FString& Parameters)
{
	MobileRealTests::FProjectFiles Files;
	auto Room = Files.Room(0);
	auto SetWidth = [&](int32 Width)
	{
		TArray<TSharedPtr<FJsonValue>> Footprint;
		for (const FVector2D& Point : TArray<FVector2D>{{100, 100}, {100.0 + Width, 100}, {100.0 + Width, 600}, {100, 600}})
		{
			Footprint.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
				MakeShared<FJsonValueNumber>(Point.X), MakeShared<FJsonValueNumber>(Point.Y)}));
		}
		Room->SetArrayField(TEXT("footprint"), Footprint);
	};
	SetWidth(75);
	const auto Opening = Room->GetArrayField(TEXT("openings"))[0]->AsObject();
	Opening->SetNumberField(TEXT("edgeIndex"), 1);
	Opening->SetNumberField(TEXT("offset"), 150);
	FMobileRealSource Source(Files.Write());
	const auto Places = MobileRealTests::Places(Source);
	if (!TestEqual(TEXT("both rooms are indexed"), Places.Num(), 2)) return false;
	FMobileRouteRequest Request;
	Request.FromPlaceId = Places[1].Id;
	Request.ToPlaceId = Places[0].Id;
	TestTrue(TEXT("the planner finds a route into a narrow room even when both centroids fail"), Source.FindRoute(Request).bFound);
	SetWidth(50);
	Room->SetArrayField(TEXT("openings"), {});
	FMobileRealSource TooNarrow(Files.Write());
	TestFalse(TEXT("the fallback still refuses a room with insufficient clearance"), TooNarrow.FindRoute(Request).bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRealRestrictedTest, "InNavi.Mobile.Source.RestrictedRooms", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRealRestrictedTest::RunTest(const FString& Parameters)
{
	MobileRealTests::FProjectFiles Files;
	for (const bool bPrivate : {true, false})
	{
		Files.Room(1)->SetStringField(TEXT("access"), bPrivate ? TEXT("private") : TEXT("public"));
		Files.Room(1)->SetBoolField(TEXT("requireKey"), !bPrivate);
		FMobileRealSource Source(Files.Write());
		const auto Places = MobileRealTests::Places(Source);
		if (!TestEqual(TEXT("both rooms remain listed"), Places.Num(), 2)) return false;
		TestTrue(TEXT("the public room remains available"), Places[0].bAvailable);
		TestFalse(TEXT("the restricted room is marked unavailable"), Places[1].bAvailable);
		FMobileRouteRequest Request;
		Request.FromPlaceId = Places[0].Id;
		Request.ToPlaceId = Places[1].Id;
		auto Route = Source.FindRoute(Request);
		TestTrue(TEXT("directions explain why a restricted destination is refused"), !Route.bFound && Route.Error.Contains(TEXT("restricted")));
		Swap(Request.FromPlaceId, Request.ToPlaceId);
		Route = Source.FindRoute(Request);
		TestTrue(TEXT("directions also refuse a restricted start"), !Route.bFound && Route.Error.Contains(TEXT("restricted")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRealCampusTest, "InNavi.Mobile.Source.RealCampusRoutes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRealCampusTest::RunTest(const FString& Parameters)
{
	for (const bool bLink : {false, true})
	{
		MobileRealTests::FProjectFiles Files; Files.AddSecondBuilding(bLink);
		FMobileRealSource Source(Files.Write()); const auto Places = MobileRealTests::Places(Source);
		FMobileRouteRequest Request; Request.FromPlaceId = Places[0].Id; Request.ToPlaceId = Places[2].Id;
		const auto Route = Source.FindRoute(Request);
		if (!TestTrue(TEXT("a real route connects authored buildings"), Route.bFound)) continue;
		TestTrue(TEXT("the route leaves the source building"), Route.Steps.ContainsByPredicate([](const auto& S) { return S.Kind == EMobileStepKind::ExitBuilding; }));
		TestTrue(TEXT("the route enters the destination building"), Route.Steps.ContainsByPredicate([](const auto& S) { return S.Kind == EMobileStepKind::EnterBuilding; }));
		if (!bLink) TestTrue(TEXT("main doors route across the actual outdoor graph"), Route.Points.ContainsByPredicate([](const auto& P) { return P.Level == 0; }));
		TestTrue(TEXT("building-local points do not produce a zero-length campus route"), Route.Metres > 10.5);
		TestEqual(TEXT("the arrival is in the destination building"), Route.Steps.Last().BuildingId, Places[2].BuildingId);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRealLiveTest, "InNavi.Mobile.Source.RealLiveSnapshot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRealLiveTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	const auto Rooms = Fixture.Interior->GetRoomsOnFloor(1);
	if (!TestFalse(TEXT("fixture has authored rooms"), Rooms.IsEmpty())) return false;
	Fixture.Interior->SetRoomName(Rooms[0]->RoomId, TEXT("Unsaved mobile room"));
	const auto Before = LayoutValidation::Fingerprint(ULayoutValidationSession::Capture(*Fixture.Generator, *Fixture.Interior));
	FMobileRealSource Source(Fixture.Generator, Fixture.Interior, TEXT("live.json"));
	const auto Hits = Source.SearchPlaces(Source.ListSites()[0].Id, TEXT("Unsaved mobile room"), FMobilePlaceFilter());
	TestEqual(TEXT("live applied edits appear without saving"), Hits.Num(), 1);
	TestEqual(TEXT("indexing does not open another building"), Fixture.Interior->GetLoadedBuildingId(), FString(TEXT("E2")));
	TestEqual(TEXT("the editor data is untouched"), LayoutValidation::Fingerprint(ULayoutValidationSession::Capture(*Fixture.Generator, *Fixture.Interior)), Before);
	Fixture.Interior->SetRoomName(Rooms[0]->RoomId, TEXT("Changed after capture"));
	TestEqual(TEXT("the source holds a detached consistent snapshot"), Source.SearchPlaces(Source.ListSites()[0].Id, TEXT("Unsaved mobile room"), FMobilePlaceFilter()).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRealScreenTest, "InNavi.Mobile.Source.RealScreenIntegration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRealScreenTest::RunTest(const FString& Parameters)
{
	MobileRealTests::FProjectFiles Files; Files.AddSecondBuilding(false);
	auto Source = MakeShared<FMobileRealSource>(Files.Write()); const auto Places = MobileRealTests::Places(*Source);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (!TestNotNull(TEXT("mobile screen"), Screen)) { World->DestroyWorld(false); return false; }
	Screen->SetSource(Source);
	const auto Slate = Screen->TakeWidget();
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	TestFalse(TEXT("real Explore does not show mock-location text"), Screen->GetExplorePage()->IsDataNoteShown());
	Screen->ShowPlace(Places[2]); Screen->SendSheetEvent(EMobileSheetEvent::DirectionsRequested);
	TestFalse(TEXT("the route is still pending when the button handler returns"), Screen->GetCurrentRoute().bFound);
	TestTrue(TEXT("the page shows that calculation is pending"), Screen->GetRoutePage()->GetErrorString().Contains(TEXT("Finding route")));
	Screen->SendSheetEvent(EMobileSheetEvent::NavigationStarted);
	TestTrue(TEXT("a pending route cannot start navigation"), Screen->GetFlow().GetState() == EMobileSheetState::Route);

	FMobileRouteRequest Changed;
	Changed.FromPlaceId = Changed.ToPlaceId = Places[2].Id;
	Screen->ApplyRouteRequest(Changed);
	Slate->Tick(FGeometry(), FPlatformTime::Seconds(), 0.f);
	TestTrue(TEXT("the latest request supersedes the original job"), Screen->GetCurrentRoute().Error.Contains(TEXT("already at")));
	Changed.FromPlaceId = Places[0].Id;
	Screen->ApplyRouteRequest(Changed);
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (!Screen->GetCurrentRoute().bFound && Screen->GetCurrentRoute().Error.IsEmpty() && FPlatformTime::Seconds() < Deadline)
	{
		Slate->Tick(FGeometry(), FPlatformTime::Seconds(), 0.f);
		FPlatformProcess::SleepNoStats(0.001f);
	}
	if (TestTrue(TEXT("the real route completes across ticks"), Screen->GetCurrentRoute().bFound))
	{
		Screen->SendSheetEvent(EMobileSheetEvent::NavigationStarted);
		bool bSawOutside = false;
		for (int32 I = 0; I < 100 && Screen->GetFlow().GetState() == EMobileSheetState::Navigating; ++I)
		{
			if (Screen->GetCurrentRoute().Steps[Screen->GetNavStepIndex()].Level == 0) bSawOutside |= Screen->IsMapOverview();
			Screen->AdvanceNavigation();
		}
		TestTrue(TEXT("walking outside switches to the campus overview"), bSawOutside);
		TestTrue(TEXT("the real walk ends at arrival"), Screen->GetFlow().GetState() == EMobileSheetState::Arrived);
	}
	Screen->RebuildForAppearance();
	TestTrue(TEXT("appearance rebuild retains the real provider"), Screen->GetSource() == &Source.Get());
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRoomBookmarkIdentityTest, "InNavi.Mobile.Source.RoomBookmarkIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRoomBookmarkIdentityTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	Fixture.Interior->ClearLoadedInterior();
	Fixture.Generator->ClearAllBuildings();
	const double Unit = 1.0 / Fixture.Generator->GetScale();
	const FString BuildingId = TEXT("Bookmarks");
	Fixture.Generator->SpawnBuildingFromData(BuildingId, BuildingId,
		{{0, 0}, {1000 * Unit, 0}, {1000 * Unit, 800 * Unit}, {0, 800 * Unit}}, 1, 300, {0, 0});
	Fixture.Interior->LoadInteriorForBuilding(BuildingId);
	const auto Create = [&](const FString& Name, double Left)
	{
		FString Error;
		return Fixture.Interior->CreateRoomFromFootprint(
			{{Left * Unit, 100 * Unit}, {(Left + 300) * Unit, 100 * Unit},
			{(Left + 300) * Unit, 400 * Unit}, {Left * Unit, 400 * Unit}}, FString(), Name, true, false, false, Error);
	};
	const auto* Original = Create(TEXT("Original"), 100);
	if (!TestNotNull(TEXT("original room exists"), Original)) return false;
	const FString RoomId = Original->RoomId;
	MobileRealTests::FProjectFiles Files;
	const FString Path = Files.Directory / TEXT("bookmarks.json");
	const auto Save = [&]()
	{
		Fixture.Interior->SyncInteriorToBuildingGenerator();
		return Fixture.Generator->SaveBuildingsToFile(Path);
	};
	TestTrue(TEXT("the first project is saved"), Save());
	FMobileRealSource Before(Path);
	const auto FirstPlaces = MobileRealTests::Places(Before);
	if (!TestEqual(TEXT("the original room appears in mobile"), FirstPlaces.Num(), 1)) return false;
	TestTrue(TEXT("the editor can delete the room"), Fixture.Interior->DeleteRoom(RoomId));
	const auto* Replacement = Create(TEXT("Replacement"), 500);
	if (!TestNotNull(TEXT("replacement room exists"), Replacement)) return false;
	TestEqual(TEXT("the editor reuses the display ID"), Replacement->RoomId, RoomId);
	TestTrue(TEXT("the changed project is saved"), Save());
	FMobileRealSource After(Path);
	FMobilePlace Place;
	TestFalse(TEXT("the old bookmark cannot resolve to the replacement"), After.FindPlace(FirstPlaces[0].Id, Place));
	const auto SecondPlaces = MobileRealTests::Places(After);
	if (TestEqual(TEXT("the replacement is indexed"), SecondPlaces.Num(), 1))
		TestNotEqual(TEXT("the replacement has a separate persisted identity"), SecondPlaces[0].Id, FirstPlaces[0].Id);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRealDefaultTest, "InNavi.Mobile.Source.RealDefaultAndAndroidStaging", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRealDefaultTest::RunTest(const FString& Parameters)
{
	FConfigFile AndroidConfig;
	FConfigCacheIni::LoadLocalIniFile(AndroidConfig, TEXT("Game"), true, TEXT("Android"));
	TArray<FString> UFS, NonUFS;
	AndroidConfig.GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("DirectoriesToAlwaysStageAsUFS"), UFS);
	AndroidConfig.GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("DirectoriesToAlwaysStageAsNonUFS"), NonUFS);
	TestTrue(TEXT("Android stages project data inside the APK filesystem"), UFS.Contains(TEXT("(Path=\"../Data\")")));
	TestFalse(TEXT("Android does not rely on a separately deployed loose Data directory"), NonUFS.Contains(TEXT("(Path=\"../Data\")")));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (!TestNotNull(TEXT("default mobile screen"), Screen)) { World->DestroyWorld(false); return false; }
	Screen->TakeWidget(); const auto Sites = Screen->GetSource()->ListSites();
	TestFalse(TEXT("a normal launch finds the staged project library"), Sites.IsEmpty());
	TestTrue(TEXT("production default is real project data"), Screen->GetSource()->GetDataNote().IsEmpty());
	FMobilePlace MockPlace; TestFalse(TEXT("prototype places are not quietly substituted"), Screen->GetSource()->FindPlace(TEXT("E2-01-101"), MockPlace));
	TestFalse(TEXT("authored rooms are visible on the initial level"), Screen->GetSearchPage()->GetShownPlaces().IsEmpty());
	World->DestroyWorld(false); return true;
}

// "Turn left" / "Turn right" must be what the person sees on the map, which draws footprint y downward: heading east and then south
// (down the screen) is a RIGHT turn.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRealTurnDirectionTest, "InNavi.Mobile.Source.RealTurnDirections", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRealTurnDirectionTest::RunTest(const FString& Parameters)
{
	const FVector2D East(1, 0), West(-1, 0), South(0, 1), North(0, -1);
	TestFalse(TEXT("east, then down the screen, is a right turn"), FMobileRealSource::IsLeftTurnOnMap(East, South));
	TestTrue(TEXT("east, then up the screen, is a left turn"), FMobileRealSource::IsLeftTurnOnMap(East, North));
	TestTrue(TEXT("west, then down the screen, is a left turn"), FMobileRealSource::IsLeftTurnOnMap(West, South));
	TestFalse(TEXT("south, then west, is a right turn"), FMobileRealSource::IsLeftTurnOnMap(South, West));

	// The same on real routes: every turn step agrees with the way the route's own line bends on the map.
	FMobileRealSource Source(FPaths::ProjectDir() / TEXT("../docs/test-data/entrance-test-2-with-doors.json"));
	const TArray<FMobileSite> Sites = Source.ListSites();
	if (!TestFalse(TEXT("the test project is read"), Sites.IsEmpty())) return false;
	const TArray<FMobilePlace> Places = Source.SearchPlaces(Sites[0].Id, FString(), FMobilePlaceFilter());
	int32 Routes = 0, Turns = 0, Reversed = 0;
	for (int32 I = 0; I < Places.Num() && Routes < 12; I += 5)
	{
		for (int32 J = Places.Num() - 1; J > I && Routes < 12; J -= 7)
		{
			FMobileRouteRequest Request; Request.FromPlaceId = Places[I].Id; Request.ToPlaceId = Places[J].Id;
			const FMobileRoute Route = Source.FindRoute(Request);
			if (!Route.bFound) continue;
			++Routes;
			for (int32 K = 1; K + 1 < Route.Points.Num(); ++K)
			{
				const FMobileRoutePoint &A = Route.Points[K - 1], &B = Route.Points[K], &C = Route.Points[K + 1];
				if (A.BuildingId != B.BuildingId || B.BuildingId != C.BuildingId || A.Level != B.Level || B.Level != C.Level) continue;
				const FVector2D In = (B.Position - A.Position).GetSafeNormal(), Out = (C.Position - B.Position).GetSafeNormal();
				if (In.IsNearlyZero() || Out.IsNearlyZero() || FVector2D::DotProduct(In, Out) >= 0.90) continue;
				const FMobileStep* Step = Route.Steps.FindByPredicate([&B](const FMobileStep& S)
					{ return S.Position.Equals(B.Position, 0.01) && (S.Kind == EMobileStepKind::TurnLeft || S.Kind == EMobileStepKind::TurnRight); });
				if (!Step) continue;
				++Turns;
				// Worked out another way: on a map with y down, the direction to the walker's left is (In.Y, -In.X) (heading east, left is up the
				// screen), and the route turns left when it goes on that way.
				const FVector2D LeftOfWalker(In.Y, -In.X);
				const bool bBendsLeft = FVector2D::DotProduct(Out, LeftOfWalker) > 0;
				Reversed += (Step->Kind == EMobileStepKind::TurnLeft) != bBendsLeft ? 1 : 0;
			}
		}
	}
	TestTrue(TEXT("the routes have turns to check"), Routes >= 5 && Turns >= 10);
	TestEqual(TEXT("no turn step says the opposite of the map"), Reversed, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRealMissingDataTest, "InNavi.Mobile.Source.RealMissingDataAndOverrides", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRealMissingDataTest::RunTest(const FString& Parameters)
{
	MobileRealTests::FProjectFiles Files; const auto Path = Files.Write();
	const FString OverrideDirectory = Files.Directory / TEXT("Overrides"); IFileManager::Get().MakeDirectory(*OverrideDirectory, true);
	Files.Root->SetStringField(TEXT("projectName"), TEXT("Updated campus")); Files.Write(TEXT("Overrides/campus.json"));
	FMobileRealSource Source(Files.Directory, OverrideDirectory);
	TestEqual(TEXT("an updated file replaces, rather than duplicates, its shipped site"), Source.ListSites().Num(), 1);
	TestEqual(TEXT("local data takes precedence"), Source.ListSites()[0].Name, FString(TEXT("Updated campus")));
	FMobileRealSource Original(Path);
	TestEqual(TEXT("updating project data preserves saved-place identities"), MobileRealTests::Places(Source)[0].Id, MobileRealTests::Places(Original)[0].Id);
	FMobileLevelMap MissingMap;
	TestFalse(TEXT("unknown buildings cannot reuse another site's plan"), Original.GetLevelMap(TEXT("no-such-building"), 1, MissingMap));
	FMobileRealSource Empty(Files.Directory / TEXT("empty.json"));
	TestTrue(TEXT("missing data does not fall back to invented places"), Empty.ListSites().IsEmpty());
	FMobileRouteRequest Request; Request.ToPlaceId = TEXT("no-such-place");
	const auto Route = Empty.FindRoute(Request); TestTrue(TEXT("missing destinations produce an explanation"), !Route.bFound && !Route.Error.IsEmpty());
	return true;
}

#endif
