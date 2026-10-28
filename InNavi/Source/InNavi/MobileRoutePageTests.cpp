#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileMockSource.h"
#include "MobilePlacePageWidget.h"
#include "MobileRoutePageWidget.h"
#include "MobileSearchPageWidget.h"
#include "MobileSelectWidget.h"
#include "MobileSheetFlow.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// The drop-down, the route page of the phone app's sheet (alone and on the screen): From and To, My location's building and level, the numbers,
// the ways between levels and the steps.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSelectTest, "InNavi.Mobile.Select", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSelectTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileSelectWidget* Select = CreateWidget<UMobileSelectWidget>(World, UMobileSelectWidget::StaticClass());
	if (!TestNotNull(TEXT("created"), Select)) return false;
	Select->TakeWidget();
	Select->SetCaption(TEXT("Level"));
	Select->SetOptions({ { TEXT("1"), TEXT("L1") }, { TEXT("2"), TEXT("L2") }, { TEXT("3"), TEXT("L2M") } }, TEXT("2"));
	TestEqual(TEXT("three options"), Select->GetOptionCount(), 3);
	TestEqual(TEXT("the chosen one shows its label"), Select->GetSelectedLabel(), FString(TEXT("L2")));
	TestFalse(TEXT("closed at first"), Select->IsOpen());

	// A tap on the field opens it; a choice reports, closes it and changes what it shows.
	TArray<FString> Log;
	Select->SetOnChanged([&Log](const FString& Value) { Log.Add(Value); });
	bool bOpened = false;
	Select->SetOnOpened([&bOpened]() { bOpened = true; });
	Select->ToggleOpen();
	TestTrue(TEXT("a tap opens it"), Select->IsOpen() && bOpened);
	Select->Choose(TEXT("3"));
	TestFalse(TEXT("a choice closes it"), Select->IsOpen());
	TestEqual(TEXT("and shows the choice"), Select->GetSelectedLabel(), FString(TEXT("L2M")));
	TestEqual(TEXT("and reports it"), FString::Join(Log, TEXT(",")), FString(TEXT("3")));
	Select->ToggleOpen();
	Select->ToggleOpen();
	TestFalse(TEXT("a second tap closes it"), Select->IsOpen());
	Select->SetSelected(TEXT("1"));
	TestEqual(TEXT("the program can set it without a report"), Select->GetSelectedLabel(), FString(TEXT("L1")));
	TestEqual(TEXT("(no report)"), Log.Num(), 1);
	Select->SetSelected(TEXT("nonsense"));
	TestTrue(TEXT("a value that is not an option shows as nothing"), Select->GetSelectedLabel().IsEmpty());
	Select->SetOptions({ { FString(), TEXT("My location") } }, FString());
	TestEqual(TEXT("an empty value is an option too"), Select->GetSelectedLabel(), FString(TEXT("My location")));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRoutePageTest, "InNavi.Mobile.RoutePage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRoutePageTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobileRoutePageWidget* Page = CreateWidget<UMobileRoutePageWidget>(World, UMobileRoutePageWidget::StaticClass());
	if (!TestNotNull(TEXT("the page is created"), Page)) return false;
	Page->TakeWidget();
	for (const TCHAR* Name : { TEXT("Btn_RouteBack"), TEXT("Btn_StartNavigation"), TEXT("RouteOptions"), TEXT("RouteSteps"), TEXT("RouteScroll"), TEXT("MyLocationRow") })
	{
		TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Page->FindNamedWidget(Name));
	}
	Page->SetChoices(Source.SearchPlaces(TEXT("sit-punggol"), FString(), FMobilePlaceFilter()), Source.ListBuildings(TEXT("sit-punggol")));

	// A route from My location in E2 on level 1 to a study lounge.
	FMobileRouteRequest Request;
	Request.ToPlaceId = TEXT("E2-01-101");
	Request.MyLocationBuildingId = TEXT("E2");
	Request.MyLocationLevel = 1;
	const FMobileRoute Route = Source.FindRoute(Request);
	Page->SetRequest(Request, Route);
	TestEqual(TEXT("From offers My location and every place"), Page->GetFromSelect()->GetOptionCount(), 1 + 36);
	TestEqual(TEXT("From says My location"), Page->GetFromSelect()->GetSelectedLabel(), FString(TEXT("My location")));
	TestEqual(TEXT("To offers every place"), Page->GetToSelect()->GetOptionCount(), 36);
	TestTrue(TEXT("To says the place, with its building and level"), Page->GetToSelect()->GetSelectedLabel().StartsWith(TEXT("Study lounge")) && Page->GetToSelect()->GetSelectedLabel().Contains(TEXT("L1")));
	TestTrue(TEXT("the building and level are asked for My location"), Page->IsMyLocationRowShown());
	TestEqual(TEXT("three buildings"), Page->GetBuildingSelect()->GetOptionCount(), 3);
	TestEqual(TEXT("E2 is chosen"), Page->GetBuildingSelect()->GetSelectedLabel(), FString(TEXT("E2")));
	TestEqual(TEXT("with its three levels"), Page->GetLevelSelect()->GetOptionCount(), 3);
	TestEqual(TEXT("L1 is chosen"), Page->GetLevelSelect()->GetSelectedLabel(), FString(TEXT("L1")));
	TestEqual(TEXT("the minutes"), Page->GetMinutesString(), FString::Printf(TEXT("%d min"), Route.Minutes));
	TestEqual(TEXT("the metres"), Page->GetMetresString(), FString::Printf(TEXT("%d m"), FMath::RoundToInt(Route.Metres)));
	TestEqual(TEXT("a line for each step"), Page->GetShownStepCount(), Route.Steps.Num());
	TestTrue(TEXT("Start navigation can be pressed"), Cast<UButton>(Page->FindNamedWidget(TEXT("Btn_StartNavigation")))->GetIsEnabled());
	TestEqual(TEXT("the error is hidden"), Cast<UWidget>(Page->FindNamedWidget(TEXT("Txt_RouteError")))->GetVisibility(), ESlateVisibility::Collapsed);

	// What the person chooses is reported as a new request.
	TArray<FMobileRouteRequest> Changes;
	Page->SetOnRequestChanged([&Changes](const FMobileRouteRequest& R) { Changes.Add(R); });
	Page->ChooseMyBuilding(TEXT("E6"));
	TestEqual(TEXT("another building is reported"), Changes.Last().MyLocationBuildingId, FString(TEXT("E6")));
	TestEqual(TEXT("E6 has four levels to choose from"), Page->GetLevelSelect()->GetOptionCount(), 4);
	TestEqual(TEXT("the level stays, as E6 has it"), Changes.Last().MyLocationLevel, 1);
	Page->ChooseMyLevel(3);
	TestEqual(TEXT("level 3 of E6 is the mezzanine"), Page->GetLevelSelect()->GetSelectedLabel(), FString(TEXT("L2M")));
	Page->ChooseMyBuilding(TEXT("W2"));
	TestEqual(TEXT("W2 has no level 3, so the level goes to its first"), Changes.Last().MyLocationLevel, 1);
	Page->ChooseFrom(TEXT("E2-01-103"));
	TestEqual(TEXT("a place as the start is reported"), Changes.Last().FromPlaceId, FString(TEXT("E2-01-103")));
	TestFalse(TEXT("and the building and level are not asked for"), Page->IsMyLocationRowShown());
	Page->ChooseFrom(FString());
	TestTrue(TEXT("My location asks for them again"), Page->IsMyLocationRowShown());
	Page->ChooseTo(TEXT("E2-01-102"));
	TestEqual(TEXT("a destination is reported"), Changes.Last().ToPlaceId, FString(TEXT("E2-01-102")));

	// The ways between levels.
	Changes.Reset();
	Page->TapOption(TEXT("lift"));
	TestFalse(TEXT("a tap on Lift turns it off"), Page->GetPreferences().bAllowLift);
	Page->TapOption(TEXT("lift"));
	Page->TapOption(TEXT("stairs"));
	Page->TapOption(TEXT("escalator"));
	TestTrue(TEXT("two off leaves the lift alone"), Page->GetPreferences().bAllowLift && !Page->GetPreferences().bAllowStairs && !Page->GetPreferences().bAllowEscalator);
	Page->TapOption(TEXT("stairs"));
	Page->TapOption(TEXT("liftonly"));
	TestTrue(TEXT("Use lift only is the lift and nothing else"), Page->GetPreferences().bAllowLift && !Page->GetPreferences().bAllowStairs && !Page->GetPreferences().bAllowEscalator);
	TestEqual(TEXT("every tap was reported"), Changes.Num(), 6);
	Page->TapOption(TEXT("nonsense"));
	TestEqual(TEXT("an unknown option changes nothing"), Changes.Num(), 6);

	// Only one drop-down is open at a time.
	Page->GetFromSelect()->ToggleOpen();
	TestTrue(TEXT("From is open"), Page->GetFromSelect()->IsOpen());
	Page->GetToSelect()->ToggleOpen();
	TestTrue(TEXT("opening To"), Page->GetToSelect()->IsOpen());
	TestFalse(TEXT("closes From"), Page->GetFromSelect()->IsOpen());

	// No route: the reason, no steps, and Start navigation off.
	FMobileRoute None;
	None.Error = TEXT("There is no way between levels with the options you chose.");
	Page->SetRequest(Request, None);
	TestEqual(TEXT("the reason is shown"), Page->GetErrorString(), None.Error);
	TestNotEqual(TEXT("in place of the numbers"), Cast<UWidget>(Page->FindNamedWidget(TEXT("Txt_RouteError")))->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("no steps"), Page->GetShownStepCount(), 0);
	TestFalse(TEXT("and Start navigation is off"), Cast<UButton>(Page->FindNamedWidget(TEXT("Btn_StartNavigation")))->GetIsEnabled());

	// The buttons.
	FString Log;
	Page->SetOnBack([&Log]() { Log += TEXT("back;"); });
	Page->SetOnStart([&Log]() { Log += TEXT("start;"); });
	Cast<UButton>(Page->FindNamedWidget(TEXT("Btn_RouteBack")))->OnClicked.Broadcast();
	Page->SetRequest(Request, Route);
	Cast<UButton>(Page->FindNamedWidget(TEXT("Btn_StartNavigation")))->OnClicked.Broadcast();
	TestEqual(TEXT("each reports"), Log, FString(TEXT("back;start;")));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRouteFlowTest, "InNavi.Mobile.RouteFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileRouteFlowTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (Screen) Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	UMobileRoutePageWidget* Route = Screen->GetRoutePage();
	if (!TestNotNull(TEXT("the screen has the route page"), Route)) return false;
	auto HasStep = [Screen](EMobileStepKind Kind) { return Screen->GetCurrentRoute().Steps.ContainsByPredicate([Kind](const FMobileStep& S) { return S.Kind == Kind; }); };

	// Get directions finds the route from My location, which starts in the first building on its first level.
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-02-101"));
	Cast<UButton>(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")))->OnClicked.Broadcast();
	TestEqual(TEXT("the route page"), Screen->GetFlow().GetState(), EMobileSheetState::Route);
	TestTrue(TEXT("with a route"), Screen->GetCurrentRoute().bFound);
	TestEqual(TEXT("from My location"), Route->GetFromSelect()->GetSelectedLabel(), FString(TEXT("My location")));
	TestEqual(TEXT("in E2"), Route->GetBuildingSelect()->GetSelectedLabel(), FString(TEXT("E2")));
	TestEqual(TEXT("on L1"), Route->GetLevelSelect()->GetSelectedLabel(), FString(TEXT("L1")));
	TestTrue(TEXT("to the place"), Route->GetToSelect()->GetSelectedLabel().StartsWith(TEXT("Study lounge")));
	TestTrue(TEXT("by the lift, as the place is a level up"), HasStep(EMobileStepKind::TakeLift));

	// Where My location is changes the route.
	Route->ChooseMyLevel(2);
	TestEqual(TEXT("the screen keeps the level"), Screen->GetMyLocationLevel(), 2);
	TestFalse(TEXT("on the place's own level there is no lift in the route"), HasStep(EMobileStepKind::TakeLift));
	Route->ChooseMyBuilding(TEXT("E6"));
	TestEqual(TEXT("the screen keeps the building"), Screen->GetMyLocationBuildingId(), FString(TEXT("E6")));
	TestTrue(TEXT("from another building the route leaves it"), HasStep(EMobileStepKind::ExitBuilding) && HasStep(EMobileStepKind::EnterBuilding));
	Route->ChooseMyBuilding(TEXT("E2"));

	// A place as the start.
	Route->ChooseFrom(TEXT("E2-01-103"));
	TestEqual(TEXT("the start is that place"), Screen->GetStartPlaceId(), FString(TEXT("E2-01-103")));
	TestFalse(TEXT("so the building and level are not asked for"), Route->IsMyLocationRowShown());
	TestTrue(TEXT("and the route is from there"), Screen->GetCurrentRoute().bFound);
	Route->ChooseFrom(FString());
	TestTrue(TEXT("My location clears the start"), Screen->GetStartPlaceId().IsEmpty() && Route->IsMyLocationRowShown());

	// Another destination: the place page follows.
	Route->ChooseTo(TEXT("E2-01-102"));
	TestEqual(TEXT("the destination changes"), Screen->GetSelectedPlace().Id, FString(TEXT("E2-01-102")));
	TestEqual(TEXT("and the place page"), Screen->GetPlacePage()->GetPlace().Id, FString(TEXT("E2-01-102")));
	TestTrue(TEXT("the To field follows"), Route->GetToSelect()->GetSelectedLabel().StartsWith(TEXT("Classroom")));

	// The start as the destination is not a route.
	Route->ChooseFrom(TEXT("E2-01-102"));
	TestFalse(TEXT("a place to itself has no route"), Screen->GetCurrentRoute().bFound);
	TestFalse(TEXT("so Start navigation is off"), Cast<UButton>(Route->FindNamedWidget(TEXT("Btn_StartNavigation")))->GetIsEnabled());
	Route->ChooseFrom(FString());

	// The ways between levels change the route.
	Route->ChooseTo(TEXT("E2-03-102"));
	Route->TapOption(TEXT("lift"));
	TestTrue(TEXT("without the lift, by the stairs"), HasStep(EMobileStepKind::TakeStairs));
	Route->TapOption(TEXT("stairs"));
	TestFalse(TEXT("with neither there is no route"), Screen->GetCurrentRoute().bFound);
	TestFalse(TEXT("and no Start navigation"), Cast<UButton>(Route->FindNamedWidget(TEXT("Btn_StartNavigation")))->GetIsEnabled());
	Route->TapOption(TEXT("liftonly"));
	TestTrue(TEXT("Use lift only brings the route back"), Screen->GetCurrentRoute().bFound);
	TestFalse(TEXT("the choice is kept by the screen"), Screen->GetRoutePreferences().bAllowStairs);

	// Start navigation.
	Cast<UButton>(Route->FindNamedWidget(TEXT("Btn_StartNavigation")))->OnClicked.Broadcast();
	TestEqual(TEXT("walking"), Screen->GetFlow().GetState(), EMobileSheetState::Navigating);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
