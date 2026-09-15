// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PolyBuilding.generated.h"

class UProceduralMeshComponent;

UCLASS()
class INNAVI_API APolyBuilding : public AActor
{
	GENERATED_BODY()

public:
	APolyBuilding();

	// Build the 3D mesh by extruding a 2D polygon footprint up to a height
	void BuildFromFootprint(const TArray<FVector2D>& Footprint, float Height);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	UProceduralMeshComponent* MeshComp;
};