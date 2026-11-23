#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileMockSource.h"
#include "MobileMapWidget.h"
#include "MobileSheetFlow.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanelSlot.h"
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
	UButton* MiniMapButton = Cast<UButton>(Screen->FindNamedWidget(TEXT("CampusMiniMapButton")));
	UButton* OverviewButton = Cast<UButton>(Screen->FindNamedWidget(TEXT("Btn_Overview")));
	if (!TestTrue(TEXT("the campus inset and overview controls are built"), MiniMapButton && OverviewButton)) return false;
	auto Title = [Screen]() { return Cast<UTextBlock>(Screen->FindNamedWidget(TEXT("SiteName")))->GetText().ToString(); };

	// The header is there on every tab, 44 tall, with the app's name; the tab bar is 60.
	for (const UMobileAppWidget::ETab Tab : { UMobileAppWidget::ETab::Explore, UMobileAppWidget::ETab::Directory, UMobileAppWidget::ETab::Saved, UMobileAppWidget::ETab::Profile })
	{
		Screen->SetActiveTab(Tab);
		TestEqual(TEXT("the header is shown on every tab"), Cast<UBorder>(Screen->FindNamedWidget(TEXT("Header")))->GetVisibility(), ESlateVisibility::Visible);
	}
	TestEqual(TEXT("with the app's name"), Cast<UTextBlock>(Screen->FindNamedWidget(TEXT("Txt_HeaderBrand")))->GetText().ToString(), FString(TEXT("InNav")));
	TestEqual(TEXT("the campus inset is hidden on Profile"), MiniMapButton->GetVisibility(), ESlateVisibility::Collapsed);
	const FMobileLayoutSnapshot Layout = Screen->GetLayoutSnapshot();
	TestTrue(TEXT("44 tall at the top, 60 for the tab bar"), FMath::IsNearlyEqual(Layout.HeaderBottom, 44.0) && FMath::IsNearlyEqual(Layout.TabBarHeight, 60.0));

	// The sheet rests at 76 (minimised: a handle and a title), 44 and 88 percent of what is between them; open to its top it stops under the top row (the search field and 2D | 3D) if that is lower.
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	const double Workspace = 812.0 - 44.0 - 60.0;
	for (const TPair<EMobileSheetSnap, double>& Rest : { TPair<EMobileSheetSnap, double>(EMobileSheetSnap::Collapsed, 0.26), TPair<EMobileSheetSnap, double>(EMobileSheetSnap::Half, 0.44), TPair<EMobileSheetSnap, double>(EMobileSheetSnap::Full, 0.88) })
	{
		Screen->SetSnap(Rest.Key);
		Screen->SnapSheetNow();
		const double UnderTopRow = 812.0 - 60.0 - (44.0 + UMobileAppWidget::TopRowTop + UMobileAppWidget::TopRowHeight + UMobileAppWidget::TopRowGap);
		const double Expected = Rest.Key == EMobileSheetSnap::Collapsed ? 76.0 : Rest.Key == EMobileSheetSnap::Full ? FMath::Min(Workspace * Rest.Value, UnderTopRow) : Workspace * Rest.Value;
		TestTrue(*FString::Printf(TEXT("the sheet at %.0f percent"), Rest.Value * 100.0), FMath::IsNearlyEqual(Screen->GetLayoutSnapshot().SheetHeight, Expected, 0.01));
		TestTrue(*FString::Printf(TEXT("the sheet at %.0f percent stays under the top row"), Rest.Value * 100.0), Screen->GetLayoutSnapshot().SheetTop >= Screen->GetLayoutSnapshot().TopBarBottom - 0.01);
	}

	// Open to the top the plan and the toolbar are hidden, and the top row stays; at the other heights they are shown.
	TestEqual(TEXT("the plan is hidden at 88 percent"), Screen->GetMapView()->GetVisibility(), ESlateVisibility::Hidden);
	TestEqual(TEXT("the campus inset is also hidden under the full sheet"), MiniMapButton->GetVisibility(), ESlateVisibility::Collapsed);
	TestFalse(TEXT("and so is the toolbar"), Screen->GetLayoutSnapshot().bControlsVisible);
	TestEqual(TEXT("the top row stays"), Cast<UBorder>(Screen->FindNamedWidget(TEXT("TopBar")))->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Zoom out is available when the campus inset is hidden"), OverviewButton->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("saying Zoom out"), Cast<UTextBlock>(Screen->FindNamedWidget(TEXT("Btn_Overview_Label")))->GetText().ToString(), FString(TEXT("Zoom out")));
	Screen->SetSnap(EMobileSheetSnap::Half);
	Screen->SnapSheetNow();
	TestEqual(TEXT("the plan is shown at 44 percent"), Screen->GetMapView()->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("the campus inset is tappable alongside the plan"), MiniMapButton->GetVisibility(), ESlateVisibility::Visible);
	TestTrue(TEXT("and the toolbar"), Screen->GetLayoutSnapshot().bControlsVisible);
	const UButtonSlot* InsetContentSlot = Cast<UButtonSlot>(MiniMapButton->GetContent()->Slot);
	if (TestNotNull(TEXT("the campus frame is inside the button"), InsetContentSlot))
	{
		TestEqual(TEXT("the campus frame fills the button width"), InsetContentSlot->GetHorizontalAlignment(), HAlign_Fill);
		TestEqual(TEXT("the campus frame fills the button height"), InsetContentSlot->GetVerticalAlignment(), VAlign_Fill);
	}
	const UCanvasPanelSlot* InsetSlot = Cast<UCanvasPanelSlot>(MiniMapButton->Slot);
	if (TestNotNull(TEXT("the inset has a canvas slot"), InsetSlot))
	{
		const FMobileLayoutSnapshot BrowsingLayout = Screen->GetLayoutSnapshot();
		const double SearchDockBottom = BrowsingLayout.HeaderBottom + UMobileAppWidget::SearchDockTop + UMobileAppWidget::SearchDockHeight;
		const FVector2D InsetSize = InsetSlot->GetSize();
		TestTrue(TEXT("the campus inset fits within the allowed card size"), InsetSize.X > 0.0 && InsetSize.X <= 172.0 && InsetSize.Y > 0.0 && InsetSize.Y <= 98.0);
		TestTrue(TEXT("the browsing inset sits on the left between search and the sheet"),
			FMath::IsNearlyEqual(InsetSlot->GetPosition().X, 12.0) && InsetSlot->GetPosition().Y >= SearchDockBottom
			&& InsetSlot->GetPosition().Y + InsetSlot->GetSize().Y < BrowsingLayout.SheetTop);
	}

	// The plan fits between the top row and the toolbar, above the sheet as it is now, and follows the sheet.
	const FBox2D HalfExtent = Screen->GetMapView()->GetPlanExtentInWidget(375.0, Screen->GetLayoutSnapshot().DesignHeight);
	Screen->SetSnap(EMobileSheetSnap::Collapsed);
	Screen->SnapSheetNow();
	const FBox2D LowExtent = Screen->GetMapView()->GetPlanExtentInWidget(375.0, Screen->GetLayoutSnapshot().DesignHeight);
	TestTrue(TEXT("a lower sheet gives the plan more room"), LowExtent.GetSize().Y >= HalfExtent.GetSize().Y - 0.5);
	TestTrue(TEXT("and the plan stays above the toolbar"), LowExtent.Max.Y <= Screen->GetLayoutSnapshot().ControlsBottom - UMobileAppWidget::ControlsHeight + 1.0 || LowExtent.Max.Y <= Screen->GetLayoutSnapshot().SheetTop);

	// The inset opens the full campus view and the top-row button returns to the same indoor level.
	TestEqual(TEXT("the campus opens on a level: building and level"), Title(), FString(TEXT("E2 · L1")));
	Screen->ChooseMapLevel(2);
	const FString IndoorTitle = Title();
	TestEqual(TEXT("the inset takes the place of Zoom out"), OverviewButton->GetVisibility(), ESlateVisibility::Collapsed);
	MiniMapButton->OnClicked.Broadcast();
	TestTrue(TEXT("tapping the inset opens the overview"), Screen->GetMapView()->IsOverview());
	TestEqual(TEXT("the inset hides in the full campus view"), MiniMapButton->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("on the overview the title is the site"), Title(), FString(TEXT("SIT Campus")));
	TestEqual(TEXT("Back to building is available"), OverviewButton->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("and the button says Back to building"), Cast<UTextBlock>(Screen->FindNamedWidget(TEXT("Btn_Overview_Label")))->GetText().ToString(), FString(TEXT("Back to building")));
	OverviewButton->OnClicked.Broadcast();
	TestTrue(TEXT("back to the same indoor level"), !Screen->GetMapView()->IsOverview() && Screen->GetMapView()->GetLevel() == 2);
	TestEqual(TEXT("the indoor title is restored"), Title(), IndoorTitle);
	TestEqual(TEXT("the campus inset returns with the plan"), MiniMapButton->GetVisibility(), ESlateVisibility::Visible);
	Screen->OpenSite(TEXT("paya-lebar"));
	TestEqual(TEXT("the inset is also available at a one-building site"), MiniMapButton->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("there is no additional Zoom out"), OverviewButton->GetVisibility(), ESlateVisibility::Collapsed);
	Screen->ChooseMapLevel(2);
	MiniMapButton->OnClicked.Broadcast();
	TestTrue(TEXT("a single building can be viewed from the campus map"), Screen->GetMapView()->IsOverview());
	TestEqual(TEXT("a single building also has a back button in overview"), OverviewButton->GetVisibility(), ESlateVisibility::Visible);
	OverviewButton->OnClicked.Broadcast();
	TestTrue(TEXT("the mall returns to its selected indoor level"), !Screen->GetMapView()->IsOverview() && Screen->GetMapView()->GetLevel() == 2);
	TestEqual(TEXT("and the title follows the level"), Title(), FString(TEXT("Paya Lebar Mall · L2")));

	// The toolbar: level, locate and more, in one bar.
	for (const TCHAR* Name : { TEXT("Btn_Level"), TEXT("Btn_Locate"), TEXT("MapCaption"), TEXT("MapToolbar") })
	{
		TestNotNull(*FString::Printf(TEXT("%s is there"), Name), Screen->FindNamedWidget(Name));
	}
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
