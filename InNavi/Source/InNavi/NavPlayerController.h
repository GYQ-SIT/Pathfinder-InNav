// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "NavPlayerController.generated.h"

class APolyBuilding;
class ABuildingGenerator;

// Broadcast whenever build mode turns on/off, so UI can show an indicator without polling every tick.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuildModeChanged, bool, bNewBuildMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShapeModeChanged, bool, bNewShapeMode);
// Broadcast after each footprint point is added or cleared, so UI can show a live point count.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFootprintPointCountChanged, int32, PointCount);

UCLASS()
class INNAVI_API ANavPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ANavPlayerController();

	// Plan A (freehand footprint). Callable from Blueprint/UMG as well as the B key, so a UI
	// button can drive this directly once one exists (e.g. for mobile, where there's no keyboard).
	UFUNCTION(BlueprintCallable, Category = "Building")
	void ToggleBuildMode();

	UFUNCTION(BlueprintPure, Category = "Building")
	bool IsInBuildMode() const { return bBuildMode; }

	UPROPERTY(BlueprintAssignable, Category = "Building")
	FOnBuildModeChanged OnBuildModeChanged;

	// Plan A: confirm (generate from the currently drawn points) / cancel the in-progress footprint.
	// Bind these to on-screen buttons - they do the same thing as the Enter/Esc keys.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void FinishBuilding();

	UFUNCTION(BlueprintCallable, Category = "Building")
	void CancelBuilding();

	UPROPERTY(BlueprintAssignable, Category = "Building")
	FOnFootprintPointCountChanged OnFootprintPointCountChanged;

	// Plan B: the permanent entry points Padre's parameter form should call directly (thin
	// pass-throughs to BuildingGenerator, so UI only ever needs a reference to the PlayerController).
	UFUNCTION(BlueprintCallable, Category = "Building")
	APolyBuilding* CreateRectangleBuilding(const FString& Id, const FString& Name, FVector2D Position, float Width, float Depth, float RotationDegrees, int32 FloorCount, float FloorHeight);

	UFUNCTION(BlueprintCallable, Category = "Building")
	APolyBuilding* CreateRegularPolygonBuilding(const FString& Id, const FString& Name, FVector2D Position, int32 Sides, float Radius, float RotationDegrees, int32 FloorCount, float FloorHeight);

	// TEST-ONLY - do not build UI against these two. See ConfirmShapeTest below.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void ToggleShapeTestMode();

	UFUNCTION(BlueprintPure, Category = "Building")
	bool IsInShapeTestMode() const { return bShapeTestMode; }

	UPROPERTY(BlueprintAssignable, Category = "Building")
	FOnShapeModeChanged OnShapeModeChanged;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	// Left-click: selects a building normally, or adds a footprint point while in build mode.
	void OnClick();
	void SelectBuildingUnderCursor();

	// Build mode (Plan A): click to trace out a new building's footprint on the ground.
	// FinishBuilding/CancelBuilding are declared public above (Blueprint-callable).
	void AddFootprintPointUnderCursor();

	// Shape test mode (Plan B): TEMPORARY stand-in for Padre's UI form. Reads
	// Data/test_shape_params.json (a "shape" field picks rectangle vs regular polygon) and calls
	// the matching Generator->Create...Building(...). Delete ConfirmShapeTest + the R-key/shape-mode
	// plumbing once the real UI calls those Create...Building functions directly - those functions
	// themselves are the permanent part.
	void ConfirmShapeTest();

	// Enter/Esc are shared between both modes; dispatch to whichever is active.
	void OnConfirmPressed();
	void OnCancelPressed();

	UPROPERTY()
	APolyBuilding* SelectedBuilding;

	// The single BuildingGenerator placed in the level; found in BeginPlay.
	UPROPERTY()
	ABuildingGenerator* Generator;

	bool bBuildMode = false;
	TArray<FVector2D> PendingFootprint;
	int32 NextNewBuildingIndex = 1;

	bool bShapeTestMode = false;
};