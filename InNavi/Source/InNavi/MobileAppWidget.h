#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileMockSource.h"
#include "MobileNavSource.h"
#include "MobileSheetFlow.h"
#include "MobileAppWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UScaleBox;
class USizeBox;
class UTextBlock;
class UWidget;
class UWidgetSwitcher;
class UMobileSearchPageWidget;
class UMobilePlacePageWidget;
class UMobileRoutePageWidget;
class UMobileNavigatePageWidget;
class UMobileArrivedPageWidget;

// Where the main pieces of the phone screen are, in design pixels (the phone screen is 375 wide however big the window is), so a test can
// check that nothing falls off a small or a tall screen.
struct FMobileLayoutSnapshot
{
	double DesignWidth = 0.0;
	double DesignHeight = 0.0;
	double SheetTop = 0.0;
	double SheetHeight = 0.0;
	double TabBarTop = 0.0;
	double TabBarHeight = 0.0;
	double TopBarBottom = 0.0;
	double ControlsBottom = 0.0;
	bool bControlsVisible = false;
};

// The phone app's screen: a full-screen map area with a draggable bottom sheet, a top bar, the level and locate controls, and four tabs
// at the bottom. Portrait only. This is the skeleton: the map area and every page of the sheet are placeholders for now, and nothing here
// reads the real navigation data (that goes through IMobileNavSource later). The sheet's flow is FMobileSheetFlow.
//
// The layout is always 375 design pixels wide: the whole screen is scaled to the window's width, so a bigger or smaller phone looks the
// same, only taller or shorter. A window 375 pixels wide shows design pixels one to one.
UCLASS()
class INNAVI_API UMobileAppWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr double DesignWidth = 375.0;
	static constexpr double TabBarHeight = 56.0;
	// What stays clear above the sheet when it is fully open: the top bar.
	static constexpr double TopMargin = 64.0;
	// The part of the sheet's top that drags it: the handle and a little around it.
	static constexpr double HandleZoneHeight = 36.0;
	// The level and locate buttons stacked, and the gap above the sheet: they are hidden when the sheet is open so far that they would cover the top bar.
	static constexpr double ControlsHeight = 88.0;
	static constexpr double ControlsGap = 12.0;

	enum class ETab : uint8 { Explore, Directory, Saved, Profile };

	// Lays the screen out for a window LocalWidth x LocalHeight (in Slate units). Called every frame from the window's real size; tests
	// call it with the sizes of several phones.
	void ApplyViewportSize(double LocalWidth, double LocalHeight);

	// The status bar and the gesture bar of a phone (design pixels). Zero on the desktop; set from the device when there is one.
	void SetSafeInsets(double Top, double Bottom);

	FMobileLayoutSnapshot GetLayoutSnapshot() const;

	const FMobileSheetFlow& GetFlow() const { return Flow; }
	EMobileSheetSnap GetSnap() const { return Snap; }
	ETab GetActiveTab() const { return ActiveTab; }
	bool IsShowing3D() const { return bShow3D; }

	void SetActiveTab(ETab Tab);
	// For trying the screen on the desktop: goes the way through the flow to a state (Search, Place, Route, Navigating, Arrived).
	void PreviewState(EMobileSheetState Target);
	// For trying the route page: opens one of its drop-downs (From, To, Building, Level).
	void PreviewOpenSelect(const FString& Name);
	void SendSheetEvent(EMobileSheetEvent Event);
	// A place was picked on the search page: the sheet moves on to it.
	void ShowPlace(const FMobilePlace& Place);
	// Makes a place the one the place page shows, without moving the sheet.
	void SelectPlace(const FMobilePlace& Place);
	// Finds the route again for the start, the destination and the ways between levels, and shows it on the route page.
	void RefreshRoute();
	// The person changed something on the route page (from, to, where My location is, the ways between levels): the screen keeps it and finds the route again.
	void ApplyRouteRequest(const FMobileRouteRequest& Changed);
	// Walking: the next instruction (on a real phone this follows the person; here a button), and the last one is the arrival.
	void AdvanceNavigation();
	void RefreshNavigation();
	void RefreshArrived();
	void SetSnap(EMobileSheetSnap NewSnap);
	// The sheet's height right now, mid-drag or mid-move included (design pixels).
	double GetSheetHeight() const { return SheetHeight; }
	// Skips the sliding so the sheet is at its rest height at once (tests, and the first frame).
	void SnapSheetNow();

	// What a drag on the handle does; the mouse and touch events below both end up here. Positions are in design pixels from the top left.
	bool BeginSheetDrag(const FVector2D& DesignPosition);
	void MoveSheetDrag(const FVector2D& DesignPosition);
	void EndSheetDrag(const FVector2D& DesignPosition);
	bool IsDraggingSheet() const { return bDraggingSheet; }

	UWidget* FindNamedWidget(const FName& Name) const;

	UMobileSearchPageWidget* GetSearchPage() const { return SearchPage; }
	UMobilePlacePageWidget* GetPlacePage() const { return PlacePage; }
	UMobileRoutePageWidget* GetRoutePage() const { return RoutePage; }
	UMobileNavigatePageWidget* GetNavigatePage() const { return NavigatePage; }
	UMobileArrivedPageWidget* GetArrivedPage() const { return ArrivedPage; }
	int32 GetNavStepIndex() const { return NavStepIndex; }
	const FMobileRoute& GetCurrentRoute() const { return CurrentRoute; }
	const FMobileRoutePreferences& GetRoutePreferences() const { return RoutePreferences; }
	bool IsPlaceSaved(const FString& PlaceId) const { return SavedPlaceIds.Contains(PlaceId); }
	const FString& GetStartPlaceId() const { return StartPlaceId; }
	int32 GetMyLocationLevel() const { return MyLocationLevel; }
	const FString& GetMyLocationBuildingId() const { return MyLocationBuildingId; }
	const FMobilePlace& GetSelectedPlace() const { return SelectedPlace; }
	IMobileNavSource* GetSource() const { return Source.Get(); }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;

private:
	UFUNCTION()
	void OnTabExploreClicked();
	UFUNCTION()
	void OnTabDirectoryClicked();
	UFUNCTION()
	void OnTabSavedClicked();
	UFUNCTION()
	void OnTabProfileClicked();
	UFUNCTION()
	void OnOpenSiteClicked();
	UFUNCTION()
	void OnShow2DClicked();
	UFUNCTION()
	void OnShow3DClicked();

	// Turns the screen's position (Slate units, from the window's top left) into design pixels.
	FVector2D ToDesign(const FGeometry& Geometry, const FVector2D& ScreenSpacePosition) const;
	FReply HandlePointerDown(const FGeometry& Geometry, const FPointerEvent& Event);
	FReply HandlePointerMove(const FGeometry& Geometry, const FPointerEvent& Event);
	FReply HandlePointerUp(const FGeometry& Geometry, const FPointerEvent& Event);

	void RefreshTabs();
	void RefreshModeButtons();
	void RefreshSheetPage();
	void PositionSheet();
	// Whether the level and locate buttons have room between the top bar and the sheet.
	bool ControlsFit() const;
	double RestHeight(EMobileSheetSnap Target) const;

	UPROPERTY()
	UScaleBox* PhoneScaleBox = nullptr;
	UPROPERTY()
	USizeBox* PhoneSize = nullptr;
	UPROPERTY()
	UCanvasPanel* Phone = nullptr;
	UPROPERTY()
	UBorder* SheetBorder = nullptr;
	UPROPERTY()
	UWidgetSwitcher* SheetPages = nullptr;
	UPROPERTY()
	UBorder* TabBarBorder = nullptr;
	UPROPERTY()
	UBorder* TopBarBorder = nullptr;
	UPROPERTY()
	UWidget* ControlsBox = nullptr;
	UPROPERTY()
	UBorder* ExploreOverlay = nullptr;
	UPROPERTY()
	UBorder* SavedOverlay = nullptr;
	UPROPERTY()
	UBorder* ProfileOverlay = nullptr;
	UPROPERTY()
	UButton* TabButtons[4] = { nullptr, nullptr, nullptr, nullptr };
	UPROPERTY()
	UTextBlock* TabLabels[4] = { nullptr, nullptr, nullptr, nullptr };
	UPROPERTY()
	UBorder* TabIcons[4] = { nullptr, nullptr, nullptr, nullptr };
	UPROPERTY()
	UButton* Mode2DButton = nullptr;
	UPROPERTY()
	UButton* Mode3DButton = nullptr;
	UPROPERTY()
	UTextBlock* Mode2DLabel = nullptr;
	UPROPERTY()
	UTextBlock* Mode3DLabel = nullptr;
	// Where the places come from (the prototype's data for now), the site shown, and the place the person picked.
	TSharedPtr<IMobileNavSource> Source;
	FString SiteId = TEXT("sit-punggol");
	FMobilePlace SelectedPlace;
	UPROPERTY()
	UMobileSearchPageWidget* SearchPage = nullptr;
	UPROPERTY()
	UMobilePlacePageWidget* PlacePage = nullptr;
	UPROPERTY()
	UMobileRoutePageWidget* RoutePage = nullptr;
	UPROPERTY()
	UMobileNavigatePageWidget* NavigatePage = nullptr;
	UPROPERTY()
	UMobileArrivedPageWidget* ArrivedPage = nullptr;
	// Which step of the route the person is on while walking (0-based).
	int32 NavStepIndex = 0;
	FMobileRoute CurrentRoute;
	FMobileRoutePreferences RoutePreferences;
	// Where "My location" is for the route (the prototype lets the person say which building and level they are on).
	FString MyLocationBuildingId;
	int32 MyLocationLevel = 1;
	// The places the person saved, and the one they chose to start from (empty = their location).
	TSet<FString> SavedPlaceIds;
	FString StartPlaceId;

	FMobileSheetFlow Flow;
	ETab ActiveTab = ETab::Explore;
	EMobileSheetSnap Snap = EMobileSheetSnap::Half;
	bool bShow3D = false;

	double DesignHeight = 812.0;
	double Scale = 1.0;
	double SafeTop = 0.0;
	double SafeBottom = 0.0;
	double SheetHeight = 0.0;
	bool bDraggingSheet = false;
	bool bSheetMoved = false;
	double DragStartY = 0.0;
	double DragStartHeight = 0.0;
	bool bSheetSettled = false;
};
