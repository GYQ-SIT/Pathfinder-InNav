#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "MobileNavSource.h"
#include "Misc/AutomationTest.h"

namespace MobileNavSourceTests
{
	// The promises the screens rely on. Reports through Test so the same checks run on any source.
	inline void CheckContract(FAutomationTestBase& Test, IMobileNavSource& Source)
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


#endif
