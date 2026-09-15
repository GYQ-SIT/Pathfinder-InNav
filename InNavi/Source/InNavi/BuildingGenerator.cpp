// Fill out your copyright notice in the Description page of Project Settings.


#include "BuildingGenerator.h"
#include "PolyBuilding.h"
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

		FString Id     = B->GetStringField(TEXT("id"));
		FString Name   = B->GetStringField(TEXT("name"));
		float   Height = B->GetNumberField(TEXT("height"));

		// Read the footprint vertex array
		const TArray<TSharedPtr<FJsonValue>>* FootprintArray;
		if (!B->TryGetArrayField(TEXT("footprint"), FootprintArray))
		{
			UE_LOG(LogTemp, Warning, TEXT("Building %s has no footprint"), *Id);
			continue;
		}

		TArray<FVector2D> Footprint;
		for (const TSharedPtr<FJsonValue>& PointValue : *FootprintArray)
		{
			const TArray<TSharedPtr<FJsonValue>>* Pair;
			if (PointValue->TryGetArray(Pair) && Pair->Num() >= 2)
			{
				float X = (*Pair)[0]->AsNumber();
				float Y = (*Pair)[1]->AsNumber();
				Footprint.Add(FVector2D(X, Y));
			}
		}

		// Spawn a PolyBuilding and build its mesh
		APolyBuilding* NewBuilding = GetWorld()->SpawnActor<APolyBuilding>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (NewBuilding)
		{
			NewBuilding->BuildingId = Id;
			NewBuilding->BuildingName = Name;
			NewBuilding->BuildFromFootprint(Footprint, Height);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("Generated %d buildings from JSON"), BuildingsArray->Num());
}