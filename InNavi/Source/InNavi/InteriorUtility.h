#pragma once

#include "CoreMinimal.h"

// Utility points (PAT-193): things people look for that are not rooms - a defibrillator, a bicycle rack, a drop-off point. A point on one floor with
// a type and a name; no 3D model, and routes do not go through it.
namespace InteriorUtilityTypes
{
	inline const FString Aed = TEXT("aed");
	inline const FString Bicycle = TEXT("bicycle");
	inline const FString DropOff = TEXT("dropoff");
	inline const FString Other = TEXT("other");

	inline TArray<FString> All() { return { Aed, Bicycle, DropOff, Other }; }
	// An unknown type (a newer or hand-edited file) reads as "other" rather than being lost.
	inline FString Normalize(const FString& Type)
	{
		const FString Lower = Type.TrimStartAndEnd().ToLower();
		for (const FString& Known : All()) if (Known == Lower) return Known;
		return Other;
	}
	inline FString Label(const FString& Type)
	{
		if (Type == Aed) return TEXT("AED");
		if (Type == Bicycle) return TEXT("Bicycle rack");
		if (Type == DropOff) return TEXT("Drop-off point");
		return TEXT("Other");
	}
	// The one character drawn in the marker on the plan.
	inline FString Letter(const FString& Type)
	{
		if (Type == Aed) return TEXT("A");
		if (Type == Bicycle) return TEXT("B");
		if (Type == DropOff) return TEXT("D");
		return TEXT("?");
	}
	constexpr int32 MaxNameLength = 60;
}

struct FInteriorUtility
{
	FString Id;
	FString Type = InteriorUtilityTypes::Other;
	FString Name;
	int32 Level = 1;
	FVector2D Position = FVector2D::ZeroVector;
};
