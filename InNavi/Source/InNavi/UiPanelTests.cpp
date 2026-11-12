#if WITH_DEV_AUTOMATION_TESTS

#include "AudioPanelWidget.h"
#include "BuildingInfoPanelWidget.h"
#include "FootprintPreviewWidget.h"
#include "PolyBuilding.h"
#include "DebugPanelWidget.h"
#include "MapPanelWidget.h"
#include "MapBuildingRowWidget.h"
#include "EditScreenWidget.h"
#include "InteriorScreenWidget.h"
#include "UiTestFixture.h"
#include "UiSize.h"
#include "UiSizePanelWidget.h"
#include "UiSizeSubsystem.h"
#include "UiToolTip.h"
#include "Blueprint/WidgetTree.h"
#include "DockHostUMG.h"
#include "DockSharedPanels.h"
#include "ExteriorDockLayout.h"
#include "InteriorDockLayout.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Overlay.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/Border.h"
#include "Engine/GameInstance.h"
#include "Engine/UserInterfaceSettings.h"
#include "Components/CheckBox.h"
#include "Components/ScrollBox.h"
#include "Misc/AutomationTest.h"

// Mute All is a state of the controller: a screen that opens after it was ticked shows it ticked (it was built unticked, whatever was muted).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiAudioPanelMuteStateTest, "InNavi.UI.Panels.AudioMuteAllSurvivesTheScreen", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiAudioPanelMuteStateTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	auto Open = [&]() -> UAudioPanelWidget*
	{
		UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>();
		Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(Controller);
		UAudioPanelWidget* Panel = Screen->GetAudioPanelForTests();
		if (Panel) Panel->Refresh();
		return Panel;
	};
	auto Box = [](UAudioPanelWidget* Panel, const TCHAR* Name) { return Panel ? Cast<UCheckBox>(Panel->FindContentWidget(Name)) : nullptr; };

	Controller->SetAllUiSoundsEnabled(false);
	UAudioPanelWidget* Panel = Open();
	if (!TestNotNull(TEXT("the panel"), Panel)) return false;
	TestTrue(TEXT("muted: the screen that opens shows Mute All ticked"), Box(Panel, TEXT("Chk_AudioMuteAll"))->IsChecked());
	TestFalse(TEXT("and the three sounds unticked"), Box(Panel, TEXT("Chk_AudioClickSound"))->IsChecked() || Box(Panel, TEXT("Chk_AudioSelectSound"))->IsChecked() || Box(Panel, TEXT("Chk_AudioQuitConfirmSound"))->IsChecked());

	Controller->SetAllUiSoundsEnabled(true);
	Panel = Open();
	TestFalse(TEXT("not muted: Mute All is unticked"), Box(Panel, TEXT("Chk_AudioMuteAll"))->IsChecked());
	TestTrue(TEXT("and the three sounds ticked"), Box(Panel, TEXT("Chk_AudioClickSound"))->IsChecked() && Box(Panel, TEXT("Chk_AudioSelectSound"))->IsChecked() && Box(Panel, TEXT("Chk_AudioQuitConfirmSound"))->IsChecked());

	// One sound turned back on by itself ends the muting.
	Controller->SetAllUiSoundsEnabled(false);
	Controller->SetButtonClickSoundEnabled(true);
	Panel = Open();
	TestFalse(TEXT("with one sound on again, Mute All is unticked"), Box(Panel, TEXT("Chk_AudioMuteAll"))->IsChecked());
	TestTrue(TEXT("and that sound is ticked"), Box(Panel, TEXT("Chk_AudioClickSound"))->IsChecked());
	return true;
}

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

	// Both screens have the building list and the filters: the same panel.
	TestNotNull(TEXT("interior: building search"), Interior->FindScreenWidget(TEXT("Txt_BuildingSearch")));
	TestNotNull(TEXT("interior: campus building switch"), Interior->FindScreenWidget(TEXT("Chk_BuildingCampus")));
	TestNotNull(TEXT("exterior: building search"), Exterior->FindScreenWidget(TEXT("Txt_BuildingSearch")));
	TestNotNull(TEXT("exterior: campus building switch"), Exterior->FindScreenWidget(TEXT("Chk_BuildingCampus")));
	TestNotNull(TEXT("exterior: non-campus building switch"), Exterior->FindScreenWidget(TEXT("Chk_BuildingNonCampus")));
	TestNotNull(TEXT("exterior: reset button"), Exterior->FindScreenWidget(TEXT("Btn_ResetBuildingFilters")));

	// On the interior screen a click on a building of the list opens its interior.
	UMapPanelWidget* InteriorMap = Interior->GetMapPanelForTests();
	if (TestNotNull(TEXT("interior: the map panel"), InteriorMap))
	{
		InteriorMap->Refresh();
		TestTrue(TEXT("interior: the building list is filled"), InteriorMap->GetBuildingRowCountForTests() > 1);
		const FString Open = Fixture.Interior->GetLoadedBuildingId();
		UMapBuildingRowWidget* Other = nullptr;
		for (UMapBuildingRowWidget* Row : InteriorMap->GetBuildingRowsForTests())
		{
			if (Row->GetBuildingId() != Open && Fixture.Generator->FindBuildingById(Row->GetBuildingId())) { Other = Row; break; }
		}
		if (TestNotNull(TEXT("interior: a building other than the open one is listed"), Other))
		{
			const FString OtherId = Other->GetBuildingId();
			Other->ClickForTests();
			TestEqual(TEXT("interior: clicking it opens that building"), Fixture.Interior->GetLoadedBuildingId(), OtherId);
		}
	}

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

namespace
{
	// The text of a control's hover hint, or empty if it has none. The hint is the card UiToolTip::Make builds: an overlay whose second child is the
	// light card, which holds the text.
	FString HoverHintText(const UWidget* Control)
	{
		const UOverlay* Hint = Control ? Cast<UOverlay>(Control->GetToolTip()) : nullptr;
		const UBorder* Card = Hint && Hint->GetChildrenCount() > 1 ? Cast<UBorder>(Hint->GetChildAt(1)) : nullptr;
		const UTextBlock* Text = Card ? Cast<UTextBlock>(Card->GetContent()) : nullptr;
		return Text ? Text->GetText().ToString() : FString();
	}
}

// The hover hint every button of the UI has is as wide as its text, up to a maximum, and a longer text wraps there: a short hint is not a bar
// across the screen.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiToolTipWidthTest, "InNavi.UI.ToolTipWidth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiToolTipWidthTest::RunTest(const FString& Parameters)
{
	UWidgetTree* Tree = NewObject<UWidgetTree>(GetTransientPackage());
	auto WidthOf = [Tree](const FString& Label)
	{
		UWidget* Hint = UiToolTip::Make(Tree, Label);
		const TSharedRef<SWidget> Slate = Hint->TakeWidget();
		Slate->SlatePrepass(1.f);
		return Slate->GetDesiredSize();
	};

	const FVector2D Short = WidthOf(TEXT("Save [Ctrl+S]"));
	const FVector2D Medium = WidthOf(TEXT("Make the UI smaller [Ctrl -]"));
	const FVector2D Long = WidthOf(TEXT("Fill this floor with rooms along its outer wall and a core beside each door, then check every room that was made and the doors between them one by one"));
	AddInfo(FString::Printf(TEXT("widths: short %.0f, medium %.0f, long %.0f (maximum %.0f)"), Short.X, Medium.X, Long.X, UiToolTip::MaxWidth));

	TestTrue(TEXT("a short hint is narrower than a longer one"), Short.X < Medium.X && Medium.X < Long.X);
	TestTrue(TEXT("a short hint is well under the maximum, not stretched to it"), Medium.X < UiToolTip::MaxWidth * 0.8f);
	// The card adds its padding (10 on each side) and the shadow (3) to the text, which wraps at the maximum.
	TestTrue(TEXT("a long hint stops at the maximum and wraps"), Long.X <= UiToolTip::MaxWidth + 20.f + 3.f + 1.f);
	TestTrue(TEXT("and is taller than a short one for it"), Long.Y > Short.Y);
	return true;
}

// The UI Size panel is a dock panel like the others: on both screens, registered with the dock host, hidden at first, shown by one Window menu
// check box (the menu only shows or hides it), and it sets the size itself.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiSizePanelTest, "InNavi.UI.Panels.UiSize", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiSizePanelTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	UInteriorScreenWidget* Interior = NewObject<UInteriorScreenWidget>();
	Interior->Initialize(); Interior->TakeWidget(); Interior->SetOwningPlayer(Controller);
	UEditScreenWidget* Exterior = NewObject<UEditScreenWidget>();
	Exterior->Initialize(); Exterior->TakeWidget(); Exterior->SetOwningPlayer(Controller);

	UUserInterfaceSettings* Settings = GetMutableDefault<UUserInterfaceSettings>();
	const float OriginalScale = Settings->ApplicationScale;
	UGameInstance* Instance = NewObject<UGameInstance>(GetTransientPackage());
	UUiSizeSubsystem* Subsystem = NewObject<UUiSizeSubsystem>(Instance);
	Subsystem->SetSavingEnabledForTests(false);
	Subsystem->SetPercent(UiSize::DefaultPercent, false);

	// In both default layouts the panel is placed (along the bottom, with Audio and Debug) and hidden, and both screens share it.
	{
		FDockLayout ExteriorLayout, InteriorLayout;
		ExteriorDock::BuildDefaultLayout(ExteriorLayout);
		InteriorDock::BuildDefaultLayout(InteriorLayout);
		TestTrue(TEXT("exterior: the default layout hides the UI Size panel"), ExteriorLayout.IsHidden(ExteriorDock::UiSizePanel));
		TestTrue(TEXT("interior: the default layout hides the UI Size panel"), InteriorLayout.IsHidden(InteriorDock::UiSizePanel));
		TestTrue(TEXT("both screens list it"), ExteriorDock::AllPanels().Contains(ExteriorDock::UiSizePanel) && InteriorDock::AllPanels().Contains(InteriorDock::UiSizePanel));
		TestEqual(TEXT("under the same name"), ExteriorDock::UiSizePanel, InteriorDock::UiSizePanel);
		TestTrue(TEXT("so it is a shared panel: where it is on one screen it is on the other"), DockSharedPanels::SharedPanels().Contains(ExteriorDock::UiSizePanel));
	}

	struct FCase { const TCHAR* Name; UUiSizePanelWidget* Panel; UDockHostWidget* Dock; UWidget* MenuRow; };
	const FCase Cases[] = {
		{ TEXT("interior"), Interior->GetUiSizePanelForTests(), Interior->GetDockHost(), Interior->FindScreenWidget(TEXT("Chk_WindowUiSize")) },
		{ TEXT("exterior"), Exterior->GetUiSizePanelForTests(), Exterior->GetDockHost(), Exterior->FindScreenWidget(TEXT("Chk_WindowUiSize")) } };
	for (const FCase& Case : Cases)
	{
		if (!TestNotNull(*FString::Printf(TEXT("%s screen has the shared UI Size panel"), Case.Name), Case.Panel) || !TestNotNull(*FString::Printf(TEXT("%s: dock host"), Case.Name), Case.Dock))
		{
			return false;
		}
		const FName PanelId(TEXT("UI Size"));
		TestTrue(*FString::Printf(TEXT("%s: the panel is registered with the dock host"), Case.Name), Case.Dock->IsPanelRegistered(PanelId));

		// One Window menu row, and it only shows or hides the panel.
		UCheckBox* MenuRow = Cast<UCheckBox>(Case.MenuRow);
		if (!TestNotNull(*FString::Printf(TEXT("%s: the Window menu has a UI Size check box"), Case.Name), MenuRow)) return false;
		// Not asserted: whether the box starts ticked depends on the saved layout of the machine the test runs on (a screen reads it when it starts);
		// that the panel starts hidden is checked on the default layouts above. What is checked here is the same from any starting state.
		MenuRow->SetIsChecked(true);
		MenuRow->OnCheckStateChanged.Broadcast(true);
		TestFalse(*FString::Printf(TEXT("%s: ticking it shows the panel"), Case.Name), Case.Dock->GetLayout()->IsHidden(PanelId));
		MenuRow->SetIsChecked(false);
		MenuRow->OnCheckStateChanged.Broadcast(false);
		TestTrue(*FString::Printf(TEXT("%s: unticking it hides the panel"), Case.Name), Case.Dock->GetLayout()->IsHidden(PanelId));

		// The controls live in the panel (found through the screen), and set the size.
		UButton* Smaller = Cast<UButton>(Case.Panel->FindContentWidget(TEXT("Btn_UiSizeSmaller")));
		UButton* Larger = Cast<UButton>(Case.Panel->FindContentWidget(TEXT("Btn_UiSizeLarger")));
		UButton* Reset = Cast<UButton>(Case.Panel->FindContentWidget(TEXT("Btn_UiSizeReset")));
		UTextBlock* Value = Cast<UTextBlock>(Case.Panel->FindContentWidget(TEXT("Txt_UiSizeValue")));
		// Each one checked on its own, so that the analyser (and the next reader) can see that none is null past this point.
		if (!Smaller || !Larger || !Reset || !Value)
		{
			AddError(FString::Printf(TEXT("%s: a control of the UI Size panel is missing"), Case.Name));
			return false;
		}
		TestNotNull(*FString::Printf(TEXT("%s: the screen finds the panel's controls"), Case.Name), (Case.Name[0] == 'i' ? Interior->FindScreenWidget(TEXT("Btn_UiSizeLarger")) : Exterior->FindScreenWidget(TEXT("Btn_UiSizeLarger"))));

		Case.Panel->SetSubsystemForTests(Subsystem);
		Subsystem->SetPercent(UiSize::DefaultPercent, false);
		Larger->OnClicked.Broadcast();
		TestEqual(*FString::Printf(TEXT("%s: + makes the UI one step larger"), Case.Name), Subsystem->GetPercent(), 110);
		TestEqual(*FString::Printf(TEXT("%s: and the panel shows it"), Case.Name), Value->GetText().ToString(), FString(TEXT("110%")));
		Smaller->OnClicked.Broadcast();
		Smaller->OnClicked.Broadcast();
		TestEqual(*FString::Printf(TEXT("%s: - makes it smaller"), Case.Name), Subsystem->GetPercent(), 90);
		Reset->OnClicked.Broadcast();
		TestEqual(*FString::Printf(TEXT("%s: Default size puts it back"), Case.Name), Subsystem->GetPercent(), UiSize::DefaultPercent);
		TestEqual(*FString::Printf(TEXT("%s: and the panel shows it"), Case.Name), Value->GetText().ToString(), FString(TEXT("100%")));

		// The size is shown in a framed box between the two buttons, and there is no slider: each step rescales the whole UI, the panel moves under
		// the mouse and a dragged slider flickers.
		UBorder* Frame = Cast<UBorder>(Case.Panel->FindContentWidget(TEXT("Pnl_UiSizeValue")));
		if (TestNotNull(*FString::Printf(TEXT("%s: the size is in a framed box"), Case.Name), Frame))
		{
			TestTrue(*FString::Printf(TEXT("%s: the box holds the figure"), Case.Name), Frame->GetContent() == Value);
		}
		TestNull(*FString::Printf(TEXT("%s: no slider"), Case.Name), Case.Panel->FindContentWidget(TEXT("Slider_UiSize")));

		// What each control does, and its shortcut, is in the hover hint every button of the UI has - not in text in the panel.
		TestNull(*FString::Printf(TEXT("%s: no explanation paragraph in the panel"), Case.Name), Case.Panel->FindContentWidget(TEXT("Txt_UiSizeHelp")));
		struct FHint { const TCHAR* What; UWidget* Control; const TCHAR* Keys; };
		const FHint Hints[] = { { TEXT("smaller"), Smaller, TEXT("[Ctrl -]") }, { TEXT("larger"), Larger, TEXT("[Ctrl +]") }, { TEXT("default size"), Reset, TEXT("[Ctrl 0]") } };
		for (const FHint& Hint : Hints)
		{
			const FString Shown = HoverHintText(Hint.Control);
			TestTrue(*FString::Printf(TEXT("%s: the %s control has a hover hint with its shortcut (%s)"), Case.Name, Hint.What, *Shown), Shown.Contains(Hint.Keys));
			// It speaks of the UI, not of "everything on screen".
			TestTrue(*FString::Printf(TEXT("%s: the %s hint says what it does to the UI (%s)"), Case.Name, Hint.What, *Shown), Shown.Contains(TEXT("UI")) && !Shown.Contains(TEXT("everything")));
		}

		// The width the dock gives the panel reaches its controls.
		Case.Panel->SetContentWidth(300.f);
		USizeBox* Row = Cast<USizeBox>(Case.Panel->FindContentWidget(TEXT("Size_UiSizeRow")));
		if (TestNotNull(*FString::Printf(TEXT("%s: the row is in a sized box"), Case.Name), Row))
		{
			TestNearlyEqual(*FString::Printf(TEXT("%s: the row follows the panel width"), Case.Name), Row->GetWidthOverride(), 300.f, 0.01f);
		}
	}


	// Whatever width the dock gives it, what the panel needs fits that width: Slate's own measure of the controls, not a guess.
	for (const FCase& Case : Cases)
	{
		for (const float Width : { 100.f, 160.f, 220.f, 400.f })
		{
			Case.Panel->SetContentWidth(Width);
			const TSharedRef<SWidget> Slate = Case.Panel->TakeWidget();
			Slate->SlatePrepass(1.f);
			const float Needed = Slate->GetDesiredSize().X - 28.f; // the panel's own padding, 14 on each side
			AddInfo(FString::Printf(TEXT("%s, content width %.0f: needs %.1f"), Case.Name, Width, Needed));
			// The screen gives a panel its group's width minus 48: the 28 of padding and 20 of room for the scroll bar. A button adds its own
			// few pixels around the width it is given, which that room takes up (the guide's rule 5). The panel never goes below 148.
			TestTrue(*FString::Printf(TEXT("%s: at a content width of %.0f the controls (%.1f wide) fit the width the dock gives"), Case.Name, Width, Needed), Needed <= FMath::Max(Width, 148.f) + 20.f);
		}
	}

	Settings->ApplicationScale = OriginalScale;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
