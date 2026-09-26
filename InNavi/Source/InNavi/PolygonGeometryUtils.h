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
