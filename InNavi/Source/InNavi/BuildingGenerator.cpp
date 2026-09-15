// Fill out your copyright notice in the Description page of Project Settings.


#include "BuildingGenerator.h"
#include "Building.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

ABuildingGenerator::ABuildingGenerator()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ABuildingGenerator::BeginPlay()
{
	Super::BeginPlay();

	GenerateBuildings();
}

void ABuildingGenerator::GenerateBuildings()
{
	// Path: <ProjectDir>/Data/buildings.json
	FString FilePath = FPaths::ProjectDir() / TEXT("Data") / TEXT("buildings.json");

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not read JSON file at: %s"), *FilePath);
		return;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to parse JSON"));
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* BuildingsArray;
	if (!RootObject->TryGetArrayField(TEXT("buildings"), BuildingsArray))
	{
		UE_LOG(LogTemp, Error, TEXT("No 'buildings' array found in JSON"));
		return;
	}

	if (!GetWorld())
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& Value : *BuildingsArray)
	{
		const TSharedPtr<FJsonObject> B = Value->AsObject();
		if (!B.IsValid())
		{
			continue;
		}

		float OriginX = B->GetNumberField(TEXT("originX"));
		float OriginY = B->GetNumberField(TEXT("originY"));
		float Width = B->GetNumberField(TEXT("width"));
		float Depth = B->GetNumberField(TEXT("depth"));
		int32 FloorCount = B->GetIntegerField(TEXT("floorCount"));
		float FloorH = B->GetNumberField(TEXT("floorHeight"));
		FString Name = B->GetStringField(TEXT("name"));

		float Height = FloorCount * FloorH;
		FVector Location(OriginX + Width * 0.5f, OriginY + Depth * 0.5f, Height * 0.5f);

		ABuilding* NewBuilding = GetWorld()->SpawnActor<ABuilding>(Location, FRotator::ZeroRotator);
		if (NewBuilding)
		{
			NewBuilding->InitBuilding(Name, FVector(Width, Depth, Height));
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("Generated %d buildings from JSON"), BuildingsArray->Num());
}