#pragma once

#include "CoreMinimal.h"

// What the phone app's screens may ask for. A screen never reaches into AInteriorGenerator, the route planner or the project files: it asks an
// IMobileNavSource. FMobileMockSource answers with the design team's prototype data; a real source answers from the real code later, and no
// screen changes when it does. See docs/design/mobile-app-data-interface.md.

namespace MobileNav
{
	// How fast a person walks, for turning metres into minutes.
	inline constexpr double WalkingMetresPerSecond = 1.2;
}

struct FMobileSite
{
	FString Id;
	FString Name;
	// "Learning & community spaces", "Campus demo . Schematic maps"
	FString Subtitle;
	int32 BuildingCount = 1;
	int32 LevelCount = 1;
	// "2.4 km", or "Demo"
	FString DistanceLabel;
};

struct FMobileLevel
{
	// The level's number in the building (what the planner knows).
	int32 Level = 1;
	// What the person sees: "L1", "L2M".
	FString Label;
};

struct FMobileBuilding
{
	FString Id;
	FString Name;
	TArray<FMobileLevel> Levels;
};

struct FMobilePlace
{
	FString Id;
	FString Name;
	// What is printed on the door: "E2-01-101", "#01-102".
	FString Code;
	FString SiteId;
	FString BuildingId;
	FString BuildingName;
	int32 Level = 1;
	FString LevelLabel;
	// What the list and the filters show: "Learning", "Facilities", "Dining".
	FString Category;
	// The room's type in the editor (RoomTypes.h), when it has one.
	FString RoomType;
	TArray<FString> Tags;
	// Where it is on its level's map (the building's own footprint units).
	FVector2D Position = FVector2D::ZeroVector;
	bool bAvailable = true;
	// Empty when there is no photo.
	FString PhotoPath;
	FString Description;

	// "E2 . L1": what the list groups places by.
	FString GroupLabel() const;
};

struct FMobilePlaceFilter
{
	// Empty = every category.
	TArray<FString> Categories;
	bool bAvailableOnly = false;
	// Empty / INDEX_NONE = anywhere.
	FString BuildingId;
	int32 Level = INDEX_NONE;
};

// Which ways between levels a person will take; every one is on unless switched off. "Use lift only" is the lift alone.
struct FMobileRoutePreferences
{
	bool bAllowLift = true;
	bool bAllowStairs = true;
	bool bAllowEscalator = true;

	static FMobileRoutePreferences LiftOnly()
	{
		FMobileRoutePreferences Preferences;
		Preferences.bAllowStairs = false;
		Preferences.bAllowEscalator = false;
		return Preferences;
	}
};

struct FMobileRouteRequest
{
	// Empty = "My location".
	FString FromPlaceId;
	FString ToPlaceId;
	FMobileRoutePreferences Preferences;
};

enum class EMobileStepKind : uint8
{
	Straight,
	TurnLeft,
	TurnRight,
	TakeLift,
	TakeStairs,
	TakeEscalator,
	ExitBuilding,
	EnterBuilding,
	Arrive,
};

struct FMobileStep
{
	EMobileStepKind Kind = EMobileStepKind::Straight;
	// "Follow the corridor", "Take the lift to L2", "You have arrived: Study lounge".
	FString Text;
	FString BuildingId;
	int32 Level = 1;
	FString LevelLabel;
	double Metres = 0.0;
	FVector2D Position = FVector2D::ZeroVector;
};

// One point of the route, for drawing it on the map.
struct FMobileRoutePoint
{
	FString BuildingId;
	int32 Level = 1;
	FVector2D Position = FVector2D::ZeroVector;
};

struct FMobileRoute
{
	bool bFound = false;
	// Why there is no route, in words for a person.
	FString Error;
	double Metres = 0.0;
	int32 Minutes = 0;
	TArray<FMobileStep> Steps;
	TArray<FMobileRoutePoint> Points;
};

class INNAVI_API IMobileNavSource
{
public:
	virtual ~IMobileNavSource() = default;

	virtual TArray<FMobileSite> ListSites() = 0;
	virtual TArray<FMobileBuilding> ListBuildings(const FString& SiteId) = 0;
	// The categories places of the site have, for the filters.
	virtual TArray<FString> ListCategories(const FString& SiteId) = 0;
	// Places of the site that match every word of the query (and the filter), in building, level and list order. An empty query is all of them.
	virtual TArray<FMobilePlace> SearchPlaces(const FString& SiteId, const FString& Query, const FMobilePlaceFilter& Filter) = 0;
	// False if there is no such place.
	virtual bool FindPlace(const FString& PlaceId, FMobilePlace& OutPlace) = 0;
	virtual FMobileRoute FindRoute(const FMobileRouteRequest& Request) = 0;
};

namespace MobileNav
{
	// Minutes to walk a distance, at least one.
	INNAVI_API int32 MinutesForMetres(double Metres);
}
