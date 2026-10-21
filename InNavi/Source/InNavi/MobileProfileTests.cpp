#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileMapWidget.h"
#include "MobileNavigatePageWidget.h"
#include "MobilePlacePageWidget.h"
#include "MobilePreferences.h"
#include "MobileProfilePageWidget.h"
#include "MobileRoutePageWidget.h"
#include "MobileStyle.h"
#include "MobileSavedPageWidget.h"
#include "MobileSearchPageWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

// The Profile tab: the preferences (kept in a file), the page that shows them, and the reset.

namespace
{
	FString TempPreferencesFile()
	{
		return FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("Preferences_") + FGuid::NewGuid().ToString() + TEXT(".json"));
	}

	// The theme is one for the whole process; a test that changes it puts it back.
	struct FAppearanceGuard
	{
		FAppearanceGuard() { MobileStyle::ApplyAppearance(EMobileTheme::Light, false); }
		~FAppearanceGuard() { MobileStyle::ApplyAppearance(EMobileTheme::Light, false); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobilePreferencesTest, "InNavi.Mobile.Preferences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobilePreferencesTest::RunTest(const FString& Parameters)
{
	FMobilePreferences Prefs;
	TestTrue(TEXT("light and normal text at first"), Prefs.GetTheme() == EMobileTheme::Light && !Prefs.IsLargerText());
	Prefs.SetTheme(EMobileTheme::Night);
	Prefs.SetLargerText(true);
	TestFalse(TEXT("without a file nothing is written"), Prefs.Save());
	Prefs.Reset();
	TestTrue(TEXT("reset is the app's own choices"), Prefs.GetTheme() == EMobileTheme::Light && !Prefs.IsLargerText());

	const FString Path = TempPreferencesFile();
	{
		FMobilePreferences Kept;
		Kept.UseFile(Path);
		TestTrue(TEXT("a file that is not there is the defaults"), Kept.GetTheme() == EMobileTheme::Light && !Kept.IsLargerText());
		Kept.SetTheme(EMobileTheme::Night);
		Kept.SetLargerText(true);
	}
	FMobilePreferences Reopened;
	Reopened.UseFile(Path);
	TestTrue(TEXT("the choices come back"), Reopened.GetTheme() == EMobileTheme::Night && Reopened.IsLargerText());
	Reopened.Reset();
	FMobilePreferences AfterReset;
	AfterReset.UseFile(Path);
	TestTrue(TEXT("and so does a reset"), AfterReset.GetTheme() == EMobileTheme::Light && !AfterReset.IsLargerText());

	FFileHelper::SaveStringToFile(TEXT("{ this is not json"), *Path);
	FMobilePreferences Damaged;
	Damaged.UseFile(Path);
	TestTrue(TEXT("a damaged file is the defaults"), Damaged.GetTheme() == EMobileTheme::Light && !Damaged.IsLargerText());
	IFileManager::Get().Delete(*Path);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileProfilePageTest, "InNavi.Mobile.ProfilePage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileProfilePageTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileProfilePageWidget* Page = CreateWidget<UMobileProfilePageWidget>(World, UMobileProfilePageWidget::StaticClass());
	if (!TestNotNull(TEXT("the page is created"), Page)) return false;
	Page->TakeWidget();
	for (const TCHAR* Name : { TEXT("Txt_ProfileTitle"), TEXT("Txt_ProfileSubtitle"), TEXT("Txt_ProfileName"), TEXT("Txt_ProfileAccount"), TEXT("Txt_ProfileAppearance"), TEXT("Txt_ProfileAbout"), TEXT("Txt_ProfileStatus"), TEXT("Txt_ProfileNote"), TEXT("ProfileScroll") })
	{
		TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Page->FindNamedWidget(Name));
	}
	TestEqual(TEXT("the title"), Cast<UTextBlock>(Page->FindNamedWidget(TEXT("Txt_ProfileTitle")))->GetText().ToString(), FString(TEXT("Make it yours")));
	TestEqual(TEXT("there is no account"), Cast<UTextBlock>(Page->FindNamedWidget(TEXT("Txt_ProfileName")))->GetText().ToString(), FString(TEXT("Visitor")));
	TestTrue(TEXT("light and normal text at first, and nothing said yet"), Page->GetShownTheme() == EMobileTheme::Light && !Page->IsLargerTextShown() && Page->GetStatusString().IsEmpty());

	// What the screen gives it shows, without saying anything.
	FMobilePreferences Prefs;
	Prefs.SetTheme(EMobileTheme::Night);
	Prefs.SetLargerText(true);
	Page->SetPreferences(Prefs);
	TestTrue(TEXT("shows the preferences it is given"), Page->GetShownTheme() == EMobileTheme::Night && Page->IsLargerTextShown() && Page->GetStatusString().IsEmpty());
	Page->SetNote(TEXT("All simulated."));
	TestEqual(TEXT("the closing note"), Page->GetNoteString(), FString(TEXT("All simulated.")));

	// Each tap reports, and the page says what happened.
	FString Log;
	Page->SetOnThemeChosen([&Log](EMobileTheme Theme) { Log += Theme == EMobileTheme::Night ? TEXT("night;") : TEXT("light;"); });
	Page->SetOnLargerTextToggled([&Log](bool bLarger) { Log += bLarger ? TEXT("larger;") : TEXT("normal;"); });
	Page->SetOnReset([&Log]() { Log += TEXT("reset;"); });
	Page->ChooseTheme(EMobileTheme::Light);
	TestEqual(TEXT("the theme is shown"), static_cast<int32>(Page->GetShownTheme()), static_cast<int32>(EMobileTheme::Light));
	TestEqual(TEXT("and said"), Page->GetStatusString(), FString(TEXT("Appearance updated")));
	Page->ToggleLargerText();
	TestFalse(TEXT("larger text is turned off by a tap"), Page->IsLargerTextShown());
	Page->TapReset();
	TestEqual(TEXT("reset says so"), Page->GetStatusString(), FString(TEXT("Saved places and preferences were reset")));
	TestEqual(TEXT("each tap reported"), Log, FString(TEXT("light;normal;reset;")));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileProfileFlowTest, "InNavi.Mobile.ProfileFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileProfileFlowTest::RunTest(const FString& Parameters)
{
	FAppearanceGuard Guard;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	UMobileProfilePageWidget* Profile = Screen->GetProfilePage();
	if (!TestNotNull(TEXT("the screen has the Profile page"), Profile)) return false;
	auto Click = [](UWidget* Widget) { Cast<UButton>(Widget)->OnClicked.Broadcast(); };

	// The tab shows the page, and the page tells the screen what was chosen.
	Click(Screen->FindNamedWidget(TEXT("Btn_TabProfile")));
	TestEqual(TEXT("the Profile tab"), Screen->GetActiveTab(), UMobileAppWidget::ETab::Profile);
	TestTrue(TEXT("with the source's note that its data is simulated"), !Profile->GetNoteString().IsEmpty() && Profile->GetNoteString().Contains(TEXT("simulated")));
	const FString Path = TempPreferencesFile();
	Screen->UsePreferencesFile(Path);
	Profile->ChooseTheme(EMobileTheme::Night);
	Profile->ToggleLargerText();
	TestTrue(TEXT("the screen has the choices"), Screen->GetPreferences().GetTheme() == EMobileTheme::Night && Screen->GetPreferences().IsLargerText());

	// They are kept: a new screen using the file shows them.
	UMobileAppWidget* Next = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	Next->TakeWidget();
	Next->UsePreferencesFile(Path);
	TestTrue(TEXT("the next screen has them"), Next->GetPreferences().GetTheme() == EMobileTheme::Night && Next->GetProfilePage()->GetShownTheme() == EMobileTheme::Night && Next->GetProfilePage()->IsLargerTextShown());
	TestFalse(TEXT("built in the look that is open, it has nothing to build again"), Next->IsAppearanceRebuildQueued());
	Next->RebuildForAppearance();
	Screen->RebuildForAppearance();
	Profile = Screen->GetProfilePage();

	// Reset takes the saved places off (and the place and Saved pages agree) and puts the preferences back.
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-101"));
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_SavePlace")));
	TestTrue(TEXT("a place is saved"), Screen->IsPlaceSaved(TEXT("E2-01-101")) && Screen->GetPlacePage()->IsSaved());
	Profile->TapReset();
	TestEqual(TEXT("nothing is saved"), Screen->GetSavedPlaces().Num(), 0);
	TestTrue(TEXT("the Saved tab is empty"), Screen->GetSavedPage()->IsEmptyMessageShown());
	TestFalse(TEXT("the place page agrees"), Screen->GetPlacePage()->IsSaved());
	TestTrue(TEXT("the preferences are the app's own"), Screen->GetPreferences().GetTheme() == EMobileTheme::Light && !Screen->GetPreferences().IsLargerText());
	TestTrue(TEXT("and so is what the page shows"), Profile->GetShownTheme() == EMobileTheme::Light && !Profile->IsLargerTextShown());
	TestEqual(TEXT("the page says it was reset"), Profile->GetStatusString(), FString(TEXT("Saved places and preferences were reset")));
	Screen->RebuildForAppearance();
	IFileManager::Get().Delete(*Path);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileAppearanceTest, "InNavi.Mobile.Appearance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileAppearanceTest::RunTest(const FString& Parameters)
{
	FAppearanceGuard Guard;
	const FLinearColor LightInk = MobileStyle::Ink;
	const FLinearColor LightSurface = MobileStyle::White;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	auto Click = [](UWidget* Widget) { Cast<UButton>(Widget)->OnClicked.Broadcast(); };
	auto TitleSize = [](UMobileAppWidget* S) -> float { return Cast<UTextBlock>(S->GetProfilePage()->FindNamedWidget(TEXT("Txt_ProfileTitle")))->GetFont().Size; };
	const float NormalTitleSize = TitleSize(Screen);

	// Somewhere in the middle of a walk, on the Profile tab, with a place saved and the tilted map.
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-02-101"));
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_SavePlace")));
	Click(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")));
	Click(Screen->GetRoutePage()->FindNamedWidget(TEXT("Btn_StartNavigation")));
	Click(Screen->GetNavigatePage()->FindNamedWidget(TEXT("Btn_NextInstruction")));
	Screen->SetMap3D(true);
	Screen->SetActiveTab(UMobileAppWidget::ETab::Profile);
	const int32 Step = Screen->GetNavStepIndex();
	const FString MapBuilding = Screen->GetMapBuildingId();
	const int32 MapLevel = Screen->GetMapLevel();
	UMobileProfilePageWidget* OldProfile = Screen->GetProfilePage();
	TestFalse(TEXT("nothing waits at first"), Screen->IsAppearanceRebuildQueued());

	// The night theme: the colours change at once, the pages are built again a moment later, and the screen is as it was.
	OldProfile->ChooseTheme(EMobileTheme::Night);
	TestTrue(TEXT("the colours are the night ones"), !FMath::IsNearlyEqual(MobileStyle::Ink.R, LightInk.R) && MobileStyle::Ink.R > 0.5f && MobileStyle::White.R < 0.2f);
	TestTrue(TEXT("a build again is waiting"), Screen->IsAppearanceRebuildQueued());
	Screen->RebuildForAppearance();
	TestFalse(TEXT("and done"), Screen->IsAppearanceRebuildQueued());
	TestTrue(TEXT("the Profile page is a new one, showing the night theme and what it said"), Screen->GetProfilePage() != OldProfile && Screen->GetProfilePage()->GetShownTheme() == EMobileTheme::Night && Screen->GetProfilePage()->GetStatusString() == TEXT("Appearance updated"));
	TestEqual(TEXT("still on the Profile tab"), Screen->GetActiveTab(), UMobileAppWidget::ETab::Profile);
	TestEqual(TEXT("still walking"), Screen->GetFlow().GetState(), EMobileSheetState::Navigating);
	TestTrue(TEXT("on the same step of the same route"), Screen->GetNavStepIndex() == Step && Screen->GetCurrentRoute().bFound && Screen->GetNavigatePage()->GetProgressString().Contains(FString::Printf(TEXT("Step %d of"), Step + 1)));
	TestEqual(TEXT("to the same place"), Screen->GetSelectedPlace().Id, FString(TEXT("E2-02-101")));
	TestTrue(TEXT("the place is still saved, on the page too"), Screen->IsPlaceSaved(TEXT("E2-02-101")) && Screen->GetSavedPage()->GetShownPlaces().Num() == 1 && Screen->GetPlacePage()->IsSaved());
	TestTrue(TEXT("the map shows the same level, tilted, with the route"), Screen->GetMapBuildingId() == MapBuilding && Screen->GetMapLevel() == MapLevel && Screen->IsShowing3D() && Screen->GetMapView()->IsShowing3D() && Screen->GetMapView()->GetRoutePointsOnLevel() > 0);
	TestEqual(TEXT("and the site's name"), Cast<UTextBlock>(Screen->FindNamedWidget(TEXT("SiteName")))->GetText().ToString(), FString(TEXT("SIT Campus")));
	TestTrue(TEXT("the same theme again waits for nothing"), (Screen->GetProfilePage()->ChooseTheme(EMobileTheme::Night), !Screen->IsAppearanceRebuildQueued()));

	// Larger text: the text is a little bigger (1.125 times).
	Screen->GetProfilePage()->ToggleLargerText();
	TestTrue(TEXT("a build again is waiting for larger text"), Screen->IsAppearanceRebuildQueued());
	Screen->RebuildForAppearance();
	TestTrue(TEXT("the title is bigger"), TitleSize(Screen) > NormalTitleSize && FMath::IsNearlyEqual(MobileStyle::TextScale, 1.125f));
	TestEqual(TEXT("still walking after it"), Screen->GetFlow().GetState(), EMobileSheetState::Navigating);

	// Reset puts the light theme and the normal size back.
	Screen->GetProfilePage()->TapReset();
	TestTrue(TEXT("a build again is waiting for the reset"), Screen->IsAppearanceRebuildQueued());
	Screen->RebuildForAppearance();
	TestTrue(TEXT("the colours are the light ones"), FMath::IsNearlyEqual(MobileStyle::Ink.R, LightInk.R) && FMath::IsNearlyEqual(MobileStyle::White.R, LightSurface.R));
	TestEqual(TEXT("the text is the normal size"), TitleSize(Screen), NormalTitleSize);
	TestEqual(TEXT("nothing is saved"), Screen->GetSavedPlaces().Num(), 0);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
