#pragma once

#include "CoreMinimal.h"
#include "DockPanelContent.h"
#include "MapPanelWidget.generated.h"

class UButton;
class UCheckBox;
class UEditableTextBox;
class UMapBuildingRowWidget;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;

// The Map dock panel, shared by the interior editor and the exterior screen.
//
// Both screens show the road filters: Hide All, one check box per road category and "Campus Only". The exterior screen
// turns on the site overview (SetShowBuildingFilters), in the style of the design team's page: "Choose a building" with a
// search box and the list of buildings (the selected one highlighted), then two collapsible sections, Filters (floor count,
// campus / non-campus, reset) and Roads, and a note. The interior editor has no outside buildings, so it keeps the road
// check boxes only. The interior editor can turn the overview on too (the same panel and the same filters, which belong to the
// controller and are shared by both screens), and then decides what a click on a building does (SetBuildingClickHandler): open it.
UCLASS()
class INNAVI_API UMapPanelWidget : public UDockPanelContentWidget
{
	GENERATED_BODY()

public:
	// Turns on the site overview. Has to be called before the panel is first shown, because the content is built then.
	void SetShowBuildingFilters(bool bShow) { bShowBuildingFilters = bShow; }

	// Whether the roads start shown (the exterior screen) or hidden (the interior editor, where they are not wanted). Sets how
	// the check boxes start and what ApplyDefaultsToController makes the controller do. Has to be called before the panel is
	// first shown.
	void SetRoadsVisibleByDefault(bool bVisible) { bRoadsVisibleByDefault = bVisible; }

	// What a click on a building of the list does, when it is not selecting it on the campus (the exterior screen); and which building
	// the list shows as the chosen one (the controller's selected building when not set).
	void SetBuildingClickHandler(TFunction<void(const FString&)> Handler) { BuildingClickHandler = MoveTemp(Handler); }
	void SetSelectedBuildingProvider(TFunction<FString()> Provider) { SelectedBuildingProvider = MoveTemp(Provider); }

	// Whether ApplyDefaultsToController also puts the building filters in their starting state (buildings outside the campus hidden).
	// A screen that carries on from another one keeps what the filters hold, and turns this off.
	void SetBuildingVisibilityDefaults(bool bApply) { bApplyBuildingDefaults = bApply; }

	// The width the controls are sized to (the dock group's width minus the panel padding).
	void SetContentWidth(float Width);

	// Brings the building controls in line with the filters the controller currently holds.
	void Refresh();

	// Puts the controller in the state the check boxes start in: roads shown or hidden (see SetRoadsVisibleByDefault), roads
	// and POIs limited to the campus, buildings outside the campus hidden. The check boxes alone do not do that, because they are created before a controller
	// is available.
	void ApplyDefaultsToController();

	// Rebuilds the building list and the count line from the controller's current building results.
	void RefreshBuildingFilterDisplay();

	// Same, but only when the results or the selected building changed since the last time - cheap enough to call every tick.
	void RefreshBuildingFilterDisplayIfChanged();

	// Tests.
	int32 GetBuildingRowCountForTests() const { return BuildingRows.Num(); }
	const TArray<UMapBuildingRowWidget*>& GetBuildingRowsForTests() const { return BuildingRows; }
	FText GetBuildingCountTextForTests() const;
	bool AreRoadsCheckedForTests() const;
	void SetBuildingSearchForTests(const FString& Text);
	bool IsFiltersSectionOpenForTests() const { return bFiltersOpen; }
	void SetSectionOpenForTests(bool bFilters, bool bOpen);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	// One handler per road category, since UCheckBox::OnCheckStateChanged only carries the new state, not which row it
	// came from.
	UFUNCTION()
	void OnRoadHideAllCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadPrimaryCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadSecondaryCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadResidentialCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadCyclewayCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadFootwayCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadOtherCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnRoadCampusOnlyCheckChanged(bool bIsChecked);

	UFUNCTION()
	void OnBuildingCampusCheckChanged(bool bIsChecked);
	UFUNCTION()
	void OnBuildingNonCampusCheckChanged(bool bIsChecked);
	UFUNCTION()
	void HandleBuildingSearchChanged(const FText& Text);
	UFUNCTION()
	void OnResetBuildingFiltersClicked();
	UFUNCTION()
	void OnFloorAnyClicked();
	UFUNCTION()
	void OnFloorOneClicked();
	UFUNCTION()
	void OnFloorMidClicked();
	UFUNCTION()
	void OnFloorTallClicked();
	UFUNCTION()
	void OnFiltersHeaderClicked();
	UFUNCTION()
	void OnRoadsHeaderClicked();

	FString SelectedBuildingId() const;
	void SelectFloorOption(int32 Minimum, int32 Maximum);
	void RefreshFloorChips();
	void ApplyBuildingFilters();
	void SetSectionOpen(bool bFilters, bool bOpen);
	void HandleBuildingRowClicked(const FString& BuildingId);

	// A header button that opens and closes the section under it; Body is the box the section's controls go into.
	UVerticalBox* AddCollapsibleSection(UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Title, UButton*& OutHeader, UTextBlock*& OutSign, USizeBox*& OutHeaderBox);
	void AddRoadRows(UVerticalBox* Stack);
	// A control inside a SizeBox whose width follows the panel.
	USizeBox* WrapInPanelWidth(UWidget* Control);

	UPROPERTY()
	UCheckBox* RoadHideAllCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadPrimaryCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadSecondaryCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadResidentialCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadCyclewayCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadFootwayCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadOtherCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* RoadCampusOnlyCheckbox = nullptr;

	UPROPERTY()
	UCheckBox* BuildingCampusCheckbox = nullptr;
	UPROPERTY()
	UCheckBox* BuildingNonCampusCheckbox = nullptr;
	UPROPERTY()
	UEditableTextBox* BuildingSearchBox = nullptr;
	UPROPERTY()
	UScrollBox* BuildingList = nullptr;
	UPROPERTY()
	TArray<UMapBuildingRowWidget*> BuildingRows;
	UPROPERTY()
	UTextBlock* BuildingFilterResultsText = nullptr;
	UPROPERTY()
	UButton* ResetBuildingFiltersButton = nullptr;
	// The four floor-count choices (any, 1, 2-5, 6+), as a row of buttons.
	UPROPERTY()
	TArray<UButton*> FloorChips;
	UPROPERTY()
	UButton* FiltersHeader = nullptr;
	UPROPERTY()
	UTextBlock* FiltersSign = nullptr;
	UPROPERTY()
	UVerticalBox* FiltersBody = nullptr;
	UPROPERTY()
	UButton* RoadsHeader = nullptr;
	UPROPERTY()
	UTextBlock* RoadsSign = nullptr;
	UPROPERTY()
	UVerticalBox* RoadsBody = nullptr;

	TFunction<void(const FString&)> BuildingClickHandler;
	TFunction<FString()> SelectedBuildingProvider;
	bool bApplyBuildingDefaults = true;
	bool bShowBuildingFilters = false;
	bool bRoadsVisibleByDefault = true;
	// True while the controls are being set from the controller, so their change events do not write back.
	bool bUpdatingBuildingFilters = false;
	bool bFiltersOpen = false;
	bool bRoadsOpen = false;
	int32 FloorMinimum = 0;
	int32 FloorMaximum = 0;
	uint64 DisplayedBuildingResultsRevision = MAX_uint64;
	FString DisplayedSelectedBuildingId;
	float ContentWidth = 220.f;
};
