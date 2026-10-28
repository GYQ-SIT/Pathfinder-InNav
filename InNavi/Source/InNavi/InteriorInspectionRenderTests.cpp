#if WITH_DEV_AUTOMATION_TESTS

#include "InteriorScreenWidget.h"
#include "InteriorBuildingPreviewPivot.h"
#include "NavPlayerController.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace InteriorInspectionRenderTests
{
	// Run explicitly in a rendered desktop game, separately from the headless editor suite.
	class FSceneCheck : public IAutomationLatentCommand
	{
	public:
		FSceneCheck(ANavPlayerController* InController, FAutomationTestBase* InTest, const FTransform& InCamera)
			: Controller(InController), Test(InTest), OriginalCamera(InCamera), After(FPlatformTime::Seconds() + 2.0) {}
		virtual bool Update() override
		{
			ANavPlayerController* PC = Controller.Get();
			if (!PC) { Test->AddError(TEXT("Smoke-test controller was destroyed")); return true; }
			if (FPlatformTime::Seconds() < After || FScreenshotRequest::IsScreenshotRequested()) return false;
			const FString Directory = FPaths::ProjectSavedDir() / TEXT("InspectionVerification/Screenshots");
			IFileManager::Get().MakeDirectory(*Directory, true);
			if (Step == 0)
			{
				Test->TestTrue(TEXT("rendered inspection is active"), PC->IsInteriorInspectionEnabled());
				FScreenshotRequest::RequestScreenshot(Directory / TEXT("Rotated.png"), true, false);
			}
			else if (Step == 1)
			{
				Test->TestTrue(TEXT("rotated view screenshot written"), IFileManager::Get().FileSize(*(Directory / TEXT("Rotated.png"))) > 0);
				PC->SetInteriorInspectionEnabled(false);
				Test->TestTrue(TEXT("rendered view restored its original camera"), PC->GetPawn()->GetActorTransform().Equals(OriginalCamera));
				Test->TestFalse(TEXT("rendered view restored input"), PC->IsLookInputIgnored() || PC->IsMoveInputIgnored());
			}
			else if (Step == 2) FScreenshotRequest::RequestScreenshot(Directory / TEXT("Restored.png"), true, false);
			else
			{
				Test->TestTrue(TEXT("restored view screenshot written"), IFileManager::Get().FileSize(*(Directory / TEXT("Restored.png"))) > 0);
				return true;
			}
			++Step;
			After = FPlatformTime::Seconds() + 1.0;
			return false;
		}
	private:
		TWeakObjectPtr<ANavPlayerController> Controller;
		FAutomationTestBase* Test;
		FTransform OriginalCamera;
		double After;
		int32 Step = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteriorInspectionSceneSmoke, "InNavi.Interior.Inspection.SceneSmoke",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FInteriorInspectionSceneSmoke::RunTest(const FString& Parameters)
{
	UWorld* GameWorld = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Game && Context.World()) { GameWorld = Context.World(); break; }
	}
	ANavPlayerController* PC = GameWorld ? Cast<ANavPlayerController>(GameWorld->GetFirstPlayerController()) : nullptr;
	if (!TestNotNull(TEXT("render smoke needs a desktop game world"), PC)) return false;
	TArray<UUserWidget*> Screens;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GameWorld, Screens, UUserWidget::StaticClass(), true);
	for (UUserWidget* Screen : Screens) Screen->RemoveFromParent();
	PC->LoadBuildingDataFromPath(FPaths::ProjectDir() / TEXT("Data/buildings.json"));
	PC->LoadInteriorForBuilding(TEXT("E2"));
	UInteriorScreenWidget* Screen = CreateWidget<UInteriorScreenWidget>(PC);
	Screen->AddToViewport();
	PC->SetIn3DPreviewMode(true);
	const FTransform Camera = PC->GetPawn()->GetActorTransform();
	PC->SetInteriorInspectionEnabled(true);
	PC->RotateInspectionBy(40.f, -20.f);
	ADD_LATENT_AUTOMATION_COMMAND(InteriorInspectionRenderTests::FSceneCheck(PC, this, Camera));
	return true;
}

#endif
