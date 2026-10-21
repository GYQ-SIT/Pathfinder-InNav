#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileArrivedPageWidget.h"
#include "MobileNavigatePageWidget.h"
#include "MobilePlacePageWidget.h"
#include "MobileRoutePageWidget.h"
#include "MobileSearchPageWidget.h"
#include "MobileSheetFlow.h"
#include "MobileMockSource.h"
#include "MobileTapTarget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
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
			TestTrue(*(Where + TEXT(": and has room for its handle and a button")), L.SheetHeight >= 100.0);
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
	Page->SetSource(&Source, TEXT("sit-punggol"), TEXT("E2"), 1);

	// Every control is there.
	for (const TCHAR* Name : { TEXT("Txt_Search"), TEXT("Btn_Filters"), TEXT("Btn_SearchBack"), TEXT("ListScroll"), TEXT("FilterPanel"), TEXT("Btn_BrowseAll") })
	{
		TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Page->FindNamedWidget(Name));
	}

	// At first: the places of the building and level the person is in, under one heading, and a way to see the rest.
	TestEqual(TEXT("four places around the person"), Page->GetShownPlaces().Num(), 4);
	TestEqual(TEXT("under one heading"), Page->GetGroupCount(), 1);
	TestEqual(TEXT("all on E2's first level"), Page->GetShownPlaces()[0].GroupLabel(), FString(TEXT("E2 \u00B7 L1")));
	TestFalse(TEXT("the filters are closed"), Page->AreFiltersOpen());
	TestEqual(TEXT("so their panel is hidden"), Cast<UWidget>(Page->FindNamedWidget(TEXT("FilterPanel")))->GetVisibility(), ESlateVisibility::Collapsed);

	// Browse all places: every place, a heading for each building and level, and the button goes away.
	Page->BrowseAll();
	TestEqual(TEXT("every place of the campus"), Page->GetShownPlaces().Num(), 36);
	TestEqual(TEXT("under a heading for each of the nine levels"), Page->GetGroupCount(), 9);
	TestNull(TEXT("and the button is gone"), Page->FindNamedWidget(TEXT("Btn_BrowseAll")));

	// A search lists every match, wherever it is.
	Page->SetQuery(TEXT("classroom"));
	TestEqual(TEXT("a classroom on every level"), Page->GetShownPlaces().Num(), 9);
	Page->SetQuery(TEXT("e6 restrooms"));
	TestEqual(TEXT("E6's restrooms"), Page->GetShownPlaces().Num(), 4);
	Page->SetQuery(TEXT("zzqxjw"));
	TestTrue(TEXT("nothing matches"), Page->GetShownPlaces().IsEmpty());
	TestNotNull(TEXT("and the page says so"), Page->FindNamedWidget(TEXT("Empty")));
	Page->SetQuery(FString());

	// Filters.
	Page->SetFiltersOpen(true);
	TestEqual(TEXT("the filters open"), Cast<UWidget>(Page->FindNamedWidget(TEXT("FilterPanel")))->GetVisibility(), ESlateVisibility::Visible);
	Page->ToggleCategory(TEXT("Facilities"));
	TestEqual(TEXT("a category keeps its places"), Page->GetShownPlaces().Num(), 18);
	Page->ToggleCategory(TEXT("Learning"));
	TestEqual(TEXT("two categories keep both"), Page->GetShownPlaces().Num(), 36);
	Page->ToggleCategory(TEXT("Facilities"));
	Page->ToggleCategory(TEXT("Learning"));
	Page->ToggleCategory(TEXT("Learning"));
	Page->SetQuery(TEXT("e2"));
	TestEqual(TEXT("a word and a category together"), Page->GetShownPlaces().Num(), 6);
	Page->ClearFilters();
	TestTrue(TEXT("clearing the filters clears them"), Page->GetFilter().Categories.IsEmpty() && !Page->GetFilter().bAvailableOnly);
	Page->SetQuery(FString());
	Page->ToggleAvailableOnly();
	TestEqual(TEXT("the campus has no closed place"), Page->GetShownPlaces().Num(), 36);
	Page->ClearFilters();

	// Picking a place hands it on; the back arrow too.
	FString Chosen;
	bool bWentBack = false;
	Page->SetOnPlaceChosen([&Chosen](const FMobilePlace& Place) { Chosen = Place.Id; });
	Page->SetOnBack([&bWentBack]() { bWentBack = true; });
	Page->ChoosePlace(TEXT("E6-2M-103"));
	TestEqual(TEXT("the chosen place arrives"), Chosen, FString(TEXT("E6-2M-103")));
	Cast<UButton>(Page->FindNamedWidget(TEXT("Btn_SearchBack")))->OnClicked.Broadcast();
	TestTrue(TEXT("and so does the back arrow"), bWentBack);

	// Typing in the box searches.
	Cast<UEditableTextBox>(Page->FindNamedWidget(TEXT("Txt_Search")))->OnTextChanged.Broadcast(FText::FromString(TEXT("lounge")));
	TestEqual(TEXT("a study lounge on each level"), Page->GetShownPlaces().Num(), 9);

	// The mall, where one restaurant is closed.
	Page->SetSource(&Source, TEXT("paya-lebar"), TEXT("PLM"), 1);
	TestEqual(TEXT("four places on the mall's first level"), Page->GetShownPlaces().Num(), 4);
	Page->ToggleAvailableOnly();
	TestEqual(TEXT("a filter lists every match: all 28 of the mall but the closed one"), Page->GetShownPlaces().Num(), 27);
	Page->ClearFilters();

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
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
