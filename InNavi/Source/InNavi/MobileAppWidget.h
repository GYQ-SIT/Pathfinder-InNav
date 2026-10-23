#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileMockSource.h"
#include "MobileNavSource.h"
#include "MobilePreferences.h"
#include "MobileSavedPlaces.h"
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
class UMobileMapWidget;
class UMobileExplorePageWidget;
class UMobileSavedPageWidget;
class UMobileProfilePageWidget;
class UVerticalBox;
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
	double HeaderBottom = 0.0;
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
	// As in the prototype: a header (the app's name) of 44 at the top of every tab, and a tab bar of 60 at the bottom. The status bar and the gesture bar
	// of a phone come on top of them (SafeTop, SafeBottom).
	static constexpr double HeaderHeight = 44.0;
	static constexpr double TabBarHeight = 60.0;
	// The part of the sheet's top that drags it: the handle and a little around it.
	static constexpr double HandleZoneHeight = 28.0;
	// The map's toolbar (level, locate, more) and its gap above the sheet. The top row (the title, Overview, 2D / 3D) is 54 tall, 8 below the map's top.
	// The toolbar and the plan are hidden while the sheet is open to its top (88 percent), as in the prototype.
	static constexpr double TopRowTop = 8.0;
	static constexpr double TopRowHeight = 54.0;
	static constexpr double ControlsHeight = 54.0;
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
	// Shows on the map what the sheet is about: the place, the route, the person walking; and keeps the level button right.
	void RefreshMap();
	// Tells the search page where the walk starts (a place the person chose, or nothing for My location).
	void RefreshSearchStart();
	void RefreshSaved();
	void ApplyMap();
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
	// What a press, a move and a release of one pointer do to the sheet (the mouse and touch events above call these). Only the pointer that
	// pressed on the handle moves it; a second one (the mouse cursor of an emulator click, which comes with the touch) is ignored.
	FReply HandlePointerDown(const FGeometry& Geometry, const FPointerEvent& Event);
	FReply HandlePointerMove(const FGeometry& Geometry, const FPointerEvent& Event);
	FReply HandlePointerUp(const FGeometry& Geometry, const FPointerEvent& Event);

	UWidget* FindNamedWidget(const FName& Name) const;

	UMobileSearchPageWidget* GetSearchPage() const { return SearchPage; }
	UMobilePlacePageWidget* GetPlacePage() const { return PlacePage; }
	UMobileRoutePageWidget* GetRoutePage() const { return RoutePage; }
	UMobileNavigatePageWidget* GetNavigatePage() const { return NavigatePage; }
	UMobileMapWidget* GetMapView() const { return MapView; }
	UMobileExplorePageWidget* GetExplorePage() const { return ExplorePage; }
	const FString& GetSiteId() const { return SiteId; }
	// Opens a site (a card on Explore): everything starts again for it, on the map tab.
	void OpenSite(const FString& NewSiteId);
	const FString& GetMapBuildingId() const { return MapBuildingId; }
	int32 GetMapLevel() const { return MapLevel; }
	bool IsMapOverview() const { return bMapOverview; }
	// The level popup (the L1 button above the sheet): open or closed, and a choice in it.
	bool IsLevelPopupOpen() const { return bLevelPopupOpen; }
	void SetLevelPopupOpen(bool bOpen);
	void ChooseMapLevel(int32 Level);
	// The Overview, Locate and 2D / 3D buttons.
	void ToggleOverview();
	void LocateMe();
	void SetMap3D(bool bShow3DMap);
	// The map's tools (the More button above the sheet): the Move switch (drag and pinch move the view only when it is on, as in the prototype)
	// and the zoom buttons, which always work.
	void SetMapToolsOpen(bool bOpen);
	bool IsMapToolsOpen() const { return bMapToolsOpen; }
	void ToggleMapGestures();
	bool AreMapGesturesOn() const { return bMapGestures; }
	void ZoomMap(double Delta);
	// A tap on a building of the overview opens it.
	void OpenBuilding(const FString& BuildingId);
	UMobileArrivedPageWidget* GetArrivedPage() const { return ArrivedPage; }
	int32 GetNavStepIndex() const { return NavStepIndex; }
	const FMobileRoute& GetCurrentRoute() const { return CurrentRoute; }
	const FMobileRoutePreferences& GetRoutePreferences() const { return RoutePreferences; }
	bool IsPlaceSaved(const FString& PlaceId) const { return SavedPlaces.Contains(PlaceId); }
	const FMobileSavedPlaces& GetSavedPlaces() const { return SavedPlaces; }
	UMobileSavedPageWidget* GetSavedPage() const { return SavedPage; }
	// Keeps the saved places in a file (the real app does; tests do not touch one unless they ask), and reads what is in it.
	void UseSavedFile(const FString& Path);
	// For trying the Saved tab: saves some places (comma separated ids) without a file.
	void PreviewSave(const FString& CommaIds);
	// A saved place was tapped: its site opens if it is another, and the place with it.
	void OpenSavedPlace(const FMobilePlace& Place);
	UMobileProfilePageWidget* GetProfilePage() const { return ProfilePage; }
	const FMobilePreferences& GetPreferences() const { return Preferences; }
	// Keeps the preferences in a file (as for the saved places) and reads what is in it.
	void UsePreferencesFile(const FString& Path);
	// The Profile tab's reset: no saved places, the app's own preferences.
	void ResetSavedAndPreferences();
	// The theme or the text size changed: every page is built again in the new look, and the screen is put back as it was (the same tab, site,
	// place, route and step). The Profile tab asks for it while one of its own buttons is being tapped, so it happens a moment later; tests call
	// it themselves.
	void RebuildForAppearance();
	bool IsAppearanceRebuildQueued() const { return bAppearanceRebuildQueued; }
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
	void OnOverviewClicked();
	UFUNCTION()
	void OnLevelClicked();
	UFUNCTION()
	void OnLocateClicked();
	UFUNCTION()
	void OnShow2DClicked();
	UFUNCTION()
	void OnShow3DClicked();
	UFUNCTION()
	void OnMapMoreClicked();
	UFUNCTION()
	void OnMapMoveClicked();
	UFUNCTION()
	void OnZoomInClicked();
	UFUNCTION()
	void OnZoomOutClicked();

	// Turns the screen's position (Slate units, from the window's top left) into design pixels.
	FVector2D ToDesign(const FGeometry& Geometry, const FVector2D& ScreenSpacePosition) const;

	void RefreshTabs();
	void RefreshModeButtons();
	void RefreshMapTools();
	void RefreshSiteName();
	// On a phone: reads the safe area the system reports (a camera cut-out, the status and gesture bars) and lays the screen out inside it.
	void ApplyDeviceSafeArea();
	// Makes the look of the preferences the open one, and asks for the pages to be built again if they were built in another.
	void SyncAppearance();
	void QueueAppearanceRebuild();
	void RefreshSheetPage();
	void PositionSheet();
	// Whether the level and locate buttons have room between the top bar and the sheet.
	bool ControlsFit() const;
	// Whether the sheet is open to the top (80 percent of the space between the header and the tab bar or more).
	bool SheetIsOpen() const;
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
	UBorder* HeaderBorder = nullptr;
	UPROPERTY()
	UButton* OverviewButton = nullptr;
	UPROPERTY()
	UTextBlock* OverviewLabel = nullptr;
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
	UMobileMapWidget* MapView = nullptr;
	UPROPERTY()
	UMobileExplorePageWidget* ExplorePage = nullptr;
	UPROPERTY()
	UMobileSavedPageWidget* SavedPage = nullptr;
	UPROPERTY()
	UMobileProfilePageWidget* ProfilePage = nullptr;
	UPROPERTY()
	UTextBlock* SiteNameText = nullptr;
	UPROPERTY()
	UTextBlock* LevelLabel = nullptr;
	UPROPERTY()
	UVerticalBox* LevelPopupList = nullptr;
	UPROPERTY()
	UBorder* LevelPopupBorder = nullptr;
	// What the map shows: a level of a building, or the overview of the site.
	FString MapBuildingId;
	int32 MapLevel = 1;
	bool bMapOverview = false;
	bool bLevelPopupOpen = false;
	// The map tools: the pop-up beside the More button, the Move switch, and the zoom buttons.
	UPROPERTY()
	UBorder* MapToolsBorder = nullptr;
	UPROPERTY()
	UButton* MapMoveButton = nullptr;
	UPROPERTY()
	UTextBlock* MapMoveLabel = nullptr;
	UPROPERTY()
	UButton* ZoomInButton = nullptr;
	UPROPERTY()
	UButton* ZoomOutButton = nullptr;
	UPROPERTY()
	UTextBlock* ZoomInLabel = nullptr;
	UPROPERTY()
	UTextBlock* ZoomOutLabel = nullptr;
	bool bMapToolsOpen = false;
	bool bMapGestures = false;
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
	FMobileSavedPlaces SavedPlaces;
	// How the person chose the app to look (the Profile tab).
	FMobilePreferences Preferences;
	// The look the pages were built in, and whether a build again is waiting.
	EMobileTheme BuiltTheme = EMobileTheme::Light;
	bool bBuiltLarger = false;
	bool bAppearanceRebuildQueued = false;
	FString StartPlaceId;

	FMobileSheetFlow Flow;
	ETab ActiveTab = ETab::Explore;
	EMobileSheetSnap Snap = EMobileSheetSnap::Half;
	bool bShow3D = false;

	double DesignHeight = 812.0;
	double Scale = 1.0;
	double SafeTop = 0.0;
	double SafeBottom = 0.0;
	float SafeAreaPollSeconds = 0.f;
	// A line in the log every two seconds with how long the frames took (not in a shipping build), to see how the app runs on a phone.
	int32 PerfFrames = 0;
	int32 PerfSlowFrames = 0;
	int32 PerfVerySlowFrames = 0;
	double PerfSeconds = 0.0;
	double PerfWorst = 0.0;
	double SheetHeight = 0.0;
	bool bDraggingSheet = false;
	bool bSheetMoved = false;
	// The pointer that started the drag (a finger, or the mouse). A phone can report both for one click (the emulator sends the touch and the
	// mouse cursor's moves), and only the one that pressed may move the sheet.
	int32 DragPointerIndex = INDEX_NONE;
	bool bDragIsTouch = false;
	double DragStartY = 0.0;
	double DragStartHeight = 0.0;
	bool bSheetSettled = false;
};
