#if WITH_DEV_AUTOMATION_TESTS

#include "InNaviDPIScalingRule.h"
#include "DockLayoutModel.h"
#include "Engine/UserInterfaceSettings.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesktopResponsiveScaleTest, "InNavi.Desktop.Responsive.Scale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesktopResponsiveScaleTest::RunTest(const FString& Parameters)
{
	struct FCase { FIntPoint Size; float Expected; };
	const FCase Cases[] = {
		{{1920, 1080}, 0.77f}, {{1280, 720}, 0.77f * 2.f / 3.f},
		{{1366, 768}, 0.77f * 768.f / 1080.f}, {{2560, 1440}, 0.77f * 4.f / 3.f},
		{{3840, 2160}, 1.54f}, {{3440, 1440}, 0.77f * 4.f / 3.f},
		{{1280, 1024}, 0.77f * 2.f / 3.f}, {{1080, 1920}, 0.77f * 1080.f / 1920.f},
		{{800, 600}, 0.77f * 800.f / 1920.f}
	};
	for (const FCase& Case : Cases)
	{
		const float Scale = UInNaviDPIScalingRule::CalculateScale(Case.Size, false);
		const FString Label = FString::Printf(TEXT("%dx%d"), Case.Size.X, Case.Size.Y);
		TestNearlyEqual(*Label, Scale, Case.Expected, 0.0001f);
		// The fixed desktop controls must always have at least the design's logical space.
		TestTrue(*(Label + TEXT(" fits the design width")), Case.Size.X / Scale >= 1920.f / 0.77f - 0.01f);
		TestTrue(*(Label + TEXT(" fits the design height")), Case.Size.Y / Scale >= 1080.f / 0.77f - 0.01f);
		TestEqual(*(Label + TEXT(" preserves phone scaling")), UInNaviDPIScalingRule::CalculateScale(Case.Size, true), 0.77f);
	}
	for (const FIntPoint Size : {FIntPoint(0, 0), FIntPoint(1920, 0), FIntPoint(-1, 1080), FIntPoint(1, 1)})
	{
		const float Scale = UInNaviDPIScalingRule::CalculateScale(Size, false);
		TestTrue(TEXT("minimized/tiny viewports have a finite positive scale"), FMath::IsFinite(Scale) && Scale >= 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesktopResponsiveSettingsTest, "InNavi.Desktop.Responsive.ConfiguredRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesktopResponsiveSettingsTest::RunTest(const FString& Parameters)
{
	const UUserInterfaceSettings* Settings = GetDefault<UUserInterfaceSettings>();
	TestTrue(TEXT("UMG uses the custom rule"), Settings->UIScaleRule == EUIScalingRule::Custom);
	for (const FIntPoint Size : {FIntPoint(1280, 720), FIntPoint(1920, 1080), FIntPoint(3840, 2160)})
	{
		TestNearlyEqual(TEXT("configured rule resolves and drives viewport DPI"), Settings->GetDPIScaleBasedOnSize(Size),
			GetDefault<UInNaviDPIScalingRule>()->GetDPIScaleBasedOnSize(Size) * Settings->ApplicationScale, 0.0001f);
	}
	return true;
}

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
