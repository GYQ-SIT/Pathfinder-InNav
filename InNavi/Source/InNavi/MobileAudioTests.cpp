#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileMockSource.h"
#include "MobileNavigatePageWidget.h"
#include "MobilePlacePageWidget.h"
#include "MobileRoutePageWidget.h"
#include "MobileSearchPageWidget.h"
#include "UiTestFixture.h"
#include "WavFileLoader.h"

#include "Components/Button.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundWaveProcedural.h"

// The sounds of the phone app: the files are in the project, and Get directions asks for its sound when the route page opens.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileDirectionsSoundTest, "InNavi.Mobile.Audio.DirectionsSound", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileDirectionsSoundTest::RunTest(const FString& Parameters)
{
	const FString AudioFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data") / TEXT("Audio"));
	// Every sound that was added for the phone is in Data/Audio, and the one for Get directions reads as the 2 seconds of 24 bit stereo it is.
	for (const TCHAR* Name : { TEXT("UI_Dull_08.wav"), TEXT("UI_Dull_05.wav"), TEXT("UI-Important-press_D.wav"), TEXT("UI_Off-Route.wav"), TEXT("UI_Place_2.wav") })
	{
		TestTrue(*FString::Printf(TEXT("%s is in Data/Audio"), Name), IFileManager::Get().FileExists(*(AudioFolder / Name)));
	}
	USoundWaveProcedural* Wave = LoadWavFileAsSoundWave(AudioFolder / TEXT("UI_Dull_08.wav"), GetTransientPackage());
	if (TestNotNull(TEXT("the Get directions sound loads"), Wave))
	{
		TestTrue(TEXT("it is about 2 seconds long"), FMath::IsNearlyEqual(Wave->Duration, 2.f, 0.1f));
		TestEqual(TEXT("in stereo"), Wave->NumChannels, 2);
	}

	// Choosing a place does not play it; Get directions does, each time the route page opens; going back does not.
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(Controller, UMobileAppWidget::StaticClass());
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	Screen->TakeWidget();
	TestEqual(TEXT("nothing has been asked for"), Controller->GetDirectionsSoundRequestsForTests(), 0);
	Screen->GetSearchPage()->ChoosePlace(TEXT("E2-01-102"));
	TestEqual(TEXT("choosing a place is not Get directions"), Controller->GetDirectionsSoundRequestsForTests(), 0);
	Cast<UButton>(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")))->OnClicked.Broadcast();
	TestEqual(TEXT("the route page is up"), Screen->GetFlow().GetState(), EMobileSheetState::Route);
	TestEqual(TEXT("Get directions asks for its sound"), Controller->GetDirectionsSoundRequestsForTests(), 1);
	Screen->SendSheetEvent(EMobileSheetEvent::DirectionsRequested);
	TestEqual(TEXT("a Get directions that the flow does not take is silent"), Controller->GetDirectionsSoundRequestsForTests(), 1);
	Screen->SendSheetEvent(EMobileSheetEvent::Back);
	TestEqual(TEXT("going back is silent"), Controller->GetDirectionsSoundRequestsForTests(), 1);
	Cast<UButton>(Screen->GetPlacePage()->FindNamedWidget(TEXT("Btn_GetDirections")))->OnClicked.Broadcast();
	TestEqual(TEXT("and the next Get directions asks again"), Controller->GetDirectionsSoundRequestsForTests(), 2);

	// Walking the route: each instruction is silent, and the last one's button ("I have arrived") plays the arrival sound as the arrival page opens.
	Cast<UButton>(Screen->GetRoutePage()->FindNamedWidget(TEXT("Btn_StartNavigation")))->OnClicked.Broadcast();
	TestEqual(TEXT("walking"), Screen->GetFlow().GetState(), EMobileSheetState::Navigating);
	TestEqual(TEXT("setting off is silent"), Controller->GetArrivalSoundRequestsForTests(), 0);
	int32 Presses = 0;
	for (; Presses < 30 && Screen->GetFlow().GetState() == EMobileSheetState::Navigating; ++Presses)
	{
		TestEqual(TEXT("no arrival sound before the last button"), Controller->GetArrivalSoundRequestsForTests(), 0);
		Cast<UButton>(Screen->GetNavigatePage()->FindNamedWidget(TEXT("Btn_NextInstruction")))->OnClicked.Broadcast();
	}
	TestEqual(TEXT("the arrival page is up"), Screen->GetFlow().GetState(), EMobileSheetState::Arrived);
	TestEqual(TEXT("arriving asks for its sound, once"), Controller->GetArrivalSoundRequestsForTests(), 1);
	TestEqual(TEXT("and not for the directions one"), Controller->GetDirectionsSoundRequestsForTests(), 2);
	USoundWaveProcedural* ArrivalWave = LoadWavFileAsSoundWave(AudioFolder / TEXT("UI_Place_2.wav"), GetTransientPackage());
	TestNotNull(TEXT("the arrival sound loads"), ArrivalWave);

	// Muting the sounds mutes these two, and Unmute brings them back.
	Controller->SetAllUiSoundsEnabled(false);
	TestFalse(TEXT("Mute All mutes the directions sound"), Controller->IsDirectionsSoundEnabled());
	TestFalse(TEXT("and the arrival sound"), Controller->IsArrivalSoundEnabled());
	Controller->SetAllUiSoundsEnabled(true);
	TestTrue(TEXT("and unmuting brings the directions sound back"), Controller->IsDirectionsSoundEnabled());
	TestTrue(TEXT("and the arrival sound"), Controller->IsArrivalSoundEnabled());
	return true;
}

#endif
