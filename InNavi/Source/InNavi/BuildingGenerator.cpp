// Fill out your copyright notice in the Description page of Project Settings.


#include "BuildingGenerator.h"
#include "BuildingTypes.h"
#include "GroundFit.h"
#include "LayoutValidation.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "NavigationSearchUtils.h"
#include "PolyBuilding.h"
#include "ProjectFile.h"
#include "RoadSegmentActor.h"
#include "POIMarkerActor.h"
#include "ContextBuildingActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "SQLiteDatabase.h"

static bool CheckProjectFloorLimits(const TArray<TSharedPtr<FJsonValue>>& Buildings, FString& OutProblem)
{
	const auto IsValidFloor = [](const FJsonObject& Object, const TCHAR* Field)
	{
		double Value = 0;
		return !Object.HasField(Field) || (Object.TryGetNumberField(Field, Value)
			&& FMath::IsFinite(Value) && Value >= 1 && Value <= LayoutValidation::MaxBuildingFloors
			&& Value == FMath::FloorToDouble(Value));
	};
	for (const auto& Value : Buildings)
	{
		const TSharedPtr<FJsonObject>* Building = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Building) || !Building->IsValid()) continue;
		bool bValid = IsValidFloor(**Building, TEXT("floorCount"));
		const TSharedPtr<FJsonObject>* Interior = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Floors = nullptr;
		if (bValid && (*Building)->TryGetObjectField(TEXT("interior"), Interior) && Interior->IsValid()
			&& (*Interior)->TryGetArrayField(TEXT("floors"), Floors))
		{
			for (const auto& FloorValue : *Floors)
			{
				const TSharedPtr<FJsonObject>* Floor = nullptr;
				if (FloorValue.IsValid() && FloorValue->TryGetObject(Floor) && Floor->IsValid() && !IsValidFloor(**Floor, TEXT("level")))
				{
					bValid = false;
					break;
				}
			}
		}
		if (!bValid)
		{
			FString Id;
			(*Building)->TryGetStringField(TEXT("id"), Id);
			OutProblem = FString::Printf(TEXT("Building '%s' must use whole floor counts and levels from 1 to %d."), *Id, LayoutValidation::MaxBuildingFloors);
			return false;
		}
	}
	return true;
}

// ---- Helper: do segments AB and CD properly cross each other? ---- Named distinctly (not just
// "SegmentsIntersect") because NavigationRoutePlannerTests.cpp defines its own same-named,
// same-signature helper in its own anonymous namespace - both being `static`/internal-linkage
// normally keeps them apart, but Unreal's adaptive-unity build can still merge unrelated .cpp files
// into one translation unit (it only excludes files with LOCAL uncommitted changes, which is why
// this went unnoticed until a from-clean CI build - the fresh checkout has nothing "changed" to
// exclude), and two same-named functions landing in one merged unity file is a real, if easy to
// miss, redefinition error.
static bool FootprintSegmentsIntersect(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
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

// ---- Helper: shortest distance from point P to segment AB ----
static float PointToSegmentDistance(const FVector2D& P, const FVector2D& A, const FVector2D& B)
{
	const FVector2D AB = B - A;
	const float LenSq = AB.SizeSquared();
	if (LenSq <= KINDA_SMALL_NUMBER)
	{
		return FVector2D::Distance(P, A);
	}
	const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LenSq, 0.f, 1.f);
	return FVector2D::Distance(P, A + AB * T);
}

// ---- Helper: shortest distance between segments AB and CD (0 if they cross) ----
static float SegmentToSegmentDistance(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
{
	if (FootprintSegmentsIntersect(A, B, C, D))
	{
		return 0.f;
	}
	return FMath::Min(
		FMath::Min(PointToSegmentDistance(A, C, D), PointToSegmentDistance(B, C, D)),
		FMath::Min(PointToSegmentDistance(C, A, B), PointToSegmentDistance(D, A, B)));
}

// ---- Helper: are two building footprints (same world-space frame) physically the same connected
// structure - touching/overlapping, or close enough to cover a tiny OSM mapping gap? ----
static bool FootprintsAreLinked(const TArray<FVector2D>& FootprintA, const TArray<FVector2D>& FootprintB, float ToleranceWorldUnits)
{
	for (int32 i = 0; i < FootprintA.Num(); ++i)
	{
		const FVector2D& A0 = FootprintA[i];
		const FVector2D& A1 = FootprintA[(i + 1) % FootprintA.Num()];
		for (int32 j = 0; j < FootprintB.Num(); ++j)
		{
			const FVector2D& B0 = FootprintB[j];
			const FVector2D& B1 = FootprintB[(j + 1) % FootprintB.Num()];
			if (SegmentToSegmentDistance(A0, A1, B0, B1) <= ToleranceWorldUnits)
			{
				return true;
			}
		}
	}
	return false;
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
			// Skip edges that share a vertex with edge i - they touch by construction, not a crossing.
			if (j == iNext || jNext == i)
			{
				continue;
			}

			if (FootprintSegmentsIntersect(Poly[i], Poly[iNext], Poly[j], Poly[jNext]))
			{
				return true;
			}
		}
	}
	return false;
}

ABuildingGenerator::ABuildingGenerator()
{
	PrimaryActorTick.bCanEverTick = false;
}

static bool ReadProjectScale(const FJsonObject& Project, float& OutScale, FString& OutProblem)
{
	double Value = 1.0;
	if (Project.HasField(TEXT("scale")) && (!Project.HasTypedField<EJson::Number>(TEXT("scale"))
		|| !Project.TryGetNumberField(TEXT("scale"), Value) || !FMath::IsFinite(Value)
		|| Value <= KINDA_SMALL_NUMBER || Value > TNumericLimits<float>::Max()))
	{
		OutProblem = TEXT("The project scale must be a positive number within the supported range.");
		return false;
	}
	OutScale = static_cast<float>(Value);
	return true;
}

static FString BuildingProjectRevision(const FString& Text)
{
	const FTCHARToUTF8 Utf8(*Text, Text.Len());
	return FMD5::HashBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}

static bool ReadBuildingFloorHeight(const FJsonObject& Object, float Default, float& OutHeight)
{
	double Height = Default;
	if (Object.HasField(TEXT("floorHeight")) && (!Object.HasTypedField<EJson::Number>(TEXT("floorHeight"))
		|| !Object.TryGetNumberField(TEXT("floorHeight"), Height) || !FMath::IsFinite(Height)
		|| Height <= UE_SMALL_NUMBER || Height > TNumericLimits<float>::Max()))
	{
		return false;
	}
	OutHeight = static_cast<float>(Height);
	return true;
}

static bool ReadBuildingProjectPoint(const TSharedPtr<FJsonValue>& Value, FVector2D& Point)
{
	const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
	return Value && Value->TryGetArray(Pair) && Pair->Num() >= 2
		&& (*Pair)[0] && (*Pair)[0]->Type == EJson::Number && (*Pair)[0]->TryGetNumber(Point.X) && FMath::IsFinite(Point.X)
		&& (*Pair)[1] && (*Pair)[1]->Type == EJson::Number && (*Pair)[1]->TryGetNumber(Point.Y) && FMath::IsFinite(Point.Y);
}

static bool ReadProjectRecords(const FJsonObject& Owner, const TCHAR* Field, TArray<TSharedPtr<FJsonObject>>& Out)
{
	Out.Reset();
	if (!Owner.HasField(Field)) return true;
	const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
	if (!Owner.TryGetArrayField(Field, Records)) return false;
	for (const auto& Value : *Records)
	{
		const TSharedPtr<FJsonObject>* Record = nullptr;
		if (!Value || !Value->TryGetObject(Record) || !Record->IsValid()) return false;
		Out.Add(*Record);
	}
	return true;
}

static bool CanLoadInteriorRecords(const FJsonObject& Building)
{
	if (!Building.HasField(TEXT("interior"))) return true;
	const TSharedPtr<FJsonObject>* Interior = nullptr;
	if (!Building.TryGetObjectField(TEXT("interior"), Interior) || !Interior->IsValid()) return false;

	TArray<TSharedPtr<FJsonObject>> Floors, Rooms, Records;
	if (!ReadProjectRecords(**Interior, TEXT("floors"), Floors)
		|| !ReadProjectRecords(**Interior, TEXT("entrances"), Records)) return false;
	TSet<int32> Levels;
	TSet<FString> RoomIds;
	for (const auto& Floor : Floors)
	{
		int32 Level = 1;
		Floor->TryGetNumberField(TEXT("level"), Level);
		if (Levels.Contains(Level) || !ReadProjectRecords(*Floor, TEXT("rooms"), Rooms)
			|| !ReadProjectRecords(*Floor, TEXT("navNodes"), Records)) return false;
		Levels.Add(Level);
		for (const auto& Room : Rooms)
		{
			FString Id;
			bool bClosed = true;
			const TArray<TSharedPtr<FJsonValue>>* Footprint = nullptr;
			if (!Room->TryGetStringField(TEXT("id"), Id) || Id.TrimStartAndEnd().IsEmpty() || RoomIds.Contains(Id)
				|| (Room->HasField(TEXT("closed")) && !Room->TryGetBoolField(TEXT("closed"), bClosed))
				|| !Room->TryGetArrayField(TEXT("footprint"), Footprint) || Footprint->Num() < (bClosed ? 3 : 2)
				|| !ReadProjectRecords(*Room, TEXT("openings"), Records)) return false;
			for (const auto& Value : *Footprint)
			{
				FVector2D Point;
				if (!ReadBuildingProjectPoint(Value, Point)) return false;
			}
			RoomIds.Add(Id);
		}
	}
	return true;
}

static TSharedPtr<FJsonObject> ReadBuildingProjectFile(const FString& FilePath, FString& OutProblem, FString* OutRevision = nullptr)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		OutProblem = TEXT("That file could not be read.");
		return nullptr;
	}
	if (OutRevision) *OutRevision = BuildingProjectRevision(JsonString);

	TSharedPtr<FJsonObject> RootObject;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonString), RootObject) || !RootObject.IsValid())
	{
		OutProblem = TEXT("That file is not valid JSON.");
		return nullptr;
	}
	return RootObject;
}

int32 ABuildingGenerator::LoadBuildingsFromFile(const FString& FilePath)
{
	FString Problem;
	FString Revision;
	const TSharedPtr<FJsonObject> RootObject = ReadBuildingProjectFile(FilePath, Problem, &Revision);
	if (!RootObject)
	{
		UE_LOG(LogTemp, Error, TEXT("Could not load %s: %s"), *FilePath, *Problem);
		return 0;
	}
	const int32 Count = LoadBuildingsFromJson(RootObject);
	LoadSceneryFromJson(RootObject);
	const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
	if (RootObject->TryGetArrayField(TEXT("buildings"), Buildings) && Count == Buildings->Num())
	{
		ProjectRevisionPath = FPaths::ConvertRelativePathToFull(FilePath);
		ProjectRevision = MoveTemp(Revision);
	}
	return Count;
}

int32 ABuildingGenerator::LoadBuildingsFromJson(const TSharedPtr<FJsonObject>& RootObject)
{
	if (!RootObject.IsValid()) return 0;
	const TArray<TSharedPtr<FJsonValue>>* BuildingsArray;
	if (!RootObject->TryGetArrayField(TEXT("buildings"), BuildingsArray))
	{
		UE_LOG(LogTemp, Error, TEXT("No 'buildings' array found in JSON"));
		return 0;
	}
	FString Problem;
	float ProjectScale = 1.f;
	if (!ReadProjectScale(*RootObject, ProjectScale, Problem) || !CheckProjectFloorLimits(*BuildingsArray, Problem))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Problem);
		return 0;
	}
	float GlobalFloorHeight = 300.f;
	if (!ReadBuildingFloorHeight(*RootObject, 300.f, GlobalFloorHeight))
	{
		UE_LOG(LogTemp, Warning, TEXT("The project floor height must be a positive number."));
		return 0;
	}
	if (!GetWorld()) return 0;

	// No "version" means a file from before versions existed (read as 0, the same format as 1). A newer file is read as well as it can be.
	double Version = 0;
	RootObject->TryGetNumberField(TEXT("version"), Version);
	LoadedFileVersion = Version >= BasicProjectFileVersion
		? static_cast<int32>(FMath::Min(FMath::CeilToDouble(Version), double(MAX_int32)))
		: 0;
	if (LoadedFileVersion > ProjectFileVersion)
	{
		UE_LOG(LogTemp, Warning, TEXT("Project file version %d is newer than this build reads (%d); loading what it can"), LoadedFileVersion, ProjectFileVersion);
	}

	Scale = ProjectScale;

	// A file made before projects had a name carries neither field.
	ProjectName.Reset();
	ProjectDescription.Reset();
	RootObject->TryGetStringField(TEXT("projectName"), ProjectName);
	RootObject->TryGetStringField(TEXT("description"), ProjectDescription);

	int32 SpawnedCount = 0;

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

		float FloorHeight = GlobalFloorHeight;
		if (!ReadBuildingFloorHeight(*B, GlobalFloorHeight, FloorHeight)) continue;

		FVector2D Offset(0.f, 0.f);
		if (B->HasField(TEXT("offset")) && !ReadBuildingProjectPoint(B->TryGetField(TEXT("offset")), Offset)) continue;

		const TArray<TSharedPtr<FJsonValue>>* FootprintArray;
		if (!B->TryGetArrayField(TEXT("footprint"), FootprintArray))
		{
			UE_LOG(LogTemp, Warning, TEXT("Building %s has no footprint"), *Id);
			continue;
		}

		TArray<FVector2D> Footprint;
		for (const TSharedPtr<FJsonValue>& PointValue : *FootprintArray)
		{
			FVector2D Point;
			if (!ReadBuildingProjectPoint(PointValue, Point))
			{
				Footprint.Reset();
				break;
			}
			Footprint.Add(Point);
		}

		if (APolyBuilding* Spawned = SpawnBuildingFromData(Id, Name, Footprint, FloorCount, FloorHeight, Offset))
		{
			SpawnedCount++;

			// A file made before building types has none: the building is Other.
			FString BuildingType;
			if (B->TryGetStringField(TEXT("buildingType"), BuildingType))
			{
				Spawned->SetBuildingType(BuildingType);
				RefreshBuildingFilterFor(*Spawned);
			}

			const TSharedPtr<FJsonObject>* InteriorObject;
			if (B->TryGetObjectField(TEXT("interior"), InteriorObject))
			{
				InteriorDataById.Add(Id, *InteriorObject);
			}
		}
	}

	if (!bNavigationGeometryOnly)
	{
		RefreshWallOpenings();
		MarkGroundDirty();
	}
	NotifyNavigationLayoutChanged();
	return SpawnedCount;
}

int32 ABuildingGenerator::LoadBuildingsFromDatabase(const FString& DatabasePath)
{
	FSQLiteDatabase Database;
	if (!Database.Open(*DatabasePath, ESQLiteDatabaseOpenMode::ReadOnly))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not open SQLite database at: %s"), *DatabasePath);
		return 0;
	}

	// One building's accumulated data - LatLonPoints is built up in sequence order by the query's own
	// ORDER BY, so no separate node_id/sequence bookkeeping is needed here.
	struct FDatabaseBuildingRow
	{
		FString BuildingType;
		TArray<FVector2D> LatLonPoints; // X = lon, Y = lat
	};
	TMap<int64, FDatabaseBuildingRow> RowsById;

	// Every building, regardless of type - unlike an earlier, WHERE-filtered version of this query, a
	// building not tagged 'university' can still turn out to be part of the real campus (see the
	// adjacency pass below), so classification now happens here in C++, not in the query itself.
	const int64 NumRowsRead = Database.Execute(
		TEXT("SELECT b.building_id, b.building_type, bn.lat, bn.lon FROM buildings b ")
		TEXT("JOIN building_nodes bn ON b.building_id = bn.building_id ")
		TEXT("ORDER BY b.building_id, bn.sequence"),
		[&RowsById](const FSQLitePreparedStatement& Statement) -> ESQLitePreparedStatementExecuteRowResult
		{
			int64 BuildingId = 0;
			FString BuildingType;
			double Lat = 0.0, Lon = 0.0;
			if (!Statement.GetColumnValueByName(TEXT("building_id"), BuildingId)
				|| !Statement.GetColumnValueByName(TEXT("building_type"), BuildingType)
				|| !Statement.GetColumnValueByName(TEXT("lat"), Lat)
				|| !Statement.GetColumnValueByName(TEXT("lon"), Lon))
			{
				return ESQLitePreparedStatementExecuteRowResult::Error;
			}

			FDatabaseBuildingRow& Row = RowsById.FindOrAdd(BuildingId);
			Row.BuildingType = BuildingType;
			// Doubles: a float holds a longitude of 104 degrees only to about 0.4 m, and corners of a building that are a few
			// centimetres apart ran together.
			Row.LatLonPoints.Add(FVector2D(Lon, Lat));
			return ESQLitePreparedStatementExecuteRowResult::Continue;
		});
	Database.Close();

	if (NumRowsRead == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("Query failed while reading buildings from database at: %s"), *DatabasePath);
		return 0;
	}

	if (RowsById.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Database at %s contained no buildings"), *DatabasePath);
		return 0;
	}

	if (!GetWorld())
	{
		return 0;
	}

	// Reference point for the local-meters projection: the centroid of the 'university'-tagged nodes
	// only (not every building now being read), so the campus still lands near the world origin
	// regardless of where on Earth it actually sits (same equirectangular approximation already
	// validated by hand against this exact dataset - see project memory).
	double SumLat = 0.0, SumLon = 0.0;
	int32 TotalPoints = 0;
	for (const TPair<int64, FDatabaseBuildingRow>& Pair : RowsById)
	{
		if (Pair.Value.BuildingType != TEXT("university"))
		{
			continue;
		}
		for (const FVector2D& LonLat : Pair.Value.LatLonPoints)
		{
			SumLon += LonLat.X;
			SumLat += LonLat.Y;
			TotalPoints++;
		}
	}
	if (TotalPoints == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Database at %s contained no 'university'-tagged buildings to anchor the campus on"), *DatabasePath);
		return 0;
	}
	const double RefLat = SumLat / TotalPoints;
	const double RefLon = SumLon / TotalPoints;

	// Stashed for LoadRoadsAndPOIsFromDatabase/LoadContextBuildingsFromDatabase - see their own
	// comments on why they need this exact same reference point rather than computing their own.
	DatabaseRefLat = RefLat;
	DatabaseRefLon = RefLon;
	bHasDatabaseRefPoint = true;

	constexpr double MetersPerDegreeLat = 111320.0;
	const double MetersPerDegreeLon = MetersPerDegreeLat * FMath::Cos(FMath::DegreesToRadians(RefLat));

	// This data is the real campus at real scale (unlike buildings.json's hand-authored, non-
	// geographic footprint units) - Scale=100 makes SpawnBuildingFromData's Footprint*Scale land
	// directly in UE's cm-per-unit convention, so footprint units below can just be real meters with
	// no further distortion.
	Scale = 100.f;

	// Project every building's footprint into the same world-space frame up front - needed for the
	// adjacency test below regardless of a building's own type, and reused again when actually
	// spawning the ones that make the cut.
	TMap<int64, TArray<FVector2D>> WorldFootprintsById;
	WorldFootprintsById.Reserve(RowsById.Num());
	for (const TPair<int64, FDatabaseBuildingRow>& Pair : RowsById)
	{
		TArray<FVector2D> Footprint;
		Footprint.Reserve(Pair.Value.LatLonPoints.Num());
		for (const FVector2D& LonLat : Pair.Value.LatLonPoints)
		{
			const double X = (LonLat.X - RefLon) * MetersPerDegreeLon;
			const double Y = (LonLat.Y - RefLat) * MetersPerDegreeLat;
			Footprint.Add(FVector2D((float)X, (float)Y));
		}
		// OSM closes a way by repeating its first node as the last one - SpawnBuildingFromData (and
		// the adjacency test's own edge-wrapping) treat this as an already-open polygon, so strip the
		// duplicate first.
		if (Footprint.Num() > 1 && Footprint[0].Equals(Footprint.Last(), 0.5f))
		{
			Footprint.RemoveAt(Footprint.Num() - 1);
		}
		WorldFootprintsById.Add(Pair.Key, MoveTemp(Footprint));
	}

	// 'university' is the OSM tag that happens to land on the buildings actually inside the campus
	// footprint (see project memory) - but it doesn't cover every building that's really part of the
	// same physical campus structure: covered walkways/bridges between buildings, and a handful of
	// buildings mapped under a different type, come back untagged even though they visually belong to
	// the same connected complex. Grow the campus set from the tagged seed by following physical
	// adjacency instead: any building touching (or nearly touching - see
	// LinkAdjacencyToleranceMeters, which covers small OSM mapping gaps) something already in the
	// campus set joins it too, repeated until a full pass adds nothing new - so a whole chain of
	// several linked buildings/walkways gets pulled in, not just ones directly touching a
	// 'university'-tagged building.
	TSet<int64> CampusIds;
	for (const TPair<int64, FDatabaseBuildingRow>& Pair : RowsById)
	{
		if (Pair.Value.BuildingType == TEXT("university"))
		{
			CampusIds.Add(Pair.Key);
		}
	}
	const int32 TaggedCampusCount = CampusIds.Num();

	// WorldFootprintsById is still in real meters here (Scale is only applied later, inside
	// SpawnBuildingFromData/BuildFromFootprint) - this tolerance must match that same unit, NOT
	// world/cm-scale units, or it ends up 100x too generous and the flood-fill never stops.
	constexpr float LinkAdjacencyToleranceMeters = 3.f;
	bool bAddedAny = true;
	while (bAddedAny)
	{
		bAddedAny = false;
		TArray<int64> NewlyLinkedIds;
		for (const TPair<int64, FDatabaseBuildingRow>& CandidatePair : RowsById)
		{
			const int64 CandidateId = CandidatePair.Key;
			if (CampusIds.Contains(CandidateId))
			{
				continue;
			}
			const TArray<FVector2D>& CandidateFootprint = WorldFootprintsById.FindChecked(CandidateId);
			for (int64 CampusId : CampusIds)
			{
				if (FootprintsAreLinked(CandidateFootprint, WorldFootprintsById.FindChecked(CampusId), LinkAdjacencyToleranceMeters))
				{
					NewlyLinkedIds.Add(CandidateId);
					break;
				}
			}
		}
		for (int64 NewId : NewlyLinkedIds)
		{
			CampusIds.Add(NewId);
			bAddedAny = true;
		}
	}

	DatabaseCampusBuildingIds = CampusIds;

	int32 SpawnedCount = 0;
	for (int64 CampusId : CampusIds)
	{
		const FDatabaseBuildingRow& Row = RowsById.FindChecked(CampusId);
		const FString Id = FString::Printf(TEXT("SQL-%lld"), CampusId);
		const FString Name = FString::Printf(TEXT("%s %lld"), *Row.BuildingType, CampusId);
		if (APolyBuilding* Spawned = SpawnBuildingFromData(Id, Name, WorldFootprintsById.FindChecked(CampusId), DefaultFloorCountForDatabaseBuildings, DefaultFloorHeightForDatabaseBuildings, FVector2D::ZeroVector))
		{
			SpawnedCount++;
			Spawned->SetBuildingType(BuildingTypes::FromMapDatabase(Row.BuildingType));
			RefreshBuildingFilterFor(*Spawned);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("LoadBuildingsFromDatabase: spawned %d campus building(s) (%d tagged 'university', %d pulled in via adjacency)"),
		SpawnedCount, TaggedCampusCount, CampusIds.Num() - TaggedCampusCount);
	MarkGroundDirty();
	return SpawnedCount;
}

namespace
{
	// OSM's own highway= tagging, as this sample dataset uses it, bucketed into the coarser
	// ERoadCategory a filter panel actually shows checkboxes for - not an exhaustive OSM vocabulary,
	// just what LoadRoadsAndPOIsFromDatabase's own inspection of this dataset found. Anything not
	// called out explicitly (service, track, ...) falls into Other.
	ERoadCategory GetRoadCategory(const FString& HighwayType)
	{
		if (HighwayType == TEXT("primary") || HighwayType == TEXT("primary_link"))
		{
			return ERoadCategory::Primary;
		}
		if (HighwayType == TEXT("secondary") || HighwayType == TEXT("secondary_link"))
		{
			return ERoadCategory::Secondary;
		}
		if (HighwayType == TEXT("tertiary") || HighwayType == TEXT("tertiary_link") || HighwayType == TEXT("residential"))
		{
			return ERoadCategory::Residential;
		}
		if (HighwayType == TEXT("cycleway"))
		{
			return ERoadCategory::Cycleway;
		}
		if (HighwayType == TEXT("footway") || HighwayType == TEXT("pedestrian") || HighwayType == TEXT("path"))
		{
			return ERoadCategory::Footway;
		}
		return ERoadCategory::Other;
	}

	// Width in real-world meters (still pre-Scale, like everything else in this loading path) and a
	// rough asphalt/pavement/path tint so the different road categories are at least distinguishable
	// at a glance.
	void GetRoadCategoryStyle(ERoadCategory Category, float& OutWidthMeters, FLinearColor& OutColor)
	{
		switch (Category)
		{
		case ERoadCategory::Primary:
			OutWidthMeters = 9.f;
			OutColor = FLinearColor(0.55f, 0.15f, 0.12f);
			break;
		case ERoadCategory::Secondary:
			OutWidthMeters = 7.f;
			OutColor = FLinearColor(0.6f, 0.4f, 0.1f);
			break;
		case ERoadCategory::Residential:
			OutWidthMeters = 5.5f;
			OutColor = FLinearColor(0.35f, 0.35f, 0.38f);
			break;
		case ERoadCategory::Cycleway:
			OutWidthMeters = 2.f;
			OutColor = FLinearColor(0.15f, 0.5f, 0.2f);
			break;
		case ERoadCategory::Footway:
			OutWidthMeters = 1.8f;
			OutColor = FLinearColor(0.62f, 0.56f, 0.45f);
			break;
		default:
			// service, track, and anything else not called out above.
			OutWidthMeters = 3.f;
			OutColor = FLinearColor(0.45f, 0.45f, 0.45f);
			break;
		}
	}
}

int32 ABuildingGenerator::LoadRoadsAndPOIsFromDatabase(const FString& DatabasePath)
{
	if (!bHasDatabaseRefPoint)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadRoadsAndPOIsFromDatabase: call LoadBuildingsFromDatabase first (same path) to establish the shared lat/lon reference point"));
		return 0;
	}
	if (!GetWorld())
	{
		return 0;
	}

	FSQLiteDatabase Database;
	if (!Database.Open(*DatabasePath, ESQLiteDatabaseOpenMode::ReadOnly))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not open SQLite database at: %s"), *DatabasePath);
		return 0;
	}

	constexpr double MetersPerDegreeLat = 111320.0;
	const double MetersPerDegreeLon = MetersPerDegreeLat * FMath::Cos(FMath::DegreesToRadians(DatabaseRefLat));
	const double CapturedRefLat = DatabaseRefLat;
	const double CapturedRefLon = DatabaseRefLon;
	auto ProjectToWorldXY = [CapturedRefLat, CapturedRefLon, MetersPerDegreeLat, MetersPerDegreeLon, this](double Lat, double Lon) -> FVector2D
	{
		const double MetersX = (Lon - CapturedRefLon) * MetersPerDegreeLon;
		const double MetersY = (Lat - CapturedRefLat) * MetersPerDegreeLat;
		// Scale=100 (see LoadBuildingsFromDatabase) turns real-world meters directly into UE's
		// cm-per-unit world space - the exact same conversion every road/POI position needs too.
		return FVector2D((float)(MetersX * Scale), (float)(MetersY * Scale));
	};

	ClearAllRoadsAndPOIs();

	struct FDatabaseRoadRow
	{
		FString HighwayType;
		TArray<FVector2D> Points;
	};
	TMap<int64, FDatabaseRoadRow> RoadsById;

	Database.Execute(
		TEXT("SELECT r.road_id, r.highway_type, n.lat, n.lon FROM roads r ")
		TEXT("JOIN road_nodes rn ON r.road_id = rn.road_id ")
		TEXT("JOIN nodes n ON rn.node_id = n.node_id ")
		TEXT("ORDER BY r.road_id, rn.sequence"),
		[&RoadsById, &ProjectToWorldXY](const FSQLitePreparedStatement& Statement) -> ESQLitePreparedStatementExecuteRowResult
		{
			int64 RoadId = 0;
			FString HighwayType;
			double Lat = 0.0, Lon = 0.0;
			if (!Statement.GetColumnValueByName(TEXT("road_id"), RoadId)
				|| !Statement.GetColumnValueByName(TEXT("highway_type"), HighwayType)
				|| !Statement.GetColumnValueByName(TEXT("lat"), Lat)
				|| !Statement.GetColumnValueByName(TEXT("lon"), Lon))
			{
				return ESQLitePreparedStatementExecuteRowResult::Error;
			}
			FDatabaseRoadRow& Row = RoadsById.FindOrAdd(RoadId);
			Row.HighwayType = HighwayType;
			Row.Points.Add(ProjectToWorldXY(Lat, Lon));
			return ESQLitePreparedStatementExecuteRowResult::Continue;
		});

	int32 SpawnedCount = 0;
	for (const TPair<int64, FDatabaseRoadRow>& Pair : RoadsById)
	{
		if (SpawnRoad(Pair.Value.HighwayType, Pair.Value.Points))
		{
			SpawnedCount++;
		}
	}

	// The database's places of interest (shops, amenities ...) are not loaded: they were small coloured spheres scattered over the ground
	// that no screen did anything with.
	Database.Close();

	UE_LOG(LogTemp, Warning, TEXT("LoadRoadsAndPOIsFromDatabase: spawned %d road(s) from %s"), SpawnedRoadActors.Num(), *DatabasePath);
	MarkGroundDirty();
	return SpawnedCount;
}

int32 ABuildingGenerator::LoadContextBuildingsFromDatabase(const FString& DatabasePath)
{
	if (!bHasDatabaseRefPoint)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadContextBuildingsFromDatabase: call LoadBuildingsFromDatabase first (same path) to establish the shared lat/lon reference point"));
		return 0;
	}
	if (!GetWorld())
	{
		return 0;
	}

	FSQLiteDatabase Database;
	if (!Database.Open(*DatabasePath, ESQLiteDatabaseOpenMode::ReadOnly))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not open SQLite database at: %s"), *DatabasePath);
		return 0;
	}

	constexpr double MetersPerDegreeLat = 111320.0;
	const double MetersPerDegreeLon = MetersPerDegreeLat * FMath::Cos(FMath::DegreesToRadians(DatabaseRefLat));
	const double CapturedRefLat = DatabaseRefLat;
	const double CapturedRefLon = DatabaseRefLon;
	auto ProjectToWorldXY = [CapturedRefLat, CapturedRefLon, MetersPerDegreeLat, MetersPerDegreeLon, this](double Lat, double Lon) -> FVector2D
	{
		const double MetersX = (Lon - CapturedRefLon) * MetersPerDegreeLon;
		const double MetersY = (Lat - CapturedRefLat) * MetersPerDegreeLat;
		return FVector2D((float)(MetersX * Scale), (float)(MetersY * Scale));
	};

	ClearAllContextBuildings();

	struct FDatabaseContextBuildingRow
	{
		FString BuildingType;
		TArray<FVector2D> Points;
	};
	TMap<int64, FDatabaseContextBuildingRow> RowsById;

	// Same schema as LoadBuildingsFromDatabase's own query, no type filter here either - which
	// buildings are "campus" vs "context" is decided by that function's own adjacency pass (not just
	// its building_type tag), and DatabaseCampusBuildingIds is that decision. Skipping by id below
	// (not by re-filtering on type) is what keeps a building pulled into the campus by adjacency from
	// ALSO getting spawned here as a second, non-interactive placeholder copy.
	Database.Execute(
		TEXT("SELECT b.building_id, b.building_type, bn.lat, bn.lon FROM buildings b ")
		TEXT("JOIN building_nodes bn ON b.building_id = bn.building_id ")
		TEXT("ORDER BY b.building_id, bn.sequence"),
		[this, &RowsById](const FSQLitePreparedStatement& Statement) -> ESQLitePreparedStatementExecuteRowResult
		{
			int64 BuildingId = 0;
			FString BuildingType;
			double Lat = 0.0, Lon = 0.0;
			if (!Statement.GetColumnValueByName(TEXT("building_id"), BuildingId)
				|| !Statement.GetColumnValueByName(TEXT("building_type"), BuildingType)
				|| !Statement.GetColumnValueByName(TEXT("lat"), Lat)
				|| !Statement.GetColumnValueByName(TEXT("lon"), Lon))
			{
				return ESQLitePreparedStatementExecuteRowResult::Error;
			}
			if (DatabaseCampusBuildingIds.Contains(BuildingId))
			{
				return ESQLitePreparedStatementExecuteRowResult::Continue;
			}
			FDatabaseContextBuildingRow& Row = RowsById.FindOrAdd(BuildingId);
			Row.BuildingType = BuildingType;
			Row.Points.Add(FVector2D(Lon, Lat));
			return ESQLitePreparedStatementExecuteRowResult::Continue;
		});

	Database.Close();

	int32 SpawnedCount = 0;
	for (const TPair<int64, FDatabaseContextBuildingRow>& Pair : RowsById)
	{
		TArray<FVector2D> Footprint;
		Footprint.Reserve(Pair.Value.Points.Num());
		for (const FVector2D& LonLat : Pair.Value.Points)
		{
			Footprint.Add(ProjectToWorldXY(LonLat.Y, LonLat.X));
		}
		// OSM closes a way by repeating its first node as the last one - strip that duplicate so
		// BuildFromFootprint's own edge loop (which wraps last-to-first itself) doesn't double it up.
		if (Footprint.Num() > 1 && Footprint[0].Equals(Footprint.Last(), 0.5f))
		{
			Footprint.RemoveAt(Footprint.Num() - 1);
		}
		if (Footprint.Num() < 3)
		{
			continue;
		}

		if (SpawnContextBuilding(BuildingTypes::FromMapDatabase(Pair.Value.BuildingType), Footprint))
		{
			SpawnedCount++;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("LoadContextBuildingsFromDatabase: spawned %d context building(s) from %s"),
		SpawnedContextBuildingActors.Num(), *DatabasePath);
	MarkGroundDirty();
	return SpawnedCount;
}

bool ABuildingGenerator::SpawnRoad(const FString& HighwayType, const TArray<FVector2D>& WorldPoints)
{
	if (!GetWorld())
	{
		return false;
	}
	const ERoadCategory Category = GetRoadCategory(HighwayType);
	float WidthMeters = 3.f;
	FLinearColor Color = FLinearColor::Gray;
	GetRoadCategoryStyle(Category, WidthMeters, Color);

	ARoadSegmentActor* RoadActor = GetWorld()->SpawnActor<ARoadSegmentActor>();
	if (!RoadActor)
	{
		return false;
	}
	// Roads are flat and cross each other. Where two lie in the same plane they fight for the same pixels, so each category sits a little
	// above the one before it: the narrow paths are drawn over the wide roads they cross, never the other way round.
	const float WorldZ = 2.f + 0.6f * static_cast<float>(Category == ERoadCategory::Primary ? 0 : Category == ERoadCategory::Secondary ? 1
		: Category == ERoadCategory::Residential ? 2 : Category == ERoadCategory::Other ? 3 : Category == ERoadCategory::Cycleway ? 4 : 5);
	RoadActor->BuildRoad(WorldPoints, WidthMeters * Scale, WorldZ, Color);
	RoadActor->SetCategory(Category);
	SpawnedRoadActors.Add(RoadActor);
	SceneryRoads.Add({ HighwayType, WorldPoints });
	return true;
}

bool ABuildingGenerator::SpawnContextBuilding(const FString& BuildingType, const TArray<FVector2D>& WorldFootprint)
{
	// A flat, uniform placeholder height for every context building; what tells them apart is the colour of
	// their type (BuildingTypes.h), from the database's own building_type.
	constexpr float ContextBuildingHeightWorldUnits = 900.f; // 9m at Scale=100

	AContextBuildingActor* ContextActor = GetWorld() && WorldFootprint.Num() >= 3 ? GetWorld()->SpawnActor<AContextBuildingActor>() : nullptr;
	if (!ContextActor)
	{
		return false;
	}
	const FString Type = BuildingTypes::Normalize(BuildingType);
	ContextActor->BuildFromFootprint(WorldFootprint, ContextBuildingHeightWorldUnits, Type);
	ContextActor->SetActorHiddenInGame(!ShouldShowContextBuilding(*ContextActor));
	SpawnedContextBuildingActors.Add(ContextActor);
	SceneryContextBuildings.Add({ Type, WorldFootprint });
	return true;
}

void ABuildingGenerator::LoadSceneryFromJson(const TSharedPtr<FJsonObject>& RootObject)
{
	if (!RootObject.IsValid() || Scale <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	// What the file has replaces what is there (loading the same file twice does not double the map).
	const TArray<TSharedPtr<FJsonValue>>* Present = nullptr;
	if (RootObject->TryGetArrayField(TEXT("contextBuildings"), Present))
	{
		ClearAllContextBuildings();
	}
	if (RootObject->TryGetArrayField(TEXT("roads"), Present))
	{
		ClearAllRoadsAndPOIs();
	}
	// A point of the file is [x, y] in footprint units; the actors are in world units.
	// A coordinate that is not a finite number is not read as 0: the point is left out (a road would otherwise get a spike to the origin).
	auto ReadPair = [this](const TArray<TSharedPtr<FJsonValue>>& Pair, FVector2D& Out) -> bool
	{
		if (Pair.Num() < 2 || !Pair[0].IsValid() || !Pair[1].IsValid() || Pair[0]->Type != EJson::Number || Pair[1]->Type != EJson::Number)
		{
			return false;
		}
		const double X = Pair[0]->AsNumber() * Scale, Y = Pair[1]->AsNumber() * Scale;
		if (!FMath::IsFinite(X) || !FMath::IsFinite(Y))
		{
			return false;
		}
		Out = FVector2D(X, Y);
		return true;
	};
	auto ReadPoints = [&ReadPair](const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<FVector2D> Points;
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Object->TryGetArrayField(Field, Array))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Array)
			{
				const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
				FVector2D Point;
				if (Value.IsValid() && Value->TryGetArray(Pair) && ReadPair(*Pair, Point))
				{
					Points.Add(Point);
				}
			}
		}
		return Points;
	};

	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (RootObject->TryGetArrayField(TEXT("contextBuildings"), Array))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			if (Object.IsValid())
			{
				FString Type;
				Object->TryGetStringField(TEXT("buildingType"), Type);
				SpawnContextBuilding(Type, ReadPoints(Object, TEXT("footprint")));
			}
		}
	}
	if (RootObject->TryGetArrayField(TEXT("roads"), Array))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			if (Object.IsValid())
			{
				const TArray<FVector2D> Points = ReadPoints(Object, TEXT("points"));
				FString Type;
				Object->TryGetStringField(TEXT("highway"), Type);
				if (Points.Num() >= 2)
				{
					SpawnRoad(Type, Points);
				}
			}
		}
	}
	// A "pois" array, in a file saved by an earlier build, is read past: the places of interest are not shown any more.
	MarkGroundDirty();
}

void ABuildingGenerator::ClearAllRoadsAndPOIs()
{
	SceneryRoads.Reset();
	for (AActor* Actor : SpawnedRoadActors)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	SpawnedRoadActors.Reset();

	for (AActor* Actor : SpawnedPOIActors)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	SpawnedPOIActors.Reset();
	MarkGroundDirty();
}

void ABuildingGenerator::ClearAllContextBuildings()
{
	SceneryContextBuildings.Reset();
	for (AActor* Actor : SpawnedContextBuildingActors)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	SpawnedContextBuildingActors.Reset();
	MarkGroundDirty();
}

void ABuildingGenerator::SetCampusBuildingsVisible(bool bVisible)
{
	if (bCampusBuildingsVisible == bVisible) return;
	bCampusBuildingsVisible = bVisible;
	++BuildingResultsRevision;
	ApplyBuildingFilter();
}

void ABuildingGenerator::SetBuildingFilter(const FString& Query, int32 MinimumFloors, int32 MaximumFloors)
{
	const FString NewText = Query.TrimStartAndEnd();
	const int32 NewMinimum = FMath::Max(0, MinimumFloors);
	const int32 NewMaximum = MaximumFloors > 0 ? FMath::Max(NewMinimum, MaximumFloors) : 0;
	if (BuildingFilterText == NewText && BuildingFilterMinimumFloors == NewMinimum
		&& BuildingFilterMaximumFloors == NewMaximum) return;
	BuildingFilterText = NewText;
	BuildingFilterWords.Reset();
	BuildingFilterText.ParseIntoArrayWS(BuildingFilterWords);
	BuildingFilterMinimumFloors = NewMinimum;
	BuildingFilterMaximumFloors = NewMaximum;
	++BuildingResultsRevision;
	ApplyBuildingFilter();
}

bool ABuildingGenerator::MatchesBuildingFilter(const APolyBuilding& Building) const
{
	return bCampusBuildingsVisible && IsBuildingTypeVisible(Building.GetBuildingType()) && Building.GetFloorCount() >= BuildingFilterMinimumFloors
		&& (BuildingFilterMaximumFloors == 0 || Building.GetFloorCount() <= BuildingFilterMaximumFloors)
		&& NavigationSearch::MatchesWords(BuildingFilterWords, Building.BuildingName + TEXT(" ") + Building.BuildingId);
}

const TMap<FString, FString>& ABuildingGenerator::GetFilteredBuildingOptions() const
{
	if (CachedBuildingResultsRevision != BuildingResultsRevision)
	{
		CachedBuildingOptions.Reset();
		for (const auto& Pair : BuildingsById)
		{
			const APolyBuilding* Building = Pair.Value;
			if (IsValid(Building) && MatchesBuildingFilter(*Building))
			{
				CachedBuildingOptions.Add(FString::Printf(TEXT("%s [%s] - %d floors"), *Building->BuildingName,
					*Building->BuildingId, Building->GetFloorCount()), Building->BuildingId);
			}
		}
		CachedBuildingResultsRevision = BuildingResultsRevision;
	}
	return CachedBuildingOptions;
}

void ABuildingGenerator::RefreshBuildingFilterFor(APolyBuilding& Building) const
{
	const bool bVisible = MatchesBuildingFilter(Building);
	Building.SetActorHiddenInGame(!bVisible);
	Building.SetActorEnableCollision(bVisible);
}

void ABuildingGenerator::NotifyNavigationLayoutChanged()
{
	++NavigationRevision;
	OnNavigationLayoutChanged.Broadcast();
}

void ABuildingGenerator::HandleBuildingFloorCountChanged(APolyBuilding*)
{
	++BuildingResultsRevision;
	NotifyNavigationLayoutChanged();
	// Floor edits also run inside the 3D preview. The controller reapplies exterior visibility
	// when leaving that view; only the result cache needs invalidating during the edit itself.
}

void ABuildingGenerator::HandleBuildingDestroyed(AActor* Actor)
{
	APolyBuilding* Building = Cast<APolyBuilding>(Actor);
	if (Building && FindBuildingById(Building->BuildingId) == Building)
	{
		BuildingsById.Remove(Building->BuildingId);
		UsedBuildingIds.Remove(Building->BuildingId);
		RemovedBuildingIds.Add(Building->BuildingId);
		InteriorDataById.Remove(Building->BuildingId);
		BuildingBaselines.Remove(Building->BuildingId);
		for (auto& Pair : InteriorDataById) Pair.Value = WithoutRemovedBuildingLinks(Pair.Value);
		for (auto& Pair : BuildingBaselines)
		{
			FBuildingBaseline& Baseline = Pair.Value;
			if (!Baseline.bHadInterior) continue;
			TSharedPtr<FJsonObject> Snapshot;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Baseline.InteriorJson), Snapshot)) continue;
			const auto Updated = WithoutRemovedBuildingLinks(Snapshot);
			if (Updated == Snapshot) continue;
			FString Text;
			if (FJsonSerializer::Serialize(Updated.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)))
				Baseline.InteriorJson = MoveTemp(Text);
		}
		RefreshWallOpenings();
		OnBuildingRemoved.Broadcast(Building->BuildingId);
		++BuildingResultsRevision;
		MarkGroundDirty();
		NotifyNavigationLayoutChanged();
	}
}

bool ABuildingGenerator::RemoveBuilding(APolyBuilding& Building)
{
	if (FindBuildingById(Building.BuildingId) != &Building || !Building.Destroy())
	{
		return false;
	}
	// In worlds with actor callbacks disabled, Destroy does not broadcast OnDestroyed.
	HandleBuildingDestroyed(&Building);
	return true;
}

TArray<APolyBuilding*> ABuildingGenerator::GetFilteredBuildings() const
{
	TArray<APolyBuilding*> Result;
	for (const auto& Pair : BuildingsById)
	{
		if (Pair.Value && MatchesBuildingFilter(*Pair.Value)) Result.Add(Pair.Value);
	}
	return Result;
}

void ABuildingGenerator::ApplyBuildingFilter()
{
	for (const TPair<FString, APolyBuilding*>& Pair : BuildingsById)
	{
		if (APolyBuilding* Building = Pair.Value)
		{
			RefreshBuildingFilterFor(*Building);
		}
	}
	MarkGroundDirty();
}

void ABuildingGenerator::SetNonCampusBuildingsVisible(bool bVisible)
{
	bNonCampusBuildingsVisible = bVisible;
	RefreshContextBuildingVisibility();
}

bool ABuildingGenerator::ShouldShowContextBuilding(const AContextBuildingActor& Building) const
{
	return bNonCampusBuildingsVisible && IsBuildingTypeVisible(Building.GetBuildingType());
}

void ABuildingGenerator::RefreshContextBuildingVisibility()
{
	for (AActor* Actor : SpawnedContextBuildingActors)
	{
		if (const AContextBuildingActor* ContextBuilding = Cast<AContextBuildingActor>(Actor))
		{
			Actor->SetActorHiddenInGame(!ShouldShowContextBuilding(*ContextBuilding));
		}
	}
	MarkGroundDirty();
}

bool ABuildingGenerator::SetBuildingType(const FString& BuildingId, const FString& Type)
{
	APolyBuilding* Building = FindBuildingById(BuildingId);
	if (!IsValid(Building))
	{
		return false;
	}
	const FString Known = BuildingTypes::Normalize(Type);
	if (Building->GetBuildingType() != Known)
	{
		CaptureBuildingBaseline(BuildingId);
		Building->SetBuildingType(Known);
		++BuildingResultsRevision;
		RefreshBuildingFilterFor(*Building);
	}
	return true;
}

FString ABuildingGenerator::GetBuildingType(const FString& BuildingId) const
{
	const APolyBuilding* Building = FindBuildingById(BuildingId);
	return IsValid(Building) ? Building->GetBuildingType() : BuildingTypes::Default();
}

void ABuildingGenerator::SetBuildingTypeVisible(const FString& Type, bool bVisible)
{
	const FString Known = BuildingTypes::Normalize(Type);
	if (IsBuildingTypeVisible(Known) == bVisible)
	{
		return;
	}
	if (bVisible)
	{
		HiddenBuildingTypes.Remove(Known);
	}
	else
	{
		HiddenBuildingTypes.Add(Known);
	}
	++BuildingResultsRevision;
	ApplyBuildingFilter();
	RefreshContextBuildingVisibility();
}

bool ABuildingGenerator::IsBuildingTypeVisible(const FString& Type) const
{
	return !HiddenBuildingTypes.Contains(BuildingTypes::Normalize(Type));
}

void ABuildingGenerator::ResetBuildingTypeFilter()
{
	if (HiddenBuildingTypes.IsEmpty())
	{
		return;
	}
	HiddenBuildingTypes.Reset();
	++BuildingResultsRevision;
	ApplyBuildingFilter();
	RefreshContextBuildingVisibility();
}

void ABuildingGenerator::SetRoadCategoryVisible(ERoadCategory Category, bool bVisible)
{
	for (AActor* Actor : SpawnedRoadActors)
	{
		ARoadSegmentActor* RoadActor = Cast<ARoadSegmentActor>(Actor);
		if (RoadActor && RoadActor->GetCategory() == Category)
		{
			RoadActor->SetCategoryVisible(bVisible);
		}
	}
	MarkGroundDirty();
}

void ABuildingGenerator::SetAllRoadsVisible(bool bVisible)
{
	for (AActor* Actor : SpawnedRoadActors)
	{
		if (ARoadSegmentActor* RoadActor = Cast<ARoadSegmentActor>(Actor))
		{
			RoadActor->SetCategoryVisible(bVisible);
		}
	}
	MarkGroundDirty();
}

void ABuildingGenerator::SetRoadsAndPOIsWithinCampusOnly(bool bEnable, const FVector2D& Center, float Radius)
{
	const float RadiusSquared = Radius * Radius;
	for (AActor* Actor : SpawnedRoadActors)
	{
		if (ARoadSegmentActor* RoadActor = Cast<ARoadSegmentActor>(Actor))
		{
			const bool bWithinRange = !bEnable || FVector2D::DistSquared(RoadActor->GetMidpointPosition(), Center) <= RadiusSquared;
			RoadActor->SetWithinCampusRange(bWithinRange);
		}
	}
	for (AActor* Actor : SpawnedPOIActors)
	{
		if (APOIMarkerActor* POIActor = Cast<APOIMarkerActor>(Actor))
		{
			const FVector Location = POIActor->GetActorLocation();
			const bool bWithinRange = !bEnable || FVector2D::DistSquared(FVector2D(Location.X, Location.Y), Center) <= RadiusSquared;
			POIActor->SetWithinCampusRange(bWithinRange);
		}
	}
	MarkGroundDirty();
}

APolyBuilding* ABuildingGenerator::SpawnBuildingFromData(const FString& Id, const FString& Name, const TArray<FVector2D>& Footprint, int32 FloorCount, float FloorHeight, FVector2D Offset)
{
	if (Id.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("SpawnBuildingFromData: building id cannot be empty"));
		return nullptr;
	}

	if (UsedBuildingIds.Contains(Id))
	{
		UE_LOG(LogTemp, Error, TEXT("SpawnBuildingFromData: building id '%s' is already in use"), *Id);
		return nullptr;
	}

	if (Footprint.Num() < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("SpawnBuildingFromData: building '%s' needs at least 3 footprint points (has %d)"), *Id, Footprint.Num());
		return nullptr;
	}
	double TwiceArea = 0;
	for (int32 I = 1; I + 1 < Footprint.Num(); ++I)
	{
		TwiceArea += FVector2D::CrossProduct(Footprint[I] - Footprint[0], Footprint[I + 1] - Footprint[0]);
	}
	if (!FMath::IsFinite(TwiceArea) || !FMath::IsFinite(FloorHeight)
		|| FloorHeight <= UE_SMALL_NUMBER || double(FloorHeight) * FMath::Max(1, FloorCount) > TNumericLimits<float>::Max())
	{
		UE_LOG(LogTemp, Warning, TEXT("Building '%s' must have a nonzero footprint and a valid floor height."), *Id);
		return nullptr;
	}

	if (PolygonSelfIntersects(Footprint))
	{
		UE_LOG(LogTemp, Error, TEXT("SpawnBuildingFromData: building '%s' footprint crosses itself - fix the outline before finishing"), *Id);
		return nullptr;
	}
	if (TwiceArea == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Building '%s' has a footprint with zero area."), *Id);
		return nullptr;
	}

	if (!GetWorld())
	{
		return nullptr;
	}

	APolyBuilding* NewBuilding = GetWorld()->SpawnActor<APolyBuilding>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (NewBuilding)
	{
		NewBuilding->BuildingId = Id;
		NewBuilding->BuildingName = Name;
		NewBuilding->BuildFromFootprint(Footprint, Scale, Offset, FloorCount, FloorHeight, !bNavigationGeometryOnly);
		UsedBuildingIds.Add(Id);
		RemovedBuildingIds.Remove(Id);
		BuildingsById.Add(Id, NewBuilding);
		NewBuilding->OnFloorCountChanged.AddUObject(this, &ABuildingGenerator::HandleBuildingFloorCountChanged);
		NewBuilding->OnDestroyed.AddDynamic(this, &ABuildingGenerator::HandleBuildingDestroyed);
		++BuildingResultsRevision;
		RefreshBuildingFilterFor(*NewBuilding);
		NotifyNavigationLayoutChanged();
	}

	return NewBuilding;
}

APolyBuilding* ABuildingGenerator::FindBuildingById(const FString& Id) const
{
	APolyBuilding* const* Found = BuildingsById.Find(Id);
	return Found ? *Found : nullptr;
}

TArray<APolyBuilding*> ABuildingGenerator::GetAllBuildings() const
{
	TArray<APolyBuilding*> Result;
	BuildingsById.GenerateValueArray(Result);
	return Result;
}

TArray<FString> ABuildingGenerator::GetAllBuildingIds() const
{
	TArray<FString> Result;
	BuildingsById.GenerateKeyArray(Result);
	return Result;
}
FString ABuildingGenerator::GenerateUniqueBuildingId(const FString& BaseId) const
{
	FString NewId = BaseId + TEXT("_Copy");
	int32 CopyNumber = 1;

	while (UsedBuildingIds.Contains(NewId))
	{
		NewId = FString::Printf(TEXT("%s_Copy%d"), *BaseId, CopyNumber);
		CopyNumber++;
	}

	return NewId;
}
FString ABuildingGenerator::GetBuildingName(const FString& BuildingId) const
{
	const APolyBuilding* const* Found = BuildingsById.Find(BuildingId);
	return Found && *Found ? (*Found)->BuildingName : FString();
}

bool ABuildingGenerator::SetBuildingName(const FString& BuildingId, const FString& NewName)
{
	APolyBuilding* const* Found = BuildingsById.Find(BuildingId);
	if (!Found || !*Found)
	{
		UE_LOG(LogTemp, Error, TEXT("SetBuildingName: '%s' not found"), *BuildingId);
		return false;
	}

	for (const auto& Pair : BuildingsById)
	{
		if (Pair.Key != BuildingId && Pair.Value && Pair.Value->BuildingName == NewName)
		{
			UE_LOG(LogTemp, Error, TEXT("SetBuildingName: '%s' is already used by '%s'"), *NewName, *Pair.Key);
			return false;
		}
	}

	if ((*Found)->BuildingName != NewName)
	{
		(*Found)->BuildingName = NewName;
		++BuildingResultsRevision;
		NotifyNavigationLayoutChanged();
	}
	return true;
}

FString ABuildingGenerator::FindBuildingIdByName(const FString& Name) const
{
	for (const auto& Pair : BuildingsById)
	{
		if (Pair.Value && Pair.Value->BuildingName == Name)
		{
			return Pair.Key;
		}
	}
	return FString();
}

bool ABuildingGenerator::SaveBuildingsToFile(const FString& FilePath) const
{
	FString Problem;
	return SaveBuildingsToFile(FilePath, Problem);
}

bool ABuildingGenerator::SaveBuildingsToFile(const FString& FilePath, FString& OutProblem) const
{
	OutProblem.Reset();
	// The revision check and replacement are one operation for cooperating editor instances.
	const ProjectFile::FMutationLock SaveLock;
	if (!SaveLock.IsValid())
	{
		OutProblem = TEXT("Another editor is saving a project. Try again.");
		return false;
	}
	const FString FullPath = FPaths::ConvertRelativePathToFull(FilePath);
	const bool bCheckRevision = !ProjectRevision.IsEmpty() && FPaths::IsSamePath(ProjectRevisionPath, FullPath);
	FString CurrentRevision;
	if (IFileManager::Get().FileExists(*FilePath))
	{
		FString ReadProblem;
		const auto Existing = ReadBuildingProjectFile(FilePath, ReadProblem, &CurrentRevision);
		double ExistingVersion = 0;
		if (Existing && Existing->TryGetNumberField(TEXT("version"), ExistingVersion) && ExistingVersion > ProjectFileVersion)
		{
			OutProblem = TEXT("That file uses a newer project format. Choose a different file name to save a compatible copy.");
			UE_LOG(LogTemp, Warning, TEXT("%s"), *OutProblem);
			return false;
		}
	}
	if (bCheckRevision && CurrentRevision != ProjectRevision)
	{
		OutProblem = TEXT("The project file changed on disk. Reload it or save your edits under a different name.");
		return false;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	// Version 2 only for a project that has the map around the campus; every other project stays version 1.
	const bool bHasMap = Scale > KINDA_SMALL_NUMBER
		&& (!SceneryContextBuildings.IsEmpty() || !SceneryRoads.IsEmpty());
	RootObject->SetNumberField(TEXT("version"), bHasMap ? ProjectFileVersion : BasicProjectFileVersion);
	RootObject->SetNumberField(TEXT("scale"), Scale);
	RootObject->SetNumberField(TEXT("floorHeight"), 300.f);
	if (!ProjectName.IsEmpty())
	{
		RootObject->SetStringField(TEXT("projectName"), ProjectName);
	}
	if (!ProjectDescription.IsEmpty())
	{
		RootObject->SetStringField(TEXT("description"), ProjectDescription);
	}

	TArray<TSharedPtr<FJsonValue>> BuildingsArray;
	for (const auto& Pair : BuildingsById)
	{
		APolyBuilding* Building = Pair.Value;
		if (!Building)
		{
			continue;
		}

		TSharedRef<FJsonObject> BuildingObject = MakeShared<FJsonObject>();
		BuildingObject->SetStringField(TEXT("id"), Building->BuildingId);
		BuildingObject->SetStringField(TEXT("name"), Building->BuildingName);

		TArray<TSharedPtr<FJsonValue>> OffsetArray;
		OffsetArray.Add(MakeShared<FJsonValueNumber>(Building->BuildingOffset.X));
		OffsetArray.Add(MakeShared<FJsonValueNumber>(Building->BuildingOffset.Y));
		BuildingObject->SetArrayField(TEXT("offset"), OffsetArray);

		BuildingObject->SetNumberField(TEXT("floorCount"), Building->GetFloorCount());
		BuildingObject->SetNumberField(TEXT("floorHeight"), Building->GetFloorHeight());
		BuildingObject->SetStringField(TEXT("buildingType"), Building->GetBuildingType());

		TArray<TSharedPtr<FJsonValue>> FootprintArray;
		for (const FVector2D& Point : Building->GetFootprint())
		{
			TArray<TSharedPtr<FJsonValue>> PointArray;
			PointArray.Add(MakeShared<FJsonValueNumber>(Point.X));
			PointArray.Add(MakeShared<FJsonValueNumber>(Point.Y));
			FootprintArray.Add(MakeShared<FJsonValueArray>(PointArray));
		}
		BuildingObject->SetArrayField(TEXT("footprint"), FootprintArray);

		if (const TSharedPtr<FJsonObject> InteriorData = GetInteriorDataForBuilding(Building->BuildingId))
		{
			BuildingObject->SetObjectField(TEXT("interior"), InteriorData);
		}

		BuildingsArray.Add(MakeShared<FJsonValueObject>(BuildingObject));
	}

	RootObject->SetArrayField(TEXT("buildings"), BuildingsArray);

	// The map around the campus, so a project saved from a map database is complete. Points are in footprint units, like the buildings'.
	auto PointValue = [this](const FVector2D& World)
	{
		auto Units = [this](double Value) { return FMath::RoundToDouble(Value / Scale * 100.0) / 100.0; };
		TArray<TSharedPtr<FJsonValue>> Pair;
		Pair.Add(MakeShared<FJsonValueNumber>(Units(World.X)));
		Pair.Add(MakeShared<FJsonValueNumber>(Units(World.Y)));
		return Pair;
	};
	auto PointsValue = [&PointValue](const TArray<FVector2D>& Points)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		for (const FVector2D& Point : Points)
		{
			Array.Add(MakeShared<FJsonValueArray>(PointValue(Point)));
		}
		return Array;
	};
	if (Scale > KINDA_SMALL_NUMBER)
	{
		if (!SceneryContextBuildings.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Array;
			for (const FSceneryContextBuilding& Building : SceneryContextBuildings)
			{
				TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
				Object->SetStringField(TEXT("buildingType"), Building.BuildingType);
				Object->SetArrayField(TEXT("footprint"), PointsValue(Building.Footprint));
				Array.Add(MakeShared<FJsonValueObject>(Object));
			}
			RootObject->SetArrayField(TEXT("contextBuildings"), Array);
		}
		if (!SceneryRoads.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Array;
			for (const FSceneryRoad& Road : SceneryRoads)
			{
				TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
				Object->SetStringField(TEXT("highway"), Road.HighwayType);
				Object->SetArrayField(TEXT("points"), PointsValue(Road.Points));
				Array.Add(MakeShared<FJsonValueObject>(Object));
			}
			RootObject->SetArrayField(TEXT("roads"), Array);
		}
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		UE_LOG(LogTemp, Error, TEXT("SaveBuildingsToFile: failed to serialize JSON"));
		return false;
	}

	if (!ProjectFile::WriteText(FilePath, OutputString))
	{
		UE_LOG(LogTemp, Error, TEXT("SaveBuildingsToFile: could not write to %s"), *FilePath);
		return false;
	}
	ProjectRevisionPath = FullPath;
	ProjectRevision = BuildingProjectRevision(OutputString);

	UE_LOG(LogTemp, Warning, TEXT("SaveBuildingsToFile: saved %d buildings to %s"), BuildingsArray.Num(), *FilePath);
	return true;
}

TSharedPtr<FJsonObject> ABuildingGenerator::GetInteriorDataForBuilding(const FString& BuildingId) const
{
	const TSharedPtr<FJsonObject>* Found = InteriorDataById.Find(BuildingId);
	return Found ? *Found : nullptr;
}

void ABuildingGenerator::SetInteriorDataForBuilding(const FString& BuildingId, TSharedPtr<FJsonObject> InteriorData, bool bNotifyNavigation)
{
	InteriorDataById.Add(BuildingId, WithoutRemovedBuildingLinks(InteriorData));
	RefreshWallOpenings();
	if (bNotifyNavigation) NotifyNavigationLayoutChanged();
}

TSharedPtr<FJsonObject> ABuildingGenerator::WithoutRemovedBuildingLinks(const TSharedPtr<FJsonObject>& Interior) const
{
	const TArray<TSharedPtr<FJsonValue>>* Stored = nullptr;
	if (!Interior || !Interior->TryGetArrayField(TEXT("entrances"), Stored)) return Interior;
	const auto IsRemovedLink = [this](const TSharedPtr<FJsonValue>& Value)
	{
		const TSharedPtr<FJsonObject>* Door = nullptr;
		FString Kind, Target;
		return Value && Value->TryGetObject(Door) && Door->IsValid()
			&& (*Door)->TryGetStringField(TEXT("kind"), Kind) && Kind.Equals(TEXT("link"), ESearchCase::IgnoreCase)
			&& (*Door)->TryGetStringField(TEXT("connectsTo"), Target)
			&& RemovedBuildingIds.Contains(Target);
	};
	if (!Stored->ContainsByPredicate(IsRemovedLink)) return Interior;
	auto Updated = MakeShared<FJsonObject>(*Interior);
	auto Remaining = *Stored;
	Remaining.RemoveAll(IsRemovedLink);
	if (Remaining.IsEmpty()) Updated->RemoveField(TEXT("entrances"));
	else Updated->SetArrayField(TEXT("entrances"), MoveTemp(Remaining));
	return Updated;
}

TArray<FInteriorEntrance> ABuildingGenerator::GetStoredEntrances(const FString& BuildingId) const
{
	return InteriorEntrances::ReadAll(GetInteriorDataForBuilding(BuildingId));
}

bool ABuildingGenerator::MakeMirrorOfEntrance(const FString& SourceBuildingId, const FInteriorEntrance& Source, const FString& OnBuildingId, FInteriorEntrance& OutMirror) const
{
	const APolyBuilding* From = FindBuildingById(SourceBuildingId);
	const APolyBuilding* To = FindBuildingById(OnBuildingId);
	if (!From || !To || Scale <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	if (Source.Level < 1 || Source.Level > From->GetFloorCount() || Source.Level > To->GetFloorCount()
		|| !InteriorEntrances::AreElevationsAligned((Source.Level - 1) * double(From->GetFloorHeight()),
			(Source.Level - 1) * double(To->GetFloorHeight())))
	{
		return false;
	}
	const TArray<FVector2D>& FromOutline = From->GetFootprint();
	const TArray<FVector2D>& ToOutline = To->GetFootprint();
	if (!FromOutline.IsValidIndex(Source.EdgeIndex) || ToOutline.Num() < 3)
	{
		return false;
	}

	// The middle of the source door, in the shared footprint space (each building's own space is shifted by its offset).
	const FVector2D A = FromOutline[Source.EdgeIndex];
	const FVector2D B = FromOutline[(Source.EdgeIndex + 1) % FromOutline.Num()];
	const FVector2D Middle = A + (B - A).GetSafeNormal() * ((Source.Offset + Source.Width * 0.5f) / Scale) + From->BuildingOffset - To->BuildingOffset;

	// The nearest point of the other outline.
	int32 BestEdge = INDEX_NONE;
	double BestDistance = TNumericLimits<double>::Max(), BestAlong = 0.0, BestLength = 0.0;
	for (int32 i = 0; i < ToOutline.Num(); ++i)
	{
		const FVector2D& P = ToOutline[i];
		const FVector2D& Q = ToOutline[(i + 1) % ToOutline.Num()];
		const double Length = FVector2D::Distance(P, Q);
		if (Length < KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const FVector2D Direction = (Q - P) / Length;
		const double Along = FMath::Clamp(FVector2D::DotProduct(Middle - P, Direction), 0.0, Length);
		const double Distance = FVector2D::Distance(Middle, P + Direction * Along);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestEdge = i;
			BestAlong = Along;
			BestLength = Length;
		}
	}
	const double LengthWorld = BestLength * Scale;
	if (BestEdge == INDEX_NONE || LengthWorld < Source.Width + 40.0)
	{
		return false;
	}
	OutMirror = Source;
	OutMirror.EdgeIndex = BestEdge;
	OutMirror.Offset = static_cast<float>(FMath::Clamp(BestAlong * Scale - Source.Width * 0.5, 20.0, LengthWorld - Source.Width - 20.0));
	const FVector2D MirrorStart = ToOutline[BestEdge];
	const FVector2D MirrorDirection = (ToOutline[(BestEdge + 1) % ToOutline.Num()] - MirrorStart).GetSafeNormal();
	const FVector2D MirrorMiddle = MirrorStart + MirrorDirection * ((OutMirror.Offset + Source.Width * 0.5) / Scale);
	if (FVector2D::Distance(Middle, MirrorMiddle) * Scale > InteriorEntrances::MaxLinkDistanceWorldUnits) return false;
	OutMirror.Kind = EInteriorEntranceKind::Link;
	OutMirror.ConnectsTo = SourceBuildingId;
	OutMirror.bMirrored = true;
	OutMirror.SourceBuildingId = SourceBuildingId;
	return true;
}

TArray<FInteriorEntrance> ABuildingGenerator::GetMirroredEntrances(const FString& BuildingId) const
{
	TArray<FInteriorEntrance> Result;
	for (const TPair<FString, APolyBuilding*>& Pair : BuildingsById)
	{
		if (Pair.Key == BuildingId)
		{
			continue;
		}
		for (const FInteriorEntrance& Stored : GetStoredEntrances(Pair.Key))
		{
			FInteriorEntrance Mirror;
			if (Stored.Kind == EInteriorEntranceKind::Link && Stored.ConnectsTo == BuildingId && MakeMirrorOfEntrance(Pair.Key, Stored, BuildingId, Mirror))
			{
				Result.Add(Mirror);
			}
		}
	}
	return Result;
}

bool ABuildingGenerator::RemoveStoredEntrance(const FString& BuildingId, const FString& EntranceId, TSharedPtr<FJsonObject>* OutRemoved)
{
	const TSharedPtr<FJsonObject> Interior = GetInteriorDataForBuilding(BuildingId);
	const TArray<TSharedPtr<FJsonValue>>* Stored = nullptr;
	if (!Interior || !Interior->TryGetArrayField(TEXT("entrances"), Stored))
	{
		return false;
	}
	TArray<TSharedPtr<FJsonValue>> Entrances = *Stored;
	const int32 Index = Entrances.IndexOfByPredicate([&EntranceId](const TSharedPtr<FJsonValue>& Value)
	{
		const TSharedPtr<FJsonObject>* Entrance = nullptr;
		FString Id;
		return Value && Value->TryGetObject(Entrance) && Entrance->IsValid()
			&& (*Entrance)->TryGetStringField(TEXT("id"), Id) && Id == EntranceId;
	});
	if (Index == INDEX_NONE) return false;
	if (OutRemoved) *OutRemoved = Entrances[Index]->AsObject();
	Entrances.RemoveAt(Index);
	CaptureBuildingBaseline(BuildingId);
	auto Updated = MakeShared<FJsonObject>(*Interior);
	if (Entrances.IsEmpty()) Updated->RemoveField(TEXT("entrances"));
	else Updated->SetArrayField(TEXT("entrances"), MoveTemp(Entrances));
	SetInteriorDataForBuilding(BuildingId, Updated);
	return true;
}

bool ABuildingGenerator::RestoreStoredEntrance(const FString& BuildingId, const TSharedPtr<FJsonObject>& Entrance)
{
	const auto Interior = GetInteriorDataForBuilding(BuildingId);
	FString Id;
	if (!FindBuildingById(BuildingId) || !Interior || !Entrance || !Entrance->TryGetStringField(TEXT("id"), Id)
		|| GetStoredEntrances(BuildingId).ContainsByPredicate([&Id](const FInteriorEntrance& Existing) { return Existing.Id == Id; }))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Stored = nullptr;
	TArray<TSharedPtr<FJsonValue>> Entrances;
	if (Interior->TryGetArrayField(TEXT("entrances"), Stored)) Entrances = *Stored;
	Entrances.Add(MakeShared<FJsonValueObject>(Entrance));
	CaptureBuildingBaseline(BuildingId);
	auto Updated = MakeShared<FJsonObject>(*Interior);
	Updated->SetArrayField(TEXT("entrances"), MoveTemp(Entrances));
	SetInteriorDataForBuilding(BuildingId, Updated);
	return true;
}

void ABuildingGenerator::RefreshWallOpenings()
{
	for (const TPair<FString, APolyBuilding*>& Pair : BuildingsById)
	{
		if (!Pair.Value)
		{
			continue;
		}
		TArray<FPolyWallOpening> Openings;
		for (const FInteriorEntrance& Entrance : GetStoredEntrances(Pair.Key))
		{
			Openings.Add({ Entrance.Level, Entrance.EdgeIndex, Entrance.Offset, Entrance.Width });
		}
		for (const FInteriorEntrance& Entrance : GetMirroredEntrances(Pair.Key))
		{
			Openings.Add({ Entrance.Level, Entrance.EdgeIndex, Entrance.Offset, Entrance.Width });
		}
		Pair.Value->SetWallOpenings(Openings);
	}
}

bool ABuildingGenerator::GetBuildingSummary(const FString& BuildingId, FBuildingSummary& OutSummary) const
{
	const APolyBuilding* Building = FindBuildingById(BuildingId);
	if (!IsValid(Building))
	{
		return false;
	}
	OutSummary = FBuildingSummary();
	OutSummary.Id = Building->BuildingId;
	OutSummary.Name = Building->BuildingName;
	OutSummary.Floors = Building->GetFloorCount();
	OutSummary.Type = Building->GetBuildingType();
	OutSummary.Footprint = Building->GetFootprint();

	if (const TSharedPtr<FJsonObject> Interior = GetInteriorDataForBuilding(BuildingId))
	{
		const TArray<TSharedPtr<FJsonValue>>* Floors = nullptr;
		if (Interior->TryGetArrayField(TEXT("floors"), Floors) && Floors)
		{
			TSet<FString> Links;
			for (const TSharedPtr<FJsonValue>& FloorValue : *Floors)
			{
				const TSharedPtr<FJsonObject> Floor = FloorValue.IsValid() ? FloorValue->AsObject() : nullptr;
				if (!Floor.IsValid())
				{
					continue;
				}
				const TArray<TSharedPtr<FJsonValue>>* Rooms = nullptr;
				if (Floor->TryGetArrayField(TEXT("rooms"), Rooms) && Rooms)
				{
					OutSummary.Rooms += Rooms->Num();
				}
				const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
				if (Floor->TryGetArrayField(TEXT("navNodes"), Nodes) && Nodes)
				{
					for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
					{
						const TSharedPtr<FJsonObject> Node = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
						FString Type;
						if (!Node.IsValid() || !Node->TryGetStringField(TEXT("type"), Type)
							|| !(Type == TEXT("elevator") || Type == TEXT("stairs") || Type == TEXT("escalator")))
						{
							continue;
						}
						// One link has a node on every floor it reaches, all sharing a group id.
						FString Key;
						if (!Node->TryGetStringField(TEXT("connectorGroupId"), Key) || Key.IsEmpty())
						{
							Node->TryGetStringField(TEXT("id"), Key);
						}
						Links.Add(Key);
					}
				}
			}
			OutSummary.VerticalLinks = Links.Num();
		}
	}
	return true;
}

int32 ABuildingGenerator::ReplaceBuildingsFromPath(const FString& Path, bool bDatabase, TFunctionRef<void()> BeforeReplace, FString& OutProblem)
{
	OutProblem.Reset();
	TSharedPtr<FJsonObject> RootObject;
	FString Revision;
	int32 ExpectedCount = 0;
	if (!bDatabase)
	{
		RootObject = ReadBuildingProjectFile(Path, OutProblem, &Revision);
		if (!RootObject) return INDEX_NONE;
		float ProjectScale = 1.f;
		if (!ReadProjectScale(*RootObject, ProjectScale, OutProblem)) return INDEX_NONE;
		float FloorHeight;
		if (!ReadBuildingFloorHeight(*RootObject, 300.f, FloorHeight))
		{
			OutProblem = TEXT("The project floor height must be a positive number.");
			return INDEX_NONE;
		}

		const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
		if (!RootObject->TryGetArrayField(TEXT("buildings"), Buildings))
		{
			OutProblem = TEXT("The project file is missing its buildings array.");
			return INDEX_NONE;
		}
		if (!CheckProjectFloorLimits(*Buildings, OutProblem)) return INDEX_NONE;
		for (const auto& Value : *Buildings)
		{
			const TSharedPtr<FJsonObject>* Building = nullptr;
			if (Value && Value->TryGetObject(Building) && Building->IsValid() && !CanLoadInteriorRecords(**Building))
			{
				FString Id;
				(*Building)->TryGetStringField(TEXT("id"), Id);
				OutProblem = FString::Printf(TEXT("Building '%s' has interior records the editor cannot load safely. Correct the file before opening it."), *Id);
				return INDEX_NONE;
			}
		}
		ExpectedCount = Buildings->Num();
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	ABuildingGenerator* Staged = GetWorld() ? GetWorld()->SpawnActor<ABuildingGenerator>(SpawnParameters) : nullptr;
	if (!Staged)
	{
		OutProblem = TEXT("Could not prepare the new project in this level.");
		return INDEX_NONE;
	}

	// Keep staged actors out of view and collision until they replace the current buildings.
	Staged->bCampusBuildingsVisible = false;
	const int32 Count = bDatabase ? Staged->LoadBuildingsFromDatabase(Path) : Staged->LoadBuildingsFromJson(RootObject);
	if (bDatabase ? Count == 0 : Count != ExpectedCount)
	{
		Staged->ClearAllBuildings();
		Staged->Destroy();
		OutProblem = bDatabase
			? TEXT("No buildings could be loaded. Check that the database has campus buildings with valid building nodes.")
			: TEXT("Some buildings could not be loaded. Check their IDs, floor heights and outlines.");
		return INDEX_NONE;
	}

	BeforeReplace();
	ClearAllBuildings();
	ClearAllRoadsAndPOIs();
	ClearAllContextBuildings();
	BuildingsById = MoveTemp(Staged->BuildingsById);
	UsedBuildingIds = MoveTemp(Staged->UsedBuildingIds);
	InteriorDataById = MoveTemp(Staged->InteriorDataById);
	Scale = Staged->Scale;
	ProjectName = MoveTemp(Staged->ProjectName);
	ProjectDescription = MoveTemp(Staged->ProjectDescription);
	LoadedFileVersion = Staged->LoadedFileVersion;
	if (!bDatabase)
	{
		ProjectRevisionPath = FPaths::ConvertRelativePathToFull(Path);
		ProjectRevision = MoveTemp(Revision);
	}
	bHasDatabaseRefPoint = Staged->bHasDatabaseRefPoint;
	DatabaseRefLat = Staged->DatabaseRefLat;
	DatabaseRefLon = Staged->DatabaseRefLon;
	DatabaseCampusBuildingIds = MoveTemp(Staged->DatabaseCampusBuildingIds);
	for (const auto& Pair : BuildingsById)
	{
		APolyBuilding* Building = Pair.Value;
		Building->OnFloorCountChanged.RemoveAll(Staged);
		Building->OnDestroyed.RemoveDynamic(Staged, &ABuildingGenerator::HandleBuildingDestroyed);
		Building->OnFloorCountChanged.AddUObject(this, &ABuildingGenerator::HandleBuildingFloorCountChanged);
		Building->OnDestroyed.AddDynamic(this, &ABuildingGenerator::HandleBuildingDestroyed);
	}
	Staged->Destroy();
	if (!bDatabase)
	{
		// The roads, places and buildings around the campus of the file (the database ones are loaded by the caller).
		LoadSceneryFromJson(RootObject);
	}
	ApplyBuildingFilter();
	NotifyNavigationLayoutChanged();
	return Count;
}

void ABuildingGenerator::ClearAllBuildings()
{
	for (const auto& Pair : BuildingsById)
	{
		if (Pair.Value)
		{
			// The bulk clear owns map removal; avoid changing it during iteration.
			Pair.Value->OnDestroyed.RemoveDynamic(this, &ABuildingGenerator::HandleBuildingDestroyed);
			Pair.Value->OnFloorCountChanged.RemoveAll(this);
			Pair.Value->Destroy();
		}
	}
	BuildingsById.Empty();
	++BuildingResultsRevision;
	UsedBuildingIds.Empty();
	RemovedBuildingIds.Empty();
	InteriorDataById.Empty();
	BuildingBaselines.Empty();
	Scale = 1.f;
	LoadedFileVersion = 0;
	ProjectRevisionPath.Reset();
	ProjectRevision.Reset();
	MarkGroundDirty();
	NotifyNavigationLayoutChanged();
}

bool ABuildingGenerator::CaptureBuildingBaselineInto(const FString& BuildingId, FBuildingBaseline& Out) const
{
	APolyBuilding* const* Found = BuildingsById.Find(BuildingId);
	if (!Found || !*Found)
	{
		return false;
	}
	Out.FloorCount = (*Found)->GetFloorCount();
	Out.Name = (*Found)->BuildingName;
	Out.Type = (*Found)->GetBuildingType();
	Out.bHadInterior = false;
	Out.InteriorJson.Reset();
	const TSharedPtr<FJsonObject> Interior = GetInteriorDataForBuilding(BuildingId);
	if (Interior.IsValid())
	{
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out.InteriorJson);
		Out.bHadInterior = FJsonSerializer::Serialize(Interior.ToSharedRef(), Writer);
	}
	return true;
}

void ABuildingGenerator::CaptureBuildingBaseline(const FString& BuildingId)
{
	if (BuildingBaselines.Contains(BuildingId))
	{
		return;
	}
	FBuildingBaseline Baseline;
	if (CaptureBuildingBaselineInto(BuildingId, Baseline))
	{
		BuildingBaselines.Add(BuildingId, MoveTemp(Baseline));
	}
}

void ABuildingGenerator::RefreshBuildingBaselines()
{
	LoadedFileVersion = ProjectFileVersion;
	for (TPair<FString, FBuildingBaseline>& Pair : BuildingBaselines)
	{
		FBuildingBaseline Fresh;
		if (CaptureBuildingBaselineInto(Pair.Key, Fresh))
		{
			Pair.Value = MoveTemp(Fresh);
		}
	}
}

void ABuildingGenerator::RestoreBuildingBaselines()
{
	for (const TPair<FString, FBuildingBaseline>& Pair : BuildingBaselines)
	{
		APolyBuilding* const* Found = BuildingsById.Find(Pair.Key);
		if (!Found || !*Found)
		{
			continue;
		}
		APolyBuilding* Building = *Found;
		const FBuildingBaseline& Baseline = Pair.Value;

		if (Building->GetFloorCount() != Baseline.FloorCount)
		{
			Building->SetFloorCount(Baseline.FloorCount);
		}
		// Straight back to the remembered name - it was valid then, so no uniqueness check is needed.
		Building->BuildingName = Baseline.Name;
		Building->SetBuildingType(Baseline.Type);

		if (Baseline.bHadInterior)
		{
			TSharedPtr<FJsonObject> Restored;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Baseline.InteriorJson);
			if (FJsonSerializer::Deserialize(Reader, Restored) && Restored.IsValid())
			{
				InteriorDataById.Add(Pair.Key, WithoutRemovedBuildingLinks(Restored));
			}
		}
		else
		{
			InteriorDataById.Remove(Pair.Key);
		}
	}
	++BuildingResultsRevision;
	ApplyBuildingFilter();
	RefreshWallOpenings();
	NotifyNavigationLayoutChanged();
}

APolyBuilding* ABuildingGenerator::CreateRectangleBuilding(const FString& Id, const FString& Name, FVector2D Position, float Width, float Depth, float RotationDegrees, int32 FloorCount, float FloorHeight)
{
	if (Scale == 0.f)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRectangleBuilding: Scale is zero"));
		return nullptr;
	}

	// Position/Width/Depth arrive in world-space units; SpawnBuildingFromData re-multiplies by
	// Scale, so work everything out in pre-scale ("footprint") units here, same as Plan A's build
	// mode does with its raycast-hit points.
	const float HalfWidth = (Width * 0.5f) / Scale;
	const float HalfDepth = (Depth * 0.5f) / Scale;
	const FVector2D LocalPosition = Position / Scale;

	const float RadAngle = FMath::DegreesToRadians(RotationDegrees);
	const float CosA = FMath::Cos(RadAngle);
	const float SinA = FMath::Sin(RadAngle);

	auto RotateAndPlace = [&](float LocalX, float LocalY) -> FVector2D
	{
		const FVector2D Rotated(LocalX * CosA - LocalY * SinA, LocalX * SinA + LocalY * CosA);
		return Rotated + LocalPosition;
	};

	TArray<FVector2D> Footprint;
	Footprint.Add(RotateAndPlace(-HalfWidth, -HalfDepth));
	Footprint.Add(RotateAndPlace( HalfWidth, -HalfDepth));
	Footprint.Add(RotateAndPlace( HalfWidth,  HalfDepth));
	Footprint.Add(RotateAndPlace(-HalfWidth,  HalfDepth));

	return SpawnBuildingFromData(Id, Name, Footprint, FloorCount, FloorHeight, FVector2D::ZeroVector);
}

APolyBuilding* ABuildingGenerator::CreateRegularPolygonBuilding(const FString& Id, const FString& Name, FVector2D Position, int32 Sides, float Radius, float RotationDegrees, int32 FloorCount, float FloorHeight)
{
	if (Sides < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRegularPolygonBuilding: needs at least 3 sides (got %d)"), Sides);
		return nullptr;
	}

	if (Scale == 0.f)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRegularPolygonBuilding: Scale is zero"));
		return nullptr;
	}

	// Position/Radius arrive in world-space units; SpawnBuildingFromData re-multiplies by Scale,
	// so work everything out in pre-scale ("footprint") units here, same as CreateRectangleBuilding.
	const float LocalRadius = Radius / Scale;
	const FVector2D LocalPosition = Position / Scale;

	TArray<FVector2D> Footprint;
	Footprint.Reserve(Sides);
	for (int32 i = 0; i < Sides; i++)
	{
		const float AngleDegrees = RotationDegrees + (360.f * i) / Sides;
		const float AngleRad = FMath::DegreesToRadians(AngleDegrees);
		Footprint.Add(LocalPosition + FVector2D(LocalRadius * FMath::Cos(AngleRad), LocalRadius * FMath::Sin(AngleRad)));
	}

	return SpawnBuildingFromData(Id, Name, Footprint, FloorCount, FloorHeight, FVector2D::ZeroVector);
}

// ---- The ground under the campus ----

namespace
{
	// What the ground is sized to (world units are centimetres): at least 40 m each way, and a margin of 5 percent of the longer side, 10 m at least.
	constexpr double GroundMinSize = 4000.0;
	constexpr double GroundMarginFraction = 0.05;
	constexpr double GroundMinMargin = 1000.0;

	FBox2D GroundBoxOf(const AActor& Actor)
	{
		const FBox Box = Actor.GetComponentsBoundingBox(true);
		return Box.IsValid ? FBox2D(FVector2D(Box.Min.X, Box.Min.Y), FVector2D(Box.Max.X, Box.Max.Y)) : FBox2D(ForceInit);
	}
}

bool ABuildingGenerator::GetVisibleExtent(FBox2D& OutExtent) const
{
	FBox2D All(ForceInit);
	auto Add = [&All](const FBox2D& Box)
	{
		if (Box.bIsValid)
		{
			All += Box;
		}
	};
	for (const TPair<FString, APolyBuilding*>& Pair : BuildingsById)
	{
		if (IsValid(Pair.Value) && MatchesBuildingFilter(*Pair.Value))
		{
			Add(GroundBoxOf(*Pair.Value));
		}
	}
	if (bNonCampusBuildingsVisible)
	{
		for (AActor* Actor : SpawnedContextBuildingActors)
		{
			if (IsValid(Actor))
			{
				Add(GroundBoxOf(*Actor));
			}
		}
	}
	for (AActor* Actor : SpawnedRoadActors)
	{
		const ARoadSegmentActor* Road = Cast<ARoadSegmentActor>(Actor);
		if (IsValid(Actor) && Road && Road->IsShownByFilters())
		{
			Add(Road->GetBounds2D());
		}
	}
	for (AActor* Actor : SpawnedPOIActors)
	{
		const APOIMarkerActor* POI = Cast<APOIMarkerActor>(Actor);
		if (IsValid(Actor) && POI && POI->IsShownByFilters())
		{
			const FVector Location = POI->GetActorLocation();
			Add(FBox2D(FVector2D(Location.X - 100.0, Location.Y - 100.0), FVector2D(Location.X + 100.0, Location.Y + 100.0)));
		}
	}
	if (!All.bIsValid)
	{
		return false;
	}
	OutExtent = All;
	return true;
}

void ABuildingGenerator::MarkGroundDirty()
{
	if (bGroundUpdateQueued || !GetWorld())
	{
		return;
	}
	bGroundUpdateQueued = true;
	GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { UpdateGroundToVisible(); }));
}

AStaticMeshActor* ABuildingGenerator::FindGroundActor()
{
	if (!GroundActor.IsValid())
	{
		if (bGroundSearched)
		{
			return nullptr;
		}
		bGroundSearched = true;
		for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		{
			const UStaticMeshComponent* Component = It->GetStaticMeshComponent();
			const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
			if (Mesh && Mesh->GetName() == TEXT("SM_Template_Map_Floor"))
			{
				GroundActor = *It;
				GroundOriginalTransform = It->GetActorTransform();
				break;
			}
		}
		if (!GroundActor.IsValid())
		{
			return nullptr;
		}
	}
	return GroundActor.Get();
}

void ABuildingGenerator::UpdateGroundToVisible()
{
	bGroundUpdateQueued = false;
	if (bGroundFitSuspended || !GetWorld() || !FindGroundActor()) return;
	UStaticMeshComponent* GroundComponent = GroundActor->GetStaticMeshComponent();
	GroundComponent->SetMobility(EComponentMobility::Movable);
	GroundComponent->SetCastShadow(false);

	FBox2D Extent, Rect;
	if (!GetVisibleExtent(Extent) || !GroundFit::Fit({ Extent }, GroundMinSize, GroundMarginFraction, GroundMinMargin, Rect))
	{
		return;
	}
	AStaticMeshActor* Ground = GroundActor.Get();
	const UStaticMesh* Mesh = Ground->GetStaticMeshComponent()->GetStaticMesh();
	if (!Mesh)
	{
		return;
	}
	// The mesh's own size and middle (it is not necessarily a unit square centred on its origin).
	const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
	const FVector2D MeshSize(MeshBounds.BoxExtent.X * 2.0, MeshBounds.BoxExtent.Y * 2.0);
	if (MeshSize.X < KINDA_SMALL_NUMBER || MeshSize.Y < KINDA_SMALL_NUMBER)
	{
		return;
	}
	FVector NewScale = Ground->GetActorScale3D();
	NewScale.X = Rect.GetSize().X / MeshSize.X;
	NewScale.Y = Rect.GetSize().Y / MeshSize.Y;
	const FVector2D Centre = Rect.GetCenter();
	const FVector Location(Centre.X - NewScale.X * MeshBounds.Origin.X, Centre.Y - NewScale.Y * MeshBounds.Origin.Y, Ground->GetActorLocation().Z);
	Ground->SetActorScale3D(NewScale);
	Ground->SetActorLocationAndRotation(Location, FQuat::Identity);
}
