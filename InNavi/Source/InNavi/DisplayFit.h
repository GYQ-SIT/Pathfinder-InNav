// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericWindow.h"

// What to do with the game window when the screen's resolution changes (the user switches it in the Windows display settings while the
// program runs, or the program starts on a screen whose resolution is not the one it last ran on).
//
// The engine does nothing about this by itself: on a display change Windows tells it (WM_DISPLAYCHANGE) and Slate only refreshes its cached
// display metrics, so the window keeps its old pixel size and the picture does not follow the new resolution. UDisplayChangeSubsystem reads
// the change and asks this function what the window should become. It is plain arithmetic on sizes, so that it can be tested.
namespace DisplayFit
{
	struct FDecision
	{
		// Whether the window has to be given a new size at all.
		bool bApply = false;
		// The new size of the window's client area, in pixels.
		FIntPoint Size = FIntPoint::ZeroValue;
		// Whether the window should also be moved to the middle of the monitor's work area (a window that was made smaller).
		bool bRecenter = false;
	};

	// The share of the work area a window may use after the screen got smaller: leaves room for the title bar, the borders and the taskbar.
	constexpr float WindowedMargin = 0.95f;

	// Mode: the window mode now. Window: the client size of the game window. Monitor: the size of the monitor the window is on, in pixels.
	// WorkArea: the part of that monitor not taken by the taskbar.
	//
	// - Full screen (both kinds): the window is the monitor, so it becomes the new monitor size whenever it is not that size already.
	// - Windowed: it keeps its size, unless it no longer fits the work area (the screen got smaller); then it is made to fit and centred.
	//   A window on a screen that got bigger stays as it is: the user chose that size.
	// - A monitor or work area without a size (nothing known) changes nothing.
	FDecision Decide(EWindowMode::Type Mode, const FIntPoint& Window, const FIntPoint& Monitor, const FIntPoint& WorkArea);
}
