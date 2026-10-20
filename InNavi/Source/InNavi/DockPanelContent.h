#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DockPanelContent.generated.h"

class ANavPlayerController;
class UCheckBox;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;

// Base class of a dock panel's content when the same panel is shown by more than one screen (Audio, Map and
// Debug exist in both the interior editor and the exterior screen). The content is built once here instead of once
// per screen; each screen only creates the panel, registers it with its dock host (see docs/design/ui-panels-guide.md)
// and forwards the few things it owns, such as the width the dock gives the panel.
//
// A panel's content is not parented in its screen's widget tree - the dock host places it - so it cannot rely on
// the screen's own lookups. GetController finds the player controller through the screen when the panel has no
// player of its own, and FindContentWidget looks inside the panel's own widget tree.
UCLASS(Abstract)
class INNAVI_API UDockPanelContentWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// The screen this panel belongs to. Set right after the panel is created.
	void SetOwnerScreen(UUserWidget* Screen) { OwnerScreen = Screen; }

	// Finds a widget by name anywhere inside this panel's own content (tests and tooling).
	UWidget* FindContentWidget(FName Name) const;

protected:
	ANavPlayerController* GetController() const;

	// The app's standard 15pt text, same as UInteriorScreenWidget::StandardFontSize.
	static void ApplyStandardFont(UTextBlock* TextBlock);
	// Same size, with the app's dark box and white text (the engine defaults read as the same grey as the panel).
	static void ApplyStandardFont(UEditableTextBox* TextBox);

	// A check box with a label, as a row at the end of Stack. Returns the check box.
	UCheckBox* AddCheckRow(UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label, bool bChecked, float BottomPadding = 6.f);

	// Plays the same click sound every button in the app plays.
	UFUNCTION()
	void PlayButtonClickSound();

private:
	TWeakObjectPtr<UUserWidget> OwnerScreen;
};
