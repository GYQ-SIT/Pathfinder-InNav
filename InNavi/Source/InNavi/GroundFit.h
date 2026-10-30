#pragma once

#include "CoreMinimal.h"

// How big the ground under the campus has to be. Pure geometry, so it is tested by itself.
namespace GroundFit
{
	// The rectangle (world X, Y) the ground should cover: the union of everything that is shown (Shown), grown on every side by a margin
	// (MarginFraction of the longer side, but at least MinMargin), and at least MinSize wide and deep (grown evenly about its middle).
	// False, with OutRect untouched, when nothing is shown.
	INNAVI_API bool Fit(const TArray<FBox2D>& Shown, double MinSize, double MarginFraction, double MinMargin, FBox2D& OutRect);
}
