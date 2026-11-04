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

	// Ready after the logo has had its time: it leaves at once, over the fade.
	Splash->SetAppReady();
	TestTrue(TEXT("it leaves at once when the logo has had its time"), FMath::IsNearlyEqual(Splash->GetLeaveSeconds(), Splash->GetElapsedSeconds()));
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
	TestTrue(TEXT("a splash whose app is ready at once leaves only after the logo has been shown"), FMath::IsNearlyEqual(Early->GetLeaveSeconds(), UMobileSplashWidget::EarliestLeaveSeconds()));
	for (int32 Frame = 0; Frame < 80; ++Frame) Early->Advance(0.02);
	TestFalse(TEXT("and is still there before that time is over"), Early->IsFinished());
	TestEqual(TEXT("with nothing of it gone yet"), Early->GetRenderOpacity(), 1.f);
	for (int32 Frame = 0; Frame < 60; ++Frame) Early->Advance(0.02);
	TestTrue(TEXT("and gone when the time and the fade are over"), Early->IsFinished());

	// A long frame (the app being built) counts for no more than the longest frame, so it cannot skip the fade.
	UMobileSplashWidget* Slow = CreateWidget<UMobileSplashWidget>(World, UMobileSplashWidget::StaticClass());
	if (!TestNotNull(TEXT("a third splash is created"), Slow)) return false;
	Slow->TakeWidget();
	Slow->Advance(5.0);
	TestTrue(TEXT("a frame of five seconds moves it on by the longest frame only"), FMath::IsNearlyEqual(Slow->GetElapsedSeconds(), UMobileSplashWidget::LongestFrameSeconds));
	return true;
}

#endif
