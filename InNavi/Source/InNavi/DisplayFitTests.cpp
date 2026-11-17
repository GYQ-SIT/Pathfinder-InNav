#if WITH_DEV_AUTOMATION_TESTS

#include "DisplayFit.h"
#include "Misc/AutomationTest.h"

// What the game window does when the screen's resolution changes: a full-screen window follows the monitor, a window that no longer fits is
// made to fit, and nothing else moves.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDisplayFitFullscreenTest, "InNavi.Display.FullscreenFollowsTheMonitor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDisplayFitFullscreenTest::RunTest(const FString& Parameters)
{
	for (const EWindowMode::Type Mode : { EWindowMode::WindowedFullscreen, EWindowMode::Fullscreen })
	{
		// The screen was switched from 2K to 1080p: the window becomes the 1080p monitor.
		DisplayFit::FDecision Down = DisplayFit::Decide(Mode, FIntPoint(2560, 1440), FIntPoint(1920, 1080), FIntPoint(1920, 1040));
		TestTrue(TEXT("a smaller screen resizes the window"), Down.bApply && Down.Size == FIntPoint(1920, 1080));
		TestFalse(TEXT("a full-screen window is not moved to the middle"), Down.bRecenter);

		// And back up: the window grows with the monitor.
		DisplayFit::FDecision Up = DisplayFit::Decide(Mode, FIntPoint(1920, 1080), FIntPoint(2560, 1440), FIntPoint(2560, 1400));
		TestTrue(TEXT("a bigger screen resizes the window"), Up.bApply && Up.Size == FIntPoint(2560, 1440));

		// Already the size of the monitor: nothing to do.
		TestFalse(TEXT("the same size changes nothing"), DisplayFit::Decide(Mode, FIntPoint(1920, 1080), FIntPoint(1920, 1080), FIntPoint(1920, 1040)).bApply);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDisplayFitWindowedTest, "InNavi.Display.WindowedFitsTheScreen", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDisplayFitWindowedTest::RunTest(const FString& Parameters)
{
	// A window made for a big screen no longer fits a small one: it is made to fit, with a margin, and centred.
	const DisplayFit::FDecision Shrunk = DisplayFit::Decide(EWindowMode::Windowed, FIntPoint(2400, 1300), FIntPoint(1366, 768), FIntPoint(1366, 728));
	TestTrue(TEXT("a window that no longer fits is resized"), Shrunk.bApply);
	TestTrue(TEXT("to inside the work area, with a margin of 5 percent"), Shrunk.Size == FIntPoint(1297, 691));
	TestTrue(TEXT("and centred"), Shrunk.bRecenter);

	// Only the side that is too big is made smaller.
	const DisplayFit::FDecision OneSide = DisplayFit::Decide(EWindowMode::Windowed, FIntPoint(1000, 900), FIntPoint(1920, 1080), FIntPoint(1920, 600));
	TestTrue(TEXT("a window too high keeps its width"), OneSide.bApply && OneSide.Size == FIntPoint(1000, 570));

	// A window that fits stays as the user left it, on a smaller and on a bigger screen.
	TestFalse(TEXT("a window that fits is left alone"), DisplayFit::Decide(EWindowMode::Windowed, FIntPoint(1280, 720), FIntPoint(1920, 1080), FIntPoint(1920, 1040)).bApply);
	TestFalse(TEXT("also when the screen got bigger"), DisplayFit::Decide(EWindowMode::Windowed, FIntPoint(1280, 720), FIntPoint(3840, 2160), FIntPoint(3840, 2120)).bApply);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDisplayFitUnknownTest, "InNavi.Display.UnknownSizesChangeNothing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDisplayFitUnknownTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("no monitor known, full screen"), DisplayFit::Decide(EWindowMode::WindowedFullscreen, FIntPoint(1920, 1080), FIntPoint::ZeroValue, FIntPoint::ZeroValue).bApply);
	TestFalse(TEXT("no work area known, windowed"), DisplayFit::Decide(EWindowMode::Windowed, FIntPoint(1920, 1080), FIntPoint(1280, 720), FIntPoint::ZeroValue).bApply);
	TestFalse(TEXT("a window without a size"), DisplayFit::Decide(EWindowMode::WindowedFullscreen, FIntPoint::ZeroValue, FIntPoint(1920, 1080), FIntPoint(1920, 1040)).bApply);
	return true;
}

#endif
