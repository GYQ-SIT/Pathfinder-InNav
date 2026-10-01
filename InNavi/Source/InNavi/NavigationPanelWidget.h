#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NavigationRoutePlanner.h"
#include "NavigationPanelWidget.generated.h"

class ABuildingGenerator;
class AInteriorGenerator;
class APolyBuilding;
class UBorder;

UENUM(BlueprintType)
enum class ENavigationSelectionMode : uint8
{
	None,
	Start,
	Destination
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

// Navigation selection/route-planning state and logic. No UI of its own any more -
// UInteriorScreenWidget's own Navigation dock panel is the presentation layer; this
// class stays alive as a UUserWidget owned/added-to-viewport by ANavPlayerController
// purely so its existing PC-side lifecycle (create/AddToViewport/visibility toggles)
// doesn't need to change. See UInteriorScreenWidget::RefreshNavigationPanelDisplay.
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
	void SelectPoint(FVector2D FootprintPosition, int32 Floor);
	bool IsSelectingPoint() const { return SelectionMode != ENavigationSelectionMode::None; }

	bool HasStart() const { return Start.bValid; }
	bool HasDestination() const { return Destination.bValid; }
	const FNavigationRoutePoint& GetStart() const { return Start.Point; }
	const FNavigationRoutePoint& GetDestination() const { return Destination.Point; }
	const TArray<FNavigationRoutePoint>& GetRoutePoints() const { return RoutePoints; }
	bool IsStartOutside() const { return Start.bValid && Start.Point.Floor == OutdoorFloor; }
	bool IsDestinationOutside() const { return Destination.bValid && Destination.Point.Floor == OutdoorFloor; }
	bool WasStartSetViaDropdown() const { return Start.bValid && Start.bSetViaDropdown; }
	bool WasDestinationSetViaDropdown() const { return Destination.bValid && Destination.bSetViaDropdown; }

	// The FNavigationRoomOption::GetDisplayLabel() string the room dropdowns should show for the
	// current Start/Destination, if it was set via that dropdown (empty otherwise) - lets
	// UInteriorScreenWidget::RefreshNavRoomDropdowns restore the combo box's visible text after
	// rebuilding its option list (ClearOptions() wipes the displayed text, but doesn't touch this
	// class's own Start/Destination state - see that function's own comment for the full bug).
	FString GetStartRoomLabel() const { return WasStartSetViaDropdown() ? Start.RoomOptionLabel : FString(); }
	FString GetDestinationRoomLabel() const { return WasDestinationSetViaDropdown() ? Destination.RoomOptionLabel : FString(); }

	// Shared with the floor-plan view so the highlighted outdoor area and the clickable outdoor
	// area always use the same bounds. Coordinates are unscaled footprint coordinates.
	static FBox2D GetOutdoorBounds(const APolyBuilding& Building);
	static bool IsOutdoorPoint(const APolyBuilding& Building, FVector2D Point);

	// Former OnXClicked handlers, renamed and made public now that they're called from
	// UInteriorScreenWidget's own buttons instead of being bound to widgets this class
	// no longer builds. Bodies are unchanged from the original implementation.
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void ToggleSelectStart();
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void ToggleSelectDestination();
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void RequestFindRoute();
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void RequestClearRoute();

	// Presentation seam for Person A's combined planner. Its result may contain floor-zero outdoor
	// points and indoor floor points; the floor-plan and directions render both from this one array.
	// Kept as a C++ method because FNavigationRouteResult is intentionally not a reflected type.
	void ApplyRouteResult(const FNavigationRouteResult& Result);

	// What UInteriorScreenWidget's Navigation panel needs to display itself - kept here
	// rather than duplicated on the display side, since some of it (CanFindRoute) needs
	// private state (FSelectedEndpoint::BuildingId) that has no other public accessor.
	FString GetDisplayStatusMessage() const;
	FLinearColor GetStatusColor() const;
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
		int32 Floor = 1;
		FVector2D Centroid = FVector2D::ZeroVector;

		// Shared by both the dropdown's own population (UInteriorScreenWidget) and matching a
		// selection back to its option (SetStartToRoom/SetDestinationToRoom) - one format, so they
		// can never drift out of sync with each other. Always includes the floor, not just when
		// names collide, so the format never has to change and pick a room genuinely still isn't
		// ambiguous when two floors happen to reuse the same room name.
		FString GetDisplayLabel() const { return FString::Printf(TEXT("%s (Floor %d)"), *RoomName, Floor); }
	};

	// Every closed-loop room (walls/standalone partitions are excluded - see AInteriorRoom::
	// IsClosedLoop) across every floor of the currently loaded building. Empty if nothing's loaded.
	TArray<FNavigationRoomOption> GetSelectableRooms() const;

	// Sets Start/Destination directly to the named room's centroid, bypassing the click-to-pick flow
	// (and cancelling one if it was in progress) - what the Navigation panel's room dropdowns call.
	// RoomOptionLabel is an FNavigationRoomOption::GetDisplayLabel() string, as populated into the
	// dropdown. Returns false (with a status message explaining why) if no such room exists, or its
	// centroid isn't actually a reachable point (e.g. an extremely irregular room shape whose area
	// centroid falls outside its own footprint).
	bool SetStartToRoom(const FString& RoomOptionLabel);
	bool SetDestinationToRoom(const FString& RoomOptionLabel);

	// Called by ANavPlayerController::DeleteRoom right after a room is actually removed. A point
	// picked by clicking the floor plan is an absolute position, independent of any room, so it stays
	// put even if a nearby room disappears - but a point picked from the Start/Destination room
	// dropdown conceptually *is* that room, so deleting it should clear the point too, not leave a
	// dangling reference to a room that no longer exists.
	void NotifyRoomDeleted(const FString& RoomId);

	// Hit-tests against this instead of a widget of its own now - UInteriorScreenWidget
	// hands it the Border of its own Navigation dock panel (see SetPanelBorderForHitTest)
	// so ANavPlayerController::OnClick's existing IsPointerOverPanel() guard (skip
	// treating a click on the panel's own buttons as picking a route point) keeps
	// working unchanged.
	bool IsPointerOverPanel() const;
	void SetPanelBorderForHitTest(UBorder* Border) { HitTestBorder = Border; }

	// Broadcast whenever anything display-relevant changes (selection mode, a point
	// accepted/rejected, a route found/cleared, a status message) - UInteriorScreenWidget
	// subscribes to refresh its own Navigation panel display.
	UPROPERTY(BlueprintAssignable)
	FOnNavigationStateChanged OnStateChanged;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	struct FSelectedEndpoint
	{
		bool bValid = false;
		FNavigationRoutePoint Point;
		FString BuildingId;
		bool bSetViaDropdown = false;
		FString RoomOptionLabel;
		FString RoomId;
	};

	void SetSelectionMode(ENavigationSelectionMode NewMode);
	void SetStatusMessage(const FString& Message, ENavigationMessageKind Kind);
	void BroadcastStateChanged();
	bool SetEndpointToRoom(const FString& RoomOptionLabel, bool bIsStart);
	void SetEndpoint(FSelectedEndpoint& Endpoint, const FNavigationRoutePoint& Point,
		const FString& BuildingId, const FString& EndpointName, const FString& LocationDescription,
		bool bSetViaDropdown, const FString& RoomOptionLabel = FString(), const FString& RoomId = FString());

	UPROPERTY()
	UBorder* HitTestBorder = nullptr;

	UPROPERTY()
	ABuildingGenerator* Generator = nullptr;

	UPROPERTY()
	AInteriorGenerator* Interior = nullptr;

	FSelectedEndpoint Start;
	FSelectedEndpoint Destination;
	TArray<FNavigationRoutePoint> RoutePoints;
	FString StatusMessage;
	ENavigationMessageKind StatusKind = ENavigationMessageKind::Instruction;
	ENavigationSelectionMode SelectionMode = ENavigationSelectionMode::None;
	bool bLastFindRouteFailed = false;
};
