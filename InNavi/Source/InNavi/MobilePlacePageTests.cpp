#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileMockSource.h"
#include "MobilePlacePageWidget.h"
#include "MobileSearchPageWidget.h"

#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// The place page of the phone app's sheet, alone and on the screen.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobilePlacePageTest, "InNavi.Mobile.PlacePage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobilePlacePageTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobilePlacePageWidget* Page = CreateWidget<UMobilePlacePageWidget>(World, UMobilePlacePageWidget::StaticClass());
	if (!TestNotNull(TEXT("the page is created"), Page)) return false;
	Page->TakeWidget();
	for (const TCHAR* Name : { TEXT("Btn_PlaceBack"), TEXT("Btn_SavePlace"), TEXT("Btn_UseAsStart"), TEXT("Btn_GetDirections"), TEXT("Txt_PlaceTitle"), TEXT("Txt_PlaceSubtitle"), TEXT("Txt_PlacePhoto"), TEXT("PlacePills"), TEXT("PlaceScroll") })
	{
		TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Page->FindNamedWidget(Name));
	}
	auto TextOf = [Page](const TCHAR* Name) { return Cast<UTextBlock>(Page->FindNamedWidget(Name))->GetText().ToString(); };

	// It shows the place it is given.
	FMobilePlace Lounge;
	Source.FindPlace(TEXT("E2-01-101"), Lounge);
	Page->SetPlace(Lounge, false, false);
	TestEqual(TEXT("the name"), TextOf(TEXT("Txt_PlaceTitle")), FString(TEXT("Study lounge")));
	TestEqual(TEXT("the code, building and level"), TextOf(TEXT("Txt_PlaceSubtitle")), FString(TEXT("E2-01-101 · E2 · L1")));
	TestEqual(TEXT("no photo yet"), TextOf(TEXT("Txt_PlacePhoto")), FString(TEXT("No photo")));
	UHorizontalBox* Pills = Cast<UHorizontalBox>(Page->FindNamedWidget(TEXT("PlacePills")));
	TestEqual(TEXT("two pills: the category and Available"), Pills->GetChildrenCount(), 2);
	TestEqual(TEXT("the save button offers to save"), TextOf(TEXT("Btn_SavePlace_Label")), FString(TEXT("Save place")));
	TestEqual(TEXT("and to use as the starting point"), TextOf(TEXT("Btn_UseAsStart_Label")), FString(TEXT("Use as starting point")));

	// What the person has done shows on the two buttons.
	Page->SetPlace(Lounge, true, true);
	TestEqual(TEXT("a saved place says Saved"), TextOf(TEXT("Btn_SavePlace_Label")), FString(TEXT("Saved")));
	TestEqual(TEXT("the starting point says so"), TextOf(TEXT("Btn_UseAsStart_Label")), FString(TEXT("Starting point")));

	// A closed place says so, and another place changes the page.
	FMobilePlace Closed;
	Source.FindPlace(TEXT("PLM-01-102"), Closed);
	Page->SetPlace(Closed, false, false);
	TestEqual(TEXT("the other place's name"), TextOf(TEXT("Txt_PlaceTitle")), Closed.Name);
	TestEqual(TEXT("still two pills: the category and Unavailable"), Pills->GetChildrenCount(), 2);

	// The buttons tell whoever made the page.
	FString Log;
	Page->SetOnBack([&Log]() { Log += TEXT("back;"); });
	Page->SetOnDirections([&Log](const FMobilePlace& P) { Log += TEXT("directions:") + P.Id + TEXT(";"); });
	Page->SetOnSaveToggled([&Log](const FMobilePlace& P) { Log += TEXT("save:") + P.Id + TEXT(";"); });
	Page->SetOnUseAsStart([&Log](const FMobilePlace& P) { Log += TEXT("start:") + P.Id + TEXT(";"); });
	for (const TCHAR* Name : { TEXT("Btn_PlaceBack"), TEXT("Btn_GetDirections"), TEXT("Btn_SavePlace"), TEXT("Btn_UseAsStart") })
	{
		Cast<UButton>(Page->FindNamedWidget(Name))->OnClicked.Broadcast();
	}
	TestEqual(TEXT("each button reports, with the place"), Log, FString(TEXT("back;directions:PLM-01-102;save:PLM-01-102;start:PLM-01-102;")));
	World->DestroyWorld(false);
	return true;
}

// On the screen: picking a place shows it, and saving and the starting point are remembered by the screen.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobilePlaceFlowTest, "InNavi.Mobile.PlaceFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobilePlaceFlowTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	UMobilePlacePageWidget* Page = Screen->GetPlacePage();
	if (!TestNotNull(TEXT("the screen has the place page"), Page)) return false;
	auto Click = [Page](const TCHAR* Name) { Cast<UButton>(Page->FindNamedWidget(Name))->OnClicked.Broadcast(); };

	Screen->GetSearchPage()->ChoosePlace(TEXT("E6-2M-102"));
	TestEqual(TEXT("choosing a place moves the sheet to the place"), Screen->GetFlow().GetState(), EMobileSheetState::Place);
	TestEqual(TEXT("and the page shows it"), Page->GetPlace().Id, FString(TEXT("E6-2M-102")));
	TestEqual(TEXT("the screen knows it"), Screen->GetSelectedPlace().Id, FString(TEXT("E6-2M-102")));

	// Saving toggles; the page follows.
	TestFalse(TEXT("not saved yet"), Screen->IsPlaceSaved(TEXT("E6-2M-102")));
	Click(TEXT("Btn_SavePlace"));
	TestTrue(TEXT("saved"), Screen->IsPlaceSaved(TEXT("E6-2M-102")) && Page->IsSaved());
	Click(TEXT("Btn_SavePlace"));
	TestTrue(TEXT("and unsaved by another tap"), !Screen->IsPlaceSaved(TEXT("E6-2M-102")) && !Page->IsSaved());

	// The starting point toggles the same way.
	Click(TEXT("Btn_UseAsStart"));
	TestEqual(TEXT("the starting point is this place"), Screen->GetStartPlaceId(), FString(TEXT("E6-2M-102")));
	TestTrue(TEXT("and the page shows it"), Page->IsStart());
	Click(TEXT("Btn_UseAsStart"));
	TestTrue(TEXT("another tap goes back to my location"), Screen->GetStartPlaceId().IsEmpty() && !Page->IsStart());
	Click(TEXT("Btn_UseAsStart"));

	// Back, and another place: the starting point stays, the page follows the place.
	Click(TEXT("Btn_PlaceBack"));
	TestEqual(TEXT("back goes to the search"), Screen->GetFlow().GetState(), EMobileSheetState::Search);
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-101"));
	TestEqual(TEXT("another place on the page"), Page->GetPlace().Id, FString(TEXT("E2-01-101")));
	TestFalse(TEXT("which is not the starting point"), Page->IsStart());
	TestEqual(TEXT("while the starting point is remembered"), Screen->GetStartPlaceId(), FString(TEXT("E6-2M-102")));

	// Get directions goes on to the route.
	Click(TEXT("Btn_GetDirections"));
	TestEqual(TEXT("the route"), Screen->GetFlow().GetState(), EMobileSheetState::Route);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
