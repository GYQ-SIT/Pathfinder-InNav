#if WITH_DEV_AUTOMATION_TESTS

#include "AudioPanelWidget.h"
#include "DebugPanelWidget.h"
#include "MapPanelWidget.h"
#include "EditScreenWidget.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiDebugPanelTest, "InNavi.UI.Panels.Debug", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiDebugPanelTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();

	UInteriorScreenWidget* Interior = NewObject<UInteriorScreenWidget>();
	Interior->Initialize(); Interior->TakeWidget(); Interior->SetOwningPlayer(Controller);
	UEditScreenWidget* Exterior = NewObject<UEditScreenWidget>();
	Exterior->Initialize(); Exterior->TakeWidget(); Exterior->SetOwningPlayer(Controller);

	struct FCase { const TCHAR* Name; UDebugPanelWidget* Panel; const TCHAR* ExpectedFirstLine; };
	const FCase Cases[] = {
		{ TEXT("interior"), Interior->GetDebugPanelForTests(), TEXT("Building:") },
		{ TEXT("exterior"), Exterior->GetDebugPanelForTests(), TEXT("Selected Building:") } };
	for (const FCase& Case : Cases)
	{
		if (!TestNotNull(*FString::Printf(TEXT("%s screen has the shared debug panel"), Case.Name), Case.Panel)) return false;
		Case.Panel->BindToController();
		TestTrue(*FString::Printf(TEXT("%s: starts with no error"), Case.Name), Case.Panel->GetLastErrorTextForTests().Contains(TEXT("None")) || Case.Panel->GetLastErrorTextForTests().IsEmpty());
	}

	// A rejected action shows up in both panels, yellow for a warning...
	Controller->OnActionFailed.Broadcast(TEXT("Something was refused"), EActionFailureSeverity::Warning);
	for (const FCase& Case : Cases)
	{
		TestTrue(*FString::Printf(TEXT("%s: shows the failure message"), Case.Name), Case.Panel->GetLastErrorTextForTests().Contains(TEXT("Something was refused")));
		TestTrue(*FString::Printf(TEXT("%s: a warning is yellow"), Case.Name), Case.Panel->GetLastErrorColorForTests().Equals(FLinearColor::Yellow, 0.01f));
		// ...with the screen's own lines first and the frame figures after them.
		const FString Info = Case.Panel->GetInfoTextForTests();
		TestTrue(*FString::Printf(TEXT("%s: the screen's own lines come first (%s)"), Case.Name, *Info.Left(30)), Info.StartsWith(Case.ExpectedFirstLine));
		TestTrue(*FString::Printf(TEXT("%s: followed by FPS and frame time"), Case.Name), Info.Contains(TEXT("FPS:")) && Info.Contains(TEXT("Frame Time:")));
	}

	// ...and red when data may have been lost.
	Controller->OnActionFailed.Broadcast(TEXT("Save failed"), EActionFailureSeverity::Critical);
	for (const FCase& Case : Cases)
	{
		TestTrue(*FString::Printf(TEXT("%s: a critical failure is red"), Case.Name), Case.Panel->GetLastErrorColorForTests().Equals(FLinearColor::Red, 0.01f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiMapPanelTest, "InNavi.UI.Panels.Map", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiMapPanelTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();

	UInteriorScreenWidget* Interior = NewObject<UInteriorScreenWidget>();
	Interior->Initialize(); Interior->TakeWidget(); Interior->SetOwningPlayer(Controller);
	UEditScreenWidget* Exterior = NewObject<UEditScreenWidget>();
	Exterior->Initialize(); Exterior->TakeWidget(); Exterior->SetOwningPlayer(Controller);

	auto CheckRoadSection = [this](const TCHAR* Screen, auto* Widget)
	{
		UCheckBox* HideAll = Cast<UCheckBox>(Widget->FindScreenWidget(TEXT("Chk_RoadHideAll")));
		UCheckBox* Primary = Cast<UCheckBox>(Widget->FindScreenWidget(TEXT("Chk_RoadPrimary")));
		UCheckBox* CampusOnly = Cast<UCheckBox>(Widget->FindScreenWidget(TEXT("Chk_RoadCampusOnly")));
		if (!TestNotNull(*FString::Printf(TEXT("%s: Hide All exists"), Screen), HideAll) || !TestNotNull(*FString::Printf(TEXT("%s: Primary exists"), Screen), Primary)
			|| !TestNotNull(*FString::Printf(TEXT("%s: Campus Only exists"), Screen), CampusOnly))
		{
			return;
		}
		TestTrue(*FString::Printf(TEXT("%s: Primary starts shown"), Screen), Primary->IsChecked());
		TestTrue(*FString::Printf(TEXT("%s: Campus Only starts on"), Screen), CampusOnly->IsChecked());
		// The master switch drags the category check boxes along with it, both ways.
		HideAll->SetIsChecked(true);
		HideAll->OnCheckStateChanged.Broadcast(true);
		TestFalse(*FString::Printf(TEXT("%s: Hide All unchecks the categories"), Screen), Primary->IsChecked());
		HideAll->SetIsChecked(false);
		HideAll->OnCheckStateChanged.Broadcast(false);
		TestTrue(*FString::Printf(TEXT("%s: unticking Hide All brings them back"), Screen), Primary->IsChecked());
	};
	CheckRoadSection(TEXT("interior"), Interior);
	CheckRoadSection(TEXT("exterior"), Exterior);

	// Only the exterior screen has outside buildings to filter.
	TestNull(TEXT("interior: no building search"), Interior->FindScreenWidget(TEXT("Txt_BuildingSearch")));
	TestNull(TEXT("interior: no campus building switch"), Interior->FindScreenWidget(TEXT("Chk_BuildingCampus")));
	TestNotNull(TEXT("exterior: building search"), Exterior->FindScreenWidget(TEXT("Txt_BuildingSearch")));
	TestNotNull(TEXT("exterior: campus building switch"), Exterior->FindScreenWidget(TEXT("Chk_BuildingCampus")));
	TestNotNull(TEXT("exterior: non-campus building switch"), Exterior->FindScreenWidget(TEXT("Chk_BuildingNonCampus")));
	TestNotNull(TEXT("exterior: reset button"), Exterior->FindScreenWidget(TEXT("Btn_ResetBuildingFilters")));

	// The site overview: a list of buildings, and the filters and roads in sections that start closed.
	UMapPanelWidget* Map = Exterior->GetMapPanelForTests();
	if (TestNotNull(TEXT("exterior: the map panel"), Map))
	{
		Map->Refresh();
		TestTrue(TEXT("the building list is filled"), Map->GetBuildingRowCountForTests() > 0);
		TestFalse(TEXT("the filters section starts closed"), Map->IsFiltersSectionOpenForTests());
		if (UButton* FiltersHeader = Cast<UButton>(Exterior->FindScreenWidget(TEXT("FiltersHeader"))))
		{
			FiltersHeader->OnClicked.Broadcast();
			TestTrue(TEXT("its header opens it"), Map->IsFiltersSectionOpenForTests());
			FiltersHeader->OnClicked.Broadcast();
			TestFalse(TEXT("and closes it again"), Map->IsFiltersSectionOpenForTests());
		}
		else
		{
			AddError(TEXT("the filters header is missing"));
		}
		// A floor-count chip narrows the list; Any brings it back.
		const int32 All = Map->GetBuildingRowCountForTests();
		if (UButton* OneFloor = Cast<UButton>(Exterior->FindScreenWidget(TEXT("Btn_FloorsOne"))))
		{
			OneFloor->OnClicked.Broadcast();
			TestEqual(TEXT("1 floor leaves only one-floor buildings (the example has none)"), Map->GetBuildingRowCountForTests(), 0);
			Cast<UButton>(Exterior->FindScreenWidget(TEXT("Btn_FloorsAny")))->OnClicked.Broadcast();
			TestEqual(TEXT("Any brings them all back"), Map->GetBuildingRowCountForTests(), All);
		}
		else
		{
			AddError(TEXT("the floor chips are missing"));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
