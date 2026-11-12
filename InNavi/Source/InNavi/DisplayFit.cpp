// Fill out your copyright notice in the Description page of Project Settings.

#include "DisplayFit.h"

DisplayFit::FDecision DisplayFit::Decide(EWindowMode::Type Mode, const FIntPoint& Window, const FIntPoint& Monitor, const FIntPoint& WorkArea)
{
	FDecision Decision;
	if (Window.X <= 0 || Window.Y <= 0)
	{
		return Decision;
	}

	if (Mode == EWindowMode::Fullscreen || Mode == EWindowMode::WindowedFullscreen)
	{
		if (Monitor.X > 0 && Monitor.Y > 0 && Window != Monitor)
		{
			Decision.bApply = true;
			Decision.Size = Monitor;
		}
		return Decision;
	}

	if (WorkArea.X <= 0 || WorkArea.Y <= 0)
	{
		return Decision;
	}
	const FIntPoint Limit(FMath::FloorToInt(WorkArea.X * WindowedMargin), FMath::FloorToInt(WorkArea.Y * WindowedMargin));
	if (Window.X > Limit.X || Window.Y > Limit.Y)
	{
		Decision.bApply = true;
		Decision.Size = FIntPoint(FMath::Min(Window.X, Limit.X), FMath::Min(Window.Y, Limit.Y));
		Decision.bRecenter = true;
	}
	return Decision;
}
