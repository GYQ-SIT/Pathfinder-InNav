// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteriorFloorPlanWidget.generated.h"

class AInteriorGenerator;
class ABuildingGenerator;
class ANavPlayerController;

// A true 2D top-down floor plan view of the currently loaded building's active floor - draws the
// exterior boundary and each room/wall's outline directly as 2D lines (no 3D camera or mesh
// involved at all), matching the PM's "2D mode" mockup rather than the tilted-3D-camera approach
// used elsewhere in this project so far. Place this as a child widget anywhere in
// WBP_InteriorScreen; it finds its own BuildingGenerator/InteriorGenerator references in
// NativeConstruct and redraws every frame from their current live state - no manual refresh wiring
// needed, and no separate "3D mode" logic is touched by this widget.
UCLASS()
class INNAVI_API UInteriorFloorPlanWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Converts a click's local widget-space position (e.g. from this widget's own
	// OnMouseButtonDown event - MyGeometry's "Absolute To Local" of the mouse event's screen
	// position) into a footprint point, in the same pre-scale/pre-offset units
	// CreateRoomFromFootprint/CreateWallFromPoints expect. WidgetSize should be MyGeometry's local
	// size for that same event, so the inverse mapping matches exactly what NativePaint drew.
	// Returns (0,0) if nothing is loaded.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	FVector2D LocalPositionToFootprintPoint(FVector2D LocalPosition, FVector2D WidgetSize) const;

protected:
	virtual void NativeConstruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// Left-click anywhere on the canvas feeds the click straight into
	// ANavPlayerController::AddRoomPointAtFootprintPosition (a no-op unless draw room/wall mode is
	// active) - no Blueprint wiring needed for this, it's handled entirely here.
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	// The view is auto-framed to fit the loaded building's footprint into WidgetSize (with a
	// margin), centered - shared by NativePaint (to draw) and LocalPositionToFootprintPoint (to
	// invert a click), so a click always lands exactly where it visually appears.
	// OutOrigin is the world-space point that maps to the widget's center;
	// OutPixelsPerUnit is world units to screen pixels.
	void ComputeViewTransform(FVector2D WidgetSize, FVector2D& OutOrigin, float& OutPixelsPerUnit) const;

	UPROPERTY()
	AInteriorGenerator* InteriorGen;

	UPROPERTY()
	ABuildingGenerator* Generator;

	UPROPERTY()
	ANavPlayerController* PlayerController;
};
