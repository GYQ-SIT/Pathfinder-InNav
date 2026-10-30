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

	// The view: how far in the person has zoomed (1 = the plan fits) and how far they moved it (design pixels). Zoom is kept within the prototype's
	// 0.7 to 2, and the plan cannot be moved out of sight. A different level, the overview, or the tilt starts again at the fit (ResetView).
	static constexpr double MinZoom = 0.7;
	static constexpr double MaxZoom = 2.0;
	static constexpr double ZoomStep = 0.2;
	double GetZoom() const { return Zoom; }
	const FVector2D& GetPan() const { return Pan; }
	void ResetView();
	// Dragging and pinching move the view only when the person has turned gestures on (the prototype's "Move" switch); the zoom buttons always work.
	void SetGesturesEnabled(bool bEnabled) { bGestures = bEnabled; }
	bool AreGesturesEnabled() const { return bGestures; }
	// The size the map is drawn at (design pixels); it learns it when it is drawn or touched, tests say it.
	void SetViewSize(double Width, double Height) { ViewSize = FVector2D(Width, Height); }
	// Zooms by a step about the middle of the uncovered part (the buttons), or by a factor about a point (a pinch, the wheel).
	void ZoomBy(double Delta);
	void ZoomAround(const FVector2D& Local, double Factor);
	void PanBy(const FVector2D& Delta);

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

	// What a press, a move and a release of one pointer (a finger or the mouse) do: a tap picks a room; a drag moves the view and two fingers
	// zoom it, when gestures are on. Only pointers that pressed on the map are followed (an emulator also reports the mouse cursor's moves with
	// a touch). The mouse and touch events below call these; tests call them with made-up events.
	FReply HandlePointerDown(const FGeometry& Geometry, const FPointerEvent& Event);
	FReply HandlePointerMove(const FGeometry& Geometry, const FPointerEvent& Event);
	FReply HandlePointerUp(const FGeometry& Geometry, const FPointerEvent& Event);
	FReply HandleWheel(const FGeometry& Geometry, const FPointerEvent& Event);

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	// Keeps some of the plan in the uncovered part of the widget after a move or a zoom.
	void ClampPan();
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
	double Zoom = 1.0;
	FVector2D Pan = FVector2D::ZeroVector;
	bool bGestures = false;
	mutable FVector2D ViewSize = FVector2D(375.0, 812.0);

	// The pointers that are down (a finger or the mouse button), where they are, and what the press so far is: a tap or a drag.
	TMap<int32, FVector2D> Pointers;
	FVector2D PressLocal = FVector2D::ZeroVector;
	bool bPressMoved = false;
	bool bMultiTouch = false;

	TFunction<void(const FString&)> OnPlaceTapped;
	TFunction<void(const FString&)> OnBuildingTapped;
};
