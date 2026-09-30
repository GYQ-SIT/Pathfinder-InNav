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

// Placeholder 3D geometry for one road read from ABuildingGenerator::LoadRoadsAndPOIsFromDatabase -
// a flat ribbon strip following the road's own ordered points, same "world-space vertices baked
// directly into the mesh, spawned at the origin" convention as AInteriorConnectorMarker/AInteriorRoom.
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

private:
	UPROPERTY()
	UProceduralMeshComponent* MeshComp;

	ERoadCategory Category = ERoadCategory::Other;
};
