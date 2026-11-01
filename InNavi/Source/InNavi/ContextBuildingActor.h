// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BuildingTypes.h"
#include "GameFramework/Actor.h"
#include "ContextBuildingActor.generated.h"

class UMaterialInstanceDynamic;
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
	// closes it itself). Extrudes a flat-roofed block from Z=0 to Z=HeightWorldUnits, in the colour of
	// BuildingType (one value from BuildingTypes::All(); anything else becomes Other).
	// No-op (empty mesh) if Footprint has fewer than 3 points.
	void BuildFromFootprint(const TArray<FVector2D>& Footprint, float HeightWorldUnits, const FString& InBuildingType);

	const FString& GetBuildingType() const { return BuildingType; }
	// The colour the block is drawn in, for tests.
	FLinearColor GetBodyColorForTests() const { return CurrentBodyColor; }

private:
	// Paints the block in the colour of its type. The one place a type becomes how the block looks.
	void ApplyBodyColor();

	UPROPERTY()
	UProceduralMeshComponent* MeshComp;

	UPROPERTY()
	UMaterialInstanceDynamic* DynMaterial = nullptr;

	FString BuildingType = BuildingTypes::Default();
	FLinearColor CurrentBodyColor = FLinearColor::White;
};
