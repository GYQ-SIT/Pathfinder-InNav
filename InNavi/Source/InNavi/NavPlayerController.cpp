// Fill out your copyright notice in the Description page of Project Settings.


#include "NavPlayerController.h"
#include "Building.h"
#include "NavigationPanelWidget.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"

ANavPlayerController::ANavPlayerController()
{
	SelectedBuilding = nullptr;
}

void ANavPlayerController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	NavigationPanel = CreateWidget<UNavigationPanelWidget>(this, UNavigationPanelWidget::StaticClass());
	if (NavigationPanel)
	{
		NavigationPanel->AddToViewport();
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
	}
}

void ANavPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindAction("SelectBuilding", IE_Pressed, this, &ANavPlayerController::OnClick);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ANavPlayerController::CancelNavigationSelection);
}

void ANavPlayerController::CancelNavigationSelection()
{
	if (NavigationPanel)
	{
		NavigationPanel->CancelSelection();
	}
}

void ANavPlayerController::OnClick()
{
	if (NavigationPanel && NavigationPanel->IsPointerOverPanel())
	{
		return;
	}

	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	if (Hit.bBlockingHit)
	{
		ABuilding* ClickedBuilding = Cast<ABuilding>(Hit.GetActor());
		if (ClickedBuilding)
		{
			// Un-highlight the previously selected building
			if (SelectedBuilding && SelectedBuilding != ClickedBuilding)
			{
				SelectedBuilding->SetHighlighted(false);
			}

			// Highlight the new one
			ClickedBuilding->SetHighlighted(true);
			SelectedBuilding = ClickedBuilding;

			UE_LOG(LogTemp, Warning, TEXT("Selected building: %s"), *ClickedBuilding->BuildingName);
		}
	}
}
