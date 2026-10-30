#pragma once

#include "CoreMinimal.h"

// How the last few seconds of frames went: what the Debug panel shows, so a frame rate can be read off at the demo (and checked against
// the budget in docs/performance.md) instead of only the one smoothed figure. Pure logic, no engine objects.
struct FFrameStats
{
	// Seconds of frames kept.
	static constexpr double WindowSeconds = 10.0;

	void AddFrame(double DeltaSeconds)
	{
		if (DeltaSeconds <= 0.0)
		{
			return;
		}
		Deltas.Add(DeltaSeconds);
		Total += DeltaSeconds;
		// The oldest frames go once the window is longer than it should be.
		int32 Drop = 0;
		while (Drop < Deltas.Num() - 1 && Total > WindowSeconds)
		{
			Total -= Deltas[Drop++];
		}
		if (Drop > 0)
		{
			Deltas.RemoveAt(0, Drop, EAllowShrinking::No);
		}
	}

	int32 NumFrames() const { return Deltas.Num(); }
	// Frames per second over the window: frames divided by the time they took, not the mean of the rates.
	double AverageFps() const { return Total > 0.0 ? Deltas.Num() / Total : 0.0; }
	// The rate of the slowest frame.
	double MinFps() const { return Deltas.IsEmpty() ? 0.0 : 1.0 / Slowest(1); }
	// The rate over the slowest 1 % of the frames (at least one): the usual "1 % low".
	double OnePercentLowFps() const { return Deltas.IsEmpty() ? 0.0 : 1.0 / Slowest(FMath::Max(1, FMath::CeilToInt(0.01 * Deltas.Num()))); }
	FString ToText() const
	{
		return FString::Printf(TEXT("Last %.0f s: avg %.1f | 1%% low %.1f | min %.1f FPS"), FMath::Min(Total, WindowSeconds), AverageFps(), OnePercentLowFps(), MinFps());
	}

private:
	// The mean frame time of the Count slowest frames.
	double Slowest(int32 Count) const
	{
		TArray<double> Sorted = Deltas;
		Sorted.Sort([](double A, double B) { return A > B; });
		double Sum = 0.0;
		for (int32 Index = 0; Index < Count; ++Index) Sum += Sorted[Index];
		return Sum / Count;
	}

	TArray<double> Deltas;
	double Total = 0.0;
};
