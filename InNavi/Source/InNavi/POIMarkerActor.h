// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "POIMarkerActor.generated.h"

class UStaticMeshComponent;

// Placeholder 3D marker for one point of interest read from
// ABuildingGenerator::LoadRoadsAndPOIsFromDatabase - a small tinted sphere, not real art, per the
// same "占位" (placeholder) scope as AInteriorConnectorMarker's own shapes.
UCLASS()
class INNAVI_API APOIMarkerActor : public AActor
{
	GENERATED_BODY()

public:
	APOIMarkerActor();

	// WorldLocation is the already-projected world-space (X,Y) position, at ground level (Z=0) -
	// the marker sphere is placed sitting on top of it (its own Radius above), not centered on it.
	void BuildMarker(const FVector2D& WorldLocation, float Radius, const FLinearColor& Color);

	// The campus-range filter's only visibility input for a POI (unlike roads, POIs have no separate
	// category filter to combine with) - ABuildingGenerator::SetRoadsAndPOIsWithinCampusOnly calls this
	// directly off the actor's own GetActorLocation(), which BuildMarker already set to its real
	// world-space position.
	void SetWithinCampusRange(bool bVisible) { SetActorHiddenInGame(!bVisible); }

private:
	UPROPERTY()
	UStaticMeshComponent* MeshComp;
};
