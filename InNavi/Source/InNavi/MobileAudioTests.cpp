#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileMockSource.h"
#include "MobileNavigatePageWidget.h"
#include "MobilePlacePageWidget.h"
#include "MobilePreferences.h"
#include "MobileProfilePageWidget.h"
#include "MobileRoutePageWidget.h"
#include "MobileSearchPageWidget.h"
#include "UiTestFixture.h"
#include "WavFileLoader.h"

#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundWaveProcedural.h"

// The sounds of the phone app: the files are in the project, and Start navigation asks for its sound as the walk begins (Get directions is silent).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileStartNavigationSoundTest, "InNavi.Mobile.Audio.StartNavigationSound", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileStartNavigationSoundTest::RunTest(const FString& Parameters)
{
	const FString AudioFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data") / TEXT("Audio"));
	// Every sound that was added for the phone is in Data/Audio, and the one for Start navigation reads as the 2 seconds of 24 bit stereo it is.
	for (const TCHAR* Name : { TEXT("UI_Dull_08.wav"), TEXT("UI_Dull_05.wav"), TEXT("UI-Important-press_D.wav"), TEXT("UI_Off-Route.wav"), TEXT("UI_Place_2.wav") })
	{
		TestTrue(*FString::Printf(TEXT("%s is in Data/Audio"), Name), IFileManager::Get().FileExists(*(AudioFolder / Name)));
	}
	USoundWaveProcedural* Wave = LoadWavFileAsSoundWave(AudioFolder / TEXT("UI_Dull_08.wav"), GetTransientPackage());
	if (TestNotNull(TEXT("the Start navigation sound loads"), Wave))
	{
		TestTrue(TEXT("it is about 2 seconds long"), FMath::IsNearlyEqual(Wave->Duration, 2.f, 0.1f));
		TestEqual(TEXT("in stereo"), Wave->NumChannels, 2);
	}

	// Choosing a place does not play it, and neither does Get directions (the route page only shows the route); Start navigation does, each time
	// the walk begins; going back does not.
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(Controller, UMobileAppWidget::StaticClass());
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	Screen->TakeWidget();
	TestEqual(TEXT("nothing has been asked for"), Controller->GetStartNavigationSoundRequestsForTests(), 0);
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-102"));
	TestEqual(TEXT("choosing a place is not Start navigation"), Controller->GetStartNavigationSoundRequestsForTests(), 0);
	Cast<UButton>(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")))->OnClicked.Broadcast();
	TestEqual(TEXT("the route page is up"), Screen->GetFlow().GetState(), EMobileSheetState::Route);
	TestEqual(TEXT("Get directions is silent"), Controller->GetStartNavigationSoundRequestsForTests(), 0);
	Screen->SendSheetEvent(EMobileSheetEvent::Back);
	TestEqual(TEXT("going back is silent"), Controller->GetStartNavigationSoundRequestsForTests(), 0);
	Cast<UButton>(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")))->OnClicked.Broadcast();
	TestEqual(TEXT("and so is the next Get directions"), Controller->GetStartNavigationSoundRequestsForTests(), 0);

	// Start navigation plays its sound as the walk begins; then each instruction is silent, and the last one's button ("I have arrived") plays
	// the arrival sound as the arrival page opens.
	Cast<UButton>(Screen->GetRoutePage()->FindNamedWidget(TEXT("Btn_StartNavigation")))->OnClicked.Broadcast();
	TestEqual(TEXT("walking"), Screen->GetFlow().GetState(), EMobileSheetState::Navigating);
	TestEqual(TEXT("Start navigation asks for its sound"), Controller->GetStartNavigationSoundRequestsForTests(), 1);
	Screen->SendSheetEvent(EMobileSheetEvent::NavigationStarted);
	TestEqual(TEXT("a Start navigation that the flow does not take is silent"), Controller->GetStartNavigationSoundRequestsForTests(), 1);
	TestEqual(TEXT("setting off has no arrival sound"), Controller->GetArrivalSoundRequestsForTests(), 0);
	int32 Presses = 0;
	for (; Presses < 30 && Screen->GetFlow().GetState() == EMobileSheetState::Navigating; ++Presses)
	{
		TestEqual(TEXT("no arrival sound before the last button"), Controller->GetArrivalSoundRequestsForTests(), 0);
		Cast<UButton>(Screen->GetNavigatePage()->FindNamedWidget(TEXT("Btn_NextInstruction")))->OnClicked.Broadcast();
	}
	TestEqual(TEXT("every instruction before the last one played the Next instruction sound"), Controller->GetNextInstructionSoundRequestsForTests(), FMath::Max(0, Presses - 1));
	TestEqual(TEXT("the arrival page is up"), Screen->GetFlow().GetState(), EMobileSheetState::Arrived);
	TestEqual(TEXT("arriving asks for its sound, once"), Controller->GetArrivalSoundRequestsForTests(), 1);
	TestEqual(TEXT("and not again for the start one"), Controller->GetStartNavigationSoundRequestsForTests(), 1);
	USoundWaveProcedural* ArrivalWave = LoadWavFileAsSoundWave(AudioFolder / TEXT("UI_Place_2.wav"), GetTransientPackage());
	TestNotNull(TEXT("the arrival sound loads"), ArrivalWave);

	// Muting the sounds mutes these two, and Unmute brings them back.
	Controller->SetAllUiSoundsEnabled(false);
	TestFalse(TEXT("Mute All mutes the Start navigation sound"), Controller->IsStartNavigationSoundEnabled());
	TestFalse(TEXT("and the arrival sound"), Controller->IsArrivalSoundEnabled());
	Controller->SetAllUiSoundsEnabled(true);
	TestTrue(TEXT("and unmuting brings the Start navigation sound back"), Controller->IsStartNavigationSoundEnabled());
	TestTrue(TEXT("and the arrival sound"), Controller->IsArrivalSoundEnabled());
	TestFalse(TEXT("the Next instruction sound was muted with them"), (Controller->SetAllUiSoundsEnabled(false), Controller->IsNextInstructionSoundEnabled()));
	TestTrue(TEXT("and comes back"), (Controller->SetAllUiSoundsEnabled(true), Controller->IsNextInstructionSoundEnabled()));
	TestTrue(TEXT("UI_Dull_05 loads"), LoadWavFileAsSoundWave(AudioFolder / TEXT("UI_Dull_05.wav"), GetTransientPackage()) != nullptr);
	return true;
}

// The volume of the phone's sounds: kept with the other preferences (full when there is none), moved on the Profile page, heard at once,
// and put back to full by Reset.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileVolumeTest, "InNavi.Mobile.Audio.Volume", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileVolumeTest::RunTest(const FString& Parameters)
{
	const FString File = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("volume-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));

	// The preferences: full volume to begin with, kept in the file, clamped, and full again for a file that has none.
	FMobilePreferences Preferences;
	TestEqual(TEXT("full volume to begin with"), Preferences.GetVolume(), 1.f);
	Preferences.UseFile(File);
	TestTrue(TEXT("a volume is set"), Preferences.SetVolume(0.4f));
	TestTrue(TEXT("and kept"), Preferences.SetTheme(EMobileTheme::Night));
	FMobilePreferences Reread;
	Reread.UseFile(File);
	TestTrue(TEXT("it reads back from the file"), FMath::IsNearlyEqual(Reread.GetVolume(), 0.4f, 0.001f) && Reread.GetTheme() == EMobileTheme::Night);
	Preferences.SetVolume(7.f);
	TestEqual(TEXT("above full is full"), Preferences.GetVolume(), 1.f);
	Preferences.SetVolume(-1.f);
	TestEqual(TEXT("below silent is silent"), Preferences.GetVolume(), 0.f);
	FFileHelper::SaveStringToFile(TEXT("{ \"version\": 1, \"theme\": \"night\", \"largerText\": true }"), *File);
	Reread.UseFile(File);
	TestTrue(TEXT("a file from before the volume is full volume"), Reread.GetVolume() == 1.f && Reread.IsLargerText());
	FFileHelper::SaveStringToFile(TEXT("{ \"version\": 1, \"volume\": 3 }"), *File);
	Reread.UseFile(File);
	TestEqual(TEXT("a volume above 1 in a file is full"), Reread.GetVolume(), 1.f);
	Preferences.SetVolume(0.2f);
	Preferences.Reset();
	TestEqual(TEXT("Reset is full volume"), Preferences.GetVolume(), 1.f);

	// The Profile page and the screen.
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(Controller, UMobileAppWidget::StaticClass());
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	Screen->TakeWidget();
	IFileManager::Get().Delete(*File);
	Screen->UsePreferencesFile(File);
	UMobileProfilePageWidget* Page = Screen->GetProfilePage();
	if (!TestNotNull(TEXT("the profile page"), Page)) return false;
	USlider* Slider = Cast<USlider>(Page->FindNamedWidget(TEXT("Slider_ProfileVolume")));
	UTextBlock* Number = Cast<UTextBlock>(Page->FindNamedWidget(TEXT("Txt_ProfileVolume")));
	if (!TestTrue(TEXT("the page has a volume slider and its number"), Slider && Number)) return false;
	TestEqual(TEXT("the slider starts at full"), Slider->GetValue(), 1.f);
	TestEqual(TEXT("and says 100%"), Number->GetText().ToString(), FString(TEXT("100%")));

	// Moving the slider is heard at once and not written; lifting the finger writes it.
	Controller->SetMasterVolume(1.f);
	Page->ChooseVolume(0.35f, false);
	TestTrue(TEXT("the sounds follow the slider"), FMath::IsNearlyEqual(Controller->GetMasterVolume(), 0.35f, 0.001f));
	TestEqual(TEXT("the number follows it"), Number->GetText().ToString(), FString(TEXT("35%")));
	TestEqual(TEXT("it is not kept yet"), Screen->GetPreferences().GetVolume(), 1.f);
	Page->ChooseVolume(0.35f, true);
	TestTrue(TEXT("the finger is lifted: it is kept"), FMath::IsNearlyEqual(Screen->GetPreferences().GetVolume(), 0.35f, 0.001f));
	TestEqual(TEXT("and the page says so"), Page->GetStatusString(), FString(TEXT("Volume updated")));
	FMobilePreferences OnDisk;
	OnDisk.UseFile(File);
	TestTrue(TEXT("in the file"), FMath::IsNearlyEqual(OnDisk.GetVolume(), 0.35f, 0.001f));

	// The slider itself calls the same way.
	Slider->SetValue(0.6f);
	Slider->OnValueChanged.Broadcast(0.6f);
	TestTrue(TEXT("the slider moves the volume"), FMath::IsNearlyEqual(Controller->GetMasterVolume(), 0.6f, 0.001f));
	Slider->OnMouseCaptureEnd.Broadcast();
	TestTrue(TEXT("and lifting the finger keeps it"), FMath::IsNearlyEqual(Screen->GetPreferences().GetVolume(), 0.6f, 0.001f));

	// A new start with that file is at that volume; Reset is full.
	Controller->SetMasterVolume(1.f);
	Screen->UsePreferencesFile(File);
	TestTrue(TEXT("opening the app with the file sets the volume"), FMath::IsNearlyEqual(Controller->GetMasterVolume(), 0.6f, 0.001f));
	Page->TapReset();
	TestEqual(TEXT("Reset puts the volume back to full"), Controller->GetMasterVolume(), 1.f);
	TestEqual(TEXT("and the slider"), Slider->GetValue(), 1.f);
	IFileManager::Get().Delete(*File);
	return true;
}

#endif
