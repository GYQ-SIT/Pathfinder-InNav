// Fill out your copyright notice in the Description page of Project Settings.


#include "BuildingGenerator.h"
#include "PolyBuilding.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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
		}
	}

	return SpawnedCount;
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