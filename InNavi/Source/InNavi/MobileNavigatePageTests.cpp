#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileArrivedPageWidget.h"
#include "MobileMockSource.h"
#include "MobileNavigatePageWidget.h"
#include "MobilePlacePageWidget.h"
#include "MobileRoutePageWidget.h"
#include "MobileSearchPageWidget.h"
#include "MobileSheetFlow.h"

#include "Components/Button.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// The two last pages of the phone app's sheet: walking (the instruction, the step, the metres left, Next instruction, Exit) and having arrived
// (the place, Find another place, Save this place, View destination), alone and in the whole flow.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileNavigatePageTest, "InNavi.Mobile.NavigatePage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileNavigatePageTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobileNavigatePageWidget* Page = CreateWidget<UMobileNavigatePageWidget>(World, UMobileNavigatePageWidget::StaticClass());
	if (!TestNotNull(TEXT("the page is created"), Page)) return false;
	Page->TakeWidget();
	for (const TCHAR* Name : { TEXT("Btn_Exit"), TEXT("Btn_NextInstruction") })
	{
		TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Page->FindNamedWidget(Name));
	}

	FMobilePlace Lounge;
	Source.FindPlace(TEXT("E2-01-101"), Lounge);
	FMobileRouteRequest Request;
	Request.ToPlaceId = Lounge.Id;
	const FMobileRoute Route = Source.FindRoute(Request);
	const int32 Count = Route.Steps.Num();
	TestEqual(TEXT("three steps to the lounge"), Count, 3);

	// The first instruction.
	Page->SetNavigation(Lounge, Route, 0);
	TestTrue(TEXT("where to"), Page->GetDestinationString().Contains(TEXT("Study lounge")) && Page->GetDestinationString().Contains(TEXT("E2")));
	TestEqual(TEXT("the instruction"), Page->GetInstructionString(), Route.Steps[0].Text);
	TestEqual(TEXT("step 1 of 3"), Page->GetProgressString(), FString(TEXT("Step 1 of 3")));
	TestEqual(TEXT("all the metres are left"), Page->GetRemainingString(), FString::Printf(TEXT("%d m remaining"), FMath::RoundToInt(Route.Metres)));
	TestEqual(TEXT("Next instruction"), Page->GetNextLabelString(), FString(TEXT("Next instruction")));

	// Further on, fewer metres are left.
	Page->SetNavigation(Lounge, Route, 1);
	TestEqual(TEXT("step 2 of 3"), Page->GetProgressString(), FString(TEXT("Step 2 of 3")));
	TestEqual(TEXT("the second instruction"), Page->GetInstructionString(), Route.Steps[1].Text);
	TestEqual(TEXT("less is left"), Page->GetRemainingString(), FString::Printf(TEXT("%d m remaining"), FMath::RoundToInt(Route.Steps[1].Metres + Route.Steps[2].Metres)));

	// The last step is the arrival: nothing is left and the button says so.
	Page->SetNavigation(Lounge, Route, 2);
	TestEqual(TEXT("step 3 of 3"), Page->GetProgressString(), FString(TEXT("Step 3 of 3")));
	TestEqual(TEXT("the arrival"), Page->GetInstructionString(), FString(TEXT("You have arrived: Study lounge")));
	TestEqual(TEXT("0 m remaining"), Page->GetRemainingString(), FString(TEXT("0 m remaining")));
	TestEqual(TEXT("the button says I have arrived"), Page->GetNextLabelString(), FString(TEXT("I have arrived")));
	Page->SetNavigation(Lounge, Route, 9);
	TestEqual(TEXT("a step past the end shows the last"), Page->GetProgressString(), FString(TEXT("Step 3 of 3")));

	// The remaining metres.
	TestEqual(TEXT("the sum from a step on"), UMobileNavigatePageWidget::MetresRemaining(Route, 0), Route.Metres, 0.001);
	TestEqual(TEXT("nothing after the end"), UMobileNavigatePageWidget::MetresRemaining(Route, 5), 0.0, 0.001);

	// No route.
	Page->SetNavigation(Lounge, FMobileRoute(), 0);
	TestEqual(TEXT("nothing to follow"), Page->GetInstructionString(), FString(TEXT("No route to follow.")));

	// The buttons.
	FString Log;
	Page->SetOnExit([&Log]() { Log += TEXT("exit;"); });
	Page->SetOnNext([&Log]() { Log += TEXT("next;"); });
	Cast<UButton>(Page->FindNamedWidget(TEXT("Btn_Exit")))->OnClicked.Broadcast();
	Cast<UButton>(Page->FindNamedWidget(TEXT("Btn_NextInstruction")))->OnClicked.Broadcast();
	TestEqual(TEXT("each reports"), Log, FString(TEXT("exit;next;")));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileArrivedPageTest, "InNavi.Mobile.ArrivedPage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileArrivedPageTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobileArrivedPageWidget* Page = CreateWidget<UMobileArrivedPageWidget>(World, UMobileArrivedPageWidget::StaticClass());
	if (!TestNotNull(TEXT("the page is created"), Page)) return false;
	Page->TakeWidget();
	for (const TCHAR* Name : { TEXT("Txt_ArrivedTitle"), TEXT("Btn_FindAnother"), TEXT("Btn_SaveThisPlace"), TEXT("Btn_ViewDestination") })
	{
		TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Page->FindNamedWidget(Name));
	}
	FMobilePlace Lounge;
	Source.FindPlace(TEXT("E2-01-101"), Lounge);
	Page->SetPlace(Lounge, false);
	TestEqual(TEXT("the place, its code and level"), Page->GetPlaceString(), FString(TEXT("Study lounge · E2-01-101 · Level 1")));
	TestEqual(TEXT("Save this place"), Page->GetSaveLabelString(), FString(TEXT("Save this place")));
	Page->SetPlace(Lounge, true);
	TestEqual(TEXT("a saved place says Saved"), Page->GetSaveLabelString(), FString(TEXT("Saved")));

	FString Log;
	Page->SetOnFindAnother([&Log]() { Log += TEXT("another;"); });
	Page->SetOnSaveToggled([&Log](const FMobilePlace& P) { Log += TEXT("save:") + P.Id + TEXT(";"); });
	Page->SetOnViewDestination([&Log]() { Log += TEXT("view;"); });
	for (const TCHAR* Name : { TEXT("Btn_FindAnother"), TEXT("Btn_SaveThisPlace"), TEXT("Btn_ViewDestination") })
	{
		Cast<UButton>(Page->FindNamedWidget(Name))->OnClicked.Broadcast();
	}
	TestEqual(TEXT("each reports"), Log, FString(TEXT("another;save:E2-01-101;view;")));
	World->DestroyWorld(false);
	return true;
}

// On the screen: walking step by step to the arrival, and what the arrival offers.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileWalkFlowTest, "InNavi.Mobile.WalkFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileWalkFlowTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (Screen) Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	UMobileNavigatePageWidget* Walk = Screen->GetNavigatePage();
	UMobileArrivedPageWidget* Arrived = Screen->GetArrivedPage();
	if (!TestTrue(TEXT("the screen has both pages"), Walk && Arrived)) return false;
	auto Click = [](UWidget* Widget) { Cast<UButton>(Widget)->OnClicked.Broadcast(); };

	// To a place a level up: walk to the lift, ride it, walk, turn, arrive.
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-02-101"));
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")));
	const int32 Count = Screen->GetCurrentRoute().Steps.Num();
	TestTrue(TEXT("a route of several steps"), Screen->GetCurrentRoute().bFound && Count >= 4);
	Click(Screen->GetRoutePage()->FindNamedWidget(TEXT("Btn_StartNavigation")));
	TestEqual(TEXT("walking"), Screen->GetFlow().GetState(), EMobileSheetState::Navigating);
	TestEqual(TEXT("on the first step"), Screen->GetNavStepIndex(), 0);
	TestEqual(TEXT("the page shows it"), Walk->GetProgressString(), FString::Printf(TEXT("Step 1 of %d"), Count));
	TestEqual(TEXT("with the sheet low"), Screen->GetSnap(), EMobileSheetSnap::Collapsed);

	// Next instruction goes through the steps.
	for (int32 Step = 1; Step < Count; ++Step)
	{
		Click(Walk->FindNamedWidget(TEXT("Btn_NextInstruction")));
		TestEqual(TEXT("the next step"), Screen->GetNavStepIndex(), Step);
		TestEqual(TEXT("and the page"), Walk->GetProgressString(), FString::Printf(TEXT("Step %d of %d"), Step + 1, Count));
	}
	TestEqual(TEXT("the last step is the arrival"), Walk->GetNextLabelString(), FString(TEXT("I have arrived")));
	TestEqual(TEXT("still walking"), Screen->GetFlow().GetState(), EMobileSheetState::Navigating);
	Click(Walk->FindNamedWidget(TEXT("Btn_NextInstruction")));
	TestEqual(TEXT("the arrival"), Screen->GetFlow().GetState(), EMobileSheetState::Arrived);
	TestTrue(TEXT("shows the place"), Arrived->GetPlaceString().Contains(TEXT("Study lounge")));

	// Save this place: the screen remembers, the place page agrees, and it can be undone.
	Click(Arrived->FindNamedWidget(TEXT("Btn_SaveThisPlace")));
	TestTrue(TEXT("saved"), Screen->IsPlaceSaved(TEXT("E2-02-101")) && Arrived->IsSaved());
	TestTrue(TEXT("the place page agrees"), Screen->GetPlacePage()->IsSaved());
	Click(Arrived->FindNamedWidget(TEXT("Btn_SaveThisPlace")));
	TestFalse(TEXT("and unsaved by another tap"), Screen->IsPlaceSaved(TEXT("E2-02-101")));

	// View destination goes back to the place; from there directions start a fresh walk at the first step.
	Click(Arrived->FindNamedWidget(TEXT("Btn_ViewDestination")));
	TestEqual(TEXT("the place"), Screen->GetFlow().GetState(), EMobileSheetState::Place);
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")));
	Click(Screen->GetRoutePage()->FindNamedWidget(TEXT("Btn_StartNavigation")));
	TestEqual(TEXT("a new walk starts at the first step"), Screen->GetNavStepIndex(), 0);

	// Exit goes back to the route, which can be started again from the first step.
	Click(Walk->FindNamedWidget(TEXT("Btn_NextInstruction")));
	Click(Walk->FindNamedWidget(TEXT("Btn_Exit")));
	TestEqual(TEXT("exit goes to the route"), Screen->GetFlow().GetState(), EMobileSheetState::Route);
	Click(Screen->GetRoutePage()->FindNamedWidget(TEXT("Btn_StartNavigation")));
	TestEqual(TEXT("starting again begins at the first step"), Screen->GetNavStepIndex(), 0);

	// Walk to the arrival and find another place: back to the search.
	for (int32 Guard = 0; Guard < 20 && Screen->GetFlow().GetState() == EMobileSheetState::Navigating; ++Guard)
	{
		Click(Walk->FindNamedWidget(TEXT("Btn_NextInstruction")));
	}
	Click(Arrived->FindNamedWidget(TEXT("Btn_FindAnother")));
	TestEqual(TEXT("find another place goes to the search"), Screen->GetFlow().GetState(), EMobileSheetState::Search);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
