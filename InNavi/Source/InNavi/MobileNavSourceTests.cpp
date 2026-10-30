#if WITH_DEV_AUTOMATION_TESTS

#include "MobileMockSource.h"
#include "MobileNavSource.h"

#include "Misc/AutomationTest.h"

// What every IMobileNavSource must do, whoever answers (the mock now, the real one later), and what the mock's prototype data looks like.

#include "MobileNavSourceContract.h"

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
