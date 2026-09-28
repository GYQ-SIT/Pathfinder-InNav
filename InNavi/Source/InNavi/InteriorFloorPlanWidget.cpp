// Fill out your copyright notice in the Description page of Project Settings.


#include "InteriorFloorPlanWidget.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "InteriorTypes.h"
#include "BuildingGenerator.h"
#include "PolyBuilding.h"
#include "NavPlayerController.h"
#include "NavigationPanelWidget.h"
#include "PolygonGeometryUtils.h"
#include "EngineUtils.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Engine/Texture2D.h"

void UInteriorFloorPlanWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AInteriorGenerator> It(World); It; ++It)
		{
			InteriorGen = *It;
			break;
		}
		for (TActorIterator<ABuildingGenerator> It(World); It; ++It)
		{
			Generator = *It;
			break;
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
	LoadConnectorIcon(TEXT("/Game/ImageAssets/Icon_AddElevator.Icon_AddElevator"), ElevatorIconTexture, ElevatorIconBrush);
	LoadConnectorIcon(TEXT("/Game/ImageAssets/Icon_AddStairs.Icon_AddStairs"), StairsIconTexture, StairsIconBrush);
	LoadConnectorIcon(TEXT("/Game/ImageAssets/Icon_AddEscalator.Icon_AddEscalator"), EscalatorIconTexture, EscalatorIconBrush);

	UE_LOG(LogTemp, Warning,
		TEXT("[Navigation] Floor-plan constructed: PlayerController=%s Generator=%s Interior=%s BuildingId=%s Floor=%d"),
		PlayerController ? TEXT("valid") : TEXT("NULL"),
		Generator ? TEXT("valid") : TEXT("NULL"),
		InteriorGen ? TEXT("valid") : TEXT("NULL"),
		InteriorGen ? *InteriorGen->GetLoadedBuildingId() : TEXT("<none>"),
		InteriorGen ? InteriorGen->GetActiveFloor() : INDEX_NONE);
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
			if (ActiveFloor < 1)
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
			FString DoorRoomId;
			FString DoorId;
			int32 DoorEdgeIndex = INDEX_NONE;
			if (FindDoorNearLocalPosition(LocalPosition, InGeometry.GetLocalSize(), DoorRoomId, DoorId, &DoorEdgeIndex))
			{
				PlayerController->SelectDoor(DoorRoomId, DoorId);

				// A plain click (mouse up without moving) has no further effect; dragging slides
				// the door along DraggingDoorEdgeIndex - see NativeOnMouseMove.
				bIsDraggingDoor = true;
				DraggingDoorRoomId = DoorRoomId;
				DraggingDoorId = DoorId;
				DraggingDoorEdgeIndex = DoorEdgeIndex;
				return FReply::Handled().CaptureMouse(TakeWidget());
			}

			FString ConnectorGroupId;
			if (FindConnectorNearLocalPosition(LocalPosition, InGeometry.GetLocalSize(), ConnectorGroupId))
			{
				PlayerController->SelectConnector(ConnectorGroupId);
				return FReply::Handled();
			}

			AInteriorRoom* Room = FindRoomNearLocalPosition(LocalPosition, InGeometry.GetLocalSize());
			if (!Room)
			{
				Room = FindRoomContainingLocalPosition(LocalPosition, InGeometry.GetLocalSize());
			}

			if (Room)
			{
				PlayerController->SelectRoom(Room->RoomId);

				// Same idea as the door drag above - a plain click just selects; dragging slides the
				// whole room via MoveRoomTo, computed fresh each move from this snapshot (see
				// NativeOnMouseMove) rather than accumulated deltas, so it doesn't drift.
				bIsDraggingRoom = true;
				DraggingRoomId = Room->RoomId;
				DragStartRoomFootprint = Room->GetFootprint();
				DragStartFootprintPosition = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
				return FReply::Handled().CaptureMouse(TakeWidget());
			}

			PlayerController->SelectRoom(FString());
			return FReply::Handled();
		}

		if (PlayerController->IsInAddDoorMode())
		{
			int32 EdgeIndex = INDEX_NONE;
			float OffsetWorldUnits = 0.f;
			if (AInteriorRoom* Room = FindRoomNearLocalPosition(LocalPosition, InGeometry.GetLocalSize(), &EdgeIndex, &OffsetWorldUnits))
			{
				PlayerController->AddDoorToWall(Room->RoomId, EdgeIndex, OffsetWorldUnits);
			}
			return FReply::Handled();
		}

		if (PlayerController->IsInPrefabRoomMode())
		{
			const FVector2D FootprintCenter = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
			PlayerController->PlacePrefabRoomAt(FootprintCenter);
			return FReply::Handled();
		}

		if (PlayerController->IsInConnectorMode())
		{
			const FVector2D FootprintPosition = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
			PlayerController->PlaceConnectorAt(FootprintPosition);
			return FReply::Handled();
		}

		const FVector2D FootprintPoint = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
		PlayerController->AddRoomPointAtFootprintPosition(FootprintPoint);
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UInteriorFloorPlanWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton && bIsPanning)
	{
		bIsPanning = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDraggingDoor)
	{
		bIsDraggingDoor = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDraggingRoom)
	{
		bIsDraggingRoom = false;
		if (PlayerController)
		{
			// Overlap with another room/wall was allowed while dragging (see NativeOnMouseMove) -
			// this is what actually enforces it, reverting to DragStartRoomFootprint if needed.
			PlayerController->CommitRoomMove(DraggingRoomId, DragStartRoomFootprint);
		}
		return FReply::Handled().ReleaseMouseCapture();
	}

	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UInteriorFloorPlanWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsDraggingDoor)
	{
		// Same defensive check as panning below - a mouse-up that doesn't reach this widget
		// shouldn't leave the drag stuck on forever.
		if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
		{
			bIsDraggingDoor = false;
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
		if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
		{
			bIsDraggingRoom = false;
			return FReply::Handled().ReleaseMouseCapture();
		}

		const FVector2D CurrentLocalPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D CurrentFootprintPosition = LocalPositionToFootprintPoint(CurrentLocalPosition, InGeometry.GetLocalSize());
		const FVector2D Delta = CurrentFootprintPosition - DragStartFootprintPosition;

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
			// tolerance/approach as P-mode's next click (see AddRoomPointAtFootprintPosition).
			constexpr float SnapToleranceFootprintUnits = 8.f;
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
			PanOffset = PanStartOffset - DeltaPixels / PixelsPerUnit;
		}
		return FReply::Handled();
	}

	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UInteriorFloorPlanWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
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
			return WidgetSize * 0.5f + (WorldPoint - Origin) * PixelsPerUnit;
		};

	constexpr float ClickTolerancePixels = 10.f;
	constexpr float DoorWidthWorldUnits = 90.f; // matches AInteriorGenerator::AddDoorToRoom's fixed door width

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

			if (DistancePointToSegment2D(LocalPosition, A, B) <= ClickTolerancePixels)
			{
				if (OutEdgeIndex)
				{
					*OutEdgeIndex = i;
				}
				if (OutOffsetWorldUnits)
				{
					const FVector2D LocalAB = B - A;
					const float LocalLengthSq = LocalAB.SizeSquared();
					const float T = (LocalLengthSq > KINDA_SMALL_NUMBER) ? FMath::Clamp(FVector2D::DotProduct(LocalPosition - A, LocalAB) / LocalLengthSq, 0.f, 1.f) : 0.f;
					const float EdgeLengthWorldUnits = FVector2D::Distance(WorldA, WorldB);
					const float ClickOffsetWorldUnits = T * EdgeLengthWorldUnits;
					*OutOffsetWorldUnits = FMath::Clamp(ClickOffsetWorldUnits - DoorWidthWorldUnits * 0.5f, 0.f, FMath::Max(0.f, EdgeLengthWorldUnits - DoorWidthWorldUnits));
				}
				return Room;
			}
		}
	}

	return nullptr;
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
			return WidgetSize * 0.5f + (WorldPoint - Origin) * PixelsPerUnit;
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
			return WidgetSize * 0.5f + (WorldPoint - Origin) * PixelsPerUnit;
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
			return WidgetSize * 0.5f + (WorldPoint - Origin) * PixelsPerUnit;
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
			return WidgetSize * 0.5f + (WorldPoint - Origin) * PixelsPerUnit;
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

	const float ScaleX = (BoundingWidth > KINDA_SMALL_NUMBER) ? (WidgetSize.X * Margin) / BoundingWidth : TNumericLimits<float>::Max();
	const float ScaleY = (BoundingHeight > KINDA_SMALL_NUMBER) ? (WidgetSize.Y * Margin) / BoundingHeight : TNumericLimits<float>::Max();
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

	const FVector2D WorldPoint = Origin + (LocalPosition - WidgetSize * 0.5f) / PixelsPerUnit;

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
			return WidgetSize * 0.5f + (WorldPoint - Origin) * PixelsPerUnit;
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
		TriangulatePolygon(Building->GetFootprint(), TriangleIndices);
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

	// ---- Rooms/walls on the active floor ----
	const FString SelectedRoomId = PlayerController ? PlayerController->GetSelectedRoomId() : FString();
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

		const bool bIsSelected = !SelectedRoomId.IsEmpty() && Room->RoomId == SelectedRoomId;
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
					const FLinearColor DoorColor = bIsSelectedDoor ? FLinearColor(1.f, 0.15f, 0.15f, 1.f) : FLinearColor(0.f, 0.85f, 1.f, 1.f); // fluorescent blue, or red when selected
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
				FMath::DegreesToRadians(Node.RotationDegrees), FVector2f(ScaledConnectorSize, ScaledConnectorSize) * 0.5f,
				FSlateDrawElement::RelativeToElement, NodeColor);
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
		if (const UNavigationPanelWidget* NavigationPanel = PlayerController->GetNavigationPanel())
		{
			const int32 ActiveFloor = InteriorGen->GetActiveFloor();
			auto IsVisibleOnCurrentMap = [ActiveFloor](const FNavigationRoutePoint& Point)
				{
					return Point.Floor == ActiveFloor
						|| (ActiveFloor == 1 && Point.Floor == UNavigationPanelWidget::OutdoorFloor);
				};
			const TArray<FNavigationRoutePoint>& Route = NavigationPanel->GetRoutePoints();
			for (int32 I = 1; I < Route.Num(); ++I)
			{
				if (!IsVisibleOnCurrentMap(Route[I - 1]) || !IsVisibleOnCurrentMap(Route[I]))
				{
					continue;
				}
				TArray<FVector2D> Segment;
				Segment.Add(WorldToLocal((Route[I - 1].Position + Building->BuildingOffset) * Scale));
				Segment.Add(WorldToLocal((Route[I].Position + Building->BuildingOffset) * Scale));
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 4,
					AllottedGeometry.ToPaintGeometry(), Segment, ESlateDrawEffect::None,
					FLinearColor(1.f, 0.55f, 0.f, 1.f), true, 6.f);
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
					const FVector2D MarkerLocal = WorldToLocal((Point.Position + Building->BuildingOffset) * Scale);
					if (Point.Floor == UNavigationPanelWidget::OutdoorFloor)
					{
						TArray<FVector2D> Diamond;
						Diamond.Add(MarkerLocal + FVector2D(0.f, -OutlineSize * 0.5f));
						Diamond.Add(MarkerLocal + FVector2D(OutlineSize * 0.5f, 0.f));
						Diamond.Add(MarkerLocal + FVector2D(0.f, OutlineSize * 0.5f));
						Diamond.Add(MarkerLocal + FVector2D(-OutlineSize * 0.5f, 0.f));
						Diamond.Add(Diamond[0]);
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
		}
	}

	return LayerId + 7;
}
