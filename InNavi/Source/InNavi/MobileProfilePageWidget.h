#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobilePreferences.h"
#include "MobileProfilePageWidget.generated.h"

class UBorder;
class USlider;
class UTextBlock;

// The Profile tab: "Make it yours", the visitor card (no account), Appearance (light or night theme, larger text), Sound (the volume of the
// app's sounds), About, and a button that resets the saved places and the preferences. It shows the preferences it is given and tells whoever made it what the person did; the
// screen keeps and saves them.
UCLASS()
class INNAVI_API UMobileProfilePageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// What the Appearance controls show.
	void SetPreferences(const FMobilePreferences& InPreferences);
	// The line that closes the page (the source's note that its data is simulated); nothing when empty.
	void SetNote(const FString& InNote);

	void SetOnThemeChosen(TFunction<void(EMobileTheme)> InCallback) { OnThemeChosen = MoveTemp(InCallback); }
	void SetOnLargerTextToggled(TFunction<void(bool)> InCallback) { OnLargerTextToggled = MoveTemp(InCallback); }
	void SetOnReset(TFunction<void()> InCallback) { OnReset = MoveTemp(InCallback); }
	// The volume was moved: Volume is 0 to 1; bFinal is false while the slider is being dragged and true when the finger is lifted (or a
	// tap sets it), which is when the screen keeps it and plays a sound to hear it by.
	void SetOnVolumeChanged(TFunction<void(float, bool)> InCallback) { OnVolumeChanged = MoveTemp(InCallback); }

	// What a tap on a theme, on Larger text, and on Reset does.
	void ChooseTheme(EMobileTheme Theme);
	void ToggleLargerText();
	void TapReset();
	// What the volume slider does (tests call it too).
	void ChooseVolume(float NewVolume, bool bFinal);
	float GetShownVolume() const { return Volume; }

	EMobileTheme GetShownTheme() const { return Theme; }
	bool IsLargerTextShown() const { return bLargerText; }
	// The line under the controls that says what the last tap did ("Appearance updated"); empty before any.
	FString GetStatusString() const;
	FString GetNoteString() const;
	// Shows a line under the controls (the screen puts back the one the person was reading after it builds the page again).
	void ShowStatus(const FString& Text) { SetStatus(Text); }
	UWidget* FindNamedWidget(const FName& Name) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void Refresh();
	void SetStatus(const FString& Text);

	UFUNCTION()
	void HandleVolumeSlider(float NewValue);
	UFUNCTION()
	void HandleVolumeReleased();

	UPROPERTY()
	UBorder* LightPill = nullptr;
	UPROPERTY()
	UBorder* NightPill = nullptr;
	UPROPERTY()
	UTextBlock* LightLabel = nullptr;
	UPROPERTY()
	UTextBlock* NightLabel = nullptr;
	UPROPERTY()
	UBorder* LargerBox = nullptr;
	UPROPERTY()
	UBorder* LargerTick = nullptr;
	UPROPERTY()
	USlider* VolumeSlider = nullptr;
	UPROPERTY()
	UTextBlock* VolumeText = nullptr;
	UPROPERTY()
	UTextBlock* StatusText = nullptr;
	UPROPERTY()
	UTextBlock* NoteText = nullptr;

	EMobileTheme Theme = EMobileTheme::Light;
	bool bLargerText = false;
	float Volume = 1.f;
	bool bRefreshing = false;
	FString Note;

	TFunction<void(EMobileTheme)> OnThemeChosen;
	TFunction<void(bool)> OnLargerTextToggled;
	TFunction<void()> OnReset;
	TFunction<void(float, bool)> OnVolumeChanged;
};
