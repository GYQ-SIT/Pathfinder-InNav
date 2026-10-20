// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LayerRowWidget.generated.h"

class UButton;
class UHorizontalBox;
class USizeBox;
class UTextBlock;

// Broadcast with the row's own RowKey - UButton::OnClicked itself carries no identifying data, so
// this is how UInteriorScreenWidget's Layers panel (see RefreshLayersList) finds out which row was
// clicked without needing one hand-written handler per row. RowKey is opaque to this widget: for a
// category row it's "category:Walls"/"category:Boundary"/etc, for an item row it's
// "item:Walls:<RoomId>"/"item:PathNodes:<NodeId>"/"item:IONodes:<NodeId>" - InteriorScreenWidget
// parses it, this widget just echoes back whatever Setup gave it.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLayerRowKeyClicked, FString, RowKey);

// One row in UInteriorScreenWidget's Layers panel - either a category header (Boundary/Walls/
// Path Nodes/I/O Nodes, with an expand arrow) or an individual item under one (indented, no expand
// arrow) - same class either way, built the same way as the other pure-C++ row widgets
// (RebuildWidget()). Call Setup right after creating one, before adding it to a parent.
UCLASS()
class INNAVI_API ULayerRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	ULayerRowWidget(const FObjectInitializer& ObjectInitializer);

	void Setup(const FString& InRowKey, const FString& InLabel, float IndentPx, bool bInShowExpandArrow, bool bInIsExpanded, bool bInIsVisible);

	// Lets UInteriorScreenWidget::RefreshDockPanelContentWidths keep this row's width in sync with
	// the Layers panel as it's dragged - same "not realized yet" caveat as Setup, so this is also
	// safe to call before RebuildWidget has run.
	void SetWidth(float NewWidth);

	// Turns this row into a search result: its label can be clicked (OnLabelClicked) and the visibility dot,
	// which means nothing for a result, is hidden.
	void SetSearchResult(bool bInSearchResult);

	const FString& GetRowKeyForTests() const { return RowKey; }

	UPROPERTY(BlueprintAssignable, Category = "LayerRow")
	FOnLayerRowKeyClicked OnExpandClicked;

	UPROPERTY(BlueprintAssignable, Category = "LayerRow")
	FOnLayerRowKeyClicked OnVisibilityClicked;

	// Only fires for search-result rows (see SetSearchResult).
	UPROPERTY(BlueprintAssignable, Category = "LayerRow")
	FOnLayerRowKeyClicked OnLabelClicked;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	UFUNCTION()
	void HandleExpandClicked();

	UFUNCTION()
	void HandleVisibilityClicked();

	// Bound alongside both buttons' own click handlers so clicking either plays the same click sound
	// every other button in the app does - see ANavPlayerController::PlayButtonClickSound.
	UFUNCTION()
	void PlayButtonClickSound();

	// Applies RowKey/LabelText/Indent/bShowExpandArrow/bIsExpanded/bIsVisible/Width to the actual
	// widgets - called from RebuildWidget (so the very first build already shows the right state)
	// and again from Setup/SetWidth if the widget has already been realized by then.
	void ApplyVisuals();

	UPROPERTY()
	USizeBox* RowSizeBox = nullptr;

	UPROPERTY()
	UHorizontalBox* Row = nullptr;

	UPROPERTY()
	USizeBox* IndentSizeBox = nullptr;

	UPROPERTY()
	UButton* ExpandButton = nullptr;

	UPROPERTY()
	UTextBlock* ExpandButtonText = nullptr;

	UPROPERTY()
	UTextBlock* Label = nullptr;

	UPROPERTY()
	UButton* VisibilityButton = nullptr;

	UPROPERTY()
	UTextBlock* VisibilityButtonText = nullptr;

	FString RowKey;
	FString LabelText;
	float Indent = 0.f;
	bool bShowExpandArrow = false;
	bool bIsExpanded = false;
	bool bIsVisible = true;
	bool bSearchResult = false;
	float Width = 200.f;
};
