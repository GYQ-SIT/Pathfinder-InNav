#pragma once

#include "CoreMinimal.h"

// What kind of room a room is: one value from a fixed list, unlike its free-form search tags. Stored in a project
// file as "roomType" (left out when it is Other, so older files load unchanged).
namespace RoomTypes
{
	inline const FString& Default()
	{
		static const FString Value(TEXT("Other"));
		return Value;
	}

	// The list in the order the Properties drop-down shows it; Other is last.
	inline const TArray<FString>& All()
	{
		static const TArray<FString> Values = {
			TEXT("Classroom"), TEXT("Lecture hall"), TEXT("Lab"), TEXT("Office"), TEXT("Study area"), TEXT("Meeting room"),
			TEXT("Canteen"), TEXT("Toilet"), TEXT("Lift lobby"), TEXT("Corridor"), TEXT("Storage"), TEXT("Other") };
		return Values;
	}

	// One colour per type, in the same order as All(), for the tag and the drop-down. Other is a quiet grey-blue.
	inline FLinearColor Color(const FString& Type)
	{
		static const TArray<FColor> Colors = {
			FColor(56, 189, 248), FColor(129, 140, 248), FColor(192, 132, 252), FColor(251, 191, 36), FColor(45, 212, 191), FColor(251, 146, 60),
			FColor(248, 113, 113), FColor(244, 114, 182), FColor(163, 230, 53), FColor(168, 162, 158), FColor(214, 166, 120), FColor(100, 116, 139) };
		const int32 Index = All().IndexOfByPredicate([&Type](const FString& Known) { return Known.Equals(Type, ESearchCase::IgnoreCase); });
		return FLinearColor::FromSRGBColor(Colors[Index == INDEX_NONE ? Colors.Num() - 1 : Index]);
	}

	// The list's own spelling of a type, matched ignoring case and surrounding spaces; anything not on the list becomes Other.
	inline FString Normalize(const FString& Value)
	{
		const FString Trimmed = Value.TrimStartAndEnd();
		for (const FString& Known : All())
		{
			if (Known.Equals(Trimmed, ESearchCase::IgnoreCase))
			{
				return Known;
			}
		}
		return Default();
	}
}
