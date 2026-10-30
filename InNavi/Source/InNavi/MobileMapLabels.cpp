#include "MobileMapLabels.h"

namespace
{
	bool Overlap(const FBox2D& A, const FBox2D& B)
	{
		return A.Min.X < B.Max.X && A.Max.X > B.Min.X && A.Min.Y < B.Max.Y && A.Max.Y > B.Min.Y;
	}
}

TArray<bool> MobileMapLabels::Choose(const TArray<FCandidate>& Candidates, const TArray<FBox2D>& Obstacles, const FBox2D& View, double Padding)
{
	TArray<bool> Chosen;
	Chosen.Init(false, Candidates.Num());
	TArray<int32> Order;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		Order.Add(Index);
	}
	// The most important first; for equal priority the one listed first, so the result does not depend on the sort.
	Order.Sort([&Candidates](int32 A, int32 B)
	{
		return Candidates[A].Priority != Candidates[B].Priority ? Candidates[A].Priority > Candidates[B].Priority : A < B;
	});

	TArray<FBox2D> Taken = Obstacles;
	for (const int32 Index : Order)
	{
		const FBox2D& Box = Candidates[Index].Box;
		if (!Box.bIsValid || !Overlap(Box, View))
		{
			continue;
		}
		const FBox2D Padded(Box.Min - FVector2D(Padding, Padding), Box.Max + FVector2D(Padding, Padding));
		bool bFree = true;
		for (const FBox2D& Other : Taken)
		{
			if (Overlap(Padded, Other))
			{
				bFree = false;
				break;
			}
		}
		if (bFree)
		{
			Chosen[Index] = true;
			Taken.Add(Box);
		}
	}
	return Chosen;
}
