// Fill out your copyright notice in the Description page of Project Settings.


#include "NavGameMode.h"
#include "NavPlayerController.h"
#include "NavCameraPawn.h"

ANavGameMode::ANavGameMode()
{
	// Use our custom player controller
	PlayerControllerClass = ANavPlayerController::StaticClass();
	DefaultPawnClass = ANavCameraPawn::StaticClass();
}
