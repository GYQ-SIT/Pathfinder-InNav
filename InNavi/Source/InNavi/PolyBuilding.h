// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PolyBuilding.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class INNAVI_API APolyBuilding : public AActor
{
	GENERATED_BODY()

public:
	APolyBuilding();

	// Build the 3D mesh by extruding a 2D polygon footprint into FloorCount floors of FloorHeight
	// each. Offset shifts the whole building in the XY plane (in the same units as the footprint,
	// before scale). Footprint/Scale/Offset/FloorCount/FloorHeight are cached so the mesh can be
	// regenerated later (see SetFloorCount) without needing them passed in again.
	void BuildFromFootprint(const TArray<FVector2D>& Footprint, float Scale, FVector2D Offset, int32 FloorCount, float FloorHeight);

	// Changes floor count and regenerates the exterior mesh at the new height. This is how the
	// interior editor's "Add floor" (or remove floor) stays in sync with the exterior shell -
	// interior floor count is the source of truth, this just re-extrudes to match it.
	// NewFloorCount must be >= 1; smaller values are clamped.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetFloorCount(int32 NewFloorCount);

	UFUNCTION(BlueprintPure, Category = "Building")
	int32 GetFloorCount() const { return FloorCount; }

	UFUNCTION(BlueprintPure, Category = "Building")
	float GetFloorHeight() const { return FloorHeight; }

	// The footprint as originally given to BuildFromFootprint (pre-scale, pre-offset units) - what
	// SaveBuildingsToFile writes back out, so a saved-then-reloaded building round-trips exactly.
	UFUNCTION(BlueprintPure, Category = "Building")
	const TArray<FVector2D>& GetFootprint() const { return CachedFootprint; }

	// Highlight / unhighlight this building
	void SetHighlighted(bool bHighlighted);

	// Shows the given floor's plan by hiding every ceiling from that floor up through the roof
	// (both visually and its collision, so clicks reach the floor below too) - every slab AT or BELOW
	// the given floor stay visible/solid, so you don't see (or click) through into whatever's
	// underneath. Also hides every OTHER floor's own exterior wall ring, showing only Level's - the
	// 2D floor-plan camera never needed this (near-top-down, and hidden behind the 2D overlay anyway),
	// but the 3D preview camera sits outside the footprint at an oblique angle, where a full-height
	// wall would otherwise block the view into whichever floor is being looked at. The mesh keeps each
	// floor boundary as its own section (see RebuildMesh) specifically so this doesn't have to hide the
	// whole building.
	// Level must be between 1 and GetFloorCount(); pass 0 to show everything (exterior view).
	void SetVisibleInteriorLevel(int32 Level);

	// Data
	UPROPERTY()
	FString BuildingId;

	UPROPERTY()
	FString BuildingName;

	UPROPERTY()
	FVector2D BuildingOffset;

	UPROPERTY()
	float BuildingHeight;

private:
	// Regenerates the mesh sections from the cached footprint/scale/offset and current
	// FloorCount/FloorHeight. Called by both BuildFromFootprint and SetFloorCount.
	void RebuildMesh();

	// Builds (or rebuilds) one flat cap section (the triangulated footprint, facing up) at height Z,
	// at the given mesh section index - used for the roof and every inter-floor slab, which are
	// otherwise identical except for their Z and which section they occupy. bCreateCollision is
	// re-specified every time because UProceduralMeshComponent has no way to toggle an existing
	// section's collision after the fact - the only way to turn it off is to recreate the section
	// without it (see ApplySlabVisibility).
	void BuildCapSection(const TArray<FVector2D>& Poly2D, const TArray<int32>& CapTris, float Z, int32 SectionIndex, bool bCreateCollision);

	// Builds (or rebuilds) one floor's own exterior wall ring - a box per footprint edge, ZBottom to
	// ZTop - at the given section index. Same bCreateCollision-must-be-respecified reasoning as
	// BuildCapSection (recreating is the only way to toggle a section's collision).
	void BuildWallSection(const TArray<FVector2D>& Poly2D, float ZBottom, float ZTop, int32 SectionIndex, bool bCreateCollision);

	// Rebuilds every roof/slab section AND every floor's wall ring (visibility AND collision together)
	// based on VisibleInteriorLevel - called after RebuildMesh (so a floor add/remove reapplies it) and
	// by SetVisibleInteriorLevel. Uses CachedPoly2D/CachedCapTris rather than needing RebuildMesh to
	// re-triangulate the footprint just to hide one floor.
	void ApplySlabVisibility();

	// Wall sections live after the cap sections (0 = ground cap, 1..FloorCount = roof/inter-floor
	// caps - see RebuildMesh) so adding/removing floors never renumbers either group relative to
	// itself.
	int32 GetWallSectionIndex(int32 Level) const { return FloorCount + Level; }

	TArray<FVector2D> CachedFootprint;
	float CachedScale = 1.f;
	int32 FloorCount = 1;
	float FloorHeight = 300.f;

	// The current build's transformed footprint and its ear-clipped triangulation - cached so
	// ApplySlabVisibility can rebuild roof/slab sections without redoing RebuildMesh's work.
	TArray<FVector2D> CachedPoly2D;
	TArray<int32> CachedCapTris;

	// See SetVisibleInteriorLevel. 0 = nothing hidden (normal exterior view).
	int32 VisibleInteriorLevel = 0;

	UPROPERTY()
	UProceduralMeshComponent* MeshComp;

	UPROPERTY()
	UMaterialInstanceDynamic* DynMaterial;
};