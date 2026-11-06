#if WITH_DEV_AUTOMATION_TESTS

#include "DockLayoutModel.h"
#include "DockSharedPanels.h"
#include "ExteriorDockLayout.h"
#include "InteriorDockLayout.h"

#include "Misc/AutomationTest.h"

namespace DockSharedPanelsTests
{
	bool SamePlace(const DockSharedPanels::FPlacement& A, const DockSharedPanels::FPlacement& B)
	{
		return A.bHidden == B.bHidden && A.Kind == B.Kind && A.Side == B.Side
			&& FVector2D::Distance(A.FloatPosition, B.FloatPosition) < 1.0 && FVector2D::Distance(A.FloatSize, B.FloatSize) < 1.0;
	}

	bool Valid(const FDockLayout& Layout)
	{
		FString Error;
		return Layout.Validate(Error);
	}
}

// The panels both screens have (Audio, Map, Debug, Navigation, ...) are one panel to the person: shown or hidden, on which edge, or floating
// where, they are the same on the other screen after the switch; the panels only one screen has are not touched.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockSharedPanelsTest, "InNavi.Dock.SharedPanels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockSharedPanelsTest::RunTest(const FString& Parameters)
{
	using namespace DockSharedPanels;
	const TSet<FName> Shared = SharedPanels();
	TestTrue(TEXT("Audio, Map, Debug and Navigation are on both screens"), Shared.Contains(InteriorDock::Audio) && Shared.Contains(InteriorDock::Map) && Shared.Contains(InteriorDock::Debug) && Shared.Contains(InteriorDock::Navigation));
	TestFalse(TEXT("a panel of one screen only is not shared"), Shared.Contains(ExteriorDock::Building) || Shared.Contains(InteriorDock::Properties) || Shared.Contains(InteriorDock::Toolbar));

	// Shown on one screen, hidden on the other: it is shown there too.
	{
		FDockLayout Interior, Exterior;
		InteriorDock::BuildDefaultLayout(Interior);
		ExteriorDock::BuildDefaultLayout(Exterior);
		TestTrue(TEXT("Audio starts hidden on both"), Interior.IsHidden(InteriorDock::Audio) && Exterior.IsHidden(ExteriorDock::Audio));
		Interior.ShowPanel(InteriorDock::Audio, EDockSide::Bottom);
		TestTrue(TEXT("the exterior takes over a shown panel"), Apply(Exterior, Capture(Interior, Shared), Shared));
		TestTrue(TEXT("Audio is shown there"), Exterior.ContainsPanel(ExteriorDock::Audio));
		TestTrue(TEXT("and the layout is valid"), DockSharedPanelsTests::Valid(Exterior));
		TestFalse(TEXT("a second time there is nothing to change"), Apply(Exterior, Capture(Interior, Shared), Shared));
	}

	// Hidden on one screen, shown on the other: hidden there too.
	{
		FDockLayout Interior, Exterior;
		InteriorDock::BuildDefaultLayout(Interior);
		ExteriorDock::BuildDefaultLayout(Exterior);
		Exterior.ShowPanel(ExteriorDock::Debug, EDockSide::Bottom);
		Interior.ShowPanel(InteriorDock::Debug, EDockSide::Bottom);
		Interior.HidePanel(InteriorDock::Debug);
		TestTrue(TEXT("the exterior takes over a hidden panel"), Apply(Exterior, Capture(Interior, Shared), Shared));
		TestFalse(TEXT("Debug is gone from there"), Exterior.ContainsPanel(ExteriorDock::Debug));
		TestTrue(TEXT("and remembered as hidden"), Exterior.IsHidden(ExteriorDock::Debug));
	}

	// Floating: the same place and size.
	{
		FDockLayout Interior, Exterior;
		InteriorDock::BuildDefaultLayout(Interior);
		ExteriorDock::BuildDefaultLayout(Exterior);
		Exterior.ShowPanel(ExteriorDock::Audio, EDockSide::Bottom);
		Interior.ShowPanel(InteriorDock::Audio, EDockSide::Bottom);
		Interior.FloatPanel(InteriorDock::Audio, FVector2D(120.0, 90.0), FVector2D(300.0, 200.0));
		TestTrue(TEXT("a floating panel is taken over"), Apply(Exterior, Capture(Interior, Shared), Shared));
		FDockLocation Location;
		if (TestTrue(TEXT("Audio is in the exterior layout"), Exterior.FindPanel(ExteriorDock::Audio, Location)) && TestTrue(TEXT("floating"), Location.Kind == FDockLocation::EKind::Floating))
		{
			const FDockFloatingWindow& Window = Exterior.GetFloating()[Location.FloatingIndex];
			TestTrue(TEXT("at the same place and size"), Window.Position.Equals(FVector2D(120.0, 90.0), 1.0) && Window.Size.Equals(FVector2D(300.0, 200.0), 1.0));
		}
		TestTrue(TEXT("the layout is valid"), DockSharedPanelsTests::Valid(Exterior));
	}

	// Docked on another edge: moved to that edge. On the same edge: left where it is.
	{
		FDockLayout Interior, Exterior;
		InteriorDock::BuildDefaultLayout(Interior);
		ExteriorDock::BuildDefaultLayout(Exterior);
		FDockLocation Before, After;
		// The interior hides Map to begin with (roads are not wanted while editing a building); shown, it is on the left, as it is on the exterior.
		Interior.ShowPanel(InteriorDock::Map, EDockSide::Left);
		Interior.FindPanel(InteriorDock::Map, Before);
		TestTrue(TEXT("Map is on the left of both"), Before.Kind == FDockLocation::EKind::Area && Before.Side == EDockSide::Left);
		TestFalse(TEXT("the same edge needs no change"), Apply(Exterior, Capture(Interior, Shared), Shared));
		Exterior.DockPanelToEdge(ExteriorDock::Map, EDockSide::Right);
		TestTrue(TEXT("Map is moved on the exterior"), Exterior.FindPanel(ExteriorDock::Map, After) && After.Side == EDockSide::Right);
		TestTrue(TEXT("the interior's place comes back to it"), Apply(Exterior, Capture(Interior, Shared), Shared));
		TestTrue(TEXT("on the left"), Exterior.FindPanel(ExteriorDock::Map, After) && After.Kind == FDockLocation::EKind::Area && After.Side == EDockSide::Left);
		TestTrue(TEXT("the layout is valid"), DockSharedPanelsTests::Valid(Exterior));
	}

	// The panels of one screen only keep their places.
	{
		FDockLayout Interior, Exterior;
		InteriorDock::BuildDefaultLayout(Interior);
		ExteriorDock::BuildDefaultLayout(Exterior);
		const TSet<FName> Own = { ExteriorDock::Building };
		const FState BuildingBefore = Capture(Exterior, Own);
		Interior.ShowPanel(InteriorDock::Audio, EDockSide::Left);
		Interior.ShowPanel(InteriorDock::Navigation, EDockSide::Bottom);
		Apply(Exterior, Capture(Interior, Shared), Shared);
		const FState BuildingAfter = Capture(Exterior, Own);
		TestTrue(TEXT("Building stays where it was"), BuildingBefore.Contains(ExteriorDock::Building) && BuildingAfter.Contains(ExteriorDock::Building)
			&& DockSharedPanelsTests::SamePlace(BuildingBefore[ExteriorDock::Building], BuildingAfter[ExteriorDock::Building]));
	}

	// What the screen left is kept for the next one.
	{
		ForgetPublished();
		FDockLayout Interior, Exterior;
		InteriorDock::BuildDefaultLayout(Interior);
		ExteriorDock::BuildDefaultLayout(Exterior);
		TestFalse(TEXT("nothing is published at first"), HasPublished());
		TestFalse(TEXT("so a screen that opens takes nothing"), ApplyPublished(Exterior));
		Interior.ShowPanel(InteriorDock::Audio, EDockSide::Bottom);
		Publish(Interior);
		TestTrue(TEXT("the interior's panels are published"), HasPublished());
		TestTrue(TEXT("the exterior that opens takes them"), ApplyPublished(Exterior));
		TestTrue(TEXT("Audio is shown there"), Exterior.ContainsPanel(ExteriorDock::Audio));
		ForgetPublished();
	}
	return true;
}

#endif
