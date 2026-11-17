// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

// The user's "UI size": a choice on top of the scale the window size gives (UIScaleCurve in DefaultEngine.ini), so that a presenter can make
// the whole UI bigger or smaller at any resolution (the UI Size panel, Ctrl + / Ctrl -). 100 is the default and leaves the
// curve as it is; 150 makes everything one and a half times as big. It is applied through UUserInterfaceSettings::ApplicationScale, which
// the engine multiplies into every UI scale it computes (see UUiSizeSubsystem). This part is plain arithmetic, so that it can be tested.
namespace UiSize
{
	constexpr int32 DefaultPercent = 100;
	constexpr int32 MinPercent = 80;
	constexpr int32 MaxPercent = 200;

	// The sizes the menu and the keys step through, smallest first.
	const TArray<int32>& Steps();

	// A size brought into the range the UI can take.
	int32 Clamp(int32 Percent);

	// The next size above (Direction > 0) or below (Direction < 0) Percent. A size between two steps (a hand-edited setting) goes to the
	// nearest step in that direction; at the end of the range it stays at the end.
	int32 Step(int32 Percent, int32 Direction);

	// The factor for UUserInterfaceSettings::ApplicationScale.
	float ToApplicationScale(int32 Percent);

}
