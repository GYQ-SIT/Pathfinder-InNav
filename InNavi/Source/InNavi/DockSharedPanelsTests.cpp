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
		TestTrue(TEXT("Building stays where it was"), BuildingBefore.Panels.Contains(ExteriorDock::Building) && BuildingAfter.Panels.Contains(ExteriorDock::Building)
			&& DockSharedPanelsTests::SamePlace(BuildingBefore.Panels[ExteriorDock::Building], BuildingAfter.Panels[ExteriorDock::Building]));
	}

	// A floating window of shared panels is taken over whole: Audio above Navigation on one screen is Audio above Navigation on the other, not two
	// tabs, and the other way round.
	{
		FDockLayout Interior, Exterior;
		InteriorDock::BuildDefaultLayout(Interior);
		ExteriorDock::BuildDefaultLayout(Exterior);
		Interior.ShowPanel(InteriorDock::Audio, EDockSide::Bottom);
		Interior.FloatPanel(InteriorDock::Audio, FVector2D(60.0, 40.0), FVector2D(340.0, 420.0));
		Interior.ShowPanel(InteriorDock::Navigation, EDockSide::Right);
		Interior.DockPanel(InteriorDock::Navigation, InteriorDock::Audio, EDockDropZone::Bottom);
		Exterior.ShowPanel(ExteriorDock::Navigation, EDockSide::Right);
		Exterior.FloatPanel(ExteriorDock::Navigation, FVector2D(300.0, 200.0), FVector2D(380.0, 440.0));
		Exterior.ShowPanel(ExteriorDock::Audio, EDockSide::Bottom);
		Exterior.DockPanel(ExteriorDock::Audio, ExteriorDock::Navigation, EDockDropZone::Center);
		const FString InteriorShape = FDockLayout::DescribeTree(*Interior.GetFloating()[0].Root);
		TestTrue(TEXT("the interior has them one above the other"), InteriorShape.StartsWith(TEXT("V(")));
		TestTrue(TEXT("the exterior has them as tabs"), FDockLayout::DescribeTree(*Exterior.GetFloating()[0].Root).StartsWith(TEXT("T[")));

		TestTrue(TEXT("the exterior takes the window over"), Apply(Exterior, Capture(Interior, Shared), Shared));
		TestEqual(TEXT("one floating window, not two"), Exterior.GetFloating().Num(), 1);
		if (Exterior.GetFloating().Num() == 1)
		{
			TestEqual(TEXT("with the interior's arrangement"), FDockLayout::DescribeTree(*Exterior.GetFloating()[0].Root), InteriorShape);
			TestTrue(TEXT("at its place and size"), Exterior.GetFloating()[0].Position.Equals(FVector2D(60.0, 40.0), 1.0) && Exterior.GetFloating()[0].Size.Equals(FVector2D(340.0, 420.0), 1.0));
		}
		TestTrue(TEXT("the layout is valid"), DockSharedPanelsTests::Valid(Exterior));
		TestFalse(TEXT("nothing is left to change"), Apply(Exterior, Capture(Interior, Shared), Shared));

		// And back: the exterior's tabs become the interior's.
		Exterior.FloatPanel(ExteriorDock::Audio, FVector2D(10.0, 10.0), FVector2D(300.0, 300.0));
		Exterior.DockPanel(ExteriorDock::Navigation, ExteriorDock::Audio, EDockDropZone::Center);
		TestTrue(TEXT("the interior takes the tabs over"), Apply(Interior, Capture(Exterior, Shared), Shared));
		TestEqual(TEXT("one window"), Interior.GetFloating().Num(), 1);
		if (Interior.GetFloating().Num() == 1)
		{
			TestTrue(TEXT("as tabs"), FDockLayout::DescribeTree(*Interior.GetFloating()[0].Root).StartsWith(TEXT("T[")));
		}
		TestTrue(TEXT("the layout is valid"), DockSharedPanelsTests::Valid(Interior));
	}

	// An edge area of shared panels only is taken over whole too; one with a panel of its own is not replaced.
	{
		FDockLayout Interior, Exterior;
		InteriorDock::BuildDefaultLayout(Interior);
		ExteriorDock::BuildDefaultLayout(Exterior);
		Interior.ShowPanel(InteriorDock::Audio, EDockSide::Bottom);
		Interior.ShowPanel(InteriorDock::Debug, EDockSide::Bottom);
		Interior.DockPanel(InteriorDock::Debug, InteriorDock::Audio, EDockDropZone::Right);
		Exterior.ShowPanel(ExteriorDock::Audio, EDockSide::Bottom);
		Exterior.FloatPanel(ExteriorDock::Audio, FVector2D(50.0, 50.0), FVector2D(300.0, 300.0));
		const FString BottomShape = FDockLayout::DescribeTree(*Interior.GetAreaRoot(EDockSide::Bottom));
		TestTrue(TEXT("the interior has Audio and Debug side by side below"), BottomShape.StartsWith(TEXT("H(")));
		TestTrue(TEXT("the exterior takes the area over"), Apply(Exterior, Capture(Interior, Shared), Shared));
		TestTrue(TEXT("Audio and Debug are docked below, side by side"), Exterior.GetAreaRoot(EDockSide::Bottom).IsValid()
			&& FDockLayout::DescribeTree(*Exterior.GetAreaRoot(EDockSide::Bottom)) == BottomShape);
		TestEqual(TEXT("and no window floats any more"), Exterior.GetFloating().Num(), 0);
		TestTrue(TEXT("the layout is valid"), DockSharedPanelsTests::Valid(Exterior));
		// The right edge holds Building (the exterior's own) with Navigation: that area is not replaced.
		FDockLocation BuildingLocation;
		TestTrue(TEXT("Building is still docked on the right"), Exterior.FindPanel(ExteriorDock::Building, BuildingLocation) && BuildingLocation.Kind == FDockLocation::EKind::Area && BuildingLocation.Side == EDockSide::Right);
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
