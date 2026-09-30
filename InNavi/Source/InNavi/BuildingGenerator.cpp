// Fill out your copyright notice in the Description page of Project Settings.


#include "BuildingGenerator.h"
#include "PolyBuilding.h"
#include "RoadSegmentActor.h"
#include "POIMarkerActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "SQLiteDatabase.h"

// ---- Helper: do segments AB and CD properly cross each other? ----
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
			// Skip edges that share a vertex with edge i - they touch by construction, not a crossing.
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

	// Filtered to building_type='university' - this sample database (see project memory) covers the
	// whole surrounding Punggol neighborhood, not just the SIT campus, and 'university' is the OSM tag
	// that happens to land on the ~10 buildings actually inside the campus footprint. This is specific
	// to this particular demo dataset's own tagging convention, not a general rule - revisit once the
	// real database/schema is confirmed (it may not use OSM-style type tags at all).
	const int64 NumRowsRead = Database.Execute(
		TEXT("SELECT b.building_id, b.building_type, bn.lat, bn.lon FROM buildings b ")
		TEXT("JOIN building_nodes bn ON b.building_id = bn.building_id ")
		TEXT("WHERE b.building_type = 'university' ")
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

	// Reference point for the local-meters projection: the centroid of every node just read, so the
	// whole cluster lands near the world origin regardless of where on Earth it actually sits (same
	// equirectangular approximation already validated by hand against this exact dataset - see
	// project memory).
	double SumLat = 0.0, SumLon = 0.0;
	int32 TotalPoints = 0;
	for (const TPair<int64, FDatabaseBuildingRow>& Pair : RowsById)
	{
		for (const FVector2D& LonLat : Pair.Value.LatLonPoints)
		{
			SumLon += LonLat.X;
			SumLat += LonLat.Y;
			TotalPoints++;
		}
	}
	const double RefLat = SumLat / TotalPoints;
	const double RefLon = SumLon / TotalPoints;

	// Stashed for LoadRoadsAndPOIsFromDatabase - see its own comment on why it needs this exact same
	// reference point rather than computing its own.
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

	int32 SpawnedCount = 0;
	for (const TPair<int64, FDatabaseBuildingRow>& Pair : RowsById)
	{
		const FDatabaseBuildingRow& Row = Pair.Value;

		TArray<FVector2D> Footprint;
		Footprint.Reserve(Row.LatLonPoints.Num());
		for (const FVector2D& LonLat : Row.LatLonPoints)
		{
			const double X = (LonLat.X - RefLon) * MetersPerDegreeLon;
			const double Y = (LonLat.Y - RefLat) * MetersPerDegreeLat;
			Footprint.Add(FVector2D((float)X, (float)Y));
		}

		// OSM closes a way by repeating its first node as the last one - SpawnBuildingFromData treats
		// its input as an already-open polygon and closes it itself, so strip that duplicate first.
		if (Footprint.Num() > 1 && Footprint[0].Equals(Footprint.Last(), 0.5f))
		{
			Footprint.RemoveAt(Footprint.Num() - 1);
		}

		const FString Id = FString::Printf(TEXT("SQL-%lld"), Pair.Key);
		const FString Name = FString::Printf(TEXT("%s %lld"), *Row.BuildingType, Pair.Key);
		if (SpawnBuildingFromData(Id, Name, Footprint, DefaultFloorCountForDatabaseBuildings, DefaultFloorHeightForDatabaseBuildings, FVector2D::ZeroVector))
		{
			SpawnedCount++;
		}
	}

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

void ABuildingGenerator::SetRoadCategoryVisible(ERoadCategory Category, bool bVisible)
{
	for (AActor* Actor : SpawnedRoadActors)
	{
		ARoadSegmentActor* RoadActor = Cast<ARoadSegmentActor>(Actor);
		if (RoadActor && RoadActor->GetCategory() == Category)
		{
			RoadActor->SetActorHiddenInGame(!bVisible);
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