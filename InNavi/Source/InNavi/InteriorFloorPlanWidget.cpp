// Fill out your copyright notice in the Description page of Project Settings.


#include "InteriorFloorPlanWidget.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "BuildingGenerator.h"
#include "PolyBuilding.h"
#include "NavPlayerController.h"
#include "EngineUtils.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

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

		if (PlayerController->IsInSelectMode())
		{
			AInteriorRoom* Room = FindRoomNearLocalPosition(LocalPosition, InGeometry.GetLocalSize());
			PlayerController->SelectRoom(Room ? Room->RoomId : FString());
			return FReply::Handled();
		}

		if (PlayerController->IsInPrefabRoomMode())
		{
			const FVector2D FootprintCenter = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
			PlayerController->PlacePrefabRoomAt(FootprintCenter);
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

	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UInteriorFloorPlanWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
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

AInteriorRoom* UInteriorFloorPlanWidget::FindRoomNearLocalPosition(FVector2D LocalPosition, FVector2D WidgetSize) const
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

		TArray<FVector2D> LocalPoints;
		LocalPoints.Reserve(N);
		for (const FVector2D& Point : Footprint)
		{
			LocalPoints.Add(WorldToLocal((Point + Building->BuildingOffset) * Scale));
		}

		const int32 NumEdges = Room->IsClosedLoop() ? N : N - 1;
		for (int32 i = 0; i < NumEdges; i++)
		{
			const FVector2D& A = LocalPoints[i];
			const FVector2D& B = LocalPoints[(i + 1) % N];
			if (DistancePointToSegment2D(LocalPosition, A, B) <= ClickTolerancePixels)
			{
				return Room;
			}
		}
	}

	return nullptr;
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

	// ---- Building boundary ----
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

		TArray<FVector2D> LinePoints;
		for (const FVector2D& Point : Room->GetFootprint())
		{
			LinePoints.Add(WorldToLocal((Point + Building->BuildingOffset) * Scale));
		}
		if (LinePoints.Num() < 2)
		{
			continue;
		}

		if (Room->IsClosedLoop())
		{
			const FVector2D FirstPoint = LinePoints[0];
			LinePoints.Add(FirstPoint);
		}

		const bool bIsSelected = !SelectedRoomId.IsEmpty() && Room->RoomId == SelectedRoomId;
		const FLinearColor LineColor = bIsSelected ? FLinearColor(1.f, 0.15f, 0.15f, 1.f) : FLinearColor::Black;
		const float LineThickness = bIsSelected ? 4.f : 3.f;
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::None, LineColor, true, LineThickness);
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

	return LayerId + 4;
}
