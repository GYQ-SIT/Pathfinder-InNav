#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// A door in a building's outer wall, as opposed to a door in a room's wall. It sits on one edge of the building's outline (edge i =
// Outline[i] to Outline[i + 1]) on one floor. Saved with the building's interior, under "entrances".
//
// There are two kinds, and a door's place decides which it is:
//  - Main: the way out of the building. Ground floor only, and only where no other building's wall is next to this one.
//  - Link: a door to the building next door. Any floor, but only where the two outer walls are next to each other. The other building
//    shows the same door at the matching spot (a "mirrored" entrance, worked out from this one rather than stored twice).
enum class EInteriorEntranceKind : uint8
{
	Main,
	Link
};

struct FInteriorEntrance
{
	FString Id;
	EInteriorEntranceKind Kind = EInteriorEntranceKind::Main;
	int32 Level = 1;
	int32 EdgeIndex = 0;
	// Distance along the edge from its start to the start of the opening, and the opening's width (world units, like FInteriorOpening).
	float Offset = 0.f;
	float Width = 240.f;
	// For a Link, the other building's id.
	FString ConnectsTo;

	// True for the copy of another building's Link door that is shown on this building; it is not stored here. Id is the other
	// building's door id and SourceBuildingId is that building.
	bool bMirrored = false;
	FString SourceBuildingId;
};

namespace InteriorEntrances
{
	inline FInteriorEntrance FromJson(const TSharedPtr<FJsonObject>& Object)
	{
		FInteriorEntrance Entrance;
		Object->TryGetStringField(TEXT("id"), Entrance.Id);
		FString KindText;
		Object->TryGetStringField(TEXT("kind"), KindText);
		Entrance.Kind = KindText.Equals(TEXT("link"), ESearchCase::IgnoreCase) ? EInteriorEntranceKind::Link : EInteriorEntranceKind::Main;
		Object->TryGetNumberField(TEXT("level"), Entrance.Level);
		Object->TryGetNumberField(TEXT("edge"), Entrance.EdgeIndex);
		double Number = 0.0;
		if (Object->TryGetNumberField(TEXT("offset"), Number)) Entrance.Offset = static_cast<float>(Number);
		if (Object->TryGetNumberField(TEXT("width"), Number)) Entrance.Width = static_cast<float>(Number);
		Object->TryGetStringField(TEXT("connectsTo"), Entrance.ConnectsTo);
		return Entrance;
	}

	inline TSharedRef<FJsonObject> ToJson(const FInteriorEntrance& Entrance)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), Entrance.Id);
		Object->SetStringField(TEXT("kind"), Entrance.Kind == EInteriorEntranceKind::Link ? TEXT("link") : TEXT("main"));
		Object->SetNumberField(TEXT("level"), Entrance.Level);
		Object->SetNumberField(TEXT("edge"), Entrance.EdgeIndex);
		Object->SetNumberField(TEXT("offset"), Entrance.Offset);
		Object->SetNumberField(TEXT("width"), Entrance.Width);
		if (!Entrance.ConnectsTo.IsEmpty())
		{
			Object->SetStringField(TEXT("connectsTo"), Entrance.ConnectsTo);
		}
		return Object;
	}

	// The entrances stored in an interior's JSON (empty if it has none).
	inline TArray<FInteriorEntrance> ReadAll(const TSharedPtr<FJsonObject>& Interior)
	{
		TArray<FInteriorEntrance> Result;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Interior.IsValid() && Interior->TryGetArrayField(TEXT("entrances"), Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
				if (Object.IsValid())
				{
					Result.Add(FromJson(Object));
				}
			}
		}
		return Result;
	}
}
