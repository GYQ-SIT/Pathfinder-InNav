// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "NavPlayerController.generated.h"

class APolyBuilding;
class ABuildingGenerator;

// Broadcast whenever build mode turns on/off, so UI can show an indicator without polling every tick.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuildModeChanged, bool, bNewBuildMode);

UCLASS()
class INNAVI_API ANavPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ANavPlayerController();

	// Callable from Blueprint/UMG as well as the B key, so a UI button can drive this directly
	// once one exists (e.g. for mobile, where there's no keyboard).
	UFUNCTION(BlueprintCallable, Category = "Building")
	void ToggleBuildMode();

	UFUNCTION(BlueprintPure, Category = "Building")
	bool IsInBuildMode() const { return bBuildMode; }

	UPROPERTY(BlueprintAssignable, Category = "Building")
	FOnBuildModeChanged OnBuildModeChanged;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	// Left-click: selects a building normally, or adds a footprint point while in build mode.
	void OnClick();
	void SelectBuildingUnderCursor();

	// Build mode: click to trace out a new building's footprint on the ground.
	void AddFootprintPointUnderCursor();
	void FinishBuilding();
	void CancelBuilding();

	UPROPERTY()
	APolyBuilding* SelectedBuilding;

	// The single BuildingGenerator placed in the level; found in BeginPlay.
	UPROPERTY()
	ABuildingGenerator* Generator;

	bool bBuildMode = false;
	TArray<FVector2D> PendingFootprint;
	int32 NextNewBuildingIndex = 1;
};