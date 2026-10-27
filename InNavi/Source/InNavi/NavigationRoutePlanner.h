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

// Routes through the authored floor geometry. It leaves Jiahao's graph storage untouched;
// a future navigation service can replace this planner while keeping the UI contract.
class INNAVI_API FNavigationRoutePlanner
{
public:
	// Uses the same visibility graph as routing, without switching buildings or changing layout data.
	static void ValidateLayoutConnectivity(const ABuildingGenerator& Generator, const AInteriorGenerator& Interior,
		const FString& ScopeBuildingId, LayoutValidation::FReport& Report);
	static bool FindCampusRoute(const ABuildingGenerator& Generator, const AInteriorGenerator& Interior,
		const FNavigationRoutePoint& Start, const FNavigationRoutePoint& Destination, FNavigationRouteResult& OutResult);
	static bool IsValidPoint(const APolyBuilding& Building, const AInteriorGenerator& Interior,
		float Scale, int32 Floor, FVector2D Point, FString& OutError);

	static bool FindRoute(const APolyBuilding& Building, const AInteriorGenerator& Interior,
		float Scale, int32 StartFloor, FVector2D Start, int32 DestinationFloor,
		FVector2D Destination, FNavigationRouteResult& OutResult);
};
