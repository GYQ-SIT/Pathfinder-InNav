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
	if (PlayerController && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2D LocalPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D FootprintPoint = LocalPositionToFootprintPoint(LocalPosition, InGeometry.GetLocalSize());
		PlayerController->AddRoomPointAtFootprintPosition(FootprintPoint);
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
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

	OutOrigin = (MinPoint + MaxPoint) * 0.5f;

	const float Span = FMath::Max(MaxPoint.X - MinPoint.X, MaxPoint.Y - MinPoint.Y);
	const float SmallerDimension = FMath::Min(WidgetSize.X, WidgetSize.Y);
	const float Margin = 0.85f; // leave ~15% breathing room around the footprint

	OutPixelsPerUnit = (Span > KINDA_SMALL_NUMBER) ? (SmallerDimension * Margin) / Span : 1.f;
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
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::None, FLinearColor(0.5f, 0.5f, 0.5f, 1.f), true, 2.f);
		}
	}

	// ---- Rooms/walls on the active floor ----
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
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::None, FLinearColor::White, true, 3.f);
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
