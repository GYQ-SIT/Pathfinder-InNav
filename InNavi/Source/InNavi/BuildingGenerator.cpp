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

	float Scale = 1.f;
	RootObject->TryGetNumberField(TEXT("scale"), Scale);

	float GlobalFloorHeight = 300.f;
	RootObject->TryGetNumberField(TEXT("floorHeight"), GlobalFloorHeight);

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

		FString Id   = B->GetStringField(TEXT("id"));
		FString Name = B->GetStringField(TEXT("name"));

		// Each building has its own floorCount and floorHeight (falls back to global)
		int32 FloorCount = 10;
		B->TryGetNumberField(TEXT("floorCount"), FloorCount);

		float FloorHeight = GlobalFloorHeight;   // default to global
		B->TryGetNumberField(TEXT("floorHeight"), FloorHeight);   // override if building has its own

		float Height = FloorCount * FloorHeight;

		FVector2D Offset(0.f, 0.f);
		const TArray<TSharedPtr<FJsonValue>>* OffsetArray;
		if (B->TryGetArrayField(TEXT("offset"), OffsetArray) && OffsetArray->Num() >= 2)
		{
			Offset.X = (*OffsetArray)[0]->AsNumber();
			Offset.Y = (*OffsetArray)[1]->AsNumber();
		}

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

		APolyBuilding* NewBuilding = GetWorld()->SpawnActor<APolyBuilding>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (NewBuilding)
		{
			NewBuilding->BuildingId = Id;
			NewBuilding->BuildingName = Name;
			NewBuilding->BuildFromFootprint(Footprint, Height, Scale, Offset);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("Generated %d buildings from JSON"), BuildingsArray->Num());
}