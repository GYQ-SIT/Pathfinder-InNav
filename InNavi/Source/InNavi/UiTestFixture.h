#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "NavigationPanelWidget.h"
#include "NavPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

// A small headless world for the UI tests: the example project loaded, E2's interior open, and a local player
// controller wired to the generators and the navigation panel, so screens and panels created against it behave as
// they do in the game.
namespace
{
	struct FNavigationSearchFixture
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		ABuildingGenerator* Generator = World->SpawnActor<ABuildingGenerator>();
		AInteriorGenerator* Interior = World->SpawnActor<AInteriorGenerator>();
		UNavigationPanelWidget* Nav = NewObject<UNavigationPanelWidget>();
		FNavigationSearchFixture()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Interior->DispatchBeginPlay();
			Generator->LoadBuildingsFromFile(FPaths::ProjectDir() / TEXT("Data/buildings.json"));
			Interior->LoadInteriorForBuilding(TEXT("E2"));
			Nav->SetNavigationContext(Generator, Interior);
		}
		~FNavigationSearchFixture()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		ANavPlayerController* MakeController()
		{
			ANavPlayerController* Controller = World->SpawnActor<ANavPlayerController>();
			ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
			Controller->Player = Player;
			Player->PlayerController = Controller;
			Controller->SetAsLocalPlayerController();
			World->AddController(Controller);
			Controller->SetAllUiSoundsEnabled(false);
			FindFProperty<FObjectPropertyBase>(Controller->GetClass(), TEXT("Generator"))->SetObjectPropertyValue_InContainer(Controller, Generator);
			FindFProperty<FObjectPropertyBase>(Controller->GetClass(), TEXT("InteriorGen"))->SetObjectPropertyValue_InContainer(Controller, Interior);
			FindFProperty<FObjectPropertyBase>(Controller->GetClass(), TEXT("NavigationPanel"))->SetObjectPropertyValue_InContainer(Controller, Nav);
			return Controller;
		}
	};
}


#endif // WITH_DEV_AUTOMATION_TESTS
