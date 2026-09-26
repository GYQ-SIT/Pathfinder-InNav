#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NavigationRoutePlanner.h"
#include "NavigationPanelWidget.generated.h"

class ABuildingGenerator;
class AInteriorGenerator;
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
	void SetSelectionMode(ENavigationSelectionMode NewMode);
	void SetStatusMessage(const FString& Message, ENavigationMessageKind Kind);
	void BroadcastStateChanged();

	UPROPERTY()
	UBorder* HitTestBorder = nullptr;

	UPROPERTY()
	ABuildingGenerator* Generator = nullptr;

	UPROPERTY()
	AInteriorGenerator* Interior = nullptr;

	struct FSelectedEndpoint
	{
		bool bValid = false;
		FNavigationRoutePoint Point;
		FString BuildingId;
	};

	FSelectedEndpoint Start;
	FSelectedEndpoint Destination;
	TArray<FNavigationRoutePoint> RoutePoints;
	FString StatusMessage;
	ENavigationMessageKind StatusKind = ENavigationMessageKind::Instruction;
	ENavigationSelectionMode SelectionMode = ENavigationSelectionMode::None;
	bool bLastFindRouteFailed = false;
};
