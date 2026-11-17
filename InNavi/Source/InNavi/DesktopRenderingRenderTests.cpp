#if WITH_DEV_AUTOMATION_TESTS

#include "DesktopRenderingSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "RenderUtils.h"

namespace DesktopRenderingRenderTests
{
	// The game/RHI must keep rendering between switches; run separately from the -nullrhi suite.
	class FTransitionCheck : public IAutomationLatentCommand
	{
	public:
		FTransitionCheck(UDesktopRenderingSubsystem* InSettings, FAutomationTestBase* InTest, bool bInOriginalPreset)
			: Settings(InSettings), Test(InTest), bOriginalPreset(bInOriginalPreset),
			  OriginalShadows(IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable"))->GetInt()),
			  OriginalRaySwitch(IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Enable"))->GetInt()),
			  bOriginalRayTracing(IsRayTracingEnabled()) {}

		virtual bool Update() override
		{
			auto* Preset = Settings.Get();
			if (!Preset) { Test->AddError(TEXT("rendering subsystem was destroyed")); return true; }
			if (FPlatformTime::Seconds() < After) return false;
			auto* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable"));
			auto* RaySwitch = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Enable"));
			if (Step == 0)
			{
				Preset->SetLightweight(true, false);
			}
			else if (Step == 1)
			{
				Test->TestEqual(TEXT("rendered Lightweight switches VSM off"), Shadows->GetInt(), 0);
				Test->TestEqual(TEXT("rendered RT state respects startup on-demand support"), IsRayTracingEnabled(), IsRayTracingEnableOnDemandSupported() ? false : bOriginalRayTracing);
				Test->TestEqual(TEXT("RT switch changes only with on-demand support"), RaySwitch->GetInt(), IsRayTracingEnableOnDemandSupported() ? 0 : OriginalRaySwitch);
				Test->AddInfo(FString::Printf(TEXT("Rendered transition: on-demand=%d, effective RT Standard=%d -> Lightweight=%d; VSM %d -> %d."), IsRayTracingEnableOnDemandSupported(), bOriginalRayTracing, IsRayTracingEnabled(), OriginalShadows, Shadows->GetInt()));
				Preset->SetLightweight(false, false);
			}
			else
			{
				Test->TestEqual(TEXT("rendered Standard restores VSM"), Shadows->GetInt(), OriginalShadows);
				Test->TestEqual(TEXT("rendered Standard restores effective RT"), IsRayTracingEnabled(), bOriginalRayTracing);
				Preset->SetLightweight(bOriginalPreset, false);
				return true;
			}
			++Step;
			After = FPlatformTime::Seconds() + 0.75;
			return false;
		}
	private:
		TWeakObjectPtr<UDesktopRenderingSubsystem> Settings;
		FAutomationTestBase* Test;
		bool bOriginalPreset;
		int32 OriginalShadows, OriginalRaySwitch;
		bool bOriginalRayTracing;
		int32 Step = 0;
		double After = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesktopRenderingRuntimeTransitionsTest, "InNavi.Desktop.Rendering.RuntimeTransitions", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDesktopRenderingRuntimeTransitionsTest::RunTest(const FString& Parameters)
{
	UDesktopRenderingSubsystem* Settings = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Game && Context.World() && Context.World()->GetGameInstance())
		{
			Settings = Context.World()->GetGameInstance()->GetSubsystem<UDesktopRenderingSubsystem>();
			break;
		}
	}
	if (!TestNotNull(TEXT("rendered check needs a desktop game instance"), Settings)) return false;
	if (!TestTrue(TEXT("desktop presets are available"), Settings->IsAvailable())) return false;
	const bool bOriginalPreset = Settings->IsLightweight();
	AddInfo(FString::Printf(TEXT("Initial game preset: %s."), bOriginalPreset ? TEXT("Lightweight") : TEXT("Standard")));
	Settings->SetLightweight(false, false);
	ADD_LATENT_AUTOMATION_COMMAND(DesktopRenderingRenderTests::FTransitionCheck(Settings, this, bOriginalPreset));
	return true;
}

#endif
