#if WITH_DEV_AUTOMATION_TESTS

#include "DockHostUMG.h"
#include "EditScreenWidget.h"
#include "ExteriorDockLayout.h"
#include "InteriorDockLayout.h"
#include "InteriorScreenWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// UI unification guard.
//
// Every panel in the program lives in the dock system (DockHostWidget): its content is registered with the dock
// host and the user can drag, dock, float, hide and save it. These tests keep it that way. A screen's root canvas may
// hold only the dock host and a short, fixed list of *chrome* - things that are deliberately NOT dockable:
//
//   Background / FloorPlanWidget   the 2D plan itself and the backdrop behind it (the "central canvas")
//   Pnl_MenuBar, Pnl_BuildingSwitcherBar   the fixed bars along the top
//   Pnl_*Dropdown, MenuClickCatcher        the menus that open from those bars, and the click-away layer that closes them
//   Txt_InspectionHint                     transient viewport-mode instructions; fixed beside the model, not a movable tool panel
//   Img_InspectionPivot                    projects the bottom anchor into the viewport; must follow the model, not a dock panel
//   Chrome_*                               decoration such as divider lines
//   DockHost                               the dock system itself
//
// If this test fails because you added a Border/panel straight onto the canvas: make it a dock panel instead
// (see docs/design/ui-panels-guide.md). Only add a name here if the element truly must stay fixed, and say why above.

namespace
{
	bool IsAllowedCanvasChild(const UWidget* Child)
	{
		static const TCHAR* const AllowedNames[] = {
			TEXT("Background"), TEXT("FloorPlanWidget"), TEXT("DockHost"), TEXT("MenuClickCatcher"),
			TEXT("Pnl_MenuBar"), TEXT("Pnl_BuildingSwitcherBar"), TEXT("Txt_InspectionHint"), TEXT("Img_InspectionPivot"),
			TEXT("Pnl_FileDropdown"), TEXT("Pnl_WindowDropdown"), TEXT("Pnl_CameraDropdown") };
		const FString Name = Child->GetName();
		for (const TCHAR* Allowed : AllowedNames)
		{
			if (Name == Allowed)
			{
				return true;
			}
		}
		return Name.StartsWith(TEXT("Chrome_"));
	}

	template <typename TScreen>
	void CheckScreen(FAutomationTestBase& Test, const TCHAR* ScreenName, const TArray<FName>& Panels)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		TScreen* Screen = CreateWidget<TScreen>(World, TScreen::StaticClass());
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s created"), ScreenName), Screen))
		{
			return;
		}
		Screen->TakeWidget();
		UCanvasPanel* Canvas = Screen->WidgetTree ? Cast<UCanvasPanel>(Screen->WidgetTree->RootWidget) : nullptr;
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s has a root canvas"), ScreenName), Canvas))
		{
			return;
		}

		// 1. Nothing but the dock host and the known chrome sits directly on the canvas.
		bool bSawDockHost = false;
		for (UWidget* Child : Canvas->GetAllChildren())
		{
			bSawDockHost |= Child == Cast<UWidget>(Screen->GetDockHost());
			Test.TestTrue(*FString::Printf(TEXT("%s: '%s' (%s) is on the root canvas but is neither the dock host nor listed chrome - make it a dock panel (docs/design/ui-panels-guide.md)"),
				ScreenName, *Child->GetName(), *Child->GetClass()->GetName()), IsAllowedCanvasChild(Child));
		}

		Test.TestTrue(*FString::Printf(TEXT("%s: the dock host is a child of the root canvas (so the walk above really sees the screen's contents)"), ScreenName), bSawDockHost);

		// 2. The dock host is there, and every panel of this screen has been registered with it.
		UDockHostWidget* Dock = Screen->GetDockHost();
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s has a dock host"), ScreenName), Dock))
		{
			return;
		}
		for (const FName Panel : Panels)
		{
			Test.TestTrue(*FString::Printf(TEXT("%s: panel '%s' is registered with the dock host"), ScreenName, *Panel.ToString()), Dock->IsPanelRegistered(Panel));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiGuardRuleTest, "InNavi.UI.Guard.Rule", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiGuardRuleTest::RunTest(const FString& Parameters)
{
	// The rule itself: stray panels are rejected, the known chrome and the dock host are accepted.
	UBorder* Stray = NewObject<UBorder>(GetTransientPackage(), TEXT("Pnl_SomeNewPanel"));
	UBorder* Panel = NewObject<UBorder>(GetTransientPackage(), TEXT("Pnl_Audio"));
	UBorder* Chrome = NewObject<UBorder>(GetTransientPackage(), TEXT("Chrome_Separator_40"));
	UBorder* Bar = NewObject<UBorder>(GetTransientPackage(), TEXT("Pnl_MenuBar"));
	TestFalse(TEXT("a new panel put straight on the canvas is rejected"), IsAllowedCanvasChild(Stray));
	TestFalse(TEXT("so is a dock panel's own content root (it must be registered with the dock host, not placed)"), IsAllowedCanvasChild(Panel));
	TestTrue(TEXT("named chrome is accepted"), IsAllowedCanvasChild(Chrome));
	TestTrue(TEXT("the menu bar is accepted"), IsAllowedCanvasChild(Bar));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiGuardInteriorTest, "InNavi.UI.Guard.InteriorScreen", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiGuardInteriorTest::RunTest(const FString& Parameters)
{
	CheckScreen<UInteriorScreenWidget>(*this, TEXT("Interior screen"), InteriorDock::AllPanels());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiGuardExteriorTest, "InNavi.UI.Guard.ExteriorScreen", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiGuardExteriorTest::RunTest(const FString& Parameters)
{
	CheckScreen<UEditScreenWidget>(*this, TEXT("Exterior screen"), ExteriorDock::AllPanels());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
