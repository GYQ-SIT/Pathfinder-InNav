// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FloorListEntryWidget.generated.h"

class UButton;
class USizeBox;
class UTextBlock;

// Broadcast with the floor number this row represents - UButton::OnClicked itself carries no
// identifying data, so this is how UInteriorScreenWidget's floor list (see RefreshFloorList)
// finds out which row was clicked without needing one hand-written handler per floor.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFloorEntryClicked, int32, FloorNumber);

// One row in UInteriorScreenWidget's floor list - a single "Level N" button, highlighted when it's
// the active floor. Built the same way as the other pure-C++ widgets (RebuildWidget()). Call
// SetFloorNumber right after creating one, before adding it to the viewport/parent.
UCLASS()
class INNAVI_API UFloorListEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFloorListEntryWidget(const FObjectInitializer& ObjectInitializer);

	void SetFloorNumber(int32 InFloorNumber, bool bInIsActive);

	// Lets UInteriorScreenWidget::RefreshSidebarContentWidths keep this row's width in sync with the
	// right sidebar bar as it's dragged - same "not realized yet" caveat as SetFloorNumber, so this
	// is also safe to call before RebuildWidget has run.
	void SetWidth(float NewWidth);

	UPROPERTY(BlueprintAssignable, Category = "FloorListEntry")
	FOnFloorEntryClicked OnEntryClicked;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleClicked();

	// Applies FloorNumber/bIsActive to Button/Label - called from RebuildWidget (so the very first
	// build already shows the right text/color) and again from SetFloorNumber if the widget has
	// already been realized by then. SetFloorNumber runs right after CreateWidget, before this
	// widget is added to any parent - RebuildWidget (and so Button/Label) doesn't exist yet at that
	// point, which is what left every row blank until this split.
	void ApplyVisuals();

	UPROPERTY()
	UButton* Button = nullptr;

	UPROPERTY()
	UTextBlock* Label = nullptr;

	UPROPERTY()
	USizeBox* SizeBox = nullptr;

	int32 FloorNumber = 0;
	bool bIsActive = false;
	float Width = 150.f;
};
