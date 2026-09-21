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

static EInteriorNavNodeType ParseNavNodeType(const FString& TypeString)
{
	if (TypeString == TEXT("elevator"))  { return EInteriorNavNodeType::Elevator; }
	if (TypeString == TEXT("stairs"))    { return EInteriorNavNodeType::Stairs; }
	if (TypeString == TEXT("escalator")) { return EInteriorNavNodeType::Escalator; }
	return EInteriorNavNodeType::PathNode;
}

static FString NavNodeTypeToString(EInteriorNavNodeType Type)
{
	switch (Type)
	{
	case EInteriorNavNodeType::Elevator:  return TEXT("elevator");
	case EInteriorNavNodeType::Stairs:    return TEXT("stairs");
	case EInteriorNavNodeType::Escalator: return TEXT("escalator");
	default:                              return TEXT("pathNode");
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

// ---- Helper: even-odd ray-casting point-in-polygon test (mirrors NavPlayerController.cpp's copy -
// each file that needs this keeps its own small static copy rather than sharing one across
// translation units, same as SegmentsIntersect/PolygonSelfIntersects above). ----
static bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Poly)
{
	bool bInside = false;
	const int32 N = Poly.Num();
	for (int32 i = 0, j = N - 1; i < N; j = i++)
	{
		const FVector2D& Pi = Poly[i];
		const FVector2D& Pj = Poly[j];
		const bool bCrosses = ((Pi.Y > Point.Y) != (Pj.Y > Point.Y)) &&
			(Point.X < (Pj.X - Pi.X) * (Point.Y - Pi.Y) / (Pj.Y - Pi.Y) + Pi.X);
		if (bCrosses)
		{
			bInside = !bInside;
		}
	}
	return bInside;
}

// ---- Helper: is every point of Footprint inside Poly? (a room/wall staying within the building's
// own outline - approximate for a concave building, since it only checks vertices, but consistent
// with the rest of this project's schematic fidelity) ----
static bool IsFootprintFullyInsidePolygon(const TArray<FVector2D>& Footprint, const TArray<FVector2D>& Poly)
{
	for (const FVector2D& Point : Footprint)
	{
		if (!IsPointInPolygon(Point, Poly))
		{
			return false;
		}
	}
	return true;
}

// ---- Helper: the point on segment AB closest to Point (mirrors InteriorFloorPlanWidget.cpp's
// DistancePointToSegment2D, generalized to return the point itself, not just the distance to it -
// FindSnapOffset needs the point; DistancePointToSegment2D below is now just this plus one more step) ----
static FVector2D ClosestPointOnSegment2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
{
	const FVector2D AB = B - A;
	const float LengthSq = AB.SizeSquared();
	if (LengthSq < KINDA_SMALL_NUMBER)
	{
		return A;
	}

	const float T = FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSq, 0.f, 1.f);
	return A + AB * T;
}

// ---- Helper: shortest distance from Point to segment AB ----
static float DistancePointToSegment2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
{
	return FVector2D::Distance(Point, ClosestPointOnSegment2D(Point, A, B));
}

// ---- Helper: is Point genuinely inside Poly, not just sitting on (or a hair's breadth from) its
// boundary? Ray-casting alone is fragile right at the boundary - a point that's supposed to sit
// exactly on an edge (e.g. two vertices snapped together - see FindSnapOffset) isn't guaranteed to
// land at bit-identical float coordinates, and that tiny noise can flip which side the ray-cast
// thinks it's on. Anything within BoundaryDeadZone of an edge is treated as "on the boundary,
// therefore not inside" up front, before ever asking the ray-cast. ----
static bool IsPointClearlyInsidePolygon(const FVector2D& Point, const TArray<FVector2D>& Poly)
{
	constexpr float BoundaryDeadZoneFootprintUnits = 0.5f;
	const int32 N = Poly.Num();
	for (int32 i = 0; i < N; i++)
	{
		if (DistancePointToSegment2D(Point, Poly[i], Poly[(i + 1) % N]) < BoundaryDeadZoneFootprintUnits)
		{
			return false;
		}
	}
	return IsPointInPolygon(Point, Poly);
}

// ---- Helper: do two rooms/walls' footprints overlap? Checks edge-edge crossings (catches partial
// overlaps, works for open walls too) plus containment (one shape's vertex landing fully inside the
// other's closed loop - edge crossings alone miss this when one shape sits entirely within another).
// bBClosed only matters for the edge-count/wrap-around and the containment check - an open wall has
// no interior to be "inside". ----
static bool DoFootprintsOverlap(const TArray<FVector2D>& FootprintA, bool bAClosed, const TArray<FVector2D>& FootprintB, bool bBClosed)
{
	const int32 NumA = FootprintA.Num();
	const int32 NumB = FootprintB.Num();
	if (NumA < 2 || NumB < 2)
	{
		return false;
	}

	const int32 NumEdgesA = bAClosed ? NumA : NumA - 1;
	const int32 NumEdgesB = bBClosed ? NumB : NumB - 1;

	// Deliberately no "near miss" tolerance here (an earlier version had one, to catch a drag
	// jumping several footprint units in one frame and grazing a corner without a clean crossing) -
	// snapping (see FindSnapOffset) intentionally lands two vertices exactly on top of each other,
	// and any tolerance greater than zero would flag that legitimate touch as an overlap. The vertex-
	// containment check below (which checks every point, not just one) turned out to be what that
	// original bug actually needed.
	//
	// When two edges share an endpoint (the normal result of snapping two shapes' corners together),
	// SegmentsIntersect's strict cross-product signs can misfire: one of its four cross products is
	// exactly zero because it's evaluated at that shared point, and zero doesn't cleanly fall on
	// either side of ">0" - depending on the angle, that can spuriously read as "crossing" even
	// though the edges only touch at the one shared corner. So a shared endpoint is checked for and
	// excluded up front, the same way the containment check below treats it as boundary, not inside.
	constexpr float SharedEndpointEpsilonFootprintUnits = 0.5f;
	auto NearlySamePoint = [](const FVector2D& P, const FVector2D& Q)
	{
		return FVector2D::DistSquared(P, Q) < SharedEndpointEpsilonFootprintUnits * SharedEndpointEpsilonFootprintUnits;
	};

	for (int32 i = 0; i < NumEdgesA; i++)
	{
		const FVector2D& A0 = FootprintA[i];
		const FVector2D& A1 = FootprintA[(i + 1) % NumA];
		for (int32 j = 0; j < NumEdgesB; j++)
		{
			const FVector2D& B0 = FootprintB[j];
			const FVector2D& B1 = FootprintB[(j + 1) % NumB];

			const bool bSharesEndpoint = NearlySamePoint(A0, B0) || NearlySamePoint(A0, B1) || NearlySamePoint(A1, B0) || NearlySamePoint(A1, B1);
			if (!bSharesEndpoint && SegmentsIntersect(A0, A1, B0, B1))
			{
				return true;
			}
		}
	}

	// Checks every point, not just one representative point - a bend in an open wall (a vertex
	// that isn't its first point) can land inside a small closed room without either wall segment
	// either side of that vertex actually crossing the room's boundary.
	if (bAClosed)
	{
		for (const FVector2D& Point : FootprintB)
		{
			if (IsPointClearlyInsidePolygon(Point, FootprintA))
			{
				return true;
			}
		}
	}
	if (bBClosed)
	{
		for (const FVector2D& Point : FootprintA)
		{
			if (IsPointClearlyInsidePolygon(Point, FootprintB))
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
				FString TypeString;
				NodeObject->TryGetStringField(TEXT("type"), TypeString);
				Node.Type = ParseNavNodeType(TypeString);
				Node.Level = Level;
				NodeObject->TryGetStringField(TEXT("connectorGroupId"), Node.ConnectorGroupId);

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

	// Whole-footprint check, not just a center point (a prefab shape placed near the building's own
	// wall can have its center inside while a corner still pokes outside it).
	if (!IsFootprintFullyInsidePolygon(Footprint, Building->GetFootprint()))
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: footprint extends outside the building's outline"));
		return nullptr;
	}

	// Unlike a drag (see CommitRoomMove), there's no "already had a valid spot to revert to" for a
	// brand new room, so overlap is rejected outright here rather than allowed-then-corrected.
	if (const FInteriorRoomList* ExistingRooms = RoomsByFloor.Find(ActiveFloor))
	{
		for (const AInteriorRoom* Other : ExistingRooms->Rooms)
		{
			if (Other && DoFootprintsOverlap(Footprint, true, Other->GetFootprint(), Other->IsClosedLoop()))
			{
				UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: footprint overlaps existing room/wall '%s'"), *Other->RoomId);
				return nullptr;
			}
		}
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

bool AInteriorGenerator::CanPlaceConnectorOnFloor(FVector2D Position, int32 Level) const
{
	const FInteriorFloorInfo* FloorInfo = FloorInfoByLevel.Find(Level);
	if (!FloorInfo)
	{
		UE_LOG(LogTemp, Error, TEXT("Connector placement: floor %d doesn't exist"), Level);
		return false;
	}

	APolyBuilding* Building = Generator ? Generator->FindBuildingById(LoadedBuildingId) : nullptr;
	if (!Building)
	{
		UE_LOG(LogTemp, Error, TEXT("Connector placement: building '%s' no longer exists"), *LoadedBuildingId);
		return false;
	}

	if (!IsPointInPolygon(Position, Building->GetFootprint()))
	{
		UE_LOG(LogTemp, Error, TEXT("Connector placement: outside the building's outline on floor %d"), Level);
		return false;
	}

	// Being inside a room's open interior is fine (that's an explicitly supported placement, unlike
	// a room or door) - only sitting inside a wall's own solid thickness is rejected.
	if (const FInteriorRoomList* RoomList = RoomsByFloor.Find(Level))
	{
		const float HalfWallThickness = FloorInfo->WallThickness * 0.5f;
		for (const AInteriorRoom* Room : RoomList->Rooms)
		{
			if (!Room)
			{
				continue;
			}

			const TArray<FVector2D>& RoomFootprint = Room->GetFootprint();
			const int32 NumPoints = RoomFootprint.Num();
			const int32 NumEdges = Room->IsClosedLoop() ? NumPoints : NumPoints - 1;
			for (int32 i = 0; i < NumEdges; i++)
			{
				const FVector2D& EdgeA = RoomFootprint[i];
				const FVector2D& EdgeB = RoomFootprint[(i + 1) % NumPoints];
				if (DistancePointToSegment2D(Position, EdgeA, EdgeB) < HalfWallThickness)
				{
					UE_LOG(LogTemp, Error, TEXT("Connector placement: blocked by a wall on floor %d"), Level);
					return false;
				}
			}
		}
	}

	return true;
}

FString AInteriorGenerator::PlaceConnectorNode(EInteriorNavNodeType Type, FVector2D Position, const TArray<int32>& ConnectsToFloors)
{
	if (LoadedBuildingId.IsEmpty() || !Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("PlaceConnectorNode: no building interior currently loaded"));
		return FString();
	}

	if (!Generator->FindBuildingById(LoadedBuildingId))
	{
		UE_LOG(LogTemp, Error, TEXT("PlaceConnectorNode: building '%s' no longer exists"), *LoadedBuildingId);
		return FString();
	}

	// A connector shaft has to be clear space all the way through the building - one blocked floor
	// makes the whole thing unplaceable rather than silently connecting fewer floors than asked for
	// (which would be easy to miss and would leave navigation with a connector that mysteriously
	// doesn't reach a floor it looks like it should). So this validates every requested floor
	// first, and only creates anything once all of them pass.
	for (int32 Level : ConnectsToFloors)
	{
		if (!CanPlaceConnectorOnFloor(Position, Level))
		{
			UE_LOG(LogTemp, Error, TEXT("PlaceConnectorNode: nothing placed"));
			return FString();
		}
	}

	const FString GroupId = FString::Printf(TEXT("%s-NAV-%02d"), *LoadedBuildingId, NavNodes.Num() + 1);
	for (int32 Level : ConnectsToFloors)
	{
		FInteriorNavNode Node;
		Node.Id = FString::Printf(TEXT("%s-F%02d"), *GroupId, Level);
		Node.Type = Type;
		Node.Level = Level;
		Node.Position = Position;
		Node.ConnectsFloors = ConnectsToFloors;
		Node.ConnectorGroupId = GroupId;
		NavNodes.Add(Node);
	}

	UE_LOG(LogTemp, Warning, TEXT("Placed connector '%s' on %d floor(s)"), *GroupId, ConnectsToFloors.Num());
	return GroupId;
}

bool AInteriorGenerator::DeleteConnectorGroup(const FString& GroupId)
{
	const int32 RemovedCount = NavNodes.RemoveAll([&GroupId](const FInteriorNavNode& Node)
	{
		return Node.ConnectorGroupId == GroupId;
	});

	if (RemovedCount > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Deleted connector '%s' (%d node(s))"), *GroupId, RemovedCount);
		return true;
	}

	UE_LOG(LogTemp, Error, TEXT("DeleteConnectorGroup: '%s' not found"), *GroupId);
	return false;
}

bool AInteriorGenerator::SetConnectorFloors(const FString& GroupId, const TArray<int32>& NewConnectsToFloors)
{
	if (NewConnectsToFloors.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("SetConnectorFloors: '%s' needs at least one floor - use DeleteConnectorGroup to remove it entirely"), *GroupId);
		return false;
	}

	EInteriorNavNodeType Type = EInteriorNavNodeType::PathNode;
	FVector2D Position = FVector2D::ZeroVector;
	TArray<int32> CurrentFloors;
	for (const FInteriorNavNode& Node : NavNodes)
	{
		if (Node.ConnectorGroupId == GroupId)
		{
			Type = Node.Type;
			Position = Node.Position;
			CurrentFloors.Add(Node.Level);
		}
	}

	if (CurrentFloors.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("SetConnectorFloors: '%s' not found"), *GroupId);
		return false;
	}

	// Same all-or-nothing reasoning as PlaceConnectorNode - only the newly-added floors need
	// (re-)validating, a floor already in CurrentFloors already has a node sitting there.
	for (int32 Level : NewConnectsToFloors)
	{
		if (!CurrentFloors.Contains(Level) && !CanPlaceConnectorOnFloor(Position, Level))
		{
			UE_LOG(LogTemp, Error, TEXT("SetConnectorFloors: '%s' unchanged"), *GroupId);
			return false;
		}
	}

	// Drop nodes for floors no longer in the list.
	NavNodes.RemoveAll([&GroupId, &NewConnectsToFloors](const FInteriorNavNode& Node)
	{
		return Node.ConnectorGroupId == GroupId && !NewConnectsToFloors.Contains(Node.Level);
	});

	// Every remaining node reflects the same full floor list.
	for (FInteriorNavNode& Node : NavNodes)
	{
		if (Node.ConnectorGroupId == GroupId)
		{
			Node.ConnectsFloors = NewConnectsToFloors;
		}
	}

	// Add nodes for newly-added floors.
	for (int32 Level : NewConnectsToFloors)
	{
		if (CurrentFloors.Contains(Level))
		{
			continue;
		}

		FInteriorNavNode NewNode;
		NewNode.Id = FString::Printf(TEXT("%s-F%02d"), *GroupId, Level);
		NewNode.Type = Type;
		NewNode.Level = Level;
		NewNode.Position = Position;
		NewNode.ConnectsFloors = NewConnectsToFloors;
		NewNode.ConnectorGroupId = GroupId;
		NavNodes.Add(NewNode);
	}

	UE_LOG(LogTemp, Warning, TEXT("Connector '%s' now on %d floor(s)"), *GroupId, NewConnectsToFloors.Num());
	return true;
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

			APolyBuilding* Building = Generator ? Generator->FindBuildingById(LoadedBuildingId) : nullptr;
			if (Building && !IsFootprintFullyInsidePolygon(NewFootprint, Building->GetFootprint()))
			{
				UE_LOG(LogTemp, Error, TEXT("ResizeRoom: '%s' at %.1f x %.1f would extend outside the building's outline"), *RoomId, NewWidth, NewDepth);
				return false;
			}

			Room->ResizeFootprint(NewFootprint);
			UE_LOG(LogTemp, Warning, TEXT("Resized %s to %.1f x %.1f"), *RoomId, NewWidth, NewDepth);
			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("ResizeRoom: '%s' not found"), *RoomId);
	return false;
}

bool AInteriorGenerator::MoveRoom(const FString& RoomId, const TArray<FVector2D>& NewFootprint)
{
	for (auto& Pair : RoomsByFloor)
	{
		for (AInteriorRoom* Room : Pair.Value.Rooms)
		{
			if (!Room || Room->RoomId != RoomId)
			{
				continue;
			}

			// Enforced every call (i.e. every frame of a drag, not just when it's released) - the
			// room should never even momentarily sit outside the building, unlike overlapping another
			// room/wall, which is only rejected once the drag ends (see CommitRoomMove). A rejected
			// move is simply not applied, so the room stays at its last valid position instead of
			// following the cursor past the wall - no other feedback needed for a live drag.
			APolyBuilding* Building = Generator ? Generator->FindBuildingById(LoadedBuildingId) : nullptr;
			if (Building && !IsFootprintFullyInsidePolygon(NewFootprint, Building->GetFootprint()))
			{
				return false;
			}

			Room->ResizeFootprint(NewFootprint);
			return true;
		}
	}

	return false;
}

bool AInteriorGenerator::CommitRoomMove(const FString& RoomId, const TArray<FVector2D>& FallbackFootprint)
{
	for (auto& Pair : RoomsByFloor)
	{
		TArray<AInteriorRoom*>& Rooms = Pair.Value.Rooms;
		for (AInteriorRoom* Room : Rooms)
		{
			if (!Room || Room->RoomId != RoomId)
			{
				continue;
			}

			// Overlap is allowed while a room is being dragged around (see MoveRoom) - only checked
			// once, here, when the drag ends. If it landed on top of another room/wall, revert to
			// where it was before the drag started rather than leaving it overlapping.
			for (const AInteriorRoom* Other : Rooms)
			{
				if (!Other || Other == Room)
				{
					continue;
				}

				if (DoFootprintsOverlap(Room->GetFootprint(), Room->IsClosedLoop(), Other->GetFootprint(), Other->IsClosedLoop()))
				{
					Room->ResizeFootprint(FallbackFootprint);
					UE_LOG(LogTemp, Warning, TEXT("CommitRoomMove: '%s' would overlap '%s' - reverted to its position before the drag"), *RoomId, *Other->RoomId);
					return false;
				}
			}

			return true;
		}
	}

	return false;
}

bool AInteriorGenerator::FindSnapOffset(int32 Level, const TArray<FVector2D>& Footprint, const FString& RoomIdToExclude, float ToleranceFootprintUnits, FVector2D& OutSnapOffset) const
{
	const FInteriorRoomList* RoomList = RoomsByFloor.Find(Level);
	if (!RoomList)
	{
		return false;
	}

	float BestDistSq = ToleranceFootprintUnits * ToleranceFootprintUnits;
	bool bFound = false;

	// Checking against the closest point on each edge - not just each vertex - covers snapping to
	// the middle of a wall as well as a corner; a vertex is just the case where that closest point
	// happens to land on an edge's own endpoint, so this subsumes a vertex-only check. Shared by
	// every candidate shape below (other rooms/walls, and the building's own outline).
	auto CheckAgainstEdges = [&](const TArray<FVector2D>& CandidateFootprint, bool bCandidateClosed)
	{
		const int32 NumCandidate = CandidateFootprint.Num();
		if (NumCandidate < 2)
		{
			return;
		}

		const int32 NumEdgesCandidate = bCandidateClosed ? NumCandidate : NumCandidate - 1;
		for (const FVector2D& MyVertex : Footprint)
		{
			for (int32 i = 0; i < NumEdgesCandidate; i++)
			{
				const FVector2D& EdgeA = CandidateFootprint[i];
				const FVector2D& EdgeB = CandidateFootprint[(i + 1) % NumCandidate];
				const FVector2D ClosestPoint = ClosestPointOnSegment2D(MyVertex, EdgeA, EdgeB);

				const float DistSq = FVector2D::DistSquared(MyVertex, ClosestPoint);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					OutSnapOffset = ClosestPoint - MyVertex;
					bFound = true;
				}
			}
		}
	};

	for (const AInteriorRoom* Other : RoomList->Rooms)
	{
		if (Other && Other->RoomId != RoomIdToExclude)
		{
			CheckAgainstEdges(Other->GetFootprint(), Other->IsClosedLoop());
		}
	}

	// The building's own outline - always closed, and in the same pre-scale/pre-offset footprint
	// units as everything else here (see IsPointInPolygon's callers elsewhere in this file for the
	// same assumption).
	if (Generator)
	{
		if (const APolyBuilding* Building = Generator->FindBuildingById(LoadedBuildingId))
		{
			CheckAgainstEdges(Building->GetFootprint(), true);
		}
	}

	return bFound;
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

			// A door only makes sense on a closed room - it has no enclosed space (and so no
			// defined size) to lead into otherwise. Refuse on an open wall.
			if (!Room->IsClosedLoop())
			{
				UE_LOG(LogTemp, Error, TEXT("AddDoorToRoom: '%s' is an open wall, not a closed room - doors can only go on closed rooms"), *RoomId);
				return FString();
			}

			constexpr float NewDoorWidth = 90.f;

			// Refuse to stack a door on top of one that's already on this edge (e.g. a stray
			// "add door" click landing back on a door someone just dragged there) - without this,
			// the new opening would silently coexist with the old one: NativePaint only draws the
			// first opening it finds per edge, so the earlier door would keep rendering while the
			// new one sat there invisible, only surfacing after the visible one got deleted.
			for (const FInteriorOpening& Existing : Room->GetOpenings())
			{
				if (Existing.EdgeIndex != EdgeIndex)
				{
					continue;
				}

				const bool bOverlaps = OffsetWorldUnits < Existing.Offset + Existing.Width && Existing.Offset < OffsetWorldUnits + NewDoorWidth;
				if (bOverlaps)
				{
					UE_LOG(LogTemp, Error, TEXT("AddDoorToRoom: '%s' edge %d already has door '%s' near that position"), *RoomId, EdgeIndex, *Existing.Id);
					return FString();
				}
			}

			FInteriorOpening Opening;
			Opening.Id = FString::Printf(TEXT("%s-D%02d"), *RoomId, Room->GetOpenings().Num() + 1);
			Opening.Type = EInteriorOpeningType::Door;
			Opening.EdgeIndex = EdgeIndex;
			Opening.Offset = OffsetWorldUnits;
			Opening.Width = NewDoorWidth;
			Opening.Height = 210.f;
			// InputId is the door's own room - always known, never user-edited. ConnectsTo is left
			// blank on purpose - see the header comment.
			Opening.InputId = RoomId;

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

bool AInteriorGenerator::MoveDoor(const FString& RoomId, const FString& DoorId, float NewOffsetWorldUnits)
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
					Room->MoveOpening(DoorId, NewOffsetWorldUnits);
					return true;
				}
			}
			return false;
		}
	}

	return false;
}

bool AInteriorGenerator::GetDoorOpening(const FString& RoomId, const FString& DoorId, FInteriorOpening& OutOpening) const
{
	for (const auto& Pair : RoomsByFloor)
	{
		for (const AInteriorRoom* Room : Pair.Value.Rooms)
		{
			if (!Room || Room->RoomId != RoomId)
			{
				continue;
			}

			for (const FInteriorOpening& Opening : Room->GetOpenings())
			{
				if (Opening.Id == DoorId)
				{
					OutOpening = Opening;
					return true;
				}
			}
			return false;
		}
	}

	return false;
}

bool AInteriorGenerator::SetDoorConnectsTo(const FString& RoomId, const FString& DoorId, const FString& NewConnectsTo)
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
					Room->SetOpeningConnectsTo(DoorId, NewConnectsTo);
					return true;
				}
			}
			return false;
		}
	}

	return false;
}

bool AInteriorGenerator::SetRoomName(const FString& RoomId, const FString& NewName)
{
	for (auto& Pair : RoomsByFloor)
	{
		for (AInteriorRoom* Room : Pair.Value.Rooms)
		{
			if (!Room || Room->RoomId != RoomId)
			{
				continue;
			}

			Room->RoomName = NewName;
			UE_LOG(LogTemp, Warning, TEXT("Renamed %s to '%s'"), *RoomId, *NewName);
			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("SetRoomName: '%s' not found"), *RoomId);
	return false;
}

FString AInteriorGenerator::GetRoomName(const FString& RoomId) const
{
	for (const auto& Pair : RoomsByFloor)
	{
		for (const AInteriorRoom* Room : Pair.Value.Rooms)
		{
			if (Room && Room->RoomId == RoomId)
			{
				return Room->RoomName;
			}
		}
	}

	return FString();
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
			NodeObject->SetStringField(TEXT("type"), NavNodeTypeToString(Node.Type));

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

			if (!Node.ConnectorGroupId.IsEmpty())
			{
				NodeObject->SetStringField(TEXT("connectorGroupId"), Node.ConnectorGroupId);
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
