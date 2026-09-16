// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InteriorTypes.h"
#include "NavPlayerController.generated.h"

class APolyBuilding;
class ABuildingGenerator;
class AInteriorGenerator;
class AInteriorRoom;

// Broadcast whenever build mode turns on/off, so UI can show an indicator without polling every tick.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuildModeChanged, bool, bNewBuildMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShapeModeChanged, bool, bNewShapeMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDrawRoomModeChanged, bool, bNewDrawRoomMode);
// Broadcast after each footprint point is added or cleared, so UI can show a live point count.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFootprintPointCountChanged, int32, PointCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomPointCountChanged, int32, PointCount);
// Broadcast whenever a user-triggered action fails (a button press that did nothing), with a
// short human-readable reason. UE_LOG still gets the detailed message for developers; this is
// the same idea aimed at UI - bind it once to show a toast/message, since a packaged game has no
// visible Output Log. Not exhaustive, but covers the common "why didn't anything happen" cases.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActionFailed, FString, Reason);

UCLASS()
class INNAVI_API ANavPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ANavPlayerController();

	// Bind this once to catch every failed action (see FOnActionFailed above) instead of hunting
	// through the Output Log.
	UPROPERTY(BlueprintAssignable, Category = "Feedback")
	FOnActionFailed OnActionFailed;

	// Plan A (freehand footprint). Callable from Blueprint/UMG as well as the B key, so a UI
	// button can drive this directly once one exists (e.g. for mobile, where there's no keyboard).
	UFUNCTION(BlueprintCallable, Category = "Building")
	void ToggleBuildMode();

	UFUNCTION(BlueprintPure, Category = "Building")
	bool IsInBuildMode() const { return bBuildMode; }

	UPROPERTY(BlueprintAssignable, Category = "Building")
	FOnBuildModeChanged OnBuildModeChanged;

	// Plan A: confirm (generate from the currently drawn points) / cancel the in-progress footprint.
	// Bind Finish to an on-screen button; pass CustomId/CustomName from optional text fields if you
	// want to let the user name the building, or leave them empty to auto-generate ("NEW_1", ...).
	UFUNCTION(BlueprintCallable, Category = "Building")
	void FinishBuilding(const FString& CustomId = FString(), const FString& CustomName = FString());

	UFUNCTION(BlueprintCallable, Category = "Building")
	void CancelBuilding();

	UPROPERTY(BlueprintAssignable, Category = "Building")
	FOnFootprintPointCountChanged OnFootprintPointCountChanged;

	// Opens the OS "open file" dialog filtered to .json and returns the path the user picked (empty
	// string if they cancelled). Doesn't load anything - pairs with LoadBuildingDataFromPath so UI
	// can show the picked path (e.g. in a text box) before the user commits to loading it.
	UFUNCTION(BlueprintCallable, Category = "Building")
	FString BrowseForBuildingDataFile();

	// Loads a buildings.json-format file from an explicit path (typically one
	// BrowseForBuildingDataFile just returned) the same way the startup buildings.json is loaded.
	// Returns how many buildings were spawned.
	UFUNCTION(BlueprintCallable, Category = "Building")
	int32 LoadBuildingDataFromPath(const FString& FilePath);

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

	// Plan A-for-interiors (Phase 3): draws a room's footprint on the currently active floor of the
	// currently loaded building, the same way build mode draws a building's footprint. Mutually
	// exclusive with build mode / shape test mode. Fails to turn on if no building interior is
	// currently loaded (see InteriorGenerator::LoadInteriorForBuilding).
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void ToggleDrawRoomMode();

	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsInDrawRoomMode() const { return bDrawRoomMode; }

	UPROPERTY(BlueprintAssignable, Category = "Interior")
	FOnDrawRoomModeChanged OnDrawRoomModeChanged;

	// Confirm (generate from the currently drawn points) / cancel the in-progress room footprint.
	// RoomId/RoomName/bIsPublic/bRequiresKey feed the "Create Room" prompt; leave RoomId empty to
	// auto-generate one matching the {Building}-{Floor:2}-{Room:2} convention
	// (InteriorGenerator::PeekNextRoomId() previews it).
	UFUNCTION(BlueprintCallable, Category = "Interior")
	AInteriorRoom* FinishRoom(const FString& RoomId = FString(), const FString& RoomName = FString(), bool bIsPublic = true, bool bRequiresKey = false);

	UFUNCTION(BlueprintCallable, Category = "Interior")
	void CancelRoom();

	UPROPERTY(BlueprintAssignable, Category = "Interior")
	FOnRoomPointCountChanged OnRoomPointCountChanged;

	// Permanent pass-throughs to InteriorGenerator - same reasoning as the Building pass-throughs
	// above: Padre's UI should never need a separate InteriorGenerator reference, just this one.
	UFUNCTION(BlueprintCallable, Category = "Interior")
	int32 LoadInteriorForBuilding(const FString& BuildingId);

	UFUNCTION(BlueprintCallable, Category = "Interior")
	void SetActiveFloor(int32 Level);

	UFUNCTION(BlueprintPure, Category = "Interior")
	int32 GetActiveFloor() const;

	UFUNCTION(BlueprintCallable, Category = "Interior")
	int32 AddFloor();

	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool RemoveFloor(int32 Level);

	UFUNCTION(BlueprintPure, Category = "Interior")
	FString GetLoadedBuildingId() const;

	UFUNCTION(BlueprintPure, Category = "Interior")
	FString PeekNextRoomId() const;

	UFUNCTION(BlueprintPure, Category = "Interior")
	const TArray<FInteriorNavNode>& GetNavNodes() const;

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

	// Draw room mode (Phase 3): click to trace out a new room's footprint on the ground.
	// FinishRoom/CancelRoom are declared public above (Blueprint-callable).
	void AddRoomPointUnderCursor();

	// Shape test mode (Plan B): TEMPORARY stand-in for Padre's UI form. Reads
	// Data/test_shape_params.json (a "shape" field picks rectangle vs regular polygon) and calls
	// the matching Generator->Create...Building(...). Delete ConfirmShapeTest + the R-key/shape-mode
	// plumbing once the real UI calls those Create...Building functions directly - those functions
	// themselves are the permanent part.
	void ConfirmShapeTest();

	// Enter/Esc are shared between both modes; dispatch to whichever is active.
	void OnConfirmPressed();
	void OnCancelPressed();

	// TEST-ONLY (Phase 0.2 verification): loads the currently selected building's interior.
	// Delete once there's a real "enter interior edit mode" UI flow calling
	// InteriorGen->LoadInteriorForBuilding(...) directly.
	void LoadInteriorForSelectedBuilding();

	// TEST-ONLY (Phase 2.1 verification): cycles the loaded building's active floor up/down via
	// InteriorGen->SetActiveFloor(...). Delete once there's a real floor-switcher UI calling
	// SetActiveFloor directly.
	void CycleInteriorFloorUp();
	void CycleInteriorFloorDown();

	// TEST-ONLY (Phase 2.2 verification): adds a floor to the loaded building via
	// InteriorGen->AddFloor(). Delete once a real "+Add floor" button calls it directly.
	void AddInteriorFloor();

	// TEST-ONLY (Phase 2.2 verification): removes the currently active floor via
	// InteriorGen->RemoveFloor(GetActiveFloor()) - matches the UX call that deleting removes
	// whichever floor you're currently viewing, not always the top one. Delete once a real
	// "remove floor" button calls RemoveFloor directly.
	void RemoveActiveInteriorFloor();

	UPROPERTY()
	APolyBuilding* SelectedBuilding;

	// The single BuildingGenerator placed in the level; found in BeginPlay.
	UPROPERTY()
	ABuildingGenerator* Generator;

	// The single InteriorGenerator placed in the level (if any); found in BeginPlay.
	UPROPERTY()
	AInteriorGenerator* InteriorGen;

	bool bBuildMode = false;
	TArray<FVector2D> PendingFootprint;
	int32 NextNewBuildingIndex = 1;

	bool bShapeTestMode = false;

	bool bDrawRoomMode = false;
	TArray<FVector2D> PendingRoomFootprint;

	// Whichever building had its collision disabled to let draw-room clicks reach the ground
	// plane inside its footprint (the solid exterior shell would otherwise block the raycast
	// before it gets there). Restored and cleared whenever draw room mode ends.
	UPROPERTY()
	APolyBuilding* RoomDrawingBuilding;

	// Turns draw room mode off from any path (self-toggle, or another mode taking over) -
	// restores RoomDrawingBuilding's collision and clears pending points.
	void ExitDrawRoomMode();
};