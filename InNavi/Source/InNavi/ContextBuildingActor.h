// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildingTypes.h"
#include "ContextBuildingActor.generated.h"

class UProceduralMeshComponent;

// Placeholder 3D geometry for one non-campus building read from
// ABuildingGenerator::LoadContextBuildingsFromDatabase - a flat-roofed extruded block over the
// building's own footprint, not real art, same "占位" (placeholder) scope as ARoadSegmentActor/
// APOIMarkerActor. Visual/context only - no floors, no interior data, unlike APolyBuilding (which
// stays reserved for the real, university-tagged campus buildings).
UCLASS()
class INNAVI_API AContextBuildingActor : public AActor
{
	GENERATED_BODY()

public:
	AContextBuildingActor();

	// Footprint is already-projected world-space (X,Y) points, in order (an open polygon - this
	// closes it itself). Extrudes a flat-roofed block from Z=0 to Z=HeightWorldUnits, tinted Color.
	// No-op (empty mesh) if Footprint has fewer than 3 points.
	void BuildFromFootprint(const TArray<FVector2D>& Footprint, float HeightWorldUnits, const FLinearColor& Color);
	// Sets the normalized database type used by the map filter.
	void SetBuildingType(const FString& Type) { BuildingType = BuildingTypes::Normalize(Type); }
	// Returns the context building's normalized type.
	const FString& GetBuildingType() const { return BuildingType; }
	// Sets context visibility while remembering the filter result for tests.
	void SetContextVisible(bool bVisible);
	// Returns the context filter result for automation tests.
	bool IsContextVisibleForTests() const { return bContextVisible; }

private:
	UPROPERTY()
	UProceduralMeshComponent* MeshComp;
	FString BuildingType = BuildingTypes::Default();
	bool bContextVisible = true;
};
