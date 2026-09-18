#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NavigationRoutePlanner.h"
#include "NavigationPanelWidget.generated.h"

class ABuildingGenerator;
class AInteriorGenerator;
class UBorder;
class UButton;
class UTextBlock;

UENUM(BlueprintType)
enum class ENavigationSelectionMode : uint8
{
	None,
	Start,
	Destination
};

// Phase 1 navigation controls. The selection state is independent of interior edit modes.
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

	// The panel's border, rather than its full-screen canvas, owns pointer input.
	bool IsPointerOverPanel() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UFUNCTION()
	void OnSetStartClicked();

	UFUNCTION()
	void OnSetDestinationClicked();

	UFUNCTION()
	void OnClearRouteClicked();
	UFUNCTION()
	void OnFindRouteClicked();

	void SetSelectionMode(ENavigationSelectionMode NewMode);
	void RefreshControls();

	UPROPERTY()
	UBorder* PanelBorder = nullptr;

	UPROPERTY()
	UButton* SetStartButton = nullptr;

	UPROPERTY()
	UButton* SetDestinationButton = nullptr;

	UPROPERTY()
	UButton* FindRouteButton = nullptr;

	UPROPERTY()
	UButton* ClearRouteButton = nullptr;

	UPROPERTY()
	UTextBlock* StatusText = nullptr;
	UPROPERTY()
	UTextBlock* RouteDetailsText = nullptr;
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

	ENavigationSelectionMode SelectionMode = ENavigationSelectionMode::None;
};
