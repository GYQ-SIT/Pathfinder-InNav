// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class UWidget;
class UWidgetTree;

// The hover hint every button of the UI shows (the Toolbar's tools, Save, the camera buttons ...): a light card with a soft shadow, the text in
// black, centred. The label says what the control does and, in square brackets at the end, its shortcut: "Save [Ctrl+S]", "Snapping ON [N]".
// Built here once so that every panel gives the same hint, instead of each screen having a copy (UInteriorScreenWidget::MakeToolTipWidget
// calls this). Give the result to UWidget::SetToolTip; the widgets are constructed in Tree.
namespace UiToolTip
{
	// The card is as wide as its text up to this (in pixels); a longer text wraps there, centred.
	inline constexpr float MaxWidth = 640.f;

	INNAVI_API UWidget* Make(UWidgetTree* Tree, const FString& Label);
}
