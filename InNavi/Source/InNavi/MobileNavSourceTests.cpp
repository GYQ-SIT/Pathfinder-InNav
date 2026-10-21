#if WITH_DEV_AUTOMATION_TESTS

#include "MobileMockSource.h"
#include "MobileNavSource.h"

#include "Misc/AutomationTest.h"

// What every IMobileNavSource must do, whoever answers (the mock now, the real one later), and what the mock's prototype data looks like.

namespace MobileNavSourceTests
{
	// The promises the screens rely on. Reports through Test so the same checks run on any source.
	void CheckContract(FAutomationTestBase& Test, IMobileNavSource& Source)
	{
		const TArray<FMobileSite> Sites = Source.ListSites();
		Test.TestFalse(TEXT("there is at least one site"), Sites.IsEmpty());
		TSet<FString> SiteIds;
		for (const FMobileSite& Site : Sites)
		{
			Test.TestTrue(TEXT("a site has an id and a name"), !Site.Id.IsEmpty() && !Site.Name.IsEmpty());
			Test.TestFalse(TEXT("site ids are unique"), SiteIds.Contains(Site.Id));
			SiteIds.Add(Site.Id);

			const TArray<FMobileBuilding> Buildings = Source.ListBuildings(Site.Id);
			Test.TestEqual(TEXT("a site's building count is its buildings"), Buildings.Num(), Site.BuildingCount);
			TSet<FString> BuildingIds;
			for (const FMobileBuilding& Building : Buildings)
			{
				Test.TestFalse(TEXT("a building has levels"), Building.Levels.IsEmpty());
				Test.TestFalse(TEXT("building ids are unique"), BuildingIds.Contains(Building.Id));
				BuildingIds.Add(Building.Id);
				TSet<int32> Numbers;
				for (const FMobileLevel& Level : Building.Levels)
				{
					Test.TestFalse(TEXT("a level has a label"), Level.Label.IsEmpty());
					Test.TestFalse(TEXT("level numbers are unique in a building"), Numbers.Contains(Level.Level));
					Numbers.Add(Level.Level);
				}
			}

			// Every place belongs to a building and level of its site, and can be found again by its id.
			const TArray<FMobilePlace> All = Source.SearchPlaces(Site.Id, FString(), FMobilePlaceFilter());
			TSet<FString> PlaceIds;
			const TArray<FString> Categories = Source.ListCategories(Site.Id);
			for (const FMobilePlace& Place : All)
			{
				Test.TestFalse(TEXT("place ids are unique"), PlaceIds.Contains(Place.Id));
				PlaceIds.Add(Place.Id);
				const FMobileBuilding* Building = Buildings.FindByPredicate([&Place](const FMobileBuilding& B) { return B.Id == Place.BuildingId; });
				Test.TestNotNull(TEXT("a place's building is one of the site's"), Building);
				if (Building)
				{
					Test.TestTrue(TEXT("and its level is one of that building's"), Building->Levels.ContainsByPredicate([&Place](const FMobileLevel& L) { return L.Level == Place.Level && L.Label == Place.LevelLabel; }));
				}
				Test.TestTrue(TEXT("a place has a name, a code and a category the filters know"), !Place.Name.IsEmpty() && !Place.Code.IsEmpty() && Categories.Contains(Place.Category));
				FMobilePlace Found;
				Test.TestTrue(TEXT("a place is found by its id"), Source.FindPlace(Place.Id, Found) && Found.Id == Place.Id && Found.Name == Place.Name);
			}

			// A search gives some of the places, never new ones; a word that is nowhere gives none.
			const TArray<FMobilePlace> Hits = Source.SearchPlaces(Site.Id, All.IsEmpty() ? FString() : All[0].Name, FMobilePlaceFilter());
			Test.TestTrue(TEXT("searching a place's name finds it"), All.IsEmpty() || Hits.ContainsByPredicate([&All](const FMobilePlace& P) { return P.Id == All[0].Id; }));
			Test.TestTrue(TEXT("a search gives only the site's places"), Hits.Num() <= All.Num());
			Test.TestTrue(TEXT("a word that is nowhere gives nothing"), Source.SearchPlaces(Site.Id, TEXT("zzqxjw"), FMobilePlaceFilter()).IsEmpty());

			// Every level of every building has a floor plan whose rooms are its places, and the site has an overview with every building.
			for (const FMobileBuilding& Building : Buildings)
			{
				for (const FMobileLevel& Level : Building.Levels)
				{
					FMobileLevelMap Map;
					Test.TestTrue(TEXT("a level has a floor plan"), Source.GetLevelMap(Building.Id, Level.Level, Map));
					Test.TestTrue(TEXT("with an outline"), Map.Outline.Num() >= 3);
					TArray<FString> OnThisLevel;
					for (const FMobilePlace& Place : All)
					{
						if (Place.BuildingId == Building.Id && Place.Level == Level.Level) OnThisLevel.Add(Place.Id);
					}
					for (const FMobileMapRoom& Room : Map.Rooms)
					{
						Test.TestTrue(TEXT("a room has a shape"), Room.Polygon.Num() >= 3);
						Test.TestTrue(TEXT("a room that is a place is one of the level's"), Room.PlaceId.IsEmpty() || OnThisLevel.Contains(Room.PlaceId));
					}
					for (const FString& PlaceId : OnThisLevel)
					{
						Test.TestTrue(TEXT("every place of the level is a room on its plan"), Map.Rooms.ContainsByPredicate([&PlaceId](const FMobileMapRoom& R) { return R.PlaceId == PlaceId; }));
					}
				}
				FMobileLevelMap NoSuchLevel;
				Test.TestFalse(TEXT("a level the building does not have has no plan"), Source.GetLevelMap(Building.Id, 99, NoSuchLevel));
			}
			const FMobileCampusMap Campus = Source.GetCampusMap(Site.Id);
			Test.TestEqual(TEXT("the overview has every building"), Campus.Buildings.Num(), Buildings.Num());
			for (const FMobileCampusBuilding& CampusBuilding : Campus.Buildings)
			{
				Test.TestTrue(TEXT("a building on the overview has a shape and is one of the site's"), CampusBuilding.Outline.Num() >= 3 && BuildingIds.Contains(CampusBuilding.BuildingId));
			}

			// A route between two places ends where the person goes, in a time that fits its length.
			if (All.Num() >= 2)
			{
				FMobileRouteRequest Request;
				Request.FromPlaceId = All[0].Id;
				Request.ToPlaceId = All.Last().Id;
				const FMobileRoute Route = Source.FindRoute(Request);
				Test.TestTrue(TEXT("two places of a site have a route"), Route.bFound);
				if (Route.bFound)
				{
					Test.TestFalse(TEXT("with steps"), Route.Steps.IsEmpty());
					Test.TestTrue(TEXT("the last step is the arrival"), Route.Steps.Last().Kind == EMobileStepKind::Arrive && Route.Steps.Last().Text.Contains(All.Last().Name));
					Test.TestEqual(TEXT("the time follows the length"), Route.Minutes, MobileNav::MinutesForMetres(Route.Metres));
					Test.TestTrue(TEXT("a route has points to draw"), Route.Points.Num() >= 2);
				}
			}
		}
		FMobilePlace Nothing;
		Test.TestFalse(TEXT("an unknown place is not found"), Source.FindPlace(TEXT("no-such-place"), Nothing));
		FMobileRouteRequest Bad;
		Bad.ToPlaceId = TEXT("no-such-place");
		const FMobileRoute NoRoute = Source.FindRoute(Bad);
		Test.TestFalse(TEXT("a route to an unknown place is not found"), NoRoute.bFound);
		Test.TestFalse(TEXT("and says why"), NoRoute.Error.IsEmpty());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMockContractTest, "InNavi.Mobile.Source.MockContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMockContractTest::RunTest(const FString& Parameters)
{
	FMobileMockSource Source;
	MobileNavSourceTests::CheckContract(*this, Source);
	return true;
}

// The prototype's data: the three sites, E6's mezzanine, the mall's closed restaurant, four places on every level.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMockDataTest, "InNavi.Mobile.Source.MockData", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMockDataTest::RunTest(const FString& Parameters)
{
	FMobileMockSource Source;
	const TArray<FMobileSite> Sites = Source.ListSites();
	TestEqual(TEXT("three sites"), Sites.Num(), 3);
	TestEqual(TEXT("the mall has 7 levels"), Sites[0].LevelCount, 7);
	TestEqual(TEXT("the campus has 3 buildings"), Sites[2].BuildingCount, 3);

	const TArray<FMobileBuilding> Campus = Source.ListBuildings(TEXT("sit-punggol"));
	TestEqual(TEXT("E2, E6 and W2"), Campus.Num(), 3);
	TArray<FString> E6Labels;
	for (const FMobileLevel& Level : Campus[1].Levels) E6Labels.Add(Level.Label);
	TestEqual(TEXT("E6 has a mezzanine between L2 and L3"), FString::Join(E6Labels, TEXT(",")), FString(TEXT("L1,L2,L2M,L3")));

	const FMobilePlaceFilter Nothing;
	TestEqual(TEXT("four places on each of E2's 3 levels, E6's 4 and W2's 2"), Source.SearchPlaces(TEXT("sit-punggol"), FString(), Nothing).Num(), 4 * (3 + 4 + 2));
	FMobilePlace Lounge;
	TestTrue(TEXT("E2's study lounge exists"), Source.FindPlace(TEXT("E2-01-101"), Lounge));
	TestEqual(TEXT("with the code the prototype shows"), Lounge.Code, FString(TEXT("E2-01-101")));
	TestEqual(TEXT("in the Learning category"), Lounge.Category, FString(TEXT("Learning")));
	TestTrue(TEXT("and it is listed under its building and level"), Lounge.GroupLabel().Contains(TEXT("E2")) && Lounge.GroupLabel().Contains(TEXT("L1")));
	FMobilePlace Mezzanine;
	TestTrue(TEXT("a place on E6's mezzanine exists"), Source.FindPlace(TEXT("E6-2M-102"), Mezzanine) && Mezzanine.LevelLabel == TEXT("L2M") && Mezzanine.Level == 3);

	// Search: words, codes, categories, building, filters.
	TestEqual(TEXT("'classroom' finds one on every level"), Source.SearchPlaces(TEXT("sit-punggol"), TEXT("classroom"), Nothing).Num(), 9);
	TestEqual(TEXT("'e6 classroom' only E6's"), Source.SearchPlaces(TEXT("sit-punggol"), TEXT("e6 classroom"), Nothing).Num(), 4);
	TestEqual(TEXT("a code finds its place"), Source.SearchPlaces(TEXT("sit-punggol"), TEXT("W2-02-103"), Nothing).Num(), 1);
	FMobilePlaceFilter Facilities;
	Facilities.Categories = { TEXT("Facilities") };
	TestEqual(TEXT("the Facilities filter keeps restrooms and lift lobbies"), Source.SearchPlaces(TEXT("sit-punggol"), FString(), Facilities).Num(), 2 * (3 + 4 + 2));
	FMobilePlaceFilter OnLevel;
	OnLevel.BuildingId = TEXT("E2");
	OnLevel.Level = 2;
	TestEqual(TEXT("a building and level filter gives its four places"), Source.SearchPlaces(TEXT("sit-punggol"), FString(), OnLevel).Num(), 4);
	TestEqual(TEXT("categories of the mall are Dining and Facilities"), FString::Join(Source.ListCategories(TEXT("paya-lebar")), TEXT(",")), FString(TEXT("Dining,Facilities")));
	FMobilePlaceFilter Open;
	Open.bAvailableOnly = true;
	const int32 MallAll = Source.SearchPlaces(TEXT("paya-lebar"), FString(), Nothing).Num();
	TestEqual(TEXT("the closed restaurant drops out of 'available only'"), Source.SearchPlaces(TEXT("paya-lebar"), FString(), Open).Num(), MallAll - 1);
	TestTrue(TEXT("and every other site is untouched"), Source.SearchPlaces(TEXT("sit-punggol"), FString(), Open).Num() == Source.SearchPlaces(TEXT("sit-punggol"), FString(), Nothing).Num());
	TestTrue(TEXT("an unknown site has no places"), Source.SearchPlaces(TEXT("nowhere"), FString(), Nothing).IsEmpty());

	// Places are in building, level and list order.
	const TArray<FMobilePlace> InOrder = Source.SearchPlaces(TEXT("sit-punggol"), FString(), Nothing);
	TestEqual(TEXT("the first place is E2's first"), InOrder[0].Id, FString(TEXT("E2-01-101")));
	TestEqual(TEXT("the fifth is on E2's next level"), InOrder[4].LevelLabel, FString(TEXT("L2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMockRouteTest, "InNavi.Mobile.Source.MockRoute", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMockRouteTest::RunTest(const FString& Parameters)
{
	FMobileMockSource Source;

	// From "my location": walk from the door to the place.
	FMobileRouteRequest FromMe;
	FromMe.ToPlaceId = TEXT("E2-01-101");
	FMobileRoute Route = Source.FindRoute(FromMe);
	if (!TestTrue(TEXT("a route from my location"), Route.bFound)) return false;
	TestEqual(TEXT("three steps: along the corridor, turn, arrive"), Route.Steps.Num(), 3);
	TestEqual(TEXT("the second is a turn"), Route.Steps[1].Text, FString(TEXT("Turn towards Study lounge")));
	TestTrue(TEXT("on the left, as the lounge is on the left of the plan"), Route.Steps[1].Kind == EMobileStepKind::TurnLeft);
	TestEqual(TEXT("the last says where"), Route.Steps[2].Text, FString(TEXT("You have arrived: Study lounge")));
	TestTrue(TEXT("a few minutes at most"), Route.Minutes >= 1 && Route.Minutes <= 3);
	double Sum = 0.0;
	for (const FMobileStep& Step : Route.Steps) Sum += Step.Metres;
	TestTrue(TEXT("the length is the steps' lengths"), FMath::IsNearlyEqual(Sum, Route.Metres, 0.01));

	// Two places on the same level.
	FMobileRouteRequest SameLevel;
	SameLevel.FromPlaceId = TEXT("E2-01-101");
	SameLevel.ToPlaceId = TEXT("E2-01-103");
	Route = Source.FindRoute(SameLevel);
	TestTrue(TEXT("between two places of one level"), Route.bFound && Route.Steps.Num() == 3);
	SameLevel.ToPlaceId = SameLevel.FromPlaceId;
	TestFalse(TEXT("a place to itself is no route"), Source.FindRoute(SameLevel).bFound);

	// "My location" in a building and level the person said: the same level needs no lift, another building means walking out and in.
	FMobileRouteRequest Here;
	Here.ToPlaceId = TEXT("E2-02-101");
	Here.MyLocationBuildingId = TEXT("E2");
	Here.MyLocationLevel = 2;
	Route = Source.FindRoute(Here);
	TestTrue(TEXT("from level 2 of E2"), Route.bFound);
	TestFalse(TEXT("to a place on level 2 there is no lift"), Route.Steps.ContainsByPredicate([](const FMobileStep& S) { return S.Kind == EMobileStepKind::TakeLift; }));
	Here.MyLocationLevel = 1;
	TestTrue(TEXT("from level 1 it is a lift ride"), Source.FindRoute(Here).Steps.ContainsByPredicate([](const FMobileStep& S) { return S.Kind == EMobileStepKind::TakeLift; }));
	Here.MyLocationBuildingId = TEXT("E6");
	Here.MyLocationLevel = 3;
	Route = Source.FindRoute(Here);
	TestTrue(TEXT("from E6's mezzanine to E2"), Route.bFound && Route.Steps.ContainsByPredicate([](const FMobileStep& S) { return S.Kind == EMobileStepKind::ExitBuilding && S.Text == TEXT("Leave E6"); }));
	Here.MyLocationBuildingId = TEXT("nowhere");
	TestTrue(TEXT("a building the site does not have is ignored: the door of the destination's building"), Source.FindRoute(Here).bFound);
	Here.MyLocationBuildingId = TEXT("E2");
	Here.MyLocationLevel = 9;
	TestTrue(TEXT("a level the building does not have goes to its first"), Source.FindRoute(Here).bFound);

	// Up a level: by lift unless the person does not want it.
	FMobileRouteRequest Up;
	Up.FromPlaceId = TEXT("E2-01-101");
	Up.ToPlaceId = TEXT("E2-03-102");
	Route = Source.FindRoute(Up);
	TestTrue(TEXT("up to level 3"), Route.bFound);
	TestTrue(TEXT("by the lift"), Route.Steps.ContainsByPredicate([](const FMobileStep& S) { return S.Kind == EMobileStepKind::TakeLift && S.Text == TEXT("Take the lift to L3"); }));
	Up.Preferences.bAllowLift = false;
	Route = Source.FindRoute(Up);
	TestTrue(TEXT("without the lift, by the stairs (the campus has no escalator)"), Route.bFound && Route.Steps.ContainsByPredicate([](const FMobileStep& S) { return S.Kind == EMobileStepKind::TakeStairs; }));
	Up.Preferences.bAllowStairs = false;
	Route = Source.FindRoute(Up);
	TestFalse(TEXT("with neither there is no route"), Route.bFound);
	TestFalse(TEXT("and it says why"), Route.Error.IsEmpty());
	FMobileRouteRequest LiftOnly = Up;
	LiftOnly.Preferences = FMobileRoutePreferences::LiftOnly();
	TestTrue(TEXT("lift only is fine when there is a lift"), Source.FindRoute(LiftOnly).bFound);

	// The mall has escalators.
	FMobileRouteRequest Mall;
	Mall.FromPlaceId = TEXT("PLM-01-101");
	Mall.ToPlaceId = TEXT("PLM-04-103");
	Mall.Preferences.bAllowLift = false;
	Route = Source.FindRoute(Mall);
	TestTrue(TEXT("in the mall, without the lift, by the escalator"), Route.bFound && Route.Steps.ContainsByPredicate([](const FMobileStep& S) { return S.Kind == EMobileStepKind::TakeEscalator; }));

	// To another building: out of one, along the path, into the other.
	FMobileRouteRequest Across;
	Across.FromPlaceId = TEXT("E2-02-101");
	Across.ToPlaceId = TEXT("E6-2M-102");
	Route = Source.FindRoute(Across);
	TestTrue(TEXT("between buildings"), Route.bFound);
	int32 ExitAt = INDEX_NONE, EnterAt = INDEX_NONE;
	for (int32 i = 0; i < Route.Steps.Num(); ++i)
	{
		if (Route.Steps[i].Kind == EMobileStepKind::ExitBuilding) ExitAt = i;
		if (Route.Steps[i].Kind == EMobileStepKind::EnterBuilding) EnterAt = i;
	}
	TestTrue(TEXT("a person leaves E2 and then enters E6"), ExitAt != INDEX_NONE && EnterAt > ExitAt);
	TestEqual(TEXT("the step names the building"), Route.Steps[ExitAt].Text, FString(TEXT("Leave E2")));
	TestEqual(TEXT("and the arrival is on the mezzanine"), Route.Steps.Last().LevelLabel, FString(TEXT("L2M")));
	TestTrue(TEXT("a longer way than within a building"), Route.Metres > Source.FindRoute(SameLevel).Metres + 100.0);

	// Things that are not routes.
	FMobileRouteRequest Other;
	Other.FromPlaceId = TEXT("E2-01-101");
	Other.ToPlaceId = TEXT("PLM-01-101");
	TestFalse(TEXT("places of two sites have no route"), Source.FindRoute(Other).bFound);
	Other.FromPlaceId = TEXT("no-such-place");
	Other.ToPlaceId = TEXT("E2-01-101");
	TestFalse(TEXT("from an unknown place is none"), Source.FindRoute(Other).bFound);

	// Minutes.
	TestEqual(TEXT("100 m is 2 minutes, as in the prototype"), MobileNav::MinutesForMetres(100.0), 2);
	TestEqual(TEXT("a few metres is still one minute"), MobileNav::MinutesForMetres(3.0), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
