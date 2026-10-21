#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileSheetFlow.h"

#include "Components/Border.h"
#include "Components/Button.h"
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
		const double Collapsed = MobileSheet::SnapHeight(EMobileSheetSnap::Collapsed, ScreenHeight, 56.0, 64.0);
		const double Half = MobileSheet::SnapHeight(EMobileSheetSnap::Half, ScreenHeight, 56.0, 64.0);
		const double Full = MobileSheet::SnapHeight(EMobileSheetSnap::Full, ScreenHeight, 56.0, 64.0);
		TestTrue(*FString::Printf(TEXT("%.0f: collapsed < half < full"), ScreenHeight), Collapsed < Half && Half < Full);
		TestTrue(TEXT("full leaves the top bar clear"), ScreenHeight - 56.0 - Full >= 64.0 - 0.001);
		TestEqual(TEXT("the nearest height to each is itself"), MobileSheet::NearestSnap(Half, ScreenHeight, 56.0, 64.0), EMobileSheetSnap::Half);
		TestEqual(TEXT("a drag just under half is half"), MobileSheet::NearestSnap(Half - 10.0, ScreenHeight, 56.0, 64.0), EMobileSheetSnap::Half);
		TestEqual(TEXT("a drag nearly to the bottom is collapsed"), MobileSheet::NearestSnap(Collapsed * 0.7, ScreenHeight, 56.0, 64.0), EMobileSheetSnap::Collapsed);
		TestEqual(TEXT("a drag nearly to the top is full"), MobileSheet::NearestSnap(Full - 5.0, ScreenHeight, 56.0, 64.0), EMobileSheetSnap::Full);
	}
	TestEqual(TEXT("a tap on collapsed opens to half"), MobileSheet::NextSnap(EMobileSheetSnap::Collapsed), EMobileSheetSnap::Half);
	TestEqual(TEXT("then full"), MobileSheet::NextSnap(EMobileSheetSnap::Half), EMobileSheetSnap::Full);
	TestEqual(TEXT("then collapsed again"), MobileSheet::NextSnap(EMobileSheetSnap::Full), EMobileSheetSnap::Collapsed);
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
		TEXT("Btn_PickPlace"), TEXT("Btn_GetDirections"), TEXT("Btn_StartNavigation"), TEXT("Btn_Arrive"), TEXT("Btn_FindAnother"), TEXT("Btn_Exit"), TEXT("Btn_ViewDestination"), TEXT("Btn_OpenSite"), TEXT("Sheet"), TEXT("TabBar"), TEXT("SheetPages") })
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
	TestTrue(TEXT("the tab bar is taller by the gesture bar"), FMath::IsNearlyEqual(Safe.TabBarHeight, 56.0 + 34.0, 0.01));
	TestTrue(TEXT("the open sheet stays below the status bar and top bar"), Safe.SheetTop >= 44.0 + 12.0 + 40.0 - 0.01);
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
	Click(TEXT("Btn_PickPlace"));
	TestEqual(TEXT("the place page"), Pages->GetActiveWidgetIndex(), 1);
	Click(TEXT("Btn_GetDirections"));
	TestEqual(TEXT("the route page"), Pages->GetActiveWidgetIndex(), 2);
	Click(TEXT("Btn_StartNavigation"));
	TestEqual(TEXT("the navigating page"), Pages->GetActiveWidgetIndex(), 3);
	TestEqual(TEXT("with the sheet low"), Screen->GetSnap(), EMobileSheetSnap::Collapsed);
	Click(TEXT("Btn_Arrive"));
	TestEqual(TEXT("the arrived page"), Pages->GetActiveWidgetIndex(), 4);
	Click(TEXT("Btn_FindAnother"));
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
	TestEqual(TEXT("and a tap with no movement goes to the next height, after full the collapsed one"), Screen->GetSnap(), EMobileSheetSnap::Collapsed);
	Screen->SetActiveTab(UMobileAppWidget::ETab::Profile);
	TestFalse(TEXT("on another tab the handle does nothing"), Screen->BeginSheetDrag(FVector2D(187.0, 700.0)));

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
