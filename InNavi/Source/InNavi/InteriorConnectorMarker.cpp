// Fill out your copyright notice in the Description page of Project Settings.

#include "InteriorConnectorMarker.h"
#include "PolygonGeometryUtils.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

// One part of a connector (the frame of a lift, the treads of a staircase, the glass of a balustrade ...): its own mesh section and its own material.
struct FConnectorPart
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	FLinearColor Color = FLinearColor::White;
	// Under 1 the part is drawn see-through (glass).
	float Opacity = 1.f;
};

namespace
{
	// How far a connector stays under the slab above it (centimetres): its top must not lie in the plane of the floor cap, which is where the cap is.
	constexpr float SlabClearance = 2.f;

	// Appends an axis-aligned box (in local XY, Z up). "Local" here means centered on the connector's own origin, before
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

	// A box from X0 to X1, Y0 to Y1 and Z0 to Z1 (each pair in rising order).
	void ConnectorBox(FConnectorPart& Part, float X0, float X1, float Y0, float Y1, float Z0, float Z1)
	{
		AppendAxisAlignedBox(Part.Vertices, Part.Triangles, FVector2D(X0, Y0), FVector2D(X1, Y1), Z0, Z1);
	}

	// A slanted slab, as long as a ramp: its centre line runs from (X0, Z0) to (X1, Z1) in the X-Z plane (X1 > X0), and the slab reaches Below under it
	// and Above over it, across Y0..Y1. The corner order is the one AppendWallBox winds outward for a right-handed (X, Y, Z) box, which a shear keeps.
	void ConnectorSlab(FConnectorPart& Part, float X0, float Z0, float X1, float Z1, float Below, float Above, float Y0, float Y1)
	{
		AppendWallBox(Part.Vertices, Part.Triangles,
			FVector(X0, Y0, Z0 - Below), FVector(X1, Y0, Z1 - Below), FVector(X0, Y1, Z0 - Below), FVector(X1, Y1, Z1 - Below),
			FVector(X0, Y0, Z0 + Above), FVector(X1, Y0, Z1 + Above), FVector(X0, Y1, Z0 + Above), FVector(X1, Y1, Z1 + Above));
	}

	// A ConnectorSlab whose top never goes over ZMax: where the centre line plus Above would pass it, the slab goes on flat at ZMax (a handrail that
	// has to stay under the slab above ends in a level stretch).
	void ConnectorClippedSlab(FConnectorPart& Part, float X0, float Z0, float X1, float Z1, float Below, float Above, float ZMax, float Y0, float Y1)
	{
		const float Limit = ZMax - Above;
		if (Z0 >= Limit)
		{
			ConnectorSlab(Part, X0, Limit, X1, Limit, Below, Above, Y0, Y1);
		}
		else if (Z1 <= Limit)
		{
			ConnectorSlab(Part, X0, Z0, X1, Z1, Below, Above, Y0, Y1);
		}
		else
		{
			const float XCorner = X0 + (X1 - X0) * (Limit - Z0) / (Z1 - Z0);
			ConnectorSlab(Part, X0, Z0, XCorner, Limit, Below, Above, Y0, Y1);
			ConnectorSlab(Part, XCorner, Limit, X1, Limit, Below, Above, Y0, Y1);
		}
	}

	// A wedge: flat underneath at ZBottom, its top rising from ZTop0 at X0 to ZTop1 at X1 (the side cladding of an escalator).
	void ConnectorWedge(FConnectorPart& Part, float X0, float X1, float Y0, float Y1, float ZBottom, float ZTop0, float ZTop1)
	{
		AppendWallBox(Part.Vertices, Part.Triangles,
			FVector(X0, Y0, ZBottom), FVector(X1, Y0, ZBottom), FVector(X0, Y1, ZBottom), FVector(X1, Y1, ZBottom),
			FVector(X0, Y0, ZTop0),   FVector(X1, Y0, ZTop1),   FVector(X0, Y1, ZTop0),   FVector(X1, Y1, ZTop1));
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
		BuildStairs(WorldCenter, FloorBaseZ, FloorHeight, RotationDegrees, SizeScale);
		break;
	case EInteriorNavNodeType::Escalator:
		BuildEscalator(WorldCenter, FloorBaseZ, FloorHeight, RotationDegrees, SizeScale);
		break;
	default:
		// PathNode - no 3D marker. SpawnConnectorMarker shouldn't even spawn one of these actors for
		// a PathNode, but leaving this branch harmless (an actor with an empty mesh) either way.
		break;
	}
}

void AInteriorConnectorMarker::CommitParts(TArray<FConnectorPart>& Parts, const FVector2D& WorldCenter, float RotationDegrees, float SizeScale)
{
	MeshComp->ClearAllMeshSections();
	PartMaterials.Reset();
	PartColors.Reset();

	UMaterialInterface* SolidMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	// The material the buildings fade with: it has an Opacity and a Color.
	UMaterialInterface* GlassMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ImageAssets/Materials/M_BuildingTransparent.M_BuildingTransparent"));

	int32 Section = 0;
	for (FConnectorPart& Part : Parts)
	{
		if (Part.Vertices.IsEmpty())
		{
			continue;
		}
		TransformFootprintVertices(Part.Vertices, WorldCenter, RotationDegrees, SizeScale);

		TArray<FVector> Normals;
		TArray<FVector2D> UV0;
		TArray<FProcMeshTangent> Tangents;
		UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Part.Vertices, Part.Triangles, UV0, Normals, Tangents);
		TArray<FColor> VertexColors;
		MeshComp->CreateMeshSection(Section, Part.Vertices, Part.Triangles, Normals, UV0, VertexColors, Tangents, true);

		const bool bGlass = Part.Opacity < 0.999f && GlassMaterial != nullptr;
		if (UMaterialInterface* BaseMaterial = bGlass ? GlassMaterial : SolidMaterial)
		{
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			Material->SetVectorParameterValue(TEXT("Color"), Part.Color);
			if (bGlass)
			{
				Material->SetScalarParameterValue(TEXT("Opacity"), Part.Opacity);
			}
			MeshComp->SetMaterial(Section, Material);
			PartMaterials.Add(Material);
			PartColors.Add(Part.Color);
		}
		++Section;
	}
}

void AInteriorConnectorMarker::BuildElevatorShaft(const FVector2D& WorldCenter, float FloorBaseZ, float FloorHeight, float RotationDegrees, float SizeScale)
{
	// A glass shaft with a steel frame, as one floor of it: four corner posts, a rail along the bottom and the top, glass on the back and the two
	// sides, and on the front (+X) a pair of sliding doors under a lintel with a floor indicator, and a call button on one jamb. The segments of
	// the floors stack into one tall shaft. Everything stays inside the connector's footprint.
	const FVector2D Half = GetConnectorFootprintHalfSize(EInteriorNavNodeType::Elevator);
	const float HX = Half.X;
	const float HY = Half.Y;
	constexpr float Post = 12.f;
	const float Floor = FloorBaseZ;
	// The posts end a little under the slab above (the building's floor cap lies exactly at FloorBaseZ + FloorHeight, and a top in that plane flickers
	// against it), and start the same little under this floor's own slab, so the posts of floors stacked into one shaft still overlap.
	const float Top = FloorBaseZ + FloorHeight - SlabClearance;
	const float Bottom = FloorBaseZ - SlabClearance;
	const float DoorHeight = FMath::Min(210.f, FMath::Max(60.f, FloorHeight - 50.f));
	const float OpenHalf = HY - Post;

	TArray<FConnectorPart> Parts;
	Parts.SetNum(5);
	FConnectorPart& Frame = Parts[0];
	FConnectorPart& Doors = Parts[1];
	FConnectorPart& Glass = Parts[2];
	FConnectorPart& Plate = Parts[3];
	FConnectorPart& Accent = Parts[4];
	Frame.Color = FLinearColor(0.40f, 0.43f, 0.48f);
	Doors.Color = FLinearColor(0.80f, 0.82f, 0.85f);
	Glass.Color = FLinearColor(0.70f, 0.86f, 0.96f);
	Glass.Opacity = 0.25f;
	Plate.Color = FLinearColor(0.55f, 0.56f, 0.55f);
	Accent.Color = FLinearColor(0.95f, 0.72f, 0.18f);

	// The frame: posts in the four corners, rails round the bottom and the top, and the lintel over the doors.
	for (const float SignX : { -1.f, 1.f })
	{
		for (const float SignY : { -1.f, 1.f })
		{
			ConnectorBox(Frame, FMath::Min(SignX * (HX - Post), SignX * HX), FMath::Max(SignX * (HX - Post), SignX * HX),
				FMath::Min(SignY * (HY - Post), SignY * HY), FMath::Max(SignY * (HY - Post), SignY * HY), Bottom, Top);
		}
	}
	for (const float RailZ : { Floor, Top - 8.f })
	{
		const float RailHeight = RailZ == Floor ? 4.f : 8.f;
		ConnectorBox(Frame, -HX, -HX + 8.f, -HY, HY, RailZ, RailZ + RailHeight);
		ConnectorBox(Frame, -HX, HX, HY - 8.f, HY, RailZ, RailZ + RailHeight);
		ConnectorBox(Frame, -HX, HX, -HY, -HY + 8.f, RailZ, RailZ + RailHeight);
	}
	ConnectorBox(Frame, HX - 12.f, HX, -HY, HY, Floor, Floor + 4.f); // the sill
	ConnectorBox(Frame, HX - 14.f, HX - 2.f, -HY, HY, Floor + DoorHeight, Top); // the lintel

	// The doors: two sliding leaves, a thin gap between them.
	ConnectorBox(Doors, HX - 8.f, HX - 4.f, -OpenHalf, -1.5f, Floor + 4.f, Floor + DoorHeight);
	ConnectorBox(Doors, HX - 8.f, HX - 4.f, 1.5f, OpenHalf, Floor + 4.f, Floor + DoorHeight);

	// The glass: the back and the two sides, between the rails.
	ConnectorBox(Glass, -HX + 2.f, -HX + 5.f, -(HY - Post), HY - Post, Floor + 4.f, Top - 8.f);
	ConnectorBox(Glass, -(HX - Post), HX - Post, HY - 5.f, HY - 2.f, Floor + 4.f, Top - 8.f);
	ConnectorBox(Glass, -(HX - Post), HX - Post, -(HY - 2.f), -(HY - 5.f), Floor + 4.f, Top - 8.f);

	// The floor inside the shaft.
	ConnectorBox(Plate, -(HX - Post), HX - Post, -(HY - Post), HY - Post, Floor, Floor + 2.5f);

	// A floor indicator over the doors, and a call button on the jamb.
	ConnectorBox(Accent, HX - 2.f, HX, -16.f, 16.f, Floor + DoorHeight + 10.f, Floor + DoorHeight + 24.f);
	ConnectorBox(Accent, HX - 2.f, HX, HY - Post + 2.f, HY - 2.f, Floor + 100.f, Floor + 130.f);

	CommitParts(Parts, WorldCenter, RotationDegrees, SizeScale);
}

void AInteriorConnectorMarker::BuildStairs(const FVector2D& WorldCenter, float FloorBaseZ, float FloorHeight, float RotationDegrees, float SizeScale)
{
	// One straight flight rising along +X: a tread and a riser for every step, a stringer along each side, a baluster every third step and a
	// handrail on both sides that levels out under the slab above.
	const FVector2D Half = GetConnectorFootprintHalfSize(EInteriorNavNodeType::Stairs);
	const float HX = Half.X;
	const float HY = Half.Y;
	const float Depth = HX * 2.f;
	const float Base = FloorBaseZ;
	const float Top = FloorBaseZ + FloorHeight - SlabClearance;
	// About 20 cm to a step. (The rise is not part of the footprint TransformFootprintVertices scales: a scaled-up connector gets a wider, deeper
	// staircase, not a taller one.)
	const int32 NumSteps = FMath::Clamp(FMath::RoundToInt(FloorHeight / 20.f), 8, 24);
	const float StepDepth = Depth / NumSteps;
	const float StepRise = FloorHeight / NumSteps;
	// The line the noses of the treads lie on (a little below at the first step, at the top of the last).
	auto NoseZ = [&](float X) { return Base + StepRise + (FloorHeight - StepRise) * (X + HX) / Depth; };

	TArray<FConnectorPart> Parts;
	Parts.SetNum(4);
	FConnectorPart& Treads = Parts[0];
	FConnectorPart& Risers = Parts[1];
	FConnectorPart& Structure = Parts[2];
	FConnectorPart& Rails = Parts[3];
	Treads.Color = FLinearColor(0.80f, 0.76f, 0.68f);
	Risers.Color = FLinearColor(0.50f, 0.46f, 0.40f);
	Structure.Color = FLinearColor(0.34f, 0.35f, 0.38f);
	Rails.Color = FLinearColor(0.20f, 0.21f, 0.23f);

	const float TreadHalfWidth = HY - 8.f;
	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		const float X0 = -HX + Step * StepDepth;
		// The last tread ends a little under the slab above, not in its plane (see SlabClearance).
		const float TreadTop = Base + (Step + 1) * StepRise - (Step + 1 == NumSteps ? SlabClearance : 0.f);
		ConnectorBox(Treads, X0, FMath::Min(X0 + StepDepth + 2.f, HX), -TreadHalfWidth, TreadHalfWidth, TreadTop - 4.f, TreadTop);
		ConnectorBox(Risers, X0, X0 + 3.f, -TreadHalfWidth, TreadHalfWidth, Base + Step * StepRise, TreadTop - 4.f);
	}

	// A stringer along each side, carrying the treads; it ends under the top of the last tread.
	for (const float SignY : { -1.f, 1.f })
	{
		const float Y0 = SignY > 0.f ? HY - 8.f : -HY;
		ConnectorSlab(Structure, -HX, Base + StepRise * 0.5f, HX, Base + FloorHeight - StepRise * 0.5f - 4.f, 10.f, 8.f, Y0, Y0 + 8.f);
	}

	// The handrail: about 90 cm over the noses, ending in a level stretch under the slab above; balusters under it, and a newel at each end.
	const float RailTop = Top - 4.f;
	auto RailZ = [&](float X) { return FMath::Min(NoseZ(X) + 90.f, RailTop); };
	for (const float SignY : { -1.f, 1.f })
	{
		const float RailY = SignY * (HY - 4.f);
		ConnectorClippedSlab(Rails, -HX, NoseZ(-HX) + 90.f, HX, NoseZ(HX) + 90.f, 2.5f, 2.5f, Top, RailY - 2.5f, RailY + 2.5f);
		for (int32 Step = 0; Step < NumSteps; Step += 3)
		{
			const float X = -HX + (Step + 0.5f) * StepDepth;
			const float Z0 = Base + (Step + 1) * StepRise;
			const float Z1 = RailZ(X) - 2.5f;
			if (Z1 > Z0 + 10.f)
			{
				ConnectorBox(Rails, X - 1.5f, X + 1.5f, RailY - 1.5f, RailY + 1.5f, Z0, Z1);
			}
		}
		ConnectorBox(Rails, -HX + 1.f, -HX + 7.f, RailY - 3.f, RailY + 3.f, Base, FMath::Min(NoseZ(-HX + 4.f) + 100.f, RailTop));
		ConnectorBox(Rails, HX - 7.f, HX - 1.f, RailY - 3.f, RailY + 3.f, Top - 70.f, RailTop);
	}

	CommitParts(Parts, WorldCenter, RotationDegrees, SizeScale);
}

void AInteriorConnectorMarker::BuildEscalator(const FVector2D& WorldCenter, float FloorBaseZ, float FloorHeight, float RotationDegrees, float SizeScale)
{
	// A run of narrow steps rising along +X between two side claddings; over each cladding a glass balustrade and a black handrail, which level
	// out under the slab above; a comb plate at each end where the steps meet the floor.
	const FVector2D Half = GetConnectorFootprintHalfSize(EInteriorNavNodeType::Escalator);
	const float HX = Half.X;
	const float HY = Half.Y;
	const float Depth = HX * 2.f;
	const float Base = FloorBaseZ;
	const float Top = FloorBaseZ + FloorHeight - SlabClearance;
	const int32 NumSteps = FMath::Clamp(FMath::RoundToInt(FloorHeight / 12.5f), 10, 32);
	const float StepDepth = Depth / NumSteps;
	const float StepRise = FloorHeight / NumSteps;
	auto StepZ = [&](float X) { return Base + StepRise + (FloorHeight - StepRise) * (X + HX) / Depth; };

	TArray<FConnectorPart> Parts;
	Parts.SetNum(6);
	FConnectorPart& Steps = Parts[0];
	FConnectorPart& Risers = Parts[1];
	FConnectorPart& Cladding = Parts[2];
	FConnectorPart& Glass = Parts[3];
	FConnectorPart& Handrails = Parts[4];
	FConnectorPart& Plates = Parts[5];
	Steps.Color = FLinearColor(0.16f, 0.17f, 0.18f);
	Risers.Color = FLinearColor(0.70f, 0.72f, 0.75f);
	Cladding.Color = FLinearColor(0.48f, 0.52f, 0.57f);
	Glass.Color = FLinearColor(0.72f, 0.86f, 0.96f);
	Glass.Opacity = 0.3f;
	Handrails.Color = FLinearColor(0.05f, 0.05f, 0.06f);
	Plates.Color = FLinearColor(0.78f, 0.80f, 0.82f);

	const float StepHalfWidth = HY - 14.f;
	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		const float X0 = -HX + Step * StepDepth;
		const float TreadTop = Base + (Step + 1) * StepRise - (Step + 1 == NumSteps ? SlabClearance : 0.f);
		ConnectorBox(Steps, X0, X0 + StepDepth, -StepHalfWidth, StepHalfWidth, TreadTop - 2.5f, TreadTop);
		ConnectorBox(Risers, X0, X0 + 2.f, -StepHalfWidth, StepHalfWidth, TreadTop - StepRise, TreadTop - 2.5f);
	}

	for (const float SignY : { -1.f, 1.f })
	{
		// The cladding at the side of the steps: a wedge from the floor up to the line of the steps, with the balustrade standing on it.
		const float CladY0 = SignY > 0.f ? HY - 14.f : -(HY - 8.f);
		ConnectorWedge(Cladding, -HX, HX, CladY0, CladY0 + 6.f, Base, Base + StepRise + 4.f, Top - 2.f);
		const float RailY = SignY * (HY - 9.f);
		ConnectorClippedSlab(Glass, -HX, StepZ(-HX) + 50.f, HX, StepZ(HX) + 50.f, 36.f, 36.f, Top, RailY - 1.f, RailY + 1.f);
		ConnectorClippedSlab(Handrails, -HX, StepZ(-HX) + 95.f, HX, StepZ(HX) + 95.f, 3.f, 3.f, Top, RailY - 4.f, RailY + 4.f);
	}

	// The comb plates where the steps go under the floor, at the lower end and at the upper end.
	ConnectorBox(Plates, -HX, -HX + 24.f, -StepHalfWidth, StepHalfWidth, Base, Base + 3.f);
	ConnectorBox(Plates, HX - 24.f, HX, -StepHalfWidth, StepHalfWidth, Top - 3.f, Top);

	CommitParts(Parts, WorldCenter, RotationDegrees, SizeScale);
}

void AInteriorConnectorMarker::SetHighlighted(bool bHighlighted)
{
	for (int32 Index = 0; Index < PartMaterials.Num(); ++Index)
	{
		if (PartMaterials[Index])
		{
			PartMaterials[Index]->SetVectorParameterValue(TEXT("Color"), bHighlighted ? FLinearColor(0.55f, 1.0f, 0.6f) : PartColors[Index]);
		}
	}
}
