#include "GroundFit.h"

bool GroundFit::Fit(const TArray<FBox2D>& Shown, double MinSize, double MarginFraction, double MinMargin, FBox2D& OutRect)
{
	FBox2D All(ForceInit);
	for (const FBox2D& Box : Shown)
	{
		if (Box.bIsValid)
		{
			All += Box;
		}
	}
	if (!All.bIsValid)
	{
		return false;
	}
	const FVector2D Size = All.GetSize();
	const double Margin = FMath::Max(MinMargin, MarginFraction * FMath::Max(Size.X, Size.Y));
	FVector2D Min = All.Min - FVector2D(Margin, Margin);
	FVector2D Max = All.Max + FVector2D(Margin, Margin);
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const double Short = MinSize - (Max[Axis] - Min[Axis]);
		if (Short > 0.0)
		{
			Min[Axis] -= Short * 0.5;
			Max[Axis] += Short * 0.5;
		}
	}
	OutRect = FBox2D(Min, Max);
	return true;
}
