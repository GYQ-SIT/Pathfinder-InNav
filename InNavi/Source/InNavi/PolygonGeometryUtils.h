// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

// Shared 2D polygon math, originally written for APolyBuilding's own roof/ground cap
// generation (BuildCapSection) and extracted here so AInteriorRoom's floor slabs can
// use the exact same ear-clipping triangulator instead of a second copy - room
// footprints can be concave too, so a naive fan triangulation isn't safe for either.

// Signed area of a simple polygon (shoelace formula) - positive for CCW winding,
// negative for CW.
float PolygonSignedArea(const TArray<FVector2D>& Poly);

// Whether point P lies inside (or on the edge of) triangle ABC, via barycentric sign
// test.
bool PointInTriangle(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C);

// Ear-clipping triangulation - handles convex AND concave simple (non-self-
// intersecting) polygons. Appends triangle indices (into Poly, 3 per triangle) to
// OutTriangles; winds Poly to CCW internally first if it wasn't already, so the
// resulting triangles are consistently wound regardless of the input's own winding.
void TriangulatePolygon(const TArray<FVector2D>& Poly, TArray<int32>& OutTriangles);

// Appends one box (8 logical corners, 6 hard-edged faces, each wound outward) to the mesh arrays -
// originally written for AInteriorRoom's per-edge wall segments and extracted here so
// APolyBuilding's own per-floor exterior wall rings (see APolyBuilding::BuildWallSection) can reuse
// it instead of a flat, single-sided quad, which goes invisible (backface-culled) whenever the 3D
// preview camera ends up looking at its back side.
// Corner naming: P<x><y><z> where x=start/end along the wall edge, y=inner/outer across the wall
// thickness, z=bottom/top. Each quad is listed in outward-facing CCW order, verified against the
// right-handed (Dir, Perp, Up) frame callers construct each box in (Perp = (-Dir.Y, Dir.X)).
void AppendWallBox(TArray<FVector>& Vertices, TArray<int32>& Triangles,
	const FVector& P000, const FVector& P100, const FVector& P010, const FVector& P110,
	const FVector& P001, const FVector& P101, const FVector& P011, const FVector& P111);

// ---- Smooth walls and flat roads -------------------------------------------------------------------------------------------------------
//
// AppendWallBox draws every wall edge as a box of its own, and CalculateTangentsForMesh then averages the normals of vertices that lie
// in the same place. Two things follow: neighbouring boxes overlap at a corner and leave a notch on the other side of it, and each box
// is shaded a little differently from the next, so a curved front reads as a row of separate pieces. The helpers below draw the same
// wall as one continuous ribbon: the faces of two edges meet at one mitred corner point, no cap is drawn where the wall carries on,
// and the normals are chosen here (flat on a straight stretch, blended round a gentle bend) instead of being averaged afterwards.

// A mesh whose normals are set by whoever builds it. Pass Normals to CreateMeshSection as they are; do not recompute them.
struct FShadedMesh
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;

	int32 AddVertex(const FVector& Position, const FVector& Normal)
	{
		Vertices.Add(Position);
		Normals.Add(Normal);
		UV0.Add(FVector2D::ZeroVector);
		return Vertices.Num() - 1;
	}
	void AddTriangle(int32 A, int32 B, int32 C) { Triangles.Add(A); Triangles.Add(B); Triangles.Add(C); }
	// A quad given in the order that faces the viewer (counter-clockwise seen from the side it faces), as AppendWallBox does.
	void AddQuad(int32 A, int32 B, int32 C, int32 D) { AddTriangle(A, B, C); AddTriangle(A, C, D); }
};

// How the wall of a closed polygon turns at one of its vertices. "Plus side" is the side Perp = (-Dir.Y, Dir.X) points to.
struct FWallCorner
{
	// From the vertex to the corner of the plus-side face; the minus-side corner is the vertex minus this. Mitred, so it is longer than
	// half the thickness on a sharp corner (capped), and the two walls that meet here share these two points.
	FVector2D Offset = FVector2D::ZeroVector;
	// The plus-side normal the face of the edge before the vertex gets at the vertex, and the one the edge after it gets. Equal on a
	// gentle bend (smooth), different on a sharp corner (a crisp edge). The minus side uses the negated values.
	FVector2D PrevNormal = FVector2D::ZeroVector;
	FVector2D NextNormal = FVector2D::ZeroVector;
};

// One corner per vertex of Poly (its winding does not matter). A bend is smooth when it turns less than SmoothBelowDegrees and both
// edges at it are no longer than SmoothMaxEdge (world units): a curved front is drawn as many short edges, a long straight wall must
// not get a gradient across it. MaxMiter caps Offset at that many times the half thickness.
void ComputeWallCorners(const TArray<FVector2D>& Poly, float HalfThickness, TArray<FWallCorner>& OutCorners,
	float SmoothBelowDegrees = 40.f, float SmoothMaxEdge = 1500.f, float MaxMiter = 2.f);

// One end of a wall span: where it stands, the offset to its plus-side corner, the plus-side normal there, and whether the end is
// closed with a cap (a door gap, a jamb) or runs on into the next span (a corner).
struct FWallEnd
{
	FVector2D Center = FVector2D::ZeroVector;
	FVector2D Offset = FVector2D::ZeroVector;
	FVector2D Normal = FVector2D::ZeroVector;
	bool bCap = true;
};

// A span that ends in the middle of an edge: a plain square end with a cap. Perp is the edge's own (-Dir.Y, Dir.X).
FWallEnd MakePlainWallEnd(const FVector2D& Center, const FVector2D& Perp, float HalfThickness);
// A span that ends at a polygon vertex and runs on into the next edge: the mitred corner, no cap. bStart is whether this is the
// start of the span (the edge begins at the vertex) or its end.
FWallEnd MakeCornerWallEnd(const FVector2D& Vertex, const FWallCorner& Corner, bool bStart);

// Appends the stretch of wall between Start and End from Z0 to Z1: two long faces, top and bottom, and a cap at each end that asks for one.
void AppendWallSpan(FShadedMesh& Mesh, const FWallEnd& Start, const FWallEnd& End, float Z0, float Z1);

// Appends the flat roof of a polygon at Z (faces up, normal straight up). Poly may wind either way.
void AppendFlatCap(FShadedMesh& Mesh, const TArray<FVector2D>& Poly, float Z);

// Rounds the bends of a polyline so a road made of straight pieces reads as a curve. Every bend that turns more than MinTurnDegrees
// is replaced by a short arc that starts at most MaxCut before the bend and ends at most MaxCut after it (never more than half of
// either piece, so neighbouring arcs do not meet). The first and last point stay where they are. A closed loop (first point equals the
// last) is smoothed all the way round.
void SmoothPolyline(const TArray<FVector2D>& Points, float MaxCut, TArray<FVector2D>& OutPoints, float MinTurnDegrees = 6.f);

// Appends a flat ribbon of the given width along Points at height Z, facing up (and, if bTwoSided, a second copy facing down so it
// is seen from below too). Bends are mitred; with bRoundEnds each open end gets a half disc, so roads that meet end to end close up
// without a wedge-shaped gap. A closed loop is drawn without a seam.
void AppendFlatRibbon(FShadedMesh& Mesh, const TArray<FVector2D>& Points, float HalfWidth, float Z, bool bTwoSided, bool bRoundEnds);
