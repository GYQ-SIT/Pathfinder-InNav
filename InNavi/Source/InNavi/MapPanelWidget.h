#pragma once

#include "CoreMinimal.h"
#include "DockPanelContent.h"
#include "Types/SlateEnums.h"
#include "MapPanelWidget.generated.h"

class UButton;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class UTextBlock;

// The Map dock panel, shared by the interior editor and the exterior screen.
//
// Both screens show the Roads section: Hide All, one check box per road category and "Campus Only". The exterior
// screen also turns on the Buildings section (SetShowBuildingFilters): the campus / non-campus switches and the
// building search with its floor-count filter and result list. The interior editor has no outside buildings to
// filter, so it keeps the Roads section only.
UCLASS()
class INNAVI_API UMapPanelWidget : public UDockPanelContentWidget
{
	GENERATED_BODY()

public:
	// Adds the Buildings section. Has to be called before the panel is first shown, because the content is built then.
	void SetShowBuildingFilters(bool bShow) { bShowBuildingFilters = bShow; }

	// The width the building controls are sized to (the dock group's width minus the panel padding).
	void SetContentWidth(float Width);

	// Brings the building controls in line with the filters the controller currently holds.
	void Refresh();

	// Puts the controller in the state the check boxes start in: roads and POIs limited to the campus, buildings
	// outside the campus hidden. The check boxes alone do not do that, because they are created before a controller
	// is available.
	void ApplyDefaultsToController();

	// Rebuilds the result list and the count line from the controller's current building results.
	void RefreshBuildingFilterDisplay();

	// Same, but only when the controller's results changed since the last time - cheap enough to call every tick.
	void RefreshBuildingFilterDisplayIfChanged();

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
	void HandleBuildingFloorFilterChanged(FString Item, ESelectInfo::Type Type);
	UFUNCTION()
	void HandleFilteredBuildingChanged(FString Item, ESelectInfo::Type Type);
	UFUNCTION()
	void OnResetBuildingFiltersClicked();
	UFUNCTION()
	UWidget* GenerateBuildingFilterItem(FString Item);

	void ApplyBuildingFilters();
	void AddSectionHeader(class UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label);
	// A control inside a SizeBox whose width follows the panel.
	class USizeBox* WrapInPanelWidth(UWidget* Control);

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
	UComboBoxString* BuildingFloorFilter = nullptr;
	UPROPERTY()
	UComboBoxString* FilteredBuildingComboBox = nullptr;
	UPROPERTY()
	UTextBlock* BuildingFilterResultsText = nullptr;
	UPROPERTY()
	UButton* ResetBuildingFiltersButton = nullptr;

	TMap<FString, FString> CachedBuildingOptions;
	bool bShowBuildingFilters = false;
	// True while the controls are being set from the controller, so their change events do not write back.
	bool bUpdatingBuildingFilters = false;
	uint64 DisplayedBuildingResultsRevision = MAX_uint64;
	float ContentWidth = 220.f;
};
