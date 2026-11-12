// Fill out your copyright notice in the Description page of Project Settings.

#include "UiSize.h"

const TArray<int32>& UiSize::Steps()
{
	static const TArray<int32> Result = { 80, 90, 100, 110, 125, 150, 175, 200 };
	return Result;
}

int32 UiSize::Clamp(int32 Percent)
{
	return FMath::Clamp(Percent, MinPercent, MaxPercent);
}

int32 UiSize::Step(int32 Percent, int32 Direction)
{
	const int32 Current = Clamp(Percent);
	if (Direction > 0)
	{
		for (const int32 Candidate : Steps())
		{
			if (Candidate > Current)
			{
				return Candidate;
			}
		}
		return MaxPercent;
	}
	if (Direction < 0)
	{
		const TArray<int32>& All = Steps();
		for (int32 Index = All.Num() - 1; Index >= 0; --Index)
		{
			if (All[Index] < Current)
			{
				return All[Index];
			}
		}
		return MinPercent;
	}
	return Current;
}

float UiSize::ToApplicationScale(int32 Percent)
{
	return static_cast<float>(Clamp(Percent)) / 100.f;
}
