#include "DesktopRenderingSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "RenderUtils.h"

namespace
{
	const TCHAR* SettingsSection = TEXT("InNavi.DesktopRendering");
	struct FRenderSetting { const TCHAR* Name; int32 Lightweight; };
	const FRenderSetting RenderSettings[] = {
		{ TEXT("r.DynamicGlobalIlluminationMethod"), 0 },
		{ TEXT("r.Lumen.DiffuseIndirect.Allow"), 0 },
		{ TEXT("r.ReflectionMethod"), 2 }, // Screen-space reflections.
		{ TEXT("r.Lumen.Reflections.Allow"), 0 },
		{ TEXT("r.Shadow.Virtual.Enable"), 0 }, // Keep conventional shadows.
		{ TEXT("r.RayTracing.Enable"), 0 },
		{ TEXT("r.DistanceFieldAO"), 0 },
		{ TEXT("r.DistanceFieldShadowing"), 0 }
	};
}

void UDesktopRenderingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if PLATFORM_DESKTOP
	bAvailable = !GIsEditor && !FParse::Param(FCommandLine::Get(), TEXT("MobileApp"));
#endif
	if (!bAvailable) return;
	// Apply at world BeginPlay, after engine scalability/user settings have finished loading.
	bLightweight = ResolveLightweightPreference(GConfig->Find(GGameUserSettingsIni), FCommandLine::Get());
}

bool UDesktopRenderingSubsystem::ResolveLightweightPreference(const FConfigFile* Settings, const TCHAR* CommandLine)
{
	bool bUseLightweight = false;
	if (Settings) Settings->GetBool(SettingsSection, TEXT("Lightweight"), bUseLightweight);
	FString Override;
	if (FParse::Value(CommandLine, TEXT("DesktopRenderPreset="), Override))
	{
		if (Override.Equals(TEXT("Standard"), ESearchCase::IgnoreCase)) bUseLightweight = false;
		else if (Override.Equals(TEXT("Lightweight"), ESearchCase::IgnoreCase)) bUseLightweight = true;
	}
	return bUseLightweight;
}

void UDesktopRenderingSubsystem::SetLightweight(bool bEnabled, bool bPersist)
{
	if (!bAvailable) return;
	for (const FRenderSetting& Setting : RenderSettings)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Setting.Name))
		{
			if (Variable->TestFlags(ECVF_ReadOnly)) continue;
			// UE only consults this switch after initialization when on-demand RT was enabled at startup.
			if (FCString::Strcmp(Setting.Name, TEXT("r.RayTracing.Enable")) == 0 && !IsRayTracingEnableOnDemandSupported()) continue;
			if (bEnabled && !OriginalSettings.Contains(Setting.Name)) OriginalSettings.Add(Setting.Name, Variable->GetInt());
			const int32* Original = OriginalSettings.Find(Setting.Name);
			if (!bEnabled && !Original) continue; // Standard startup leaves the engine's settings alone.
			const int32 Desired = bEnabled ? Setting.Lightweight : *Original;
			if (Variable->GetInt() != Desired) Variable->SetWithCurrentPriority(Desired);
		}
	}
	bLightweight = bEnabled;
	if (bPersist)
	{
		GConfig->SetBool(SettingsSection, TEXT("Lightweight"), bEnabled, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UDesktopRenderingSubsystem::SetOpaqueScreen(UUserWidget* Screen, bool bVisible)
{
	UpdateOpaqueScreen(Screen, bVisible, GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr);
}

void UDesktopRenderingSubsystem::UpdateOpaqueScreen(UUserWidget* Screen, bool bVisible, UGameViewportClient* Viewport)
{
	if (!bAvailable || !Screen || FParse::Param(FCommandLine::Get(), TEXT("DesktopRenderBehindUI"))) return;
	for (auto It = OpaqueScreens.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
	if (bVisible)
	{
		OpaqueScreens.Add(Screen);
		if (!PausedViewport.IsValid() && Viewport)
		{
			PausedViewport = Viewport;
			bPreviousWorldRenderingDisabled = Viewport->bDisableWorldRendering;
			Viewport->bDisableWorldRendering = true;
		}
	}
	else
	{
		OpaqueScreens.Remove(Screen);
		if (OpaqueScreens.IsEmpty()) RestoreWorldRendering();
	}
}

void UDesktopRenderingSubsystem::RestoreWorldRendering()
{
	if (UGameViewportClient* Viewport = PausedViewport.Get()) Viewport->bDisableWorldRendering = bPreviousWorldRenderingDisabled;
	PausedViewport.Reset();
}

void UDesktopRenderingSubsystem::Deinitialize()
{
	RestoreWorldRendering();
	OpaqueScreens.Reset();
	for (const auto& Setting : OriginalSettings)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Setting.Key))
		{
			if (!Variable->TestFlags(ECVF_ReadOnly) && Variable->GetInt() != Setting.Value) Variable->SetWithCurrentPriority(Setting.Value);
		}
	}
	OriginalSettings.Reset();
	Super::Deinitialize();
}
