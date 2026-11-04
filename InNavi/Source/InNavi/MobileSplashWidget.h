#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileSplashWidget.generated.h"

class UScaleBox;
class UWidget;

// What the phone app opens with: a black screen, the logo fading in on it, and then the whole splash fading out onto the app, which has
// been built under it by then. It also hides the desktop start screen that the level puts up for a moment before the phone app takes over.
//
// Built from C++ like the other screens, and moved on by the tick, not by an animation asset. The times are constants so that a test can
// check the opacity at any moment (LogoOpacity, CoverOpacity).
UCLASS()
class INNAVI_API UMobileSplashWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Seconds: nothing but black, then the logo comes in, then it stays, then the splash goes.
	static constexpr double BlackSeconds = 0.4;
	static constexpr double FadeInSeconds = 0.8;
	static constexpr double HoldSeconds = 0.8;
	static constexpr double FadeOutSeconds = 0.5;
	// A frame counts for no more than this much: the app is built under the splash in one frame, and that frame must not eat the fade.
	static constexpr double LongestFrameSeconds = 0.05;
	// The splash is above everything the level and the app put up.
	static constexpr int32 ZOrder = 10000;

	// How much of the logo shows (0 to 1) Seconds after the opening started.
	static float LogoOpacity(double Seconds);
	// How much of the splash (the black and the logo) shows at Seconds, when it leaves at LeaveSeconds (a negative time: it does not leave yet).
	static float CoverOpacity(double Seconds, double LeaveSeconds);
	// The earliest time the splash can leave: the logo has come in and been held.
	static constexpr double EarliestLeaveSeconds() { return BlackSeconds + FadeInSeconds + HoldSeconds; }

	// The app under the splash is built: the splash leaves once the logo has been shown for its time (at once if that has passed).
	void SetAppReady();

	// Moves the opening on by DeltaSeconds, as the tick does; a test calls it directly.
	void Advance(double DeltaSeconds);

	bool IsFinished() const { return bFinished; }
	double GetElapsedSeconds() const { return Elapsed; }
	double GetLeaveSeconds() const { return LeaveAt; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void ApplyOpacities();

	UPROPERTY()
	TObjectPtr<UWidget> LogoGroup;

	// The logo is laid out 375 design pixels wide and blown up to the window's width, like the app.
	UPROPERTY()
	TObjectPtr<UScaleBox> LogoScale;

	double Elapsed = 0.0;
	double LeaveAt = -1.0;
	bool bFinished = false;
};
