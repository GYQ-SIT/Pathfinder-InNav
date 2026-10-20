#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Widgets/SLeafWidget.h"
#include "FootprintPreviewWidget.generated.h"

// Draws a building's outline (its footprint) as a line drawing that fits the space it is given - the picture on the
// Building panel.
class SFootprintPreview : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFootprintPreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) {}
	void SetPoints(const TArray<FVector2D>& InPoints);

	// The outline fitted into a space of this size, as a closed line (the first point repeated at the end), keeping its
	// proportions and centred with a margin. False if there is nothing to draw (fewer than three points, no size, no extent).
	static bool FitOutline(const TArray<FVector2D>& InPoints, const FVector2D& Size, TArray<FVector2f>& OutLine);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D(120.0, 90.0); }

private:
	TArray<FVector2D> Points;
};

UCLASS()
class INNAVI_API UFootprintPreviewWidget : public UWidget
{
	GENERATED_BODY()

public:
	// The outline to draw, in footprint units (any scale; it is fitted). Fewer than three points draws nothing.
	void SetFootprint(const TArray<FVector2D>& InPoints);
	int32 GetPointCountForTests() const { return Points.Num(); }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TArray<FVector2D> Points;
	TSharedPtr<SFootprintPreview> Preview;
};
