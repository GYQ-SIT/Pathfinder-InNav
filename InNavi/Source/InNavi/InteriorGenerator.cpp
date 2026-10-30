// Fill out your copyright notice in the Description page of Project Settings.


#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "FloorLayout.h"
#include "NavigationSearchUtils.h"
#include "RoomTypes.h"
#include "InteriorConnectorMarker.h"
#include "BuildingGenerator.h"
#include "PolyBuilding.h"
#include "EngineUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"

// How close two footprints' edges/corners have to be to count as "just touching" rather than
// "overlapping" (DoFootprintsOverlap's own ToleranceUnits parameter) - a fixed WORLD distance. Most
// DoFootprintsOverlap callers work in footprint-space (pre-scale) points, so they divide this by
// Generator->GetScale() to convert it down; the one caller that already converted both footprints to
// world space first (CanPlaceConnectorOnFloor's building-vs-building check) uses it bare.
static constexpr float OverlapToleranceWorldUnits = 5.f;

AInteriorGenerator::AInteriorGenerator()
{
	PrimaryActorTick.bCanEverTick = false;
	Generator = nullptr;
}

float AInteriorGenerator::GetWallThicknessForFloor(int32 Level) const
{
	if (const FInteriorFloorInfo* FloorInfo = FloorInfoByLevel.Find(Level))
	{
		return FloorInfo->WallThickness;
	}
	return 20.f;
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
static bool InteriorGen_SegmentsIntersect(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
{
	auto Cross = [](const FVector2D& O, const FVector2D& P1, const FVector2D& P2)
	{
		return (P1.X - O.X) * (P2.Y - O.Y) - (P1.Y - O.Y) * (P2.X - O.X);
	};

	// A point within a millimetre of the other segment's line counts as on it. Without this two rooms side by side in a turned row,
	// whose edges lie on one line with a gap between them, can read as crossing when float noise tips one cross product's sign.
	auto Settle = [](float Value, float LineLength)
	{
		return FMath::Abs(Value) < 1e-3f * LineLength ? 0.f : Value;
	};
	const float LengthAB = FVector2D::Distance(A, B);
	const float LengthCD = FVector2D::Distance(C, D);
	const float d1 = Settle(Cross(C, D, A), LengthCD);
	const float d2 = Settle(Cross(C, D, B), LengthCD);
	const float d3 = Settle(Cross(A, B, C), LengthAB);
	const float d4 = Settle(Cross(A, B, D), LengthAB);

	return ((d1 > 0.f) != (d2 > 0.f)) && ((d3 > 0.f) != (d4 > 0.f));
}

// ---- Helper: does this polygon's outline cross itself? (non-adjacent edges only) ----
static bool InteriorGen_PolygonSelfIntersects(const TArray<FVector2D>& Poly)
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

			if (InteriorGen_SegmentsIntersect(Poly[i], Poly[iNext], Poly[j], Poly[jNext]))
			{
				return true;
			}
		}
	}
	return false;
}

// ---- Helper: even-odd ray-casting point-in-polygon test (mirrors NavPlayerController.cpp's copy -
// each file that needs this keeps its own small static copy rather than sharing one across
// translation units. Prefixed InteriorGen_ (same for the two helpers above) because Unreal's Unity
// Build occasionally groups two of these files into the same translation unit depending on how
// many .cpp files the project has - a same-named static in both then collides at compile time
// (hit this in practice once a new .cpp file shifted the Unity batching). Keep this prefix
// convention for any future per-file copy of a same-purpose helper. ----
static bool InteriorGen_IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Poly)
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
		if (!InteriorGen_IsPointInPolygon(Point, Poly))
		{
			return false;
		}
	}
	return true;
}

// ---- Helper: the point on segment AB closest to Point (mirrors InteriorFloorPlanWidget.cpp's
// InteriorGen_DistancePointToSegment2D, generalized to return the point itself, not just the distance to it -
// FindSnapOffset needs the point; InteriorGen_DistancePointToSegment2D below is now just this plus one more step) ----
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
static float InteriorGen_DistancePointToSegment2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
{
	return FVector2D::Distance(Point, ClosestPointOnSegment2D(Point, A, B));
}

// ---- Helper: shortest distance between segments AB and CD - 0 if they cross (checked via
// InteriorGen_SegmentsIntersect first), otherwise the minimum is always achieved at one of the 4
// endpoint-to-opposite-segment distances (standard result for 2D segment-segment distance). Used by
// CanPlaceConnectorOnFloor to check a connector's whole footprint rectangle against wall segments,
// not just a single point. ----
static float InteriorGen_DistanceSegmentToSegment2D(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
{
	if (InteriorGen_SegmentsIntersect(A, B, C, D))
	{
		return 0.f;
	}
	float MinDist = InteriorGen_DistancePointToSegment2D(A, C, D);
	MinDist = FMath::Min(MinDist, InteriorGen_DistancePointToSegment2D(B, C, D));
	MinDist = FMath::Min(MinDist, InteriorGen_DistancePointToSegment2D(C, A, B));
	MinDist = FMath::Min(MinDist, InteriorGen_DistancePointToSegment2D(D, A, B));
	return MinDist;
}

// ---- Helper: is Point genuinely inside Poly, not just sitting on (or a hair's breadth from) its
// boundary? Ray-casting alone is fragile right at the boundary - a point that's supposed to sit
// exactly on an edge (e.g. two vertices snapped together - see FindSnapOffset) isn't guaranteed to
// land at bit-identical float coordinates, and that tiny noise can flip which side the ray-cast
// thinks it's on. Anything within BoundaryDeadZone of an edge is treated as "on the boundary,
// therefore not inside" up front, before ever asking the ray-cast. BoundaryDeadZone is in
// FootprintA/FootprintB's own units, whatever those happen to be for this call - see
// DoFootprintsOverlap's own comment on why this can't be a bare constant. ----
static bool IsPointClearlyInsidePolygon(const FVector2D& Point, const TArray<FVector2D>& Poly, float BoundaryDeadZone)
{
	const int32 N = Poly.Num();
	for (int32 i = 0; i < N; i++)
	{
		if (InteriorGen_DistancePointToSegment2D(Point, Poly[i], Poly[(i + 1) % N]) < BoundaryDeadZone)
		{
			return false;
		}
	}
	return InteriorGen_IsPointInPolygon(Point, Poly);
}

// ---- Helper: do two rooms/walls' footprints overlap? Checks edge-edge crossings (catches partial
// overlaps, works for open walls too) plus containment (one shape's vertex landing fully inside the
// other's closed loop - edge crossings alone miss this when one shape sits entirely within another).
// bBClosed only matters for the edge-count/wrap-around and the containment check - an open wall has
// no interior to be "inside".
//
// ToleranceUnits is a "how close counts as touching, not overlapping" distance, in FootprintA/
// FootprintB's OWN units - most callers pass footprint-space (pre-scale) points, in which case this
// should be a small WORLD distance divided by Generator->GetScale() (not a bare constant - a
// footprint-unit value calibrated for buildings.json's own Scale~10 reads as 10x too generous for a
// database-loaded building's Scale~100, which is exactly what let two connectors' footprints overlap
// without being caught - see project memory), but at least one caller (CanPlaceConnectorOnFloor's
// building-vs-building check) already converts both footprints to world space first, and should pass
// a bare world distance instead. ----
static bool DoFootprintsOverlap(const TArray<FVector2D>& FootprintA, bool bAClosed, const TArray<FVector2D>& FootprintB, bool bBClosed, float ToleranceUnits)
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
	// InteriorGen_SegmentsIntersect's strict cross-product signs can misfire: one of its four cross products is
	// exactly zero because it's evaluated at that shared point, and zero doesn't cleanly fall on
	// either side of ">0" - depending on the angle, that can spuriously read as "crossing" even
	// though the edges only touch at the one shared corner. So a shared endpoint is checked for and
	// excluded up front, the same way the containment check below treats it as boundary, not inside.
	auto NearlySamePoint = [ToleranceUnits](const FVector2D& P, const FVector2D& Q)
	{
		return FVector2D::DistSquared(P, Q) < ToleranceUnits * ToleranceUnits;
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
			if (!bSharesEndpoint && InteriorGen_SegmentsIntersect(A0, A1, B0, B1))
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
			if (IsPointClearlyInsidePolygon(Point, FootprintA, ToleranceUnits))
			{
				return true;
			}
		}
	}
	if (bBClosed)
	{
		for (const FVector2D& Point : FootprintA)
		{
			if (IsPointClearlyInsidePolygon(Point, FootprintB, ToleranceUnits))
			{
				return true;
			}
		}
	}

	return false;
}

int32 AInteriorGenerator::LoadInteriorForBuilding(const FString& BuildingId)
{
	TGuardValue<bool> LoadingGuard(bLoadingInterior, true);
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

	// The doors in the outer wall, if the file has any, and the neighbours' doors that show here.
	Entrances = InteriorEntrances::ReadAll(InteriorData);
	RefreshMirroredEntrances();

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

		// Start a fresh Undo/Redo history for this building.
		if (!bIsRestoringHistory)
		{
			ClearHistory();
			RecordHistorySnapshot();
		}

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
						// Retain legacy outside exits so older projects remain routable without reauthoring doors.

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
					Room->SetTypeColourShown(bRoomColoursVisible);
					Room->RoomId = RoomId;
					Room->RoomName = RoomName;
					const TArray<TSharedPtr<FJsonValue>>* TagValues;
					if (RoomObject->TryGetArrayField(TEXT("tags"), TagValues))
					{
						TArray<FString> LoadedTags;
						for (const TSharedPtr<FJsonValue>& Tag : *TagValues)
						{
							FString Text;
							if (Tag.IsValid() && Tag->TryGetString(Text)) LoadedTags.Add(Text);
						}
						Room->RoomTags = NavigationSearch::NormalizeTags(LoadedTags);
					}
					FString LoadedType;
					RoomObject->TryGetStringField(TEXT("roomType"), LoadedType);
					Room->RoomType = RoomTypes::Normalize(LoadedType);
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

				NodeObject->TryGetNumberField(TEXT("rotationDegrees"), Node.RotationDegrees);
				if (!NodeObject->TryGetNumberField(TEXT("sizeScale"), Node.SizeScale))
				{
					Node.SizeScale = 1.f;
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
				SpawnConnectorMarker(NavNodes.Last(), Building);
			}
		}
	}

	// Interior floor count is the source of truth for how tall the exterior shell is.
	Building->SetFloorCount(FloorsArray->Num());

	ActiveFloor = 1;
	SetActiveFloor(ActiveFloor);

	UE_LOG(LogTemp, Warning, TEXT("Loaded interior for %s: %d floors, %d rooms"), *BuildingId, FloorsArray->Num(), SpawnedCount);

	// Start a fresh Undo/Redo history for this building.
	if (!bIsRestoringHistory)
	{
		ClearHistory();
		RecordHistorySnapshot();
	}

	return SpawnedCount;
}

void AInteriorGenerator::ClearLoadedInterior()
{
	++RoomSearchRevision;
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
	for (FInteriorNavNode& Node : NavNodes)
	{
		if (Node.MarkerActor)
		{
			Node.MarkerActor->Destroy();
		}
	}
	NavNodes.Empty();
	Entrances.Empty();
	AllEntrances.Empty();
	FloorInfoByLevel.Empty();
	UsedRoomIds.Empty();
	LoadedBuildingId.Empty();
	ActiveFloor = 1;
}

TArray<int32> AInteriorGenerator::GetFloorLevels() const
{
	TArray<int32> Levels;
	FloorInfoByLevel.GetKeys(Levels);
	Levels.Sort();
	return Levels;
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

	for (const FInteriorNavNode& Node : NavNodes)
	{
		if (Node.MarkerActor)
		{
			const bool bVisible = (Node.Level == ActiveFloor);
			Node.MarkerActor->SetActorHiddenInGame(!bVisible);
			Node.MarkerActor->SetActorEnableCollision(bVisible);
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
	RecordHistorySnapshot();
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

	UE_LOG(LogTemp, Warning,TEXT("Removed floor %d from %s, now %d floors"),Level, *LoadedBuildingId, NewFloorCount);

	RecordHistorySnapshot();
	return true;
}

FString AInteriorGenerator::PeekNextRoomId() const
{
	if (LoadedBuildingId.IsEmpty())
	{
		return FString();
	}

	// Scans for the first free index rather than basing this on how many rooms currently exist on the
	// floor (the old Rooms.Num() + 1 scheme) - that undercounts as soon as anything on this floor has
	// ever been deleted (UsedRoomIds still remembers the id was used, Rooms.Num() doesn't), so it could
	// hand out an id that's actually still in use by a room/wall that was never deleted. This is
	// correct regardless of how many rooms/walls were created and deleted before this call.
	for (int32 Index = 1; ; Index++)
	{
		const FString CandidateId = FString::Printf(TEXT("%s-%02d-%02d"), *LoadedBuildingId, ActiveFloor, Index);
		if (!UsedRoomIds.Contains(CandidateId))
		{
			return CandidateId;
		}
	}
}

AInteriorRoom* AInteriorGenerator::CreateRoomFromFootprint(const TArray<FVector2D>& Footprint,const FString& RoomId,const FString& RoomName,bool bIsPublic,bool bRequiresKey,bool bAllowOverlap, FString& OutError)
{
	return CreateRoomFromFootprintWithTags(Footprint, RoomId, RoomName, bIsPublic, bRequiresKey, bAllowOverlap, OutError, {});
}

AInteriorRoom* AInteriorGenerator::CreateRoomFromFootprintWithTags(const TArray<FVector2D>& Footprint, const FString& RoomId,
	const FString& RoomName, bool bIsPublic, bool bRequiresKey, bool bAllowOverlap, FString& OutError, const TArray<FString>& InTags,
	const FString& InRoomType)
{
	OutError.Empty();

	if (LoadedBuildingId.IsEmpty() || !Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: no building interior currently loaded"));
		OutError = TEXT("No building interior is currently loaded.");
		return nullptr;
	}

	const FInteriorFloorInfo* FloorInfo = FloorInfoByLevel.Find(ActiveFloor);
	if (!FloorInfo)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: active floor %d doesn't exist"), ActiveFloor);
		OutError = TEXT("The active floor doesn't exist.");
		return nullptr;
	}

	APolyBuilding* Building = Generator->FindBuildingById(LoadedBuildingId);
	if (!Building)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: building '%s' no longer exists"), *LoadedBuildingId);
		OutError = TEXT("The building no longer exists.");
		return nullptr;
	}

	if (Footprint.Num() < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: needs at least 3 footprint points (has %d)"), Footprint.Num());
		OutError = TEXT("A room needs at least 3 points.");
		return nullptr;
	}

	if (InteriorGen_PolygonSelfIntersects(Footprint))
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: footprint crosses itself - fix the outline before finishing"));
		OutError = TEXT("The outline crosses itself - fix it before finishing.");
		return nullptr;
	}

	// Whole-footprint check, not just a center point (a prefab shape placed near the building's own
	// wall can have its center inside while a corner still pokes outside it).
	if (!IsFootprintFullyInsidePolygon(Footprint, Building->GetFootprint()))
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: footprint extends outside the building's outline"));
		OutError = TEXT("It extends outside the building's outline.");
		return nullptr;
	}

	// Unlike a drag (see CommitRoomMove), there's no "already had a valid spot to revert to" for a
	// brand new room, so overlap is rejected outright here rather than allowed-then-corrected.
	if (const FInteriorRoomList* ExistingRooms = RoomsByFloor.Find(ActiveFloor))
	{
		for (const AInteriorRoom* Other : ExistingRooms->Rooms)
		{
			if (Other && DoFootprintsOverlap(Footprint, true, Other->GetFootprint(), Other->IsClosedLoop(), OverlapToleranceWorldUnits / Generator->GetScale()))
			{
				UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: footprint overlaps existing room/wall '%s'"), *Other->RoomId);
				OutError = FString::Printf(TEXT("It overlaps an existing room/wall ('%s')."), *Other->RoomId);
				return nullptr;
			}
		}
	}

	if (DoesFootprintBlockConnector(Footprint, /*bClosedLoop=*/true, ActiveFloor))
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: footprint would wall in an elevator/stairs/escalator"));
		OutError = TEXT("It would wall in an elevator/stairs/escalator.");
		return nullptr;
	}

	const FString ResolvedId = RoomId.IsEmpty() ? PeekNextRoomId() : RoomId;
	if (UsedRoomIds.Contains(ResolvedId))
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRoomFromFootprint: room id '%s' is already in use"), *ResolvedId);
		OutError = FString::Printf(TEXT("The room id '%s' is already in use."), *ResolvedId);
		return nullptr;
	}

	if (!GetWorld())
	{
		OutError = TEXT("No world to spawn the room into.");
		return nullptr;
	}

	const float Scale = Generator->GetScale();
	const float FloorBaseZ = (ActiveFloor - 1) * Building->GetFloorHeight();

	AInteriorRoom* Room = GetWorld()->SpawnActor<AInteriorRoom>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (Room)
	{
		Room->SetTypeColourShown(bRoomColoursVisible);
		Room->RoomId = ResolvedId;
		Room->RoomName = RoomName.IsEmpty() ? ResolvedId : RoomName;
		Room->RoomTags = NavigationSearch::NormalizeTags(InTags);
		Room->RoomType = RoomTypes::Normalize(InRoomType);
		Room->FloorLevel = ActiveFloor;
		Room->bIsPublic = bIsPublic;
		Room->bRequiresKey = bRequiresKey;
		Room->BuildWalls(Footprint, Scale, Building->BuildingOffset, FloorBaseZ, FloorInfo->WallHeight, FloorInfo->WallThickness, TArray<FInteriorOpening>(), /*bClosedLoop=*/true);

		RoomsByFloor.FindOrAdd(ActiveFloor).Rooms.Add(Room);
		UsedRoomIds.Add(ResolvedId);
		RecordHistorySnapshot();
	}
	return Room;
}

AInteriorRoom* AInteriorGenerator::CreateWallFromPoints(const TArray<FVector2D>& Points, const FString& WallId, FString& OutError)
{
	OutError.Empty();

	if (LoadedBuildingId.IsEmpty() || !Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateWallFromPoints: no building interior currently loaded"));
		OutError = TEXT("No building interior is currently loaded.");
		return nullptr;
	}

	const FInteriorFloorInfo* FloorInfo = FloorInfoByLevel.Find(ActiveFloor);
	if (!FloorInfo)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateWallFromPoints: active floor %d doesn't exist"), ActiveFloor);
		OutError = TEXT("The active floor doesn't exist.");
		return nullptr;
	}

	APolyBuilding* Building = Generator->FindBuildingById(LoadedBuildingId);
	if (!Building)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateWallFromPoints: building '%s' no longer exists"), *LoadedBuildingId);
		OutError = TEXT("The building no longer exists.");
		return nullptr;
	}

	if (Points.Num() < 2)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateWallFromPoints: needs at least 2 points (has %d)"), Points.Num());
		OutError = TEXT("A wall needs at least 2 points.");
		return nullptr;
	}

	// Same overlap/connector-block checks CreateRoomFromFootprint runs for a closed room, adapted for
	// an open polyline (bClosedLoop=false throughout - DoFootprintsOverlap/DoesFootprintBlockConnector
	// both already support open loops, this was simply never wired up here before) - a wall previously
	// had no geometric validation at all, so it could be drawn straight through an existing room,
	// another wall, or an elevator/stairs/escalator shaft.
	if (const FInteriorRoomList* ExistingRooms = RoomsByFloor.Find(ActiveFloor))
	{
		for (const AInteriorRoom* Other : ExistingRooms->Rooms)
		{
			if (Other && DoFootprintsOverlap(Points, false, Other->GetFootprint(), Other->IsClosedLoop(), OverlapToleranceWorldUnits / Generator->GetScale()))
			{
				UE_LOG(LogTemp, Error, TEXT("CreateWallFromPoints: wall overlaps existing room/wall '%s'"), *Other->RoomId);
				OutError = FString::Printf(TEXT("It overlaps an existing room/wall ('%s')."), *Other->RoomId);
				return nullptr;
			}
		}
	}

	if (DoesFootprintBlockConnector(Points, /*bClosedLoop=*/false, ActiveFloor))
	{
		UE_LOG(LogTemp, Error, TEXT("CreateWallFromPoints: wall would cut through an elevator/stairs/escalator"));
		OutError = TEXT("It would cut through an elevator/stairs/escalator.");
		return nullptr;
	}

	// Shares the room id namespace/uniqueness check and auto-numbering - a wall is just an
	// open-loop room as far as ids/floors are concerned.
	const FString ResolvedId = WallId.IsEmpty() ? PeekNextRoomId() : WallId;
	if (UsedRoomIds.Contains(ResolvedId))
	{
		UE_LOG(LogTemp, Error, TEXT("CreateWallFromPoints: id '%s' is already in use"), *ResolvedId);
		OutError = FString::Printf(TEXT("The wall id '%s' is already in use."), *ResolvedId);
		return nullptr;
	}

	if (!GetWorld())
	{
		OutError = TEXT("No world to spawn the wall into.");
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
		RecordHistorySnapshot();
	}

	return Wall;
}

bool AInteriorGenerator::DoesFootprintBlockConnector(const TArray<FVector2D>& Footprint, bool bClosedLoop, int32 Level) const
{
	const FInteriorFloorInfo* FloorInfo = FloorInfoByLevel.Find(Level);
	if (!FloorInfo || Footprint.Num() < 2)
	{
		return false;
	}

	const float Scale = Generator ? Generator->GetScale() : 1.f;
	// FloorInfo->WallThickness is world-space cm, but EdgeA/EdgeB/NodeCorners below are all footprint-
	// space (pre-scale) points - dividing by Scale converts the threshold down to match, instead of
	// comparing a world-space distance directly against a footprint-space one (a straight unit
	// mismatch, not just a "Scale~10 assumption" - see project memory). At Scale=10 this happened to
	// read as a plausible ~1m real tolerance by coincidence; at Scale=100 the undivided version
	// ballooned to ~10m.
	const float HalfWallThickness = (FloorInfo->WallThickness * 0.5f) / Scale;
	const int32 NumPoints = Footprint.Num();
	const int32 NumEdges = bClosedLoop ? NumPoints : NumPoints - 1;

	for (const FInteriorNavNode& Node : NavNodes)
	{
		if (Node.Level != Level)
		{
			continue;
		}

		// The connector's own real (rotated/scaled) footprint rectangle (see CanPlaceConnectorOnFloor's
		// own comment on this same conversion) - checking against just Node.Position would miss a wall
		// passing through the connector's footprint without touching its exact center point.
		TArray<FVector2D> LocalCorners;
		GetConnectorFootprintCorners(Node.Type, Node.RotationDegrees, Node.SizeScale, LocalCorners);
		if (LocalCorners.Num() == 0)
		{
			continue;
		}
		FVector2D NodeCorners[4];
		for (int32 c = 0; c < 4; c++)
		{
			NodeCorners[c] = Node.Position + LocalCorners[c] / FMath::Max(Scale, KINDA_SMALL_NUMBER);
		}

		for (int32 i = 0; i < NumEdges; i++)
		{
			const FVector2D& EdgeA = Footprint[i];
			const FVector2D& EdgeB = Footprint[(i + 1) % NumPoints];
			for (int32 c = 0; c < 4; c++)
			{
				if (InteriorGen_DistanceSegmentToSegment2D(EdgeA, EdgeB, NodeCorners[c], NodeCorners[(c + 1) % 4]) < HalfWallThickness)
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool AInteriorGenerator::CanPlaceConnectorOnFloor(EInteriorNavNodeType Type, FVector2D Position, int32 Level, float RotationDegrees, float SizeScale, const FString& IgnoreGroupId, bool bLogFailures) const
{
	const FInteriorFloorInfo* FloorInfo = FloorInfoByLevel.Find(Level);
	if (!FloorInfo)
	{
		if (bLogFailures) UE_LOG(LogTemp, Error, TEXT("Connector placement: floor %d doesn't exist"), Level);
		return false;
	}

	APolyBuilding* Building = Generator ? Generator->FindBuildingById(LoadedBuildingId) : nullptr;
	if (!Building)
	{
		if (bLogFailures) UE_LOG(LogTemp, Error, TEXT("Connector placement: building '%s' no longer exists"), *LoadedBuildingId);
		return false;
	}

	const float Scale = Generator->GetScale();

	// The connector's real (rotated/scaled) 3D footprint rectangle, centered on Position -
	// GetConnectorFootprintCorners is defined in world units (matching AInteriorConnectorMarker's own
	// mesh), converted down to this function's own pre-scale space (matching Building->GetFootprint()/
	// Room->GetFootprint()'s own convention) rather than converting everything else up to world space.
	TArray<FVector2D> LocalCorners;
	GetConnectorFootprintCorners(Type, RotationDegrees, SizeScale, LocalCorners);
	if (LocalCorners.Num() == 0)
	{
		// PathNode - no footprint to validate.
		return true;
	}
	TArray<FVector2D> Corners;
	Corners.Reserve(4);
	for (const FVector2D& Local : LocalCorners)
	{
		Corners.Add(Position + Local / FMath::Max(Scale, KINDA_SMALL_NUMBER));
	}

	if (!IsFootprintFullyInsidePolygon(Corners, Building->GetFootprint()))
	{
		if (bLogFailures) UE_LOG(LogTemp, Error, TEXT("Connector placement: its footprint doesn't fit inside the building's outline on floor %d"), Level);
		return false;
	}

	// Being inside a room's open interior is fine (that's an explicitly supported placement, unlike
	// a room or door) - only the connector's footprint overlapping a wall's own solid thickness is
	// rejected. Checks the whole rectangle now, not just Position, since AInteriorConnectorMarker's
	// real 3D geometry can be considerably bigger than the single point this used to validate.
	if (const FInteriorRoomList* RoomList = RoomsByFloor.Find(Level))
	{
		// See DoesFootprintBlockConnector's own comment on why this divides by Scale - WallThickness
		// is world-space cm, but Corners/EdgeA/EdgeB below are all footprint-space.
		const float HalfWallThickness = (FloorInfo->WallThickness * 0.5f) / Scale;
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
				for (int32 c = 0; c < 4; c++)
				{
					if (InteriorGen_DistanceSegmentToSegment2D(Corners[c], Corners[(c + 1) % 4], EdgeA, EdgeB) < HalfWallThickness)
					{
						if (bLogFailures) UE_LOG(LogTemp, Error, TEXT("Connector placement: its footprint overlaps a wall on floor %d"), Level);
						return false;
					}
				}
			}
		}
	}

	// Also can't overlap another connector's own footprint on this floor (elevators/stairs/escalators
	// placed on top of each other) - IgnoreGroupId skips nodes belonging to the connector being
	// rotated/scaled itself (see this function's own header comment), which already legitimately sit
	// exactly here.
	for (const FInteriorNavNode& Node : NavNodes)
	{
		if (Node.Level != Level || (!IgnoreGroupId.IsEmpty() && Node.ConnectorGroupId == IgnoreGroupId))
		{
			continue;
		}

		TArray<FVector2D> OtherLocalCorners;
		GetConnectorFootprintCorners(Node.Type, Node.RotationDegrees, Node.SizeScale, OtherLocalCorners);
		if (OtherLocalCorners.Num() == 0)
		{
			continue;
		}
		TArray<FVector2D> OtherCorners;
		OtherCorners.Reserve(4);
		for (const FVector2D& Local : OtherLocalCorners)
		{
			OtherCorners.Add(Node.Position + Local / FMath::Max(Scale, KINDA_SMALL_NUMBER));
		}

		if (DoFootprintsOverlap(Corners, true, OtherCorners, true, OverlapToleranceWorldUnits / Scale))
		{
			if (bLogFailures) UE_LOG(LogTemp, Error, TEXT("Connector placement: its footprint overlaps another connector ('%s') on floor %d"), *Node.ConnectorGroupId, Level);
			return false;
		}
	}

	// Also can't overlap a neighboring building's own footprint - buildings live at their own
	// BuildingOffset in world space, so this check (unlike the two above) works in world units.
	TArray<FVector2D> WorldCorners;
	WorldCorners.Reserve(4);
	for (const FVector2D& Corner : Corners)
	{
		WorldCorners.Add((Corner + Building->BuildingOffset) * Scale);
	}
	for (APolyBuilding* OtherBuilding : Generator->GetAllBuildings())
	{
		if (!OtherBuilding || OtherBuilding == Building)
		{
			continue;
		}

		TArray<FVector2D> OtherWorldFootprint;
		OtherWorldFootprint.Reserve(OtherBuilding->GetFootprint().Num());
		for (const FVector2D& Point : OtherBuilding->GetFootprint())
		{
			OtherWorldFootprint.Add((Point + OtherBuilding->BuildingOffset) * Scale);
		}

		// Already world-space (both footprints were converted just above) - the bare world tolerance,
		// not divided by Scale again.
		if (DoFootprintsOverlap(WorldCorners, true, OtherWorldFootprint, true, OverlapToleranceWorldUnits))
		{
			if (bLogFailures) UE_LOG(LogTemp, Error, TEXT("Connector placement: its footprint overlaps building '%s'"), *OtherBuilding->BuildingId);
			return false;
		}
	}

	return true;
}

void AInteriorGenerator::SpawnConnectorMarker(FInteriorNavNode& Node, APolyBuilding* Building)
{
	if (Node.Type == EInteriorNavNodeType::PathNode || !Building || !Generator)
	{
		return;
	}

	AInteriorConnectorMarker* Marker = GetWorld()->SpawnActor<AInteriorConnectorMarker>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Marker)
	{
		return;
	}

	Node.MarkerActor = Marker;
	RebuildConnectorMarker(Node, Building);
}

void AInteriorGenerator::RebuildConnectorMarker(FInteriorNavNode& Node, APolyBuilding* Building) const
{
	if (!Node.MarkerActor || !Building || !Generator)
	{
		return;
	}

	const float Scale = Generator->GetScale();
	const FVector2D WorldCenter = (Node.Position + Building->BuildingOffset) * Scale;
	const float FloorBaseZ = (Node.Level - 1) * Building->GetFloorHeight();
	Node.MarkerActor->BuildMarker(Node.Type, WorldCenter, FloorBaseZ, Building->GetFloorHeight(), Node.RotationDegrees, Node.SizeScale);
	Node.MarkerActor->SetOwnerGroupId(Node.ConnectorGroupId);
	Node.MarkerActor->SetActorHiddenInGame(Node.Level != ActiveFloor);
	Node.MarkerActor->SetActorEnableCollision(Node.Level == ActiveFloor);
}

FString AInteriorGenerator::PlaceConnectorNode(EInteriorNavNodeType Type, FVector2D Position, const TArray<int32>& ConnectsToFloors)
{
	if (LoadedBuildingId.IsEmpty() || !Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("PlaceConnectorNode: no building interior currently loaded"));
		return FString();
	}

	APolyBuilding* Building = Generator->FindBuildingById(LoadedBuildingId);
	if (!Building)
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
		// A freshly placed connector always starts unrotated/unscaled (0 degrees, 1x) - RotateConnectorGroup/
		// ScaleConnectorGroup adjust it afterward, once it's selected.
		if (!CanPlaceConnectorOnFloor(Type, Position, Level, 0.f, 1.f))
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
		SpawnConnectorMarker(NavNodes.Last(), Building);
	}

	UE_LOG(LogTemp, Warning, TEXT("Placed connector '%s' on %d floor(s)"), *GroupId, ConnectsToFloors.Num());

	RecordHistorySnapshot();
	return GroupId;
}

bool AInteriorGenerator::DeleteConnectorGroup(const FString& GroupId)
{
	for (FInteriorNavNode& Node : NavNodes)
	{
		if (Node.ConnectorGroupId == GroupId && Node.MarkerActor)
		{
			Node.MarkerActor->Destroy();
		}
	}

	const int32 RemovedCount = NavNodes.RemoveAll([&GroupId](const FInteriorNavNode& Node)
	{
		return Node.ConnectorGroupId == GroupId;
	});

	if (RemovedCount > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Deleted connector '%s' (%d node(s))"),
			*GroupId, RemovedCount);

		RecordHistorySnapshot();
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
	float RotationDegrees = 0.f;
	float SizeScale = 1.f;
	TArray<int32> CurrentFloors;
	for (const FInteriorNavNode& Node : NavNodes)
	{
		if (Node.ConnectorGroupId == GroupId)
		{
			Type = Node.Type;
			Position = Node.Position;
			RotationDegrees = Node.RotationDegrees;
			SizeScale = Node.SizeScale;
			CurrentFloors.Add(Node.Level);
		}
	}

	if (CurrentFloors.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("SetConnectorFloors: '%s' not found"), *GroupId);
		return false;
	}

	// Same all-or-nothing reasoning as PlaceConnectorNode - only the newly-added floors need
	// (re-)validating, a floor already in CurrentFloors already has a node sitting there. Uses the
	// connector's EXISTING rotation/scale (extracted above) - adding a floor to an already-rotated/
	// scaled connector shouldn't reset it back to defaults.
	for (int32 Level : NewConnectsToFloors)
	{
		if (!CurrentFloors.Contains(Level) && !CanPlaceConnectorOnFloor(Type, Position, Level, RotationDegrees, SizeScale, GroupId))
		{
			UE_LOG(LogTemp, Error, TEXT("SetConnectorFloors: '%s' unchanged"), *GroupId);
			return false;
		}
	}

	// Drop nodes for floors no longer in the list (destroying their 3D markers first).
	for (FInteriorNavNode& Node : NavNodes)
	{
		if (Node.ConnectorGroupId == GroupId && !NewConnectsToFloors.Contains(Node.Level) && Node.MarkerActor)
		{
			Node.MarkerActor->Destroy();
		}
	}
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
	APolyBuilding* Building = Generator ? Generator->FindBuildingById(LoadedBuildingId) : nullptr;
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
		NewNode.RotationDegrees = RotationDegrees;
		NewNode.SizeScale = SizeScale;
		NavNodes.Add(NewNode);
		SpawnConnectorMarker(NavNodes.Last(), Building);
	}
	UE_LOG(LogTemp, Warning,
		TEXT("Updated connector '%s' floors"),
		*GroupId);

	RecordHistorySnapshot();
	return true;
}

bool AInteriorGenerator::RotateConnectorGroup(const FString& GroupId, float DeltaDegrees)
{
	TArray<FInteriorNavNode*> GroupNodes;
	for (FInteriorNavNode& Node : NavNodes)
	{
		if (Node.ConnectorGroupId == GroupId)
		{
			GroupNodes.Add(&Node);
		}
	}
	if (GroupNodes.Num() == 0)
	{
		return false;
	}

	// Every floor instance shares one orientation (see FInteriorNavNode::RotationDegrees), so this
	// rotates them all together - GroupNodes[0] stands in for the group's shared Type/Position/scale.
	const float NewRotation = GroupNodes[0]->RotationDegrees + DeltaDegrees;
	const EInteriorNavNodeType Type = GroupNodes[0]->Type;
	const FVector2D Position = GroupNodes[0]->Position;
	const float SizeScale = GroupNodes[0]->SizeScale;

	// All-or-nothing across every floor the connector reaches, same reasoning as PlaceConnectorNode -
	// called every tick while a rotate key/button is held, so a silent no-op (no log spam) once any
	// floor would be blocked.
	for (const FInteriorNavNode* Node : GroupNodes)
	{
		if (!CanPlaceConnectorOnFloor(Type, Position, Node->Level, NewRotation, SizeScale, GroupId))
		{
			return false;
		}
	}

	APolyBuilding* Building = Generator ? Generator->FindBuildingById(LoadedBuildingId) : nullptr;
	for (FInteriorNavNode* Node : GroupNodes)
	{
		Node->RotationDegrees = NewRotation;
		RebuildConnectorMarker(*Node, Building);
	}
	NotifyNavigationLayoutChanged();
	return true;
}

bool AInteriorGenerator::ScaleConnectorGroup(const FString& GroupId, float DeltaScale)
{
	TArray<FInteriorNavNode*> GroupNodes;
	for (FInteriorNavNode& Node : NavNodes)
	{
		if (Node.ConnectorGroupId == GroupId)
		{
			GroupNodes.Add(&Node);
		}
	}
	if (GroupNodes.Num() == 0)
	{
		return false;
	}

	const float NewScale = FMath::Clamp(GroupNodes[0]->SizeScale + DeltaScale, MinConnectorScale, MaxConnectorScale);
	if (FMath::IsNearlyEqual(NewScale, GroupNodes[0]->SizeScale))
	{
		// Already at the min/max clamp - nothing to do (and nothing to log; Grow/Shrink buttons are
		// clicked repeatedly right up against the limit in normal use).
		return false;
	}

	const EInteriorNavNodeType Type = GroupNodes[0]->Type;
	const FVector2D Position = GroupNodes[0]->Position;
	const float RotationDegrees = GroupNodes[0]->RotationDegrees;

	for (const FInteriorNavNode* Node : GroupNodes)
	{
		if (!CanPlaceConnectorOnFloor(Type, Position, Node->Level, RotationDegrees, NewScale, GroupId))
		{
			UE_LOG(LogTemp, Error, TEXT("ScaleConnectorGroup: '%s' unchanged"), *GroupId);
			return false;
		}
	}

	APolyBuilding* Building = Generator ? Generator->FindBuildingById(LoadedBuildingId) : nullptr;
	for (FInteriorNavNode* Node : GroupNodes)
	{
		Node->SizeScale = NewScale;
		RebuildConnectorMarker(*Node, Building);
	}
	RecordHistorySnapshot();
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

				RecordHistorySnapshot();
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

			// Resize is a discrete per-press step (not a drag with its own release), so unlike
			// CommitRoomMove there's no separate "allowed mid-gesture" phase to speak of - each step
			// is checked and rejected immediately, same as the boundary check above.
			if (DoesFootprintBlockConnector(NewFootprint, Room->IsClosedLoop(), Pair.Key))
			{
				UE_LOG(LogTemp, Error, TEXT("ResizeRoom: '%s' at %.1f x %.1f would wall in an elevator/stairs/escalator"), *RoomId, NewWidth, NewDepth);
				return false;
			}

			Room->ResizeFootprint(NewFootprint);
			UE_LOG(LogTemp, Warning, TEXT("Resized %s to %.1f x %.1f"), *RoomId, NewWidth, NewDepth);
			RecordHistorySnapshot();
			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("ResizeRoom: '%s' not found"), *RoomId);
	return false;
}

bool AInteriorGenerator::RotateRoom(const FString& RoomId, float DeltaDegrees)
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
			const FVector2D Center = (MinPoint + MaxPoint) * 0.5f;

			const float Radians = FMath::DegreesToRadians(DeltaDegrees);
			const float CosA = FMath::Cos(Radians);
			const float SinA = FMath::Sin(Radians);

			TArray<FVector2D> NewFootprint;
			NewFootprint.Reserve(OldFootprint.Num());
			for (const FVector2D& Point : OldFootprint)
			{
				const FVector2D Offset = Point - Center;
				NewFootprint.Add(Center + FVector2D(
					Offset.X * CosA - Offset.Y * SinA,
					Offset.X * SinA + Offset.Y * CosA));
			}

			APolyBuilding* Building = Generator ? Generator->FindBuildingById(LoadedBuildingId) : nullptr;
			if (Building && !IsFootprintFullyInsidePolygon(NewFootprint, Building->GetFootprint()))
			{
				// Silently rejected, not logged as an error - this is called every tick while a
				// rotation key is held, so hitting the building's edge shouldn't spam the log.
				return false;
			}

			for (const AInteriorRoom* Other : Pair.Value.Rooms)
			{
				if (Other && Other != Room && DoFootprintsOverlap(NewFootprint, Room->IsClosedLoop(), Other->GetFootprint(), Other->IsClosedLoop(), OverlapToleranceWorldUnits / (Generator ? Generator->GetScale() : 1.f)))
				{
					// Same "no log spam" reasoning as the containment check above.
					return false;
				}
			}

			Room->ResizeFootprint(NewFootprint);
			NotifyNavigationLayoutChanged();
			return true;
		}
	}

	return false;
}

bool AInteriorGenerator::CommitRoomRotation(const FString& RoomId, const TArray<FVector2D>& FallbackFootprint)
{
	for (auto& Pair : RoomsByFloor)
	{
		for (AInteriorRoom* Room : Pair.Value.Rooms)
		{
			if (!Room || Room->RoomId != RoomId)
			{
				continue;
			}

			// Boundary and room/wall overlap are already enforced every tick by RotateRoom itself, so
			// unlike CommitRoomMove there's nothing to re-check there. Connectors are the one thing
			// RotateRoom intentionally allows passing through mid-gesture - checked only here, once the
			// rotation key is released, mirroring CommitRoomMove's connector check for a drag.
			if (DoesFootprintBlockConnector(Room->GetFootprint(), Room->IsClosedLoop(), Pair.Key))
			{
				Room->ResizeFootprint(FallbackFootprint);
				NotifyNavigationLayoutChanged();
				UE_LOG(LogTemp, Warning, TEXT("CommitRoomRotation: '%s' would wall in an elevator/stairs/escalator - reverted to its position before the rotation"), *RoomId);
				return false;
			}
			RecordHistorySnapshot();
			return true;
		}
	}

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
			NotifyNavigationLayoutChanged();
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

				if (DoFootprintsOverlap(Room->GetFootprint(), Room->IsClosedLoop(), Other->GetFootprint(), Other->IsClosedLoop(), OverlapToleranceWorldUnits / (Generator ? Generator->GetScale() : 1.f)))
				{
					Room->ResizeFootprint(FallbackFootprint);
					NotifyNavigationLayoutChanged();
					UE_LOG(LogTemp, Warning, TEXT("CommitRoomMove: '%s' would overlap '%s' - reverted to its position before the drag"), *RoomId, *Other->RoomId);
					return false;
				}
			}

			// Same "allowed while dragging, checked only on release" treatment as the overlap check
			// above.
			if (DoesFootprintBlockConnector(Room->GetFootprint(), Room->IsClosedLoop(), Pair.Key))
			{
				Room->ResizeFootprint(FallbackFootprint);
				NotifyNavigationLayoutChanged();
				UE_LOG(LogTemp, Warning, TEXT("CommitRoomMove: '%s' would wall in an elevator/stairs/escalator - reverted to its position before the drag"), *RoomId);
				return false;
			}

			RecordHistorySnapshot();
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
	// units as everything else here (see InteriorGen_IsPointInPolygon's callers elsewhere in this file for the
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
			// InputId is the door's own room - always known, never user-edited. A new door leads to the corridor (the building's
			// walkable space outside the rooms) until it is set to another room in the door's properties.
			Opening.InputId = RoomId;
			Opening.ConnectsTo = TEXT("corridor");

			Room->AddOpening(Opening);
			UE_LOG(LogTemp, Warning, TEXT("Added door %s to %s edge %d"), *Opening.Id, *RoomId, EdgeIndex);

			RecordHistorySnapshot();
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

					RecordHistorySnapshot();
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
					if (FMath::IsNearlyEqual(Opening.Offset, NewOffsetWorldUnits)) return true;
					Room->MoveOpening(DoorId, NewOffsetWorldUnits);
					NotifyNavigationLayoutChanged();
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

					RecordHistorySnapshot();
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
	// Names (not just ids) need to be unique building-wide - the door ConnectsTo dropdown and
	// SetSelectedDoorConnectsTo resolve a room purely by its current name, so two rooms/walls
	// sharing a name would make that lookup ambiguous.
	for (const auto& Pair : RoomsByFloor)
	{
		for (const AInteriorRoom* Other : Pair.Value.Rooms)
		{
			if (Other && Other->RoomId != RoomId && Other->RoomName == NewName)
			{
				UE_LOG(LogTemp, Error, TEXT("SetRoomName: '%s' is already used by '%s'"), *NewName, *Other->RoomId);
				return false;
			}
		}
	}

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
			RecordHistorySnapshot();
			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("SetRoomName: '%s' not found"), *RoomId);
	return false;
}

bool AInteriorGenerator::SetRoomTags(const FString& RoomId, const TArray<FString>& InTags)
{
	AInteriorRoom* Room = GetRoomById(RoomId);
	if (!Room || !Room->IsClosedLoop()) return false;
	const TArray<FString> Normalized = NavigationSearch::NormalizeTags(InTags);
	if (Room->RoomTags != Normalized)
	{
		Room->RoomTags = Normalized;
		RecordHistorySnapshot();
	}
	return true;
}

TArray<FString> AInteriorGenerator::GetRoomTags(const FString& RoomId) const
{
	const AInteriorRoom* Room = GetRoomById(RoomId);
	return Room ? Room->RoomTags : TArray<FString>();
}

namespace
{
	// The width of a door in the outer wall, and how close another building's wall must be to count as next to this one (metres).
	constexpr float EntranceWidthWorldUnits = 240.f;
	constexpr float AdjacentReachMetres = 1.5f;
}

void AInteriorGenerator::RefreshMirroredEntrances()
{
	AllEntrances = Entrances;
	if (Generator && !LoadedBuildingId.IsEmpty())
	{
		AllEntrances.Append(Generator->GetMirroredEntrances(LoadedBuildingId));
	}
}

const FInteriorEntrance* AInteriorGenerator::FindEntranceById(const FString& Id) const
{
	return AllEntrances.FindByPredicate([&Id](const FInteriorEntrance& E) { return E.Id == Id; });
}

FString AInteriorGenerator::AddEntranceAtPoint(const FVector2D& FootprintPoint, float ToleranceFootprintUnits, FString& OutError)
{
	OutError.Empty();
	APolyBuilding* Building = (!LoadedBuildingId.IsEmpty() && Generator) ? Generator->FindBuildingById(LoadedBuildingId) : nullptr;
	if (!Building)
	{
		OutError = TEXT("No building interior is currently loaded.");
		return FString();
	}
	const float Scale = Generator->GetScale();
	const float UnitsPerMetre = 100.f / Scale;
	const TArray<FVector2D>& Outline = Building->GetFootprint();

	// The nearest point of the outline.
	int32 Edge = INDEX_NONE;
	float Along = 0.f, EdgeLength = 0.f, Nearest = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Outline.Num(); ++i)
	{
		const FVector2D& A = Outline[i];
		const FVector2D& B = Outline[(i + 1) % Outline.Num()];
		const float Length = static_cast<float>(FVector2D::Distance(A, B));
		if (Length < KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const FVector2D Direction = (B - A) / Length;
		const float T = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(FootprintPoint - A, Direction)), 0.f, Length);
		const float Distance = static_cast<float>(FVector2D::Distance(FootprintPoint, A + Direction * T));
		if (Distance < Nearest)
		{
			Nearest = Distance;
			Edge = i;
			Along = T;
			EdgeLength = Length;
		}
	}
	if (Edge == INDEX_NONE || Nearest > FMath::Max(ToleranceFootprintUnits, 0.3f * UnitsPerMetre))
	{
		OutError = TEXT("Click on the building's outer wall to add a door there.");
		return FString();
	}

	const float Width = EntranceWidthWorldUnits / Scale;
	if (EdgeLength < Width + 0.2f * UnitsPerMetre)
	{
		OutError = TEXT("That stretch of wall is too short for a door.");
		return FString();
	}
	const float Start = FMath::Clamp(Along - Width * 0.5f, 0.1f * UnitsPerMetre, EdgeLength - Width - 0.1f * UnitsPerMetre);

	// Not on top of another door on this floor, from this building or a neighbour's mirrored one.
	for (const FInteriorEntrance& Existing : AllEntrances)
	{
		if (Existing.Level == ActiveFloor && Existing.EdgeIndex == Edge && Start < (Existing.Offset + Existing.Width) / Scale && Existing.Offset / Scale < Start + Width)
		{
			OutError = TEXT("There is already a door there.");
			return FString();
		}
	}

	// Along the span of the door, which other building's wall is next to this one? (Buildings share one footprint space once each is
	// shifted by its own offset.)
	const FVector2D EdgeStart = Outline[Edge];
	const FVector2D EdgeDirection = (Outline[(Edge + 1) % Outline.Num()] - EdgeStart).GetSafeNormal();
	const float Reach = AdjacentReachMetres * UnitsPerMetre;
	TSet<FString> Neighbours;
	int32 Samples = 0, Next = 0;
	for (float T = 0.f; T <= Width + KINDA_SMALL_NUMBER; T += FMath::Max(Width / 8.f, 0.05f * UnitsPerMetre))
	{
		++Samples;
		const FVector2D Point = EdgeStart + EdgeDirection * (Start + T);
		FString Closest;
		float ClosestDistance = Reach;
		for (const APolyBuilding* Other : Generator->GetAllBuildings())
		{
			if (!Other || Other == Building || Other->BuildingId == LoadedBuildingId)
			{
				continue;
			}
			const TArray<FVector2D>& Theirs = Other->GetFootprint();
			for (int32 i = 0; i < Theirs.Num(); ++i)
			{
				const FVector2D A = Theirs[i] + Other->BuildingOffset - Building->BuildingOffset;
				const FVector2D B = Theirs[(i + 1) % Theirs.Num()] + Other->BuildingOffset - Building->BuildingOffset;
				const float Distance = InteriorGen_DistancePointToSegment2D(Point, A, B);
				if (Distance <= ClosestDistance)
				{
					ClosestDistance = Distance;
					Closest = Other->BuildingId;
				}
			}
		}
		if (!Closest.IsEmpty())
		{
			++Next;
			Neighbours.Add(Closest);
		}
	}

	if (Next == 0)
	{
		// Away from every neighbour: the way out.
		if (ActiveFloor != 1)
		{
			OutError = TEXT("A main door (the way out of the building) can only be on the ground floor.");
			return FString();
		}
		FInteriorEntrance Door;
		Door.Id = FString::Printf(TEXT("%s-EN%02d"), *LoadedBuildingId, Entrances.Num() + 1);
		while (Entrances.ContainsByPredicate([&Door](const FInteriorEntrance& E) { return E.Id == Door.Id; }))
		{
			Door.Id += TEXT("x");
		}
		Door.Kind = EInteriorEntranceKind::Main;
		Door.Level = 1;
		Door.EdgeIndex = Edge;
		Door.Offset = Start * Scale;
		Door.Width = EntranceWidthWorldUnits;
		Entrances.Add(Door);
		RefreshMirroredEntrances();
		Generator->SetInteriorDataForBuilding(LoadedBuildingId, SerializeCurrentInterior());
		RecordHistorySnapshot();
		return Door.Id;
	}

	if (Next < Samples || Neighbours.Num() != 1)
	{
		OutError = Neighbours.Num() > 1
			? TEXT("That spot is next to more than one building. Put the door where it is next to just one.")
			: TEXT("A door has to be wholly where the two walls meet (a link door) or wholly away from every neighbour (a main door).");
		return FString();
	}

	// Wholly on the shared wall: a door to that building, which must have this floor and room for the matching door.
	const FString NeighbourId = *Neighbours.CreateConstIterator();
	const APolyBuilding* Neighbour = Generator->FindBuildingById(NeighbourId);
	if (!Neighbour || ActiveFloor > Neighbour->GetFloorCount())
	{
		OutError = FString::Printf(TEXT("%s has no floor %d, so there is nothing to connect to here."), *NeighbourId, ActiveFloor);
		return FString();
	}
	FInteriorEntrance Link;
	Link.Id = FString::Printf(TEXT("%s-EN%02d"), *LoadedBuildingId, Entrances.Num() + 1);
	while (Entrances.ContainsByPredicate([&Link](const FInteriorEntrance& E) { return E.Id == Link.Id; }))
	{
		Link.Id += TEXT("x");
	}
	Link.Kind = EInteriorEntranceKind::Link;
	Link.Level = ActiveFloor;
	Link.EdgeIndex = Edge;
	Link.Offset = Start * Scale;
	Link.Width = EntranceWidthWorldUnits;
	Link.ConnectsTo = NeighbourId;

	FInteriorEntrance Mirror;
	if (!Generator->MakeMirrorOfEntrance(LoadedBuildingId, Link, NeighbourId, Mirror))
	{
		OutError = FString::Printf(TEXT("%s has no stretch of wall long enough for the matching door."), *NeighbourId);
		return FString();
	}
	const FString MirrorError = FString::Printf(TEXT("%s already has a door at the matching spot."), *NeighbourId);
	for (const TArray<FInteriorEntrance>& Theirs : { Generator->GetStoredEntrances(NeighbourId), Generator->GetMirroredEntrances(NeighbourId) })
	{
		for (const FInteriorEntrance& Existing : Theirs)
		{
			if (Existing.Level == Mirror.Level && Existing.EdgeIndex == Mirror.EdgeIndex && Mirror.Offset < Existing.Offset + Existing.Width && Existing.Offset < Mirror.Offset + Mirror.Width)
			{
				OutError = MirrorError;
				return FString();
			}
		}
	}

	Entrances.Add(Link);
	RefreshMirroredEntrances();
	Generator->SetInteriorDataForBuilding(LoadedBuildingId, SerializeCurrentInterior());
	RecordHistorySnapshot();
	return Link.Id;
}

FString AInteriorGenerator::FindEntranceNear(const FVector2D& FootprintPoint, float ToleranceFootprintUnits) const
{
	const APolyBuilding* Building = (!LoadedBuildingId.IsEmpty() && Generator) ? Generator->FindBuildingById(LoadedBuildingId) : nullptr;
	if (!Building)
	{
		return FString();
	}
	const float Scale = Generator->GetScale();
	const TArray<FVector2D>& Outline = Building->GetFootprint();
	FString Best;
	float BestDistance = ToleranceFootprintUnits;
	for (const FInteriorEntrance& Entrance : AllEntrances)
	{
		if (Entrance.Level != ActiveFloor || !Outline.IsValidIndex(Entrance.EdgeIndex))
		{
			continue;
		}
		const FVector2D A = Outline[Entrance.EdgeIndex];
		const FVector2D Direction = (Outline[(Entrance.EdgeIndex + 1) % Outline.Num()] - A).GetSafeNormal();
		const float Distance = InteriorGen_DistancePointToSegment2D(FootprintPoint, A + Direction * (Entrance.Offset / Scale), A + Direction * ((Entrance.Offset + Entrance.Width) / Scale));
		if (Distance <= BestDistance)
		{
			BestDistance = Distance;
			Best = Entrance.Id;
		}
	}
	return Best;
}

bool AInteriorGenerator::RemoveEntrance(const FString& Id)
{
	const FInteriorEntrance* Found = FindEntranceById(Id);
	if (!Found || !Generator)
	{
		return false;
	}
	if (Found->bMirrored)
	{
		// The door belongs to the building next door; taking it from there takes it from both.
		const FString Source = Found->SourceBuildingId;
		if (!Generator->RemoveStoredEntrance(Source, Id))
		{
			return false;
		}
		RefreshMirroredEntrances();
		RecordHistorySnapshot();
		return true;
	}
	Entrances.RemoveAll([&Id](const FInteriorEntrance& E) { return E.Id == Id; });
	RefreshMirroredEntrances();
	Generator->SetInteriorDataForBuilding(LoadedBuildingId, SerializeCurrentInterior());
	RecordHistorySnapshot();
	return true;
}

bool AInteriorGenerator::GenerateFloor(int32 Seed, FFloorGenerationResult& OutResult)
{
	OutResult = FFloorGenerationResult();

	APolyBuilding* Building = (!LoadedBuildingId.IsEmpty() && Generator) ? Generator->FindBuildingById(LoadedBuildingId) : nullptr;
	if (!Building)
	{
		OutResult.Error = TEXT("No building interior is currently loaded.");
		return false;
	}
	if (!FloorInfoByLevel.Contains(ActiveFloor))
	{
		OutResult.Error = TEXT("The active floor doesn't exist.");
		return false;
	}
	const FInteriorRoomList* Existing = RoomsByFloor.Find(ActiveFloor);
	if (Existing && !Existing->Rooms.IsEmpty())
	{
		OutResult.Error = TEXT("This floor already has rooms or walls. Delete them first to generate it.");
		return false;
	}
	const float Scale = Generator->GetScale();
	if (Scale <= KINDA_SMALL_NUMBER)
	{
		OutResult.Error = TEXT("The project's scale is not valid.");
		return false;
	}

	// What the layout needs to know: the outline, this floor's doors in the outer wall, and the lifts and stairs that stand already.
	FFloorLayoutParams Params;
	Params.Seed = Seed;
	Params.UnitsPerMetre = 100.0 / Scale;
	Params.Outline = Building->GetFootprint();
	for (const FInteriorEntrance& Entrance : AllEntrances)
	{
		if (Entrance.Level == ActiveFloor && Params.Outline.IsValidIndex(Entrance.EdgeIndex))
		{
			FFloorDoor Door;
			Door.EdgeIndex = Entrance.EdgeIndex;
			Door.MiddleAlong = (Entrance.Offset + Entrance.Width * 0.5) / Scale;
			Door.Width = Entrance.Width / Scale;
			Params.Doors.Add(Door);
		}
	}
	TSet<FString> ExistingGroups;
	for (const FInteriorNavNode& Node : NavNodes)
	{
		TArray<FVector2D> Corners;
		GetConnectorFootprintCorners(Node.Type, Node.RotationDegrees, Node.SizeScale, Corners);
		if (Corners.Num() == 4)
		{
			for (FVector2D& Corner : Corners)
			{
				Corner = Node.Position + Corner / Scale;
			}
			Params.ConnectorFootprints.Add(Corners);
			Params.ConnectorCentres.Add(Node.Position);
			if (!Node.ConnectorGroupId.IsEmpty())
			{
				ExistingGroups.Add(Node.ConnectorGroupId);
			}
		}
	}
	Params.bMakeCore = ExistingGroups.IsEmpty();

	FFloorLayout Layout;
	if (!FloorLayout::Generate(Params, Layout, OutResult.Error))
	{
		return false;
	}

	const int32 Level = ActiveFloor;
	int32 HighestLevel = 0;
	for (const TPair<int32, FInteriorFloorInfo>& Pair : FloorInfoByLevel)
	{
		HighestLevel = FMath::Max(HighestLevel, Pair.Key);
	}

	bBulkEditing = true;
	// A new core: its lifts reach every floor and its stair the floors either side, as far as nothing blocks them on those floors.
	for (const FFloorConnector& Connector : Layout.Connectors)
	{
		TArray<int32> Floors;
		const bool bStair = Connector.Type != EInteriorNavNodeType::Elevator;
		for (int32 Floor = bStair ? FMath::Max(1, Level - 1) : 1; Floor <= (bStair ? FMath::Min(HighestLevel, Level + 1) : HighestLevel); ++Floor)
		{
			Floors.Add(Floor);
		}
		// Asked quietly first, so a floor where something is in the way is a "just this floor", not a line in the error log.
		for (int32 Floor : Floors)
		{
			if (!CanPlaceConnectorOnFloor(Connector.Type, Connector.Position, Floor, Connector.RotationDegrees, 1.f, FString(), false))
			{
				Floors = { Level };
				break;
			}
		}
		const FString GroupId = PlaceConnectorNode(Connector.Type, Connector.Position, Floors);
		if (!GroupId.IsEmpty())
		{
			if (!FMath::IsNearlyZero(Connector.RotationDegrees))
			{
				RotateConnectorGroup(GroupId, Connector.RotationDegrees);
			}
			++OutResult.ConnectorsPlaced;
		}
	}

	// Lifts and stairs the building already had are used as they are, and made to reach this floor.
	for (const FString& GroupId : ExistingGroups)
	{
		TArray<int32> Floors;
		for (const FInteriorNavNode& Node : NavNodes)
		{
			if (Node.ConnectorGroupId == GroupId)
			{
				Floors = Node.ConnectsFloors;
				break;
			}
		}
		if (!Floors.Contains(Level))
		{
			const FInteriorNavNode* Sample = NavNodes.FindByPredicate([&GroupId](const FInteriorNavNode& Node) { return Node.ConnectorGroupId == GroupId; });
			if (!Sample || !CanPlaceConnectorOnFloor(Sample->Type, Sample->Position, Level, Sample->RotationDegrees, Sample->SizeScale, GroupId, false))
			{
				continue;
			}
			Floors.Add(Level);
			Floors.Sort();
			if (SetConnectorFloors(GroupId, Floors))
			{
				++OutResult.ConnectorsReused;
			}
		}
		else
		{
			++OutResult.ConnectorsReused;
		}
	}

	int32 RoomNumber = 0;
	for (const FFloorRoom& Proposed : Layout.Rooms)
	{
		// What the creation function would refuse is skipped quietly, so a failed room is not an error in the log.
		if (!IsFootprintFullyInsidePolygon(Proposed.Footprint, Building->GetFootprint()) || DoesFootprintBlockConnector(Proposed.Footprint, true, Level))
		{
			++OutResult.RoomsSkipped;
			continue;
		}
		FString CreateError;
		const FString Name = FString::Printf(TEXT("%s %d-%02d"), *Proposed.Type, Level, ++RoomNumber);
		AInteriorRoom* Room = CreateRoomFromFootprintWithTags(Proposed.Footprint, FString(), Name, true, false, false, CreateError, { TEXT("generated") }, Proposed.Type);
		if (!Room)
		{
			++OutResult.RoomsSkipped;
			continue;
		}
		++OutResult.RoomsCreated;

		// The door onto the corridor, in the middle of the side that faces it.
		constexpr float DoorWidth = 90.f;
		const int32 Count = Proposed.Footprint.Num();
		const float EdgeLength = static_cast<float>(FVector2D::Distance(Proposed.Footprint[Proposed.DoorEdgeIndex], Proposed.Footprint[(Proposed.DoorEdgeIndex + 1) % Count])) * Scale;
		if (EdgeLength >= DoorWidth + 20.f)
		{
			const float Offset = FMath::Clamp(EdgeLength * Proposed.DoorFraction - DoorWidth * 0.5f, 0.f, EdgeLength - DoorWidth);
			if (!AddDoorToRoom(Room->RoomId, Proposed.DoorEdgeIndex, Offset).IsEmpty())
			{
				++OutResult.DoorsCreated;
			}
		}
	}

	bBulkEditing = false;
	RecordHistorySnapshot();
	return true;
}

void AInteriorGenerator::SetRoomColoursVisible(bool bVisible)
{
	bRoomColoursVisible = bVisible;
	for (TPair<int32, FInteriorRoomList>& Pair : RoomsByFloor)
	{
		for (AInteriorRoom* Room : Pair.Value.Rooms)
		{
			if (Room)
			{
				Room->SetTypeColourShown(bVisible);
			}
		}
	}
}

bool AInteriorGenerator::SetRoomType(const FString& RoomId, const FString& NewType)
{
	AInteriorRoom* Room = GetRoomById(RoomId);
	if (!Room || !Room->IsClosedLoop()) return false;
	const FString Normalized = RoomTypes::Normalize(NewType);
	if (Room->RoomType != Normalized)
	{
		Room->RoomType = Normalized;
		Room->RefreshFloorTint();
		RecordHistorySnapshot();
	}
	return true;
}

FString AInteriorGenerator::GetRoomType(const FString& RoomId) const
{
	const AInteriorRoom* Room = GetRoomById(RoomId);
	return Room ? Room->RoomType : RoomTypes::Default();
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

bool AInteriorGenerator::IsRoomClosedLoop(const FString& RoomId) const
{
	for (const auto& Pair : RoomsByFloor)
	{
		for (const AInteriorRoom* Room : Pair.Value.Rooms)
		{
			if (Room && Room->RoomId == RoomId)
			{
				return Room->IsClosedLoop();
			}
		}
	}

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
				TArray<TSharedPtr<FJsonValue>> SerializedTags;
				for (const FString& Tag : Room->RoomTags) SerializedTags.Add(MakeShared<FJsonValueString>(Tag));
				RoomObject->SetArrayField(TEXT("tags"), SerializedTags);
				if (Room->RoomType != RoomTypes::Default())
				{
					RoomObject->SetStringField(TEXT("roomType"), Room->RoomType);
				}
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

			// Omitted when still at their defaults (0/1), same "only write it if it's not the no-op
			// value" convention as ConnectsFloors/ConnectorGroupId above - a PathNode (or a connector
			// nobody's rotated/scaled yet) round-trips without picking up clutter.
			if (!FMath::IsNearlyZero(Node.RotationDegrees))
			{
				NodeObject->SetNumberField(TEXT("rotationDegrees"), Node.RotationDegrees);
			}
			if (!FMath::IsNearlyEqual(Node.SizeScale, 1.f))
			{
				NodeObject->SetNumberField(TEXT("sizeScale"), Node.SizeScale);
			}

			NavNodesArray.Add(MakeShared<FJsonValueObject>(NodeObject));
		}
		FloorObject->SetArrayField(TEXT("navNodes"), NavNodesArray);

		FloorsArray.Add(MakeShared<FJsonValueObject>(FloorObject));
	}

	InteriorObject->SetArrayField(TEXT("floors"), FloorsArray);

	if (!Entrances.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> EntranceArray;
		for (const FInteriorEntrance& Entrance : Entrances)
		{
			EntranceArray.Add(MakeShared<FJsonValueObject>(InteriorEntrances::ToJson(Entrance)));
		}
		InteriorObject->SetArrayField(TEXT("entrances"), EntranceArray);
	}
	return InteriorObject;
}

const TArray<AInteriorRoom*>& AInteriorGenerator::GetRoomsOnFloor(int32 Level) const
{
	static const TArray<AInteriorRoom*> Empty;
	const FInteriorRoomList* List = RoomsByFloor.Find(Level);
	return List ? List->Rooms : Empty;
}
AInteriorRoom* AInteriorGenerator::GetRoomById(const FString& RoomId) const
{
	for (const auto& FloorPair : RoomsByFloor)
	{
		for (AInteriorRoom* Room : FloorPair.Value.Rooms)
		{
			if (Room && Room->RoomId == RoomId)
			{
				return Room;
			}
		}
	}

	return nullptr;
}
TSharedPtr<FJsonObject> AInteriorGenerator::CreateTranslatedInteriorCopy(
	const TSharedPtr<FJsonObject>& SourceInterior,
	const FVector2D& MoveOffset
) const
{
	if (!SourceInterior.IsValid())
	{
		return nullptr;
	}

	// Deep-copy the JSON first so the pasted building does not
	// share its interior JSON with the original building.
	FString JsonString;

	TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&JsonString);

	if (!FJsonSerializer::Serialize(SourceInterior.ToSharedRef(), Writer))
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> NewInterior;

	TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, NewInterior) ||
		!NewInterior.IsValid())
	{
		return nullptr;
	}

	// Get all floors.
	const TArray<TSharedPtr<FJsonValue>>* FloorsArray = nullptr;

	if (!NewInterior->TryGetArrayField(TEXT("floors"), FloorsArray))
	{
		return NewInterior;
	}

	for (const TSharedPtr<FJsonValue>& FloorValue : *FloorsArray)
	{
		TSharedPtr<FJsonObject> FloorObject = FloorValue->AsObject();

		if (!FloorObject.IsValid())
		{
			continue;
		}

		// -------------------------
		// Move rooms / walls
		// -------------------------
		const TArray<TSharedPtr<FJsonValue>>* RoomsArray = nullptr;

		if (FloorObject->TryGetArrayField(TEXT("rooms"), RoomsArray))
		{
			for (const TSharedPtr<FJsonValue>& RoomValue : *RoomsArray)
			{
				TSharedPtr<FJsonObject> RoomObject = RoomValue->AsObject();

				if (!RoomObject.IsValid())
				{
					continue;
				}

				const TArray<TSharedPtr<FJsonValue>>* FootprintArray = nullptr;

				if (!RoomObject->TryGetArrayField(
					TEXT("footprint"),
					FootprintArray))
				{
					continue;
				}

				TArray<TSharedPtr<FJsonValue>> NewFootprint;

				for (const TSharedPtr<FJsonValue>& PointValue : *FootprintArray)
				{
					const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;

					if (PointValue->TryGetArray(Pair) && Pair->Num() >= 2)
					{
						TArray<TSharedPtr<FJsonValue>> NewPair;

						NewPair.Add(
							MakeShared<FJsonValueNumber>(
								(*Pair)[0]->AsNumber() + MoveOffset.X));

						NewPair.Add(
							MakeShared<FJsonValueNumber>(
								(*Pair)[1]->AsNumber() + MoveOffset.Y));

						NewFootprint.Add(
							MakeShared<FJsonValueArray>(NewPair));
					}
				}

				RoomObject->SetArrayField(
					TEXT("footprint"),
					NewFootprint);
			}
		}

		// -------------------------
		// Move nav nodes/connectors
		// -------------------------
		const TArray<TSharedPtr<FJsonValue>>* NavNodesArray = nullptr;

		if (FloorObject->TryGetArrayField(TEXT("navNodes"), NavNodesArray))
		{
			for (const TSharedPtr<FJsonValue>& NodeValue : *NavNodesArray)
			{
				TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();

				if (!NodeObject.IsValid())
				{
					continue;
				}

				const TArray<TSharedPtr<FJsonValue>>* PositionArray = nullptr;

				if (NodeObject->TryGetArrayField(
					TEXT("position"),
					PositionArray) &&
					PositionArray->Num() >= 2)
				{
					TArray<TSharedPtr<FJsonValue>> NewPosition;

					NewPosition.Add(
						MakeShared<FJsonValueNumber>(
							(*PositionArray)[0]->AsNumber() + MoveOffset.X));

					NewPosition.Add(
						MakeShared<FJsonValueNumber>(
							(*PositionArray)[1]->AsNumber() + MoveOffset.Y));

					NodeObject->SetArrayField(
						TEXT("position"),
						NewPosition);
				}
			}
		}
	}

	return NewInterior;
}
void AInteriorGenerator::SyncInteriorToBuildingGenerator()
{
	if (LoadedBuildingId.IsEmpty() || !Generator)
	{
		return;
	}

	Generator->SetInteriorDataForBuilding(LoadedBuildingId, SerializeCurrentInterior(), false);
}
void AInteriorGenerator::NotifyNavigationLayoutChanged()
{
	++RoomSearchRevision;
	if (!bLoadingInterior && !bBulkEditing && !bIsRestoringHistory)
	{
		++NavigationRevision;
		OnNavigationLayoutChanged.Broadcast();
	}
}

void AInteriorGenerator::RecordHistorySnapshot()
{
	// Only a new snapshot is an edit: selecting a door can also finish a gesture and record
	// the same state. Live previews notify separately, before their eventual commit.
	if (bIsRestoringHistory || bBulkEditing || LoadedBuildingId.IsEmpty())
	{
		++RoomSearchRevision;
		return;
	}

	// Convert the current live interior into JSON.
	TSharedRef<FJsonObject> InteriorObject = SerializeCurrentInterior();

	FString Snapshot;
	TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&Snapshot);

	if (!FJsonSerializer::Serialize(InteriorObject, Writer))
	{
		UE_LOG(LogTemp, Error,
			TEXT("RecordHistorySnapshot: failed to serialize interior"));
		return;
	}

	// Don't store the exact same state twice.
	if (HistoryIndex >= 0 &&
		InteriorHistory.IsValidIndex(HistoryIndex) &&
		InteriorHistory[HistoryIndex] == Snapshot)
	{
		return;
	}

	// If we Undo and then make a new edit, the old Redo path
	// must be discarded.
	if (HistoryIndex < InteriorHistory.Num() - 1)
	{
		InteriorHistory.RemoveAt(
			HistoryIndex + 1,
			InteriorHistory.Num() - HistoryIndex - 1);
	}

	InteriorHistory.Add(Snapshot);
	HistoryIndex = InteriorHistory.Num() - 1;
	NotifyNavigationLayoutChanged();

	UE_LOG(LogTemp, Log,
		TEXT("History snapshot recorded. Index=%d Total=%d"),
		HistoryIndex,
		InteriorHistory.Num());
}


bool AInteriorGenerator::Undo()
{
	// Need an earlier state to go back to.
	if (HistoryIndex <= 0 ||
		!InteriorHistory.IsValidIndex(HistoryIndex - 1))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Undo: nothing to undo"));
		return false;
	}

	if (!Generator || LoadedBuildingId.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Undo: no interior is currently loaded"));
		return false;
	}

	const FString BuildingId = LoadedBuildingId;
	const int32 PreviousActiveFloor = ActiveFloor;

	const int32 TargetIndex = HistoryIndex - 1;
	const FString& Snapshot = InteriorHistory[TargetIndex];

	TSharedPtr<FJsonObject> RestoredInterior;
	TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Snapshot);

	if (!FJsonSerializer::Deserialize(Reader, RestoredInterior) ||
		!RestoredInterior.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("Undo: failed to deserialize history snapshot"));
		return false;
	}

	bIsRestoringHistory = true;

	Generator->SetInteriorDataForBuilding(
		BuildingId,
		RestoredInterior);

	const int32 LoadResult =
		LoadInteriorForBuilding(BuildingId);

	if (LoadResult < 0)
	{
		bIsRestoringHistory = false;

		UE_LOG(LogTemp, Error,
			TEXT("Undo: failed to restore interior"));
		return false;
	}

	HistoryIndex = TargetIndex;

	// Try to remain on the same floor.
	if (FloorInfoByLevel.Contains(PreviousActiveFloor))
	{
		SetActiveFloor(PreviousActiveFloor);
	}

	bIsRestoringHistory = false;
	NotifyNavigationLayoutChanged();

	UE_LOG(LogTemp, Log,
		TEXT("Undo complete. Index=%d"),
		HistoryIndex);

	return true;
}


bool AInteriorGenerator::Redo()
{
	const int32 TargetIndex = HistoryIndex + 1;

	if (!InteriorHistory.IsValidIndex(TargetIndex))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Redo: nothing to redo"));
		return false;
	}

	if (!Generator || LoadedBuildingId.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Redo: no interior is currently loaded"));
		return false;
	}

	const FString BuildingId = LoadedBuildingId;
	const int32 PreviousActiveFloor = ActiveFloor;

	const FString& Snapshot = InteriorHistory[TargetIndex];

	TSharedPtr<FJsonObject> RestoredInterior;
	TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Snapshot);

	if (!FJsonSerializer::Deserialize(Reader, RestoredInterior) ||
		!RestoredInterior.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("Redo: failed to deserialize history snapshot"));
		return false;
	}

	bIsRestoringHistory = true;

	Generator->SetInteriorDataForBuilding(
		BuildingId,
		RestoredInterior);

	const int32 LoadResult =
		LoadInteriorForBuilding(BuildingId);

	if (LoadResult < 0)
	{
		bIsRestoringHistory = false;

		UE_LOG(LogTemp, Error,
			TEXT("Redo: failed to restore interior"));
		return false;
	}

	HistoryIndex = TargetIndex;

	if (FloorInfoByLevel.Contains(PreviousActiveFloor))
	{
		SetActiveFloor(PreviousActiveFloor);
	}

	bIsRestoringHistory = false;
	NotifyNavigationLayoutChanged();

	UE_LOG(LogTemp, Log,
		TEXT("Redo complete. Index=%d"),
		HistoryIndex);

	return true;
}


void AInteriorGenerator::ClearHistory()
{
	InteriorHistory.Empty();
	HistoryIndex = -1;
	bIsRestoringHistory = false;

	UE_LOG(LogTemp, Log,
		TEXT("Undo/Redo history cleared"));
}
