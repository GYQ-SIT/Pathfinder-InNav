// Fill out your copyright notice in the Description page of Project Settings.


#include "NavPlayerController.h"
#include "PolyBuilding.h"
#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"

ANavPlayerController::ANavPlayerController()
{
	SelectedBuilding = nullptr;
	Generator = nullptr;
	InteriorGen = nullptr;
	RoomDrawingBuilding = nullptr;
}

void ANavPlayerController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	for (TActorIterator<ABuildingGenerator> It(GetWorld()); It; ++It)
	{
		Generator = *It;
		break;
	}

	for (TActorIterator<AInteriorGenerator> It(GetWorld()); It; ++It)
	{
		InteriorGen = *It;
		break;
	}
}

void ANavPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindAction("SelectBuilding", IE_Pressed, this, &ANavPlayerController::OnClick);
	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ANavPlayerController::ToggleBuildMode);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ANavPlayerController::ToggleShapeTestMode);
	InputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &ANavPlayerController::CycleInteriorFloorUp);
	InputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &ANavPlayerController::CycleInteriorFloorDown);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ANavPlayerController::AddInteriorFloor);
	InputComponent->BindKey(EKeys::X, IE_Pressed, this, &ANavPlayerController::RemoveActiveInteriorFloor);
	InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ANavPlayerController::ToggleDrawRoomMode);
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ANavPlayerController::OnConfirmPressed);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ANavPlayerController::OnCancelPressed);
	InputComponent->BindKey(FInputChord(EKeys::S, /*Shift*/false, /*Ctrl*/true, /*Alt*/false, /*Cmd*/false), IE_Pressed, this, &ANavPlayerController::SaveCurrentProjectKeyHandler);
}

void ANavPlayerController::OnClick()
{
	if (bBuildMode)
	{
		AddFootprintPointUnderCursor();
	}
	else if (bDrawRoomMode)
	{
		AddRoomPointUnderCursor();
	}
	else
	{
		SelectBuildingUnderCursor();
	}
}

void ANavPlayerController::SelectBuildingUnderCursor()
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
			OnBuildingSelected.Broadcast(ClickedBuilding->BuildingId);

			UE_LOG(LogTemp, Warning, TEXT("Selected building: %s"), *ClickedBuilding->BuildingName);
		}
	}
}

void ANavPlayerController::ToggleBuildMode()
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter build mode: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("Cannot enter build mode: no BuildingGenerator found in the level."));
		return;
	}

	if (bShapeTestMode)
	{
		// Modes are mutually exclusive - only one should interpret Enter/Esc at a time.
		bShapeTestMode = false;
		OnShapeModeChanged.Broadcast(false);
	}
	if (bDrawRoomMode)
	{
		ExitDrawRoomMode();
	}

	bBuildMode = !bBuildMode;
	OnBuildModeChanged.Broadcast(bBuildMode);

	if (bBuildMode)
	{
		UE_LOG(LogTemp, Warning, TEXT("Build mode ON: click to add footprint points, Enter to finish, Esc to cancel"));
	}
	else
	{
		CancelBuilding();
		UE_LOG(LogTemp, Warning, TEXT("Build mode OFF"));
	}
}

void ANavPlayerController::ToggleShapeTestMode()
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter shape test mode: no BuildingGenerator found in the level"));
		return;
	}

	if (bBuildMode)
	{
		// Modes are mutually exclusive - only one should interpret Enter/Esc at a time.
		CancelBuilding();
		bBuildMode = false;
		OnBuildModeChanged.Broadcast(false);
	}
	if (bDrawRoomMode)
	{
		ExitDrawRoomMode();
	}

	bShapeTestMode = !bShapeTestMode;
	OnShapeModeChanged.Broadcast(bShapeTestMode);

	if (bShapeTestMode)
	{
		UE_LOG(LogTemp, Warning, TEXT("Shape test mode ON: edit Data/test_shape_params.json (\"shape\": \"rectangle\" or \"polygon\"), then Enter to generate, Esc to exit"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Shape test mode OFF"));
	}
}

void ANavPlayerController::ToggleDrawRoomMode()
{
	if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter draw room mode: no building interior currently loaded"));
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try drawing a room."));
		return;
	}

	if (bDrawRoomMode)
	{
		ExitDrawRoomMode();
		return;
	}

	if (bBuildMode)
	{
		// Modes are mutually exclusive - only one should interpret Enter/Esc at a time.
		CancelBuilding();
		bBuildMode = false;
		OnBuildModeChanged.Broadcast(false);
	}
	if (bShapeTestMode)
	{
		bShapeTestMode = false;
		OnShapeModeChanged.Broadcast(false);
	}

	bDrawRoomMode = true;
	OnDrawRoomModeChanged.Broadcast(true);

	// Temporarily disable the loaded building's collision so clicks can reach the ground plane
	// inside its footprint - otherwise the solid exterior shell blocks the raycast before it gets
	// there. Restored in ExitDrawRoomMode.
	RoomDrawingBuilding = Generator ? Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId()) : nullptr;
	if (RoomDrawingBuilding)
	{
		RoomDrawingBuilding->SetActorEnableCollision(false);
	}

	UE_LOG(LogTemp, Warning, TEXT("Draw room mode ON (floor %d): click to add footprint points, Enter to finish, Esc to cancel"), InteriorGen->GetActiveFloor());
}

void ANavPlayerController::ExitDrawRoomMode()
{
	CancelRoom();
	bDrawRoomMode = false;
	OnDrawRoomModeChanged.Broadcast(false);

	if (RoomDrawingBuilding)
	{
		RoomDrawingBuilding->SetActorEnableCollision(true);
		RoomDrawingBuilding = nullptr;
	}

	UE_LOG(LogTemp, Warning, TEXT("Draw room mode OFF"));
}

void ANavPlayerController::OnConfirmPressed()
{
	if (bBuildMode)
	{
		FinishBuilding();
	}
	else if (bShapeTestMode)
	{
		ConfirmShapeTest();
	}
	else if (bDrawRoomMode)
	{
		FinishRoom();
	}
}

void ANavPlayerController::OnCancelPressed()
{
	if (bBuildMode)
	{
		CancelBuilding();
	}
	else if (bShapeTestMode)
	{
		bShapeTestMode = false;
		OnShapeModeChanged.Broadcast(false);
		UE_LOG(LogTemp, Warning, TEXT("Shape test mode OFF"));
	}
	else if (bDrawRoomMode)
	{
		CancelRoom();
	}
}

void ANavPlayerController::AddFootprintPointUnderCursor()
{
	if (!Generator)
	{
		return;
	}

	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	if (!Hit.bBlockingHit)
	{
		return;
	}

	if (Cast<APolyBuilding>(Hit.GetActor()))
	{
		UE_LOG(LogTemp, Warning, TEXT("Click on the ground to add a footprint point, not on another building"));
		OnActionFailed.Broadcast(TEXT("Click on the ground, not on another building."));
		return;
	}

	// SpawnBuildingFromData multiplies the footprint by Generator's Scale, but Hit.Location is
	// already in final world-space units, so divide it back out before storing the point.
	const float Scale = Generator->GetScale();
	const FVector2D NewPoint(Hit.Location.X / Scale, Hit.Location.Y / Scale);

	if (PendingFootprint.Num() > 0)
	{
		// Prev is stored unscaled (see above) - scale it back up to world space to draw it correctly.
		const FVector2D& Prev = PendingFootprint.Last();
		const FVector PrevWorld(Prev.X * Scale, Prev.Y * Scale, Hit.Location.Z);
		DrawDebugLine(GetWorld(), PrevWorld, Hit.Location, FColor::Green, true, -1.f, 0, 5.f);
	}
	DrawDebugSphere(GetWorld(), Hit.Location, 20.f, 8, FColor::Green, true);

	PendingFootprint.Add(NewPoint);
	OnFootprintPointCountChanged.Broadcast(PendingFootprint.Num());

	UE_LOG(LogTemp, Warning, TEXT("Footprint point %d added: (%.1f, %.1f)"), PendingFootprint.Num(), NewPoint.X, NewPoint.Y);
}

void ANavPlayerController::FinishBuilding(const FString& CustomId, const FString& CustomName)
{
	if (!bBuildMode)
	{
		return;
	}

	if (PendingFootprint.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("Need at least 3 points to finish a building (have %d)"), PendingFootprint.Num());
		OnActionFailed.Broadcast(FString::Printf(TEXT("Need at least 3 points to finish a building (have %d)."), PendingFootprint.Num()));
		return;
	}

	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("No BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."));
		return;
	}

	// Empty CustomId/CustomName means "let the user skip naming it" - fall back to auto-numbering.
	const bool bUsingAutoId = CustomId.IsEmpty();
	const FString NewId = bUsingAutoId ? FString::Printf(TEXT("NEW_%d"), NextNewBuildingIndex) : CustomId;
	const FString NewName = CustomName.IsEmpty() ? NewId : CustomName;

	APolyBuilding* NewBuilding = Generator->SpawnBuildingFromData(NewId, NewName, PendingFootprint, 5, 300.f, FVector2D::ZeroVector);

	if (!NewBuilding)
	{
		// Spawn was rejected (e.g. self-intersecting outline, or a typed-in id that's already taken)
		// - keep the drawn points and preview so the user can see what's there; Esc still cancels.
		UE_LOG(LogTemp, Warning, TEXT("Could not finish this building - see error above. Press Esc to discard and redraw."));
		OnActionFailed.Broadcast(TEXT("Could not create the building - check that the id isn't already used and the outline doesn't cross itself."));
		return;
	}

	if (bUsingAutoId)
	{
		NextNewBuildingIndex++;
	}
	bHasUnsavedChanges = true;
	FlushPersistentDebugLines(GetWorld());
	PendingFootprint.Reset();
	OnFootprintPointCountChanged.Broadcast(0);
}

void ANavPlayerController::CancelBuilding()
{
	FlushPersistentDebugLines(GetWorld());
	PendingFootprint.Reset();
	OnFootprintPointCountChanged.Broadcast(0);
}

void ANavPlayerController::AddRoomPointUnderCursor()
{
	if (!InteriorGen)
	{
		return;
	}

	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	if (!Hit.bBlockingHit)
	{
		return;
	}

	if (Cast<APolyBuilding>(Hit.GetActor()) || Cast<AInteriorRoom>(Hit.GetActor()))
	{
		UE_LOG(LogTemp, Warning, TEXT("Click on the ground to add a room point, not on a building or another room"));
		OnActionFailed.Broadcast(TEXT("Click on the ground, not on a building or another room."));
		return;
	}

	// CreateRoomFromFootprint multiplies the footprint by Generator's Scale, but Hit.Location is
	// already in final world-space units, so divide it back out before storing the point.
	const float Scale = Generator ? Generator->GetScale() : 1.f;
	const FVector2D NewPoint(Hit.Location.X / Scale, Hit.Location.Y / Scale);

	if (PendingRoomFootprint.Num() > 0)
	{
		// Prev is stored unscaled (see above) - scale it back up to world space to draw it correctly.
		const FVector2D& Prev = PendingRoomFootprint.Last();
		const FVector PrevWorld(Prev.X * Scale, Prev.Y * Scale, Hit.Location.Z);
		DrawDebugLine(GetWorld(), PrevWorld, Hit.Location, FColor::Cyan, true, -1.f, 0, 5.f);
	}
	DrawDebugSphere(GetWorld(), Hit.Location, 20.f, 8, FColor::Cyan, true);

	PendingRoomFootprint.Add(NewPoint);
	OnRoomPointCountChanged.Broadcast(PendingRoomFootprint.Num());

	UE_LOG(LogTemp, Warning, TEXT("Room point %d added: (%.1f, %.1f)"), PendingRoomFootprint.Num(), NewPoint.X, NewPoint.Y);
}

AInteriorRoom* ANavPlayerController::FinishRoom(const FString& RoomId, const FString& RoomName, bool bIsPublic, bool bRequiresKey)
{
	if (!bDrawRoomMode)
	{
		return nullptr;
	}

	if (PendingRoomFootprint.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("Need at least 3 points to finish a room (have %d)"), PendingRoomFootprint.Num());
		OnActionFailed.Broadcast(FString::Printf(TEXT("Need at least 3 points to finish a room (have %d)."), PendingRoomFootprint.Num()));
		return nullptr;
	}

	if (!InteriorGen)
	{
		UE_LOG(LogTemp, Error, TEXT("No InteriorGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator found in the level."));
		return nullptr;
	}

	AInteriorRoom* NewRoom = InteriorGen->CreateRoomFromFootprint(PendingRoomFootprint, RoomId, RoomName, bIsPublic, bRequiresKey);

	if (!NewRoom)
	{
		// Spawn was rejected (e.g. self-intersecting outline, or a typed-in id that's already
		// taken) - keep the drawn points and preview so the user can see what's there; Esc cancels.
		UE_LOG(LogTemp, Warning, TEXT("Could not finish this room - see error above. Press Esc to discard and redraw."));
		OnActionFailed.Broadcast(TEXT("Could not create the room - check that the id isn't already used and the outline doesn't cross itself."));
		return nullptr;
	}

	bHasUnsavedChanges = true;
	FlushPersistentDebugLines(GetWorld());
	PendingRoomFootprint.Reset();
	OnRoomPointCountChanged.Broadcast(0);
	return NewRoom;
}

void ANavPlayerController::CancelRoom()
{
	FlushPersistentDebugLines(GetWorld());
	PendingRoomFootprint.Reset();
	OnRoomPointCountChanged.Broadcast(0);
}

APolyBuilding* ANavPlayerController::CreateRectangleBuilding(const FString& Id, const FString& Name, FVector2D Position, float Width, float Depth, float RotationDegrees, int32 FloorCount, float FloorHeight)
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRectangleBuilding: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."));
		return nullptr;
	}

	APolyBuilding* Result = Generator->CreateRectangleBuilding(Id, Name, Position, Width, Depth, RotationDegrees, FloorCount, FloorHeight);
	if (!Result)
	{
		OnActionFailed.Broadcast(TEXT("Could not create the building - check that the id isn't already used and the dimensions/floor count are valid."));
	}
	else
	{
		bHasUnsavedChanges = true;
	}
	return Result;
}

APolyBuilding* ANavPlayerController::CreateRegularPolygonBuilding(const FString& Id, const FString& Name, FVector2D Position, int32 Sides, float Radius, float RotationDegrees, int32 FloorCount, float FloorHeight)
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRegularPolygonBuilding: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."));
		return nullptr;
	}

	APolyBuilding* Result = Generator->CreateRegularPolygonBuilding(Id, Name, Position, Sides, Radius, RotationDegrees, FloorCount, FloorHeight);
	if (!Result)
	{
		OnActionFailed.Broadcast(TEXT("Could not create the building - check that the id isn't already used and the sides/radius/floor count are valid."));
	}
	else
	{
		bHasUnsavedChanges = true;
	}
	return Result;
}

FString ANavPlayerController::BrowseForBuildingDataFile()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		UE_LOG(LogTemp, Error, TEXT("BrowseForBuildingDataFile: desktop platform not available"));
		OnActionFailed.Broadcast(TEXT("Could not open the file picker on this system."));
		return FString();
	}

	const void* ParentWindowHandle = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (ActiveWindow.IsValid() && ActiveWindow->GetNativeWindow().IsValid())
		{
			ParentWindowHandle = ActiveWindow->GetNativeWindow()->GetOSWindowHandle();
		}
	}

	TArray<FString> OutFiles;
	const bool bPicked = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		TEXT("Import building data"),
		TEXT(""),
		TEXT(""),
		TEXT("JSON files (*.json)|*.json"),
		EFileDialogFlags::None,
		OutFiles);

	if (!bPicked || OutFiles.Num() == 0)
	{
		// User cancelled the dialog - not a failure, nothing to report.
		return FString();
	}

	return OutFiles[0];
}

int32 ANavPlayerController::LoadBuildingDataFromPath(const FString& FilePath)
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadBuildingDataFromPath: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."));
		return 0;
	}

	if (FilePath.IsEmpty())
	{
		OnActionFailed.Broadcast(TEXT("No file selected - click Browse first."));
		return 0;
	}

	if (InteriorGen)
	{
		InteriorGen->ClearLoadedInterior();
	}
	Generator->ClearAllBuildings();

	const int32 Count = Generator->LoadBuildingsFromFile(FilePath);
	UE_LOG(LogTemp, Warning, TEXT("LoadBuildingDataFromPath: loaded %d buildings from %s"), Count, *FilePath);
	if (Count == 0)
	{
		OnActionFailed.Broadcast(TEXT("No buildings were loaded from that file - check that it matches the expected JSON format."));
	}
	else
	{
		// This file is now what Ctrl+S saves back to.
		CurrentProjectFilePath = FilePath;
		bHasUnsavedChanges = false;
	}
	return Count;
}

void ANavPlayerController::StartNewProject()
{
	if (InteriorGen)
	{
		InteriorGen->ClearLoadedInterior();
	}
	if (Generator)
	{
		Generator->ClearAllBuildings();
	}

	CurrentProjectFilePath.Empty();
	bHasUnsavedChanges = false;
	NextNewBuildingIndex = 1;

	// Defensively reset any in-progress mode/selection state too, in case New is ever reachable
	// mid-edit rather than only from the start screen.
	bBuildMode = false;
	PendingFootprint.Reset();
	bDrawRoomMode = false;
	PendingRoomFootprint.Reset();
	SelectedBuilding = nullptr;
	RoomDrawingBuilding = nullptr;

	UE_LOG(LogTemp, Warning, TEXT("Started a new, blank project"));
}

bool ANavPlayerController::SaveCurrentProject()
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveCurrentProject: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."));
		return false;
	}

	if (CurrentProjectFilePath.IsEmpty())
	{
		// No file yet (e.g. a brand-new session) - ask where to save, same dialog Open uses.
		const FString ChosenPath = BrowseForSaveLocation();
		if (ChosenPath.IsEmpty())
		{
			// User cancelled the dialog - not a failure, nothing to report.
			return false;
		}
		return SaveCurrentProjectAs(ChosenPath);
	}

	if (InteriorGen)
	{
		InteriorGen->SyncInteriorToBuildingGenerator();
	}

	const bool bSuccess = Generator->SaveBuildingsToFile(CurrentProjectFilePath);
	if (bSuccess)
	{
		bHasUnsavedChanges = false;
		UE_LOG(LogTemp, Warning, TEXT("Saved to %s"), *CurrentProjectFilePath);
	}
	else
	{
		OnActionFailed.Broadcast(TEXT("Could not save - see the Output Log for details."));
	}
	return bSuccess;
}

FString ANavPlayerController::BrowseForSaveLocation()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		UE_LOG(LogTemp, Error, TEXT("BrowseForSaveLocation: desktop platform not available"));
		OnActionFailed.Broadcast(TEXT("Could not open the save dialog on this system."));
		return FString();
	}

	const void* ParentWindowHandle = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (ActiveWindow.IsValid() && ActiveWindow->GetNativeWindow().IsValid())
		{
			ParentWindowHandle = ActiveWindow->GetNativeWindow()->GetOSWindowHandle();
		}
	}

	TArray<FString> OutFiles;
	const bool bPicked = DesktopPlatform->SaveFileDialog(
		ParentWindowHandle,
		TEXT("Save building data"),
		TEXT(""),
		TEXT("NewProject.json"),
		TEXT("JSON files (*.json)|*.json"),
		EFileDialogFlags::None,
		OutFiles);

	if (!bPicked || OutFiles.Num() == 0)
	{
		// User cancelled the dialog - not a failure, nothing to report.
		return FString();
	}

	return OutFiles[0];
}

bool ANavPlayerController::SaveCurrentProjectAs(const FString& FilePath)
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveCurrentProjectAs: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."));
		return false;
	}

	if (FilePath.IsEmpty())
	{
		OnActionFailed.Broadcast(TEXT("No save location chosen."));
		return false;
	}

	if (InteriorGen)
	{
		InteriorGen->SyncInteriorToBuildingGenerator();
	}

	const bool bSuccess = Generator->SaveBuildingsToFile(FilePath);
	if (bSuccess)
	{
		CurrentProjectFilePath = FilePath;
		bHasUnsavedChanges = false;
		UE_LOG(LogTemp, Warning, TEXT("Saved to %s"), *FilePath);
	}
	else
	{
		OnActionFailed.Broadcast(TEXT("Could not save - see the Output Log for details."));
	}
	return bSuccess;
}

void ANavPlayerController::SaveCurrentProjectKeyHandler()
{
	SaveCurrentProject();
}

// ---- Permanent Interior pass-throughs (see header) ----

int32 ANavPlayerController::LoadInteriorForBuilding(const FString& BuildingId)
{
	if (!InteriorGen)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadInteriorForBuilding: no InteriorGenerator placed in the level"));
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."));
		return -1;
	}

	const int32 Result = InteriorGen->LoadInteriorForBuilding(BuildingId);
	if (Result < 0)
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not load the interior for '%s' - no building with that id exists."), *BuildingId));
	}
	return Result;
}

void ANavPlayerController::SetActiveFloor(int32 Level)
{
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."));
		return;
	}

	InteriorGen->SetActiveFloor(Level);
}

int32 ANavPlayerController::GetActiveFloor() const
{
	return InteriorGen ? InteriorGen->GetActiveFloor() : 0;
}

int32 ANavPlayerController::AddFloor()
{
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."));
		return -1;
	}

	const int32 NewLevel = InteriorGen->AddFloor();
	if (NewLevel < 0)
	{
		OnActionFailed.Broadcast(TEXT("Could not add a floor - load a building's interior first."));
	}
	else
	{
		bHasUnsavedChanges = true;
	}
	return NewLevel;
}

bool ANavPlayerController::RemoveFloor(int32 Level)
{
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."));
		return false;
	}

	const bool bSuccess = InteriorGen->RemoveFloor(Level);
	if (!bSuccess)
	{
		OnActionFailed.Broadcast(TEXT("Could not remove that floor - it may not exist, or it's the last remaining floor."));
	}
	else
	{
		bHasUnsavedChanges = true;
	}
	return bSuccess;
}

FString ANavPlayerController::GetLoadedBuildingId() const
{
	return InteriorGen ? InteriorGen->GetLoadedBuildingId() : FString();
}

FString ANavPlayerController::PeekNextRoomId() const
{
	return InteriorGen ? InteriorGen->PeekNextRoomId() : FString();
}

const TArray<FInteriorNavNode>& ANavPlayerController::GetNavNodes() const
{
	static const TArray<FInteriorNavNode> EmptyNavNodes;
	return InteriorGen ? InteriorGen->GetNavNodes() : EmptyNavNodes;
}

FString ANavPlayerController::GetSelectedBuildingId() const
{
	return SelectedBuilding ? SelectedBuilding->BuildingId : FString();
}

// ---- TEST-ONLY keyboard hooks - route through the pass-throughs above ----

void ANavPlayerController::CycleInteriorFloorUp()
{
	SetActiveFloor(GetActiveFloor() + 1);
	UE_LOG(LogTemp, Warning, TEXT("Active interior floor: %d"), GetActiveFloor());
}

void ANavPlayerController::CycleInteriorFloorDown()
{
	SetActiveFloor(GetActiveFloor() - 1);
	UE_LOG(LogTemp, Warning, TEXT("Active interior floor: %d"), GetActiveFloor());
}

void ANavPlayerController::AddInteriorFloor()
{
	AddFloor();
}

void ANavPlayerController::RemoveActiveInteriorFloor()
{
	RemoveFloor(GetActiveFloor());
}

void ANavPlayerController::ConfirmShapeTest()
{
	// TEMPORARY TEST INPUT — stands in for Padre's UI form (shape dropdown + parameter fields)
	// until it exists. Once the UI calls Generator->CreateRectangleBuilding(...) /
	// CreateRegularPolygonBuilding(...) directly, delete this function and the R-key/shape-mode
	// plumbing above. Those Create...Building functions themselves are the permanent part.
	if (!Generator)
	{
		return;
	}

	const FString FilePath = FPaths::ProjectDir() / TEXT("Data") / TEXT("test_shape_params.json");

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("ConfirmShapeTest: could not read %s"), *FilePath);
		return;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("ConfirmShapeTest: failed to parse %s"), *FilePath);
		return;
	}

	FString Shape = TEXT("rectangle");
	RootObject->TryGetStringField(TEXT("shape"), Shape);

	const FString Id = RootObject->GetStringField(TEXT("id"));
	const FString Name = RootObject->GetStringField(TEXT("name"));

	FVector2D Position(0.f, 0.f);
	const TArray<TSharedPtr<FJsonValue>>* PositionArray;
	if (RootObject->TryGetArrayField(TEXT("position"), PositionArray) && PositionArray->Num() >= 2)
	{
		Position.X = (*PositionArray)[0]->AsNumber();
		Position.Y = (*PositionArray)[1]->AsNumber();
	}

	float RotationDegrees = 0.f;
	int32 FloorCount = 5;
	float FloorHeight = 300.f;
	RootObject->TryGetNumberField(TEXT("rotationDegrees"), RotationDegrees);
	RootObject->TryGetNumberField(TEXT("floorCount"), FloorCount);
	RootObject->TryGetNumberField(TEXT("floorHeight"), FloorHeight);

	APolyBuilding* NewBuilding = nullptr;

	if (Shape == TEXT("polygon"))
	{
		int32 Sides = 6;
		float Radius = 200.f;
		RootObject->TryGetNumberField(TEXT("sides"), Sides);
		RootObject->TryGetNumberField(TEXT("radius"), Radius);

		NewBuilding = Generator->CreateRegularPolygonBuilding(Id, Name, Position, Sides, Radius, RotationDegrees, FloorCount, FloorHeight);
	}
	else
	{
		float Width = 300.f;
		float Depth = 300.f;
		RootObject->TryGetNumberField(TEXT("width"), Width);
		RootObject->TryGetNumberField(TEXT("depth"), Depth);

		NewBuilding = Generator->CreateRectangleBuilding(Id, Name, Position, Width, Depth, RotationDegrees, FloorCount, FloorHeight);
	}

	if (!NewBuilding)
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not create building - see error above. Edit the id in test_shape_params.json and try again."));
	}
}