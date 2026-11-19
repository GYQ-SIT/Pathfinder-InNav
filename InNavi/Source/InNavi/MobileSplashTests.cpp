#if WITH_DEV_AUTOMATION_TESTS

#include "MobileSplashWidget.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// The phone app's opening: black, the logo fading in, then the splash fading out onto the app.

// The opacity at every moment of the opening: black at first, the logo coming in, held, and the whole splash going when the app is ready.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSplashTimelineTest, "InNavi.Mobile.Splash.Timeline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSplashTimelineTest::RunTest(const FString& Parameters)
{
	using Splash = UMobileSplashWidget;
	TestEqual(TEXT("nothing but black at the start"), Splash::LogoOpacity(0.0), 0.f);
	TestEqual(TEXT("and until the black is over"), Splash::LogoOpacity(Splash::BlackSeconds), 0.f);
	const float Half = Splash::LogoOpacity(Splash::BlackSeconds + Splash::FadeInSeconds * 0.5);
	TestTrue(TEXT("the logo is half there in the middle of its fade"), FMath::IsNearlyEqual(Half, 0.5f, 0.01f));
	TestEqual(TEXT("and all there when it has come in"), Splash::LogoOpacity(Splash::BlackSeconds + Splash::FadeInSeconds), 1.f);
	TestEqual(TEXT("and stays"), Splash::LogoOpacity(60.0), 1.f);
	float Last = 0.f;
	bool bRises = true;
	for (double T = 0.0; T <= Splash::EarliestLeaveSeconds(); T += 0.02)
	{
		const float Now = Splash::LogoOpacity(T);
		bRises &= Now >= Last;
		Last = Now;
	}
	TestTrue(TEXT("it only comes in, never goes back"), bRises);

	TestEqual(TEXT("the splash is all there while the app is not ready"), Splash::CoverOpacity(60.0, -1.0), 1.f);
	TestEqual(TEXT("and until it leaves"), Splash::CoverOpacity(2.0, 2.0), 1.f);
	TestTrue(TEXT("it is half there in the middle of leaving"), FMath::IsNearlyEqual(Splash::CoverOpacity(2.0 + Splash::FadeOutSeconds * 0.5, 2.0), 0.5f, 0.01f));
	TestEqual(TEXT("and gone when it has left"), Splash::CoverOpacity(2.0 + Splash::FadeOutSeconds, 2.0), 0.f);
	TestTrue(TEXT("the logo is held for a moment before the splash can leave"), Splash::EarliestLeaveSeconds() > Splash::BlackSeconds + Splash::FadeInSeconds);

	// Leaving: the logo is gone before the black is, so no ghost of it is left on the app.
	const double Leave = 2.0;
	TestEqual(TEXT("the logo is all there until the splash leaves"), Splash::LogoOpacity(Leave, Leave), 1.f);
	TestEqual(TEXT("and gone a little way into the fade-out"), Splash::LogoOpacity(Leave + Splash::FadeOutSeconds * Splash::LogoFadeOutFraction, Leave), 0.f);
	TestTrue(TEXT("while the black is still there then"), Splash::CoverOpacity(Leave + Splash::FadeOutSeconds * Splash::LogoFadeOutFraction, Leave) > 0.5f);
	float LastOut = 1.f;
	bool bFalls = true;
	for (double T = Leave; T <= Leave + Splash::FadeOutSeconds; T += 0.01)
	{
		const float Now = Splash::LogoOpacity(T, Leave);
		bFalls &= Now <= LastOut;
		LastOut = Now;
	}
	TestTrue(TEXT("the logo only goes, never comes back"), bFalls);
	TestTrue(TEXT("the app is built once the logo is all in, not during a fade"), FMath::IsNearlyEqual(Splash::AppBuildSeconds, Splash::BlackSeconds + Splash::FadeInSeconds) && Splash::LogoOpacity(Splash::AppBuildSeconds) == 1.f);
	return true;
}

// The widget moves on with the frames, leaves only after the app is ready and the logo has had its time, and is not eaten by a long frame.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSplashWidgetTest, "InNavi.Mobile.Splash.Widget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSplashWidgetTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileSplashWidget* Splash = CreateWidget<UMobileSplashWidget>(World, UMobileSplashWidget::StaticClass());
	if (!TestNotNull(TEXT("the splash is created"), Splash)) return false;
	Splash->TakeWidget();
	TestFalse(TEXT("it is up at the start"), Splash->IsFinished());

	// Without the app it stays, however long it is shown.
	for (int32 Frame = 0; Frame < 400; ++Frame) Splash->Advance(0.02);
	TestFalse(TEXT("it stays while the app is not ready"), Splash->IsFinished());
	TestEqual(TEXT("with the logo all there"), Splash->GetRenderOpacity(), 1.f);

	// Ready after the logo has had its time: a few frames of the app are drawn under the cover, then it leaves, over the fade.
	Splash->SetAppReady();
	TestTrue(TEXT("it has not decided to leave before the app has been drawn"), Splash->GetLeaveSeconds() < 0.0);
	for (int32 Frame = 0; Frame < UMobileSplashWidget::FramesBeforeLeave - 1; ++Frame) Splash->Advance(0.02);
	TestTrue(TEXT("nor after fewer frames than that"), Splash->GetLeaveSeconds() < 0.0 && Splash->GetRenderOpacity() == 1.f);
	Splash->Advance(0.02);
	TestTrue(TEXT("it leaves when the logo has had its time and the app has been drawn"), FMath::IsNearlyEqual(Splash->GetLeaveSeconds(), Splash->GetElapsedSeconds()));
	for (int32 Frame = 0; Frame < 10; ++Frame) Splash->Advance(0.02);
	TestFalse(TEXT("it is going, not gone"), Splash->IsFinished());
	TestTrue(TEXT("and less there"), Splash->GetRenderOpacity() < 1.f);
	for (int32 Frame = 0; Frame < 40; ++Frame) Splash->Advance(0.02);
	TestTrue(TEXT("it is finished when the fade is over"), Splash->IsFinished());

	// Ready at once: it still shows the logo for its time.
	UMobileSplashWidget* Early = CreateWidget<UMobileSplashWidget>(World, UMobileSplashWidget::StaticClass());
	if (!TestNotNull(TEXT("another splash is created"), Early)) return false;
	Early->TakeWidget();
	Early->SetAppReady();
	for (int32 Frame = 0; Frame < UMobileSplashWidget::FramesBeforeLeave; ++Frame) Early->Advance(0.02);
	TestTrue(TEXT("a splash whose app is ready at once leaves only after the logo has been shown"), FMath::IsNearlyEqual(Early->GetLeaveSeconds(), UMobileSplashWidget::EarliestLeaveSeconds()));
	for (int32 Frame = 0; Frame < 20; ++Frame) Early->Advance(0.02);
	TestFalse(TEXT("and is still there before that time is over"), Early->IsFinished());
	TestEqual(TEXT("with nothing of it gone yet"), Early->GetRenderOpacity(), 1.f);
	for (int32 Frame = 0; Frame < 60; ++Frame) Early->Advance(0.02);
	TestTrue(TEXT("and gone when the time and the fade are over"), Early->IsFinished());

	// The outlines of the logo are drawn in their own colour, which the render opacity does not touch: they must fade with the rest (they used to appear
	// whole at the first frame and stay on the app while it showed through).
	UMobileSplashWidget* Fade = CreateWidget<UMobileSplashWidget>(World, UMobileSplashWidget::StaticClass());
	if (!TestNotNull(TEXT("a fourth splash is created"), Fade)) return false;
	Fade->TakeWidget();
	TestEqual(TEXT("at the start the outlines are not there"), Fade->GetOutlineOpacity(), 0.f);
	for (int32 Frame = 0; Frame < 80; ++Frame) Fade->Advance(0.02);
	TestEqual(TEXT("when the logo is in the outlines are all there"), Fade->GetOutlineOpacity(), 1.f);
	Fade->SetAppReady();
	for (int32 Frame = 0; Frame < UMobileSplashWidget::FramesBeforeLeave; ++Frame) Fade->Advance(0.02);
	// Into the fade-out: the logo goes first.
	Fade->Advance(UMobileSplashWidget::FadeOutSeconds * UMobileSplashWidget::LogoFadeOutFraction * 0.5);
	const float Part = Fade->GetOutlineOpacity();
	TestTrue(TEXT("the outlines go with the logo, not after it"), Part > 0.f && Part < 1.f);
	for (int32 Frame = 0; Frame < 10; ++Frame) Fade->Advance(0.02);
	TestEqual(TEXT("and are gone before the splash is"), Fade->GetOutlineOpacity(), 0.f);

	// A long frame (the app being built) counts for no more than the longest frame, so it cannot skip the fade.
	UMobileSplashWidget* Slow = CreateWidget<UMobileSplashWidget>(World, UMobileSplashWidget::StaticClass());
	if (!TestNotNull(TEXT("a third splash is created"), Slow)) return false;
	Slow->TakeWidget();
	Slow->Advance(5.0);
	TestTrue(TEXT("a frame of five seconds moves it on by the longest frame only"), FMath::IsNearlyEqual(Slow->GetElapsedSeconds(), UMobileSplashWidget::LongestFrameSeconds));
	return true;
}

#endif
