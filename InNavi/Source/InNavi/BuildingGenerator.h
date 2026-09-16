// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildingGenerator.generated.h"

class APolyBuilding;

UCLASS()
class INNAVI_API ABuildingGenerator : public AActor
{
	GENERATED_BODY()

public:
	ABuildingGenerator();

	// Spawns one building directly from already-parsed data. Used both by JSON loading
	// (GenerateBuildings) and by runtime building creation (e.g. NavPlayerController's build mode).
	// Returns nullptr if Id is empty, already used, or Footprint has fewer than 3 points.
	APolyBuilding* SpawnBuildingFromData(const FString& Id, const FString& Name, const TArray<FVector2D>& Footprint, int32 FloorCount, float FloorHeight, FVector2D Offset);

	// SpawnBuildingFromData multiplies footprint+offset by this. Callers building a footprint
	// out of world-space coordinates (e.g. from a raycast) must divide by this first.
	float GetScale() const { return Scale; }

	// Plan B: builds an axis-aligned-then-rotated rectangle footprint and spawns it.
	// Position/Width/Depth are world-space units (same units you'd read off in-level), matching
	// how Plan A's click points work. This is the function Padre's UI form should call directly -
	// it does not go through JSON.
	UFUNCTION(BlueprintCallable, Category = "Building")
	APolyBuilding* CreateRectangleBuilding(const FString& Id, const FString& Name, FVector2D Position, float Width, float Depth, float RotationDegrees, int32 FloorCount, float FloorHeight);

	// Plan B: builds a regular N-sided polygon footprint (Sides=3 triangle, 5 pentagon, 6 hexagon,
	// ...) and spawns it. Position/Radius are world-space units, same convention as
	// CreateRectangleBuilding. Radius is the distance from the center to each vertex.
	UFUNCTION(BlueprintCallable, Category = "Building")
	APolyBuilding* CreateRegularPolygonBuilding(const FString& Id, const FString& Name, FVector2D Position, int32 Sides, float Radius, float RotationDegrees, int32 FloorCount, float FloorHeight);

protected:
	virtual void BeginPlay() override;

private:
	void GenerateBuildings();

	float Scale = 1.f;
	TSet<FString> UsedBuildingIds;
};