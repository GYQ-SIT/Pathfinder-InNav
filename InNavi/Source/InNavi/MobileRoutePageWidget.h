#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileNavSource.h"
#include "MobileSelectWidget.h"
#include "MobileRoutePageWidget.generated.h"

class UBorder;
class UButton;
class UHorizontalBox;
class UImage;
class UOverlay;
class UMobileReorderHandle;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UWrapBox;

// The bottom sheet's "Your route" page, as in the prototype's second iteration: the stops in a card (where the walk starts, with the building and level for
// My location, then each destination with its badge, a way to take it off and a handle to move it, and Add stop), the minutes and metres, how the person
// goes between levels (Escalator, Lift, Stairs), Route options and Route steps that open and close, and Start navigation pinned at the bottom. It shows the
// request and the route it is given and reports any change the person makes as a new request; the screen finds the route again and gives the page the result.
UCLASS()
class INNAVI_API UMobileRoutePageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// A row of a stop is this tall (design pixels): dragging a handle by this much moves the stop one place.
	static constexpr float StopRowHeight = 56.f;

	// The places and buildings the drop-downs offer.
	void SetChoices(const TArray<FMobilePlace>& InPlaces, const TArray<FMobileBuilding>& InBuildings);
	// What the page shows: the request (where from and to, the stops, the ways between levels) and the route found for it. With several destinations Legs
	// holds the route of each leg planned so far (Route is the first); the page shows the steps of the first leg and the total.
	void SetRequest(const FMobileRouteRequest& InRequest, const FMobileRoute& InRoute, bool bInPending = false, const TArray<FMobileRoute>& InLegs = TArray<FMobileRoute>());

	void SetOnBack(TFunction<void()> InCallback) { OnBack = MoveTemp(InCallback); }
	void SetOnRequestChanged(TFunction<void(const FMobileRouteRequest&)> InCallback) { OnRequestChanged = MoveTemp(InCallback); }
	void SetOnStart(TFunction<void()> InCallback) { OnStart = MoveTemp(InCallback); }
	// The sheet is down at its lowest: "Your route" and the travel mode, and Start navigation, and nothing between (as in the prototype).
	void SetMinimized(bool bInMinimized);
	bool IsMinimized() const { return bMinimized; }

	// What the person does: a tap on a way-between-levels chip ("lift", "stairs", "escalator", "liftonly"), and a choice in each drop-down
	// (the From and To values are place ids; From's empty value is My location; the level is its number).
	void TapOption(const FString& Option);
	void ChooseFrom(const FString& PlaceId);
	void ChooseTo(const FString& PlaceId);
	// The destinations are ToPlaceId and then StopPlaceIds: Add stop puts a place at the end; Remove takes one off (never the last); Move puts the one at From
	// where To is; Choose changes which place one of them is. Each is a change of the request.
	void AddStop(const FString& PlaceId);
	void RemoveDestination(int32 Index);
	void MoveDestination(int32 From, int32 To);
	void ChooseDestination(int32 Index, const FString& PlaceId);
	void ChooseMyBuilding(const FString& BuildingId);
	void ChooseMyLevel(int32 Level);
	// The "Add stop" row turns into a drop-down of the places that are not in the trip yet (and back).
	void BeginAddStop();
	void CancelAddStop();
	bool IsAddingStop() const { return bAddingStop; }
	// Route options and Route steps open and close (closed at first).
	void SetOptionsOpen(bool bOpen);
	void SetStepsOpen(bool bOpen);
	bool AreOptionsOpen() const { return bOptionsOpen; }
	bool AreStepsOpen() const { return bStepsOpen; }

	const FMobileRouteRequest& GetRequest() const { return Request; }
	const FMobileRoutePreferences& GetPreferences() const { return Request.Preferences; }
	const FMobileRoute& GetRoute() const { return Route; }
	int32 GetShownStepCount() const { return ShownSteps; }
	int32 GetDestinationCount() const { return Request.ToPlaceId.IsEmpty() ? 0 : 1 + Request.StopPlaceIds.Num(); }
	// The walk of the whole trip: the sum of the legs, or the one route.
	int32 GetTotalMinutes() const;
	double GetTotalMetres() const;
	UWidget* FindNamedWidget(const FName& Name) const;

	// What is on the page now (the drop-downs and the numbers live in widgets of their own, so a test cannot look them up by name).
	UMobileSelectWidget* GetFromSelect() const { return FromSelect; }
	// The drop-down of the first destination.
	UMobileSelectWidget* GetToSelect() const { return DestinationSelects.IsEmpty() ? nullptr : DestinationSelects[0]; }
	UMobileSelectWidget* GetDestinationSelect(int32 Index) const { return DestinationSelects.IsValidIndex(Index) ? DestinationSelects[Index] : nullptr; }
	UMobileSelectWidget* GetAddSelect() const { return AddSelect; }
	UMobileSelectWidget* GetBuildingSelect() const { return BuildingSelect; }
	UMobileSelectWidget* GetLevelSelect() const { return LevelSelect; }
	// The handle of a destination's row, to drag it (and the tests).
	UMobileReorderHandle* GetReorderHandle(int32 Index) const { return ReorderHandles.IsValidIndex(Index) ? ReorderHandles[Index] : nullptr; }
	bool IsMyLocationRowShown() const;
	FString GetMinutesString() const;
	FString GetMetresString() const;
	FString GetErrorString() const;
	// "via Escalator, Lift, Stairs" and, with several destinations, the line over it ("3 destinations . 9 min . 120 m total").
	FString GetViaString() const;
	FString GetJourneyString() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleBackClicked();
	UFUNCTION()
	void HandleStartClicked();

	void Refresh();
	void RefreshSubtitle();

	UPROPERTY()
	UWidget* BodyScroll = nullptr;
	// The overlay over the page that the drop-down lists open in (over the stops, not pushing them down).
	UPROPERTY()
	UOverlay* PopupRoot = nullptr;
	UPROPERTY()
	UTextBlock* SubtitleText = nullptr;
	bool bMinimized = false;
	void RefreshStops();
	void RefreshSummary();
	void RefreshTransport();
	void RefreshOptions();
	void RefreshSteps();
	void RefreshSections();
	void CloseOtherSelects(const UMobileSelectWidget* Keep);
	void Changed();
	UMobileSelectWidget* MakeSelect(EMobileSelectLook Look, const TCHAR* Caption, TFunction<void(UMobileRoutePageWidget*, const FString&)> OnChosen);
	// One row of the stops card; returns its box (a drag moves it). Badge is the circle at the left, joined by a line to the rows above and below.
	USizeBox* AddStopRow(UWidget* Badge, UWidget* Content, UWidget* Actions, bool bLineAbove, bool bLineBelow);
	UWidget* MakeBadge(const FLinearColor& Fill, UWidget* Face);
	UWidget* MakeSectionHeader(const FString& Title, const FString& Count, bool bOpen, const FString& Payload);
	// The options the drop-downs offer: My location (From only), then every place; a short label is the place's name.
	TArray<FMobileSelectOption> PlaceOptions(bool bWithMyLocation, const TArray<FString>& Except) const;
	// The line under a destination's name ("E2-01-101 . E2 . L1").
	FString PlaceLine(const FString& PlaceId) const;

	UPROPERTY()
	UMobileSelectWidget* FromSelect = nullptr;
	UPROPERTY()
	TArray<UMobileSelectWidget*> DestinationSelects;
	UPROPERTY()
	UMobileSelectWidget* AddSelect = nullptr;
	UPROPERTY()
	UMobileSelectWidget* BuildingSelect = nullptr;
	UPROPERTY()
	UMobileSelectWidget* LevelSelect = nullptr;
	UPROPERTY()
	TArray<UMobileReorderHandle*> ReorderHandles;
	UPROPERTY()
	TArray<USizeBox*> StopRowBoxes;
	UPROPERTY()
	UVerticalBox* StopsBox = nullptr;
	UPROPERTY()
	UHorizontalBox* MyLocationRow = nullptr;
	UPROPERTY()
	UTextBlock* MinutesText = nullptr;
	UPROPERTY()
	UTextBlock* MetresText = nullptr;
	UPROPERTY()
	UTextBlock* ErrorText = nullptr;
	UPROPERTY()
	UTextBlock* JourneyText = nullptr;
	UPROPERTY()
	UTextBlock* ViaText = nullptr;
	UPROPERTY()
	UWidget* StatsBox = nullptr;
	UPROPERTY()
	UHorizontalBox* TransportBox = nullptr;
	UPROPERTY()
	UVerticalBox* OptionsSection = nullptr;
	UPROPERTY()
	UVerticalBox* StepsSection = nullptr;
	UPROPERTY()
	UWrapBox* OptionBox = nullptr;
	UPROPERTY()
	UVerticalBox* StepBox = nullptr;
	UPROPERTY()
	UButton* StartButton = nullptr;

	TArray<FMobilePlace> Places;
	TArray<FMobileBuilding> Buildings;
	FMobileRouteRequest Request;
	FMobileRoute Route;
	TArray<FMobileRoute> Legs;
	bool bPending = false;
	bool bAddingStop = false;
	bool bOptionsOpen = false;
	bool bStepsOpen = false;
	int32 ShownSteps = 0;

	TFunction<void()> OnBack;
	TFunction<void(const FMobileRouteRequest&)> OnRequestChanged;
	TFunction<void()> OnStart;
};
