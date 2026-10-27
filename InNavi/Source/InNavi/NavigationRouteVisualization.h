#pragma once

#include "CoreMinimal.h"
#include "Components/LineBatchComponent.h"
#include "NavigationPanelWidget.h"
#include "NavigationRoutePlanner.h"
#include "NavigationRouteVisualization.generated.h"

class UTextRenderComponent;

struct FNavigationDisplaySegment
{
	FVector From;
	FVector To;
	bool bTransition = false;
	int32 RouteLegIndex = INDEX_NONE;
};

struct FNavigationDisplayLabel
{
	FVector Position;
	FString Text;
	FColor Color = FColor::White;
};

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
		const TArray<FNavigationStop>* Stops = nullptr, const TArray<FNavigationRouteLeg>* RouteLegs = nullptr);
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
