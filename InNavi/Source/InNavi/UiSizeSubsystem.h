// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UiSizeSubsystem.generated.h"

class IInputProcessor;

// Holds the user's UI size (see UiSize.h), applies it to the whole UI, keeps it between runs, and listens for Ctrl + / Ctrl - / Ctrl 0.
//
// The size is applied through UUserInterfaceSettings::ApplicationScale, the multiplier the engine puts on top of UIScaleCurve for every
// scale it computes, so every screen (start screen, both editors, dialogs) follows it and no widget has to know about it. It is saved in
// GameUserSettings.ini (section InNavi.Display, key UiSizePercent). Windows only: the phone app keeps the fixed scale it has.
UCLASS()
class INNAVI_API UUiSizeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// The subsystem of the game the object belongs to, or null (no game instance, or not on Windows).
	static UUiSizeSubsystem* Get(const UObject* WorldContext);

	int32 GetPercent() const { return Percent; }
	// Sets and applies a size (brought into range) and, with bSave, keeps it for the next run.
	void SetPercent(int32 NewPercent, bool bSave = true);
	void Larger();
	void Smaller();
	void Reset();

	// A test must not write the user's own setting.
	void SetSavingEnabledForTests(bool bEnabled) { bSaveEnabled = bEnabled; }

private:
	void ApplyToSettings() const;

	int32 Percent = 100;
	bool bSaveEnabled = true;
	// What ApplicationScale was before this subsystem touched it, put back when the game ends (in the editor it is the project's setting).
	float OriginalApplicationScale = 1.f;
	TSharedPtr<IInputProcessor> KeyProcessor;
};
