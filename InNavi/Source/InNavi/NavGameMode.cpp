// Fill out your copyright notice in the Description page of Project Settings.


#include "NavGameMode.h"
#include "NavPlayerController.h"
#include "NavCameraPawn.h"
#include "DesktopRenderingSubsystem.h"
#include "Engine/GameInstance.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

ANavGameMode::ANavGameMode()
{
	// Use our custom player controller
	PlayerControllerClass = ANavPlayerController::StaticClass();
	DefaultPawnClass = ANavCameraPawn::StaticClass();
}

void ANavGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (GetGameInstance())
	{
		if (auto* Rendering = GetGameInstance()->GetSubsystem<UDesktopRenderingSubsystem>())
		{
			Rendering->SetLightweight(Rendering->IsLightweight(), false);
		}
	}
	if (FApp::IsUnattended() && FParse::Param(FCommandLine::Get(), TEXT("InNaviSmokeTest")))
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, []()
		{
			FString ResultPath;
			const bool bHasPath = FParse::Value(FCommandLine::Get(), TEXT("InNaviSmokeResult="), ResultPath);
			const bool bReady = bHasPath && FFileHelper::SaveStringToFile(TEXT("ready"), *ResultPath);
			FPlatformMisc::RequestExitWithStatus(false, bReady ? 0 : 1);
		}, 2.0f, false);
	}
}
