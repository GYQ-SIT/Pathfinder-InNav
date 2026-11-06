#pragma once

#include "CoreMinimal.h"
#include "DockPanelContent.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Types/SlateEnums.h"
#include "Widgets/Views/SListView.h"
#include "NavigationDockPanelWidget.generated.h"

class UBorder;
class UEditableTextBox;
class UHorizontalBox;
class UProgressBar;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UNavigationDockPanelWidget;

// A button of a row of the places list: the number that selects the row, or the crosshair that picks the place on the map. It tells
// the panel which slot it is in; what the slot shows is the panel's business.
UCLASS()
class INNAVI_API UNavigationPlaceButton : public UButton
{
	GENERATED_BODY()

public:
	void Setup(UNavigationDockPanelWidget* InPanel, int32 InSlot, bool bInPick);

private:
	UFUNCTION()
	void HandleClicked();

	TWeakObjectPtr<UNavigationDockPanelWidget> Panel;
	int32 SlotIndex = INDEX_NONE;
	bool bPick = false;
};

// The dropdown of a row of the places list.
UCLASS()
class INNAVI_API UNavigationPlaceCombo : public UComboBoxString
{
	GENERATED_BODY()

public:
	void Setup(UNavigationDockPanelWidget* InPanel, int32 InSlot);
	void SetSearchText(const FString& Query);
	int32 GetFilteredOptionCount() const { return FilteredOptions.Num(); }
	FString GetFilteredOption(int32 Index) const { return FilteredOptions.IsValidIndex(Index) ? *FilteredOptions[Index] : FString(); }
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	friend struct FEditorInteractionTestAccess;
	void SearchChanged(const FText& Text);
	void SearchCommitted(const FText& Text, ETextCommit::Type CommitType);
	FReply HandleSearchKey(const FGeometry& Geometry, const FKeyEvent& Event);
	void ChooseSearchResult(TSharedPtr<FString> Item, ESelectInfo::Type SelectionType);
	TSharedRef<ITableRow> GenerateSearchRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner);

	TWeakObjectPtr<UNavigationDockPanelWidget> Panel;
	int32 SlotIndex = INDEX_NONE;
	TArray<TSharedPtr<FString>> FilteredOptions;
	TSharedPtr<class SEditableTextBox> SearchField;
	FEditableTextBoxStyle SearchStyle;
	TSharedPtr<SListView<TSharedPtr<FString>>> SearchResults;
};

// The Navigation dock panel, shared by the interior editor and the exterior screen. It has two views. Plan is where the route is
// set: one list of places in the order they are visited (the first is the start, the last the destination, any between are stops),
// each picked from a list or by clicking the map, and moved, removed or reversed; then Find Route. Go is where it is gone through,
// one step at a time, with the route in a line above, once there is one.
//
// It holds no navigation state of its own. That lives in UNavigationPanelWidget (ANavPlayerController::GetNavigationPanel); this panel
// only shows it and forwards what the user does. The screen that owns the panel calls RefreshDisplay whenever the navigation state
// changes and RefreshRoomDropdowns whenever the lists of places could have changed.
UCLASS()
class INNAVI_API UNavigationDockPanelWidget : public UDockPanelContentWidget
{
	GENERATED_BODY()

public:
	// The width the dock gives the panel's controls.
	void SetContentWidth(float Width);

	// Repaints the places, the buttons, the status and the steps from the navigation state.
	void RefreshDisplay();

	// Refills the dropdowns of the places (and the search result line).
	void RefreshRoomDropdowns();

	// The content root, which the pointer-over-panel test of the navigation state can ask about.
	UBorder* GetRootBorder() const { return RootBorder; }

	// What the buttons and dropdowns of the rows tell the panel (see UNavigationPlaceButton, UNavigationPlaceCombo).
	void HandleSlotButton(int32 SlotIndex, bool bPick);
	void HandleSlotChosen(int32 SlotIndex, const FString& Item, ESelectInfo::Type SelectionType);
	TArray<FString> SearchPlaces(const FString& Query) const;

	// How many rows the list can show: the start, the most stops, the destination, and a row to add a place in (after the destination).
	static constexpr int32 MaxSlots = 11;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// While a route is being searched for, keeps the progress bar and the status text up to date.
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION()
	void OnPlanTabClicked();
	UFUNCTION()
	void OnGoTabClicked();
	UFUNCTION()
	void OnEditRouteClicked();
	UFUNCTION()
	void OnMoveUpClicked();
	UFUNCTION()
	void OnMoveDownClicked();
	UFUNCTION()
	void OnRemoveClicked();
	UFUNCTION()
	void OnAddStopClicked();
	UFUNCTION()
	void OnReverseClicked();
	UFUNCTION()
	void OnFindRouteClicked();
	UFUNCTION()
	void OnCancelFindRouteClicked();
	UFUNCTION()
	void OnClearRouteClicked();
	UFUNCTION()
	void OnToggleRouteClicked();
	UFUNCTION()
	void OnNextStepClicked();
	UFUNCTION()
	void OnPreviousStepClicked();
	UFUNCTION()
	void OnStepModeClicked();
	UFUNCTION()
	UWidget* HandleGenerateComboBoxItem(FString Item);

	// Which place each row of the list shows: a row of the route, PendingRow for the row a stop is being added in, or INDEX_NONE for a
	// row not in use. The rows are widgets made once; what they show is set here.
	static constexpr int32 PendingRow = -2;
	void AssignSlots(const class UNavigationPanelWidget& Nav);
	void UpdateView();

	UPROPERTY()
	UBorder* RootBorder = nullptr;
	UPROPERTY()
	UButton* PlanTabButton = nullptr;
	UPROPERTY()
	UButton* GoTabButton = nullptr;
	UPROPERTY()
	UVerticalBox* PlanBox = nullptr;
	UPROPERTY()
	UVerticalBox* GoBox = nullptr;
	UPROPERTY()
	USizeBox* TabsSize = nullptr;

	// ---- Plan ----
	UPROPERTY()
	UTextBlock* ContextText = nullptr;
	UPROPERTY()
	TArray<TObjectPtr<USizeBox>> SlotSizes;
	UPROPERTY()
	TArray<TObjectPtr<UNavigationPlaceButton>> BadgeButtons;
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> BadgeTexts;
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> RoleTexts;
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> FieldBorders;
	UPROPERTY()
	TArray<TObjectPtr<USizeBox>> ComboSizes;
	UPROPERTY()
	TArray<TObjectPtr<UNavigationPlaceCombo>> PlaceCombos;
	UPROPERTY()
	TArray<TObjectPtr<UNavigationPlaceButton>> PickButtons;
	UPROPERTY()
	USizeBox* ActionsSize = nullptr;
	UPROPERTY()
	UButton* MoveUpButton = nullptr;
	UPROPERTY()
	UButton* MoveDownButton = nullptr;
	UPROPERTY()
	UButton* RemoveButton = nullptr;
	UPROPERTY()
	UButton* AddStopButton = nullptr;
	UPROPERTY()
	UTextBlock* AddStopButtonLabel = nullptr;
	UPROPERTY()
	UButton* ReverseButton = nullptr;
	UPROPERTY()
	USizeBox* AddRowSize = nullptr;
	UPROPERTY()
	UButton* FindRouteButton = nullptr;
	// Shown only while a route is being searched for.
	UPROPERTY()
	UProgressBar* FindRouteProgress = nullptr;
	UPROPERTY()
	UButton* CancelFindRouteButton = nullptr;
	UPROPERTY()
	UTextBlock* HintText = nullptr;
	UPROPERTY()
	UTextBlock* StatusText = nullptr;
	UPROPERTY()
	UTextBlock* RouteDetailsText = nullptr;
	UPROPERTY()
	UButton* ClearRouteButton = nullptr;
	UPROPERTY()
	UButton* ToggleRouteButton = nullptr;
	UPROPERTY()
	UTextBlock* ToggleRouteButtonLabel = nullptr;

	// ---- Go ----
	// The route in a line: a number for each place, in the colour it has on the map.
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> OverviewBadges;
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> OverviewTexts;
	UPROPERTY()
	UTextBlock* OverviewInfoText = nullptr;
	UPROPERTY()
	UProgressBar* StepProgress = nullptr;
	// The steps, one per line (all of them) or the one being gone through and where it is in the route.
	UPROPERTY()
	UVerticalBox* RouteStepsBox = nullptr;
	FString ShownRouteSteps;
	UPROPERTY()
	USizeBox* StepButtonsSize = nullptr;
	UPROPERTY()
	UButton* PreviousStepButton = nullptr;
	UPROPERTY()
	UButton* NextStepButton = nullptr;
	UPROPERTY()
	UTextBlock* NextStepButtonLabel = nullptr;
	UPROPERTY()
	UButton* StepModeButton = nullptr;
	UPROPERTY()
	UTextBlock* StepModeButtonLabel = nullptr;
	UPROPERTY()
	UButton* EditRouteButton = nullptr;

	// What the panel itself keeps (nothing of the route): the view shown, the row chosen for moving or removing, whether a row to add a
	// stop in is open, and what the rows showed last so the dropdowns are refilled only when that changes.
	bool bGoView = false;
	bool bHadRoute = false;
	bool bAddingStop = false;
	int32 SelectedRow = INDEX_NONE;
	TArray<int32> SlotRow;
	FString ShownPlaces;

	// Dropdown rows are kept alive while a dropdown can still show them.
	UPROPERTY()
	TArray<TObjectPtr<UWidget>> RetainedComboItems;

	float ContentWidth = 150.f;
};
