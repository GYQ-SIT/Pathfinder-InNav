// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"
#include "DisplayChangeSubsystem.generated.h"

struct FDisplayMetrics;

// Makes the program follow the screen's resolution on Windows. When the user switches the resolution in the Windows display settings the
// engine only refreshes its cached metrics and leaves the game window as it was (see DisplayFit.h), so the picture stays at the old size.
// This subsystem listens for that change, waits for it to settle, and gives the window the size DisplayFit::Decide asks for; the window
// then changes size and the UI scale (UIScaleCurve in DefaultEngine.ini) follows it. The same check runs once shortly after start, for a
// program that was last run on another resolution.
//
// Only the packaged game on Windows: not the editor (it would resize the editor's own window), not a commandlet or server, not the phone.
UCLASS()
class INNAVI_API UDisplayChangeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Looks at the window and the monitor it is on now, and resizes the window if it no longer fits the screen. Public so that it can be
	// called by hand (the same thing the display change and the start-up check do).
	void ReconcileWithDisplay();

private:
	void HandleDisplayMetricsChanged(const FDisplayMetrics& NewMetrics);

	FDelegateHandle MetricsChangedHandle;
	FTimerHandle ReconcileTimer;
};
