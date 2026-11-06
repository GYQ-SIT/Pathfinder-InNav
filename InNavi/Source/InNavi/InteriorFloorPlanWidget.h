// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "InteriorFloorPlanWidget.generated.h"

class AInteriorGenerator;
class ABuildingGenerator;
class ANavPlayerController;
class AInteriorRoom;
class UTexture2D;

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
	void ResetView() { PanOffset = FVector2D::ZeroVector; ZoomMultiplier = 1.f; }
	void FinishEditDrag();

protected:
	virtual void NativeConstruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// Left-click anywhere on the canvas feeds the click straight into
	// ANavPlayerController::AddRoomPointAtFootprintPosition (a no-op unless draw room/wall mode is
	// active) - no Blueprint wiring needed for this, it's handled entirely here. Right-click starts
	// panning the view (see PanOffset below).
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	// Mouse wheel zooms in/out (see ZoomMultiplier below), centered on the current view rather than
	// the cursor position.
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	friend struct FEditorInteractionTestAccess;
	friend struct FEditorFeedbackTestAccess;
	TArray<FVector2D> GetResizeHandles(FVector2D WidgetSize) const;
	TArray<FVector2D> GetPrefabPreview() const;
	bool IsPrefabDragValid() const;
	void ResizeAtLocalPosition(FVector2D LocalPosition, FVector2D WidgetSize);
	bool bIsResizingRoom = false;
	bool bResizeChanged = false;
	FVector2D ResizeDirection = FVector2D::ZeroVector;
	FVector2D ResizeStartPosition = FVector2D::ZeroVector;
	FVector2D ResizePreviewPosition = FVector2D::ZeroVector;
	FVector2D ResizePreviewSize = FVector2D::ZeroVector;
	FVector2D ResizeStartSize = FVector2D::ZeroVector;
	bool bIsDraggingPrefab = false;
	FVector2D PrefabStartLocal = FVector2D::ZeroVector;
	FVector2D PrefabStart = FVector2D::ZeroVector;
	FVector2D PrefabEnd = FVector2D::ZeroVector;
	bool bPrefabWasDragged = false;
	int32 PrefabShape = 0;
	int32 DragFloor = 0;
	FString DragBuildingId;

	// The view is auto-framed to fit the loaded building's footprint into WidgetSize (with a
	// margin), centered - shared by NativePaint (to draw) and LocalPositionToFootprintPoint (to
	// invert a click), so a click always lands exactly where it visually appears.
	// OutOrigin is the world-space point that maps to the widget's center;
	// OutPixelsPerUnit is world units to screen pixels.
	void ComputeViewTransform(FVector2D WidgetSize, FVector2D& OutOrigin, float& OutPixelsPerUnit) const;

	// Select/add-door modes: finds whichever room/wall on the active floor has an edge within a few
	// pixels of LocalPosition, or nullptr if none does. Never matches the exterior building outline
	// itself - that's a separate actor (APolyBuilding), not one of InteriorGenerator's rooms/walls.
	// OutEdgeIndex/OutOffsetWorldUnits are optional (pass nullptr if not needed, as select mode
	// does) - when given, filled in with which edge matched and how far along it (world units,
	// matching FInteriorOpening::Offset) the click falls, clamped so a fixed-width door centered
	// there fits without poking past either end - see AddDoorToWall.
	AInteriorRoom* FindRoomNearLocalPosition(FVector2D LocalPosition, FVector2D WidgetSize, int32* OutEdgeIndex = nullptr, float* OutOffsetWorldUnits = nullptr) const;

	// Select mode fallback: finds whichever room/wall on the active floor's footprint CONTAINS
	// LocalPosition (point-in-polygon, not just near an edge like FindRoomNearLocalPosition), so
	// clicking anywhere inside a room's body - not just precisely on its outline - selects it and
	// can start a drag (see NativeOnMouseButtonDown). Only tried when FindRoomNearLocalPosition
	// doesn't already find an edge close enough. Never matches an open wall (no interior to speak
	// of - see AInteriorRoom::IsClosedLoop).
	AInteriorRoom* FindRoomContainingLocalPosition(FVector2D LocalPosition, FVector2D WidgetSize) const;

	// Select mode: finds whichever door marker (see NativePaint) on the active floor is within a
	// few pixels of LocalPosition, so it can be selected independently of the room/wall it belongs
	// to. Returns false (leaving OutRoomId/OutDoorId/OutEdgeIndex untouched) if none is close enough.
	// Checked before FindRoomNearLocalPosition on a select-mode click, so clicking directly on a
	// door marker selects the door rather than the wall it's cut into. OutEdgeIndex is only written
	// when non-null - select mode doesn't need it, starting a drag does (see NativeOnMouseButtonDown).
	bool FindDoorNearLocalPosition(FVector2D LocalPosition, FVector2D WidgetSize, FString& OutRoomId, FString& OutDoorId, int32* OutEdgeIndex = nullptr) const;

	// Select mode: finds whichever connector marker (elevator/stairs/escalator - see NativePaint) on
	// the active floor is within a few pixels of LocalPosition. Checked alongside
	// FindDoorNearLocalPosition (before FindRoomNearLocalPosition), since a connector is often
	// placed inside a room and should win over "select the room" the same way a door marker does.
	// Returns false (leaving OutGroupId untouched) if none is close enough.
	bool FindConnectorNearLocalPosition(FVector2D LocalPosition, FVector2D WidgetSize, FString& OutGroupId) const;

	// While dragging an already-selected door (see NativeOnMouseButtonDown/NativeOnMouseMove),
	// computes the Offset (world units, clamped to fit within the edge - same convention as
	// FindRoomNearLocalPosition's OutOffsetWorldUnits) the door would have if centered under
	// LocalPosition, projected onto RoomId's EdgeIndex edge - the same edge the door was already on
	// (a drag doesn't re-search which edge, just slides along the known one). Returns false (leaving
	// OutOffsetWorldUnits untouched) if the room or edge no longer exists.
	bool ComputeDoorDragOffset(const FString& RoomId, int32 EdgeIndex, FVector2D LocalPosition, FVector2D WidgetSize, float& OutOffsetWorldUnits) const;

	UPROPERTY()
	AInteriorGenerator* InteriorGen;

	UPROPERTY()
	ABuildingGenerator* Generator;

	UPROPERTY()
	ANavPlayerController* PlayerController;

	// Manual pan offset (world units), added on top of the auto-framed center - right-click-drag
	// adjusts this; ComputeViewTransform folds it into the origin it returns.
	FVector2D PanOffset = FVector2D::ZeroVector;

	bool bIsPanning = false;
	FVector2D PanStartLocalPosition = FVector2D::ZeroVector;
	FVector2D PanStartOffset = FVector2D::ZeroVector;

	// Set when a select-mode left-click lands on a door marker - a plain click (mouse up without
	// moving) has no visible effect beyond the SelectDoor call already made on mouse down; if the
	// user drags instead, NativeOnMouseMove slides the door along DraggingDoorEdgeIndex via MoveDoor.
	bool bIsDraggingDoor = false;
	TWeakObjectPtr<AInteriorRoom> DragTarget;
	FString DraggingDoorRoomId;
	FString DraggingDoorId;
	int32 DraggingDoorEdgeIndex = INDEX_NONE;

	// Set when a select-mode left-click lands on a room/wall's edge or interior (and no door won
	// out first) - slides the whole room via MoveRoomTo. The footprint/click position at the moment
	// the drag started are snapshotted so each move recomputes an absolute translated footprint
	// (current click position minus DragStartFootprintPosition, added to every point in
	// DragStartRoomFootprint) rather than accumulating per-frame deltas, so dragging doesn't drift.
	bool bIsDraggingRoom = false;
	FString DraggingRoomId;
	TArray<FVector2D> DragStartRoomFootprint;
	FVector2D DragStartFootprintPosition = FVector2D::ZeroVector;

	// Multiplies the auto-fit pixels-per-unit from ComputeViewTransform - 1 = the auto-fit size,
	// >1 = zoomed in, <1 = zoomed out. Adjusted by the mouse wheel.
	float ZoomMultiplier = 1.f;

	// Connector markers (elevator/stairs/escalator - see NativePaint) are drawn as these icons
	// instead of a plain colored box. Loaded once in NativeConstruct from Padre's toolbar icon
	// assets (/Game/ImageAssets/Icon_Add...) - same icon used both to add a connector and to mark
	// one already placed. Textures are UPROPERTY to keep them from being garbage-collected; the
	// brushes just wrap them and don't need reflection.
	UPROPERTY()
	TObjectPtr<UTexture2D> ElevatorIconTexture = nullptr;
	UPROPERTY()
	TObjectPtr<UTexture2D> StairsIconTexture = nullptr;
	UPROPERTY()
	TObjectPtr<UTexture2D> EscalatorIconTexture = nullptr;
	FSlateBrush ElevatorIconBrush;
	FSlateBrush StairsIconBrush;
	FSlateBrush EscalatorIconBrush;
};
