// Fill out your copyright notice in the Description page of Project Settings.


#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "BuildingGenerator.h"
#include "PolyBuilding.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

AInteriorGenerator::AInteriorGenerator()
{
	PrimaryActorTick.bCanEverTick = false;
	Generator = nullptr;
}

void AInteriorGenerator::BeginPlay()
{
	Super::BeginPlay();

	for (TActorIterator<ABuildingGenerator> It(GetWorld()); It; ++It)
	{
		Generator = *It;
		break;
	}
}

static EInteriorOpeningType ParseOpeningType(const FString& TypeString)
{
	if (TypeString == TEXT("window")) { return EInteriorOpeningType::Window; }
	if (TypeString == TEXT("void")) { return EInteriorOpeningType::Void; }
	return EInteriorOpeningType::Door;
}

int32 AInteriorGenerator::LoadInteriorForBuilding(const FString& BuildingId)
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadInteriorForBuilding: no BuildingGenerator found in the level"));
		return -1;
	}

	APolyBuilding* Building = Generator->FindBuildingById(BuildingId);
	if (!Building)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadInteriorForBuilding: no building with id '%s'"), *BuildingId);
		return -1;
	}

	const FString FilePath = FPaths::ProjectDir() / TEXT("Data") / TEXT("Interiors") / (BuildingId + TEXT(".json"));

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("LoadInteriorForBuilding: could not read %s"), *FilePath);
		return -1;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("LoadInteriorForBuilding: failed to parse %s"), *FilePath);
		return -1;
	}

	const TArray<TSharedPtr<FJsonValue>>* FloorsArray;
	if (!RootObject->TryGetArrayField(TEXT("floors"), FloorsArray))
	{
		UE_LOG(LogTemp, Error, TEXT("LoadInteriorForBuilding: no 'floors' array in %s"), *FilePath);
		return -1;
	}

	// Clear whatever this generator had loaded before.
	for (auto& Pair : RoomsByFloor)
	{
		for (AInteriorRoom* Room : Pair.Value.Rooms)
		{
			if (Room)
			{
				Room->Destroy();
			}
		}
	}
	RoomsByFloor.Empty();
	NavNodes.Empty();

	const float Scale = Generator->GetScale();
	const FVector2D BuildingOffset = Building->BuildingOffset;
	const float ExteriorFloorHeight = Building->GetFloorHeight();

	int32 SpawnedCount = 0;

	for (const TSharedPtr<FJsonValue>& FloorValue : *FloorsArray)
	{
		const TSharedPtr<FJsonObject> FloorObject = FloorValue->AsObject();
		if (!FloorObject.IsValid())
		{
			continue;
		}

		int32 Level = 1;
		FloorObject->TryGetNumberField(TEXT("level"), Level);

		float WallHeight = 280.f;
		FloorObject->TryGetNumberField(TEXT("wallHeight"), WallHeight);

		float WallThickness = 20.f;
		FloorObject->TryGetNumberField(TEXT("wallThickness"), WallThickness);

		const float FloorBaseZ = (Level - 1) * ExteriorFloorHeight;

		const TArray<TSharedPtr<FJsonValue>>* RoomsArray;
		if (FloorObject->TryGetArrayField(TEXT("rooms"), RoomsArray))
		{
			for (const TSharedPtr<FJsonValue>& RoomValue : *RoomsArray)
			{
				const TSharedPtr<FJsonObject> RoomObject = RoomValue->AsObject();
				if (!RoomObject.IsValid())
				{
					continue;
				}

				const FString RoomId = RoomObject->GetStringField(TEXT("id"));
				const FString RoomName = RoomObject->GetStringField(TEXT("name"));

				FString AccessString = TEXT("public");
				RoomObject->TryGetStringField(TEXT("access"), AccessString);
				const bool bIsPublic = AccessString != TEXT("private");

				bool bRequiresKey = false;
				RoomObject->TryGetBoolField(TEXT("requireKey"), bRequiresKey);

				const TArray<TSharedPtr<FJsonValue>>* FootprintArray;
				if (!RoomObject->TryGetArrayField(TEXT("footprint"), FootprintArray))
				{
					UE_LOG(LogTemp, Warning, TEXT("Room %s has no footprint"), *RoomId);
					continue;
				}

				TArray<FVector2D> Footprint;
				for (const TSharedPtr<FJsonValue>& PointValue : *FootprintArray)
				{
					const TArray<TSharedPtr<FJsonValue>>* Pair;
					if (PointValue->TryGetArray(Pair) && Pair->Num() >= 2)
					{
						Footprint.Add(FVector2D((*Pair)[0]->AsNumber(), (*Pair)[1]->AsNumber()));
					}
				}

				TArray<FInteriorOpening> Openings;
				const TArray<TSharedPtr<FJsonValue>>* OpeningsArray;
				if (RoomObject->TryGetArrayField(TEXT("openings"), OpeningsArray))
				{
					for (const TSharedPtr<FJsonValue>& OpeningValue : *OpeningsArray)
					{
						const TSharedPtr<FJsonObject> OpeningObject = OpeningValue->AsObject();
						if (!OpeningObject.IsValid())
						{
							continue;
						}

						FInteriorOpening Opening;
						Opening.Id = OpeningObject->GetStringField(TEXT("id"));

						FString TypeString;
						OpeningObject->TryGetStringField(TEXT("type"), TypeString);
						Opening.Type = ParseOpeningType(TypeString);

						OpeningObject->TryGetNumberField(TEXT("edgeIndex"), Opening.EdgeIndex);
						OpeningObject->TryGetNumberField(TEXT("offset"), Opening.Offset);
						OpeningObject->TryGetNumberField(TEXT("width"), Opening.Width);
						OpeningObject->TryGetNumberField(TEXT("height"), Opening.Height);
						OpeningObject->TryGetNumberField(TEXT("sillHeight"), Opening.SillHeight);
						OpeningObject->TryGetStringField(TEXT("connectsTo"), Opening.ConnectsTo);

						Openings.Add(Opening);
					}
				}

				if (!GetWorld())
				{
					continue;
				}

				AInteriorRoom* Room = GetWorld()->SpawnActor<AInteriorRoom>(FVector::ZeroVector, FRotator::ZeroRotator);
				if (Room)
				{
					Room->RoomId = RoomId;
					Room->RoomName = RoomName;
					Room->FloorLevel = Level;
					Room->bIsPublic = bIsPublic;
					Room->bRequiresKey = bRequiresKey;
					Room->BuildWalls(Footprint, Scale, BuildingOffset, FloorBaseZ, WallHeight, WallThickness, Openings);

					RoomsByFloor.FindOrAdd(Level).Rooms.Add(Room);
					SpawnedCount++;
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* NavNodesArray;
		if (FloorObject->TryGetArrayField(TEXT("navNodes"), NavNodesArray))
		{
			for (const TSharedPtr<FJsonValue>& NodeValue : *NavNodesArray)
			{
				const TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();
				if (!NodeObject.IsValid())
				{
					continue;
				}

				FInteriorNavNode Node;
				Node.Id = NodeObject->GetStringField(TEXT("id"));
				NodeObject->TryGetStringField(TEXT("type"), Node.Type);

				const TArray<TSharedPtr<FJsonValue>>* PositionArray;
				if (NodeObject->TryGetArrayField(TEXT("position"), PositionArray) && PositionArray->Num() >= 2)
				{
					Node.Position = FVector2D((*PositionArray)[0]->AsNumber(), (*PositionArray)[1]->AsNumber());
				}

				const TArray<TSharedPtr<FJsonValue>>* ConnectsFloorsArray;
				if (NodeObject->TryGetArrayField(TEXT("connectsFloors"), ConnectsFloorsArray))
				{
					for (const TSharedPtr<FJsonValue>& FloorNum : *ConnectsFloorsArray)
					{
						Node.ConnectsFloors.Add(static_cast<int32>(FloorNum->AsNumber()));
					}
				}

				NavNodes.Add(Node);
			}
		}
	}

	// Interior floor count is the source of truth for how tall the exterior shell is.
	Building->SetFloorCount(FloorsArray->Num());

	LoadedBuildingId = BuildingId;
	ActiveFloor = 1;
	SetActiveFloor(ActiveFloor);

	UE_LOG(LogTemp, Warning, TEXT("Loaded interior for %s: %d floors, %d rooms"), *BuildingId, FloorsArray->Num(), SpawnedCount);
	return SpawnedCount;
}

void AInteriorGenerator::SetActiveFloor(int32 Level)
{
	ActiveFloor = Level;

	for (auto& Pair : RoomsByFloor)
	{
		const bool bVisible = (Pair.Key == Level);
		for (AInteriorRoom* Room : Pair.Value.Rooms)
		{
			if (Room)
			{
				Room->SetActorHiddenInGame(!bVisible);
				Room->SetActorEnableCollision(bVisible);
			}
		}
	}
}
