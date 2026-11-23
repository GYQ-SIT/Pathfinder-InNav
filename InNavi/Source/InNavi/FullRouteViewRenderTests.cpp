#if WITH_DEV_AUTOMATION_TESTS

#include "InteriorScreenWidget.h"
#include "NavigationPanelWidget.h"
#include "NavPlayerController.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace FullRouteViewRenderTests
{
	class FSceneCheck : public IAutomationLatentCommand
	{
	public:
		FSceneCheck(ANavPlayerController* PC, UButton* InButton, FAutomationTestBase* InTest)
			: Controller(PC), Button(InButton), Test(InTest), OriginalCamera(PC->GetPawn()->GetActorTransform()), After(FPlatformTime::Seconds() + 3.0) {}
		virtual bool Update() override
		{
			ANavPlayerController* PC = Controller.Get();
			if (!PC || !Button.IsValid()) { Test->AddError(TEXT("Overview scene destroyed")); return true; }
			if (FPlatformTime::Seconds() < After || FScreenshotRequest::IsScreenshotRequested()) return false;
			const FString Directory = FPaths::ProjectSavedDir() / TEXT("FullRouteVerification/Screenshots");
			IFileManager::Get().MakeDirectory(*Directory, true);
			if (Step == 0) FScreenshotRequest::RequestScreenshot(Directory / TEXT("Before.png"), true, false);
			else if (Step == 1)
			{
				Button->OnClicked.Broadcast();
				Test->TestTrue(TEXT("navigation button enters rendered overview"), PC->IsFullRouteViewEnabled());
			}
			else if (Step == 2) FScreenshotRequest::RequestScreenshot(Directory / TEXT("FullRoute.png"), true, false);
			else if (Step == 3)
			{
				Button->OnClicked.Broadcast();
				Test->TestFalse(TEXT("navigation button leaves rendered overview"), PC->IsFullRouteViewEnabled());
				Test->TestTrue(TEXT("rendered camera restored exactly"), PC->GetPawn()->GetActorTransform().Equals(OriginalCamera));
			}
			else if (Step == 4) FScreenshotRequest::RequestScreenshot(Directory / TEXT("Restored.png"), true, false);
			else
			{
				for (const TCHAR* File : { TEXT("Before.png"), TEXT("FullRoute.png"), TEXT("Restored.png") })
					Test->TestTrue(TEXT("rendered screenshot saved"), IFileManager::Get().FileSize(*(Directory / File)) > 0);
				return true;
			}
			++Step;
			After = FPlatformTime::Seconds() + 2.0;
			return false;
		}
	private:
		TWeakObjectPtr<ANavPlayerController> Controller;
		TWeakObjectPtr<UButton> Button;
		FAutomationTestBase* Test;
		FTransform OriginalCamera;
		double After;
		int32 Step = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFullRouteSceneSmoke, "InNavi.Navigation.FullRoute.SceneSmoke",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FFullRouteSceneSmoke::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Game && Context.World()) { World = Context.World(); break; }
	}
	ANavPlayerController* PC = World ? Cast<ANavPlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (!TestNotNull(TEXT("render smoke requires desktop game"), PC)) return false;
	TArray<UUserWidget*> Screens;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Screens, UUserWidget::StaticClass(), true);
	for (UUserWidget* Screen : Screens) Screen->RemoveFromParent();
	PC->LoadBuildingDataFromPath(FPaths::ProjectDir() / TEXT("../docs/test-data/e2-test-fixture.json"));
	PC->LoadInteriorForBuilding(TEXT("E2"));
	UInteriorScreenWidget* Screen = CreateWidget<UInteriorScreenWidget>(PC);
	Screen->AddToViewport();
	UNavigationPanelWidget* Nav = PC->GetNavigationPanel();
	FString Start, End;
	int32 EndFloor = 1;
	for (const auto& Option : Nav->GetSelectableRooms())
	{
		if (Option.Floor == 1 && Start.IsEmpty()) Start = Option.GetDisplayLabel();
		// The destination is on the highest floor there is, so the route crosses as many floors as the building has.
		if (Option.Floor > EndFloor) { EndFloor = Option.Floor; End = Option.GetDisplayLabel(); }
	}
	if (!TestTrue(TEXT("real E2 rooms on floor 1 and on a higher floor"), !Start.IsEmpty() && !End.IsEmpty())) return false;
	Nav->SetStartToRoom(Start);
	Nav->SetDestinationToRoom(End);
	Nav->RequestFindRoute();
	if (!TestTrue(TEXT("real multi-floor route planned"), !Nav->GetRoutePoints().IsEmpty())) return false;
	if (UCheckBox* Show = Cast<UCheckBox>(Screen->FindScreenWidget(TEXT("Chk_WindowNavigation"))))
	{
		Show->SetIsChecked(true);
		Show->OnCheckStateChanged.Broadcast(true);
	}
	PC->SetIn3DPreviewMode(true);
	UButton* Button = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_NavFullRouteView")));
	if (!TestNotNull(TEXT("rendered screen exposes navigation overview button"), Button)) return false;
	ADD_LATENT_AUTOMATION_COMMAND(FullRouteViewRenderTests::FSceneCheck(PC, Button, this));
	return true;
}

#endif
