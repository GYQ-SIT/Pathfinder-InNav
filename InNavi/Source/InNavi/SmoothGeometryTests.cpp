#if WITH_DEV_AUTOMATION_TESTS

#include "PolygonGeometryUtils.h"
#include "PolyBuilding.h"
#include "InteriorRoom.h"
#include "Materials/Material.h"
#include "RoadSegmentActor.h"
#include "ProceduralMeshComponent.h"
#include "Misc/AutomationTest.h"

// Walls and roads are drawn as continuous, smooth surfaces: a wall ring with mitred corners and chosen normals, and a flat ribbon for a road
// with its bends rounded. (They used to be one box per edge, which left a notch at every corner and shaded every piece differently.)

namespace SmoothGeometryTests
{
	// Every triangle faces the way the normals of its corners say, by the rule the engine itself uses for the facing of a triangle.
	// A face that fails this is drawn inside out and lit the wrong way round.
	bool FacesAgreeWithNormals(const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals)
	{
		for (int32 i = 0; i + 2 < Triangles.Num(); i += 3)
		{
			const int32 A = Triangles[i], B = Triangles[i + 1], C = Triangles[i + 2];
			const FVector Facing = (Vertices[B] - Vertices[C]) ^ (Vertices[A] - Vertices[C]);
			if (Facing.SizeSquared() > KINDA_SMALL_NUMBER && FVector::DotProduct(Facing, Normals[A] + Normals[B] + Normals[C]) <= 0.f)
			{
				return false;
			}
		}
		return true;
	}

	bool HasVertexAt(const TArray<FVector>& Vertices, const FVector& Position, float Tolerance = 0.05f)
	{
		return Vertices.ContainsByPredicate([&](const FVector& V) { return FVector::Dist(V, Position) <= Tolerance; });
	}

	TArray<FVector2D> Square(float Size)
	{
		return { FVector2D(0, 0), FVector2D(Size, 0), FVector2D(Size, Size), FVector2D(0, Size) };
	}

	TArray<FVector2D> Circle(float Radius, int32 Points)
	{
		TArray<FVector2D> Result;
		for (int32 i = 0; i < Points; ++i)
		{
			const float Angle = 2.f * UE_PI * i / Points;
			Result.Add(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Result;
	}

	// Every edge of a polygon as one piece of wall the whole height, the way AppendWallEdges is given them.
	TArray<FWallEdge> FullHeightEdges(const TArray<FVector2D>& Poly, bool bClosed, float Height)
	{
		TArray<FWallEdge> Edges;
		const int32 Count = bClosed ? Poly.Num() : Poly.Num() - 1;
		for (int32 i = 0; i < Count; ++i)
		{
			FWallEdge Edge;
			Edge.A = Poly[i];
			Edge.B = Poly[(i + 1) % Poly.Num()];
			FWallPiece Piece;
			Piece.T1 = FVector2D::Distance(Edge.A, Edge.B);
			Piece.Z1 = Height;
			Edge.Pieces.Add(Piece);
			Edges.Add(Edge);
		}
		return Edges;
	}

	// The whole wall ring of a polygon, as spans that run on into each other at the corners.
	FShadedMesh WallRing(const TArray<FVector2D>& Poly, float HalfThickness, float Height)
	{
		TArray<FWallCorner> Corners;
		ComputeWallCorners(Poly, HalfThickness, Corners);
		FShadedMesh Mesh;
		for (int32 i = 0; i < Poly.Num(); ++i)
		{
			const int32 Next = (i + 1) % Poly.Num();
			AppendWallSpan(Mesh, MakeCornerWallEnd(Poly[i], Corners[i], true), MakeCornerWallEnd(Poly[Next], Corners[Next], false), 0.f, Height);
		}
		return Mesh;
	}
}

// Two walls that meet share one mitred corner: no notch outside, no cap inside the wall, and the corner of a square stays crisp.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWallRingCornersTest, "InNavi.Geometry.WallRingCorners", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWallRingCornersTest::RunTest(const FString& Parameters)
{
	using namespace SmoothGeometryTests;
	const FShadedMesh Ring = WallRing(Square(1000.f), 10.f, 300.f);

	// Four spans of two long faces, a top and a bottom: 4 quads of 4 corners each. A cap would add more.
	TestEqual(TEXT("no cap inside the ring: only the two long faces, the top and the bottom of each span"), Ring.Vertices.Num(), 4 * 4 * 4);

	TestTrue(TEXT("the outer corner is the mitre point, 10 out along both axes"), HasVertexAt(Ring.Vertices, FVector(-10, -10, 0)) && HasVertexAt(Ring.Vertices, FVector(1010, 1010, 300)));
	TestTrue(TEXT("the inner corner too"), HasVertexAt(Ring.Vertices, FVector(10, 10, 300)) && HasVertexAt(Ring.Vertices, FVector(990, 990, 0)));

	bool bAllCrisp = true, bAllUnit = true;
	for (const FVector& Normal : Ring.Normals)
	{
		bAllUnit &= FMath::IsNearlyEqual(Normal.Size(), 1.f, 0.001f);
		// A right angle is a crisp edge: a normal that leans both ways at once would be a blend of the two faces.
		bAllCrisp &= !(FMath::Abs(Normal.X) > 0.01f && FMath::Abs(Normal.Y) > 0.01f);
	}
	TestTrue(TEXT("every normal is a unit vector"), bAllUnit);
	TestTrue(TEXT("a right-angle corner is not blended"), bAllCrisp);
	TestTrue(TEXT("every face points the way its normals say"), FacesAgreeWithNormals(Ring.Vertices, Ring.Triangles, Ring.Normals));
	return true;
}

// A wall that ends beside a door gets a cap; the faces still point outward.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWallSpanCapTest, "InNavi.Geometry.WallSpanCaps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWallSpanCapTest::RunTest(const FString& Parameters)
{
	using namespace SmoothGeometryTests;
	FShadedMesh Mesh;
	const FVector2D Perp(0.f, 1.f);
	AppendWallSpan(Mesh, MakePlainWallEnd(FVector2D(0, 0), Perp, 10.f), MakePlainWallEnd(FVector2D(400, 0), Perp, 10.f), 0.f, 300.f);
	TestEqual(TEXT("two long faces, a top, a bottom and a cap at each end"), Mesh.Vertices.Num(), 6 * 4);
	TestTrue(TEXT("every face points the way its normals say"), FacesAgreeWithNormals(Mesh.Vertices, Mesh.Triangles, Mesh.Normals));
	TestTrue(TEXT("the wall is as thick as asked"), HasVertexAt(Mesh.Vertices, FVector(0, 10, 300)) && HasVertexAt(Mesh.Vertices, FVector(400, -10, 0)));
	return true;
}

// A bend of many short edges (a curved front) is blended into one surface; a long wall with a slight bend keeps its flat faces.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWallBendSmoothingTest, "InNavi.Geometry.WallBendSmoothing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWallBendSmoothingTest::RunTest(const FString& Parameters)
{
	using namespace SmoothGeometryTests;

	const TArray<FVector2D> Round = Circle(2000.f, 24);
	TArray<FWallCorner> Corners;
	ComputeWallCorners(Round, 10.f, Corners);
	bool bSmooth = true, bRadial = true;
	for (int32 i = 0; i < Round.Num(); ++i)
	{
		bSmooth &= Corners[i].PrevNormal.Equals(Corners[i].NextNormal, 0.001f);
		// Round the circle the two sides of the wall face away from and towards the centre.
		bRadial &= FMath::Abs(FVector2D::DotProduct(Corners[i].PrevNormal, Round[i].GetSafeNormal())) > 0.999f;
	}
	TestTrue(TEXT("a curve of short edges has one normal at each vertex"), bSmooth);
	TestTrue(TEXT("and it points along the radius"), bRadial);

	// Two edges of 3000 turning 20 degrees: too long to blend.
	const FVector2D P1(3000, 0);
	const FVector2D P2 = P1 + FVector2D(FMath::Cos(FMath::DegreesToRadians(20.f)), FMath::Sin(FMath::DegreesToRadians(20.f))) * 3000.f;
	ComputeWallCorners({ FVector2D(0, 0), P1, P2, FVector2D(0, 3000) }, 10.f, Corners);
	TestFalse(TEXT("a long wall keeps its flat faces at a slight bend"), Corners[1].PrevNormal.Equals(Corners[1].NextNormal, 0.01f));

	// The wall ring of the curve: no slanted normals, every face outward.
	const FShadedMesh Ring = WallRing(Round, 10.f, 300.f);
	TestTrue(TEXT("every face points the way its normals say"), FacesAgreeWithNormals(Ring.Vertices, Ring.Triangles, Ring.Normals));
	return true;
}

// The bends of a road are rounded, its ends and its straight parts stay where they are.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPolylineSmoothingTest, "InNavi.Geometry.PolylineSmoothing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPolylineSmoothingTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Out;

	SmoothPolyline({ FVector2D(0, 0), FVector2D(1000, 0), FVector2D(1000, 1000) }, 200.f, Out);
	TestTrue(TEXT("the ends stay"), Out[0].Equals(FVector2D(0, 0)) && Out.Last().Equals(FVector2D(1000, 1000)));
	TestTrue(TEXT("the arc starts 200 before the bend and ends 200 after"), Out.ContainsByPredicate([](const FVector2D& P) { return P.Equals(FVector2D(800, 0), 0.01f); })
		&& Out.ContainsByPredicate([](const FVector2D& P) { return P.Equals(FVector2D(1000, 200), 0.01f); }));
	TestFalse(TEXT("the sharp corner itself is gone"), Out.ContainsByPredicate([](const FVector2D& P) { return P.Equals(FVector2D(1000, 0), 0.01f); }));
	TestTrue(TEXT("with several points on the arc"), Out.Num() >= 5);
	bool bInside = true, bSteps = true;
	for (int32 i = 0; i < Out.Num(); ++i)
	{
		bInside &= Out[i].X >= -0.01f && Out[i].X <= 1000.01f && Out[i].Y >= -0.01f && Out[i].Y <= 1000.01f;
		if (i > 0)
		{
			bSteps &= FVector2D::Distance(Out[i], Out[i - 1]) < 1000.f;
		}
	}
	TestTrue(TEXT("the curve stays inside the corner it cuts"), bInside);
	TestTrue(TEXT("and has no jump"), bSteps);

	// Pieces too short for the asked cut give up at most half of themselves, so neighbouring arcs never meet.
	SmoothPolyline({ FVector2D(0, 0), FVector2D(100, 0), FVector2D(100, 100), FVector2D(200, 100) }, 5000.f, Out);
	bool bOrdered = true;
	for (int32 i = 1; i < Out.Num(); ++i)
	{
		bOrdered &= Out[i].X >= Out[i - 1].X - 0.01f || Out[i].Y >= Out[i - 1].Y - 0.01f;
	}
	TestTrue(TEXT("short pieces do not loop back"), bOrdered && Out[0].Equals(FVector2D(0, 0)) && Out.Last().Equals(FVector2D(200, 100)));

	// A straight line has no bend to round.
	const TArray<FVector2D> Straight = { FVector2D(0, 0), FVector2D(500, 0), FVector2D(1000, 0) };
	SmoothPolyline(Straight, 200.f, Out);
	TestEqual(TEXT("a straight line is left alone"), Out.Num(), 3);

	// A closed loop is rounded all the way round and stays closed.
	SmoothPolyline({ FVector2D(0, 0), FVector2D(1000, 0), FVector2D(1000, 1000), FVector2D(0, 1000), FVector2D(0, 0) }, 200.f, Out);
	TestTrue(TEXT("a closed loop stays closed"), Out.Num() > 5 && Out[0].Equals(Out.Last(), 0.01f));
	TestFalse(TEXT("with no sharp corner left"), Out.ContainsByPredicate([](const FVector2D& P) { return P.Equals(FVector2D(1000, 1000), 0.01f); }));
	return true;
}

// A road is a flat ribbon: mitred at a bend, closed up at its ends, seen from above and below, every face right.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlatRibbonTest, "InNavi.Geometry.FlatRibbon", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFlatRibbonTest::RunTest(const FString& Parameters)
{
	using namespace SmoothGeometryTests;

	FShadedMesh Bend;
	AppendFlatRibbon(Bend, { FVector2D(0, 0), FVector2D(1000, 0), FVector2D(1000, 1000) }, 100.f, 5.f, /*bTwoSided=*/true, /*bRoundEnds=*/false);
	bool bFlat = true, bUp = false, bDown = false;
	for (int32 i = 0; i < Bend.Vertices.Num(); ++i)
	{
		bFlat &= FMath::IsNearlyEqual(Bend.Vertices[i].Z, 5.f);
		bUp |= Bend.Normals[i].Z > 0.99f;
		bDown |= Bend.Normals[i].Z < -0.99f;
	}
	TestTrue(TEXT("no thickness: every vertex is at the road's height"), bFlat);
	TestTrue(TEXT("it faces up and, as a second copy, down"), bUp && bDown);
	TestTrue(TEXT("the outside of the bend is the mitre corner, no gap"), HasVertexAt(Bend.Vertices, FVector(1100, -100, 5)));
	TestTrue(TEXT("and the inside of it too"), HasVertexAt(Bend.Vertices, FVector(900, 100, 5)));
	TestTrue(TEXT("every face points the way its normals say"), FacesAgreeWithNormals(Bend.Vertices, Bend.Triangles, Bend.Normals));

	FShadedMesh Butt, Round;
	AppendFlatRibbon(Butt, { FVector2D(0, 0), FVector2D(1000, 0) }, 100.f, 5.f, false, /*bRoundEnds=*/false);
	AppendFlatRibbon(Round, { FVector2D(0, 0), FVector2D(1000, 0) }, 100.f, 5.f, false, /*bRoundEnds=*/true);
	auto MaxX = [](const FShadedMesh& Mesh) { float Result = -FLT_MAX; for (const FVector& V : Mesh.Vertices) Result = FMath::Max(Result, V.X); return Result; };
	auto MinX = [](const FShadedMesh& Mesh) { float Result = FLT_MAX; for (const FVector& V : Mesh.Vertices) Result = FMath::Min(Result, V.X); return Result; };
	TestTrue(TEXT("without round ends the ribbon stops at its end points"), FMath::IsNearlyEqual(MinX(Butt), 0.f, 0.01f) && FMath::IsNearlyEqual(MaxX(Butt), 1000.f, 0.01f));
	TestTrue(TEXT("with them it reaches half a width past each end"), FMath::IsNearlyEqual(MinX(Round), -100.f, 0.01f) && FMath::IsNearlyEqual(MaxX(Round), 1100.f, 0.01f));
	TestTrue(TEXT("and those faces point up as well"), FacesAgreeWithNormals(Round.Vertices, Round.Triangles, Round.Normals));

	FShadedMesh Loop;
	AppendFlatRibbon(Loop, { FVector2D(0, 0), FVector2D(1000, 0), FVector2D(1000, 1000), FVector2D(0, 1000), FVector2D(0, 0) }, 100.f, 5.f, false, true);
	TestEqual(TEXT("a closed loop has no seam and no ends: 4 points, two corners each"), Loop.Vertices.Num(), 8);
	return true;
}

// The road actor builds that ribbon: flat at the height it was given, and casting no shadow.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadIsFlatTest, "InNavi.Geometry.RoadIsFlat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadIsFlatTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ARoadSegmentActor* Road = World->SpawnActor<ARoadSegmentActor>();
	Road->BuildRoad({ FVector2D(0, 0), FVector2D(1000, 0), FVector2D(1000, 500) }, 400.f, 3.f, FLinearColor::Gray);
	UProceduralMeshComponent* Mesh = Road->FindComponentByClass<UProceduralMeshComponent>();
	TestNotNull(TEXT("the road has a mesh"), Mesh);
	if (Mesh)
	{
		const FProcMeshSection* Section = Mesh->GetProcMeshSection(0);
		TestTrue(TEXT("it has a section"), Section && Section->ProcVertexBuffer.Num() > 0);
		bool bFlat = true;
		if (Section)
		{
			for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
			{
				bFlat &= FMath::IsNearlyEqual(Vertex.Position.Z, 3.f);
			}
		}
		TestTrue(TEXT("every vertex is at the height given: no thickness"), bFlat);
		TestFalse(TEXT("a road casts no shadow"), Mesh->CastShadow);
	}
	World->DestroyWorld(false);
	return true;
}

// The exterior shell of a building with a curved front: one smooth surface, every face outward.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExteriorWallRingTest, "InNavi.Geometry.ExteriorWallRing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExteriorWallRingTest::RunTest(const FString& Parameters)
{
	using namespace SmoothGeometryTests;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	APolyBuilding* Building = World->SpawnActor<APolyBuilding>();
	Building->BuildFromFootprint(Circle(2000.f, 24), 1.f, FVector2D::ZeroVector, 2, 300.f);

	UProceduralMeshComponent* Mesh = Building->FindComponentByClass<UProceduralMeshComponent>();
	TestNotNull(TEXT("the building has a mesh"), Mesh);
	if (Mesh)
	{
		for (int32 Level = 1; Level <= 2; ++Level)
		{
			const FProcMeshSection* Section = Mesh->GetProcMeshSection(Building->GetFloorCount() + Level);
			TestTrue(*FString::Printf(TEXT("floor %d has a wall"), Level), Section && Section->ProcVertexBuffer.Num() > 0);
			if (!Section)
			{
				continue;
			}
			TArray<FVector> Vertices, Normals;
			for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
			{
				Vertices.Add(Vertex.Position);
				Normals.Add(Vertex.Normal);
			}
			// 24 edges of two long faces, a top and a bottom, no cap between them.
			TestEqual(*FString::Printf(TEXT("floor %d: one continuous ring, no cap between the pieces"), Level), Vertices.Num(), 24 * 4 * 4);
			bool bClean = true;
			for (const FVector& Normal : Normals)
			{
				// Sides are horizontal and the top and bottom straight up or down: never a slanted blend of the two.
				bClean &= FMath::IsNearlyZero(Normal.Z, 0.01f) || FMath::IsNearlyEqual(FMath::Abs(Normal.Z), 1.f, 0.01f);
			}
			TestTrue(*FString::Printf(TEXT("floor %d: no normal leans between a side and the top"), Level), bClean);
			TArray<int32> Indices;
			for (const uint32 Index : Section->ProcIndexBuffer)
			{
				Indices.Add(static_cast<int32>(Index));
			}
			TestTrue(*FString::Printf(TEXT("floor %d: every face points the way its normals say"), Level), FacesAgreeWithNormals(Vertices, Indices, Normals));
		}
	}
	World->DestroyWorld(false);
	return true;
}

// The walls of a room: one continuous ring, free-standing walls end square, and the material is opaque so they never look hollow.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomWallRingTest, "InNavi.Geometry.RoomWallRing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoomWallRingTest::RunTest(const FString& Parameters)
{
	using namespace SmoothGeometryTests;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);

	auto SectionOf = [](AInteriorRoom* Room, TArray<FVector>& Vertices, TArray<FVector>& Normals, TArray<int32>& Indices) -> UProceduralMeshComponent*
	{
		UProceduralMeshComponent* Mesh = Room->FindComponentByClass<UProceduralMeshComponent>();
		const FProcMeshSection* Section = Mesh ? Mesh->GetProcMeshSection(0) : nullptr;
		if (Section)
		{
			for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
			{
				Vertices.Add(Vertex.Position);
				Normals.Add(Vertex.Normal);
			}
			for (const uint32 Index : Section->ProcIndexBuffer)
			{
				Indices.Add(static_cast<int32>(Index));
			}
		}
		return Mesh;
	};

	// A room: four walls that run on into each other.
	AInteriorRoom* Room = World->SpawnActor<AInteriorRoom>();
	Room->BuildWalls({ FVector2D(0, 0), FVector2D(10, 0), FVector2D(10, 10), FVector2D(0, 10) }, 100.f, FVector2D::ZeroVector, 0.f, 300.f, 20.f, {}, /*bClosedLoop=*/true);
	TArray<FVector> Vertices, Normals;
	TArray<int32> Indices;
	UProceduralMeshComponent* Mesh = SectionOf(Room, Vertices, Normals, Indices);
	TestEqual(TEXT("a room: two long faces, a top and a bottom for each of four walls, no cap between them"), Vertices.Num(), 4 * 4 * 4);
	TestTrue(TEXT("the outer corner is the mitre point, no notch"), HasVertexAt(Vertices, FVector(-10, -10, 0)) && HasVertexAt(Vertices, FVector(1010, 1010, 300)));
	TestTrue(TEXT("every face points the way its normals say"), FacesAgreeWithNormals(Vertices, Indices, Normals));
	if (Mesh && Mesh->GetMaterial(0) && Mesh->GetMaterial(0)->GetBaseMaterial())
	{
		TestTrue(TEXT("the walls are opaque: a translucent one is painted in mesh order and looks hollow from above"), Mesh->GetMaterial(0)->GetBaseMaterial()->BlendMode == BLEND_Opaque);
	}
	else
	{
		AddError(TEXT("the room walls have no material"));
	}

	// A free-standing wall of two pieces: square ends with a cap, one corner between them.
	AInteriorRoom* Wall = World->SpawnActor<AInteriorRoom>();
	Wall->BuildWalls({ FVector2D(0, 0), FVector2D(10, 0), FVector2D(10, 10) }, 100.f, FVector2D::ZeroVector, 0.f, 300.f, 20.f, {}, /*bClosedLoop=*/false);
	Vertices.Reset(); Normals.Reset(); Indices.Reset();
	SectionOf(Wall, Vertices, Normals, Indices);
	TestEqual(TEXT("a free-standing wall: two pieces of 4 faces, and a cap at each of its two ends"), Vertices.Num(), 2 * 4 * 4 + 2 * 4);
	TestTrue(TEXT("every face points the way its normals say"), FacesAgreeWithNormals(Vertices, Indices, Normals));

	World->DestroyWorld(false);
	return true;
}

// A door that starts at a corner leaves no wall there on that edge; the wall of the edge before must end square with a cap, not run into
// an open mitred end (a slit into the wall).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWallDoorAtCornerTest, "InNavi.Geometry.WallDoorAtCorner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWallDoorAtCornerTest::RunTest(const FString& Parameters)
{
	using namespace SmoothGeometryTests;
	const TArray<FVector2D> Poly = Square(1000.f);
	TArray<FWallCorner> Corners;
	ComputeWallCorners(Poly, 10.f, Corners);

	// Without a door the corner is mitred and there is no cap: 4 edges x 16 vertices.
	{
		FShadedMesh Mesh;
		AppendWallEdges(Mesh, FullHeightEdges(Poly, true, 300.f), Corners, 10.f, true);
		TestEqual(TEXT("no door: one continuous ring, no cap"), Mesh.Vertices.Num(), 4 * 16);
	}

	// A door of 90 at the start of the second edge: the doorway is open up to 240, the lintel above it.
	TArray<FWallEdge> Edges = FullHeightEdges(Poly, true, 300.f);
	Edges[1].Pieces.Reset();
	FWallPiece Wall; Wall.T0 = 90.f; Wall.T1 = 1000.f; Wall.Z0 = 0.f; Wall.Z1 = 300.f;
	FWallPiece Lintel; Lintel.T0 = 0.f; Lintel.T1 = 90.f; Lintel.Z0 = 240.f; Lintel.Z1 = 300.f;
	Edges[1].Pieces.Add(Wall);
	Edges[1].Pieces.Add(Lintel);

	FShadedMesh Mesh;
	AppendWallEdges(Mesh, Edges, Corners, 10.f, true);
	bool bCapAtCorner = false;
	for (int32 i = 0; i < Mesh.Vertices.Num(); ++i)
	{
		// The first edge's end cap faces along the edge (+X), at the corner x = 1000, and reaches the ground.
		bCapAtCorner |= Mesh.Normals[i].X > 0.99f && FMath::IsNearlyEqual(Mesh.Vertices[i].X, 1000.f, 0.05f) && FMath::IsNearlyZero(Mesh.Vertices[i].Z, 0.05f);
	}
	TestTrue(TEXT("the wall before a door at the corner ends with a cap"), bCapAtCorner);
	TestTrue(TEXT("it ends square, 10 either side of the edge's own line"), HasVertexAt(Mesh.Vertices, FVector(1000, 10, 0)) && HasVertexAt(Mesh.Vertices, FVector(1000, -10, 0)));
	TestTrue(TEXT("every face points the way its normals say"), FacesAgreeWithNormals(Mesh.Vertices, Mesh.Triangles, Mesh.Normals));
	return true;
}

// A wall that turns straight back on itself has no mitre: both sides end square and nothing is twisted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWallUTurnTest, "InNavi.Geometry.WallUTurn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWallUTurnTest::RunTest(const FString& Parameters)
{
	using namespace SmoothGeometryTests;
	const TArray<FVector2D> Poly = { FVector2D(0, 0), FVector2D(1000, 0) };
	TArray<FWallCorner> Corners;
	ComputeWallCorners(Poly, 10.f, Corners);
	TestFalse(TEXT("a U-turn cannot be joined"), Corners[0].bJoin || Corners[1].bJoin);

	FShadedMesh Mesh;
	AppendWallEdges(Mesh, FullHeightEdges(Poly, true, 300.f), Corners, 10.f, true);
	bool bWithin = true;
	for (const FVector& Vertex : Mesh.Vertices)
	{
		bWithin &= FMath::Abs(Vertex.Y) <= 10.05f && Vertex.X >= -0.05f && Vertex.X <= 1000.05f;
	}
	TestTrue(TEXT("both pieces stay within the wall's own thickness and length"), bWithin);
	TestTrue(TEXT("every face points the way its normals say"), FacesAgreeWithNormals(Mesh.Vertices, Mesh.Triangles, Mesh.Normals));
	return true;
}

// A free-standing wall is a chain: it does not wrap round, so a repeated first point cannot make its end mitre against the far end.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWallOpenChainTest, "InNavi.Geometry.WallOpenChain", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWallOpenChainTest::RunTest(const FString& Parameters)
{
	using namespace SmoothGeometryTests;
	const TArray<FVector2D> Chain = { FVector2D(0, 0), FVector2D(0, 0), FVector2D(1000, 0), FVector2D(1000, 1000) };
	TArray<FWallCorner> Corners;
	ComputeWallCorners(Chain, 10.f, Corners, 40.f, 1500.f, 2.f, /*bClosed=*/false);
	TestFalse(TEXT("the first vertex of a chain has nothing to join"), Corners[0].bJoin || Corners[1].bJoin);
	TestFalse(TEXT("nor has the last"), Corners[3].bJoin);
	TestTrue(TEXT("the corner between the two walls is joined"), Corners[2].bJoin);

	FShadedMesh Mesh;
	AppendWallEdges(Mesh, FullHeightEdges(Chain, false, 300.f), Corners, 10.f, false);
	// The first wall runs along x from 0 to 1000 with square ends: nothing may poke out at its start.
	bool bSquareStart = true;
	for (const FVector& Vertex : Mesh.Vertices)
	{
		bSquareStart &= Vertex.X >= -0.05f;
	}
	TestTrue(TEXT("the start of the chain is square, not mitred against the far end"), bSquareStart);
	TestEqual(TEXT("two walls: 16 each, a cap at each of the two free ends"), Mesh.Vertices.Num(), 2 * 16 + 2 * 4);
	TestTrue(TEXT("every face points the way its normals say"), FacesAgreeWithNormals(Mesh.Vertices, Mesh.Triangles, Mesh.Normals));
	return true;
}

#endif
