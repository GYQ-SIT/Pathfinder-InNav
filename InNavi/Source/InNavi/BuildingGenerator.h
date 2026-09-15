// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildingGenerator.generated.h"

UCLASS()
class INNAVI_API ABuildingGenerator : public AActor
{
	GENERATED_BODY()

public:
	ABuildingGenerator();

protected:
	virtual void BeginPlay() override;

private:
	// Reads buildings.json and generates one Building per entry
	void GenerateBuildings();
};