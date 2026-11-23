#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileNavSource.h"
#include "MobileLocationSource.h"
#include "MobileMapWidget.generated.h"

// The phone app's map: one level of a building as a floor plan (its rooms coloured by what they are, named, with the lifts and stairs), or the
// site from above (Overview). It can draw the route on the level it shows, a pin on the destination, a dot where the person is, and a tilted
// 3D-looking view of the same plan. A tap on a room or (in the overview) a building is reported. It asks an IMobileNavSource for what to draw.
//
// The map fits itself into the part of the widget that is not covered (below the top bar, above the bottom sheet): SetFitInsets says how much
// of the top and bottom is covered, in design pixels. A building that is small enough is shown whole; a big one starts at about 100 metres across the
// screen (MobileMapScale), around the person, the place looked at or the route, and the whole plan is a pinch away. Icons and names follow the scale.
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
	// The stops on the way of a trip with several destinations (not the last, which the route's end shows): each is drawn as a blue circle with its number (1, 2, ...).
	void SetStops(const TArray<FMobileRoutePoint>& InStops);
	int32 GetStopCount() const { return Stops.Num(); }
	int32 GetStopsOnLevel() const;
	// The place looked at is drawn highlighted; if it is out of sight on a map that starts in the middle of a big building, the view moves to it.
	void SetHighlightPlace(const FString& PlaceId);
	void SetPosition(const FMobileRoutePoint& InPosition);
	void ClearPosition();
	void SetGpsPosition(TOptional<FVector2D> InPosition, double AccuracyMeters = 0.0, EMobileGpsStatus Status = EMobileGpsStatus::Hidden);
	EMobileGpsStatus GetGpsStatus() const { return GpsStatus; }
	double GetGpsAccuracyMeters() const { return GpsAccuracyMeters; }
	const TOptional<FVector2D>& GetGpsPosition() const { return GpsPosition; }
	void CenterOnGps();
	void Set3D(bool bInShow3D);
	bool IsShowing3D() const { return bShow3D; }
	void SetFitInsets(double Top, double Bottom);

	// The view: how far in the person has zoomed (1 = where the map starts: the plan whole, or 100 metres across for a big building) and how far they moved
	// it (design pixels). Zoom is kept between 0.7 of the whole plan (GetMinZoom: lower for a big building) and 6 (the prototype stopped at 2, which left a
	// room small), and the plan cannot be moved out of sight. The overview or the tilt starts again at the start (ResetView); another level of the same
	// building keeps the view.
	static constexpr double MinZoom = 0.7;
	double GetMinZoom() const;
	static constexpr double MaxZoom = 6.0;
	static constexpr double ZoomStep = 0.2;
	double GetZoom() const { return Zoom; }
	const FVector2D& GetPan() const { return Pan; }
	void ResetView();

	// The turn of the whole map, in radians, clockwise as the person sees it (0 = as the plan is drawn). Two fingers twisting turn it, and with a mouse
	// Shift and a drag; the room names and the icons stay upright. It is the person's own bearing, so another level, the overview or the tilt keep it;
	// ResetRotation (the compass button) puts it back.
	double GetRotation() const { return Rotation; }
	void SetRotation(double Radians);
	void ResetRotation();
	// Turns by DeltaRadians about a point of the widget: what is under that point stays under it (in the flat view).
	void RotateAround(const FVector2D& Local, double DeltaRadians);
	// Told the new turn each time it changes, for the compass button.
	void SetOnRotationChanged(TFunction<void(double)> InCallback) { OnRotationChanged = MoveTemp(InCallback); }
	// Dragging and pinching move the view only when the person has turned gestures on (the prototype's "Move" switch); the zoom buttons always work.
	void SetGesturesEnabled(bool bEnabled) { bGestures = bEnabled; }
	bool AreGesturesEnabled() const { return bGestures; }
	// The size the map is drawn at (design pixels); it learns it when it is drawn or touched, tests say it.
	void SetViewSize(double Width, double Height) { ViewSize = FVector2D(Width, Height); }
	// Where the view is built around on a big building (the middle of the route, the place looked at, the person, the way in, the plan's middle), and
	// how many of the site's other buildings are drawn round the level (for the tests).
	FVector2D FocusPoint() const;
	int32 GetContextBuildingCount() const;
	// What a test shows without a source: the level (and the site's other buildings round it).
	void SetMapsForTest(const FMobileLevelMap& Level, const FMobileCampusMap& Campus);
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
	// The plan's own units to the flat or tilted drawing space, and the fit of that space into the widget. The fit is worked out from the plan as it is
	// drawn unturned (PlanBounds uses ProjectFlat), so turning the map does not zoom it; the drawing turns the plan about its own middle first (Turn).
	FVector2D ProjectFlat(const FVector2D& PlanPoint) const;
	FVector2D Turn(const FVector2D& PlanPoint, const FVector2D& About) const;
	FBox2D RawPlanBounds() const;
	FBox2D PlanBounds() const;
	// The middle of the uncovered part of the widget: what zooming and turning are about.
	FVector2D ViewCentre() const;
	void ComputeFit(double WidgetWidth, double WidgetHeight, double& OutScale, FVector2D& OutOffset) const;
	// The scale that fits the whole plan, and the one the map starts at (the larger: see MobileMapScale::StartScale), with the part of the widget the plan has.
	struct FStart
	{
		bool bValid = false;
		double Fit = 1.0;
		double Start = 1.0;
		double AvailW = 0.0;
		double AvailH = 0.0;
		FBox2D Bounds = FBox2D(ForceInit);
	};
	FStart WorkOutStart(double WidgetWidth, double WidgetHeight) const;
	// The route's points on the level shown, as a box of the plan; false with fewer than two.
	bool RouteBoxOnLevel(FBox2D& OutBox) const;
	TArray<FVector2D> ToWidgetPolygon(const TArray<FVector2D>& Plan, double WidgetWidth, double WidgetHeight, double Lift) const;

	IMobileNavSource* Source = nullptr;
	FString SiteId;
	bool bOverview = false;
	FMobileLevelMap LevelMap;
	FMobileCampusMap CampusMap;
	FMobileRoute Route;
	TArray<FMobileRoutePoint> Stops;
	FString HighlightPlaceId;
	FMobileRoutePoint Position;
	bool bHasPosition = false;
	TOptional<FVector2D> GpsPosition;
	double GpsAccuracyMeters = 0.0;
	EMobileGpsStatus GpsStatus = EMobileGpsStatus::Hidden;
	bool bShow3D = false;
	// Where a big building's view is built around, kept once chosen (so a tap or a drag does not move it), until the view starts again or the route changes.
	mutable TOptional<FVector2D> LatchedFocus;
	double InsetTop = 64.0;
	double InsetBottom = 300.0;
	double Zoom = 1.0;
	FVector2D Pan = FVector2D::ZeroVector;
	double Rotation = 0.0;
	// Two fingers twisting turn the map only once they have twisted past a few degrees, so a pinch that wobbles does not.
	double TwistSoFar = 0.0;
	bool bTwisting = false;
	TFunction<void(double)> OnRotationChanged;
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
