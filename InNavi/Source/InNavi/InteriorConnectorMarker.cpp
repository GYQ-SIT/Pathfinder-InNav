// Fill out your copyright notice in the Description page of Project Settings.

#include "InteriorConnectorMarker.h"
#include "PolygonGeometryUtils.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// Appends an axis-aligned box (in local XY, Z up) - used for the elevator's corner posts and
	// each stair/escalator step. "Local" here means centered on the connector's own origin, before
	// TransformFootprintVertices below applies its actual position/rotation/scale.
	void AppendAxisAlignedBox(TArray<FVector>& Vertices, TArray<int32>& Triangles,
		const FVector2D& MinCorner, const FVector2D& MaxCorner, float ZBottom, float ZTop)
	{
		AppendWallBox(Vertices, Triangles,
			FVector(MinCorner.X, MinCorner.Y, ZBottom), FVector(MaxCorner.X, MinCorner.Y, ZBottom),
			FVector(MinCorner.X, MaxCorner.Y, ZBottom), FVector(MaxCorner.X, MaxCorner.Y, ZBottom),
			FVector(MinCorner.X, MinCorner.Y, ZTop),    FVector(MaxCorner.X, MinCorner.Y, ZTop),
			FVector(MinCorner.X, MaxCorner.Y, ZTop),    FVector(MaxCorner.X, MaxCorner.Y, ZTop));
	}

	// Appends a thin box running from A to B (in local XY) with the given thickness - used for the
	// elevator shaft's bottom rail, one segment per side of its square footprint.
	void AppendHorizontalBox(TArray<FVector>& Vertices, TArray<int32>& Triangles,
		const FVector2D& A, const FVector2D& B, float Thickness, float ZBottom, float ZTop)
	{
		const float Length = (B - A).Size();
		if (Length <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		const FVector2D Dir = (B - A) / Length;
		const FVector2D Perp(-Dir.Y, Dir.X);
		const float Half = Thickness * 0.5f;

		const FVector2D Inner0 = A - Perp * Half;
		const FVector2D Inner1 = B - Perp * Half;
		const FVector2D Outer0 = A + Perp * Half;
		const FVector2D Outer1 = B + Perp * Half;

		AppendWallBox(Vertices, Triangles,
			FVector(Inner0.X, Inner0.Y, ZBottom), FVector(Inner1.X, Inner1.Y, ZBottom),
			FVector(Outer0.X, Outer0.Y, ZBottom), FVector(Outer1.X, Outer1.Y, ZBottom),
			FVector(Inner0.X, Inner0.Y, ZTop),    FVector(Inner1.X, Inner1.Y, ZTop),
			FVector(Outer0.X, Outer0.Y, ZTop),    FVector(Outer1.X, Outer1.Y, ZTop));
	}

	// Rotates (about the local origin, positive = counter-clockwise - matching
	// GetConnectorFootprintCorners/RotateRoom's convention) and uniformly scales every vertex's XY in
	// place, then translates to WorldCenter - applied once at the end so the box-building code above
	// can stay simple (local, axis-aligned, centered on the origin) regardless of the connector's own
	// rotation/scale. Z (floor height) is left untouched - a bigger connector gets wider/deeper, not
	// taller.
	void TransformFootprintVertices(TArray<FVector>& Vertices, const FVector2D& WorldCenter, float RotationDegrees, float SizeScale)
	{
		const float Radians = FMath::DegreesToRadians(RotationDegrees);
		const float CosA = FMath::Cos(Radians);
		const float SinA = FMath::Sin(Radians);
		for (FVector& Vertex : Vertices)
		{
			const float LocalX = Vertex.X * SizeScale;
			const float LocalY = Vertex.Y * SizeScale;
			Vertex.X = WorldCenter.X + LocalX * CosA - LocalY * SinA;
			Vertex.Y = WorldCenter.Y + LocalX * SinA + LocalY * CosA;
		}
	}
}

AInteriorConnectorMarker::AInteriorConnectorMarker()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComp = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;

	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
}

void AInteriorConnectorMarker::BuildMarker(EInteriorNavNodeType Type, const FVector2D& WorldCenter, float FloorBaseZ, float FloorHeight, float RotationDegrees, float SizeScale)
{
	switch (Type)
	{
	case EInteriorNavNodeType::Elevator:
		BuildElevatorShaft(WorldCenter, FloorBaseZ, FloorHeight, RotationDegrees, SizeScale);
		break;
	case EInteriorNavNodeType::Stairs:
		BuildStairsSteps(WorldCenter, FloorBaseZ, FloorHeight, RotationDegrees, SizeScale, /*bIsEscalator=*/false);
		break;
	case EInteriorNavNodeType::Escalator:
		BuildStairsSteps(WorldCenter, FloorBaseZ, FloorHeight, RotationDegrees, SizeScale, /*bIsEscalator=*/true);
		break;
	default:
		// PathNode - no 3D marker. SpawnConnectorMarker shouldn't even spawn one of these actors for
		// a PathNode, but leaving this branch harmless (an actor with an empty mesh) either way.
		break;
	}
}

void AInteriorConnectorMarker::BuildElevatorShaft(const FVector2D& WorldCenter, float FloorBaseZ, float FloorHeight, float RotationDegrees, float SizeScale)
{
	// A "cage" rather than a solid box - 4 corner posts the full floor height, plus a rail looping
	// the bottom of this floor segment. Multiple floors of the same connector stack into a
	// continuous shaft with a rail marking each floor boundary.
	const FVector2D HalfSize = GetConnectorFootprintHalfSize(EInteriorNavNodeType::Elevator);
	constexpr float PostThickness = 16.f;
	constexpr float RailThickness = 12.f;
	constexpr float RailHeight = 12.f;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;

	// Local space - centered on the origin, unrotated/unscaled. TransformFootprintVertices applies
	// WorldCenter/RotationDegrees/SizeScale to everything (posts, rail, and their own thickness all
	// scale together) in one pass at the end.
	const FVector2D Corners[4] =
	{
		FVector2D(-HalfSize.X, -HalfSize.Y),
		FVector2D( HalfSize.X, -HalfSize.Y),
		FVector2D( HalfSize.X,  HalfSize.Y),
		FVector2D(-HalfSize.X,  HalfSize.Y),
	};

	const float Top = FloorBaseZ + FloorHeight;
	const float HalfPost = PostThickness * 0.5f;
	for (const FVector2D& Corner : Corners)
	{
		AppendAxisAlignedBox(Vertices, Triangles,
			FVector2D(Corner.X - HalfPost, Corner.Y - HalfPost),
			FVector2D(Corner.X + HalfPost, Corner.Y + HalfPost),
			FloorBaseZ, Top);
	}

	for (int32 i = 0; i < 4; i++)
	{
		AppendHorizontalBox(Vertices, Triangles, Corners[i], Corners[(i + 1) % 4], RailThickness, FloorBaseZ, FloorBaseZ + RailHeight);
	}

	TransformFootprintVertices(Vertices, WorldCenter, RotationDegrees, SizeScale);

	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FProcMeshTangent> Tangents;
	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices, Triangles, UV0, Normals, Tangents);

	TArray<FColor> VertexColors;
	MeshComp->CreateMeshSection(0, Vertices, Triangles, Normals, UV0, VertexColors, Tangents, true);

	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMat)
	{
		DynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
		DynMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 0.58f, 0.62f));
		MeshComp->SetMaterial(0, DynMaterial);
	}
}

void AInteriorConnectorMarker::BuildStairsSteps(const FVector2D& WorldCenter, float FloorBaseZ, float FloorHeight, float RotationDegrees, float SizeScale, bool bIsEscalator)
{
	// A solid ascending staircase silhouette (each step's box extends the rest of the run and is one
	// step taller than the last, rather than floating separate steps) - reads as stairs from the
	// side without needing separate riser/tread geometry. Escalator reuses the exact same shape (per
	// the approved plan), just its own tint below, since a moving-steps look isn't worth building
	// for a placeholder pass.
	const FVector2D HalfSize = GetConnectorFootprintHalfSize(bIsEscalator ? EInteriorNavNodeType::Escalator : EInteriorNavNodeType::Stairs);
	const float Depth = HalfSize.X * 2.f;
	constexpr int32 NumSteps = 10;

	const float StepDepth = Depth / NumSteps;
	// Rise isn't part of the footprint TransformFootprintVertices scales, so it's still computed
	// straight from FloorHeight - a scaled-up connector gets a wider/deeper staircase, not a taller
	// one (same reasoning as the elevator's rail/post thickness NOT scaling FloorHeight).
	const float StepRise = FloorHeight / NumSteps;
	const float HalfWidth = HalfSize.Y;
	// Steps rise along local +X, centered on the origin - TransformFootprintVertices applies
	// WorldCenter/RotationDegrees/SizeScale afterward, same as the elevator above.
	const float StartX = -HalfSize.X;
	const float EndX = HalfSize.X;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;

	for (int32 i = 0; i < NumSteps; i++)
	{
		const float StepStartX = StartX + i * StepDepth;
		const float StepTopZ = FloorBaseZ + (i + 1) * StepRise;
		AppendAxisAlignedBox(Vertices, Triangles,
			FVector2D(StepStartX, -HalfWidth),
			FVector2D(EndX, HalfWidth),
			FloorBaseZ, StepTopZ);
	}

	TransformFootprintVertices(Vertices, WorldCenter, RotationDegrees, SizeScale);

	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FProcMeshTangent> Tangents;
	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices, Triangles, UV0, Normals, Tangents);

	TArray<FColor> VertexColors;
	MeshComp->CreateMeshSection(0, Vertices, Triangles, Normals, UV0, VertexColors, Tangents, true);

	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMat)
	{
		DynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
		// Stairs: warm tan. Escalator: same shape, a cooler steel-blue tint instead, so the two read
		// as different connectors at a glance despite sharing geometry.
		DynMaterial->SetVectorParameterValue(TEXT("Color"),
			bIsEscalator ? FLinearColor(0.45f, 0.55f, 0.62f) : FLinearColor(0.68f, 0.58f, 0.42f));
		MeshComp->SetMaterial(0, DynMaterial);
	}
}
