#if WITH_DEV_AUTOMATION_TESTS

#include "DockLayoutModel.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesktopResponsiveFloatingTest, "InNavi.Desktop.Responsive.FloatingPanels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesktopResponsiveFloatingTest::RunTest(const FString& Parameters)
{
	FDockLayout Layout;
	Layout.FloatPanel(TEXT("Properties"), {1800, 900}, {600, 450});
	FDockMetrics Metrics;
	Metrics.TopMargin = 96.f;
	Metrics.BottomMargin = 10.f;
	FDockComputedLayout Computed;
	const FVector2D Position = Layout.GetFloating()[0].Position;
	const FVector2D Size = Layout.GetFloating()[0].Size;
	for (const FVector2D Viewport : {FVector2D(2494, 1403), FVector2D(1280, 720), FVector2D(800, 600),
		FVector2D(600, 1000), FVector2D(20, 100), FVector2D(1, 1), FVector2D(2494, 1403)})
	{
		Layout.ComputeLayout(Viewport, Metrics, Computed);
		if (!TestEqual(TEXT("floating panel remains present"), Computed.FloatingRects.Num(), 1)) return false;
		const FBox2D& Rect = Computed.FloatingRects[0];
		const float Top = FMath::Min(Metrics.TopMargin, (float)Viewport.Y);
		const float Bottom = FMath::Max(Top, (float)Viewport.Y - Metrics.BottomMargin);
		TestTrue(TEXT("all floating controls stay inside the viewport"), Rect.Min.X >= 0 && Rect.Max.X <= Viewport.X + 0.01);
		TestTrue(TEXT("floating panel stays below the bars and above the bottom margin"), Rect.Min.Y >= Top && Rect.Max.Y <= Bottom + 0.01);
		TestTrue(TEXT("floating size remains nonnegative"), Rect.GetSize().X >= 0 && Rect.GetSize().Y >= 0);
		TestTrue(TEXT("resizing preserves the stored position"), Layout.GetFloating()[0].Position.Equals(Position));
		TestTrue(TEXT("resizing preserves the stored size"), Layout.GetFloating()[0].Size.Equals(Size));
	}
	TestTrue(TEXT("growing restores the original placement"), Computed.FloatingRects[0].Min.Equals(Position));
	TestTrue(TEXT("growing restores the original size"), Computed.FloatingRects[0].GetSize().Equals(Size));
	return true;
}

#endif
