#if WITH_DEV_AUTOMATION_TESTS

#include "AudioPanelWidget.h"
#include "InteriorScreenWidget.h"
#include "UiTestFixture.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiAudioPanelTest, "InNavi.UI.Panels.Audio", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiAudioPanelTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>();
	Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(Controller);

	// The panel the screen shows is the shared one, and it finds its controller through the screen.
	UAudioPanelWidget* Panel = Screen->GetAudioPanelForTests();
	if (!TestNotNull(TEXT("the interior screen has the shared audio panel"), Panel)) return false;
	UCheckBox* Click = Cast<UCheckBox>(Panel->FindContentWidget(TEXT("Chk_AudioClickSound")));
	UCheckBox* Select = Cast<UCheckBox>(Panel->FindContentWidget(TEXT("Chk_AudioSelectSound")));
	UCheckBox* Quit = Cast<UCheckBox>(Panel->FindContentWidget(TEXT("Chk_AudioQuitConfirmSound")));
	UCheckBox* Mute = Cast<UCheckBox>(Panel->FindContentWidget(TEXT("Chk_AudioMuteAll")));
	USlider* Volume = Cast<USlider>(Panel->FindContentWidget(TEXT("Slider_AudioVolume")));
	UButton* StopAll = Cast<UButton>(Panel->FindContentWidget(TEXT("Btn_AudioStopAll")));
	UScrollBox* Files = Cast<UScrollBox>(Panel->FindContentWidget(TEXT("AudioFileListBox")));
	if (!TestTrue(TEXT("every control exists"), Click && Select && Quit && Mute && Volume && StopAll && Files)) return false;

	// Settings come from the controller...
	Controller->SetButtonClickSoundEnabled(false);
	Controller->SetMasterVolume(0.25f);
	Panel->Refresh();
	TestFalse(TEXT("the click-sound check box shows the controller's real setting"), Click->IsChecked());
	TestNearlyEqual(TEXT("and the volume slider its volume"), Volume->GetValue(), 0.25f, 0.001f);

	// ...and changes go back to it.
	Click->SetIsChecked(true);
	Click->OnCheckStateChanged.Broadcast(true);
	TestTrue(TEXT("ticking a sound turns it on in the controller"), Controller->IsButtonClickSoundEnabled());
	Mute->SetIsChecked(true);
	Mute->OnCheckStateChanged.Broadcast(true);
	TestFalse(TEXT("Mute All turns every UI sound off"), Controller->IsButtonClickSoundEnabled() || Controller->IsBuildingSelectSoundEnabled() || Controller->IsQuitConfirmSoundEnabled());
	TestFalse(TEXT("and unticks the three sound check boxes"), Click->IsChecked() || Select->IsChecked() || Quit->IsChecked());
	Mute->SetIsChecked(false);
	Mute->OnCheckStateChanged.Broadcast(false);
	TestTrue(TEXT("un-muting turns them all back on"), Click->IsChecked() && Select->IsChecked() && Quit->IsChecked() && Controller->IsButtonClickSoundEnabled());
	Volume->OnValueChanged.Broadcast(0.6f);
	TestNearlyEqual(TEXT("the volume slider drives the master volume"), Controller->GetMasterVolume(), 0.6f, 0.001f);

	// The width the dock gives the panel reaches its controls.
	Panel->SetContentWidth(333.f);
	USizeBox* StopAllSize = Cast<USizeBox>(StopAll->GetContent());
	if (TestNotNull(TEXT("Stop All is wrapped in a sized box"), StopAllSize))
	{
		TestNearlyEqual(TEXT("Stop All follows the panel width"), StopAllSize->GetWidthOverride(), 333.f, 0.01f);
	}
	USizeBox* ListSize = Cast<USizeBox>(Files->GetParent());
	if (TestNotNull(TEXT("the file list is wrapped in a sized box"), ListSize))
	{
		TestNearlyEqual(TEXT("the file list follows the panel width"), ListSize->GetWidthOverride(), 333.f, 0.01f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
