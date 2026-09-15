// Fill out your copyright notice in the Description page of Project Settings.


#include "NavPlayerController.h"
#include "PolyBuilding.h"

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
}

void ANavPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindAction("SelectBuilding", IE_Pressed, this, &ANavPlayerController::OnClick);
}

void ANavPlayerController::OnClick()
{
	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	if (Hit.bBlockingHit)
	{
		APolyBuilding* ClickedBuilding = Cast<APolyBuilding>(Hit.GetActor());
		if (ClickedBuilding)
		{
			if (SelectedBuilding && SelectedBuilding != ClickedBuilding)
			{
				SelectedBuilding->SetHighlighted(false);
			}

			ClickedBuilding->SetHighlighted(true);
			SelectedBuilding = ClickedBuilding;

			UE_LOG(LogTemp, Warning, TEXT("Selected building: %s"), *ClickedBuilding->BuildingName);
		}
	}
}