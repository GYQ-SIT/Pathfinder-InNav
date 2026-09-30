// Fill out your copyright notice in the Description page of Project Settings.


#include "BuildingGenerator.h"
#include "PolyBuilding.h"
#include "RoadSegmentActor.h"
#include "POIMarkerActor.h"
#include "ContextBuildingActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "SQLiteDatabase.h"

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

int32 ABuildingGenerator::LoadBuildingsFromFile(const FString& FilePath)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not read JSON file at: %s"), *FilePath);
		return 0;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to parse JSON"));
		return 0;
	}

	Scale = 1.f;
	RootObject->TryGetNumberField(TEXT("scale"), Scale);

	float GlobalFloorHeight = 300.f;
	RootObject->TryGetNumberField(TEXT("floorHeight"), GlobalFloorHeight);

	const TArray<TSharedPtr<FJsonValue>>* BuildingsArray;
	if (!RootObject->TryGetArrayField(TEXT("buildings"), BuildingsArray))
	{
		UE_LOG(LogTemp, Error, TEXT("No 'buildings' array found in JSON"));
		return 0;
	}

	if (!GetWorld())
	{
		return 0;
	}

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

		float FloorHeight = GlobalFloorHeight;   // default to global
		B->TryGetNumberField(TEXT("floorHeight"), FloorHeight);   // override if building has its own

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

		if (SpawnBuildingFromData(Id, Name, Footprint, FloorCount, FloorHeight, Offset))
		{
			SpawnedCount++;

			const TSharedPtr<FJsonObject>* InteriorObject;
			if (B->TryGetObjectField(TEXT("interior"), InteriorObject))
			{
				InteriorDataById.Add(Id, *InteriorObject);
			}
		}
	}

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
			Row.LatLonPoints.Add(FVector2D((float)Lon, (float)Lat));
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
		if (SpawnBuildingFromData(Id, Name, WorldFootprintsById.FindChecked(CampusId), DefaultFloorCountForDatabaseBuildings, DefaultFloorHeightForDatabaseBuildings, FVector2D::ZeroVector))
		{
			SpawnedCount++;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("LoadBuildingsFromDatabase: spawned %d campus building(s) (%d tagged 'university', %d pulled in via adjacency)"),
		SpawnedCount, TaggedCampusCount, CampusIds.Num() - TaggedCampusCount);
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

	FLinearColor GetPOIColor(const FString& POIType)
	{
		if (POIType == TEXT("amenity")) return FLinearColor(0.2f, 0.4f, 0.9f);
		if (POIType == TEXT("shop")) return FLinearColor(0.85f, 0.2f, 0.65f);
		if (POIType == TEXT("leisure")) return FLinearColor(0.25f, 0.75f, 0.3f);
		if (POIType == TEXT("tourism")) return FLinearColor(0.9f, 0.75f, 0.15f);
		return FLinearColor::White;
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
		const ERoadCategory Category = GetRoadCategory(Pair.Value.HighwayType);
		float WidthMeters = 3.f;
		FLinearColor Color = FLinearColor::Gray;
		GetRoadCategoryStyle(Category, WidthMeters, Color);

		ARoadSegmentActor* RoadActor = GetWorld()->SpawnActor<ARoadSegmentActor>();
		if (!RoadActor)
		{
			continue;
		}
		RoadActor->BuildRoad(Pair.Value.Points, WidthMeters * Scale, /*WorldZ=*/2.f, Color);
		RoadActor->SetCategory(Category);
		SpawnedRoadActors.Add(RoadActor);
		SpawnedCount++;
	}

	Database.Execute(
		TEXT("SELECT poi_id, lat, lon, poi_type FROM pois"),
		[this, &ProjectToWorldXY, &SpawnedCount](const FSQLitePreparedStatement& Statement) -> ESQLitePreparedStatementExecuteRowResult
		{
			double Lat = 0.0, Lon = 0.0;
			FString POIType;
			if (!Statement.GetColumnValueByName(TEXT("lat"), Lat)
				|| !Statement.GetColumnValueByName(TEXT("lon"), Lon)
				|| !Statement.GetColumnValueByName(TEXT("poi_type"), POIType))
			{
				return ESQLitePreparedStatementExecuteRowResult::Error;
			}

			APOIMarkerActor* POIActor = GetWorld()->SpawnActor<APOIMarkerActor>();
			if (POIActor)
			{
				constexpr float POIMarkerRadiusWorld = 60.f;
				POIActor->BuildMarker(ProjectToWorldXY(Lat, Lon), POIMarkerRadiusWorld, GetPOIColor(POIType));
				SpawnedPOIActors.Add(POIActor);
				SpawnedCount++;
			}
			return ESQLitePreparedStatementExecuteRowResult::Continue;
		});

	Database.Close();

	UE_LOG(LogTemp, Warning, TEXT("LoadRoadsAndPOIsFromDatabase: spawned %d road(s) and %d POI(s) from %s"),
		SpawnedRoadActors.Num(), SpawnedPOIActors.Num(), *DatabasePath);
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
		TArray<FVector2D> Points;
	};
	TMap<int64, FDatabaseContextBuildingRow> RowsById;

	// Same schema as LoadBuildingsFromDatabase's own query, no type filter here either - which
	// buildings are "campus" vs "context" is decided by that function's own adjacency pass (not just
	// its building_type tag), and DatabaseCampusBuildingIds is that decision. Skipping by id below
	// (not by re-filtering on type) is what keeps a building pulled into the campus by adjacency from
	// ALSO getting spawned here as a second, non-interactive placeholder copy.
	Database.Execute(
		TEXT("SELECT b.building_id, bn.lat, bn.lon FROM buildings b ")
		TEXT("JOIN building_nodes bn ON b.building_id = bn.building_id ")
		TEXT("ORDER BY b.building_id, bn.sequence"),
		[this, &RowsById](const FSQLitePreparedStatement& Statement) -> ESQLitePreparedStatementExecuteRowResult
		{
			int64 BuildingId = 0;
			double Lat = 0.0, Lon = 0.0;
			if (!Statement.GetColumnValueByName(TEXT("building_id"), BuildingId)
				|| !Statement.GetColumnValueByName(TEXT("lat"), Lat)
				|| !Statement.GetColumnValueByName(TEXT("lon"), Lon))
			{
				return ESQLitePreparedStatementExecuteRowResult::Error;
			}
			if (DatabaseCampusBuildingIds.Contains(BuildingId))
			{
				return ESQLitePreparedStatementExecuteRowResult::Continue;
			}
			RowsById.FindOrAdd(BuildingId).Points.Add(FVector2D((float)Lon, (float)Lat));
			return ESQLitePreparedStatementExecuteRowResult::Continue;
		});

	Database.Close();

	// Flat, uniform placeholder height/tint for every context building regardless of its own OSM
	// type - this dataset's building_type is far more varied than roads' highway_type (bridge, house,
	// school, parking, ...) and none of it is being shown as a filter category yet, just a single
	// "Non-Campus" on/off toggle, so there's no reason to style it beyond "clearly not a real,
	// enterable campus building".
	constexpr float ContextBuildingHeightWorldUnits = 900.f; // 9m at Scale=100
	const FLinearColor ContextBuildingColor(0.5f, 0.5f, 0.52f, 1.f);

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

		AContextBuildingActor* ContextActor = GetWorld()->SpawnActor<AContextBuildingActor>();
		if (!ContextActor)
		{
			continue;
		}
		ContextActor->BuildFromFootprint(Footprint, ContextBuildingHeightWorldUnits, ContextBuildingColor);
		SpawnedContextBuildingActors.Add(ContextActor);
		SpawnedCount++;
	}

	UE_LOG(LogTemp, Warning, TEXT("LoadContextBuildingsFromDatabase: spawned %d context building(s) from %s"),
		SpawnedContextBuildingActors.Num(), *DatabasePath);
	return SpawnedCount;
}

void ABuildingGenerator::ClearAllRoadsAndPOIs()
{
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
}

void ABuildingGenerator::ClearAllContextBuildings()
{
	for (AActor* Actor : SpawnedContextBuildingActors)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	SpawnedContextBuildingActors.Reset();
}

void ABuildingGenerator::SetCampusBuildingsVisible(bool bVisible)
{
	for (const TPair<FString, APolyBuilding*>& Pair : BuildingsById)
	{
		if (APolyBuilding* Building = Pair.Value)
		{
			Building->SetActorHiddenInGame(!bVisible);
			Building->SetActorEnableCollision(bVisible);
		}
	}
}

void ABuildingGenerator::SetNonCampusBuildingsVisible(bool bVisible)
{
	for (AActor* Actor : SpawnedContextBuildingActors)
	{
		if (Actor)
		{
			Actor->SetActorHiddenInGame(!bVisible);
		}
	}
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

	if (PolygonSelfIntersects(Footprint))
	{
		UE_LOG(LogTemp, Error, TEXT("SpawnBuildingFromData: building '%s' footprint crosses itself - fix the outline before finishing"), *Id);
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
		NewBuilding->BuildFromFootprint(Footprint, Scale, Offset, FloorCount, FloorHeight);
		UsedBuildingIds.Add(Id);
		BuildingsById.Add(Id, NewBuilding);
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

	(*Found)->BuildingName = NewName;
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
	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetNumberField(TEXT("scale"), Scale);
	RootObject->SetNumberField(TEXT("floorHeight"), 300.f);

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

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		UE_LOG(LogTemp, Error, TEXT("SaveBuildingsToFile: failed to serialize JSON"));
		return false;
	}

	if (!FFileHelper::SaveStringToFile(OutputString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("SaveBuildingsToFile: could not write to %s"), *FilePath);
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("SaveBuildingsToFile: saved %d buildings to %s"), BuildingsArray.Num(), *FilePath);
	return true;
}

TSharedPtr<FJsonObject> ABuildingGenerator::GetInteriorDataForBuilding(const FString& BuildingId) const
{
	const TSharedPtr<FJsonObject>* Found = InteriorDataById.Find(BuildingId);
	return Found ? *Found : nullptr;
}

void ABuildingGenerator::SetInteriorDataForBuilding(const FString& BuildingId, TSharedPtr<FJsonObject> InteriorData)
{
	InteriorDataById.Add(BuildingId, InteriorData);
}

void ABuildingGenerator::ClearAllBuildings()
{
	for (const auto& Pair : BuildingsById)
	{
		if (Pair.Value)
		{
			Pair.Value->Destroy();
		}
	}
	BuildingsById.Empty();
	UsedBuildingIds.Empty();
	InteriorDataById.Empty();
	Scale = 1.f;
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