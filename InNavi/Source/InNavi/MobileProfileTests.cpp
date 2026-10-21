#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobilePlacePageWidget.h"
#include "MobilePreferences.h"
#include "MobileProfilePageWidget.h"
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
	IFileManager::Get().Delete(*Path);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
