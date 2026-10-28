#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileMockSource.h"
#include "MobileMapWidget.h"
#include "MobileSheetFlow.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// The phone screen's layout as the prototype has it (measured at 390 x 844): a header on every tab, a title as plain text with Overview and
// 2D / 3D in the top row, one toolbar at the bottom right of the map, and a sheet that rests at 26, 44 and 88 percent.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileLayoutPrototypeTest, "InNavi.Mobile.LayoutLikePrototype", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileLayoutPrototypeTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (Screen) Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	Screen->ApplyViewportSize(375.0, 812.0);
	auto Click = [Screen](const TCHAR* Name) { Cast<UButton>(Screen->FindNamedWidget(Name))->OnClicked.Broadcast(); };
	auto Title = [Screen]() { return Cast<UTextBlock>(Screen->FindNamedWidget(TEXT("SiteName")))->GetText().ToString(); };

	// The header is there on every tab, 44 tall, with the app's name; the tab bar is 60.
	for (const UMobileAppWidget::ETab Tab : { UMobileAppWidget::ETab::Explore, UMobileAppWidget::ETab::Directory, UMobileAppWidget::ETab::Saved, UMobileAppWidget::ETab::Profile })
	{
		Screen->SetActiveTab(Tab);
		TestEqual(TEXT("the header is shown on every tab"), Cast<UBorder>(Screen->FindNamedWidget(TEXT("Header")))->GetVisibility(), ESlateVisibility::Visible);
	}
	TestEqual(TEXT("with the app's name"), Cast<UTextBlock>(Screen->FindNamedWidget(TEXT("Txt_HeaderBrand")))->GetText().ToString(), FString(TEXT("InNav")));
	const FMobileLayoutSnapshot Layout = Screen->GetLayoutSnapshot();
	TestTrue(TEXT("44 tall at the top, 60 for the tab bar"), FMath::IsNearlyEqual(Layout.HeaderBottom, 44.0) && FMath::IsNearlyEqual(Layout.TabBarHeight, 60.0));

	// The sheet rests at 26, 44 and 88 percent of what is between them.
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	const double Workspace = 812.0 - 44.0 - 60.0;
	for (const TPair<EMobileSheetSnap, double>& Rest : { TPair<EMobileSheetSnap, double>(EMobileSheetSnap::Collapsed, 0.26), TPair<EMobileSheetSnap, double>(EMobileSheetSnap::Half, 0.44), TPair<EMobileSheetSnap, double>(EMobileSheetSnap::Full, 0.88) })
	{
		Screen->SetSnap(Rest.Key);
		Screen->SnapSheetNow();
		TestTrue(*FString::Printf(TEXT("the sheet at %.0f percent"), Rest.Value * 100.0), FMath::IsNearlyEqual(Screen->GetLayoutSnapshot().SheetHeight, Workspace * Rest.Value, 0.01));
	}

	// Open to the top the plan and the toolbar are hidden, and the top row stays; at the other heights they are shown.
	TestEqual(TEXT("the plan is hidden at 88 percent"), Screen->GetMapView()->GetVisibility(), ESlateVisibility::Hidden);
	TestFalse(TEXT("and so is the toolbar"), Screen->GetLayoutSnapshot().bControlsVisible);
	TestEqual(TEXT("the top row stays"), Cast<UBorder>(Screen->FindNamedWidget(TEXT("TopBar")))->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	Screen->SetSnap(EMobileSheetSnap::Half);
	Screen->SnapSheetNow();
	TestEqual(TEXT("the plan is shown at 44 percent"), Screen->GetMapView()->GetVisibility(), ESlateVisibility::Visible);
	TestTrue(TEXT("and the toolbar"), Screen->GetLayoutSnapshot().bControlsVisible);

	// The plan fits between the top row and the toolbar, above the sheet as it is now, and follows the sheet.
	const FBox2D HalfExtent = Screen->GetMapView()->GetPlanExtentInWidget(375.0, Screen->GetLayoutSnapshot().DesignHeight);
	Screen->SetSnap(EMobileSheetSnap::Collapsed);
	Screen->SnapSheetNow();
	const FBox2D LowExtent = Screen->GetMapView()->GetPlanExtentInWidget(375.0, Screen->GetLayoutSnapshot().DesignHeight);
	TestTrue(TEXT("a lower sheet gives the plan more room"), LowExtent.GetSize().Y >= HalfExtent.GetSize().Y - 0.5);
	TestTrue(TEXT("and the plan stays above the toolbar"), LowExtent.Max.Y <= Screen->GetLayoutSnapshot().ControlsBottom - UMobileAppWidget::ControlsHeight + 1.0 || LowExtent.Max.Y <= Screen->GetLayoutSnapshot().SheetTop);

	// The title is plain text: the building and level, or the site's name on the overview; Overview says what a tap does, and is for campuses.
	TestEqual(TEXT("the campus opens on a level: building and level"), Title(), FString(TEXT("E2 · L1")));
	TestEqual(TEXT("Overview is shown for a campus"), Cast<UButton>(Screen->FindNamedWidget(TEXT("Btn_Overview")))->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("saying Overview"), Cast<UTextBlock>(Screen->FindNamedWidget(TEXT("Btn_Overview_Label")))->GetText().ToString(), FString(TEXT("Overview")));
	Click(TEXT("Btn_Overview"));
	TestEqual(TEXT("on the overview the title is the site"), Title(), FString(TEXT("SIT Campus")));
	TestEqual(TEXT("and the button says Current"), Cast<UTextBlock>(Screen->FindNamedWidget(TEXT("Btn_Overview_Label")))->GetText().ToString(), FString(TEXT("Current")));
	Click(TEXT("Btn_Overview"));
	TestEqual(TEXT("back to the level"), Title(), FString(TEXT("E2 · L1")));
	Screen->OpenSite(TEXT("paya-lebar"));
	TestEqual(TEXT("a mall has one building, so no Overview"), Cast<UButton>(Screen->FindNamedWidget(TEXT("Btn_Overview")))->GetVisibility(), ESlateVisibility::Collapsed);
	Screen->ChooseMapLevel(2);
	TestEqual(TEXT("and the title follows the level"), Title(), FString(TEXT("Paya Lebar Mall · L2")));

	// The toolbar: level, locate and more, in one bar.
	for (const TCHAR* Name : { TEXT("Btn_Level"), TEXT("Btn_Locate"), TEXT("Btn_MapMore"), TEXT("MapToolbar") })
	{
		TestNotNull(*FString::Printf(TEXT("%s is there"), Name), Screen->FindNamedWidget(Name));
	}
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
