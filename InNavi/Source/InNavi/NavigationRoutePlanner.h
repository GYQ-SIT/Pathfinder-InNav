#pragma once

#include "CoreMinimal.h"
#include "InteriorTypes.h"

class AInteriorGenerator;
class APolyBuilding;
class ABuildingGenerator;
namespace LayoutValidation { struct FReport; }

// Positions are in the interior's unscaled footprint coordinates, not screen coordinates.
struct FNavigationRoutePoint
{
	FVector2D Position = FVector2D::ZeroVector;
	int32 Floor = 1;
	FString ConnectorId;
	EInteriorNavNodeType ConnectorType = EInteriorNavNodeType::PathNode;
	// Optional exact position from a 3D planner. Position remains the corresponding 2D footprint
	// coordinate so the existing map and directions continue to use the same route.
	FVector WorldPosition = FVector::ZeroVector;
	bool bHasWorldPosition = false;
	// Position is local to this building. Outdoor points retain a coordinate owner too.
	FString BuildingId;
};

struct FNavigationRouteResult
{
	TArray<FNavigationRoutePoint> Points;
	FString Error;
};

// A place the route has to pass on the way from the Start to the Destination, in any building or outside. Its Point carries the
// building and the world position, as the Start and Destination do.
struct FNavigationStop
{
	FNavigationRoutePoint Point;
	FString BuildingId;
	FString RoomOptionLabel;
	FString RoomId;
};

// The stretch of a route that is being shown when it is gone through a step at a time: the route points from FirstPointIndex to
// LastPointIndex (the same point when the step is only a place). The rest of the route is drawn faint.
struct FNavigationRouteFocus
{
	int32 FirstPointIndex = INDEX_NONE;
	int32 LastPointIndex = INDEX_NONE;
	bool IsSet() const { return FirstPointIndex != INDEX_NONE; }
};

// One section of a journey through stops: the stretch from one waypoint (the Start or a stop) to the next (a stop or the
// Destination). The ranges refer to the one combined list of route points, so the map and the 3D view can draw a single route
// and still tell the sections apart; a leg begins at the point where the one before it ended.
struct FNavigationRouteLeg
{
	int32 FirstPointIndex = INDEX_NONE;
	int32 LastPointIndex = INDEX_NONE;
	FString FromLabel;
	FString ToLabel;
};

struct FNavigationRoutePreferences
{
	bool bAllowLift = true;
	bool bAllowStairs = true;
	bool bAllowEscalator = true;
	bool Allows(EInteriorNavNodeType Type) const
	{
		return Type == EInteriorNavNodeType::Elevator ? bAllowLift : Type == EInteriorNavNodeType::Stairs ? bAllowStairs
			: Type == EInteriorNavNodeType::Escalator ? bAllowEscalator : true;
	}
};

// The route part of layout validation (can a person get from outside to every room?), done in slices so a large campus does not
// freeze the editor: call Step each frame with a little time until it returns true. Findings go to the report as they are made.
class INNAVI_API FNavigationLayoutCheck
{
public:
	FNavigationLayoutCheck(const ABuildingGenerator& Generator, const AInteriorGenerator& Interior, const FString& ScopeBuildingId);
	~FNavigationLayoutCheck();
	// Works for about BudgetSeconds (at least one small step); true once the check is over. The result does not depend on how it was sliced.
	bool Step(double BudgetSeconds, LayoutValidation::FReport& Report);
	// 0 to 1, and in words what the check is doing now.
	float GetProgress() const;
	FString GetPhase() const;

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};

// Finding one route between two points (FNavigationRoutePlanner::FindCampusRoute) in slices, for the same reason: on a large
// campus it takes seconds. Step it each frame with a little time until it returns true, then read the result.
class INNAVI_API FNavigationRouteJob
{
public:
	FNavigationRouteJob(const ABuildingGenerator& Generator, const AInteriorGenerator& Interior, const FNavigationRoutePoint& Start,
		const FNavigationRoutePoint& Destination, const FNavigationRoutePreferences& Preferences = FNavigationRoutePreferences());
	~FNavigationRouteJob();
	// Works for about BudgetSeconds (at least one small step); true once the search is over. The result does not depend on how it was sliced.
	bool Step(double BudgetSeconds);
	// Whether a route was found; the route, or why not (Error), is in GetResult.
	bool Succeeded() const;
	const FNavigationRouteResult& GetResult() const;
	// 0 to 1, and in words what the search is doing now.
	float GetProgress() const;
	FString GetPhase() const;
	// True when the route was found without the outside: a trip within one building that nothing outside could have shortened.
	// Such a search skips the work of the outdoor graph, which is most of the work on a large campus.
	bool WasIndoorOnly() const;

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};

// Routes through the authored floor geometry. It leaves Jiahao's graph storage untouched;
// a future navigation service can replace this planner while keeping the UI contract.
class INNAVI_API FNavigationRoutePlanner
{
public:
	// Uses the same visibility graph as routing, without switching buildings or changing layout data. All at once; see FNavigationLayoutCheck.
	static void ValidateLayoutConnectivity(const ABuildingGenerator& Generator, const AInteriorGenerator& Interior,
		const FString& ScopeBuildingId, LayoutValidation::FReport& Report);
	static bool FindCampusRoute(const ABuildingGenerator& Generator, const AInteriorGenerator& Interior,
		const FNavigationRoutePoint& Start, const FNavigationRoutePoint& Destination, FNavigationRouteResult& OutResult,
		const FNavigationRoutePreferences& Preferences = FNavigationRoutePreferences());
	// Find an interior destination with actual planner clearance, including for concave rooms.
	static bool FindRoomDestination(const ABuildingGenerator& Generator, const AInteriorGenerator& Interior,
		const FString& BuildingId, int32 Floor, const TArray<FVector2D>& Polygon, FVector2D& OutPosition);
	static bool IsValidPoint(const APolyBuilding& Building, const AInteriorGenerator& Interior,
		float Scale, int32 Floor, FVector2D Point, FString& OutError);

	static bool FindRoute(const APolyBuilding& Building, const AInteriorGenerator& Interior,
		float Scale, int32 StartFloor, FVector2D Start, int32 DestinationFloor,
		FVector2D Destination, FNavigationRouteResult& OutResult);
};
