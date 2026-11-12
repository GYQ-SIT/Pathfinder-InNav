// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoadSegmentActor.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

// How ABuildingGenerator::LoadRoadsAndPOIsFromDatabase buckets OSM's much finer-grained highway_type
// tags for display and filtering - see GetRoadCategory (BuildingGenerator.cpp) for the exact mapping,
// and ABuildingGenerator::SetRoadCategoryVisible for toggling a whole category on/off at once. Lives
// here (not BuildingGenerator.h) since ARoadSegmentActor is the thing that actually remembers its own
// category; BuildingGenerator.h includes this file for it.
UENUM(BlueprintType)
enum class ERoadCategory : uint8
{
	Primary,
	Secondary,
	Residential,
	Cycleway,
	Footway,
	Other
};

// Flat geometry for one road read from ABuildingGenerator::LoadRoadsAndPOIsFromDatabase -
// a ribbon with no thickness following the road's own ordered points (bends rounded, no shadow), same
// "world-space vertices baked directly into the mesh, spawned at the origin" convention as
// AInteriorConnectorMarker/AInteriorRoom.
UCLASS()
class INNAVI_API ARoadSegmentActor : public AActor
{
	GENERATED_BODY()

public:
	ARoadSegmentActor();

	// Points are already-projected world-space (X,Y) positions, in order along the road. Builds a
	// flat ribbon Width wide (world units) at height WorldZ, tinted Color. No-op (empty mesh) if
	// Points has fewer than 2 entries.
	void BuildRoad(const TArray<FVector2D>& Points, float Width, float WorldZ, const FLinearColor& Color);

	// Set once, right after BuildRoad, by whatever spawned this actor - SetRoadCategoryVisible reads
	// it back to decide which actors a filter checkbox affects.
	void SetCategory(ERoadCategory InCategory) { Category = InCategory; }
	ERoadCategory GetCategory() const { return Category; }

	// Average of the points BuildRoad was given - a stand-in "location" for distance-based filtering,
	// since the actor's own transform stays at the origin (its vertices are baked world-space, see
	// BuildRoad's own comment) and GetActorLocation() would just be (0,0,0).
	FVector2D GetMidpointPosition() const { return MidpointPosition; }

	// Whether the filters (category and campus range) show this road, and the box of the ground it covers (world X, Y) - what the
	// campus ground is sized from (see ABuildingGenerator::UpdateGroundToVisible). Not the same as being hidden in game, which other
	// things can also cause.
	bool IsShownByFilters() const { return bCategoryVisible && bWithinCampusRange; }
	const FBox2D& GetBounds2D() const { return Bounds2D; }

	// Two independent visibility inputs, combined by RefreshVisibility - lets the category filter
	// (ABuildingGenerator::SetRoadCategoryVisible) and the campus-range filter
	// (ABuildingGenerator::SetRoadsAndPOIsWithinCampusOnly) coexist without either one clobbering
	// what the other decided (e.g. re-enabling a category shouldn't reveal a road that's still
	// outside the current campus range).
	void SetCategoryVisible(bool bVisible) { bCategoryVisible = bVisible; RefreshVisibility(); }
	void SetWithinCampusRange(bool bVisible) { bWithinCampusRange = bVisible; RefreshVisibility(); }

private:
	void RefreshVisibility() { SetActorHiddenInGame(!(bCategoryVisible && bWithinCampusRange)); }

	UPROPERTY()
	UProceduralMeshComponent* MeshComp;

	ERoadCategory Category = ERoadCategory::Other;
	FVector2D MidpointPosition = FVector2D::ZeroVector;
	FBox2D Bounds2D = FBox2D(ForceInit);
	bool bCategoryVisible = true;
	bool bWithinCampusRange = true;
};
