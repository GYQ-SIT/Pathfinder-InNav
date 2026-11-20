#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileSplashWidget.generated.h"

class UBorder;
class UScaleBox;
class UWidget;

// What the phone app opens with: a black screen, the logo fading in on it, and then the splash fading out onto the app, which has been built
// under it by then. It also hides the desktop start screen that the level puts up for a moment before the phone app takes over.
//
// Building the app is one long frame (the campus is read, the screens are made: about a second on a phone), and a frame that long freezes
// whatever is moving. So the app is built when the logo has just come in, while the splash stands still, never during a fade; the fade-out
// starts only after a few frames of the app have been drawn under the cover (the first draw of a screen is slow too), and the logo goes before
// the black does, so no ghost of it is left on the app.
//
// Built from C++ like the other screens, and moved on by the tick, not by an animation asset. The times are constants so that a test can
// check the opacity at any moment (LogoOpacity, CoverOpacity).
UCLASS()
class INNAVI_API UMobileSplashWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Seconds: nothing but black, then the logo comes in, then it stays, then the splash goes.
	static constexpr double BlackSeconds = 0.1;
	static constexpr double FadeInSeconds = 0.5;
	static constexpr double HoldSeconds = 0.4;
	static constexpr double FadeOutSeconds = 0.35;
	// The first part of the fade-out in which the logo goes (the black goes over all of it): the logo is gone before the app shows through.
	static constexpr double LogoFadeOutFraction = 0.4;
	// The frames the app is drawn under the cover before the fade-out starts, so the slow first draws are not in it.
	static constexpr int32 FramesBeforeLeave = 3;
	// A frame counts for no more than this much: the app is built under the splash in one frame, and that frame must not eat the fade.
	static constexpr double LongestFrameSeconds = 0.05;
	// The splash is above everything the level and the app put up.
	static constexpr int32 ZOrder = 10000;

	// How much of the logo shows (0 to 1) Seconds after the opening started, when the splash leaves at LeaveSeconds (a negative time: it does not leave yet).
	static float LogoOpacity(double Seconds, double LeaveSeconds = -1.0);
	// How much of the splash (the black and the logo) shows at Seconds, when it leaves at LeaveSeconds (a negative time: it does not leave yet).
	static float CoverOpacity(double Seconds, double LeaveSeconds);
	// When the logo has come in all the way: the app is built then (the screen is still while it is).
	static constexpr double AppBuildSeconds = BlackSeconds + FadeInSeconds;
	// The earliest time the splash can leave: the logo has come in and been held.
	static constexpr double EarliestLeaveSeconds() { return BlackSeconds + FadeInSeconds + HoldSeconds; }

	// The app under the splash is built: after a few frames of it, the splash leaves once the logo has been shown for its time (at once if that has passed).
	void SetAppReady();

	// Moves the opening on by DeltaSeconds, as the tick does; a test calls it directly.
	void Advance(double DeltaSeconds);

	bool IsFinished() const { return bFinished; }
	double GetElapsedSeconds() const { return Elapsed; }
	double GetLeaveSeconds() const { return LeaveAt; }
	// How strong the outlines of the logo are drawn (0 to 1): what the opacity put into their colour.
	float GetOutlineOpacity() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void ApplyOpacities();

	UPROPERTY()
	TObjectPtr<UWidget> LogoGroup;

	// The two outlined boxes of the logo. An outline is drawn in its own colour, which the render opacity of the widget does not touch (the fills and the
	// text fade, the outlines stay at full strength: the logo popped in whole and was left on the app while it faded), so the opacity goes into the
	// outline colour every frame.
	UPROPERTY()
	TObjectPtr<UBorder> MarkFrame;
	UPROPERTY()
	TObjectPtr<UBorder> SymbolBox;

	// The logo is laid out 375 design pixels wide and blown up to the window's width, like the app.
	UPROPERTY()
	TObjectPtr<UScaleBox> LogoScale;

	double Elapsed = 0.0;
	double LeaveAt = -1.0;
	bool bAppReady = false;
	int32 FramesSinceReady = 0;
	bool bFinished = false;
};
