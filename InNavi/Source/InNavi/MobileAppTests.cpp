#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileArrivedPageWidget.h"
#include "MobileNavigatePageWidget.h"
#include "MobilePlacePageWidget.h"
#include "MobileRoutePageWidget.h"
#include "MobileSearchPageWidget.h"
#include "MobileSelectWidget.h"
#include "MobileSheetFlow.h"
#include "MobileMockSource.h"
#include "MobileTapTarget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/Overlay.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Blueprint/WidgetTree.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// The phone app's screen: the sheet's flow and heights as plain logic, and the screen itself on the sizes of several phones.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSheetFlowTest, "InNavi.Mobile.SheetFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSheetFlowTest::RunTest(const FString& Parameters)
{
	FMobileSheetFlow Flow;
	TestEqual(TEXT("the flow starts at the search"), Flow.GetState(), EMobileSheetState::Search);

	// The whole way through and back.
	TestTrue(TEXT("choosing a place"), Flow.Handle(EMobileSheetEvent::PlaceChosen));
	TestEqual(TEXT("shows the place"), Flow.GetState(), EMobileSheetState::Place);
	TestTrue(TEXT("directions"), Flow.Handle(EMobileSheetEvent::DirectionsRequested));
	TestEqual(TEXT("show the route"), Flow.GetState(), EMobileSheetState::Route);
	TestTrue(TEXT("back goes to the place"), Flow.Handle(EMobileSheetEvent::Back));
	TestEqual(TEXT("and shows it"), Flow.GetState(), EMobileSheetState::Place);
	Flow.Handle(EMobileSheetEvent::DirectionsRequested);
	TestTrue(TEXT("start"), Flow.Handle(EMobileSheetEvent::NavigationStarted));
	TestEqual(TEXT("walking"), Flow.GetState(), EMobileSheetState::Navigating);
	TestTrue(TEXT("exit goes back to the route"), Flow.Handle(EMobileSheetEvent::ExitNavigation));
	TestEqual(TEXT("and shows it"), Flow.GetState(), EMobileSheetState::Route);
	Flow.Handle(EMobileSheetEvent::NavigationStarted);
	TestTrue(TEXT("arriving"), Flow.Handle(EMobileSheetEvent::ArrivedAtDestination));
	TestEqual(TEXT("shows the arrival"), Flow.GetState(), EMobileSheetState::Arrived);
	TestTrue(TEXT("view destination"), Flow.Handle(EMobileSheetEvent::ViewDestination));
	TestEqual(TEXT("goes to the place"), Flow.GetState(), EMobileSheetState::Place);
	Flow.Handle(EMobileSheetEvent::DirectionsRequested);
	Flow.Handle(EMobileSheetEvent::NavigationStarted);
	Flow.Handle(EMobileSheetEvent::ArrivedAtDestination);
	TestTrue(TEXT("find another place"), Flow.Handle(EMobileSheetEvent::FindAnotherPlace));
	TestEqual(TEXT("goes back to the search"), Flow.GetState(), EMobileSheetState::Search);

	// An event that means nothing here changes nothing.
	TestFalse(TEXT("back from the search is nothing"), Flow.Handle(EMobileSheetEvent::Back));
	TestFalse(TEXT("nor is starting without a route"), Flow.Handle(EMobileSheetEvent::NavigationStarted));
	TestFalse(TEXT("nor arriving"), Flow.Handle(EMobileSheetEvent::ArrivedAtDestination));
	TestEqual(TEXT("the state stayed"), Flow.GetState(), EMobileSheetState::Search);
	Flow.Handle(EMobileSheetEvent::PlaceChosen);
	TestFalse(TEXT("choosing a place again is nothing"), Flow.Handle(EMobileSheetEvent::PlaceChosen));
	Flow.Reset();
	TestEqual(TEXT("a reset is back to the search"), Flow.GetState(), EMobileSheetState::Search);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSheetHeightsTest, "InNavi.Mobile.SheetHeights", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSheetHeightsTest::RunTest(const FString& Parameters)
{
	for (double ScreenHeight : { 640.0, 812.0, 915.0 })
	{
		const double Collapsed = MobileSheet::SnapHeight(EMobileSheetSnap::Collapsed, ScreenHeight, 60.0, 44.0);
		const double Half = MobileSheet::SnapHeight(EMobileSheetSnap::Half, ScreenHeight, 60.0, 44.0);
		const double Full = MobileSheet::SnapHeight(EMobileSheetSnap::Full, ScreenHeight, 60.0, 44.0);
		TestTrue(*FString::Printf(TEXT("%.0f: collapsed < half < full"), ScreenHeight), Collapsed < Half && Half < Full);
		const double Workspace = ScreenHeight - 60.0 - 44.0;
		TestTrue(TEXT("26, 44 and 88 percent of the space between the header and the tab bar"), FMath::IsNearlyEqual(Collapsed, Workspace * 0.26, 0.01) && FMath::IsNearlyEqual(Half, Workspace * 0.44, 0.01) && FMath::IsNearlyEqual(Full, Workspace * 0.88, 0.01));
		TestEqual(TEXT("the nearest height to each is itself"), MobileSheet::NearestSnap(Half, ScreenHeight, 60.0, 44.0), EMobileSheetSnap::Half);
		TestEqual(TEXT("a drag just under half is half"), MobileSheet::NearestSnap(Half - 10.0, ScreenHeight, 60.0, 44.0), EMobileSheetSnap::Half);
		TestEqual(TEXT("a drag nearly to the bottom is collapsed"), MobileSheet::NearestSnap(Collapsed * 0.7, ScreenHeight, 60.0, 44.0), EMobileSheetSnap::Collapsed);
		TestEqual(TEXT("a drag nearly to the top is full"), MobileSheet::NearestSnap(Full - 5.0, ScreenHeight, 60.0, 44.0), EMobileSheetSnap::Full);
	}
	TestEqual(TEXT("a tap on collapsed opens all the way, as in the prototype"), MobileSheet::NextSnap(EMobileSheetSnap::Collapsed), EMobileSheetSnap::Full);
	TestEqual(TEXT("so does one on half"), MobileSheet::NextSnap(EMobileSheetSnap::Half), EMobileSheetSnap::Full);
	TestEqual(TEXT("and one on full goes back to half"), MobileSheet::NextSnap(EMobileSheetSnap::Full), EMobileSheetSnap::Half);
	TestEqual(TEXT("walking keeps the sheet low"), MobileSheet::SnapForState(EMobileSheetState::Navigating), EMobileSheetSnap::Collapsed);
	TestEqual(TEXT("searching opens it half way"), MobileSheet::SnapForState(EMobileSheetState::Search), EMobileSheetSnap::Half);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileScreenLayoutTest, "InNavi.Mobile.Screen", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileScreenLayoutTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (Screen) Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();

	// Every control is there.
	for (const TCHAR* Name : { TEXT("Btn_TabExplore"), TEXT("Btn_TabDirectory"), TEXT("Btn_TabSaved"), TEXT("Btn_TabProfile"), TEXT("Btn_Mode2D"), TEXT("Btn_Mode3D"), TEXT("Btn_Level"), TEXT("Btn_Locate"), TEXT("Btn_Overview"),
		TEXT("Sheet"), TEXT("TabBar"), TEXT("SheetPages") })
	{
		TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Screen->FindNamedWidget(Name));
	}
	TestEqual(TEXT("it opens on the Explore tab"), Screen->GetActiveTab(), UMobileAppWidget::ETab::Explore);

	// The screen is 375 design pixels wide whatever the window; on small, ordinary and tall phones nothing falls off it.
	struct FPhone { const TCHAR* Name; double Width, Height; };
	for (const FPhone& Phone : { FPhone{ TEXT("small 360x640"), 360.0, 640.0 }, FPhone{ TEXT("375x812"), 375.0, 812.0 }, FPhone{ TEXT("tall 412x915"), 412.0, 915.0 }, FPhone{ TEXT("desktop window scaled 487x1054"), 487.0, 1054.0 } })
	{
		Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
		Screen->ApplyViewportSize(Phone.Width, Phone.Height);
		for (EMobileSheetSnap Snap : { EMobileSheetSnap::Collapsed, EMobileSheetSnap::Half, EMobileSheetSnap::Full })
		{
			Screen->SetSnap(Snap);
			Screen->SnapSheetNow();
			const FMobileLayoutSnapshot L = Screen->GetLayoutSnapshot();
			const FString Where = FString::Printf(TEXT("%s, sheet %d"), Phone.Name, static_cast<int32>(Snap));
			TestEqual(*(Where + TEXT(": 375 design pixels wide")), L.DesignWidth, 375.0);
			TestNearlyEqual(*(Where + TEXT(": the design height follows the window shape")), static_cast<float>(L.DesignHeight), static_cast<float>(Phone.Height / (Phone.Width / 375.0)), 0.01f);
			TestTrue(*(Where + TEXT(": the sheet sits on the tab bar")), FMath::IsNearlyEqual(L.SheetTop + L.SheetHeight, L.TabBarTop, 0.01));
			TestTrue(*(Where + TEXT(": the sheet stays below the top bar")), L.SheetTop >= L.TopBarBottom - 0.01);
			TestTrue(*(Where + TEXT(": and has room for its handle and a title")), L.SheetHeight >= 76.0);
			TestTrue(*(Where + TEXT(": the tab bar is on the screen")), L.TabBarTop >= 0.0 && L.TabBarTop + L.TabBarHeight <= L.DesignHeight + 0.01);
			// The level and locate buttons ride on the sheet, and go away when it is open so far that they would cover the top bar.
			if (L.bControlsVisible)
			{
				TestTrue(*(Where + TEXT(": the level and locate buttons stay below the top bar")), L.ControlsBottom - UMobileAppWidget::ControlsHeight >= L.TopBarBottom);
			}
			if (Snap == EMobileSheetSnap::Full)
			{
				TestFalse(*(Where + TEXT(": and are hidden while the sheet is fully open")), L.bControlsVisible);
			}
			else
			{
				TestTrue(*(Where + TEXT(": and are shown otherwise")), L.bControlsVisible);
			}
		}
	}

	// A phone with a status bar and a gesture bar keeps clear of both.
	Screen->ApplyViewportSize(375.0, 812.0);
	Screen->SetSafeInsets(44.0, 34.0);
	Screen->SetSnap(EMobileSheetSnap::Full);
	Screen->SnapSheetNow();
	const FMobileLayoutSnapshot Safe = Screen->GetLayoutSnapshot();
	TestTrue(TEXT("the tab bar is taller by the gesture bar"), FMath::IsNearlyEqual(Safe.TabBarHeight, 60.0 + 34.0, 0.01));
	TestTrue(TEXT("the header takes the status bar's place"), FMath::IsNearlyEqual(Safe.HeaderBottom, 44.0 + 44.0, 0.01));
	TestTrue(TEXT("the open sheet stays below the status bar, the header and the top row"), Safe.SheetTop >= Safe.TopBarBottom - 0.01);
	Screen->SetSafeInsets(0.0, 0.0);

	// Tabs show their pages; only the Directory tab shows the map and the sheet.
	Screen->SetActiveTab(UMobileAppWidget::ETab::Saved);
	TestEqual(TEXT("the Saved tab opens"), Screen->GetActiveTab(), UMobileAppWidget::ETab::Saved);
	TestEqual(TEXT("its page is shown"), Cast<UBorder>(Screen->FindNamedWidget(TEXT("SavedOverlay")))->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("the Explore page is hidden"), Cast<UBorder>(Screen->FindNamedWidget(TEXT("ExploreOverlay")))->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("and the sheet is hidden"), Cast<UBorder>(Screen->FindNamedWidget(TEXT("Sheet")))->GetVisibility(), ESlateVisibility::Collapsed);
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	TestEqual(TEXT("the Directory tab shows the sheet"), Cast<UBorder>(Screen->FindNamedWidget(TEXT("Sheet")))->GetVisibility(), ESlateVisibility::Visible);

	// The sheet's page follows the flow, and each button moves it on.
	UWidgetSwitcher* Pages = Cast<UWidgetSwitcher>(Screen->FindNamedWidget(TEXT("SheetPages")));
	auto Click = [&](const TCHAR* Name) { Cast<UButton>(Screen->FindNamedWidget(Name))->OnClicked.Broadcast(); };
	TestEqual(TEXT("the search page first"), Pages->GetActiveWidgetIndex(), 0);
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-102"));
	TestEqual(TEXT("the place page"), Pages->GetActiveWidgetIndex(), 1);
	Cast<UButton>(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")))->OnClicked.Broadcast();
	TestEqual(TEXT("the route page"), Pages->GetActiveWidgetIndex(), 2);
	Cast<UButton>(Screen->GetRoutePage()->FindNamedWidget(TEXT("Btn_StartNavigation")))->OnClicked.Broadcast();
	TestEqual(TEXT("the navigating page"), Pages->GetActiveWidgetIndex(), 3);
	TestEqual(TEXT("with the sheet low"), Screen->GetSnap(), EMobileSheetSnap::Collapsed);
	for (int32 Guard = 0; Guard < 20 && Screen->GetFlow().GetState() == EMobileSheetState::Navigating; ++Guard)
	{
		Cast<UButton>(Screen->GetNavigatePage()->FindNamedWidget(TEXT("Btn_NextInstruction")))->OnClicked.Broadcast();
	}
	TestEqual(TEXT("the arrived page"), Pages->GetActiveWidgetIndex(), 4);
	Cast<UButton>(Screen->GetArrivedPage()->FindNamedWidget(TEXT("Btn_FindAnother")))->OnClicked.Broadcast();
	TestEqual(TEXT("and back to the search"), Pages->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("with the sheet half open"), Screen->GetSnap(), EMobileSheetSnap::Half);

	// Dragging the handle.
	Screen->ApplyViewportSize(375.0, 812.0);
	Screen->SetSnap(EMobileSheetSnap::Half);
	Screen->SnapSheetNow();
	const FMobileLayoutSnapshot Before = Screen->GetLayoutSnapshot();
	TestFalse(TEXT("a press in the middle of the sheet is not a drag"), Screen->BeginSheetDrag(FVector2D(100.0, Before.SheetTop + 200.0)));
	TestTrue(TEXT("a press on the handle starts one"), Screen->BeginSheetDrag(FVector2D(187.0, Before.SheetTop + 10.0)));
	Screen->MoveSheetDrag(FVector2D(187.0, Before.SheetTop - 150.0));
	TestTrue(TEXT("dragging up makes the sheet taller"), Screen->GetSheetHeight() > Before.SheetHeight + 100.0);
	Screen->EndSheetDrag(FVector2D(187.0, Before.SheetTop - 400.0));
	TestEqual(TEXT("letting go near the top rests at full"), Screen->GetSnap(), EMobileSheetSnap::Full);
	Screen->SnapSheetNow();
	const FMobileLayoutSnapshot Open = Screen->GetLayoutSnapshot();
	TestTrue(TEXT("a press on the handle of the open sheet starts a drag"), Screen->BeginSheetDrag(FVector2D(187.0, Open.SheetTop + 10.0)));
	Screen->EndSheetDrag(FVector2D(187.0, Open.SheetTop + 10.0));
	TestEqual(TEXT("and a tap with no movement on the open sheet goes back to half"), Screen->GetSnap(), EMobileSheetSnap::Half);
	Screen->SetActiveTab(UMobileAppWidget::ETab::Profile);
	TestFalse(TEXT("on another tab the handle does nothing"), Screen->BeginSheetDrag(FVector2D(187.0, 700.0)));

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSearchPageTest, "InNavi.Mobile.SearchPage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSearchPageTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobileSearchPageWidget* Page = CreateWidget<UMobileSearchPageWidget>(World, UMobileSearchPageWidget::StaticClass());
	if (!TestNotNull(TEXT("the page is created"), Page)) return false;
	Page->TakeWidget();
	Page->SetSource(&Source, TEXT("sit-punggol"));

	// The directory of the prototype's second iteration: a title and a filters button, and no search box (that is the full-screen search), back arrow or Browse all.
	for (const TCHAR* Name : { TEXT("Txt_SearchTitle"), TEXT("Btn_Filters"), TEXT("ListScroll"), TEXT("FilterPanel"), TEXT("FilterSelects"), TEXT("FiltersBadge") })
	{
		TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Page->FindNamedWidget(Name));
	}
	for (const TCHAR* Gone : { TEXT("Txt_Search"), TEXT("Btn_SearchBack"), TEXT("Btn_BrowseAll"), TEXT("ChipBox") })
	{
		TestNull(*FString::Printf(TEXT("%s is gone"), Gone), Page->FindNamedWidget(Gone));
	}

	// At first: every place of the site, a heading for each building and level, the site's name as the title, and the filters closed.
	TestEqual(TEXT("every place of the campus"), Page->GetShownPlaces().Num(), 36);
	TestEqual(TEXT("under a heading for each of the nine levels"), Page->GetGroupCount(), 9);
	FString SiteName;
	for (const FMobileSite& Site : Source.ListSites()) { if (Site.Id == TEXT("sit-punggol")) SiteName = Site.Name; }
	TestEqual(TEXT("the title is the site's name"), Page->GetTitle(), SiteName);
	TestEqual(TEXT("and it is shown"), Cast<UTextBlock>(Page->FindNamedWidget(TEXT("Txt_SearchTitle")))->GetText().ToString(), SiteName);
	TestEqual(TEXT("no filter is on"), Page->GetActiveFilterCount(), 0);
	TestEqual(TEXT("so there is no count"), Cast<UWidget>(Page->FindNamedWidget(TEXT("FiltersBadge")))->GetVisibility(), ESlateVisibility::Collapsed);
	TestFalse(TEXT("the filters are closed"), Page->AreFiltersOpen());
	TestEqual(TEXT("so their panel is hidden"), Cast<UWidget>(Page->FindNamedWidget(TEXT("FilterPanel")))->GetVisibility(), ESlateVisibility::Collapsed);

	// The button opens the panel.
	Cast<UButton>(Page->FindNamedWidget(TEXT("Btn_Filters")))->OnClicked.Broadcast();
	TestTrue(TEXT("the button opens the filters"), Page->AreFiltersOpen());
	TestEqual(TEXT("and shows the panel"), Cast<UWidget>(Page->FindNamedWidget(TEXT("FilterPanel")))->GetVisibility(), ESlateVisibility::Visible);

	// The three drop-downs: All first, then what the site has.
	UMobileSelectWidget* Category = Page->GetCategorySelect();
	UMobileSelectWidget* Building = Page->GetBuildingSelect();
	UMobileSelectWidget* Level = Page->GetLevelSelect();
	if (!TestTrue(TEXT("the three drop-downs are there"), Category && Building && Level)) return false;
	TestEqual(TEXT("the categories: All and the site's"), Category->GetOptionCount(), 1 + Source.ListCategories(TEXT("sit-punggol")).Num());
	TestEqual(TEXT("All is chosen"), Category->GetSelected(), FString());
	TestEqual(TEXT("the buildings: all and the three"), Building->GetOptionCount(), 4);
	TestEqual(TEXT("the building drop-down is for a campus"), Building->GetVisibility(), ESlateVisibility::Visible);
	TestTrue(TEXT("the levels: all, then each level once"), Level->GetOptionCount() > 1 && Level->GetOptions()[0].Label == TEXT("All levels"));

	// A category.
	Page->SetCategory(TEXT("Facilities"));
	TestEqual(TEXT("a category keeps its places"), Page->GetShownPlaces().Num(), 18);
	TestTrue(TEXT("all of that category"), !Page->GetShownPlaces().ContainsByPredicate([](const FMobilePlace& Place) { return Place.Category != TEXT("Facilities"); }));
	TestEqual(TEXT("one filter is on"), Page->GetActiveFilterCount(), 1);
	TestEqual(TEXT("and the count shows"), Cast<UWidget>(Page->FindNamedWidget(TEXT("FiltersBadge")))->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestEqual(TEXT("the count says 1"), Cast<UTextBlock>(Page->FindNamedWidget(TEXT("Txt_FiltersBadge")))->GetText().ToString(), FString(TEXT("1")));
	TestEqual(TEXT("the drop-down shows it"), Category->GetSelected(), FString(TEXT("Facilities")));
	Page->SetCategory(FString());
	TestEqual(TEXT("All brings every place back"), Page->GetShownPlaces().Num(), 36);

	// A building: its places, and its name as the title.
	Page->SetBuilding(TEXT("E6"));
	TestTrue(TEXT("only that building's places"), !Page->GetShownPlaces().IsEmpty() && !Page->GetShownPlaces().ContainsByPredicate([](const FMobilePlace& Place) { return Place.BuildingId != TEXT("E6"); }));
	FString BuildingName;
	for (const FMobileBuilding& Candidate : Source.ListBuildings(TEXT("sit-punggol"))) { if (Candidate.Id == TEXT("E6")) BuildingName = Candidate.Name.IsEmpty() ? Candidate.Id : Candidate.Name; }
	TestEqual(TEXT("the title is the building's"), Page->GetTitle(), BuildingName);
	TestEqual(TEXT("and shown"), Cast<UTextBlock>(Page->FindNamedWidget(TEXT("Txt_SearchTitle")))->GetText().ToString(), BuildingName);

	// The building the map shows is the directory's too: its name is the title and its places the list, and it is not a filter.
	Page->ClearFilters();
	Page->SetContextBuilding(TEXT("E2"));
	TestTrue(TEXT("only that building's places"), !Page->GetShownPlaces().IsEmpty() && !Page->GetShownPlaces().ContainsByPredicate([](const FMobilePlace& Place) { return Place.BuildingId != TEXT("E2"); }));
	TestEqual(TEXT("no filter is on"), Page->GetActiveFilterCount(), 0);
	TestEqual(TEXT("the building drop-down still says all buildings"), Page->GetBuildingSelect()->GetSelected(), FString());
	TestTrue(TEXT("the title is the building's"), Page->GetTitle() != SiteName && !Page->GetTitle().IsEmpty());
	Page->SetBuilding(TEXT("E6"));
	TestTrue(TEXT("the Building filter wins over the map's"), !Page->GetShownPlaces().ContainsByPredicate([](const FMobilePlace& Place) { return Place.BuildingId != TEXT("E6"); }));
	Page->ClearFilters();
	TestEqual(TEXT("clearing the filters leaves the map's building"), Page->GetContextBuilding(), FString(TEXT("E2")));
	Page->SetContextBuilding(FString());
	TestEqual(TEXT("the whole site again"), Page->GetShownPlaces().Num(), 36);
	Page->SetBuilding(TEXT("E6"));

	// A level, with the building and a category: the three together.
	Page->SetLevel(1);
	TestTrue(TEXT("a level of that building"), !Page->GetShownPlaces().ContainsByPredicate([](const FMobilePlace& Place) { return Place.Level != 1 || Place.BuildingId != TEXT("E6"); }));
	Page->SetCategory(TEXT("Learning"));
	TestEqual(TEXT("three filters are on"), Page->GetActiveFilterCount(), 3);
	TestEqual(TEXT("and the count says 3"), Cast<UTextBlock>(Page->FindNamedWidget(TEXT("Txt_FiltersBadge")))->GetText().ToString(), FString(TEXT("3")));
	Page->ClearFilters();

	// Choosing in a drop-down is the same as asking the page.
	Building->Choose(TEXT("E2"));
	TestEqual(TEXT("the building drop-down filters"), Page->GetFilter().BuildingId, FString(TEXT("E2")));
	Level->Choose(TEXT("1"));
	TestEqual(TEXT("so does the level one"), Page->GetFilter().Level, 1);
	Level->Choose(FString());
	TestEqual(TEXT("All levels clears it"), Page->GetFilter().Level, 0);
	Page->ClearFilters();
	TestTrue(TEXT("clearing the filters clears them"), Page->GetActiveFilterCount() == 0 && Page->GetShownPlaces().Num() == 36 && Page->GetTitle() == SiteName);

	// Only one drop-down is open at a time.
	Category->ToggleOpen();
	Building->ToggleOpen();
	TestTrue(TEXT("the last one opened is open"), Building->IsOpen() && !Category->IsOpen());

	// No match says so.
	Page->SetCategory(TEXT("No such category"));
	TestTrue(TEXT("nothing matches"), Page->GetShownPlaces().IsEmpty());
	TestNotNull(TEXT("and the page says so"), Page->FindNamedWidget(TEXT("Empty")));
	Page->ClearFilters();

	// Picking a place hands it on.
	FString Chosen;
	Page->SetOnPlaceChosen([&Chosen](const FMobilePlace& Place) { Chosen = Place.Id; });
	Page->ChoosePlace(TEXT("E6-2M-103"));
	TestEqual(TEXT("the chosen place arrives"), Chosen, FString(TEXT("E6-2M-103")));

	// The mall is one building: every place of it, and no building drop-down.
	Page->SetSource(&Source, TEXT("paya-lebar"));
	TestEqual(TEXT("all 28 places of the mall"), Page->GetShownPlaces().Num(), 28);
	TestEqual(TEXT("a mall has no building drop-down"), Page->GetBuildingSelect()->GetVisibility(), ESlateVisibility::Collapsed);
	TestFalse(TEXT("a new site closes the filters"), Page->AreFiltersOpen());

	World->DestroyWorld(false);
	return true;
}

// The filter lists of the directory open over the places (an overlay over the page), not in the page where they would push the places down; they
// are not taller than five rows; and a long name of a place is cut with "..." before the building and level at the right.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileDirectoryPopupTest, "InNavi.Mobile.DirectoryPopup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileDirectoryPopupTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobileSearchPageWidget* Page = CreateWidget<UMobileSearchPageWidget>(World, UMobileSearchPageWidget::StaticClass());
	if (!TestNotNull(TEXT("the page is created"), Page)) return false;
	Page->TakeWidget();
	Page->SetSource(&Source, TEXT("sit-punggol"));
	UOverlay* Root = Cast<UOverlay>(Page->FindNamedWidget(TEXT("SearchRoot")));
	if (!TestNotNull(TEXT("the page sits in an overlay"), Root)) return false;
	TestEqual(TEXT("the page is its only layer at first"), Root->GetChildrenCount(), 1);

	Page->SetFiltersOpen(true);
	UMobileSelectWidget* Category = Page->GetCategorySelect();
	UMobileSelectWidget* Building = Page->GetBuildingSelect();
	Category->ToggleOpen();
	TestEqual(TEXT("an open list is a layer over the page"), Root->GetChildrenCount(), 2);
	TestEqual(TEXT("the page under it keeps its place"), Root->GetChildAt(0), Cast<UWidget>(Page->FindNamedWidget(TEXT("SearchPage"))));
	Building->ToggleOpen();
	TestTrue(TEXT("only one is open"), Building->IsOpen() && !Category->IsOpen());
	Page->SetFiltersOpen(false);
	TestFalse(TEXT("closing the filters closes the list that floats over the page"), Building->IsOpen());

	for (UMobileSelectWidget* Select : { Category, Building, Page->GetLevelSelect() })
	{
		TestTrue(TEXT("a list is about five rows at most"), Select->GetMaxListHeight() > 150.f && Select->GetMaxListHeight() < 230.f);
	}

	// A long name is cut at the end with an ellipsis; the building and level tag is as wide as it is, not squeezed.
	const FString FirstName = Page->GetShownPlaces()[0].Name;
	bool bFound = false;
	const UVerticalBox* List = Cast<UVerticalBox>(Page->FindNamedWidget(TEXT("ListBox")));
	for (int32 Index = 0; List && Index < List->GetChildrenCount(); ++Index)
	{
		const UMobileTapTarget* Tap = Cast<UMobileTapTarget>(List->GetChildAt(Index));
		if (!Tap || !Tap->WidgetTree) continue;
		TArray<UWidget*> All;
		Tap->WidgetTree->GetAllWidgets(All);
		for (UWidget* Widget : All)
		{
			const UTextBlock* Text = Cast<UTextBlock>(Widget);
			if (Text && Text->GetText().ToString() == FirstName)
			{
				bFound = true;
				TestEqual(TEXT("a place name ends in an ellipsis when it is too long"), Text->GetTextOverflowPolicy(), ETextOverflowPolicy::Ellipsis);
			}
		}
		if (bFound) break;
	}
	TestTrue(TEXT("the name of a place is in the list"), bFound);

	World->DestroyWorld(false);
	return true;
}

// Minimised (the sheet at its lowest) the sheet keeps its handle and a title, and the button of a place or a route, as the prototype does: the map gets
// the screen. Open again, everything is back.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMinimizedSheetTest, "InNavi.Mobile.MinimizedSheet", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMinimizedSheetTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("76 for the directory"), MobileSheet::MinimizedHeight(EMobileSheetState::Search), 76.0);
	TestEqual(TEXT("and the arrival"), MobileSheet::MinimizedHeight(EMobileSheetState::Arrived), 76.0);
	TestEqual(TEXT("136 for a place, with its button"), MobileSheet::MinimizedHeight(EMobileSheetState::Place), 136.0);
	TestEqual(TEXT("and a route"), MobileSheet::MinimizedHeight(EMobileSheetState::Route), 136.0);
	TestEqual(TEXT("walking keeps its own height"), MobileSheet::MinimizedHeight(EMobileSheetState::Navigating), 0.0);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (Screen) Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	Screen->ApplyViewportSize(375.0, 812.0);
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	auto Rest = [&](EMobileSheetSnap Snap) { Screen->SetSnap(Snap); Screen->SnapSheetNow(); };

	// The directory: a title only.
	Rest(EMobileSheetSnap::Collapsed);
	TestTrue(TEXT("the sheet is minimised"), Screen->IsSheetMinimized());
	TestTrue(TEXT("76 tall"), FMath::IsNearlyEqual(Screen->GetLayoutSnapshot().SheetHeight, 76.0, 0.01));
	TestTrue(TEXT("the directory page says so"), Screen->GetSearchPage()->IsMinimized());
	TestEqual(TEXT("its list is hidden"), Screen->GetSearchPage()->FindNamedWidget(TEXT("ListScroll"))->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("and its filters button"), Screen->GetSearchPage()->FindNamedWidget(TEXT("Btn_Filters"))->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("the title stays"), Screen->GetSearchPage()->FindNamedWidget(TEXT("Txt_SearchTitle"))->GetVisibility(), ESlateVisibility::Visible);
	Rest(EMobileSheetSnap::Half);
	TestFalse(TEXT("opened again it is not"), Screen->IsSheetMinimized());
	TestEqual(TEXT("and the list is back"), Screen->GetSearchPage()->FindNamedWidget(TEXT("ListScroll"))->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("with the filters button"), Screen->GetSearchPage()->FindNamedWidget(TEXT("Btn_Filters"))->GetVisibility(), ESlateVisibility::Visible);

	// A place: the name and its button, not the photo and the rest.
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-102"));
	Rest(EMobileSheetSnap::Collapsed);
	TestTrue(TEXT("136 tall"), FMath::IsNearlyEqual(Screen->GetLayoutSnapshot().SheetHeight, 136.0, 0.01));
	TestEqual(TEXT("the place page's body is hidden"), Screen->GetPlacePage()->FindNamedWidget(TEXT("PlaceScroll"))->GetVisibility(), ESlateVisibility::Collapsed);
	TestNotEqual(TEXT("and Get directions is not"), Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections"))->GetVisibility(), ESlateVisibility::Collapsed);
	Rest(EMobileSheetSnap::Half);
	TestEqual(TEXT("opened again the body is back"), Screen->GetPlacePage()->FindNamedWidget(TEXT("PlaceScroll"))->GetVisibility(), ESlateVisibility::Visible);

	// A route: "Your route" with the travel mode, and Start navigation.
	Cast<UButton>(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")))->OnClicked.Broadcast();
	Rest(EMobileSheetSnap::Collapsed);
	TestEqual(TEXT("the route page's body is hidden"), Screen->GetRoutePage()->FindNamedWidget(TEXT("RouteScroll"))->GetVisibility(), ESlateVisibility::Collapsed);
	TestNotEqual(TEXT("and Start navigation is not"), Screen->GetRoutePage()->FindNamedWidget(TEXT("Btn_StartNavigation"))->GetVisibility(), ESlateVisibility::Collapsed);
	TestNotEqual(TEXT("the travel mode shows under the title"), Screen->GetRoutePage()->FindNamedWidget(TEXT("Txt_RouteSubtitle"))->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("and says it"), Cast<UTextBlock>(Screen->GetRoutePage()->FindNamedWidget(TEXT("Txt_RouteSubtitle")))->GetText().ToString().StartsWith(TEXT("Travel mode")));
	Rest(EMobileSheetSnap::Half);
	TestEqual(TEXT("opened again the travel mode goes"), Screen->GetRoutePage()->FindNamedWidget(TEXT("Txt_RouteSubtitle"))->GetVisibility(), ESlateVisibility::Collapsed);

	// While walking the sheet keeps its own low height and is not minimised.
	Cast<UButton>(Screen->GetRoutePage()->FindNamedWidget(TEXT("Btn_StartNavigation")))->OnClicked.Broadcast();
	Rest(EMobileSheetSnap::Collapsed);
	TestFalse(TEXT("walking is not minimised"), Screen->IsSheetMinimized());
	TestTrue(TEXT("it keeps the height of what it shows"), Screen->GetLayoutSnapshot().SheetHeight >= UMobileAppWidget::NavigationSheetHeight - 0.01);

	World->DestroyWorld(false);
	return true;
}

// The stops of a route: a hairline between the places, no arrow on a row, and a long name cut.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRouteRowsTest, "InNavi.Mobile.RouteRows", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRouteRowsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (Screen) Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-102"));
	Cast<UButton>(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")))->OnClicked.Broadcast();
	UMobileRoutePageWidget* Route = Screen->GetRoutePage();
	Route->AddStop(TEXT("E6-2M-103"));

	const UVerticalBox* Stops = Cast<UVerticalBox>(Route->FindNamedWidget(TEXT("RouteStops")));
	if (!TestNotNull(TEXT("the stops are there"), Stops)) return false;
	int32 Dividers = 0;
	for (int32 Index = 0; Index < Stops->GetChildrenCount(); ++Index)
	{
		const USizeBox* Row = Cast<USizeBox>(Stops->GetChildAt(Index));
		const UOverlay* Layers = Row ? Cast<UOverlay>(Row->GetContent()) : nullptr;
		if (Layers && Layers->GetChildrenCount() > 1) ++Dividers;
	}
	// My location, two destinations and Add stop: a line under each but the last.
	TestEqual(TEXT("a line between the places, not under the last"), Dividers, Stops->GetChildrenCount() - 1);

	for (UMobileSelectWidget* Select : { Route->GetFromSelect(), Route->GetDestinationSelect(0) })
	{
		if (!TestNotNull(TEXT("a select"), Select)) continue;
		Select->TakeWidget();
		TestFalse(TEXT("a row of a card has no arrow"), Select->HasArrow());
		const UTextBlock* Value = Select->GetValueText();
		if (TestNotNull(TEXT("its text"), Value))
		{
			TestEqual(TEXT("is cut with an ellipsis"), Value->GetTextOverflowPolicy(), ETextOverflowPolicy::Ellipsis);
		}
	}
	World->DestroyWorld(false);
	return true;
}

// A tap target hands its payload to whoever made it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileTapTargetTest, "InNavi.Mobile.TapTarget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileTapTargetTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileTapTarget* Target = CreateWidget<UMobileTapTarget>(World, UMobileTapTarget::StaticClass());
	if (!TestNotNull(TEXT("created"), Target)) return false;
	FString Got;
	Target->Setup(nullptr, TEXT("payload"), FLinearColor::Transparent, 0.f);
	Target->SetOnTapped([&Got](const FString& Payload) { Got = Payload; });
	Target->Tap();
	TestEqual(TEXT("a tap gives the payload"), Got, FString(TEXT("payload")));
	TestTrue(TEXT("a drag over a row can scroll its list (the button does not take the finger)"), Target->IsScrollFriendly());
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
