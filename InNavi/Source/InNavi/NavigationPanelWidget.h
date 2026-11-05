#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NavigationRoutePlanner.h"
#include "Containers/Ticker.h"
#include "NavigationPanelWidget.generated.h"

class ABuildingGenerator;
class AInteriorGenerator;
class APolyBuilding;
class UBorder;
class UWidget;

UENUM(BlueprintType)
enum class ENavigationSelectionMode : uint8
{
	None,
	Start,
	Destination,
	// Clicking places adds them as stops, one after another, until the mode is ended.
	Stop
};

enum class ENavigationMessageKind : uint8
{
	Instruction,
	Success,
	Error,
	Warning,
	Route
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNavigationStateChanged);

// Person A supplies absolute Unreal world coordinates in centimetres, ordered start to end.
// Floor 0 is outside; floor changes retain connector metadata for the UI's instructions.
USTRUCT(BlueprintType)
struct INNAVI_API FNavigationWorldRoutePoint
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	FVector WorldPosition = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	int32 Floor = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	FString ConnectorId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	EInteriorNavNodeType ConnectorType = EInteriorNavNodeType::PathNode;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	FString BuildingId;
};

// Navigation selection/route-planning state and logic. No UI of its own any more - the shared
// Navigation dock panel (UNavigationDockPanelWidget), on the interior editor and the exterior
// screen, is the presentation layer; this class stays alive as a UUserWidget owned/added-to-viewport
// by ANavPlayerController purely so its existing PC-side lifecycle (create/AddToViewport/visibility
// toggles) doesn't need to change. Outdoor mode (SetOutdoorMode) is on while the exterior screen
// is the one shown: the room list is then each building's main door and clicks pick ground points.
UCLASS()
class INNAVI_API UNavigationPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Route points on floor zero represent the outside/campus part of a journey. Floors one and
	// above remain indoor building floors. This is the UI contract Person A's combined planner can
	// return without adding an Unreal-reflected enum to the existing route data structure.
	static constexpr int32 OutdoorFloor = 0;

	UFUNCTION(BlueprintPure, Category = "Navigation")
	ENavigationSelectionMode GetSelectionMode() const { return SelectionMode; }

	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void CancelSelection();
	void ClearAll();
	void SetNavigationContext(ABuildingGenerator* InGenerator, AInteriorGenerator* InInterior);
	void NotifyViewedBuildingChanged();
	UFUNCTION()
	void NotifyInteriorChanged();
	void SelectPoint(FVector2D FootprintPosition, int32 Floor);
	bool IsSelectingPoint() const { return SelectionMode != ENavigationSelectionMode::None; }

	bool HasStart() const { return Start.bValid; }
	bool HasDestination() const { return Destination.bValid; }
	const FNavigationRoutePoint& GetStart() const { return Start.Point; }
	const FNavigationRoutePoint& GetDestination() const { return Destination.Point; }
	const TArray<FNavigationRoutePoint>& GetRoutePoints() const { return RoutePoints; }

	// Stops: places the route has to pass, in order, between the Start and the Destination. A stop can be in any building, or a
	// main door outside; the room dropdowns are where they are picked.
	static constexpr int32 MaxRouteStops = 8;
	const TArray<FNavigationStop>& GetStops() const { return Stops; }
	// Once a route is found, the stretches of it between the Start, the stops and the Destination (one when there are no stops).
	const TArray<FNavigationRouteLeg>& GetRouteLegs() const { return RouteLegs; }
	// The stop as the lists show it: the building, then the place ("Beta · Lab 3 (Floor 2)"; a main door names its building itself).
	FString GetStopLabel(int32 StopIndex) const;
	// Adds the place the dropdown entry names (FNavigationRoomOption::GetDisplayLabel) as the last stop. False, with a message, when
	// there is no such place, it cannot be used as a point, the limit is reached, or it is where the route already is (the Start or
	// the stop before it, or the Destination).
	bool AddStopToRoom(const FString& RoomOptionLabel);
	// What the panel's Add stop does: adds the place AFTER the Destination. The Destination becomes the last stop and the new place
	// the Destination (with no Destination set, the new place just becomes it). The same refusals as AddStopToRoom.
	bool AddPlaceToRoom(const FString& RoomOptionLabel);
	bool RemoveStop(int32 StopIndex);
	bool MoveStop(int32 FromIndex, int32 ToIndex);
	void ClearStops();
	// ---- The route as one list of places ----
	// What the Navigation panel shows: the places in the order they are visited, from the Start to the Destination. The two ends are
	// rows even before they are set; a stop is a row between them. The last place is the destination whatever it was called: a route
	// of a Start and one stop is a route to that stop (when a route is looked for, an end that is not set is taken from the stop next
	// to it).
	int32 GetPlaceCount() const { return Stops.Num() + 2; }
	// How many of the places are set; two are enough to look for a route.
	int32 GetSetPlaceCount() const;
	bool IsPlaceSet(int32 Row) const;
	// "Start", "Stop 2", "Destination".
	FString GetPlaceRole(int32 Row) const;
	// What a row shows: the room as its list names it, with its building when the building is not the open one; a place picked on the
	// map by where it is. Empty when the place is not set.
	FString GetPlaceText(int32 Row) const;
	// Sets a row from the list (the Start, the Destination, or replaces a stop).
	bool SetPlaceToRoom(int32 Row, const FString& RoomOptionLabel);
	// Empties an end, or removes a stop.
	bool ClearPlace(int32 Row);
	// Moves a place up (-1) or down (+1) the list; returns where it is now, INDEX_NONE if it cannot. A place that moves onto an end
	// that is not set takes the end.
	int32 MovePlace(int32 Row, int32 Offset);
	// The same places the other way round.
	void ReversePlaces();

	// Click mode for stops: each click on the map (a place indoors, or the ground outside) adds a place after the Destination (the place
	// becomes the Destination, the Destination before it a stop), and the mode stays on for the
	// next one until it is toggled off, Escape is pressed or the limit is reached. Picking a Start or Destination ends it, and the
	// other way round; adding a stop from the list does not disturb a click mode in progress.
	void ToggleSelectStops();
	// Presentation visibility is independent of the stored route and the navigation dock.
	UFUNCTION(BlueprintPure, Category = "Navigation")
	bool IsRouteGuidanceVisible() const { return bRouteGuidanceVisible; }
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void SetRouteGuidanceVisible(bool bVisible);
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void ToggleRouteGuidance();
	bool CanToggleRouteGuidance() const { return HasStart() || HasDestination() || !RoutePoints.IsEmpty(); }
	bool IsStartOutside() const { return Start.bValid && Start.Point.Floor == OutdoorFloor; }
	bool IsDestinationOutside() const { return Destination.bValid && Destination.Point.Floor == OutdoorFloor; }
	bool WasStartSetViaDropdown() const { return Start.bValid && Start.bSetViaDropdown; }
	bool WasDestinationSetViaDropdown() const { return Destination.bValid && Destination.bSetViaDropdown; }

	// The FNavigationRoomOption::GetDisplayLabel() string the room dropdowns should show for the
	// current Start/Destination, if it was set via that dropdown (empty otherwise) - lets
	// UNavigationDockPanelWidget::RefreshRoomDropdowns restore the combo box's visible text after
	// rebuilding its option list (ClearOptions() wipes the displayed text, but doesn't touch this
	// class's own Start/Destination state).
	FString GetStartRoomLabel() const;
	FString GetDestinationRoomLabel() const;

	// What the room dropdowns show for a Start/Destination that was picked from the dropdown while another building was
	// open: its building's name followed by that room's name and floor, e.g. "Building A · Lab 3-01 (Floor 3)". The
	// dropdown lists only the open building's rooms, so these are added to it as an extra entry. Empty when the endpoint
	// is not set from the dropdown, or is in the open building (then GetStartRoomLabel / GetDestinationRoomLabel apply).
	FString GetStartOtherBuildingLabel() const;
	FString GetDestinationOtherBuildingLabel() const;

	// Shared with the floor-plan view so the highlighted outdoor area and the clickable outdoor
	// area always use the same bounds. Coordinates are unscaled footprint coordinates.
	static FBox2D GetOutdoorBounds(const APolyBuilding& Building);
	static bool IsOutdoorPoint(const APolyBuilding& Building, FVector2D Point);

	// Former OnXClicked handlers, renamed and made public now that they're called from
	// the Navigation dock panel's own buttons instead of being bound to widgets this class
	// no longer builds. Bodies are unchanged from the original implementation.
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void ToggleSelectStart();
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void ToggleSelectDestination();
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void RequestFindRoute();
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void RequestClearRoute();

	// RequestFindRoute waits for the route. On a large campus that takes seconds, so the Find Route button calls this instead: the
	// search runs a little each frame (the panel steps itself from the core ticker), shows how far it is, and can be cancelled.
	// The result is applied exactly as RequestFindRoute applies it. Changing the start, the stops or the destination, clearing,
	// editing the interior, or closing the project stops the search without a result. With stops, each leg is searched in turn.
	void StartFindRoute();
	// Works for about BudgetSeconds; true once there is no search running any more. The ticker calls this; tests may call it directly.
	bool StepFindRoute(double BudgetSeconds);
	// The person cancelled: stops the search and says so.
	void CancelFindRoute();
	bool IsFindingRoute() const { return RouteJob.IsValid(); }
	// 0 to 1, and in words what the search is doing, while one is running.
	float GetFindRouteProgress() const;
	FString GetFindRouteProgressText() const;

	// Presentation seam for Person A's combined planner. Its result may contain floor-zero outdoor
	// points and indoor floor points; the floor-plan and directions render both from this one array.
	// Kept as a C++ method because FNavigationRouteResult is intentionally not a reflected type.
	void ApplyRouteResult(const FNavigationRouteResult& Result);

	// Replaces the displayed route and endpoints atomically. Does not calculate a NavMesh path.
	// A failed import leaves the currently displayed route intact and reports the reason.
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	bool ApplyWorldRouteResult(const FString& BuildingId, const TArray<FNavigationWorldRoutePoint>& Points);

	// What the Navigation dock panel (UNavigationDockPanelWidget) needs to display itself - kept here
	// rather than duplicated on the display side, since some of it (CanFindRoute) needs
	// private state (FSelectedEndpoint::BuildingId) that has no other public accessor.
	FString GetDisplayStatusMessage() const;
	FLinearColor GetStatusColor() const;
	// The directions of the route found, one step after another: where to start, each door, lift or staircase to take, and the
	// arrival. The last step is marked (the panel shows it in green). Empty until a route is found.
	struct FNavigationRouteStep
	{
		FString Text;
		bool bFinal = false;
		// The point of the route (RoutePoints) where the person is when the step applies; the step is shown there.
		int32 PointIndex = INDEX_NONE;
	};
	TArray<FNavigationRouteStep> BuildRouteSteps() const;

	// Step by step: the route is gone through a step at a time, as on the phone. When a route is found the first step is the one
	// shown, NextStep and PreviousStep move along, and everything that shows the route shows that step: the building and floor
	// (and the camera) it is in, the stretch of the route it covers (the rest is faint), and a marker where the person is.
	// SetStepByStep(false) shows all the steps instead, with nothing singled out.
	bool NextStep();
	bool PreviousStep();
	void SetStepByStep(bool bOn);
	bool IsStepByStep() const;
	int32 GetGuidedStepIndex() const;
	// The route point of the step being shown (null unless going step by step), and the stretch of the route it covers.
	const FNavigationRoutePoint* GetGuidedPoint() const;
	FNavigationRouteFocus GetGuidedFocus() const;
	// Broadcast whenever the step being shown changes, a route is found (the first step), or step by step is turned on or off.
	UPROPERTY(BlueprintAssignable, Category = "Navigation")
	FOnNavigationStateChanged OnGuidedStepChanged;
	// The same as text: "Step 1: ..." per line when a route is found; before that, just the Start and Destination chosen so far.
	FString BuildRouteDetails() const;
	bool CanFindRoute() const;
	bool CanClearRoute() const;

	// Whether the most recent RequestFindRoute() call came back empty-handed - lets the Find Route
	// button itself show red on failure (a route existing at all, i.e. !GetRoutePoints().IsEmpty(),
	// already covers the green/success case). Cleared back to false by a fresh RequestFindRoute()
	// success, ClearAll, or picking a new Start/Destination point (the old attempt no longer applies).
	bool DidLastFindRouteFail() const { return bLastFindRouteFailed; }

	// One selectable entry for the Start/Destination room dropdowns - Centroid is the room's own
	// area-weighted center (see ComputePolygonCentroid in the .cpp), not a door or a corner, since a
	// room can have more than one door and no single door is "the" point that represents it.
	struct FNavigationRoomOption
	{
		FString RoomId;
		FString RoomName;
		TArray<FString> Tags;
		FString RoomType;
		int32 Floor = 1;
		// Set for outdoor options (Floor == OutdoorFloor): the building the point belongs to. Empty for a room, which belongs to the
		// open building.
		FString BuildingId;
		FVector2D Centroid = FVector2D::ZeroVector;
		// Built with the centroid when the room index changes; queries reuse these strings.
		FString DisplayLabel;
		FString SearchableText;

		// Shared by both the dropdown's own population (UNavigationDockPanelWidget) and matching a
		// selection back to its option (SetStartToRoom/SetDestinationToRoom). Stable room IDs and
		// floors disambiguate names reused by copied rooms or on different floors.
		const FString& GetDisplayLabel() const { return DisplayLabel; }
	};

	// Every closed-loop room (walls/standalone partitions are excluded - see AInteriorRoom::
	// IsClosedLoop) across every floor of the currently loaded building. Empty if nothing's loaded.
	// The cached array is refreshed by the next query after an edit; copy it if keeping a snapshot.
	// In outdoor mode (see SetOutdoorMode) the list is instead each building's main doors, as points just outside them.
	const TArray<FNavigationRoomOption>& GetSelectableRooms() const;
	TArray<FNavigationRoomOption> SearchDestinations(const FString& Query) const;
	// The same matching, in the order of GetSelectableRooms (floor by floor) instead of by name - for the Start dropdown.
	TArray<FNavigationRoomOption> SearchRooms(const FString& Query) const;

	// Sets Start/Destination directly to the named room's centroid, bypassing the click-to-pick flow
	// (and cancelling one if it was in progress) - what the Navigation panel's room dropdowns call.
	// RoomOptionLabel is an FNavigationRoomOption::GetDisplayLabel() string, as populated into the
	// dropdown. Returns false (with a status message explaining why) if no such room exists, or its
	// centroid isn't actually a reachable point (e.g. an extremely irregular room shape whose area
	// centroid falls outside its own footprint).
	bool SetStartToRoom(const FString& RoomOptionLabel);
	bool SetDestinationToRoom(const FString& RoomOptionLabel);

	// Outdoor mode is on while the exterior screen (the campus, no building open) is the one on screen. The room dropdowns then list
	// each building's main doors, and clicks pick points on the ground. Off in the interior editor.
	void SetOutdoorMode(bool bOutdoor);
	bool IsOutdoorMode() const { return bOutdoorMode; }

	// Sets the point being picked (Start or Destination) to a spot on the ground, in campus footprint space (world position divided
	// by the scale). The spot belongs to the building whose outside area it is in. bOnBuilding is true when the click landed on a
	// building rather than the ground. Says why in the status line, and changes nothing, if the spot is not usable.
	void SelectOutdoorGroundPoint(FVector2D CampusPosition, bool bOnBuilding);

	// Called by ANavPlayerController::DeleteRoom right after a room is actually removed. A point
	// picked by clicking the floor plan is an absolute position, independent of any room, so it stays
	// put even if a nearby room disappears - but a point picked from the Start/Destination room
	// dropdown conceptually *is* that room, so deleting it should clear the point too, not leave a
	// dangling reference to a room that no longer exists.
	void NotifyRoomDeleted(const FString& RoomId);

	// Hit-tests against the Navigation dock panel (the screen that shows it hands over a callback,
	// see SetPointerOverPanelCallback) so ANavPlayerController::OnClick's existing IsPointerOverPanel()
	// guard (skip treating a click on the panel's own buttons as picking a route point) keeps working.
	bool IsPointerOverPanel() const;
	void SetPanelBorderForHitTest(UBorder* Border) { HitTestBorder = Border; }
	// Alternative to the border check: the owner says whether a screen position is over the panel's visible
	// content. Takes precedence over HitTestBorder when set (a border's cached geometry goes stale once its
	// tab is swapped out, so it can't be trusted for panels that live in tab groups).
	void SetPointerOverPanelCallback(TFunction<bool(const FVector2D&)> InCallback) { PointerOverPanelCallback = MoveTemp(InCallback); }

	// Broadcast whenever anything display-relevant changes (selection mode, a point
	// accepted/rejected, a route found/cleared, a status message) - the interior and exterior
	// screens subscribe to refresh their Navigation panel's display.
	UPROPERTY(BlueprintAssignable)
	FOnNavigationStateChanged OnStateChanged;
	// Broadcast when clicking the map to pick a Start, Destination or stops is turned on: the mouse is the picking's now, so the
	// owner turns the other tools that use it off.
	FSimpleMulticastDelegate OnPickingStarted;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void BeginDestroy() override;

private:
	// The checks both ways of finding a route start with; false (with the reason shown) when there is nothing to find.
	bool CanStartFindRoute();
	void HandleLayoutChanged();
	bool RefreshSelectedPlaces();
	bool bEndpointsNeedRefresh = false;
	// The trip from the Start through the stops to the Destination, as the legs it is planned in. Both ways of finding a route plan
	// one leg after another and end in ApplyJourney, or in FailJourney with the leg that could not be.
	struct FJourney;
	TSharedRef<FJourney> MakeJourney() const;
	bool MoveGuidedStep(int32 Offset);
	// Starts the step by step at the first step.
	void StartGuidance();
	int32 GuidedStep = 0;
	bool bStepByStep = true;
	// Adds a stop at the end of the others (the limit and a place the route is already at are refused, with a message).
	bool AddStop(FNavigationStop Stop);
	// Adds a place after the Destination: the Destination becomes the last stop and the place the Destination (same refusals as AddStop).
	bool AddPlace(FNavigationStop Place);
	void ApplyJourney(const FJourney& Trip);
	void FailJourney(const FJourney& Trip, const FNavigationRouteResult& LegResult);
	TSharedPtr<FJourney> Journey;
	// The route and its legs are always cleared together.
	void ClearRouteResult() { RoutePoints.Reset(); RouteLegs.Reset(); }
	// What every change of the stops does: the search and the route they were for no longer apply.
	void StopsChanged(const FString& Message, ENavigationMessageKind Kind);
	// A stop from a dropdown entry (what AddStopToRoom adds and a row set from the list is replaced by).
	bool MakeStopFromLabel(const FString& RoomOptionLabel, FNavigationStop& OutStop);
	// The ends that are not set are taken from the stops next to them, so that two places are a route.
	void PromoteEnds();
	// The point on the map a dropdown entry stands for, and the building it is in; false (with the reason, for the status line) when a
	// person cannot be sent there.
	bool MakeOptionPoint(const FNavigationRoomOption& Option, FNavigationRoutePoint& OutPoint, FString& OutBuildingId, FString& OutError) const;
	// Gives a point its building and where it is in the world, as the planner needs them.
	void StampPoint(FNavigationRoutePoint& Point, const FString& BuildingId) const;
	// Stops a running search without a message; true if there was one.
	bool StopRouteJob();
	bool TickFindRoute(float DeltaTime);
	TSharedPtr<FNavigationRouteJob> RouteJob;
	FTSTicker::FDelegateHandle RouteTicker;
	// The endpoints the running search is for; if they change, its result no longer applies.
	FNavigationRoutePoint JobStart, JobDestination;

	void RefreshRoomSearchIndex() const;
	mutable TArray<FNavigationRoomOption> CachedRooms;
	mutable TArray<int32> SortedDestinationIndices;
	mutable uint64 IndexedRoomSearchRevision = MAX_uint64;
	mutable TWeakObjectPtr<APolyBuilding> IndexedBuilding;
	mutable int32 IndexedFloorCount = 0;
	mutable bool bIndexedOutdoor = false;
	mutable int32 IndexedBuildingCount = 0;
	bool bOutdoorMode = false;

	struct FSelectedEndpoint
	{
		bool bValid = false;
		FNavigationRoutePoint Point;
		FString BuildingId;
		bool bSetViaDropdown = false;
		FString RoomOptionLabel;
		FString RoomId;
	};

	FString OtherBuildingLabel(const FSelectedEndpoint& Endpoint) const;
	// A stop as an endpoint, to be spoken of the way the Start and Destination are.
	FSelectedEndpoint StopAsEndpoint(const FNavigationStop& Stop) const;
	// The places as endpoints, Start first and Destination last, and the other way: the ends from the first and the last, the stops
	// from those between (one that is not set cannot be between, and is left out). Tags, when given, are left out and padded the same way.
	TArray<FSelectedEndpoint> PlacesAsRows() const;
	void ApplyRows(TArray<FSelectedEndpoint> Rows, TArray<int32>* Tags = nullptr);
	// How an endpoint is shown in the places list.
	FString PlaceText(const FSelectedEndpoint& Endpoint) const;
	bool ReplaceStop(int32 StopIndex, const FString& RoomOptionLabel);
	// A building by its name (its id when it has none), and where an endpoint is, in words, for the directions.
	FString BuildingDisplayName(const FString& BuildingId) const;
	FString EndpointPhrase(const FSelectedEndpoint& Endpoint) const;
	// Whether the room dropdowns list this endpoint's place as one of their own entries (a room of the open building, or in outdoor
	// mode a main door), as opposed to needing an extra entry.
	bool EndpointIsInCurrentList(const FSelectedEndpoint& Endpoint) const;
	void BuildOutdoorOptions() const;
	bool SetEndpointToOutdoorOption(const FNavigationRoomOption& Option, bool bIsStart);
	void SetSelectionMode(ENavigationSelectionMode NewMode);
	// After a Start or Destination is set by a click: the mode stays on, so the next click changes it again; says so in the status.
	void AddKeepPickingHint();
	void SetStatusMessage(const FString& Message, ENavigationMessageKind Kind);
	void BroadcastStateChanged();
	bool SetEndpointToRoom(const FString& RoomOptionLabel, bool bIsStart);
	void SetEndpoint(FSelectedEndpoint& Endpoint, const FNavigationRoutePoint& Point,
		const FString& BuildingId, const FString& EndpointName, const FString& LocationDescription,
		bool bSetViaDropdown, const FString& RoomOptionLabel = FString(), const FString& RoomId = FString());

	UPROPERTY()
	UBorder* HitTestBorder = nullptr;

	TFunction<bool(const FVector2D&)> PointerOverPanelCallback;

	UPROPERTY()
	ABuildingGenerator* Generator = nullptr;

	UPROPERTY()
	AInteriorGenerator* Interior = nullptr;

	FSelectedEndpoint Start;
	FSelectedEndpoint Destination;
	TArray<FNavigationStop> Stops;
	TArray<FNavigationRoutePoint> RoutePoints;
	TArray<FNavigationRouteLeg> RouteLegs;
	FString StatusMessage;
	ENavigationMessageKind StatusKind = ENavigationMessageKind::Instruction;
	ENavigationSelectionMode SelectionMode = ENavigationSelectionMode::None;
	bool bLastFindRouteFailed = false;
	bool bRouteGuidanceVisible = true;
};
