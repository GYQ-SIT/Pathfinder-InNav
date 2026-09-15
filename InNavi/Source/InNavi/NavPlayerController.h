// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "NavPlayerController.generated.h"

class ABuilding;

UCLASS()
class INNAVI_API ANavPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ANavPlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void OnClick();

	// The currently selected building (to un-highlight when a new one is picked)
	UPROPERTY()
	ABuilding* SelectedBuilding;
};