#include "FootprintPreviewWidget.h"

#include "Rendering/DrawElements.h"

void SFootprintPreview::SetPoints(const TArray<FVector2D>& InPoints)
{
	Points = InPoints;
	Invalidate(EInvalidateWidgetReason::Paint);
}

bool SFootprintPreview::FitOutline(const TArray<FVector2D>& InPoints, const FVector2D& Size, TArray<FVector2f>& OutLine)
{
	OutLine.Reset();
	if (InPoints.Num() < 3)
	{
		return false;
	}
	FVector2D Min(DBL_MAX, DBL_MAX), Max(-DBL_MAX, -DBL_MAX);
	for (const FVector2D& Point : InPoints)
	{
		Min = FVector2D(FMath::Min(Min.X, Point.X), FMath::Min(Min.Y, Point.Y));
		Max = FVector2D(FMath::Max(Max.X, Point.X), FMath::Max(Max.Y, Point.Y));
	}
	const FVector2D Extent = Max - Min;
	if (Size.X <= 4.0 || Size.Y <= 4.0 || (Extent.X <= KINDA_SMALL_NUMBER && Extent.Y <= KINDA_SMALL_NUMBER))
	{
		return false;
	}

	// Fit the outline into the space with a margin, keeping its proportions and centring it.
	const double Margin = 8.0;
	const double Scale = FMath::Min((Size.X - 2.0 * Margin) / FMath::Max(Extent.X, 1e-6), (Size.Y - 2.0 * Margin) / FMath::Max(Extent.Y, 1e-6));
	const FVector2D Offset = (Size - Extent * Scale) * 0.5;
	OutLine.Reserve(InPoints.Num() + 1);
	for (const FVector2D& Point : InPoints)
	{
		const FVector2D Local = (Point - Min) * Scale + Offset;
		OutLine.Add(FVector2f(static_cast<float>(Local.X), static_cast<float>(Local.Y)));
	}
	// Close the outline. (A copy: adding an element of the array to itself is not allowed.)
	const FVector2f First = OutLine[0];
	OutLine.Add(First);
	return true;
}

int32 SFootprintPreview::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	TArray<FVector2f> Line;
	if (!FitOutline(Points, AllottedGeometry.GetLocalSize(), Line))
	{
		return LayerId;
	}
	const FLinearColor Color = FLinearColor::FromSRGBColor(FColor(150, 190, 245));
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, Color, true, 2.0f);
	return LayerId + 1;
}

void UFootprintPreviewWidget::SetFootprint(const TArray<FVector2D>& InPoints)
{
	Points = InPoints;
	if (Preview.IsValid())
	{
		Preview->SetPoints(Points);
	}
}

TSharedRef<SWidget> UFootprintPreviewWidget::RebuildWidget()
{
	Preview = SNew(SFootprintPreview);
	Preview->SetPoints(Points);
	return Preview.ToSharedRef();
}

void UFootprintPreviewWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (Preview.IsValid())
	{
		Preview->SetPoints(Points);
	}
}

void UFootprintPreviewWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Preview.Reset();
}
