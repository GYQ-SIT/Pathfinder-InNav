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

protected:
	virtual void BeginPlay() override;

private:
	void GenerateBuildings();

	float Scale = 1.f;
	TSet<FString> UsedBuildingIds;
};