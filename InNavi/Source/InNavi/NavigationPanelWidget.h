#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NavigationPanelWidget.generated.h"

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

	ENavigationSelectionMode SelectionMode = ENavigationSelectionMode::None;
};
