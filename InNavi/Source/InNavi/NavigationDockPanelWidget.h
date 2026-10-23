#pragma once

#include "CoreMinimal.h"
#include "DockPanelContent.h"
#include "Types/SlateEnums.h"
#include "NavigationDockPanelWidget.generated.h"

class UBorder;
class UButton;
class UComboBoxString;
class UEditableTextBox;
class UTextBlock;

// The Navigation dock panel, shared by the interior editor and the exterior screen: where Start and Destination are chosen
// (from a room dropdown, or by clicking the map), Find Route / Clear Route, and the route's status and directions.
//
// It holds no navigation state of its own. That lives in UNavigationPanelWidget (ANavPlayerController::GetNavigationPanel);
// this panel only shows it and forwards what the user does. The screen that owns the panel calls RefreshDisplay whenever the
// navigation state changes and RefreshRoomDropdowns whenever the lists of places could have changed.
UCLASS()
class INNAVI_API UNavigationDockPanelWidget : public UDockPanelContentWidget
{
	GENERATED_BODY()

public:
	// The width the dock gives the panel's controls.
	void SetContentWidth(float Width);

	// Repaints the buttons, the dropdowns' frames and texts, and the status and directions from the navigation state.
	void RefreshDisplay();

	// Refills the Start and Destination dropdowns (and the destination search result line).
	void RefreshRoomDropdowns();

	// The content root, which the pointer-over-panel test of the navigation state can ask about.
	UBorder* GetRootBorder() const { return RootBorder; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void OnSetStartClicked();
	UFUNCTION()
	void OnSetDestinationClicked();
	UFUNCTION()
	void OnFindRouteClicked();
	UFUNCTION()
	void OnClearRouteClicked();
	UFUNCTION()
	void OnToggleRouteClicked();
	UFUNCTION()
	void HandleStartRoomChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void HandleDestinationRoomChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void HandleSearchChanged(const FText& Text);
	UFUNCTION()
	UWidget* HandleGenerateComboBoxItem(FString Item);


	UPROPERTY()
	UBorder* RootBorder = nullptr;
	UPROPERTY()
	UTextBlock* ContextText = nullptr;
	UPROPERTY()
	UTextBlock* StartLabelText = nullptr;
	UPROPERTY()
	UTextBlock* DestinationLabelText = nullptr;
	UPROPERTY()
	UComboBoxString* StartRoomComboBox = nullptr;
	// Frame around the Start dropdown, green only when Start was set by picking from it (as opposed to clicking the map).
	UPROPERTY()
	UBorder* StartRoomBorder = nullptr;
	UPROPERTY()
	UComboBoxString* DestinationRoomComboBox = nullptr;
	UPROPERTY()
	UEditableTextBox* DestinationSearchBox = nullptr;
	UPROPERTY()
	UTextBlock* DestinationSearchResultsText = nullptr;
	// Same as StartRoomBorder, red for Destination.
	UPROPERTY()
	UBorder* DestinationRoomBorder = nullptr;
	UPROPERTY()
	UButton* SetStartButton = nullptr;
	UPROPERTY()
	UTextBlock* SetStartButtonLabel = nullptr;
	UPROPERTY()
	UButton* SetDestinationButton = nullptr;
	UPROPERTY()
	UTextBlock* SetDestinationButtonLabel = nullptr;
	UPROPERTY()
	UButton* FindRouteButton = nullptr;
	UPROPERTY()
	UButton* ClearRouteButton = nullptr;
	UPROPERTY()
	UButton* ToggleRouteButton = nullptr;
	UPROPERTY()
	UTextBlock* ToggleRouteButtonLabel = nullptr;
	UPROPERTY()
	UTextBlock* StatusText = nullptr;
	UPROPERTY()
	UTextBlock* RouteDetailsText = nullptr;

	// The extra entry each dropdown currently carries for a Start / Destination picked in another building
	// (UNavigationPanelWidget::GetStartOtherBuildingLabel), empty if none - so the lists are rebuilt only when it changes.
	FString ShownOtherBuildingStart;
	FString ShownOtherBuildingDestination;

	// Dropdown rows are kept alive while a dropdown can still show them.
	UPROPERTY()
	TArray<TObjectPtr<UWidget>> RetainedComboItems;

	float ContentWidth = 150.f;
};
