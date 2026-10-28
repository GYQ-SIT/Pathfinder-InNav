#pragma once

#include "CoreMinimal.h"
#include "Components/LineBatchComponent.h"
#include "NavigationRoutePlanner.h"
#include "NavigationRouteVisualization.generated.h"

class UTextRenderComponent;

struct FNavigationDisplaySegment
{
	FVector From;
	FVector To;
	bool bTransition = false;
	// The leg of the journey this belongs to (see FNavigationRouteLeg), INDEX_NONE when the route has no stops.
	int32 RouteLegIndex = INDEX_NONE;
	// Not the step being shown (see FNavigationRouteFocus): drawn faint.
	bool bDimmed = false;
};

struct FNavigationDisplayLabel
{
	FVector Position;
	FString Text;
	FColor Color = FColor::White;
};

// A stop of the route, numbered from 1 in the order it is visited.
struct FNavigationDisplayStop
{
	FVector Position;
	int32 Number = 0;
	FColor Color = FColor::White;
};

struct FNavigation3DView
{
	TArray<FNavigationDisplaySegment> Segments;
	TArray<FNavigationDisplayLabel> Labels;
	TArray<FNavigationDisplayStop> Stops;
	TOptional<FVector> Start;
	TOptional<FVector> Destination;
	// Where the person is, at the step being shown.
	TOptional<FVector> Here;
};

// Arrow geometry in local map pixels, so zooming does not make direction cues unreadable.
struct FNavigationDirectionArrow2D
{
	FVector2D Tip;
	FVector2D HeadLeft;
	FVector2D HeadRight;
};

// A route owns its drawing buffers: clearing/redrawing it never flushes editor drawing guides.
UCLASS()
class INNAVI_API UNavigationRouteVisualization : public ULineBatchComponent
{
	GENERATED_BODY()
public:
	UNavigationRouteVisualization();
	static FNavigation3DView BuildView(const TArray<FNavigationRoutePoint>& Route,
		const FNavigationRoutePoint* Start, const FNavigationRoutePoint* Destination,
		FVector2D BuildingOffset, float Scale, float FloorHeight, int32 ActiveFloor, bool bGuidanceVisible = true,
		const FString& ActiveBuildingId = FString(), const TArray<FNavigationStop>* Stops = nullptr,
		const TArray<FNavigationRouteLeg>* RouteLegs = nullptr, const FNavigationRouteFocus* Focus = nullptr);
	static bool IsPointVisible(const FNavigationRoutePoint& Point, const FString& ActiveBuildingId, int32 ActiveFloor);
	// The route as the exterior screen draws it: the parts outside, a label where it enters or leaves a building, and the Start and
	// Destination markers (also when they are indoors, then at their spot inside the building). Points carry their world positions.
	static FNavigation3DView BuildCampusView(const TArray<FNavigationRoutePoint>& Route,
		const FNavigationRoutePoint* Start, const FNavigationRoutePoint* Destination, bool bGuidanceVisible = true,
		const TArray<FNavigationStop>* Stops = nullptr, const TArray<FNavigationRouteLeg>* RouteLegs = nullptr,
		const FNavigationRouteFocus* Focus = nullptr);
	// What colour the legs of a journey are drawn in, one after the other (they begin again after the last colour). The 3D view
	// takes the first, the map the second: the same colours, as each of them wants them.
	static FColor LegColor(int32 LegIndex);
	static FLinearColor LegColor2D(int32 LegIndex);
	static TArray<FNavigationDirectionArrow2D> BuildDirectionArrows2D(FVector2D From, FVector2D To);
	void ShowView(const FNavigation3DView& View);
	void ClearRoute();

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> RouteLabels;
};
