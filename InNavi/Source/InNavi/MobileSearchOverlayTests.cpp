#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileExplorePageWidget.h"
#include "MobileMapWidget.h"
#include "MobileMockSource.h"
#include "MobilePlacePageWidget.h"
#include "MobileSearchHistory.h"
#include "MobileSearchOverlayWidget.h"
#include "MobileSearchPageWidget.h"
#include "MobileSheetFlow.h"

#include "Components/Button.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

// The full-screen search (the map's and the Explore tab's): what it lists with nothing typed and with something typed, what a tap on a row does, the
// recent searches, and how the phone screen opens it and follows what it picked.

namespace
{
	UMobileSearchOverlayWidget* MakeSearchOverlay(UWorld* World, IMobileNavSource* Source, FMobileSearchHistory* History)
	{
		UMobileSearchOverlayWidget* Overlay = CreateWidget<UMobileSearchOverlayWidget>(World, UMobileSearchOverlayWidget::StaticClass());
		if (Overlay)
		{
			Overlay->TakeWidget();
			Overlay->SetSource(Source, TEXT("sit-punggol"));
			Overlay->SetHistory(History);
		}
		return Overlay;
	}

	UMobileAppWidget* MakeSearchScreen(UWorld* World)
	{
		UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
		if (Screen)
		{
			Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
			Screen->TakeWidget();
			Screen->ApplyViewportSize(375.0, 812.0);
		}
		return Screen;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSearchOverlayPlacesTest, "InNavi.Mobile.SearchOverlay.Places", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSearchOverlayPlacesTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	FMobileSearchHistory History;
	UMobileSearchOverlayWidget* Overlay = MakeSearchOverlay(World, &Source, &History);
	if (!TestNotNull(TEXT("the overlay is created"), Overlay)) return false;

	TestFalse(TEXT("shut until it is opened"), Overlay->IsOpen());
	TestEqual(TEXT("and then it lists nothing"), Overlay->GetShownKeys().Num(), 0);
	for (const TCHAR* Name : { TEXT("SearchOverlayRoot"), TEXT("SearchOverlayField"), TEXT("Txt_OverlaySearch"), TEXT("Btn_OverlayClose"), TEXT("SearchOverlayList") })
	{
		TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Overlay->FindNamedWidget(Name));
	}

	// Nothing typed: the recent searches, and none yet.
	Overlay->Open(UMobileSearchOverlayWidget::EMode::Places);
	TestTrue(TEXT("open"), Overlay->IsOpen() && Overlay->GetMode() == UMobileSearchOverlayWidget::EMode::Places);
	TestEqual(TEXT("recent searches come first"), Overlay->GetHeading(), FString(TEXT("Recent searches")));
	TestEqual(TEXT("none yet"), Overlay->GetShownKeys().Num(), 0);
	TestEqual(TEXT("and it says so"), Overlay->GetEmptyText(), FString(TEXT("No recent searches yet. Search for a building or place.")));
	TestFalse(TEXT("so there is nothing to clear"), Overlay->IsClearHistoryShown());

	// Something typed: the buildings first, then the places, as the source finds them.
	Overlay->SetQuery(TEXT("e2"));
	TestEqual(TEXT("results now"), Overlay->GetHeading(), FString(TEXT("Search results")));
	if (TestTrue(TEXT("there are rows"), Overlay->GetShownKeys().Num() > 1))
	{
		TestEqual(TEXT("the building E2 is first"), Overlay->GetShownKeys()[0], FString(TEXT("building:E2")));
		TestTrue(TEXT("then its places"), Overlay->GetShownKeys().Contains(TEXT("place:E2-01-101")) && Overlay->GetShownKeys()[1].StartsWith(TEXT("place:")));
		TestFalse(TEXT("and no place of another building"), Overlay->GetShownKeys().ContainsByPredicate([](const FString& Key) { return Key.StartsWith(TEXT("place:W2")); }));
	}
	TestTrue(TEXT("a row is never more than the most"), Overlay->GetShownKeys().Num() <= UMobileSearchOverlayWidget::MaxRows);
	TestTrue(TEXT("there is no 'nothing found' line"), Overlay->GetEmptyText().IsEmpty());
	Overlay->SetQuery(TEXT("zzzz nothing"));
	TestEqual(TEXT("no match lists nothing"), Overlay->GetShownKeys().Num(), 0);
	TestEqual(TEXT("and says so"), Overlay->GetEmptyText(), FString(TEXT("No matching buildings or places.")));
	Overlay->SetQuery(TEXT("lounge"));
	TestTrue(TEXT("a word of a name finds the place"), Overlay->GetShownKeys().Contains(TEXT("place:E2-01-101")));
	Overlay->SetQuery(FString());
	TestEqual(TEXT("an emptied field is the recent searches again"), Overlay->GetHeading(), FString(TEXT("Recent searches")));

	// A tap on a place: handed on, kept as a recent search, and the search shuts.
	FString ChosenPlace, ChosenBuilding;
	int32 Closed = 0;
	Overlay->SetOnPlaceChosen([&ChosenPlace](const FMobilePlace& Place) { ChosenPlace = Place.Id; });
	Overlay->SetOnBuildingChosen([&ChosenBuilding](const FString& Id) { ChosenBuilding = Id; });
	Overlay->SetOnClosed([&Closed]() { ++Closed; });
	Overlay->SetQuery(TEXT("lounge"));
	Overlay->ChooseRow(TEXT("place:E2-01-101"));
	TestEqual(TEXT("the place was handed on"), ChosenPlace, FString(TEXT("E2-01-101")));
	TestFalse(TEXT("the search is shut"), Overlay->IsOpen());
	TestEqual(TEXT("and said so once"), Closed, 1);
	TestEqual(TEXT("it is a recent search"), FString::Join(History.Chosen(), TEXT(",")), FString(TEXT("place:E2-01-101")));

	Overlay->Open(UMobileSearchOverlayWidget::EMode::Places);
	TestEqual(TEXT("opened again, with an empty field"), Overlay->GetQuery(), FString());
	TestEqual(TEXT("it lists the recent search"), FString::Join(Overlay->GetShownKeys(), TEXT(",")), FString(TEXT("place:E2-01-101")));
	TestTrue(TEXT("with a way to clear them"), Overlay->IsClearHistoryShown());
	Overlay->ChooseRow(TEXT("building:W2"));
	TestEqual(TEXT("a building was handed on"), ChosenBuilding, FString(TEXT("W2")));
	Overlay->Open(UMobileSearchOverlayWidget::EMode::Places);
	TestEqual(TEXT("the newest first"), FString::Join(Overlay->GetShownKeys(), TEXT(",")), FString(TEXT("building:W2,place:E2-01-101")));

	// A recent search that is not of this site is left out; a place that is gone too.
	History.AddChosen(FMobileSearchHistory::PlaceKey(TEXT("PLM-01-102")));
	History.AddChosen(FMobileSearchHistory::PlaceKey(TEXT("no-such-place")));
	History.AddChosen(FMobileSearchHistory::BuildingKey(TEXT("no-such-building")));
	Overlay->Open(UMobileSearchOverlayWidget::EMode::Places);
	TestEqual(TEXT("only what the site has"), FString::Join(Overlay->GetShownKeys(), TEXT(",")), FString(TEXT("building:W2,place:E2-01-101")));

	Overlay->ClearHistory();
	TestEqual(TEXT("Clear history empties the list"), Overlay->GetShownKeys().Num(), 0);
	TestEqual(TEXT("and the history"), History.Chosen().Num(), 0);

	// The close button.
	Overlay->Open(UMobileSearchOverlayWidget::EMode::Places);
	Overlay->SetQuery(TEXT("abc"));
	Overlay->Close();
	TestTrue(TEXT("shut, and what was typed is forgotten"), !Overlay->IsOpen() && Overlay->GetQuery().IsEmpty());
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSearchOverlaySitesTest, "InNavi.Mobile.SearchOverlay.Sites", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSearchOverlaySitesTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	FMobileSearchHistory History;
	UMobileSearchOverlayWidget* Overlay = MakeSearchOverlay(World, &Source, &History);
	if (!TestNotNull(TEXT("the overlay is created"), Overlay)) return false;

	Overlay->Open(UMobileSearchOverlayWidget::EMode::Sites);
	TestEqual(TEXT("recent searches first"), Overlay->GetHeading(), FString(TEXT("Recent searches")));
	TestEqual(TEXT("none yet"), Overlay->GetEmptyText(), FString(TEXT("No recent searches yet.")));

	Overlay->SetQuery(TEXT("sit"));
	TestEqual(TEXT("one site matches"), FString::Join(Overlay->GetShownKeys(), TEXT(",")), FString(TEXT("site:sit-punggol")));
	Overlay->SetQuery(TEXT("mall"));
	TestEqual(TEXT("the name is searched"), FString::Join(Overlay->GetShownKeys(), TEXT(",")), FString(TEXT("site:paya-lebar")));
	Overlay->SetQuery(TEXT("singapore"));
	TestTrue(TEXT("so is what it says under the name"), Overlay->GetShownKeys().Contains(TEXT("site:paya-lebar")));
	Overlay->SetQuery(TEXT("nothing at all"));
	TestEqual(TEXT("no match says so"), Overlay->GetEmptyText(), FString(TEXT("No sites found. Try another name.")));

	// Enter keeps the words as a recent search.
	Overlay->SetQuery(TEXT("sit"));
	Overlay->Submit();
	TestEqual(TEXT("kept"), FString::Join(History.Queries(), TEXT(",")), FString(TEXT("sit")));
	TestEqual(TEXT("not as a chosen place"), History.Chosen().Num(), 0);

	// A tap on a site: handed on, the words kept, shut.
	FString ChosenSite;
	Overlay->SetOnSiteChosen([&ChosenSite](const FString& Id) { ChosenSite = Id; });
	Overlay->SetQuery(TEXT("mall"));
	Overlay->ChooseRow(TEXT("site:paya-lebar"));
	TestEqual(TEXT("the site was handed on"), ChosenSite, FString(TEXT("paya-lebar")));
	TestFalse(TEXT("shut"), Overlay->IsOpen());
	TestEqual(TEXT("the words were kept, newest first"), FString::Join(History.Queries(), TEXT(",")), FString(TEXT("mall,sit")));

	// A recent search of words: a tap types it again and the search stays open, listing what it finds.
	Overlay->Open(UMobileSearchOverlayWidget::EMode::Sites);
	TestEqual(TEXT("the words are the rows"), FString::Join(Overlay->GetShownKeys(), TEXT(",")), FString(TEXT("query:mall,query:sit")));
	TestTrue(TEXT("with a way to clear them"), Overlay->IsClearHistoryShown());
	Overlay->ChooseRow(TEXT("query:sit"));
	TestTrue(TEXT("still open"), Overlay->IsOpen());
	TestEqual(TEXT("the words are typed"), Overlay->GetQuery(), FString(TEXT("sit")));
	TestEqual(TEXT("and listed"), FString::Join(Overlay->GetShownKeys(), TEXT(",")), FString(TEXT("site:sit-punggol")));
	TestEqual(TEXT("and are at the top of the history"), History.Queries()[0], FString(TEXT("sit")));

	Overlay->Open(UMobileSearchOverlayWidget::EMode::Sites);
	Overlay->ClearHistory();
	TestTrue(TEXT("clearing empties the words"), History.Queries().IsEmpty() && Overlay->GetShownKeys().IsEmpty());

	// Without a history nothing is kept and nothing is listed as recent.
	Overlay->SetHistory(nullptr);
	Overlay->Open(UMobileSearchOverlayWidget::EMode::Sites);
	Overlay->SetQuery(TEXT("sit"));
	Overlay->Submit();
	Overlay->SetQuery(FString());
	TestEqual(TEXT("no history, no rows"), Overlay->GetShownKeys().Num(), 0);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSearchOverlayScreenTest, "InNavi.Mobile.SearchOverlay.Screen", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSearchOverlayScreenTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = MakeSearchScreen(World);
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	UMobileSearchOverlayWidget* Overlay = Screen->GetSearchOverlay();
	if (!TestNotNull(TEXT("it has the full-screen search"), Overlay)) return false;
	TestFalse(TEXT("shut at first"), Overlay->IsOpen());

	// The search field of the map: on the map tab, above the top row, and not while walking.
	UWidget* Dock = Screen->FindNamedWidget(TEXT("Btn_MapSearch"));
	if (!TestNotNull(TEXT("the map has a search field"), Dock)) return false;
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	TestTrue(TEXT("it is there on the map tab"), Dock->GetVisibility() == ESlateVisibility::Visible);
	Screen->SetActiveTab(UMobileAppWidget::ETab::Profile);
	TestTrue(TEXT("and not on the other tabs"), Dock->GetVisibility() == ESlateVisibility::Collapsed);
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	TestTrue(TEXT("the top row is under it"), Screen->GetLayoutSnapshot().TopBarBottom >= 44.0 + UMobileAppWidget::SearchDockTop + UMobileAppWidget::SearchDockHeight + UMobileAppWidget::TopRowHeight - 0.01);

	// A tap on it opens the search of buildings and places.
	Screen->OpenMapSearch();
	TestTrue(TEXT("the map's search opens"), Overlay->IsOpen() && Overlay->GetMode() == UMobileSearchOverlayWidget::EMode::Places);
	Overlay->Close();

	// A place picked: its page, with the place on the map; the search is kept as a recent one.
	Screen->OpenMapSearch();
	Overlay->ChooseRow(TEXT("place:E2-01-102"));
	TestFalse(TEXT("the search is shut"), Overlay->IsOpen());
	TestEqual(TEXT("the place page"), Screen->GetFlow().GetState(), EMobileSheetState::Place);
	TestEqual(TEXT("for that place"), Screen->GetPlacePage()->GetPlace().Id, FString(TEXT("E2-01-102")));
	TestEqual(TEXT("it is a recent search"), Screen->GetSearchHistory().Chosen()[0], FString(TEXT("place:E2-01-102")));

	// From the route: the route is left and the new place is shown.
	Screen->SendSheetEvent(EMobileSheetEvent::DirectionsRequested);
	TestEqual(TEXT("on the route page"), Screen->GetFlow().GetState(), EMobileSheetState::Route);
	Screen->OpenMapSearch();
	Overlay->ChooseRow(TEXT("place:E2-01-103"));
	TestEqual(TEXT("the place page again"), Screen->GetFlow().GetState(), EMobileSheetState::Place);
	TestEqual(TEXT("for the new place"), Screen->GetPlacePage()->GetPlace().Id, FString(TEXT("E2-01-103")));

	// A building picked: the map shows it, and the sheet is back at the list of places.
	Screen->OpenMapSearch();
	Overlay->ChooseRow(TEXT("building:E6"));
	TestEqual(TEXT("the list of places"), Screen->GetFlow().GetState(), EMobileSheetState::Search);
	TestEqual(TEXT("the map shows the building"), Screen->GetMapBuildingId(), FString(TEXT("E6")));
	if (TestFalse(TEXT("the list has places"), Screen->GetSearchPage()->GetShownPlaces().IsEmpty()))
	{
		TestEqual(TEXT("and the list is of its places"), Screen->GetSearchPage()->GetShownPlaces()[0].BuildingId, FString(TEXT("E6")));
		TestEqual(TEXT("the directory follows the map's building"), Screen->GetSearchPage()->GetContextBuilding(), FString(TEXT("E6")));
		TestEqual(TEXT("and no filter is on"), Screen->GetSearchPage()->GetActiveFilterCount(), 0);
	}

	// While walking the search field is not on the map.
	Screen->PreviewState(EMobileSheetState::Navigating);
	TestEqual(TEXT("walking"), Screen->GetFlow().GetState(), EMobileSheetState::Navigating);
	TestTrue(TEXT("no search field"), Dock->GetVisibility() == ESlateVisibility::Collapsed);

	// The Explore tab: its search field opens the search of the sites, and a site picked opens it.
	Screen->SetActiveTab(UMobileAppWidget::ETab::Explore);
	Screen->GetExplorePage()->RequestSearch();
	TestTrue(TEXT("the sites' search opens"), Overlay->IsOpen() && Overlay->GetMode() == UMobileSearchOverlayWidget::EMode::Sites);
	Overlay->SetQuery(TEXT("mall"));
	Overlay->ChooseRow(TEXT("site:paya-lebar"));
	TestEqual(TEXT("the site is open"), Screen->GetSiteId(), FString(TEXT("paya-lebar")));
	TestEqual(TEXT("on the map tab"), Screen->GetActiveTab(), UMobileAppWidget::ETab::Directory);
	Screen->OpenMapSearch();
	Overlay->SetQuery(TEXT("lounge"));
	TestFalse(TEXT("the map's search now looks in the new site"), Overlay->GetShownKeys().ContainsByPredicate([](const FString& Key) { return Key.StartsWith(TEXT("place:E2")); }));
	Overlay->Close();

	// What was searched is kept in a file, and a new screen has it.
	const FString Path = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("ScreenHistory_") + FGuid::NewGuid().ToString() + TEXT(".json"));
	Screen->UseSearchHistoryFile(Path);
	TestEqual(TEXT("a file that is not there empties the history"), Screen->GetSearchHistory().Chosen().Num(), 0);
	Screen->OpenSite(TEXT("sit-punggol"));
	Screen->OpenMapSearch();
	Overlay->ChooseRow(TEXT("building:E2"));
	UMobileAppWidget* Next = MakeSearchScreen(World);
	Next->UseSearchHistoryFile(Path);
	TestEqual(TEXT("the next screen has it"), FString::Join(Next->GetSearchHistory().Chosen(), TEXT(",")), FString(TEXT("building:E2")));
	Next->ResetSavedAndPreferences();
	TestEqual(TEXT("Reset takes the recent searches too"), Next->GetSearchHistory().Chosen().Num(), 0);
	IFileManager::Get().Delete(*Path);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
