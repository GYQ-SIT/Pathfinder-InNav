#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileExplorePageWidget.h"
#include "MobileMapWidget.h"
#include "MobileMockSource.h"
#include "MobileNavigatePageWidget.h"
#include "MobilePlacePageWidget.h"
#include "MobileRoutePageWidget.h"
#include "MobileSelectWidget.h"
#include "MobileSearchPageWidget.h"
#include "MobileSheetFlow.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// The Explore tab: a card for each site, and opening one starts the app again for that site.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileExplorePageTest, "InNavi.Mobile.ExplorePage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileExplorePageTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobileExplorePageWidget* Page = CreateWidget<UMobileExplorePageWidget>(World, UMobileExplorePageWidget::StaticClass());
	if (!TestNotNull(TEXT("the page is created"), Page)) return false;
	Page->TakeWidget();
	for (const TCHAR* Name : { TEXT("Txt_ExploreTitle"), TEXT("Txt_ExploreSubtitle"), TEXT("Txt_ExploreSites"), TEXT("ExploreCards"), TEXT("ExploreScroll") })
	{
		TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Page->FindNamedWidget(Name));
	}
	TestEqual(TEXT("the title"), Cast<UTextBlock>(Page->FindNamedWidget(TEXT("Txt_ExploreTitle")))->GetText().ToString(), FString(TEXT("Find your way indoors")));
	TestTrue(TEXT("no cards without a source"), Page->GetShownSites().IsEmpty());

	Page->SetSource(&Source);
	const TArray<FMobileSite>& Sites = Page->GetShownSites();
	TestEqual(TEXT("a card for each site"), Sites.Num(), 3);
	TestEqual(TEXT("in the source's order"), Sites[0].Name, FString(TEXT("Paya Lebar Mall")));
	TestEqual(TEXT("the mall counts its levels"), Page->GetCountLabel(Sites[0]), FString(TEXT("7 levels")));
	TestEqual(TEXT("so does the centre"), Page->GetCountLabel(Sites[1]), FString(TEXT("3 levels")));
	TestEqual(TEXT("the campus counts its buildings"), Page->GetCountLabel(Sites[2]), FString(TEXT("3 buildings")));
	TestTrue(TEXT("simulated data says so"), Page->IsDataNoteShown());

	FString Chosen;
	Page->SetOnSiteChosen([&Chosen](const FString& Id) { Chosen = Id; });
	Page->ChooseSite(TEXT("sit-punggol"));
	TestEqual(TEXT("a tap on a card reports the site"), Chosen, FString(TEXT("sit-punggol")));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileOpenSiteTest, "InNavi.Mobile.OpenSite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileOpenSiteTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (Screen) Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	UMobileExplorePageWidget* Explore = Screen->GetExplorePage();
	if (!TestNotNull(TEXT("the screen has the Explore page"), Explore)) return false;
	auto SiteName = [Screen]() { return Cast<UTextBlock>(Screen->FindNamedWidget(TEXT("SiteName")))->GetText().ToString(); };
	auto Click = [](UWidget* Widget) { Cast<UButton>(Widget)->OnClicked.Broadcast(); };

	// The app opens on Explore, and the campus is the first site opened.
	TestEqual(TEXT("on Explore"), Screen->GetActiveTab(), UMobileAppWidget::ETab::Explore);
	TestEqual(TEXT("with the campus ready"), Screen->GetSiteId(), FString(TEXT("sit-punggol")));

	// A tap on the mall's card opens the mall on the map tab, started again.
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-101"));
	Screen->SetActiveTab(UMobileAppWidget::ETab::Explore);
	Explore->ChooseSite(TEXT("paya-lebar"));
	TestEqual(TEXT("the map tab"), Screen->GetActiveTab(), UMobileAppWidget::ETab::Directory);
	TestEqual(TEXT("the mall"), Screen->GetSiteId(), FString(TEXT("paya-lebar")));
	TestEqual(TEXT("its building and level at the top"), SiteName(), FString(TEXT("Paya Lebar Mall · L1")));
	TestEqual(TEXT("started again at Where to?"), Screen->GetFlow().GetState(), EMobileSheetState::Search);
	TestTrue(TEXT("with no place chosen and no start"), Screen->GetSelectedPlace().Id.IsEmpty() && Screen->GetStartPlaceId().IsEmpty());
	TestEqual(TEXT("the search lists the mall's first level"), Screen->GetSearchPage()->GetShownPlaces().Num(), 4);
	TestEqual(TEXT("(Pepper Lunch first)"), Screen->GetSearchPage()->GetShownPlaces()[0].Name, FString(TEXT("Pepper Lunch")));
	TestTrue(TEXT("the map shows the mall's first level"), Screen->GetMapView()->GetBuildingId() == TEXT("PLM") && Screen->GetMapView()->GetLevel() == 1);
	TestTrue(TEXT("with the person in the mall (My location)"), Screen->GetMyLocationBuildingId() == TEXT("PLM") && Screen->GetMapView()->HasPosition());

	// A walk in the mall: by escalator when the lift is off (the campus has none).
	Screen->GetSearchPage()->ChoosePlace(TEXT("PLM-03-103"));
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")));
	TestTrue(TEXT("a route in the mall"), Screen->GetCurrentRoute().bFound);
	Screen->GetRoutePage()->TapOption(TEXT("lift"));
	TestTrue(TEXT("by escalator without the lift"), Screen->GetCurrentRoute().Steps.ContainsByPredicate([](const FMobileStep& S) { return S.Kind == EMobileStepKind::TakeEscalator; }));
	Click(Screen->GetRoutePage()->FindNamedWidget(TEXT("Btn_StartNavigation")));
	TestEqual(TEXT("walking in the mall"), Screen->GetFlow().GetState(), EMobileSheetState::Navigating);

	// Opening another site in the middle of a walk starts that site again.
	Screen->SetActiveTab(UMobileAppWidget::ETab::Explore);
	Explore->ChooseSite(TEXT("sit-punggol"));
	TestEqual(TEXT("the campus (its first building and level at the top)"), SiteName(), FString(TEXT("E2 · L1")));
	TestEqual(TEXT("back at Where to?"), Screen->GetFlow().GetState(), EMobileSheetState::Search);
	TestTrue(TEXT("no route is left"), !Screen->GetCurrentRoute().bFound && Screen->GetCurrentRoute().Steps.IsEmpty());
	TestTrue(TEXT("the ways between levels are all on again"), Screen->GetRoutePreferences().bAllowLift && Screen->GetRoutePreferences().bAllowStairs && Screen->GetRoutePreferences().bAllowEscalator);
	TestTrue(TEXT("the map is on E2"), Screen->GetMapView()->GetBuildingId() == TEXT("E2") && Screen->GetMapView()->GetRoutePointsOnLevel() == 0);
	TestEqual(TEXT("with the campus's places"), Screen->GetSearchPage()->GetShownPlaces()[0].BuildingId, FString(TEXT("E2")));
	TestEqual(TEXT("and the route page's choices are the campus's"), Screen->GetRoutePage()->GetToSelect()->GetOptionCount(), 36);

	// A site that is not there changes nothing.
	Screen->OpenSite(TEXT("nowhere"));
	TestEqual(TEXT("an unknown site is ignored"), Screen->GetSiteId(), FString(TEXT("sit-punggol")));
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
