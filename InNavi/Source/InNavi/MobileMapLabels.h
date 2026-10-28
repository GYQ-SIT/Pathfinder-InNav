#pragma once

#include "CoreMinimal.h"

// Which of the map's labels get drawn. A label is a box on the screen; the labels are looked at in order of importance, and one that would
// cover a label already chosen (or something that has to stay clear, such as a lift's letter) is left out. Zooming in spreads the boxes
// apart, so more of them are chosen without anything else changing. Pure geometry, so it is tested by itself.
namespace MobileMapLabels
{
	struct FCandidate
	{
		// Where the label would be, in the map widget's own pixels.
		FBox2D Box = FBox2D(ForceInit);
		// Higher is chosen first. The one listed earlier wins a tie.
		double Priority = 0.0;
	};

	// One flag for each candidate: true = draw it. A box that is not at all inside View is not chosen. Padding is the gap kept around a
	// chosen label.
	INNAVI_API TArray<bool> Choose(const TArray<FCandidate>& Candidates, const TArray<FBox2D>& Obstacles, const FBox2D& View, double Padding = 3.0);
}
