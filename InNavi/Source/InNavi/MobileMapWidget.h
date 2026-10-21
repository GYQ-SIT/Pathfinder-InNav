#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileNavSource.h"
#include "MobileMapWidget.generated.h"

// The phone app's map: one level of a building as a floor plan (its rooms coloured by what they are, named, with the lifts and stairs), or the
// site from above (Overview). It can draw the route on the level it shows, a pin on the destination, a dot where the person is, and a tilted
// 3D-looking view of the same plan. A tap on a room or (in the overview) a building is reported. It asks an IMobileNavSource for what to draw.
//
// The map fits itself into the part of the widget that is not covered (below the top bar, above the bottom sheet): SetFitInsets says how much
// of the top and bottom is covered, in design pixels.
UCLASS()
class INNAVI_API UMobileMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMobileMapWidget(const FObjectInitializer& ObjectInitializer);

	void SetSource(IMobileNavSource* InSource, const FString& InSiteId);

	// What is shown: a level of a building, or the overview of the site.
	void ShowLevel(const FString& BuildingId, int32 Level);
	void ShowOverview();
	bool IsOverview() const { return bOverview; }
	const FString& GetBuildingId() const { return LevelMap.BuildingId; }
	int32 GetLevel() const { return LevelMap.Level; }
	bool HasLevelMap() const { return !LevelMap.Outline.IsEmpty(); }

	// What is drawn on it.
	void SetRoute(const FMobileRoute& InRoute);
	void ClearRoute();
	void SetHighlightPlace(const FString& PlaceId);
	void SetPosition(const FMobileRoutePoint& InPosition);
	void ClearPosition();
	void Set3D(bool bInShow3D);
	bool IsShowing3D() const { return bShow3D; }
	void SetFitInsets(double Top, double Bottom);

	void SetOnPlaceTapped(TFunction<void(const FString&)> InCallback) { OnPlaceTapped = MoveTemp(InCallback); }
	void SetOnBuildingTapped(TFunction<void(const FString&)> InCallback) { OnBuildingTapped = MoveTemp(InCallback); }

	// A tap at a point of the widget (design pixels): the place of the room there, or the building on the overview. Empty if there is none.
	FString PlaceAt(const FVector2D& LocalPosition, double WidgetWidth, double WidgetHeight) const;
	FString BuildingAt(const FVector2D& LocalPosition, double WidgetWidth, double WidgetHeight) const;
	void Tap(const FVector2D& LocalPosition, double WidgetWidth, double WidgetHeight);

	// For the tests: how much of the route is on the level shown, and where things are.
	int32 GetRoutePointsOnLevel() const;
	const FString& GetHighlightPlace() const { return HighlightPlaceId; }
	bool HasPosition() const { return bHasPosition; }
	// Where a point of the plan falls in the widget (design pixels).
	FVector2D ToWidget(const FVector2D& PlanPoint, double WidgetWidth, double WidgetHeight) const;
	// The extent the plan fills in the widget, to check it stays inside the uncovered part.
	FBox2D GetPlanExtentInWidget(double WidgetWidth, double WidgetHeight) const;

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;

private:
	// The plan's own units to the flat or tilted drawing space, and the fit of that space into the widget.
	FVector2D Project(const FVector2D& PlanPoint) const;
	FBox2D PlanBounds() const;
	void ComputeFit(double WidgetWidth, double WidgetHeight, double& OutScale, FVector2D& OutOffset) const;
	TArray<FVector2D> ToWidgetPolygon(const TArray<FVector2D>& Plan, double WidgetWidth, double WidgetHeight, double Lift) const;

	IMobileNavSource* Source = nullptr;
	FString SiteId;
	bool bOverview = false;
	FMobileLevelMap LevelMap;
	FMobileCampusMap CampusMap;
	FMobileRoute Route;
	FString HighlightPlaceId;
	FMobileRoutePoint Position;
	bool bHasPosition = false;
	bool bShow3D = false;
	double InsetTop = 64.0;
	double InsetBottom = 300.0;

	TFunction<void(const FString&)> OnPlaceTapped;
	TFunction<void(const FString&)> OnBuildingTapped;
};
