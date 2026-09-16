// Fill out your copyright notice in the Description page of Project Settings.


#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "BuildingGenerator.h"
#include "PolyBuilding.h"
#include "EngineUtils.h"
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

static FString OpeningTypeToString(EInteriorOpeningType Type)
{
	switch (Type)
	{
	case EInteriorOpeningType::Window: return TEXT("window");
	case EInteriorOpeningType::Void:   return TEXT("void");
	default:                           return TEXT("door");
	}
}

// ---- Helper: do segments AB and CD properly cross each other? (mirrors BuildingGenerator.cpp) ----
static bool SegmentsIntersect(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
{
	auto Cross = [](const FVector2D& O, const FVector2D& P1, const FVector2D& P2)
	{
		return (P1.X - O.X) * (P2.Y - O.Y) - (P1.Y - O.Y) * (P2.X - O.X);
	};

	const float d1 = Cross(C, D, A);
	const float d2 = Cross(C, D, B);
	const float d3 = Cross(A, B, C);
	const float d4 = Cross(A, B, D);

	return ((d1 > 0.f) != (d2 > 0.f)) && ((d3 > 0.f) != (d4 > 0.f));
}

// ---- Helper: does this polygon's outline cross itself? (non-adjacent edges only) ----
static bool PolygonSelfIntersects(const TArray<FVector2D>& Poly)
{
	const int32 N = Poly.Num();
	for (int32 i = 0; i < N; i++)
	{
		const int32 iNext = (i + 1) % N;
		for (int32 j = i + 1; j < N; j++)
		{
			const int32 jNext = (j + 1) % N;
			if (j == iNext || jNext == i)
			{
				continue;
			}

			if (SegmentsIntersect(Poly[i], Poly[iNext], Poly[j], Poly[jNext]))
			{
				return true;
			}
		}
	}
	return false;
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

	ClearLoadedInterior();

	LoadedBuildingId = BuildingId;

	const TSharedPtr<FJsonObject> InteriorData = Generator->GetInteriorDataForBuilding(BuildingId);
	const TArray<TSharedPtr<FJsonValue>>* FloorsArray = nullptr;
	if (!InteriorData.IsValid() || !InteriorData->TryGetArrayField(TEXT("floors"), FloorsArray))
	{
		// No interior data yet for this building in the current project - not an error, start blank.
		// Pre-fill one empty floor slot per floor the exterior shell already claims to have, so a
		// later AddFloor continues on from there instead of shrinking the shell back down to 1 floor.
		const int32 ExistingFloorCount = FMath::Max(1, Building->GetFloorCount());
		for (int32 Level = 1; Level <= ExistingFloorCount; Level++)
		{
			FloorInfoByLevel.Add(Level, FInteriorFloorInfo());
			RoomsByFloor.Add(Level, FInteriorRoomList());
		}

		ActiveFloor = 1;
		SetActiveFloor(ActiveFloor);

		UE_LOG(LogTemp, Warning, TEXT("No interior data yet for %s - starting blank with %d floor(s) matching the exterior"), *BuildingId, ExistingFloorCount);
		return 0;
	}

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

		FloorInfoByLevel.Add(Level, FInteriorFloorInfo{ WallHeight, WallThickness });

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

				// Absent means "closed" - keeps older saved interiors (no "closed" field at all,
				// from before standalone walls existed) loading as ordinary closed rooms.
				bool bClosedLoop = true;
				RoomObject->TryGetBoolField(TEXT("closed"), bClosedLoop);

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
						OpeningObject->TryGetStringField(TEXT("inputId"), Opening.InputId);
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
					Room->BuildWalls(Footprint, Scale, BuildingOffset, FloorBaseZ, WallHeight, WallThickness, Openings, bClosedLoop);

					RoomsByFloor.FindOrAdd(Level).Rooms.Add(Room);
					UsedRoomIds.Add(RoomId);
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
				Node.Level = Level;

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

	ActiveFloor = 1;
	SetActiveFloor(ActiveFloor);

	UE_LOG(LogTemp, Warning, TEXT("Loaded interior for %s: %d floors, %d rooms"), *BuildingId, FloorsArray->Num(), SpawnedCount);
	return SpawnedCount;
}

void AInteriorGenerator::ClearLoadedInterior()
{
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
	FloorInfoByLevel.Empty();
	UsedRoomIds.Empty();
	LoadedBuildingId.Empty();
	ActiveFloor = 1;
}

void AInteriorGenerator::SetActiveFloor(int32 Level)
{
	if (FloorInfoByLevel.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("SetActiveFloor: no building interior is currently loaded"));
		return;
	}

	int32 MinLevel = TNumericLimits<int32>::Max();
	int32 MaxLevel = TNumericLimits<int32>::Min();
	for (const auto& Pair : FloorInfoByLevel)
	{
		MinLevel = FMath::Min(MinLevel, Pair.Key);
		MaxLevel = FMath::Max(MaxLevel, Pair.Key);
	}

	const int32 ClampedLevel = FMath::Clamp(Level, MinLevel, MaxLevel);
	if (ClampedLevel != Level)
	{
		UE_LOG(LogTemp, Warning, TEXT("SetActiveFloor: %d is outside the loaded range [%d, %d], clamped to %d"), Level, MinLevel, MaxLevel, ClampedLevel);
	}

	ActiveFloor = ClampedLevel;

	for (auto& Pair : RoomsByFloor)
	{
		const bool bVisible = (Pair.Key == ActiveFloor);
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

int32 AInteriorGenerator::AddFloor()
{
	if (LoadedBuildingId.IsEmpty() || !Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("AddFloor: no building interior currently loaded"));
		return -1;
	}

	APolyBuilding* Building = Generator->FindBuildingById(LoadedBuildingId);
	if (!Building)
	{
		UE_LOG(LogTemp, Error, TEXT("AddFloor: building '%s' no longer exists"), *LoadedBuildingId);
		return -1;
	}

	int32 HighestLevel = 0;
	for (const auto& Pair : FloorInfoByLevel)
	{
		HighestLevel = FMath::Max(HighestLevel, Pair.Key);
	}
	const int32 NewLevel = HighestLevel + 1;

	FInteriorFloorInfo NewFloorInfo;
	if (const FInteriorFloorInfo* TopFloor = FloorInfoByLevel.Find(HighestLevel))
	{
		NewFloorInfo = *TopFloor;
	}
	FloorInfoByLevel.Add(NewLevel, NewFloorInfo);
	RoomsByFloor.Add(NewLevel, FInteriorRoomList());

	// New floor count is the source of truth for how tall the exterior shell is.
	Building->SetFloorCount(NewLevel);

	SetActiveFloor(NewLevel);

	UE_LOG(LogTemp, Warning, TEXT("Added floor %d to %s (wallHeight=%.0f, wallThickness=%.0f)"), NewLevel, *LoadedBuildingId, NewFloorInfo.WallHeight, NewFloorInfo.WallThickness);
	return NewLevel;
}

bool AInteriorGenerator::RemoveFloor(int32 Level)
{
	if (!FloorInfoByLevel.Contains(Level))
	{
		UE_LOG(LogTemp, Error, TEXT("RemoveFloor: floor %d doesn't exist"), Level);
		return false;
	}

	if (FloorInfoByLevel.Num() <= 1)
	{
		UE_LOG(LogTemp, Error, TEXT("RemoveFloor: can't remove the last remaining floor"));
		return false;
	}

	if (!Generator)
	{
		return false;
	}

	APolyBuilding* Building = Generator->FindBuildingById(LoadedBuildingId);
	if (!Building)
	{
		UE_LOG(LogTemp, Error, TEXT("RemoveFloor: building '%s' no longer exists"), *LoadedBuildingId);
		return false;
	}

	// Compute the highest floor BEFORE removing anything, so it doesn't matter whether Level
	// itself happens to be the top floor.
	int32 MaxLevel = 0;
	for (const auto& Pair : FloorInfoByLevel)
	{
		MaxLevel = FMath::Max(MaxLevel, Pair.Key);
	}

	// Destroy the removed floor's rooms.
	if (FInteriorRoomList* RemovedRooms = RoomsByFloor.Find(Level))
	{
		for (AInteriorRoom* Room : RemovedRooms->Rooms)
		{
			if (Room)
			{
				Room->Destroy();
			}
		}
	}
	RoomsByFloor.Remove(Level);
	FloorInfoByLevel.Remove(Level);

	const float ExteriorFloorHeight = Building->GetFloorHeight();

	// Shift every floor above the removed one down by one level: renumber it and reposition its
	// rooms' meshes to the new (lower) height.
	for (int32 OldLevel = Level + 1; OldLevel <= MaxLevel; OldLevel++)
	{
		const int32 NewLevelNum = OldLevel - 1;

		if (const FInteriorFloorInfo* Info = FloorInfoByLevel.Find(OldLevel))
		{
			const FInteriorFloorInfo InfoCopy = *Info;
			FloorInfoByLevel.Remove(OldLevel);
			FloorInfoByLevel.Add(NewLevelNum, InfoCopy);
		}

		if (const FInteriorRoomList* MovingRooms = RoomsByFloor.Find(OldLevel))
		{
			FInteriorRoomList RoomsCopy = *MovingRooms;
			RoomsByFloor.Remove(OldLevel);

			const float NewFloorBaseZ = (NewLevelNum - 1) * ExteriorFloorHeight;
			for (AInteriorRoom* Room : RoomsCopy.Rooms)
			{
				if (Room)
				{
					Room->FloorLevel = NewLevelNum;
					Room->SetFloorBaseZ(NewFloorBaseZ);
				}
			}

			RoomsByFloor.Add(NewLevelNum, RoomsCopy);
		}
	}

	const int32 NewFloorCount = MaxLevel - 1;

	// New floor count is the source of truth for how tall the exterior shell is.
	Building->SetFloorCount(NewFloorCount);

	SetActiveFloor(FMath::Clamp(ActiveFloor, 1, NewFloorCount));

	UE_LOG(LogTemp, Warning, TEXT("Removed floor %d from %s, now %d floors"), Level, *LoadedBuildingId, NewFloorCount);
	return true;
}

FString AInteriorGenerator::PeekNextRoomId() const
{
	if (LoadedBuildingId.IsEmpty())
	{
		return FString();
	}

	int32 NextIndex = 1;
	if (const FInteriorRoomList* Rooms = RoomsByFloor.Find(ActiveFloor))
	{
		NextIndex = Rooms->Rooms.Num() + 1;
	}

	return FString::Printf(TEXT("%s-%02d-%02d"), *LoadedBuildingId, ActiveFloor, NextIndex);
}

AInteriorRoom* AInteriorGenerator::CreateRoomFromFootprint(const TArray<FVector2D>& Footprint, const FString& RoomId, const FString& RoomName, bool bIsPublic, bool bRequiresKey)
{
	if (LoadedBuildingId.IsEmpty() || !Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: no building interior currently loaded"));
		return nullptr;
	}

	const FInteriorFloorInfo* FloorInfo = FloorInfoByLevel.Find(ActiveFloor);
	if (!FloorInfo)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: active floor %d doesn't exist"), ActiveFloor);
		return nullptr;
	}

	APolyBuilding* Building = Generator->FindBuildingById(LoadedBuildingId);
	if (!Building)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: building '%s' no longer exists"), *LoadedBuildingId);
		return nullptr;
	}

	if (Footprint.Num() < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: needs at least 3 footprint points (has %d)"), Footprint.Num());
		return nullptr;
	}

	if (PolygonSelfIntersects(Footprint))
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: footprint crosses itself - fix the outline before finishing"));
		return nullptr;
	}

	const FString ResolvedId = RoomId.IsEmpty() ? PeekNextRoomId() : RoomId;
	if (UsedRoomIds.Contains(ResolvedId))
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: room id '%s' is already in use"), *ResolvedId);
		return nullptr;
	}

	if (!GetWorld())
	{
		return nullptr;
	}

	const float Scale = Generator->GetScale();
	const float FloorBaseZ = (ActiveFloor - 1) * Building->GetFloorHeight();

	AInteriorRoom* Room = GetWorld()->SpawnActor<AInteriorRoom>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (Room)
	{
		Room->RoomId = ResolvedId;
		Room->RoomName = RoomName.IsEmpty() ? ResolvedId : RoomName;
		Room->FloorLevel = ActiveFloor;
		Room->bIsPublic = bIsPublic;
		Room->bRequiresKey = bRequiresKey;
		Room->BuildWalls(Footprint, Scale, Building->BuildingOffset, FloorBaseZ, FloorInfo->WallHeight, FloorInfo->WallThickness, TArray<FInteriorOpening>(), /*bClosedLoop=*/true);

		RoomsByFloor.FindOrAdd(ActiveFloor).Rooms.Add(Room);
		UsedRoomIds.Add(ResolvedId);
	}

	return Room;
}

AInteriorRoom* AInteriorGenerator::CreateWallFromPoints(const TArray<FVector2D>& Points, const FString& WallId)
{
	if (LoadedBuildingId.IsEmpty() || !Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateWallFromPoints: no building interior currently loaded"));
		return nullptr;
	}

	const FInteriorFloorInfo* FloorInfo = FloorInfoByLevel.Find(ActiveFloor);
	if (!FloorInfo)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateWallFromPoints: active floor %d doesn't exist"), ActiveFloor);
		return nullptr;
	}

	APolyBuilding* Building = Generator->FindBuildingById(LoadedBuildingId);
	if (!Building)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateWallFromPoints: building '%s' no longer exists"), *LoadedBuildingId);
		return nullptr;
	}

	if (Points.Num() < 2)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateWallFromPoints: needs at least 2 points (has %d)"), Points.Num());
		return nullptr;
	}

	// Shares the room id namespace/uniqueness check and auto-numbering - a wall is just an
	// open-loop room as far as ids/floors are concerned.
	const FString ResolvedId = WallId.IsEmpty() ? PeekNextRoomId() : WallId;
	if (UsedRoomIds.Contains(ResolvedId))
	{
		UE_LOG(LogTemp, Error, TEXT("CreateWallFromPoints: id '%s' is already in use"), *ResolvedId);
		return nullptr;
	}

	if (!GetWorld())
	{
		return nullptr;
	}

	const float Scale = Generator->GetScale();
	const float FloorBaseZ = (ActiveFloor - 1) * Building->GetFloorHeight();

	AInteriorRoom* Wall = GetWorld()->SpawnActor<AInteriorRoom>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (Wall)
	{
		Wall->RoomId = ResolvedId;
		Wall->RoomName = ResolvedId;
		Wall->FloorLevel = ActiveFloor;
		Wall->bIsPublic = true;
		Wall->bRequiresKey = false;
		Wall->BuildWalls(Points, Scale, Building->BuildingOffset, FloorBaseZ, FloorInfo->WallHeight, FloorInfo->WallThickness, TArray<FInteriorOpening>(), /*bClosedLoop=*/false);

		RoomsByFloor.FindOrAdd(ActiveFloor).Rooms.Add(Wall);
		UsedRoomIds.Add(ResolvedId);
	}

	return Wall;
}

bool AInteriorGenerator::DeleteRoom(const FString& RoomId)
{
	for (auto& Pair : RoomsByFloor)
	{
		TArray<AInteriorRoom*>& Rooms = Pair.Value.Rooms;
		for (int32 i = 0; i < Rooms.Num(); i++)
		{
			if (Rooms[i] && Rooms[i]->RoomId == RoomId)
			{
				Rooms[i]->Destroy();
				Rooms.RemoveAt(i);
				UsedRoomIds.Remove(RoomId);
				UE_LOG(LogTemp, Warning, TEXT("Deleted room/wall %s"), *RoomId);
				return true;
			}
		}
	}

	UE_LOG(LogTemp, Error, TEXT("DeleteRoom: '%s' not found"), *RoomId);
	return false;
}

bool AInteriorGenerator::ResizeRoom(const FString& RoomId, float NewWidth, float NewDepth)
{
	for (auto& Pair : RoomsByFloor)
	{
		for (AInteriorRoom* Room : Pair.Value.Rooms)
		{
			if (!Room || Room->RoomId != RoomId)
			{
				continue;
			}

			const TArray<FVector2D>& OldFootprint = Room->GetFootprint();
			if (OldFootprint.Num() == 0)
			{
				return false;
			}

			FVector2D MinPoint(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
			FVector2D MaxPoint(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
			for (const FVector2D& Point : OldFootprint)
			{
				MinPoint.X = FMath::Min(MinPoint.X, Point.X);
				MinPoint.Y = FMath::Min(MinPoint.Y, Point.Y);
				MaxPoint.X = FMath::Max(MaxPoint.X, Point.X);
				MaxPoint.Y = FMath::Max(MaxPoint.Y, Point.Y);
			}

			const float OldWidth = MaxPoint.X - MinPoint.X;
			const float OldDepth = MaxPoint.Y - MinPoint.Y;
			if (OldWidth < KINDA_SMALL_NUMBER || OldDepth < KINDA_SMALL_NUMBER)
			{
				UE_LOG(LogTemp, Error, TEXT("ResizeRoom: '%s' has a degenerate bounding box"), *RoomId);
				return false;
			}

			const FVector2D Center = (MinPoint + MaxPoint) * 0.5f;
			const float ScaleX = NewWidth / OldWidth;
			const float ScaleY = NewDepth / OldDepth;

			TArray<FVector2D> NewFootprint;
			NewFootprint.Reserve(OldFootprint.Num());
			for (const FVector2D& Point : OldFootprint)
			{
				NewFootprint.Add(Center + FVector2D((Point.X - Center.X) * ScaleX, (Point.Y - Center.Y) * ScaleY));
			}

			Room->ResizeFootprint(NewFootprint);
			UE_LOG(LogTemp, Warning, TEXT("Resized %s to %.1f x %.1f"), *RoomId, NewWidth, NewDepth);
			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("ResizeRoom: '%s' not found"), *RoomId);
	return false;
}

FString AInteriorGenerator::AddDoorToRoom(const FString& RoomId, int32 EdgeIndex, float OffsetWorldUnits)
{
	for (auto& Pair : RoomsByFloor)
	{
		for (AInteriorRoom* Room : Pair.Value.Rooms)
		{
			if (!Room || Room->RoomId != RoomId)
			{
				continue;
			}

			FInteriorOpening Opening;
			Opening.Id = FString::Printf(TEXT("%s-D%02d"), *RoomId, Room->GetOpenings().Num() + 1);
			Opening.Type = EInteriorOpeningType::Door;
			Opening.EdgeIndex = EdgeIndex;
			Opening.Offset = OffsetWorldUnits;
			Opening.Width = 90.f;
			Opening.Height = 210.f;
			// InputId/ConnectsTo left blank on purpose - see the header comment.

			Room->AddOpening(Opening);
			UE_LOG(LogTemp, Warning, TEXT("Added door %s to %s edge %d"), *Opening.Id, *RoomId, EdgeIndex);
			return Opening.Id;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("AddDoorToRoom: '%s' not found"), *RoomId);
	return FString();
}

bool AInteriorGenerator::DeleteDoor(const FString& RoomId, const FString& DoorId)
{
	for (auto& Pair : RoomsByFloor)
	{
		for (AInteriorRoom* Room : Pair.Value.Rooms)
		{
			if (!Room || Room->RoomId != RoomId)
			{
				continue;
			}

			for (const FInteriorOpening& Opening : Room->GetOpenings())
			{
				if (Opening.Id == DoorId)
				{
					Room->RemoveOpening(DoorId);
					UE_LOG(LogTemp, Warning, TEXT("Deleted door %s from %s"), *DoorId, *RoomId);
					return true;
				}
			}

			UE_LOG(LogTemp, Error, TEXT("DeleteDoor: '%s' has no door '%s'"), *RoomId, *DoorId);
			return false;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("DeleteDoor: room '%s' not found"), *RoomId);
	return false;
}

TSharedRef<FJsonObject> AInteriorGenerator::SerializeCurrentInterior() const
{
	TSharedRef<FJsonObject> InteriorObject = MakeShared<FJsonObject>();

	TArray<int32> Levels;
	FloorInfoByLevel.GetKeys(Levels);
	Levels.Sort();

	TArray<TSharedPtr<FJsonValue>> FloorsArray;
	for (int32 Level : Levels)
	{
		const FInteriorFloorInfo& FloorInfo = FloorInfoByLevel[Level];

		TSharedRef<FJsonObject> FloorObject = MakeShared<FJsonObject>();
		FloorObject->SetNumberField(TEXT("level"), Level);
		FloorObject->SetNumberField(TEXT("wallHeight"), FloorInfo.WallHeight);
		FloorObject->SetNumberField(TEXT("wallThickness"), FloorInfo.WallThickness);

		TArray<TSharedPtr<FJsonValue>> RoomsArray;
		if (const FInteriorRoomList* RoomList = RoomsByFloor.Find(Level))
		{
			for (const AInteriorRoom* Room : RoomList->Rooms)
			{
				if (!Room)
				{
					continue;
				}

				TSharedRef<FJsonObject> RoomObject = MakeShared<FJsonObject>();
				RoomObject->SetStringField(TEXT("id"), Room->RoomId);
				RoomObject->SetStringField(TEXT("name"), Room->RoomName);
				RoomObject->SetStringField(TEXT("access"), Room->bIsPublic ? TEXT("public") : TEXT("private"));
				RoomObject->SetBoolField(TEXT("requireKey"), Room->bRequiresKey);
				RoomObject->SetBoolField(TEXT("closed"), Room->IsClosedLoop());

				TArray<TSharedPtr<FJsonValue>> FootprintArray;
				for (const FVector2D& Point : Room->GetFootprint())
				{
					TArray<TSharedPtr<FJsonValue>> PointArray;
					PointArray.Add(MakeShared<FJsonValueNumber>(Point.X));
					PointArray.Add(MakeShared<FJsonValueNumber>(Point.Y));
					FootprintArray.Add(MakeShared<FJsonValueArray>(PointArray));
				}
				RoomObject->SetArrayField(TEXT("footprint"), FootprintArray);

				TArray<TSharedPtr<FJsonValue>> OpeningsArray;
				for (const FInteriorOpening& Opening : Room->GetOpenings())
				{
					TSharedRef<FJsonObject> OpeningObject = MakeShared<FJsonObject>();
					OpeningObject->SetStringField(TEXT("id"), Opening.Id);
					OpeningObject->SetStringField(TEXT("type"), OpeningTypeToString(Opening.Type));
					OpeningObject->SetNumberField(TEXT("edgeIndex"), Opening.EdgeIndex);
					OpeningObject->SetNumberField(TEXT("offset"), Opening.Offset);
					OpeningObject->SetNumberField(TEXT("width"), Opening.Width);
					if (Opening.Type == EInteriorOpeningType::Window)
					{
						OpeningObject->SetNumberField(TEXT("height"), Opening.Height);
						OpeningObject->SetNumberField(TEXT("sillHeight"), Opening.SillHeight);
					}
					if (Opening.Type == EInteriorOpeningType::Door)
					{
						if (!Opening.InputId.IsEmpty())
						{
							OpeningObject->SetStringField(TEXT("inputId"), Opening.InputId);
						}
						if (!Opening.ConnectsTo.IsEmpty())
						{
							OpeningObject->SetStringField(TEXT("connectsTo"), Opening.ConnectsTo);
						}
					}
					OpeningsArray.Add(MakeShared<FJsonValueObject>(OpeningObject));
				}
				RoomObject->SetArrayField(TEXT("openings"), OpeningsArray);

				RoomsArray.Add(MakeShared<FJsonValueObject>(RoomObject));
			}
		}
		FloorObject->SetArrayField(TEXT("rooms"), RoomsArray);

		TArray<TSharedPtr<FJsonValue>> NavNodesArray;
		for (const FInteriorNavNode& Node : NavNodes)
		{
			if (Node.Level != Level)
			{
				continue;
			}

			TSharedRef<FJsonObject> NodeObject = MakeShared<FJsonObject>();
			NodeObject->SetStringField(TEXT("id"), Node.Id);
			NodeObject->SetStringField(TEXT("type"), Node.Type);

			TArray<TSharedPtr<FJsonValue>> PositionArray;
			PositionArray.Add(MakeShared<FJsonValueNumber>(Node.Position.X));
			PositionArray.Add(MakeShared<FJsonValueNumber>(Node.Position.Y));
			NodeObject->SetArrayField(TEXT("position"), PositionArray);

			if (Node.ConnectsFloors.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> ConnectsFloorsArray;
				for (int32 ConnectedLevel : Node.ConnectsFloors)
				{
					ConnectsFloorsArray.Add(MakeShared<FJsonValueNumber>(ConnectedLevel));
				}
				NodeObject->SetArrayField(TEXT("connectsFloors"), ConnectsFloorsArray);
			}

			NavNodesArray.Add(MakeShared<FJsonValueObject>(NodeObject));
		}
		FloorObject->SetArrayField(TEXT("navNodes"), NavNodesArray);

		FloorsArray.Add(MakeShared<FJsonValueObject>(FloorObject));
	}

	InteriorObject->SetArrayField(TEXT("floors"), FloorsArray);
	return InteriorObject;
}

const TArray<AInteriorRoom*>& AInteriorGenerator::GetRoomsOnFloor(int32 Level) const
{
	static const TArray<AInteriorRoom*> Empty;
	const FInteriorRoomList* List = RoomsByFloor.Find(Level);
	return List ? List->Rooms : Empty;
}

void AInteriorGenerator::SyncInteriorToBuildingGenerator()
{
	if (LoadedBuildingId.IsEmpty() || !Generator)
	{
		return;
	}

	Generator->SetInteriorDataForBuilding(LoadedBuildingId, SerializeCurrentInterior());
}
