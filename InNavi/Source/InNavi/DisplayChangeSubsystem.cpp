// Fill out your copyright notice in the Description page of Project Settings.

#include "DisplayChangeSubsystem.h"
#include "DisplayFit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "GenericPlatform/GenericApplication.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

namespace
{
	// How long the screen has to be quiet before the window is resized: Windows reports a resolution change in several messages, and the
	// desktop is still rearranging itself for a moment after the first one.
	constexpr float SettleSeconds = 0.75f;
	// The check at start-up waits for the game window to exist.
	constexpr float StartupCheckSeconds = 2.f;
}

bool UDisplayChangeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if PLATFORM_WINDOWS
	return Super::ShouldCreateSubsystem(Outer) && !GIsEditor && !IsRunningCommandlet() && !IsRunningDedicatedServer();
#else
	(void)Outer;
	return false;
#endif
}

void UDisplayChangeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if PLATFORM_WINDOWS
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPlatformApplication().IsValid())
	{
		MetricsChangedHandle = FSlateApplication::Get().GetPlatformApplication()->OnDisplayMetricsChanged().AddUObject(
			this, &UDisplayChangeSubsystem::HandleDisplayMetricsChanged);
	}
	if (UGameInstance* Instance = GetGameInstance())
	{
		Instance->GetTimerManager().SetTimer(ReconcileTimer, this, &UDisplayChangeSubsystem::ReconcileWithDisplay, StartupCheckSeconds, false);
	}
#endif
}

void UDisplayChangeSubsystem::Deinitialize()
{
#if PLATFORM_WINDOWS
	if (MetricsChangedHandle.IsValid() && FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPlatformApplication().IsValid())
	{
		FSlateApplication::Get().GetPlatformApplication()->OnDisplayMetricsChanged().Remove(MetricsChangedHandle);
	}
	MetricsChangedHandle.Reset();
	if (UGameInstance* Instance = GetGameInstance())
	{
		Instance->GetTimerManager().ClearTimer(ReconcileTimer);
	}
#endif
	Super::Deinitialize();
}

void UDisplayChangeSubsystem::HandleDisplayMetricsChanged(const FDisplayMetrics& /*NewMetrics*/)
{
	// A burst of messages restarts the wait, so the window is resized once, after the screen has settled.
	if (UGameInstance* Instance = GetGameInstance())
	{
		Instance->GetTimerManager().SetTimer(ReconcileTimer, this, &UDisplayChangeSubsystem::ReconcileWithDisplay, SettleSeconds, false);
	}
}

void UDisplayChangeSubsystem::ReconcileWithDisplay()
{
	UGameInstance* Instance = GetGameInstance();
	UGameViewportClient* Viewport = Instance ? Instance->GetGameViewportClient() : nullptr;
	const TSharedPtr<SWindow> Window = Viewport ? Viewport->GetWindow() : nullptr;
	UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!Window.IsValid() || !Settings)
	{
		return;
	}

	// The monitor the window is on now: the one that holds the middle of the window, else the primary one.
	FDisplayMetrics Metrics;
	FDisplayMetrics::RebuildDisplayMetrics(Metrics);
	const FVector2D WindowPosition = Window->GetPositionInScreen();
	const FVector2D WindowCentre = WindowPosition + Window->GetSizeInScreen() * 0.5f;

	FIntPoint Monitor(Metrics.PrimaryDisplayWidth, Metrics.PrimaryDisplayHeight);
	FPlatformRect MonitorRect = { 0, 0, Metrics.PrimaryDisplayWidth, Metrics.PrimaryDisplayHeight };
	FPlatformRect WorkRect = Metrics.PrimaryDisplayWorkAreaRect;
	for (const FMonitorInfo& Info : Metrics.MonitorInfo)
	{
		if (WindowCentre.X >= Info.DisplayRect.Left && WindowCentre.X < Info.DisplayRect.Right
			&& WindowCentre.Y >= Info.DisplayRect.Top && WindowCentre.Y < Info.DisplayRect.Bottom)
		{
			MonitorRect = Info.DisplayRect;
			WorkRect = Info.WorkArea;
			Monitor = FIntPoint(Info.DisplayRect.Right - Info.DisplayRect.Left, Info.DisplayRect.Bottom - Info.DisplayRect.Top);
			break;
		}
	}
	const FIntPoint WorkArea(WorkRect.Right - WorkRect.Left, WorkRect.Bottom - WorkRect.Top);
	const FIntPoint ClientSize(FMath::RoundToInt(Window->GetClientSizeInScreen().X), FMath::RoundToInt(Window->GetClientSizeInScreen().Y));
	const EWindowMode::Type Mode = Window->GetWindowMode();

	const DisplayFit::FDecision Decision = DisplayFit::Decide(Mode, ClientSize, Monitor, WorkArea);
	UE_LOG(LogTemp, Display, TEXT("[Display] mode %d, window %dx%d, monitor %dx%d, work area %dx%d: %s"), static_cast<int32>(Mode), ClientSize.X, ClientSize.Y,
		Monitor.X, Monitor.Y, WorkArea.X, WorkArea.Y, Decision.bApply ? *FString::Printf(TEXT("resize to %dx%d"), Decision.Size.X, Decision.Size.Y) : TEXT("nothing to do"));
	if (!Decision.bApply)
	{
		return;
	}

	Settings->SetScreenResolution(Decision.Size);
	Settings->SetFullscreenMode(Mode);
	Settings->ApplyResolutionSettings(false);

	if (Decision.bRecenter)
	{
		Window->MoveWindowTo(FVector2D(WorkRect.Left + (WorkArea.X - Decision.Size.X) * 0.5f, WorkRect.Top + (WorkArea.Y - Decision.Size.Y) * 0.5f));
	}
}
