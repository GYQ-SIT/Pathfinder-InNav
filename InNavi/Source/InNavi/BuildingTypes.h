#pragma once

#include "CoreMinimal.h"

// What kind of building a building is: one value from a fixed list, like a room's type (RoomTypes.h). Stored in a project
// file as "buildingType" (left out when it is Other, so older files load unchanged). The same list colours and filters the
// buildings of the map database.
//
// Colour is how a type is shown for now. APolyBuilding and AContextBuildingActor each read it in one place (ApplyBodyColor),
// so showing a type with a material later is a change in those two functions and this file, nowhere else. Nothing else knows
// about colours: the panels, the filter, the project file and the tests only know the type.
//
// To show a type with a real material instead of a colour:
//  1. Add the material per type here, next to Color(): an optional asset path for each type in All(). A type without one keeps
//     its colour, so types can be changed over one at a time.
//  2. Give the meshes UV coordinates. They have none now (the UV arrays given to CalculateTangentsForMesh in
//     APolyBuilding::RebuildMesh, BuildCapSection and BuildWallSection, and in AContextBuildingActor::BuildFromFootprint, are
//     empty), and a textured material needs them on the walls, the floor slabs and the roof. This is the largest part.
//  3. Set a material per mesh section, not one for the whole building: APolyBuilding gives every section (ground, slabs and roof,
//     wall rings) the same DynMaterial now, and walls and roof will want different ones.
//  4. Make the highlight a parameter of the material. SetHighlighted makes the building yellow by writing the "Color" parameter
//     (see ApplyBodyColor); a material needs a parameter of its own for that, or the selected building loses its highlight.
//  5. Change the tests that read the colour (GetBodyColorForTests, in BuildingTypeTests.cpp) to check the material.
namespace BuildingTypes
{
	inline const FString& Default()
	{
		static const FString Value(TEXT("Other"));
		return Value;
	}

	// The list in the order the Building panel's drop-down and the Map panel's filter show it; Other is last.
	inline const TArray<FString>& All()
	{
		static const TArray<FString> Values = {
			TEXT("University"), TEXT("School"), TEXT("Residential"), TEXT("Commercial"),
			TEXT("Public"), TEXT("Transport"), TEXT("Utility"), TEXT("Other") };
		return Values;
	}

	// One colour per type, in the same order as All(). Other is the quiet grey the buildings had before types existed.
	inline FLinearColor Color(const FString& Type)
	{
		static const TArray<FColor> Colors = {
			FColor(59, 130, 246), FColor(34, 197, 94), FColor(214, 166, 120), FColor(251, 146, 60),
			FColor(192, 132, 252), FColor(45, 212, 191), FColor(251, 191, 36), FColor(153, 153, 153) };
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

	// The type for a building_type value of the map database (OpenStreetMap tags). A value that is not listed here is Other.
	inline FString FromMapDatabase(const FString& MapType)
	{
		struct FMapping
		{
			const TCHAR* Source;
			const TCHAR* Type;
		};
		static const FMapping Mappings[] = {
			{ TEXT("university"), TEXT("University") },
			{ TEXT("school"), TEXT("School") },
			{ TEXT("kindergarten"), TEXT("School") },
			{ TEXT("education"), TEXT("School") },
			{ TEXT("residential"), TEXT("Residential") },
			{ TEXT("house"), TEXT("Residential") },
			{ TEXT("detached"), TEXT("Residential") },
			{ TEXT("commercial"), TEXT("Commercial") },
			{ TEXT("retail"), TEXT("Commercial") },
			{ TEXT("public"), TEXT("Public") },
			{ TEXT("church"), TEXT("Public") },
			{ TEXT("train_station"), TEXT("Transport") },
			{ TEXT("parking"), TEXT("Transport") },
			{ TEXT("bridge"), TEXT("Transport") },
			{ TEXT("transportation"), TEXT("Transport") },
			{ TEXT("utility"), TEXT("Utility") } };
		const FString Trimmed = MapType.TrimStartAndEnd();
		for (const FMapping& Mapping : Mappings)
		{
			if (Trimmed.Equals(Mapping.Source, ESearchCase::IgnoreCase))
			{
				return Mapping.Type;
			}
		}
		return Default();
	}
}
