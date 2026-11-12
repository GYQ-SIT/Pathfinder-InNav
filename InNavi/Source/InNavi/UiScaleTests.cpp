#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/UserInterfaceSettings.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// The UI follows the resolution of the window: the scale comes from the window's shortest side (UIScaleCurve in DefaultEngine.ini), 0.77 at
// 1080, growing with the window above that. The phone keeps the fixed 0.77 it had (Config/Android/AndroidEngine.ini).

namespace UiScaleTests
{
	float ScaleFor(int32 Width, int32 Height)
	{
		return GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(Width, Height));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiScaleFollowsWindowTest, "InNavi.UiScale.FollowsTheWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiScaleFollowsWindowTest::RunTest(const FString& Parameters)
{
	using namespace UiScaleTests;

	TestEqual(TEXT("1080p keeps the size the screens were designed at"), ScaleFor(1920, 1080), 0.77f, 0.001f);
	TestEqual(TEXT("1440p is bigger"), ScaleFor(2560, 1440), 1.03f, 0.001f);
	TestEqual(TEXT("4K is bigger still"), ScaleFor(3840, 2160), 1.54f, 0.001f);
	TestEqual(TEXT("720p is a little smaller"), ScaleFor(1280, 720), 0.62f, 0.001f);
	TestEqual(TEXT("a very small window is still readable"), ScaleFor(854, 480), 0.55f, 0.001f);

	// Above 1080 the UI keeps its share of the screen: twice the window, twice the scale.
	TestEqual(TEXT("4K is twice 1080p"), ScaleFor(3840, 2160), 2.f * ScaleFor(1920, 1080), 0.01f);
	// Below it shrinks more slowly than the window, so text stays readable: half the height is far more than half the scale.
	TestTrue(TEXT("a window half as high keeps more than half of the scale"), ScaleFor(960, 540) > 0.5f * ScaleFor(1920, 1080));

	// The shortest side decides, so a window turned on its side gets the same scale.
	TestEqual(TEXT("a window on its side"), ScaleFor(1080, 1920), ScaleFor(1920, 1080), 0.0001f);

	// Never smaller for a larger window.
	float Previous = 0.f;
	bool bGrows = true;
	for (int32 Height = 360; Height <= 4320; Height += 60)
	{
		const float Scale = ScaleFor(Height * 16 / 9, Height);
		bGrows &= Scale >= Previous - 1.e-4f;
		Previous = Scale;
	}
	TestTrue(TEXT("the scale never shrinks as the window grows"), bGrows);
	return true;
}

// The phone is not touched by the desktop's resolution-dependent scale: its own platform file pins the scale at the fixed 0.77.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiScalePhoneFixedTest, "InNavi.UiScale.PhoneKeepsFixedScale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiScalePhoneFixedTest::RunTest(const FString& Parameters)
{
	FString Text;
	const FString Path = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("Android/AndroidEngine.ini"));
	TestTrue(TEXT("Config/Android/AndroidEngine.ini exists"), FFileHelper::LoadFileToString(Text, *Path));
	TestTrue(TEXT("it sets the user interface settings"), Text.Contains(TEXT("[/Script/Engine.UserInterfaceSettings]")));
	TestTrue(TEXT("with 0.77 at both ends of the curve"), Text.Contains(TEXT("(Time=480,Value=0.77)")) && Text.Contains(TEXT("(Time=8640,Value=0.77)")));
	TestFalse(TEXT("and nothing in between that would change it"), Text.Contains(TEXT("Time=1080")) || Text.Contains(TEXT("Time=720")));
	return true;
}

#endif
