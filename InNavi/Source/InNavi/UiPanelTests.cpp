#if WITH_DEV_AUTOMATION_TESTS

#include "AudioPanelWidget.h"
#include "BuildingInfoPanelWidget.h"
#include "FootprintPreviewWidget.h"
#include "PolyBuilding.h"
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

	auto CheckRoadSection = [this](const TCHAR* Screen, auto* Widget, bool bShownByDefault)
	{
		UCheckBox* HideAll = Cast<UCheckBox>(Widget->FindScreenWidget(TEXT("Chk_RoadHideAll")));
		UCheckBox* Primary = Cast<UCheckBox>(Widget->FindScreenWidget(TEXT("Chk_RoadPrimary")));
		UCheckBox* CampusOnly = Cast<UCheckBox>(Widget->FindScreenWidget(TEXT("Chk_RoadCampusOnly")));
		if (!TestNotNull(*FString::Printf(TEXT("%s: Hide All exists"), Screen), HideAll) || !TestNotNull(*FString::Printf(TEXT("%s: Primary exists"), Screen), Primary)
			|| !TestNotNull(*FString::Printf(TEXT("%s: Campus Only exists"), Screen), CampusOnly))
		{
			return;
		}
		// The exterior screen starts with the roads shown; the interior editor with them hidden (Hide All checked).
		TestEqual(*FString::Printf(TEXT("%s: Primary starts %s"), Screen, bShownByDefault ? TEXT("shown") : TEXT("hidden")), Primary->IsChecked(), bShownByDefault);
		TestEqual(*FString::Printf(TEXT("%s: Hide All starts %s"), Screen, bShownByDefault ? TEXT("unchecked") : TEXT("checked")), HideAll->IsChecked(), !bShownByDefault);
		TestTrue(*FString::Printf(TEXT("%s: Campus Only starts on"), Screen), CampusOnly->IsChecked());
		// The master switch drags the category check boxes along with it, both ways.
		HideAll->SetIsChecked(true);
		HideAll->OnCheckStateChanged.Broadcast(true);
		TestFalse(*FString::Printf(TEXT("%s: Hide All unchecks the categories"), Screen), Primary->IsChecked());
		HideAll->SetIsChecked(false);
		HideAll->OnCheckStateChanged.Broadcast(false);
		TestTrue(*FString::Printf(TEXT("%s: unticking Hide All brings them back"), Screen), Primary->IsChecked());
	};
	CheckRoadSection(TEXT("interior"), Interior, false);
	CheckRoadSection(TEXT("exterior"), Exterior, true);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiBuildingPanelTest, "InNavi.UI.Panels.Building", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiBuildingPanelTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	UEditScreenWidget* Exterior = NewObject<UEditScreenWidget>();
	Exterior->Initialize(); Exterior->TakeWidget(); Exterior->SetOwningPlayer(Controller);
	UBuildingInfoPanelWidget* Panel = Exterior->GetBuildingInfoPanelForTests();
	if (!TestNotNull(TEXT("the exterior screen has the Building panel"), Panel))
	{
		return false;
	}

	Panel->Refresh();
	TestFalse(TEXT("with nothing selected it asks for a selection"), Panel->IsShowingBuildingForTests());

	// E2 has rooms and lifts / stairs; E1 has neither rooms nor a vertical link in the example project.
	TestTrue(TEXT("E2 can be selected"), Controller->SelectBuildingById(TEXT("E2")));
	Panel->Refresh();
	TestTrue(TEXT("a selected building is shown"), Panel->IsShowingBuildingForTests());
	TestEqual(TEXT("with its name"), Panel->GetNameTextForTests(), Fixture.Generator->FindBuildingById(TEXT("E2"))->BuildingName);
	TestEqual(TEXT("its id"), Panel->GetIdTextForTests(), FString(TEXT("Building ID: E2")));
	TestEqual(TEXT("its floors"), Panel->GetFloorsTextForTests(), FString::FromInt(Fixture.Generator->FindBuildingById(TEXT("E2"))->GetFloorCount()));
	TestTrue(TEXT("its rooms"), FCString::Atoi(*Panel->GetRoomsTextForTests()) > 0);
	TestTrue(TEXT("its lifts and stairs"), FCString::Atoi(*Panel->GetLinksTextForTests()) > 0);
	TestTrue(TEXT("and its outline"), Panel->GetOutlinePointCountForTests() >= 3);
	TestTrue(TEXT("a building with rooms is not marked empty"), Panel->GetStatusTextForTests().Contains(TEXT("Has rooms")));

	TestTrue(TEXT("E1 can be selected"), Controller->SelectBuildingById(TEXT("E1")));
	Panel->RefreshIfChanged();
	TestEqual(TEXT("the panel follows the selection"), Panel->GetIdTextForTests(), FString(TEXT("Building ID: E1")));
	TestEqual(TEXT("a building without rooms has none"), Panel->GetRoomsTextForTests(), FString(TEXT("0")));
	TestEqual(TEXT("and is marked empty"), Panel->GetStatusTextForTests(), FString(TEXT("Empty")));

	// The two buttons hand over to whoever set them (the screen opens the interior editor).
	int32 Opened2D = 0, Opened3D = 0;
	Panel->OnOpen2D = [&Opened2D]() { ++Opened2D; };
	Panel->OnOpen3D = [&Opened3D]() { ++Opened3D; };
	if (TestNotNull(TEXT("the 2D button"), Panel->GetOpen2DButtonForTests()) && TestNotNull(TEXT("the 3D button"), Panel->GetOpen3DButtonForTests()))
	{
		Panel->GetOpen2DButtonForTests()->OnClicked.Broadcast();
		Panel->GetOpen3DButtonForTests()->OnClicked.Broadcast();
		TestEqual(TEXT("Open 2D floor editor"), Opened2D, 1);
		TestEqual(TEXT("Open 3D building view"), Opened3D, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiFootprintOutlineTest, "InNavi.UI.FootprintOutline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiFootprintOutlineTest::RunTest(const FString& Parameters)
{
	TArray<FVector2f> Line;
	// A 200 x 100 rectangle in a 100 x 100 space: scaled to fit the width, centred vertically, inside the margin.
	const TArray<FVector2D> Rectangle = { FVector2D(1000, 500), FVector2D(1200, 500), FVector2D(1200, 600), FVector2D(1000, 600) };
	TestTrue(TEXT("an outline is made"), SFootprintPreview::FitOutline(Rectangle, FVector2D(100, 100), Line));
	TestEqual(TEXT("the four corners and the closing point"), Line.Num(), 5);
	if (Line.Num() == 5)
	{
		TestEqual(TEXT("the outline is closed"), Line[0], Line[4]);
		for (const FVector2f& Point : Line)
		{
			TestTrue(TEXT("every point is inside the space"), Point.X >= 0.f && Point.X <= 100.f && Point.Y >= 0.f && Point.Y <= 100.f);
		}
		TestTrue(TEXT("proportions are kept (twice as wide as tall)"), FMath::IsNearlyEqual((Line[1].X - Line[0].X), 2.f * (Line[2].Y - Line[1].Y), 0.01f));
	}
	TestFalse(TEXT("two points are not an outline"), SFootprintPreview::FitOutline({ FVector2D(0, 0), FVector2D(1, 1) }, FVector2D(100, 100), Line));
	TestFalse(TEXT("no space, no outline"), SFootprintPreview::FitOutline(Rectangle, FVector2D(0, 0), Line));
	TestFalse(TEXT("a single repeated point has no extent"), SFootprintPreview::FitOutline({ FVector2D(5, 5), FVector2D(5, 5), FVector2D(5, 5) }, FVector2D(100, 100), Line));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
