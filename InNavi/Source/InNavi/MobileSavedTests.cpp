#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileArrivedPageWidget.h"
#include "MobileMapWidget.h"
#include "MobileMockSource.h"
#include "MobileNavigatePageWidget.h"
#include "MobilePlacePageWidget.h"
#include "MobileRoutePageWidget.h"
#include "MobileSavedPageWidget.h"
#include "MobileSavedPlaces.h"
#include "MobileSearchPageWidget.h"
#include "MobileSheetFlow.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

// The Saved tab: the list the person keeps (newest first, kept in a file), the page that shows it, and what a tap on a place there does.

namespace
{
	FString TempSavedFile(const TCHAR* Prefix)
	{
		return FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (FString(Prefix) + FGuid::NewGuid().ToString() + TEXT(".json"));
	}
}

#if PLATFORM_WINDOWS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileStorageFailureTest, "InNavi.Mobile.StorageFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileStorageFailureTest::RunTest(const FString& Parameters)
{
	const FString SavedPath = TempSavedFile(TEXT("locked-saved-"));
	const FString PrefsPath = TempSavedFile(TEXT("locked-prefs-"));
	FMobileSavedPlaces Saved;
	FMobilePreferences Preferences;
	Saved.UseFile(SavedPath);
	Preferences.UseFile(PrefsPath);
	TestTrue(TEXT("the first bookmark is written"), Saved.Add(TEXT("original")));
	TestTrue(TEXT("the first preference is written"), Preferences.SetTheme(EMobileTheme::Night));
	auto& Platform = FPlatformFileManager::Get().GetPlatformFile();
	{
		TUniquePtr<IFileHandle> SavedLock(Platform.OpenRead(*SavedPath));
		TUniquePtr<IFileHandle> PrefsLock(Platform.OpenRead(*PrefsPath));
		if (TestTrue(TEXT("both files are locked"), SavedLock && PrefsLock))
		{
			bool bPersisted = true;
			TestFalse(TEXT("failed toggle leaves the new bookmark unsaved"), Saved.Toggle(TEXT("new"), &bPersisted));
			TestFalse(TEXT("the UI can detect the failure"), bPersisted);
			TestTrue(TEXT("the original remains saved"), Saved.Contains(TEXT("original")));
			TestFalse(TEXT("preference reset fails cleanly"), Preferences.Reset());
			TestTrue(TEXT("the current preference is retained"), Preferences.GetTheme() == EMobileTheme::Night);
		}
	}
	TestTrue(TEXT("the bookmark file stays readable"), Saved.Load());
	TestTrue(TEXT("only the original bookmark was stored"), Saved.Num() == 1 && Saved.Contains(TEXT("original")));
	TestTrue(TEXT("the preferences file stays readable"), Preferences.Load());
	TestTrue(TEXT("the original preference was stored"), Preferences.GetTheme() == EMobileTheme::Night);
	IFileManager::Get().Delete(*SavedPath);
	IFileManager::Get().Delete(*PrefsPath);
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSavedPlacesTest, "InNavi.Mobile.SavedPlaces", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSavedPlacesTest::RunTest(const FString& Parameters)
{
	FMobileSavedPlaces Saved;
	TestEqual(TEXT("nothing at first"), Saved.Num(), 0);
	TestTrue(TEXT("toggling saves"), Saved.Toggle(TEXT("a")));
	Saved.Add(TEXT("b"));
	Saved.Add(TEXT("c"));
	TestEqual(TEXT("newest first"), FString::Join(Saved.Ids(), TEXT(",")), FString(TEXT("c,b,a")));
	Saved.Add(TEXT("a"));
	TestEqual(TEXT("saving one again brings it to the top, once"), FString::Join(Saved.Ids(), TEXT(",")), FString(TEXT("a,c,b")));
	TestFalse(TEXT("toggling a saved place takes it back"), Saved.Toggle(TEXT("c")));
	TestTrue(TEXT("and it is gone"), !Saved.Contains(TEXT("c")) && Saved.Num() == 2);
	TestFalse(TEXT("removing what is not there is nothing"), Saved.Remove(TEXT("zzz")));
	Saved.Add(FString());
	TestEqual(TEXT("an empty id is not a place"), Saved.Num(), 2);
	TestFalse(TEXT("without a file nothing is written"), Saved.Save());

	// With a file every change is kept, and the next run reads it back.
	const FString Path = TempSavedFile(TEXT("Saved_"));
	{
		FMobileSavedPlaces Kept;
		Kept.UseFile(Path);
		TestEqual(TEXT("a file that is not there is an empty list"), Kept.Num(), 0);
		Kept.Add(TEXT("x"));
		Kept.Add(TEXT("y"));
		Kept.Remove(TEXT("x"));
		Kept.Add(TEXT("z"));
	}
	FMobileSavedPlaces Reopened;
	Reopened.UseFile(Path);
	TestEqual(TEXT("it comes back as it was, newest first"), FString::Join(Reopened.Ids(), TEXT(",")), FString(TEXT("z,y")));

	// A damaged file is an empty list, not a crash, and is written over by the next change.
	FFileHelper::SaveStringToFile(TEXT("{ this is not json"), *Path);
	FMobileSavedPlaces Damaged;
	Damaged.UseFile(Path);
	TestEqual(TEXT("a damaged file is an empty list"), Damaged.Num(), 0);
	Damaged.Add(TEXT("k"));
	FMobileSavedPlaces Again;
	Again.UseFile(Path);
	TestEqual(TEXT("and the next change repairs it"), FString::Join(Again.Ids(), TEXT(",")), FString(TEXT("k")));
	IFileManager::Get().Delete(*Path);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSavedPageTest, "InNavi.Mobile.SavedPage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSavedPageTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobileSavedPageWidget* Page = CreateWidget<UMobileSavedPageWidget>(World, UMobileSavedPageWidget::StaticClass());
	if (!TestNotNull(TEXT("the page is created"), Page)) return false;
	Page->TakeWidget();
	Page->SetSource(&Source);
	TestEqual(TEXT("the title"), Cast<UTextBlock>(Page->FindNamedWidget(TEXT("Txt_SavedTitle")))->GetText().ToString(), FString(TEXT("Saved")));

	// Nothing saved: how to save one.
	Page->SetSaved({});
	TestTrue(TEXT("nothing saved says how to save"), Page->IsEmptyMessageShown() && Page->GetShownPlaces().IsEmpty());
	TestTrue(TEXT("no count"), Page->GetCountString().IsEmpty());

	// Some: in the order given, grouped by site in the order they come up.
	Page->SetSaved({ TEXT("E2-01-101"), TEXT("PLM-01-102"), TEXT("E2-02-101") });
	TestFalse(TEXT("the message goes"), Page->IsEmptyMessageShown());
	TestEqual(TEXT("three places"), Page->GetShownPlaces().Num(), 3);
	TestEqual(TEXT("counted"), Page->GetCountString(), FString(TEXT("3 places")));
	TestEqual(TEXT("under two sites"), Page->GetGroupCount(), 2);
	TestEqual(TEXT("the newest first"), Page->GetShownPlaces()[0].Id, FString(TEXT("E2-01-101")));
	Page->SetSaved({ TEXT("E2-01-101") });
	TestEqual(TEXT("one place is not plural"), Page->GetCountString(), FString(TEXT("1 place")));
	Page->SetSaved({ TEXT("E2-01-101"), TEXT("no-such-place") });
	TestEqual(TEXT("an id the source does not know is left out"), Page->GetShownPlaces().Num(), 1);

	// A tap and a Remove report the place.
	FString Log;
	Page->SetOnPlaceChosen([&Log](const FMobilePlace& P) { Log += TEXT("open:") + P.Id + TEXT(";"); });
	Page->SetOnRemove([&Log](const FMobilePlace& P) { Log += TEXT("remove:") + P.Id + TEXT(";"); });
	Page->ChoosePlace(TEXT("E2-01-101"));
	Page->RemovePlace(TEXT("E2-01-101"));
	Page->ChoosePlace(TEXT("no-such-place"));
	TestEqual(TEXT("each reports (and an unknown place nothing)"), Log, FString(TEXT("open:E2-01-101;remove:E2-01-101;")));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSavedFlowTest, "InNavi.Mobile.SavedFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSavedFlowTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (Screen) Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	UMobileSavedPageWidget* Saved = Screen->GetSavedPage();
	if (!TestNotNull(TEXT("the screen has the Saved page"), Saved)) return false;
	auto Click = [](UWidget* Widget) { Cast<UButton>(Widget)->OnClicked.Broadcast(); };
	TestTrue(TEXT("nothing saved at first (no file)"), Saved->IsEmptyMessageShown() && Screen->GetSavedPlaces().Num() == 0);

	// Save place on a place page puts it on the tab, newest first.
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-101"));
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_SavePlace")));
	TestTrue(TEXT("saved on the screen"), Screen->IsPlaceSaved(TEXT("E2-01-101")));
	TestEqual(TEXT("and on the Saved tab"), Saved->GetShownPlaces().Num(), 1);
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_PlaceBack")));
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-02-101"));
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_SavePlace")));
	TestEqual(TEXT("the newest is first"), Saved->GetShownPlaces()[0].Id, FString(TEXT("E2-02-101")));
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_SavePlace")));
	TestFalse(TEXT("a second tap takes it back"), Screen->IsPlaceSaved(TEXT("E2-02-101")));
	TestEqual(TEXT("and off the tab"), Saved->GetShownPlaces().Num(), 1);

	// Save this place on the arrival page does the same.
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")));
	Click(Screen->GetRoutePage()->FindNamedWidget(TEXT("Btn_StartNavigation")));
	for (int32 Guard = 0; Guard < 30 && Screen->GetFlow().GetState() == EMobileSheetState::Navigating; ++Guard)
	{
		Click(Screen->GetNavigatePage()->FindNamedWidget(TEXT("Btn_NextInstruction")));
	}
	TestEqual(TEXT("arrived"), Screen->GetFlow().GetState(), EMobileSheetState::Arrived);
	TestFalse(TEXT("the arrival page shows the place as not saved"), Screen->GetArrivedPage()->IsSaved());
	Click(Screen->GetArrivedPage()->FindNamedWidget(TEXT("Btn_SaveThisPlace")));
	TestTrue(TEXT("saved from the arrival page"), Screen->IsPlaceSaved(TEXT("E2-02-101")) && Saved->GetShownPlaces().Num() == 2);

	// A tap on a saved place opens it on the map tab, at the place.
	Screen->SetActiveTab(UMobileAppWidget::ETab::Saved);
	Saved->ChoosePlace(TEXT("E2-01-101"));
	TestEqual(TEXT("the map tab"), Screen->GetActiveTab(), UMobileAppWidget::ETab::Directory);
	TestEqual(TEXT("the place page"), Screen->GetFlow().GetState(), EMobileSheetState::Place);
	TestEqual(TEXT("for that place"), Screen->GetPlacePage()->GetPlace().Id, FString(TEXT("E2-01-101")));
	TestTrue(TEXT("shown as saved, and on the map"), Screen->GetPlacePage()->IsSaved() && Screen->GetMapView()->GetHighlightPlace() == TEXT("E2-01-101"));

	// A place of another site opens that site first; saved places of every site are kept.
	Screen->OpenSite(TEXT("paya-lebar"));
	Screen->GetSearchPage()->ChoosePlace(TEXT("PLM-01-102"));
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_SavePlace")));
	Screen->OpenSite(TEXT("sit-punggol"));
	TestEqual(TEXT("the campus is open"), Screen->GetSiteId(), FString(TEXT("sit-punggol")));
	TestEqual(TEXT("saved places of every site are kept"), Saved->GetShownPlaces().Num(), 3);
	TestEqual(TEXT("under two sites"), Saved->GetGroupCount(), 2);
	Screen->SetActiveTab(UMobileAppWidget::ETab::Saved);
	Saved->ChoosePlace(TEXT("PLM-01-102"));
	TestEqual(TEXT("a tap on the mall's place opens the mall"), Screen->GetSiteId(), FString(TEXT("paya-lebar")));
	TestEqual(TEXT("and the place"), Screen->GetPlacePage()->GetPlace().Id, FString(TEXT("PLM-01-102")));
	TestEqual(TEXT("on the map tab"), Screen->GetActiveTab(), UMobileAppWidget::ETab::Directory);

	// Remove takes a place off, and the place page agrees.
	Saved->RemovePlace(TEXT("PLM-01-102"));
	TestFalse(TEXT("removed"), Screen->IsPlaceSaved(TEXT("PLM-01-102")));
	TestFalse(TEXT("the place page agrees"), Screen->GetPlacePage()->IsSaved());
	TestEqual(TEXT("the tab has two"), Saved->GetShownPlaces().Num(), 2);

	// Kept in a file: a new screen using it has them.
	const FString Path = TempSavedFile(TEXT("ScreenSaved_"));
	Screen->UseSavedFile(Path);
	TestEqual(TEXT("a file that is not there empties the list"), Screen->GetSavedPlaces().Num(), 0);
	Screen->OpenSite(TEXT("sit-punggol"));
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-102"));
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_SavePlace")));
	UMobileAppWidget* Next = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (Next) Next->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	Next->TakeWidget();
	Next->UseSavedFile(Path);
	TestTrue(TEXT("the next screen has the saved place"), Next->IsPlaceSaved(TEXT("E2-01-102")) && Next->GetSavedPage()->GetShownPlaces().Num() == 1);
	IFileManager::Get().Delete(*Path);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
