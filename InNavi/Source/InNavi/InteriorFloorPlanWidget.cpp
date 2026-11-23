// Fill out your copyright notice in the Description page of Project Settings.


#include "InteriorFloorPlanWidget.h"
#include "EditorIcons.h"
#include "InteriorDoorColors.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "InteriorTypes.h"
#include "BuildingGenerator.h"
#include "PolyBuilding.h"
#include "NavPlayerController.h"
#include "LayoutValidationSession.h"
#include "NavigationPanelWidget.h"
#include "NavigationRouteVisualization.h"
#include "InteriorDrawing.h"
#include "PolygonGeometryUtils.h"
#include "RoomTypes.h"
#include "EngineUtils.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"

static const FVector2D FloorPlanResizeDirections[] = {
	{-1,-1}, {0,-1}, {1,-1}, {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}
};

static TArray<FVector2D> FloorPlanHandleDirections(FVector2D Size)
{
	if (FMath::IsNearlyZero(Size.X)) return {{0,-1}, {0,1}};
	if (FMath::IsNearlyZero(Size.Y)) return {{-1,0}, {1,0}};
	return TArray<FVector2D>(FloorPlanResizeDirections, UE_ARRAY_COUNT(FloorPlanResizeDirections));
}

TArray<FVector2D> UInteriorFloorPlanWidget::GetResizeHandles(FVector2D WidgetSize) const
{
	TArray<FVector2D> Handles;
	if (!PlayerController || !InteriorGen || !Generator || !PlayerController->IsInSelectMode() || PlayerController->IsInteriorInspectionEnabled()) return Handles;
	// With a gizmo on, it is the way to move, turn and resize: the plain handles stay out of its way.
	if (PlayerController->GetTransformGizmo() != ETransformGizmo::None) return Handles;
	const APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	FVector2D Position, Size, Origin;
	float PixelsPerUnit = 0;
	if (!Building || !PlayerController->GetSelectedRoomBounds(Position, Size) || Size.GetMax() <= 0) return Handles;
	if (bIsResizingRoom) { Position = ResizePreviewPosition; Size = ResizePreviewSize; }
	ComputeViewTransform(WidgetSize, Origin, PixelsPerUnit);
	for (const FVector2D& Direction : FloorPlanHandleDirections(Size))
	{
		const FVector2D Point = Position + Size * 0.5 + FVector2D(Direction.X * Size.X, Direction.Y * Size.Y) * 0.5;
		Handles.Add(ViewWorldToLocal((Point + Building->BuildingOffset) * Generator->GetScale(), Origin, PixelsPerUnit, WidgetSize));
	}
	return Handles;
}

TArray<FVector2D> UInteriorFloorPlanWidget::GetVertexHandles(FVector2D WidgetSize) const
{
	TArray<FVector2D> Handles;
	if (!PlayerController || !InteriorGen || !Generator || !PlayerController->IsInSelectMode() || PlayerController->IsInteriorInspectionEnabled()) return Handles;
	if (PlayerController->GetTransformGizmo() != ETransformGizmo::None) return Handles;
	const APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building) return Handles;
	const TArray<FVector2D> Vertices = PlayerController->GetSelectedRoomVertices();
	if (Vertices.IsEmpty()) return Handles;
	FVector2D Origin;
	float PixelsPerUnit = 0;
	ComputeViewTransform(WidgetSize, Origin, PixelsPerUnit);
	for (const FVector2D& Vertex : Vertices)
	{
		Handles.Add(ViewWorldToLocal((Vertex + Building->BuildingOffset) * Generator->GetScale(), Origin, PixelsPerUnit, WidgetSize));
	}
	return Handles;
}

int32 UInteriorFloorPlanWidget::FindVertexHandleAt(FVector2D LocalPosition, FVector2D WidgetSize, bool bCtrlHeld) const
{
	const TArray<FVector2D> Vertices = GetVertexHandles(WidgetSize);
	const TArray<FVector2D> Resize = GetResizeHandles(WidgetSize);
	int32 Best = INDEX_NONE;
	double BestDistance = 81.0;
	for (int32 Index = 0; Index < Vertices.Num(); ++Index)
	{
		const double Distance = FVector2D::DistSquared(LocalPosition, Vertices[Index]);
		if (Distance > BestDistance) continue;
		// A corner under a resize handle is left to that handle unless Ctrl is held.
		const bool bUnderResizeHandle = Resize.ContainsByPredicate([&](const FVector2D& Handle) { return FVector2D::DistSquared(Vertices[Index], Handle) <= 81.0; });
		if (bUnderResizeHandle && !bCtrlHeld) continue;
		Best = Index;
		BestDistance = Distance;
	}
	return Best;
}

FVector2D UInteriorFloorPlanWidget::ShapeDragEnd(FVector2D RawEnd, bool bShiftHeld) const
{
	if (!bShiftHeld || !PlayerController) return RawEnd;
	return InteriorDrawing::ProportionalCorner(PrefabStart, RawEnd, PlayerController->GetPrefabShiftAspect());
}

bool UInteriorFloorPlanWidget::IsPrefabDragValid() const
{
	return bIsDraggingPrefab && PlayerController && InteriorGen && PlayerController->IsInPrefabRoomMode() &&
		PlayerController->GetActivePrefabShape() == PrefabShape && InteriorGen->GetActiveFloor() == DragFloor &&
		InteriorGen->GetLoadedBuildingId() == DragBuildingId;
}

TArray<FVector2D> UInteriorFloorPlanWidget::GetPrefabPreview() const
{
	if (!IsPrefabDragValid()) return {};
	if (!bPrefabWasDragged) return PlayerController->BuildPrefabRoomFootprint(PrefabStart);
	const FBox2D Bounds(TArray<FVector2D>{PrefabStart, PrefabEnd});
	return PlayerController->BuildPrefabRoomFootprint(Bounds.GetCenter(), Bounds.GetSize());
}

void UInteriorFloorPlanWidget::ResizeAtLocalPosition(FVector2D LocalPosition, FVector2D WidgetSize)
{
	if (!DragTarget.IsValid() || !PlayerController || !InteriorGen || !PlayerController->IsInSelectMode() ||
		PlayerController->GetSelectedRoomId() != DraggingRoomId || InteriorGen->GetActiveFloor() != DragFloor ||
		InteriorGen->GetLoadedBuildingId() != DragBuildingId) return;
	const FVector2D Point = LocalPositionToFootprintPoint(LocalPosition, WidgetSize);
	FVector2D Min = ResizeStartPosition, Max = ResizeStartPosition + ResizeStartSize;
	const float Minimum = InteriorEditing::MinimumDimensionCm / PlayerController->GetBuildingScale();
	const bool bRoom = DragTarget->IsClosedLoop();
	const double MinX = bRoom || FMath::IsNearlyZero(ResizeStartSize.Y) || ResizeStartSize.Y < Minimum ? Minimum : UE_KINDA_SMALL_NUMBER;
	const double MinY = bRoom || FMath::IsNearlyZero(ResizeStartSize.X) || ResizeStartSize.X < Minimum ? Minimum : UE_KINDA_SMALL_NUMBER;
	if (ResizeDirection.X > 0) Max.X = FMath::Max(Point.X, Min.X + MinX);
	if (ResizeDirection.X < 0) Min.X = FMath::Min(Point.X, Max.X - MinX);
	if (ResizeDirection.Y > 0) Max.Y = FMath::Max(Point.Y, Min.Y + MinY);
	if (ResizeDirection.Y < 0) Min.Y = FMath::Min(Point.Y, Max.Y - MinY);
	if (bResizeUniform)
	{
		// The corner handle of the scale gizmo: the far corner follows the pointer along the room's own proportions.
		const FVector2D Corner = InteriorDrawing::ProportionalCorner(ResizeStartPosition, Point, ResizeStartAspect);
		Max.X = FMath::Max(Corner.X, Min.X + MinX);
		Max.Y = FMath::Max(Corner.Y, Min.Y + MinY);
	}

	// Stop at the building's wall: the preview never goes beyond what the release would accept (PAT-190). If the pointer is past the wall, each axis is
	// pulled back to the farthest size that still fits, one axis at a time, so a corner handle slides along the wall instead of freezing. A room that
	// already sticks out of the outline (an old project) is left alone.
	const FVector2D StartMin = ResizeStartPosition, StartMax = ResizeStartPosition + ResizeStartSize;
	auto FitsBuilding = [this](FVector2D A, FVector2D B) { return InteriorGen->DoesResizeStayInsideBuilding(DraggingRoomId, A, B - A); };
	if (!FitsBuilding(Min, Max) && FitsBuilding(StartMin, StartMax))
	{
		FVector2D ResolvedMin = StartMin, ResolvedMax = StartMax;
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			FVector2D TryMin = ResolvedMin, TryMax = ResolvedMax;
			TryMin[Axis] = Min[Axis];
			TryMax[Axis] = Max[Axis];
			if (FitsBuilding(TryMin, TryMax))
			{
				ResolvedMin = TryMin;
				ResolvedMax = TryMax;
				continue;
			}
			double Good = 0.0, Bad = 1.0;
			for (int32 Step = 0; Step < 20; ++Step)
			{
				const double Mid = (Good + Bad) * 0.5;
				FVector2D MidMin = ResolvedMin, MidMax = ResolvedMax;
				MidMin[Axis] = FMath::Lerp(ResolvedMin[Axis], Min[Axis], Mid);
				MidMax[Axis] = FMath::Lerp(ResolvedMax[Axis], Max[Axis], Mid);
				(FitsBuilding(MidMin, MidMax) ? Good : Bad) = Mid;
			}
			ResolvedMin[Axis] = FMath::Lerp(ResolvedMin[Axis], Min[Axis], Good);
			ResolvedMax[Axis] = FMath::Lerp(ResolvedMax[Axis], Max[Axis], Good);
		}
		Min = ResolvedMin;
		Max = ResolvedMax;
	}
	ResizePreviewPosition = Min;
	ResizePreviewSize = Max - Min;
	bResizeChanged = !ResizePreviewPosition.Equals(ResizeStartPosition, 1e-6) || !ResizePreviewSize.Equals(ResizeStartSize, 1e-6);
}

void UInteriorFloorPlanWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UWorld* World = GetWorld())
	{
		// The first generator of each kind. (Not a loop that stops after its first round: the Android compiler rejects that as a loop that runs at most once.)
		if (TActorIterator<AInteriorGenerator> InteriorIt(World); InteriorIt)
		{
			InteriorGen = *InteriorIt;
		}
		if (TActorIterator<ABuildingGenerator> GeneratorIt(World); GeneratorIt)
		{
			Generator = *GeneratorIt;
		}
		PlayerController = Cast<ANavPlayerController>(World->GetFirstPlayerController());
	}

	auto LoadConnectorIcon = [](const TCHAR* AssetPath, TObjectPtr<UTexture2D>& OutTexture, FSlateBrush& OutBrush)
	{
		OutTexture = LoadObject<UTexture2D>(nullptr, AssetPath);
		if (OutTexture)
		{
			OutBrush.SetResourceObject(OutTexture);
			OutBrush.ImageSize = FVector2D(OutTexture->GetSizeX(), OutTexture->GetSizeY());
			OutBrush.DrawAs = ESlateBrushDrawType::Image;
		}
	};
	LoadConnectorIcon(TEXT("/Game/ImageAssets/Icon_ToolElevator.Icon_ToolElevator"), ElevatorIconTexture, ElevatorIconBrush);
	LoadConnectorIcon(TEXT("/Game/ImageAssets/Icon_ToolStairs.Icon_ToolStairs"), StairsIconTexture, StairsIconBrush);
	LoadConnectorIcon(TEXT("/Game/ImageAssets/Icon_ToolEscalator.Icon_ToolEscalator"), EscalatorIconTexture, EscalatorIconBrush);
	for (const FString& UtilityType : InteriorUtilityTypes::All())
	{
		if (UTexture2D* UtilityTexture = EditorIcons::Load(InteriorUtilityTypes::IconAsset(UtilityType)))
		{
			UtilityIconTextures.Add(UtilityType, UtilityTexture);
			FSlateBrush UtilityBrush;
			UtilityBrush.SetResourceObject(UtilityTexture);
			UtilityBrush.ImageSize = FVector2D(UtilityTexture->GetSizeX(), UtilityTexture->GetSizeY());
			UtilityBrush.DrawAs = ESlateBrushDrawType::Image;
			UtilityIconBrushes.Add(UtilityType, UtilityBrush);
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[Navigation] Floor-plan constructed: PlayerController=%s Generator=%s Interior=%s BuildingId=%s Floor=%d"),
		PlayerController ? TEXT("valid") : TEXT("NULL"),
		Generator ? TEXT("valid") : TEXT("NULL"),
		InteriorGen ? TEXT("valid") : TEXT("NULL"),
		InteriorGen ? *InteriorGen->GetLoadedBuildingId() : TEXT("<none>"),
		InteriorGen ? InteriorGen->GetActiveFloor() : INDEX_NONE);
}

void UInteriorFloorPlanWidget::SetReferenceCropMode(bool bEnabled)
{
	CropBuildingId = bEnabled && InteriorGen ? InteriorGen->GetLoadedBuildingId() : FString();
	CropLevel = bEnabled && InteriorGen ? InteriorGen->GetActiveFloor() : INDEX_NONE;
	InvalidateLayoutAndVolatility();
}

bool UInteriorFloorPlanWidget::IsReferenceCropMode() const
{
	return InteriorGen && !CropBuildingId.IsEmpty() && CropBuildingId == InteriorGen->GetLoadedBuildingId()
		&& CropLevel == InteriorGen->GetActiveFloor();
}

const FInteriorReferenceImage* UInteriorFloorPlanWidget::EditableReferenceImage() const
{
	if (!PlayerController || PlayerController->IsInteriorInspectionEnabled() || !InteriorGen) return nullptr;
	const FInteriorReferenceImage* Image = InteriorGen->GetReferenceImage(InteriorGen->GetActiveFloor());
	return Image && Image->bVisible && !Image->bLocked && ReferenceImageTexture && Image->Path == ReferenceImagePath
		? Image : nullptr;
}

FVector2D UInteriorFloorPlanWidget::ReferenceImageSize(const FInteriorReferenceImage& Image) const
{
	const double Aspect = static_cast<double>(ReferenceImageTexture->GetSizeY()) / ReferenceImageTexture->GetSizeX();
	return FVector2D(Image.Width, Image.Width * Aspect);
}

bool UInteriorFloorPlanWidget::IsReferenceDragValid(const FInteriorReferenceImage* Image) const
{
	return ReferenceDrag && Image && *Image == ReferenceDrag->Start && InteriorGen
		&& ReferenceDrag->BuildingId == InteriorGen->GetLoadedBuildingId() && ReferenceDrag->Level == InteriorGen->GetActiveFloor();
}

FReply UInteriorFloorPlanWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		bIsPanning = true;
		PanStartLocalPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		PanStartOffset = PanOffset;
		return FReply::Handled().CaptureMouse(TakeWidget());
	}

	if (PlayerController && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2D LocalPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		if (const FInteriorReferenceImage* Image = EditableReferenceImage())
		{
			const FVector2D Point = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
			const FVector2D Size = ReferenceImageSize(*Image);
			const FVector2D UV = (Point - Image->Center) / Size + FVector2D(0.5, 0.5);
			const bool bCrop = IsReferenceCropMode();
			const FBox2D Bounds = bCrop ? FBox2D(FVector2D::ZeroVector, FVector2D(1.0, 1.0)) : Image->Crop;
			if (Bounds.IsInsideOrOn(UV))
			{
				ReferenceDrag = FReferenceDrag{*Image, *Image, Point, FBox2D(ForceInit),
					InteriorGen->GetLoadedBuildingId(), InteriorGen->GetActiveFloor(), bCrop};
				return FReply::Handled().CaptureMouse(TakeWidget());
			}
			if (bCrop) return FReply::Handled();
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[Navigation] 2D left click: local=(%.2f, %.2f) size=(%.2f, %.2f) selecting=%s"),
			LocalPosition.X, LocalPosition.Y,
			InGeometry.GetLocalSize().X, InGeometry.GetLocalSize().Y,
			PlayerController->IsSelectingNavigationPoint() ? TEXT("true") : TEXT("false"));

		if (PlayerController->IsSelectingNavigationPoint())
		{
			// Make sure the interior data is loaded before selecting a point.
			if (!InteriorGen ||
				!Generator ||
				InteriorGen->GetLoadedBuildingId().IsEmpty())
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Navigation] 2D navigation click rejected before conversion: PlayerController=%s Generator=%s Interior=%s BuildingId=%s"),
					PlayerController ? TEXT("valid") : TEXT("NULL"),
					Generator ? TEXT("valid") : TEXT("NULL"),
					InteriorGen ? TEXT("valid") : TEXT("NULL"),
					InteriorGen ? *InteriorGen->GetLoadedBuildingId() : TEXT("<none>"));
				return FReply::Handled();
			}

			// A valid floor must be selected.
			const int32 ActiveFloor = InteriorGen->GetActiveFloor();
			if (ActiveFloor == 0)
			{
				UE_LOG(LogTemp, Error, TEXT("[Navigation] 2D navigation click rejected: active floor=%d"), ActiveFloor);
				return FReply::Handled();
			}

			// Convert the mouse position from the widget to the building footprint.
			const FVector2D FootprintPosition =
				LocalPositionToFootprintPoint(
					LocalPosition,
					InGeometry.GetLocalSize());
			UE_LOG(LogTemp, Warning,
				TEXT("[Navigation] 2D click converted to footprint=(%.2f, %.2f), floor=%d"),
				FootprintPosition.X, FootprintPosition.Y, ActiveFloor);

			// Save the selected navigation point.
			PlayerController->SelectNavigationPoint(
				FootprintPosition,
				ActiveFloor);

			return FReply::Handled();
		}

		if (PlayerController->IsInSelectMode())
		{
			// The transform gizmo, when there is one: grabbing a part of it starts the move, turn or resize it stands for.
			if (const EGizmoPart Part = HitGizmo(LocalPosition, InGeometry.GetLocalSize()); Part != EGizmoPart::None &&
				StartGizmoDrag(Part, LocalPosition, InGeometry.GetLocalSize(), InMouseEvent.IsShiftDown()))
			{
				return FReply::Handled().CaptureMouse(TakeWidget());
			}

			// A corner of the selected room: a drag moves that corner alone (a corner under a resize handle needs Ctrl).
			const int32 VertexIndex = FindVertexHandleAt(LocalPosition, InGeometry.GetLocalSize(), InMouseEvent.IsControlDown());
			if (VertexIndex != INDEX_NONE && InteriorGen)
			{
				DraggingRoomId = PlayerController->GetSelectedRoomId();
				DragTarget = InteriorGen->GetRoomById(DraggingRoomId);
				if (DragTarget.IsValid())
				{
					bIsDraggingVertex = true;
					bVertexMoved = false;
					DraggingVertexIndex = VertexIndex;
					DragFloor = InteriorGen->GetActiveFloor();
					DragBuildingId = InteriorGen->GetLoadedBuildingId();
					return FReply::Handled().CaptureMouse(TakeWidget());
				}
			}

			const TArray<FVector2D> Handles = GetResizeHandles(InGeometry.GetLocalSize());
			for (int32 Index = 0; Index < Handles.Num(); ++Index)
			{
				if (FVector2D::DistSquared(LocalPosition, Handles[Index]) > 81.0) continue;
				FVector2D Position;
				if (!PlayerController->GetSelectedRoomBounds(Position, ResizeStartSize)) break;
				ResizeStartPosition = ResizePreviewPosition = Position;
				ResizePreviewSize = ResizeStartSize;
				bIsResizingRoom = true;
				bResizeChanged = false;
				ResizeDirection = FloorPlanHandleDirections(ResizeStartSize)[Index];
				DraggingRoomId = PlayerController->GetSelectedRoomId();
				DragTarget = InteriorGen->GetRoomById(DraggingRoomId);
				DragStartRoomFootprint = DragTarget->GetFootprint();
				DragFloor = InteriorGen->GetActiveFloor();
				DragBuildingId = InteriorGen->GetLoadedBuildingId();
				return FReply::Handled().CaptureMouse(TakeWidget());
			}
			FString DoorRoomId;
			FString DoorId;
			int32 DoorEdgeIndex = INDEX_NONE;
			if (FindDoorNearLocalPosition(LocalPosition, InGeometry.GetLocalSize(), DoorRoomId, DoorId, &DoorEdgeIndex))
			{
				PlayerController->SelectDoor(DoorRoomId, DoorId);

				// A plain click (mouse up without moving) has no further effect; dragging slides
				// the door along DraggingDoorEdgeIndex - see NativeOnMouseMove.
				bIsDraggingDoor = true;
				DragTarget = InteriorGen ? InteriorGen->GetRoomById(DoorRoomId) : nullptr;
				DraggingDoorRoomId = DoorRoomId;
				DraggingDoorId = DoorId;
				DraggingDoorEdgeIndex = DoorEdgeIndex;
				return FReply::Handled().CaptureMouse(TakeWidget());
			}

			// A utility point (PAT-193): a click selects it, a drag moves it.
			FString UtilityId;
			if (FindUtilityNearLocalPosition(LocalPosition, InGeometry.GetLocalSize(), UtilityId))
			{
				PlayerController->SelectUtility(UtilityId);
				const FInteriorUtility* Utility = InteriorGen ? InteriorGen->FindUtility(UtilityId) : nullptr;
				bIsDraggingUtility = Utility != nullptr;
				bUtilityMoved = false;
				DragUtilityGrabOffset = Utility ? Utility->Position - LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize()) : FVector2D::ZeroVector;
				return FReply::Handled().CaptureMouse(TakeWidget());
			}

			FString ConnectorGroupId;
			if (FindConnectorNearLocalPosition(LocalPosition, InGeometry.GetLocalSize(), ConnectorGroupId))
			{
				PlayerController->SelectConnector(ConnectorGroupId);
				// A plain click only selects; dragging moves the connector on this floor alone (Shift: on every floor it reaches) - see
				// NativeOnMouseMove.
				bIsDraggingConnector = true;
				bConnectorMoved = false;
				DragStartConnectorPositions = PlayerController->GetSelectedConnectorPositions(InMouseEvent.IsShiftDown());
				DragStartFootprintPosition = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
				return FReply::Handled().CaptureMouse(TakeWidget());
			}

			// A door in the building's outer wall.
			{
				const FVector2D ClickPoint = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
				const float PixelReach = static_cast<float>((LocalPositionToFootprintPoint(LocalPosition + FVector2D(12.0, 0.0), InGeometry.GetLocalSize()) - ClickPoint).Size());
				if (PlayerController->SelectEntranceNear(ClickPoint, PixelReach))
				{
					return FReply::Handled();
				}
			}

			AInteriorRoom* Room = FindRoomNearLocalPosition(LocalPosition, InGeometry.GetLocalSize());
			if (!Room)
			{
				Room = FindRoomContainingLocalPosition(LocalPosition, InGeometry.GetLocalSize());
			}

			if (Room)
			{
				// Shift+click adds the room to the selection, or takes it out; no drag starts.
				if (InMouseEvent.IsShiftDown())
				{
					PlayerController->ToggleRoomInSelection(Room->RoomId);
					return FReply::Handled();
				}
				PlayerController->SelectRoom(Room->RoomId);

				// Same idea as the door drag above - a plain click just selects; dragging slides the
				// whole room via MoveRoomTo, computed fresh each move from this snapshot (see
				// NativeOnMouseMove) rather than accumulated deltas, so it doesn't drift.
				bIsDraggingRoom = true;
				DragTarget = Room;
				DraggingRoomId = Room->RoomId;
				DragStartRoomFootprint = Room->GetFootprint();
				DragStartFootprintPosition = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
				return FReply::Handled().CaptureMouse(TakeWidget());
			}

			// Empty plan: a click deselects, a drag draws a selection window (Shift adds to the selection) - see FinishSelectBox.
			bIsDraggingSelectBox = true;
			bSelectBoxAdd = InMouseEvent.IsShiftDown();
			SelectBoxStart = SelectBoxEnd = LocalPosition;
			return FReply::Handled().CaptureMouse(TakeWidget());
		}

		if (PlayerController->IsInAddDoorMode())
		{
			int32 EdgeIndex = INDEX_NONE;
			float OffsetWorldUnits = 0.f;
			if (AInteriorRoom* Room = FindRoomNearLocalPosition(LocalPosition, InGeometry.GetLocalSize(), &EdgeIndex, &OffsetWorldUnits))
			{
				PlayerController->AddDoorToWall(Room->RoomId, EdgeIndex, OffsetWorldUnits);
			}
			else
			{
				// Not on a room's wall: on the building's own outer wall, a main door or a link door.
				const FVector2D ClickPoint = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
				const float PixelReach = static_cast<float>((LocalPositionToFootprintPoint(LocalPosition + FVector2D(14.0, 0.0), InGeometry.GetLocalSize()) - ClickPoint).Size());
				PlayerController->AddEntranceAtFootprintPosition(ClickPoint, PixelReach);
			}
			return FReply::Handled();
		}

		if (PlayerController->IsInPrefabRoomMode())
		{
			if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty()) return FReply::Handled();
			bIsDraggingPrefab = true;
			bPrefabWasDragged = false;
			PrefabStartLocal = LocalPosition;
			PrefabStart = PrefabEnd = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
			PrefabShape = PlayerController->GetActivePrefabShape();
			DragFloor = InteriorGen->GetActiveFloor();
			DragBuildingId = InteriorGen->GetLoadedBuildingId();
			return FReply::Handled().CaptureMouse(TakeWidget());
		}

		if (PlayerController->IsInConnectorMode())
		{
			const FVector2D FootprintPosition = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
			PlayerController->PlaceConnectorAt(FootprintPosition);
			return FReply::Handled();
		}

		if (PlayerController->IsInUtilityMode())
		{
			PlayerController->PlaceUtilityAt(LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize()));
			return FReply::Handled();
		}

		// Shift: the new point goes straight along X or Y from the last one, and is not pulled off that line by snapping.
		const FVector2D RawPoint = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
		const FVector2D FootprintPoint = PlayerController->ConstrainDrawPoint(RawPoint, InMouseEvent.IsShiftDown());
		PlayerController->AddRoomPointAtFootprintPosition(FootprintPoint, /*bSkipSnapping=*/!FootprintPoint.Equals(RawPoint, 1e-9));
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UInteriorFloorPlanWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (ReferenceDrag && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const bool bValid = IsReferenceDragValid(EditableReferenceImage());
		FReferenceDrag Drag = MoveTemp(*ReferenceDrag);
		ReferenceDrag.Reset();
		if (bValid)
		{
			if (Drag.bCrop && Drag.CropPreview.GetSize().X >= InteriorReferenceImage::MinCropFraction
				&& Drag.CropPreview.GetSize().Y >= InteriorReferenceImage::MinCropFraction)
			{
				Drag.Preview.Crop = Drag.CropPreview;
				SetReferenceCropMode(false);
			}
			PlayerController->SetActiveReferenceImage(Drag.Preview);
		}
		InvalidateLayoutAndVolatility();
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDraggingPrefab)
	{
		if (IsPrefabDragValid())
		{
			const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
			PrefabEnd = ShapeDragEnd(LocalPositionToFootprintPoint(Local, InGeometry.GetLocalSize()), InMouseEvent.IsShiftDown());
			bPrefabWasDragged |= FVector2D::DistSquared(Local, PrefabStartLocal) > 36.0;
			if (bPrefabWasDragged)
			{
				const FBox2D Bounds(TArray<FVector2D>{PrefabStart, PrefabEnd});
				PlayerController->PlacePrefabRoomInBounds(Bounds.GetCenter(), Bounds.GetSize());
			}
			else PlayerController->PlacePrefabRoomAt(PrefabStart);
		}
		bIsDraggingPrefab = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsResizingRoom)
	{
		ResizeAtLocalPosition(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()), InGeometry.GetLocalSize());
		FinishEditDrag();
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton && bIsPanning)
	{
		bIsPanning = false;
		// A right-click that did not pan (the pointer stayed put) on a room opens its menu.
		const FVector2D UpLocal = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		if (PlayerController && PlayerController->IsInSelectMode() && FVector2D::DistSquared(UpLocal, PanStartLocalPosition) < 25.0)
		{
			AInteriorRoom* Room = FindRoomNearLocalPosition(UpLocal, InGeometry.GetLocalSize());
			if (!Room) Room = FindRoomContainingLocalPosition(UpLocal, InGeometry.GetLocalSize());
			if (Room) PlayerController->OpenRoomContextMenu(Room->RoomId);
		}
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && (bIsRotatingGizmo || bIsScalingConnector))
	{
		FinishEditDrag();
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDraggingSelectBox)
	{
		SelectBoxEnd = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		FinishSelectBox(InGeometry.GetLocalSize(), true);
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDraggingVertex)
	{
		FinishEditDrag();
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && (bIsDraggingConnector || bIsDraggingUtility))
	{
		FinishEditDrag();
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDraggingDoor)
	{
		FinishEditDrag();
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDraggingRoom)
	{
		FinishEditDrag();
		return FReply::Handled().ReleaseMouseCapture();
	}

	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UInteriorFloorPlanWidget::FinishSelectBox(FVector2D WidgetSize, bool bApply)
{
	const bool bWasDragging = bIsDraggingSelectBox;
	bIsDraggingSelectBox = false;
	if (!bWasDragging || !bApply || !PlayerController) return;
	if (FVector2D::DistSquared(SelectBoxStart, SelectBoxEnd) > 36.0)
	{
		const FBox2D Rect(TArray<FVector2D>{ LocalPositionToFootprintPoint(SelectBoxStart, WidgetSize), LocalPositionToFootprintPoint(SelectBoxEnd, WidgetSize) });
		PlayerController->SelectRoomsInRect(Rect, bSelectBoxAdd);
	}
	else if (!bSelectBoxAdd)
	{
		PlayerController->SelectRoom(FString());
	}
}

void UInteriorFloorPlanWidget::FinishEditDrag()
{
	bIsDraggingPrefab = false;
	if (bIsRotatingGizmo)
	{
		// One Undo step for the whole turn.
		bIsRotatingGizmo = false;
		if (PlayerController) PlayerController->EndTransformRotation();
	}
	if (bIsScalingConnector)
	{
		bIsScalingConnector = false;
		if (bConnectorScaled && InteriorGen) InteriorGen->RecordHistorySnapshot();
		bConnectorScaled = false;
	}
	// A selection window that loses the mouse (focus lost, Esc) is dropped without selecting anything.
	bIsDraggingSelectBox = false;
	if (bIsDraggingVertex)
	{
		// One history step for the whole corner drag, and only when a corner did move.
		bIsDraggingVertex = false;
		if (bVertexMoved && InteriorGen) InteriorGen->RecordHistorySnapshot();
		bVertexMoved = false;
		DraggingVertexIndex = INDEX_NONE;
	}
	if (bIsDraggingUtility)
	{
		// One history step for the whole drag, and only when the point did move.
		bIsDraggingUtility = false;
		if (bUtilityMoved && InteriorGen) InteriorGen->RecordHistorySnapshot();
		bUtilityMoved = false;
	}
	if (bIsDraggingConnector)
	{
		// One history step for the whole drag, and only when the connector did move.
		bIsDraggingConnector = false;
		if (bConnectorMoved && InteriorGen) InteriorGen->RecordHistorySnapshot();
		// Let go on a spot that was refused: say why, once (which floor, what is in the way), instead of the connector just not following.
		if (bConnectorMoveRefused && PlayerController) PlayerController->ReportConnectorMoveRefused();
		bConnectorMoved = false;
		bConnectorMoveRefused = false;
		DragStartConnectorPositions.Reset();
	}
	if (bIsResizingRoom)
	{
		bIsResizingRoom = false;
		if (bResizeChanged && InteriorGen && DragTarget.IsValid() && PlayerController && PlayerController->IsInSelectMode() &&
			PlayerController->GetSelectedRoomId() == DraggingRoomId && InteriorGen->GetActiveFloor() == DragFloor && InteriorGen->GetLoadedBuildingId() == DragBuildingId)
			PlayerController->ResizeRoomToBounds(DraggingRoomId, ResizePreviewPosition, ResizePreviewSize);
	}
	if (bIsDraggingDoor)
	{
		bIsDraggingDoor = false;
		if (InteriorGen && DragTarget.IsValid()) InteriorGen->RecordHistorySnapshot();
	}
	if (bIsDraggingRoom)
	{
		bIsDraggingRoom = false;
		if (PlayerController && DragTarget.IsValid()) PlayerController->CommitRoomMove(DraggingRoomId, DragStartRoomFootprint, /*bNudgeToFreeSpot=*/true);
	}
	DragTarget.Reset();
	DragAxisLock = 0;
	bResizeUniform = false;
}

void UInteriorFloorPlanWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bIsPanning = false;
	FinishEditDrag();
	ReferenceDrag.Reset();
	InvalidateLayoutAndVolatility();
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
}

FReply UInteriorFloorPlanWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Where the pointer is, for the preview of the line being drawn.
	bHasPointer = true;
	PointerLocal = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	bPointerShift = InMouseEvent.IsShiftDown();
	if (ReferenceDrag)
	{
		if (!IsReferenceDragValid(EditableReferenceImage()) || !InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
		{
			ReferenceDrag.Reset();
			InvalidateLayoutAndVolatility();
			return FReply::Handled().ReleaseMouseCapture();
		}
		FReferenceDrag& Drag = *ReferenceDrag;
		const FVector2D Point = LocalPositionToFootprintPoint(PointerLocal, InGeometry.GetLocalSize());
		if (Drag.bCrop)
		{
			const FVector2D Size = ReferenceImageSize(Drag.Start);
			const auto ToUV = [&](const FVector2D& Position)
			{
				const FVector2D UV = (Position - Drag.Start.Center) / Size + FVector2D(0.5, 0.5);
				return FVector2D(FMath::Clamp(UV.X, 0.0, 1.0), FMath::Clamp(UV.Y, 0.0, 1.0));
			};
			Drag.CropPreview = FBox2D(ForceInit);
			Drag.CropPreview += ToUV(Drag.StartPoint);
			Drag.CropPreview += ToUV(Point);
		}
		else
		{
			Drag.Preview.Center = Drag.Start.Center + Point - Drag.StartPoint;
		}
		InvalidateLayoutAndVolatility();
		return FReply::Handled();
	}
	if (bIsDraggingPrefab)
	{
		if (!IsPrefabDragValid() || !InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
		{
			bIsDraggingPrefab = false;
			return FReply::Handled().ReleaseMouseCapture();
		}
		const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		PrefabEnd = ShapeDragEnd(LocalPositionToFootprintPoint(Local, InGeometry.GetLocalSize()), InMouseEvent.IsShiftDown());
		bPrefabWasDragged |= FVector2D::DistSquared(Local, PrefabStartLocal) > 36.0;
		return FReply::Handled();
	}
	if (bIsResizingRoom)
	{
		if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || !DragTarget.IsValid() || !PlayerController || !InteriorGen ||
			!PlayerController->IsInSelectMode() || PlayerController->GetSelectedRoomId() != DraggingRoomId ||
			InteriorGen->GetActiveFloor() != DragFloor || InteriorGen->GetLoadedBuildingId() != DragBuildingId)
		{
			FinishEditDrag();
			return FReply::Handled().ReleaseMouseCapture();
		}
		ResizeAtLocalPosition(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()), InGeometry.GetLocalSize());
		return FReply::Handled();
	}
	if (bIsDraggingSelectBox)
	{
		if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || !PlayerController || !PlayerController->IsInSelectMode())
		{
			bIsDraggingSelectBox = false;
			return FReply::Handled().ReleaseMouseCapture();
		}
		SelectBoxEnd = PointerLocal;
		bSelectBoxAdd = InMouseEvent.IsShiftDown();
		return FReply::Handled();
	}
	if (bIsRotatingGizmo)
	{
		if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || !PlayerController || !PlayerController->IsTransformGizmoShown())
		{
			FinishEditDrag();
			return FReply::Handled().ReleaseMouseCapture();
		}
		// The turn is the angle the pointer has gone round the centre, in the plan's own axes (so it is right whatever the plan is turned to).
		const FVector2D At = LocalPositionToFootprintPoint(PointerLocal, InGeometry.GetLocalSize()) - GizmoCenterFootprint;
		if (At.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			const float Angle = FMath::RadiansToDegrees(FMath::Atan2(At.Y, At.X));
			const float Delta = FMath::UnwindDegrees(Angle - GizmoLastAngle);
			if (!FMath::IsNearlyZero(Delta, 0.01f)) PlayerController->RotateSelectionByDegrees(Delta);
			GizmoLastAngle = Angle;
		}
		return FReply::Handled();
	}
	if (bIsScalingConnector)
	{
		if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || !PlayerController || !PlayerController->IsTransformGizmoShown())
		{
			FinishEditDrag();
			return FReply::Handled().ReleaseMouseCapture();
		}
		// The connector grows with the pointer's distance from its middle, from the size it had when grabbed.
		const float Factor = FVector2D::Distance(PointerLocal, GizmoCenterLocal) / FMath::Max(ScaleStartDistance, 1.f);
		if (PlayerController->SetSelectedConnectorScale(ScaleStartValue * Factor)) bConnectorScaled = true;
		return FReply::Handled();
	}
	if (bIsDraggingVertex)
	{
		if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || !DragTarget.IsValid() || !PlayerController || !InteriorGen ||
			!PlayerController->IsInSelectMode() || PlayerController->GetSelectedRoomId() != DraggingRoomId ||
			InteriorGen->GetActiveFloor() != DragFloor || InteriorGen->GetLoadedBuildingId() != DragBuildingId)
		{
			FinishEditDrag();
			return FReply::Handled().ReleaseMouseCapture();
		}
		FVector2D Target = LocalPositionToFootprintPoint(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()), InGeometry.GetLocalSize());
		// Snap the corner to another room's corner, as drawing does (the room's own corners are not offered).
		if (PlayerController->IsSnapEnabled())
		{
			FVector2D SnapOffset;
			if (InteriorGen->FindSnapOffset(InteriorGen->GetActiveFloor(), { Target }, DraggingRoomId, 80.f / PlayerController->GetBuildingScale(), SnapOffset)) Target += SnapOffset;
		}
		const TArray<FVector2D> Vertices = PlayerController->GetSelectedRoomVertices();
		if (Vertices.IsValidIndex(DraggingVertexIndex) && !Vertices[DraggingVertexIndex].Equals(Target, 1e-6))
		{
			// The corner goes where the pointer is, or, where that is not allowed (outside the building, across another room, crossing itself), as
			// far along the way as it is allowed: it stops at the limit instead of jumping back or freezing.
			FVector2D Goal = Target;
			if (!PlayerController->CanMoveSelectedRoomVertex(DraggingVertexIndex, Target))
			{
				FVector2D Good = Vertices[DraggingVertexIndex], Bad = Target;
				for (int32 Step = 0; Step < 16; ++Step)
				{
					const FVector2D Mid = (Good + Bad) * 0.5;
					(PlayerController->CanMoveSelectedRoomVertex(DraggingVertexIndex, Mid) ? Good : Bad) = Mid;
				}
				Goal = Good;
			}
			if (PlayerController->MoveSelectedRoomVertex(DraggingVertexIndex, Goal)) bVertexMoved = true;
		}
		return FReply::Handled();
	}
	if (bIsDraggingUtility)
	{
		if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || !PlayerController)
		{
			FinishEditDrag();
			return FReply::Handled().ReleaseMouseCapture();
		}
		FVector2D Target = LocalPositionToFootprintPoint(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()), InGeometry.GetLocalSize()) + DragUtilityGrabOffset;
		if (PlayerController->MoveSelectedUtilityTo(Target)) bUtilityMoved = true;
		return FReply::Handled();
	}
	if (bIsDraggingConnector)
	{
		// Same defensive check as the door drag below.
		if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || !PlayerController)
		{
			FinishEditDrag();
			return FReply::Handled().ReleaseMouseCapture();
		}
		const FVector2D CurrentFootprintPosition = LocalPositionToFootprintPoint(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()), InGeometry.GetLocalSize());
		FVector2D Delta = CurrentFootprintPosition - DragStartFootprintPosition;
		if (DragAxisLock == 1) Delta.Y = 0.0;
		if (DragAxisLock == 2) Delta.X = 0.0;
		TMap<int32, FVector2D> NewPositions;
		for (const TPair<int32, FVector2D>& Pair : DragStartConnectorPositions)
		{
			NewPositions.Add(Pair.Key, Pair.Value + Delta);
		}
		const bool bMoved = PlayerController->MoveSelectedConnectorTo(NewPositions);
		if (bMoved && !Delta.IsNearlyZero(0.0001))
		{
			bConnectorMoved = true;
		}
		bConnectorMoveRefused = !bMoved && !Delta.IsNearlyZero(0.0001);
		return FReply::Handled();
	}
	if (bIsDraggingDoor)
	{
		// Same defensive check as panning below - a mouse-up that doesn't reach this widget
		// shouldn't leave the drag stuck on forever.
		if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || !DragTarget.IsValid())
		{
			FinishEditDrag();
			return FReply::Handled().ReleaseMouseCapture();
		}

		const FVector2D CurrentLocalPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		float NewOffsetWorldUnits = 0.f;
		if (PlayerController && ComputeDoorDragOffset(DraggingDoorRoomId, DraggingDoorEdgeIndex, CurrentLocalPosition, InGeometry.GetLocalSize(), NewOffsetWorldUnits))
		{
			PlayerController->MoveDoor(DraggingDoorRoomId, DraggingDoorId, NewOffsetWorldUnits);
		}
		return FReply::Handled();
	}

	if (bIsDraggingRoom)
	{
		// Same defensive check as the door drag above.
		if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || !DragTarget.IsValid())
		{
			FinishEditDrag();
			return FReply::Handled().ReleaseMouseCapture();
		}

		const FVector2D CurrentLocalPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D CurrentFootprintPosition = LocalPositionToFootprintPoint(CurrentLocalPosition, InGeometry.GetLocalSize());
		FVector2D Delta = CurrentFootprintPosition - DragStartFootprintPosition;
		if (DragAxisLock == 1) Delta.Y = 0.0;
		if (DragAxisLock == 2) Delta.X = 0.0;

		TArray<FVector2D> NewFootprint;
		NewFootprint.Reserve(DragStartRoomFootprint.Num());
		for (const FVector2D& Point : DragStartRoomFootprint)
		{
			NewFootprint.Add(Point + Delta);
		}

		if (PlayerController && InteriorGen)
		{
			// Vertex snapping: if any corner of the room as currently dragged is close to another
			// room/wall's corner, nudge the whole footprint so that pair lines up exactly - same
			// tolerance/approach as P-mode's next click (see AddRoomPointAtFootprintPosition, and
			// ANavPlayerController::GetBuildingScale's own comment for why this divides by Scale
			// instead of using a bare footprint-unit constant).
			const float SnapToleranceFootprintUnits = 80.f / PlayerController->GetBuildingScale();
			FVector2D SnapOffset;
			if (PlayerController->IsSnapEnabled() && InteriorGen->FindSnapOffset(InteriorGen->GetActiveFloor(), NewFootprint, DraggingRoomId, SnapToleranceFootprintUnits, SnapOffset))
			{
				for (FVector2D& Point : NewFootprint)
				{
					Point += SnapOffset;
				}
			}

			PlayerController->MoveRoomTo(DraggingRoomId, NewFootprint);
		}
		return FReply::Handled();
	}

	if (bIsPanning)
	{
		// Defensive: NativeOnMouseButtonUp doesn't always reach this widget for the right button
		// (observed on the very first right-click right after the widget is constructed - capture/
		// routing seems to need a frame to settle) - if the button isn't actually held anymore by
		// the time a move event arrives, stop panning here instead of getting stuck forever.
		if (!InMouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
		{
			bIsPanning = false;
			return FReply::Handled().ReleaseMouseCapture();
		}

		float PixelsPerUnit;
		FVector2D UnusedOrigin;
		ComputeViewTransform(InGeometry.GetLocalSize(), UnusedOrigin, PixelsPerUnit);

		if (PixelsPerUnit > KINDA_SMALL_NUMBER)
		{
			const FVector2D CurrentLocalPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
			const FVector2D DeltaPixels = CurrentLocalPosition - PanStartLocalPosition;
			// Dragging right should move the content right, i.e. the world point at the widget's
			// center should move left by the same screen-space delta.
			// The delta is on the screen; the plan may be turned, so turn it back into the world's own directions first.
			PanOffset = PanStartOffset - RotateQuarterTurns(DeltaPixels, -ViewQuarterTurns) / PixelsPerUnit;
		}
		return FReply::Handled();
	}

	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UInteriorFloorPlanWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (ReferenceDrag || bIsDraggingPrefab || bIsResizingRoom) return FReply::Handled();
	if (const FInteriorReferenceImage* Image = EditableReferenceImage(); Image && !IsReferenceCropMode())
	{
		const FVector2D Point = LocalPositionToFootprintPoint(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()), InGeometry.GetLocalSize());
		const FVector2D UV = (Point - Image->Center) / ReferenceImageSize(*Image) + FVector2D(0.5, 0.5);
		if (Image->Crop.IsInsideOrOn(UV))
		{
			FInteriorReferenceImage Scaled = *Image;
			const float Width = FMath::Clamp(Image->Width * FMath::Pow(1.1f, InMouseEvent.GetWheelDelta()), 0.01f, 9999999.f);
			Scaled.Center = Point + (Image->Center - Point) * (Width / Image->Width);
			Scaled.Width = Width;
			PlayerController->SetActiveReferenceImage(Scaled);
			InvalidateLayoutAndVolatility();
			return FReply::Handled();
		}
	}
	constexpr float ZoomStep = 0.1f;
	constexpr float MinZoom = 0.2f;
	constexpr float MaxZoom = 5.f;

	ZoomMultiplier = FMath::Clamp(ZoomMultiplier + InMouseEvent.GetWheelDelta() * ZoomStep, MinZoom, MaxZoom);
	return FReply::Handled();
}

// ---- Helper: shortest distance from Point to segment AB, in whatever 2D space they're all in ----
static float DistancePointToSegment2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
{
	const FVector2D AB = B - A;
	const float LengthSq = AB.SizeSquared();
	if (LengthSq < KINDA_SMALL_NUMBER)
	{
		return FVector2D::Distance(Point, A);
	}

	const float T = FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSq, 0.f, 1.f);
	return FVector2D::Distance(Point, A + AB * T);
}

UInteriorFloorPlanWidget::FGizmoLayout UInteriorFloorPlanWidget::ComputeGizmoLayout(FVector2D WidgetSize) const
{
	FGizmoLayout Layout;
	if (!PlayerController || !InteriorGen || !Generator || !PlayerController->IsTransformGizmoShown()) return Layout;
	const APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building) return Layout;
	FVector2D Center, Position, Size;
	bool bRoom = false;
	if (PlayerController->GetSelectedRoomBounds(Position, Size))
	{
		Center = Position + Size * 0.5;
		bRoom = true;
	}
	else
	{
		const TMap<int32, FVector2D> Positions = PlayerController->GetSelectedConnectorPositions(false);
		if (Positions.IsEmpty()) return Layout;
		Center = Positions.CreateConstIterator().Value();
	}
	FVector2D Origin;
	float PixelsPerUnit = 0.f;
	ComputeViewTransform(WidgetSize, Origin, PixelsPerUnit);
	const auto ToLocal = [&](const FVector2D& Footprint)
	{
		return ViewWorldToLocal((Footprint + Building->BuildingOffset) * Generator->GetScale(), Origin, PixelsPerUnit, WidgetSize);
	};
	Layout.CenterFootprint = Center;
	Layout.bRoom = bRoom;
	Layout.Center = ToLocal(Center);
	Layout.DirX = (ToLocal(Center + FVector2D(1, 0)) - Layout.Center).GetSafeNormal();
	Layout.DirY = (ToLocal(Center + FVector2D(0, 1)) - Layout.Center).GetSafeNormal();
	Layout.XTip = Layout.Center + Layout.DirX * 70.0;
	Layout.YTip = Layout.Center + Layout.DirY * 70.0;
	Layout.RingRadius = 55.f;
	if (bRoom)
	{
		// The scale handles sit on the room itself: the middle of its X side, the middle of its Y side and the corner between them.
		Layout.ScaleX = ToLocal(FVector2D(Position.X + Size.X, Center.Y));
		Layout.ScaleY = ToLocal(FVector2D(Center.X, Position.Y + Size.Y));
		Layout.ScaleUniform = ToLocal(Position + Size);
	}
	else
	{
		Layout.ScaleX = Layout.XTip;
		Layout.ScaleY = Layout.YTip;
		Layout.ScaleUniform = Layout.Center + (Layout.DirX + Layout.DirY).GetSafeNormal() * 60.0;
	}
	Layout.bValid = true;
	return Layout;
}

UInteriorFloorPlanWidget::EGizmoPart UInteriorFloorPlanWidget::HitGizmo(FVector2D LocalPosition, FVector2D WidgetSize) const
{
	const FGizmoLayout Layout = ComputeGizmoLayout(WidgetSize);
	if (!Layout.bValid) return EGizmoPart::None;
	switch (PlayerController->GetTransformGizmo())
	{
	case ETransformGizmo::Translate:
		if (FVector2D::Distance(LocalPosition, Layout.Center) <= 9.0) return EGizmoPart::Center;
		if (DistancePointToSegment2D(LocalPosition, Layout.Center, Layout.XTip) <= 8.0) return EGizmoPart::ArrowX;
		if (DistancePointToSegment2D(LocalPosition, Layout.Center, Layout.YTip) <= 8.0) return EGizmoPart::ArrowY;
		break;
	case ETransformGizmo::Rotate:
		if (FMath::Abs(FVector2D::Distance(LocalPosition, Layout.Center) - Layout.RingRadius) <= 8.0) return EGizmoPart::Ring;
		break;
	case ETransformGizmo::Scale:
		if (FVector2D::Distance(LocalPosition, Layout.ScaleUniform) <= 10.0) return EGizmoPart::ScaleUniform;
		if (Layout.bRoom && FVector2D::Distance(LocalPosition, Layout.ScaleX) <= 10.0) return EGizmoPart::ScaleX;
		if (Layout.bRoom && FVector2D::Distance(LocalPosition, Layout.ScaleY) <= 10.0) return EGizmoPart::ScaleY;
		break;
	default:
		break;
	}
	return EGizmoPart::None;
}

bool UInteriorFloorPlanWidget::StartGizmoDrag(EGizmoPart Part, FVector2D LocalPosition, FVector2D WidgetSize, bool bShift)
{
	if (!PlayerController || !InteriorGen) return false;
	const FGizmoLayout Layout = ComputeGizmoLayout(WidgetSize);
	if (!Layout.bValid) return false;
	const FString RoomId = PlayerController->GetSelectedRoomId();
	AInteriorRoom* Room = RoomId.IsEmpty() ? nullptr : InteriorGen->GetRoomById(RoomId);
	DragFloor = InteriorGen->GetActiveFloor();
	DragBuildingId = InteriorGen->GetLoadedBuildingId();
	switch (Part)
	{
	case EGizmoPart::ArrowX:
	case EGizmoPart::ArrowY:
	case EGizmoPart::Center:
		DragAxisLock = Part == EGizmoPart::ArrowX ? 1 : Part == EGizmoPart::ArrowY ? 2 : 0;
		DragStartFootprintPosition = LocalPositionToFootprintPoint(LocalPosition, WidgetSize);
		if (Room)
		{
			// The same drag as pulling the room by its body, along one axis or free.
			bIsDraggingRoom = true;
			DragTarget = Room;
			DraggingRoomId = RoomId;
			DragStartRoomFootprint = Room->GetFootprint();
			return true;
		}
		bIsDraggingConnector = true;
		bConnectorMoved = false;
		bConnectorMoveRefused = false;
		DragStartConnectorPositions = PlayerController->GetSelectedConnectorPositions(bShift);
		return !DragStartConnectorPositions.IsEmpty();
	case EGizmoPart::Ring:
		bIsRotatingGizmo = true;
		GizmoCenterFootprint = Layout.CenterFootprint;
		{
			const FVector2D At = LocalPositionToFootprintPoint(LocalPosition, WidgetSize) - GizmoCenterFootprint;
			GizmoLastAngle = FMath::RadiansToDegrees(FMath::Atan2(At.Y, At.X));
		}
		PlayerController->BeginTransformRotation();
		return true;
	case EGizmoPart::ScaleX:
	case EGizmoPart::ScaleY:
	case EGizmoPart::ScaleUniform:
		if (Room)
		{
			// The same resize as pulling the room's side or corner, with the corner keeping the room's proportions.
			FVector2D Position;
			if (!PlayerController->GetSelectedRoomBounds(Position, ResizeStartSize)) return false;
			ResizeStartPosition = ResizePreviewPosition = Position;
			ResizePreviewSize = ResizeStartSize;
			bIsResizingRoom = true;
			bResizeChanged = false;
			bResizeUniform = Part == EGizmoPart::ScaleUniform;
			ResizeStartAspect = ResizeStartSize.X > KINDA_SMALL_NUMBER ? ResizeStartSize.Y / ResizeStartSize.X : 1.0;
			ResizeDirection = Part == EGizmoPart::ScaleX ? FVector2D(1, 0) : Part == EGizmoPart::ScaleY ? FVector2D(0, 1) : FVector2D(1, 1);
			DraggingRoomId = RoomId;
			DragTarget = Room;
			DragStartRoomFootprint = Room->GetFootprint();
			return true;
		}
		// A connector has one size: it grows or shrinks as a whole.
		bIsScalingConnector = true;
		bConnectorScaled = false;
		GizmoCenterLocal = Layout.Center;
		ScaleStartDistance = FMath::Max(static_cast<float>(FVector2D::Distance(LocalPosition, Layout.Center)), 1.f);
		ScaleStartValue = PlayerController->GetSelectedConnectorScale();
		return true;
	default:
		return false;
	}
}

AInteriorRoom* UInteriorFloorPlanWidget::FindRoomNearLocalPosition(FVector2D LocalPosition, FVector2D WidgetSize, int32* OutEdgeIndex, float* OutOffsetWorldUnits) const
{
	if (!InteriorGen || !Generator)
	{
		return nullptr;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return nullptr;
	}

	FVector2D Origin;
	float PixelsPerUnit;
	ComputeViewTransform(WidgetSize, Origin, PixelsPerUnit);

	const float Scale = Generator->GetScale();
	auto WorldToLocal = [&](const FVector2D& WorldPoint) -> FVector2D
		{
			return ViewWorldToLocal(WorldPoint, Origin, PixelsPerUnit, WidgetSize);
		};

	constexpr float ClickTolerancePixels = 10.f;
	constexpr float DoorWidthWorldUnits = 90.f; // matches AInteriorGenerator::AddDoorToRoom's fixed door width

	// Track the CLOSEST edge across every room, not just the first one within tolerance - two rooms
	// that share a wall both have an edge sitting right on top of each other, so a click near that
	// shared wall used to always resolve to whichever room happened to come first in
	// GetRoomsOnFloor's array order, no matter which side the user actually meant. That made it
	// impossible to ever add/select a door on the "other" room's own wall at a shared boundary (see
	// the bug report this fixed - a door added on the far side kept landing on the near room instead,
	// and the nav route planner then had to detour around the far room's still-solid wall).
	AInteriorRoom* BestRoom = nullptr;
	int32 BestEdgeIndex = INDEX_NONE;
	float BestDistance = ClickTolerancePixels;
	FVector2D BestA = FVector2D::ZeroVector;
	FVector2D BestB = FVector2D::ZeroVector;
	FVector2D BestWorldA = FVector2D::ZeroVector;
	FVector2D BestWorldB = FVector2D::ZeroVector;

	for (AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
	{
		if (!Room)
		{
			continue;
		}

		const TArray<FVector2D>& Footprint = Room->GetFootprint();
		const int32 N = Footprint.Num();
		if (N < 2)
		{
			continue;
		}

		TArray<FVector2D> WorldPoints;
		WorldPoints.Reserve(N);
		for (const FVector2D& Point : Footprint)
		{
			WorldPoints.Add((Point + Building->BuildingOffset) * Scale);
		}

		const int32 NumEdges = Room->IsClosedLoop() ? N : N - 1;
		for (int32 i = 0; i < NumEdges; i++)
		{
			const FVector2D& WorldA = WorldPoints[i];
			const FVector2D& WorldB = WorldPoints[(i + 1) % N];
			const FVector2D A = WorldToLocal(WorldA);
			const FVector2D B = WorldToLocal(WorldB);

			const float Distance = DistancePointToSegment2D(LocalPosition, A, B);
			if (Distance <= BestDistance)
			{
				BestRoom = Room;
				BestEdgeIndex = i;
				BestDistance = Distance;
				BestA = A;
				BestB = B;
				BestWorldA = WorldA;
				BestWorldB = WorldB;
			}
		}
	}

	if (!BestRoom)
	{
		return nullptr;
	}

	if (OutEdgeIndex)
	{
		*OutEdgeIndex = BestEdgeIndex;
	}
	if (OutOffsetWorldUnits)
	{
		const FVector2D LocalAB = BestB - BestA;
		const float LocalLengthSq = LocalAB.SizeSquared();
		const float T = (LocalLengthSq > KINDA_SMALL_NUMBER) ? FMath::Clamp(FVector2D::DotProduct(LocalPosition - BestA, LocalAB) / LocalLengthSq, 0.f, 1.f) : 0.f;
		const float EdgeLengthWorldUnits = FVector2D::Distance(BestWorldA, BestWorldB);
		const float ClickOffsetWorldUnits = T * EdgeLengthWorldUnits;
		*OutOffsetWorldUnits = FMath::Clamp(ClickOffsetWorldUnits - DoorWidthWorldUnits * 0.5f, 0.f, FMath::Max(0.f, EdgeLengthWorldUnits - DoorWidthWorldUnits));
	}
	return BestRoom;
}

// ---- Helper: even-odd ray-casting point-in-polygon test, in whatever 2D space Point/Poly are both in ----
static bool IsPointInPolygon2D(const FVector2D& Point, const TArray<FVector2D>& Poly)
{
	bool bInside = false;
	const int32 N = Poly.Num();
	for (int32 i = 0, j = N - 1; i < N; j = i++)
	{
		const FVector2D& Pi = Poly[i];
		const FVector2D& Pj = Poly[j];
		const bool bCrosses = ((Pi.Y > Point.Y) != (Pj.Y > Point.Y)) &&
			(Point.X < (Pj.X - Pi.X) * (Point.Y - Pi.Y) / (Pj.Y - Pi.Y) + Pi.X);
		if (bCrosses)
		{
			bInside = !bInside;
		}
	}
	return bInside;
}

AInteriorRoom* UInteriorFloorPlanWidget::FindRoomContainingLocalPosition(FVector2D LocalPosition, FVector2D WidgetSize) const
{
	if (!InteriorGen || !Generator)
	{
		return nullptr;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return nullptr;
	}

	FVector2D Origin;
	float PixelsPerUnit;
	ComputeViewTransform(WidgetSize, Origin, PixelsPerUnit);

	const float Scale = Generator->GetScale();
	auto WorldToLocal = [&](const FVector2D& WorldPoint) -> FVector2D
		{
			return ViewWorldToLocal(WorldPoint, Origin, PixelsPerUnit, WidgetSize);
		};

	for (AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
	{
		if (!Room || !Room->IsClosedLoop())
		{
			continue;
		}

		const TArray<FVector2D>& Footprint = Room->GetFootprint();
		if (Footprint.Num() < 3)
		{
			continue;
		}

		TArray<FVector2D> LocalPoints;
		LocalPoints.Reserve(Footprint.Num());
		for (const FVector2D& Point : Footprint)
		{
			LocalPoints.Add(WorldToLocal((Point + Building->BuildingOffset) * Scale));
		}

		if (IsPointInPolygon2D(LocalPosition, LocalPoints))
		{
			return Room;
		}
	}

	return nullptr;
}

bool UInteriorFloorPlanWidget::FindDoorNearLocalPosition(FVector2D LocalPosition, FVector2D WidgetSize, FString& OutRoomId, FString& OutDoorId, int32* OutEdgeIndex) const
{
	if (!InteriorGen || !Generator)
	{
		return false;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return false;
	}

	FVector2D Origin;
	float PixelsPerUnit;
	ComputeViewTransform(WidgetSize, Origin, PixelsPerUnit);

	const float Scale = Generator->GetScale();
	auto WorldToLocal = [&](const FVector2D& WorldPoint) -> FVector2D
		{
			return ViewWorldToLocal(WorldPoint, Origin, PixelsPerUnit, WidgetSize);
		};

	constexpr float ClickTolerancePixels = 10.f;

	for (AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
	{
		if (!Room)
		{
			continue;
		}

		const TArray<FVector2D>& Footprint = Room->GetFootprint();
		const int32 N = Footprint.Num();
		if (N < 2)
		{
			continue;
		}

		for (const FInteriorOpening& Opening : Room->GetOpenings())
		{
			if (Opening.Type != EInteriorOpeningType::Door || !Footprint.IsValidIndex(Opening.EdgeIndex))
			{
				continue;
			}

			const FVector2D WorldA = (Footprint[Opening.EdgeIndex] + Building->BuildingOffset) * Scale;
			const FVector2D WorldB = (Footprint[(Opening.EdgeIndex + 1) % N] + Building->BuildingOffset) * Scale;
			const float EdgeLength = FVector2D::Distance(WorldA, WorldB);
			if (EdgeLength < KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const FVector2D Dir = (WorldB - WorldA) / EdgeLength;

			const FVector2D MarkerLocalStart = WorldToLocal(WorldA + Dir * Opening.Offset);
			const FVector2D MarkerLocalEnd = WorldToLocal(WorldA + Dir * (Opening.Offset + Opening.Width));

			if (DistancePointToSegment2D(LocalPosition, MarkerLocalStart, MarkerLocalEnd) <= ClickTolerancePixels)
			{
				OutRoomId = Room->RoomId;
				OutDoorId = Opening.Id;
				if (OutEdgeIndex)
				{
					*OutEdgeIndex = Opening.EdgeIndex;
				}
				return true;
			}
		}
	}

	return false;
}

bool UInteriorFloorPlanWidget::FindUtilityNearLocalPosition(FVector2D LocalPosition, FVector2D WidgetSize, FString& OutUtilityId) const
{
	if (!InteriorGen || !Generator) return false;
	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building) return false;
	FVector2D Origin;
	float PixelsPerUnit;
	ComputeViewTransform(WidgetSize, Origin, PixelsPerUnit);
	constexpr float ClickTolerancePixels = 12.f;
	// The nearest one wins when several are close together.
	float BestDistance = ClickTolerancePixels;
	bool bFound = false;
	for (const FInteriorUtility& Utility : InteriorGen->GetUtilities())
	{
		if (Utility.Level != InteriorGen->GetActiveFloor()) continue;
		const FVector2D MarkerLocal = ViewWorldToLocal((Utility.Position + Building->BuildingOffset) * Generator->GetScale(), Origin, PixelsPerUnit, WidgetSize);
		const float Distance = static_cast<float>(FVector2D::Distance(LocalPosition, MarkerLocal));
		if (Distance <= BestDistance)
		{
			BestDistance = Distance;
			OutUtilityId = Utility.Id;
			bFound = true;
		}
	}
	return bFound;
}

bool UInteriorFloorPlanWidget::FindConnectorNearLocalPosition(FVector2D LocalPosition, FVector2D WidgetSize, FString& OutGroupId) const
{
	if (!InteriorGen || !Generator)
	{
		return false;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return false;
	}

	FVector2D Origin;
	float PixelsPerUnit;
	ComputeViewTransform(WidgetSize, Origin, PixelsPerUnit);

	const float Scale = Generator->GetScale();
	auto WorldToLocal = [&](const FVector2D& WorldPoint) -> FVector2D
		{
			return ViewWorldToLocal(WorldPoint, Origin, PixelsPerUnit, WidgetSize);
		};

	constexpr float ClickTolerancePixels = 10.f;
	const int32 ActiveFloor = InteriorGen->GetActiveFloor();

	for (const FInteriorNavNode& Node : InteriorGen->GetNavNodes())
	{
		if (Node.Level != ActiveFloor)
		{
			continue;
		}

		const FVector2D MarkerLocal = WorldToLocal((Node.Position + Building->BuildingOffset) * Scale);
		if (FVector2D::Distance(LocalPosition, MarkerLocal) <= ClickTolerancePixels)
		{
			OutGroupId = Node.ConnectorGroupId;
			return true;
		}
	}

	return false;
}

bool UInteriorFloorPlanWidget::ComputeDoorDragOffset(const FString& RoomId, int32 EdgeIndex, FVector2D LocalPosition, FVector2D WidgetSize, float& OutOffsetWorldUnits) const
{
	if (!InteriorGen || !Generator)
	{
		return false;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return false;
	}

	AInteriorRoom* TargetRoom = nullptr;
	for (AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
	{
		if (Room && Room->RoomId == RoomId)
		{
			TargetRoom = Room;
			break;
		}
	}
	if (!TargetRoom)
	{
		return false;
	}

	const TArray<FVector2D>& Footprint = TargetRoom->GetFootprint();
	const int32 N = Footprint.Num();
	if (!Footprint.IsValidIndex(EdgeIndex))
	{
		return false;
	}

	FVector2D Origin;
	float PixelsPerUnit;
	ComputeViewTransform(WidgetSize, Origin, PixelsPerUnit);

	const float Scale = Generator->GetScale();
	auto WorldToLocal = [&](const FVector2D& WorldPoint) -> FVector2D
		{
			return ViewWorldToLocal(WorldPoint, Origin, PixelsPerUnit, WidgetSize);
		};

	const FVector2D WorldA = (Footprint[EdgeIndex] + Building->BuildingOffset) * Scale;
	const FVector2D WorldB = (Footprint[(EdgeIndex + 1) % N] + Building->BuildingOffset) * Scale;
	const FVector2D LocalA = WorldToLocal(WorldA);
	const FVector2D LocalB = WorldToLocal(WorldB);

	const FVector2D LocalAB = LocalB - LocalA;
	const float LocalLengthSq = LocalAB.SizeSquared();
	const float T = (LocalLengthSq > KINDA_SMALL_NUMBER) ? FMath::Clamp(FVector2D::DotProduct(LocalPosition - LocalA, LocalAB) / LocalLengthSq, 0.f, 1.f) : 0.f;

	const float EdgeLengthWorldUnits = FVector2D::Distance(WorldA, WorldB);
	const float ClickOffsetWorldUnits = T * EdgeLengthWorldUnits;
	constexpr float DoorWidthWorldUnits = 90.f; // matches AInteriorGenerator::AddDoorToRoom's fixed door width
	OutOffsetWorldUnits = FMath::Clamp(ClickOffsetWorldUnits - DoorWidthWorldUnits * 0.5f, 0.f, FMath::Max(0.f, EdgeLengthWorldUnits - DoorWidthWorldUnits));
	return true;
}

FVector2D UInteriorFloorPlanWidget::RotateQuarterTurns(const FVector2D& Vector, int32 QuarterTurns)
{
	// Clockwise on the screen, where y points down: a point to the right goes to the bottom.
	switch (((QuarterTurns % 4) + 4) % 4)
	{
	case 1: return FVector2D(-Vector.Y, Vector.X);
	case 2: return FVector2D(-Vector.X, -Vector.Y);
	case 3: return FVector2D(Vector.Y, -Vector.X);
	default: return Vector;
	}
}

void UInteriorFloorPlanWidget::TurnViewBy(int32 QuarterTurns)
{
	ViewQuarterTurns = ((ViewQuarterTurns + QuarterTurns) % 4 + 4) % 4;
}

FVector2D UInteriorFloorPlanWidget::ViewWorldToLocal(const FVector2D& WorldPoint, const FVector2D& Origin, float PixelsPerUnit, const FVector2D& WidgetSize) const
{
	return WidgetSize * 0.5f + RotateQuarterTurns(WorldPoint - Origin, ViewQuarterTurns) * PixelsPerUnit;
}

FVector2D UInteriorFloorPlanWidget::ViewLocalToWorld(const FVector2D& LocalPosition, const FVector2D& Origin, float PixelsPerUnit, const FVector2D& WidgetSize) const
{
	return Origin + RotateQuarterTurns((LocalPosition - WidgetSize * 0.5f) / PixelsPerUnit, -ViewQuarterTurns);
}

void UInteriorFloorPlanWidget::ComputeViewTransform(FVector2D WidgetSize, FVector2D& OutOrigin, float& OutPixelsPerUnit) const
{
	OutOrigin = FVector2D::ZeroVector;
	OutPixelsPerUnit = 1.f;

	if (!InteriorGen || !Generator)
	{
		return;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return;
	}

	const float Scale = Generator->GetScale();
	const TArray<FVector2D>& Footprint = Building->GetFootprint();
	if (Footprint.Num() == 0)
	{
		return;
	}

	FVector2D MinPoint(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D MaxPoint(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	for (const FVector2D& Point : Footprint)
	{
		const FVector2D WorldPoint = (Point + Building->BuildingOffset) * Scale;
		MinPoint.X = FMath::Min(MinPoint.X, WorldPoint.X);
		MinPoint.Y = FMath::Min(MinPoint.Y, WorldPoint.Y);
		MaxPoint.X = FMath::Max(MaxPoint.X, WorldPoint.X);
		MaxPoint.Y = FMath::Max(MaxPoint.Y, WorldPoint.Y);
	}

	// Floor 1 doubles as the indoor/outdoor navigation view. Frame the same highlighted campus
	// area that navigation selection accepts, so every visible blue point is clickable.
	if (InteriorGen->GetActiveFloor() == 1)
	{
		const FBox2D OutdoorBounds = UNavigationPanelWidget::GetOutdoorBounds(*Building);
		if (OutdoorBounds.bIsValid)
		{
			MinPoint = (OutdoorBounds.Min + Building->BuildingOffset) * Scale;
			MaxPoint = (OutdoorBounds.Max + Building->BuildingOffset) * Scale;
		}
	}

	OutOrigin = (MinPoint + MaxPoint) * 0.5f + PanOffset;

	// Fit each axis of the footprint's bounding box into the matching axis of the widget, and use
	// the tighter of the two - scales evenly (no stretching) while actually using the widget's full
	// rectangle, rather than treating both the footprint and the widget as if they were square.
	const float BoundingWidth = MaxPoint.X - MinPoint.X;
	const float BoundingHeight = MaxPoint.Y - MinPoint.Y;
	const float Margin = 0.85f; // leave ~15% breathing room around the footprint

	// Turned a quarter (or three), the plan's width runs along the widget's height, so the widget's sides swap.
	const FVector2D FitSize = (ViewQuarterTurns & 1) ? FVector2D(WidgetSize.Y, WidgetSize.X) : WidgetSize;
	const float ScaleX = (BoundingWidth > KINDA_SMALL_NUMBER) ? (FitSize.X * Margin) / BoundingWidth : TNumericLimits<float>::Max();
	const float ScaleY = (BoundingHeight > KINDA_SMALL_NUMBER) ? (FitSize.Y * Margin) / BoundingHeight : TNumericLimits<float>::Max();
	OutPixelsPerUnit = FMath::Min(ScaleX, ScaleY);
	if (OutPixelsPerUnit >= TNumericLimits<float>::Max())
	{
		OutPixelsPerUnit = 1.f;
	}

	OutPixelsPerUnit *= ZoomMultiplier;
}

FVector2D UInteriorFloorPlanWidget::LocalPositionToFootprintPoint(FVector2D LocalPosition, FVector2D WidgetSize) const
{
	if (!InteriorGen || !Generator)
	{
		return FVector2D::ZeroVector;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return FVector2D::ZeroVector;
	}

	FVector2D Origin;
	float PixelsPerUnit;
	ComputeViewTransform(WidgetSize, Origin, PixelsPerUnit);
	if (PixelsPerUnit <= KINDA_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}

	const FVector2D WorldPoint = ViewLocalToWorld(LocalPosition, Origin, PixelsPerUnit, WidgetSize);

	const float Scale = Generator->GetScale();
	if (Scale <= KINDA_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}

	return WorldPoint / Scale - Building->BuildingOffset;
}

int32 UInteriorFloorPlanWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (!InteriorGen || !Generator)
	{
		return LayerId;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return LayerId;
	}

	const FVector2D WidgetSize = AllottedGeometry.GetLocalSize();
	FVector2D Origin;
	float PixelsPerUnit;
	ComputeViewTransform(WidgetSize, Origin, PixelsPerUnit);

	const float Scale = Generator->GetScale();

	auto WorldToLocal = [&](const FVector2D& WorldPoint) -> FVector2D
		{
			return ViewWorldToLocal(WorldPoint, Origin, PixelsPerUnit, WidgetSize);
		};

	// ---- Reference image (PAT-201): under the drawing, never part of it ----
	const FInteriorReferenceImage* Image = InteriorGen->GetReferenceImage(InteriorGen->GetActiveFloor());
	if (Image && Image->bVisible)
	{
		if (Image->Path != ReferenceImagePath && Image->Path != ReferenceImageFailedPath)
		{
			ReferenceImageTexture = FPaths::FileExists(Image->Path) ? FImageUtils::ImportFileAsTexture2D(Image->Path) : nullptr;
			ReferenceImagePath = ReferenceImageTexture ? Image->Path : FString();
			ReferenceImageFailedPath = ReferenceImageTexture ? FString() : Image->Path;
			if (ReferenceImageTexture)
			{
				ReferenceImageBrush = FSlateBrush();
				ReferenceImageBrush.SetResourceObject(ReferenceImageTexture);
				ReferenceImageBrush.ImageSize = FVector2f(ReferenceImageTexture->GetSizeX(), ReferenceImageTexture->GetSizeY());
				ReferenceImageBrush.DrawAs = ESlateBrushDrawType::Image;
			}
		}
	}
	const bool bDrawImage = Image && Image->bVisible && ReferenceImageTexture && Image->Path == ReferenceImagePath && ReferenceImageTexture->GetSizeX() > 0;
	const bool bImageOnTop = bDrawImage && !Image->bLocked && (!PlayerController || !PlayerController->IsInteriorInspectionEnabled());
	const auto DrawImage = [&](int32 ImageLayer)
	{
		const bool bPreview = IsReferenceDragValid(Image);
		const FInteriorReferenceImage& Item = bPreview ? ReferenceDrag->Preview : *Image;
		const FVector2D Size = ReferenceImageSize(Item);
		const FBox2D Full(FVector2D::ZeroVector, FVector2D(1.0, 1.0));
		const FBox2D Crop = bImageOnTop && IsReferenceCropMode() ? Full : Item.Crop;
		const FVector2D Center = Item.Center + (Crop.GetCenter() - FVector2D(0.5, 0.5)) * Size;
		const FVector2D PixelSize = Size * Crop.GetSize() * Scale * PixelsPerUnit;
		const FVector2D CenterLocal = WorldToLocal((Center + Building->BuildingOffset) * Scale);
		ReferenceImageBrush.SetUVRegion(Crop);
		FSlateDrawElement::MakeRotatedBox(OutDrawElements, ImageLayer,
			AllottedGeometry.ToPaintGeometry(FVector2f(PixelSize), FSlateLayoutTransform(CenterLocal - PixelSize * 0.5)),
			&ReferenceImageBrush, ESlateDrawEffect::None, FMath::DegreesToRadians(90.f * ViewQuarterTurns), FVector2f(PixelSize) * 0.5f,
			FSlateDrawElement::RelativeToElement, FLinearColor(1.f, 1.f, 1.f, bImageOnTop ? 0.5f : Item.Opacity));
		if (bImageOnTop)
		{
			const FBox2D RimUV = bPreview && ReferenceDrag->CropPreview.bIsValid ? ReferenceDrag->CropPreview : Crop;
			TArray<FVector2D> Rim = { RimUV.Min, FVector2D(RimUV.Max.X, RimUV.Min.Y), RimUV.Max, FVector2D(RimUV.Min.X, RimUV.Max.Y), RimUV.Min };
			for (FVector2D& Point : Rim)
			{
				const FVector2D At = Item.Center + (Point - FVector2D(0.5, 0.5)) * Size;
				Point = WorldToLocal((At + Building->BuildingOffset) * Scale);
			}
			FSlateDrawElement::MakeLines(OutDrawElements, ImageLayer + 1, AllottedGeometry.ToPaintGeometry(), Rim,
				ESlateDrawEffect::None, FLinearColor(0.2f, 0.65f, 1.f, 1.f), true, 2.f);
		}
	};

	// ---- Floor 1 indoor/outdoor context ----
	// Blue is the selectable campus area. The exact building footprint is overlaid in dark gray
	// and reinforced by the heavy black boundary below. Keeping this behind
	// rooms, routes and markers makes the location type clear without obscuring editor content.
	if (InteriorGen->GetActiveFloor() == 1)
	{
		const FSlateBrush* AreaBrush = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
		const FBox2D OutdoorBounds = UNavigationPanelWidget::GetOutdoorBounds(*Building);
		if (OutdoorBounds.bIsValid)
		{
			const FVector2D OutdoorMin = WorldToLocal((OutdoorBounds.Min + Building->BuildingOffset) * Scale);
			const FVector2D OutdoorMax = WorldToLocal((OutdoorBounds.Max + Building->BuildingOffset) * Scale);
			const FVector2D TopLeft(FMath::Min(OutdoorMin.X, OutdoorMax.X), FMath::Min(OutdoorMin.Y, OutdoorMax.Y));
			const FVector2D AreaSize(FMath::Abs(OutdoorMax.X - OutdoorMin.X), FMath::Abs(OutdoorMax.Y - OutdoorMin.Y));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(
				FVector2f(AreaSize.X, AreaSize.Y), FSlateLayoutTransform(TopLeft)), AreaBrush,
				ESlateDrawEffect::None, FLinearColor(0.08f, 0.32f, 0.52f, 0.55f));
		}

		TArray<int32> TriangleIndices;
		if (!bDrawImage) TriangulatePolygon(Building->GetFootprint(), TriangleIndices);
		if (!TriangleIndices.IsEmpty())
		{
			TArray<FSlateVertex> Vertices;
			Vertices.Reserve(Building->GetFootprint().Num());
			for (const FVector2D& Point : Building->GetFootprint())
			{
				Vertices.AddZeroed();
				FSlateVertex& Vertex = Vertices.Last();
				Vertex.Position = FVector2f(AllottedGeometry.LocalToAbsolute(
					WorldToLocal((Point + Building->BuildingOffset) * Scale)));
				Vertex.Color = FLinearColor(0.14f, 0.15f, 0.17f, 0.96f).ToFColor(false);
			}
			TArray<SlateIndex> SlateIndices;
			SlateIndices.Reserve(TriangleIndices.Num());
			for (const int32 Index : TriangleIndices)
			{
				SlateIndices.Add(static_cast<SlateIndex>(Index));
			}
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId,
				AreaBrush->GetRenderingResource(), Vertices, SlateIndices, nullptr, 0, 0);
		}
	}

	if (bDrawImage && !bImageOnTop) DrawImage(LayerId);

	// ---- Building boundary ----
	if (!PlayerController || PlayerController->IsBoundaryVisible())
	{
		TArray<FVector2D> LinePoints;
		for (const FVector2D& Point : Building->GetFootprint())
		{
			LinePoints.Add(WorldToLocal((Point + Building->BuildingOffset) * Scale));
		}
		if (LinePoints.Num() >= 2)
		{
			const FVector2D FirstPoint = LinePoints[0];
			LinePoints.Add(FirstPoint);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::None, FLinearColor::Black, true, 5.f);
		}
	}

	// ---- Doors in the outer wall on this floor: main doors (green) and link doors (blue), the selected one in yellow ----
	{
		const TArray<FVector2D>& Boundary = Building->GetFootprint();
		const auto* SelectedEntrance = PlayerController ? PlayerController->GetSelectedEntrance() : nullptr;
		for (const FInteriorEntrance& Entrance : InteriorGen->GetAllEntrances())
		{
			if (Entrance.Level != InteriorGen->GetActiveFloor() || !Boundary.IsValidIndex(Entrance.EdgeIndex))
			{
				continue;
			}
			const FVector2D A = Boundary[Entrance.EdgeIndex];
			const FVector2D B = Boundary[(Entrance.EdgeIndex + 1) % Boundary.Num()];
			const double Length = FVector2D::Distance(A, B);
			if (Length < KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const FVector2D Direction = (B - A) / Length;
			const FVector2D Start = A + Direction * FMath::Clamp(static_cast<double>(Entrance.Offset) / Scale, 0.0, Length);
			const FVector2D End = A + Direction * FMath::Clamp(static_cast<double>(Entrance.Offset + Entrance.Width) / Scale, 0.0, Length);
			const TArray<FVector2D> Opening = { WorldToLocal((Start + Building->BuildingOffset) * Scale), WorldToLocal((End + Building->BuildingOffset) * Scale) };
			const bool bSelected = SelectedEntrance == &Entrance;
			const FLinearColor Colour = bSelected ? FLinearColor(1.f, 0.85f, 0.2f, 1.f)
				: (Entrance.Kind == EInteriorEntranceKind::Main ? InteriorDoorColors::MainDoor() : InteriorDoorColors::LinkDoor());
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Opening, ESlateDrawEffect::None, Colour, true, bSelected ? 13.f : 9.f);
		}
	}

	// ---- Rooms/walls on the active floor ----
	for (const AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
	{
		if (!Room)
		{
			continue;
		}

		// Layers panel per-category/per-item visibility (see ANavPlayerController::AreRoomsVisible/
		// AreWallsVisible/IsRoomHidden) - Rooms and Walls are separate categories despite sharing this
		// one loop, split by IsClosedLoop() same as everywhere else that distinguishes them. A hidden
		// room's door(s) are drawn as part of this same loop iteration, so skipping the room here
		// already hides them too, no separate check needed.
		if (PlayerController)
		{
			const bool bCategoryVisible = Room->IsClosedLoop() ? PlayerController->AreRoomsVisible() : PlayerController->AreWallsVisible();
			if (!bCategoryVisible || PlayerController->IsRoomHidden(Room->RoomId))
			{
				continue;
			}
		}

		const TArray<FVector2D>& Footprint = Room->GetFootprint();
		const int32 N = Footprint.Num();
		if (N < 2)
		{
			continue;
		}

		TArray<FVector2D> WorldPoints;
		WorldPoints.Reserve(N);
		for (const FVector2D& Point : Footprint)
		{
			WorldPoints.Add((Point + Building->BuildingOffset) * Scale);
		}

		// A room is filled with its type's colour (the walls are drawn over it), so the kinds read at a glance. A freestanding wall has none.
		if (Room->IsClosedLoop() && N >= 3 && (!PlayerController || PlayerController->AreRoomColoursVisible()))
		{
			TArray<int32> FillTriangles;
			TriangulatePolygon(Footprint, FillTriangles);
			if (!FillTriangles.IsEmpty())
			{
				constexpr float RoomFillOpacity = 0.55f;
				FLinearColor Fill = RoomTypes::Color(Room->RoomType);
				Fill.A = RoomFillOpacity;
				TArray<FSlateVertex> FillVertices;
				FillVertices.Reserve(N);
				for (const FVector2D& WorldPoint : WorldPoints)
				{
					FillVertices.AddZeroed();
					FSlateVertex& Vertex = FillVertices.Last();
					Vertex.Position = FVector2f(AllottedGeometry.LocalToAbsolute(WorldToLocal(WorldPoint)));
					Vertex.Color = Fill.ToFColor(true);
				}
				TArray<SlateIndex> FillIndices;
				FillIndices.Reserve(FillTriangles.Num());
				for (const int32 Index : FillTriangles)
				{
					FillIndices.Add(static_cast<SlateIndex>(Index));
				}
				FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"))->GetRenderingResource(),
					FillVertices, FillIndices, nullptr, 0, 0);
			}
		}

		const bool bIsSelected = PlayerController && PlayerController->IsRoomSelected(Room->RoomId);
		const FLinearColor LineColor = bIsSelected ? FLinearColor(1.f, 0.15f, 0.15f, 1.f) : FLinearColor::Black;
		const float LineThickness = bIsSelected ? 4.f : 3.f;

		// Draw edge by edge (rather than one continuous outline) so a door/void opening on an edge
		// can leave a visible gap instead of a solid line straight through it.
		const int32 NumEdges = Room->IsClosedLoop() ? N : N - 1;
		for (int32 EdgeIndex = 0; EdgeIndex < NumEdges; EdgeIndex++)
		{
			const FVector2D& WorldA = WorldPoints[EdgeIndex];
			const FVector2D& WorldB = WorldPoints[(EdgeIndex + 1) % N];
			const float EdgeLength = FVector2D::Distance(WorldA, WorldB);
			const FVector2D Dir = (EdgeLength > KINDA_SMALL_NUMBER) ? (WorldB - WorldA) / EdgeLength : FVector2D::ZeroVector;

			// A door/void opening leaves a visible gap; a window still draws as a solid line in 2D
			// (it doesn't open the wall at floor level, just higher up - not meaningful to show as
			// a gap in a top-down plan). A door additionally gets its own marker line drawn on top
			// of the gap (see below) so it reads as "a door" rather than just a break in the wall.
			// A single edge can have more than one door/void on it (e.g. two doors on a long wall),
			// so every matching opening is collected and drawn, not just the first one found.
			TArray<const FInteriorOpening*> EdgeOpenings;
			for (const FInteriorOpening& Opening : Room->GetOpenings())
			{
				if (Opening.EdgeIndex == EdgeIndex && (Opening.Type == EInteriorOpeningType::Door || Opening.Type == EInteriorOpeningType::Void))
				{
					EdgeOpenings.Add(&Opening);
				}
			}
			EdgeOpenings.Sort([](const FInteriorOpening& A, const FInteriorOpening& B) { return A.Offset < B.Offset; });

			auto DrawSegment = [&](const FVector2D& WorldSegA, const FVector2D& WorldSegB, const FLinearColor& Color, float Thickness)
				{
					TArray<FVector2D> LinePoints;
					LinePoints.Add(WorldToLocal(WorldSegA));
					LinePoints.Add(WorldToLocal(WorldSegB));
					FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::None, Color, true, Thickness);
				};

			float EdgeCursor = 0.f;
			for (const FInteriorOpening* Opening : EdgeOpenings)
			{
				const float GapStart = FMath::Clamp(Opening->Offset, 0.f, EdgeLength);
				const float GapEnd = FMath::Clamp(Opening->Offset + Opening->Width, GapStart, EdgeLength);

				if (GapStart > EdgeCursor)
				{
					DrawSegment(WorldA + Dir * EdgeCursor, WorldA + Dir * GapStart, LineColor, LineThickness);
				}

				if (Opening->Type == EInteriorOpeningType::Door)
				{
					const bool bIsSelectedDoor = PlayerController && PlayerController->GetSelectedDoorRoomId() == Room->RoomId && PlayerController->GetSelectedDoorId() == Opening->Id;
					const FLinearColor DoorColor = bIsSelectedDoor ? FLinearColor(1.f, 0.15f, 0.15f, 1.f) : InteriorDoorColors::RoomDoor(); // fluorescent blue, or red when selected
					DrawSegment(WorldA + Dir * GapStart, WorldA + Dir * GapEnd, DoorColor, 5.f);
				}

				EdgeCursor = FMath::Max(EdgeCursor, GapEnd);
			}

			if (EdgeCursor < EdgeLength)
			{
				DrawSegment(WorldA + Dir * EdgeCursor, WorldB, LineColor, LineThickness);
			}
		}
	}

	// ---- Connectors (elevator/stairs/escalator) on the active floor ----
	if (PlayerController)
	{
		constexpr float ConnectorSize = 20.f;
		const FSlateBrush* FallbackBrush = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")); // PathNode only - no icon for it yet
		const FString SelectedConnectorGroupId = PlayerController->GetSelectedConnectorGroupId();

		for (const FInteriorNavNode& Node : InteriorGen->GetNavNodes())
		{
			if (Node.Level != InteriorGen->GetActiveFloor())
			{
				continue;
			}

			// Layers panel per-category/per-item visibility - Path Nodes and I/O Nodes (elevator/
			// stairs/escalator) are separate categories despite sharing this one loop/array.
			const bool bIsPathNode = Node.Type == EInteriorNavNodeType::PathNode;
			if (bIsPathNode
				? (!PlayerController->ArePathNodesVisible() || PlayerController->IsPathNodeHidden(Node.Id))
				: (!PlayerController->AreIONodesVisible() || PlayerController->IsIONodeHidden(Node.Id)))
			{
				continue;
			}

			const FSlateBrush* IconBrush = FallbackBrush;
			FLinearColor NodeColor = FLinearColor(0.6f, 0.6f, 0.6f, 1.f);
			switch (Node.Type)
			{
			case EInteriorNavNodeType::Elevator:  IconBrush = &ElevatorIconBrush;  NodeColor = FLinearColor::White; break;
			case EInteriorNavNodeType::Stairs:    IconBrush = &StairsIconBrush;    NodeColor = FLinearColor::White; break;
			case EInteriorNavNodeType::Escalator: IconBrush = &EscalatorIconBrush; NodeColor = FLinearColor::White; break;
			default: break; // PathNode - keep the plain gray box
			}

			const FVector2D MarkerLocal = WorldToLocal((Node.Position + Building->BuildingOffset) * Scale);
			// Same SizeScale multiplier the 3D marker's footprint uses (see AInteriorConnectorMarker::
			// BuildMarker) - PathNode's SizeScale is always 1 (see GetConnectorFootprintCorners/
			// FInteriorNavNode's own default), so this is a no-op for the plain gray box.
			const float ScaledConnectorSize = ConnectorSize * Node.SizeScale;
			const bool bSelected = !SelectedConnectorGroupId.IsEmpty() && Node.ConnectorGroupId == SelectedConnectorGroupId;
			if (bSelected)
			{
				// The icon's own color no longer distinguishes selected/not, so draw a highlight
				// square behind it instead - same red used for every other selection highlight.
				const float HighlightSize = ScaledConnectorSize + 8.f;
				const FPaintGeometry HighlightGeometry = AllottedGeometry.ToPaintGeometry(FVector2f(HighlightSize, HighlightSize), FSlateLayoutTransform(MarkerLocal - FVector2D(HighlightSize * 0.5f, HighlightSize * 0.5f)));
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, HighlightGeometry, FallbackBrush, ESlateDrawEffect::None, FLinearColor(1.f, 0.15f, 0.15f, 1.f));
			}

			const FPaintGeometry MarkerGeometry = AllottedGeometry.ToPaintGeometry(FVector2f(ScaledConnectorSize, ScaledConnectorSize), FSlateLayoutTransform(MarkerLocal - FVector2D(ScaledConnectorSize * 0.5f, ScaledConnectorSize * 0.5f)));
			// Rotated around the icon's own center (RelativeToElement space, so half its own size,
			// not MarkerLocal) by the same RotationDegrees the 3D marker uses (see
			// AInteriorConnectorMarker::BuildMarker/GetConnectorFootprintCorners) - kept in radians
			// only for this one call, degrees everywhere else in this codebase's own convention.
			FSlateDrawElement::MakeRotatedBox(OutDrawElements, LayerId + 3, MarkerGeometry, IconBrush, ESlateDrawEffect::None,
				FMath::DegreesToRadians(Node.RotationDegrees + 90.f * ViewQuarterTurns), FVector2f(ScaledConnectorSize, ScaledConnectorSize) * 0.5f,
				FSlateDrawElement::RelativeToElement, NodeColor);
		}
	}

	// ---- Utility points (PAT-193) on the active floor: a coloured square with the letter of its type, and its name beside it ----
	if (PlayerController)
	{
		constexpr float UtilitySize = 18.f;
		const FSlateBrush* Box = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
		const FString SelectedUtilityId = PlayerController->GetSelectedUtilityId();
		for (const FInteriorUtility& Utility : InteriorGen->GetUtilities())
		{
			if (Utility.Level != InteriorGen->GetActiveFloor()) continue;
			const FVector2D At = WorldToLocal((Utility.Position + Building->BuildingOffset) * Scale);
			if (Utility.Id == SelectedUtilityId)
			{
				const float Highlight = UtilitySize + 8.f;
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(FVector2f(Highlight, Highlight), FSlateLayoutTransform(At - FVector2D(Highlight * 0.5f, Highlight * 0.5f))),
					Box, ESlateDrawEffect::None, FLinearColor(1.f, 0.15f, 0.15f, 1.f));
			}
				const FSlateBrush* UtilityPicture = UtilityIconBrushes.Find(Utility.Type);
				if (UtilityPicture)
				{
					// The design team's circle of the type (a coloured disc with a white symbol).
					constexpr float PictureSize = UtilitySize + 4.f;
					FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(FVector2f(PictureSize, PictureSize), FSlateLayoutTransform(At - FVector2D(PictureSize * 0.5f, PictureSize * 0.5f))),
						UtilityPicture, ESlateDrawEffect::None, FLinearColor::White);
				}
				else
				{
					FLinearColor Colour(0.6f, 0.5f, 0.85f, 1.f);
					if (Utility.Type == InteriorUtilityTypes::Aed) Colour = FLinearColor(0.2f, 0.7f, 0.35f, 1.f);
					else if (Utility.Type == InteriorUtilityTypes::Bicycle) Colour = FLinearColor(0.25f, 0.55f, 0.95f, 1.f);
					else if (Utility.Type == InteriorUtilityTypes::DropOff) Colour = FLinearColor(0.95f, 0.7f, 0.15f, 1.f);
					FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(FVector2f(UtilitySize, UtilitySize), FSlateLayoutTransform(At - FVector2D(UtilitySize * 0.5f, UtilitySize * 0.5f))),
						Box, ESlateDrawEffect::None, Colour);
					FSlateDrawElement::MakeText(OutDrawElements, LayerId + 4, AllottedGeometry.ToPaintGeometry(FVector2f(UtilitySize, UtilitySize), FSlateLayoutTransform(At - FVector2D(4.f, 8.f))),
						FText::FromString(InteriorUtilityTypes::Letter(Utility.Type)), FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11), ESlateDrawEffect::None, FLinearColor::White);
				}
			if (!Utility.Name.IsEmpty())
			{
				FSlateDrawElement::MakeText(OutDrawElements, LayerId + 4, AllottedGeometry.ToPaintGeometry(FVector2f(160.f, 14.f), FSlateLayoutTransform(At + FVector2D(UtilitySize * 0.5f + 4.f, -7.f))),
					FText::FromString(Utility.Name), FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10), ESlateDrawEffect::None, FLinearColor(0.95f, 0.95f, 0.95f, 1.f));
			}
		}
	}

	// ---- In-progress room/wall being clicked out right now (if any) - not closed into a loop even
	// in draw room mode, since the last edge back to the first point doesn't exist until Finish. ----
	if (PlayerController)
	{
		const TArray<FVector2D>& Pending = PlayerController->GetPendingRoomFootprint();
		if (Pending.Num() > 0)
		{
			TArray<FVector2D> LinePoints;
			for (const FVector2D& Point : Pending)
			{
				LinePoints.Add(WorldToLocal((Point + Building->BuildingOffset) * Scale));
			}

			if (LinePoints.Num() >= 2)
			{
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::None, FLinearColor(1.f, 0.6f, 0.f, 1.f), true, 3.f);
			}

			// The line the next click would add, from the last point to the pointer: straight along X or Y while Shift is held.
			if (bHasPointer && (PlayerController->IsInDrawRoomMode() || PlayerController->IsInDrawWallMode()))
			{
				const FVector2D Next = PlayerController->ConstrainDrawPoint(LocalPositionToFootprintPoint(PointerLocal, AllottedGeometry.GetLocalSize()), bPointerShift);
				const TArray<FVector2D> Preview2 = { LinePoints.Last(), WorldToLocal((Next + Building->BuildingOffset) * Scale) };
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Preview2, ESlateDrawEffect::None,
					FLinearColor(1.f, 0.6f, 0.f, bPointerShift ? 0.9f : 0.5f), true, 2.f);
			}

			// A dot at each clicked point, so even the very first click (before there's a second
			// point to draw a line to) gives visible feedback.
			const FSlateBrush* DotBrush = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
			constexpr float DotSize = 8.f;
			for (const FVector2D& LocalPoint : LinePoints)
			{
				const FPaintGeometry DotGeometry = AllottedGeometry.ToPaintGeometry(FVector2f(DotSize, DotSize), FSlateLayoutTransform(LocalPoint - FVector2D(DotSize * 0.5f, DotSize * 0.5f)));
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3, DotGeometry, DotBrush, ESlateDrawEffect::None, FLinearColor(1.f, 0.6f, 0.f, 1.f));
			}
		}
	}

	// ---- Navigation route and endpoints on the currently viewed floor ----
	if (PlayerController)
	{
		if (const UNavigationPanelWidget* NavigationPanel = PlayerController->GetNavigationPanel();
			NavigationPanel && NavigationPanel->IsRouteGuidanceVisible())
		{
			const int32 ActiveFloor = InteriorGen->GetActiveFloor();
			auto IsVisibleOnCurrentMap = [ActiveFloor, Building](const FNavigationRoutePoint& Point)
				{
					return UNavigationRouteVisualization::IsPointVisible(Point, Building->BuildingId, ActiveFloor);
				};
			auto RouteLocal = [&](const FNavigationRoutePoint& Point)
			{
				return WorldToLocal(Point.bHasWorldPosition ? FVector2D(Point.WorldPosition) : (Point.Position + Building->BuildingOffset) * Scale);
			};
			const TArray<FNavigationRoutePoint>& Route = NavigationPanel->GetRoutePoints();
			const TArray<FNavigationRouteLeg>& RouteLegs = NavigationPanel->GetRouteLegs();
			// Going step by step: only the stretch of the step being shown is drawn in full; the rest of the route is faint.
			const FNavigationRouteFocus Focus = NavigationPanel->GetGuidedFocus();
			for (int32 I = 1; I < Route.Num(); ++I)
			{
				if (!IsVisibleOnCurrentMap(Route[I - 1]) || !IsVisibleOnCurrentMap(Route[I]))
				{
					continue;
				}
				TArray<FVector2D> Segment;
				Segment.Add(RouteLocal(Route[I - 1]));
				Segment.Add(RouteLocal(Route[I]));
				// A journey through stops is drawn in a colour for each leg; a route without stops stays orange.
				FLinearColor SegmentColor(1.f, 0.55f, 0.f, 1.f);
				if (RouteLegs.Num() > 1)
				{
					const int32 LegIndex = RouteLegs.IndexOfByPredicate([I](const FNavigationRouteLeg& Leg) { return I > Leg.FirstPointIndex && I <= Leg.LastPointIndex; });
					if (LegIndex != INDEX_NONE)
					{
						SegmentColor = UNavigationRouteVisualization::LegColor2D(LegIndex);
					}
				}
				if (Focus.IsSet() && !(I > Focus.FirstPointIndex && I <= Focus.LastPointIndex))
				{
					FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 3,
						AllottedGeometry.ToPaintGeometry(), Segment, ESlateDrawEffect::None,
						SegmentColor * 0.3f, true, 3.f);
					continue;
				}
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 4,
					AllottedGeometry.ToPaintGeometry(), Segment, ESlateDrawEffect::None,
					SegmentColor, true, 6.f);
				for (const FNavigationDirectionArrow2D& Arrow :
					UNavigationRouteVisualization::BuildDirectionArrows2D(Segment[0], Segment[1]))
				{
					TArray<FVector2D> Head = { Arrow.HeadLeft, Arrow.Tip, Arrow.HeadRight };
					// Dark outline and white centre keep arrows legible over rooms and the orange route.
					FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 4,
						AllottedGeometry.ToPaintGeometry(), Head, ESlateDrawEffect::None,
						FLinearColor(0.05f, 0.05f, 0.05f, 1.f), true, 5.f);
					FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 4,
						AllottedGeometry.ToPaintGeometry(), Head, ESlateDrawEffect::None,
						FLinearColor::White, true, 2.5f);
				}
			}

			const FSlateBrush* MarkerBrush = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
			auto DrawEndpoint = [&](const FNavigationRoutePoint& Point, const FLinearColor& Color)
				{
					if (!IsVisibleOnCurrentMap(Point))
					{
						return;
					}
					constexpr float MarkerSize = 18.f;
					constexpr float OutlineSize = 24.f;
					const FVector2D MarkerLocal = RouteLocal(Point);
					if (Point.Floor == UNavigationPanelWidget::OutdoorFloor)
					{
						TArray<FVector2D> Diamond;
						Diamond.Add(MarkerLocal + FVector2D(0.f, -OutlineSize * 0.5f));
						Diamond.Add(MarkerLocal + FVector2D(OutlineSize * 0.5f, 0.f));
						Diamond.Add(MarkerLocal + FVector2D(0.f, OutlineSize * 0.5f));
						Diamond.Add(MarkerLocal + FVector2D(-OutlineSize * 0.5f, 0.f));
						Diamond.Add(MarkerLocal + FVector2D(0.f, -OutlineSize * 0.5f));
						FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 5,
							AllottedGeometry.ToPaintGeometry(), Diamond, ESlateDrawEffect::None,
							FLinearColor::White, true, 7.f);
						FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 6,
							AllottedGeometry.ToPaintGeometry(), Diamond, ESlateDrawEffect::None,
							Color, true, 4.f);
						return;
					}
					const FPaintGeometry OutlineGeometry = AllottedGeometry.ToPaintGeometry(
						FVector2f(OutlineSize, OutlineSize),
						FSlateLayoutTransform(MarkerLocal - FVector2D(OutlineSize * 0.5f)));
					FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 5,
						OutlineGeometry, MarkerBrush, ESlateDrawEffect::None, FLinearColor::White);
					const FPaintGeometry MarkerGeometry = AllottedGeometry.ToPaintGeometry(
						FVector2f(MarkerSize, MarkerSize),
						FSlateLayoutTransform(MarkerLocal - FVector2D(MarkerSize * 0.5f)));
					FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 6,
						MarkerGeometry, MarkerBrush, ESlateDrawEffect::None, Color);
				};
			if (NavigationPanel->HasStart())
			{
				DrawEndpoint(NavigationPanel->GetStart(), FLinearColor(0.f, 0.8f, 0.2f, 1.f));
			}
			if (NavigationPanel->HasDestination())
			{
				DrawEndpoint(NavigationPanel->GetDestination(), FLinearColor(0.9f, 0.1f, 0.1f, 1.f));
			}
			// Where the person is at the step being shown: a ring.
			if (Focus.IsSet() && Route.IsValidIndex(Focus.FirstPointIndex) && IsVisibleOnCurrentMap(Route[Focus.FirstPointIndex]))
			{
				const FVector2D Here = RouteLocal(Route[Focus.FirstPointIndex]);
				for (const TPair<float, FLinearColor>& Ring : { TPair<float, FLinearColor>(17.f, FLinearColor::Black), TPair<float, FLinearColor>(14.f, FLinearColor::White) })
				{
					TArray<FVector2D> Circle;
					for (int32 Step = 0; Step <= 24; ++Step)
					{
						const double Angle = Step * 2.0 * PI / 24;
						Circle.Add(Here + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Ring.Key);
					}
					FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 7, AllottedGeometry.ToPaintGeometry(), Circle,
						ESlateDrawEffect::None, Ring.Value, true, Ring.Key > 15.f ? 6.f : 3.f);
				}
			}
			// The stops, numbered in the order they are visited, in the colour of the leg that leads to them. Once there is a route
			// a stop is where its leg ends; before that, the place it was picked.
			const TArray<FNavigationStop>& Stops = NavigationPanel->GetStops();
			for (int32 StopIndex = 0; StopIndex < Stops.Num(); ++StopIndex)
			{
				const FNavigationRoutePoint& StopPoint = RouteLegs.IsValidIndex(StopIndex) && Route.IsValidIndex(RouteLegs[StopIndex].LastPointIndex)
					? Route[RouteLegs[StopIndex].LastPointIndex] : Stops[StopIndex].Point;
				if (!IsVisibleOnCurrentMap(StopPoint))
				{
					continue;
				}
				DrawEndpoint(StopPoint, UNavigationRouteVisualization::LegColor2D(StopIndex));
				const FVector2D TextAt = RouteLocal(StopPoint) - FVector2D(4.f, 8.f);
				FSlateDrawElement::MakeText(OutDrawElements, LayerId + 7,
					AllottedGeometry.ToPaintGeometry(FVector2f(12.f, 16.f), FSlateLayoutTransform(TextAt)),
					FText::AsNumber(StopIndex + 1), FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11),
					ESlateDrawEffect::None, FLinearColor::White);
			}
		}
	}

	if (PlayerController)
	{
		const auto* Issue = PlayerController->GetLayoutValidationSession()->GetFocusedIssue();
		if (Issue && Issue->bHasPosition && Issue->BuildingId == InteriorGen->GetLoadedBuildingId() && Issue->Floor == InteriorGen->GetActiveFloor())
		{
			const FVector2D World = (Issue->Position + Building->BuildingOffset) * Generator->GetScale();
			const FVector2D Center = ViewWorldToLocal(World, Origin, PixelsPerUnit, WidgetSize);
			TArray<FVector2D> Ring;
			for (int32 I = 0; I <= 24; ++I) { const double Angle = I * 2.0 * PI / 24; Ring.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 14.0); }
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 8, AllottedGeometry.ToPaintGeometry(), Ring, ESlateDrawEffect::None, FLinearColor::Yellow, true, 3.f);
		}
	}
	for (const FVector2D& Handle : GetResizeHandles(WidgetSize))
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 9,
			AllottedGeometry.ToPaintGeometry(FVector2f(10,10), FSlateLayoutTransform(Handle - FVector2D(5,5))),
			FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")), ESlateDrawEffect::None, FLinearColor(0.2f, 0.7f, 1.f));
	}
	// The selected room's own corners: orange-edged white dots a single corner is dragged by (Ctrl for a corner under a blue resize handle).
	if (!bIsResizingRoom)
	{
		for (const FVector2D& Vertex : GetVertexHandles(WidgetSize))
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 9,
				AllottedGeometry.ToPaintGeometry(FVector2f(12,12), FSlateLayoutTransform(Vertex - FVector2D(6,6))),
				FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")), ESlateDrawEffect::None, FLinearColor(1.f, 0.6f, 0.f));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 10,
				AllottedGeometry.ToPaintGeometry(FVector2f(7,7), FSlateLayoutTransform(Vertex - FVector2D(3.5,3.5))),
				FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")), ESlateDrawEffect::None, FLinearColor::White);
		}
	}
	TArray<FVector2D> Preview = GetPrefabPreview();
	if (bIsResizingRoom && DragTarget.IsValid())
	{
		Preview.Reserve(DragStartRoomFootprint.Num() + 1);
		for (const FVector2D& Point : DragStartRoomFootprint)
		{
			const FVector2D Offset = Point - ResizeStartPosition;
			Preview.Add(ResizePreviewPosition + FVector2D(
				FMath::IsNearlyZero(ResizeStartSize.X) ? 0.0 : Offset.X * ResizePreviewSize.X / ResizeStartSize.X,
				FMath::IsNearlyZero(ResizeStartSize.Y) ? 0.0 : Offset.Y * ResizePreviewSize.Y / ResizeStartSize.Y));
		}
	}
	if (!Preview.IsEmpty())
	{
		for (FVector2D& Point : Preview) Point = WorldToLocal((Point + Building->BuildingOffset) * Scale);
		if (!bIsResizingRoom || (DragTarget.IsValid() && DragTarget->IsClosedLoop()))
		{
			const FVector2D First = Preview[0];
			Preview.Add(First);
		}
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 9, AllottedGeometry.ToPaintGeometry(), Preview,
			ESlateDrawEffect::None, FLinearColor(1.f, 0.6f, 0.f), true, 3.f);
	}
	// The transform gizmo (PAT-202): what the keys W, E and R switch on.
	if (const FGizmoLayout Layout = ComputeGizmoLayout(WidgetSize); Layout.bValid)
	{
		const FLinearColor Red(0.95f, 0.25f, 0.25f), Green(0.3f, 0.85f, 0.35f), Blue(0.3f, 0.6f, 1.f), White(0.95f, 0.95f, 0.95f);
		const FSlateBrush* Box = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
		const auto Square = [&](const FVector2D& At, float Side, const FLinearColor& Colour)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 10, AllottedGeometry.ToPaintGeometry(FVector2f(Side, Side), FSlateLayoutTransform(At - FVector2D(Side * 0.5, Side * 0.5))),
				Box, ESlateDrawEffect::None, Colour);
		};
		const auto Line = [&](const FVector2D& From, const FVector2D& To, const FLinearColor& Colour, float Width)
		{
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 10, AllottedGeometry.ToPaintGeometry(), TArray<FVector2D>{ From, To }, ESlateDrawEffect::None, Colour, true, Width);
		};
		const auto Label = [&](const FVector2D& At, const TCHAR* Text, const FLinearColor& Colour)
		{
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 10, AllottedGeometry.ToPaintGeometry(FVector2f(14.f, 16.f), FSlateLayoutTransform(At + FVector2D(6, -18))),
				FText::FromString(Text), FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11), ESlateDrawEffect::None, Colour);
		};
		const auto Arrowhead = [&](const FVector2D& Tip, const FVector2D& Dir, const FLinearColor& Colour)
		{
			const FVector2D Side(-Dir.Y, Dir.X);
			Line(Tip, Tip - Dir * 12.0 + Side * 6.0, Colour, 3.f);
			Line(Tip, Tip - Dir * 12.0 - Side * 6.0, Colour, 3.f);
		};
		switch (PlayerController->GetTransformGizmo())
		{
		case ETransformGizmo::Translate:
			Line(Layout.Center, Layout.XTip, Red, 3.f); Arrowhead(Layout.XTip, Layout.DirX, Red); Label(Layout.XTip, TEXT("X"), Red);
			Line(Layout.Center, Layout.YTip, Green, 3.f); Arrowhead(Layout.YTip, Layout.DirY, Green); Label(Layout.YTip, TEXT("Y"), Green);
			Square(Layout.Center, 12.f, White);
			break;
		case ETransformGizmo::Rotate:
		{
			TArray<FVector2D> Ring;
			for (int32 Step = 0; Step <= 48; ++Step)
			{
				const double Angle = Step * 2.0 * PI / 48;
				Ring.Add(Layout.Center + (Layout.DirX * FMath::Cos(Angle) + Layout.DirY * FMath::Sin(Angle)) * Layout.RingRadius);
			}
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 10, AllottedGeometry.ToPaintGeometry(), Ring, ESlateDrawEffect::None, Blue, true, 3.f);
			Square(Layout.Center + Layout.DirX * Layout.RingRadius, 10.f, Blue);
			Label(Layout.Center + Layout.DirX * Layout.RingRadius, TEXT("turn"), Blue);
			break;
		}
		case ETransformGizmo::Scale:
			if (Layout.bRoom)
			{
				Line(Layout.Center, Layout.ScaleX, Red, 2.f); Square(Layout.ScaleX, 12.f, Red); Label(Layout.ScaleX, TEXT("X"), Red);
				Line(Layout.Center, Layout.ScaleY, Green, 2.f); Square(Layout.ScaleY, 12.f, Green); Label(Layout.ScaleY, TEXT("Y"), Green);
			}
			Line(Layout.Center, Layout.ScaleUniform, White, 2.f);
			Square(Layout.ScaleUniform, 12.f, White);
			break;
		default:
			break;
		}
	}

	// The selection window being dragged: a faint blue fill with a blue rim.
	if (bIsDraggingSelectBox)
	{
		const FVector2D Min(FMath::Min(SelectBoxStart.X, SelectBoxEnd.X), FMath::Min(SelectBoxStart.Y, SelectBoxEnd.Y));
		const FVector2D Max(FMath::Max(SelectBoxStart.X, SelectBoxEnd.X), FMath::Max(SelectBoxStart.Y, SelectBoxEnd.Y));
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 10,
			AllottedGeometry.ToPaintGeometry(FVector2f(Max - Min), FSlateLayoutTransform(Min)),
			FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")), ESlateDrawEffect::None, FLinearColor(0.2f, 0.6f, 1.f, 0.15f));
		const TArray<FVector2D> Rim = { Min, FVector2D(Max.X, Min.Y), Max, FVector2D(Min.X, Max.Y), Min };
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 10, AllottedGeometry.ToPaintGeometry(), Rim, ESlateDrawEffect::None, FLinearColor(0.2f, 0.6f, 1.f, 0.9f), true, 1.5f);
	}
	if (bImageOnTop) DrawImage(LayerId + 11);
	return LayerId + (bImageOnTop ? 12 : 11);
}
