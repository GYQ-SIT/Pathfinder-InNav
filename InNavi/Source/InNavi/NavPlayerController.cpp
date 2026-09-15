// Fill out your copyright notice in the Description page of Project Settings.


#include "NavPlayerController.h"
#include "PolyBuilding.h"
#include "BuildingGenerator.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"

ANavPlayerController::ANavPlayerController()
{
	SelectedBuilding = nullptr;
	Generator = nullptr;
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
}

void ANavPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindAction("SelectBuilding", IE_Pressed, this, &ANavPlayerController::OnClick);
	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ANavPlayerController::ToggleBuildMode);
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ANavPlayerController::FinishBuilding);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ANavPlayerController::CancelBuilding);
}

void ANavPlayerController::OnClick()
{
	if (bBuildMode)
	{
		AddFootprintPointUnderCursor();
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

			UE_LOG(LogTemp, Warning, TEXT("Selected building: %s"), *ClickedBuilding->BuildingName);
		}
	}
}

void ANavPlayerController::ToggleBuildMode()
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter build mode: no BuildingGenerator found in the level"));
		return;
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

	UE_LOG(LogTemp, Warning, TEXT("Footprint point %d added: (%.1f, %.1f)"), PendingFootprint.Num(), NewPoint.X, NewPoint.Y);
}

void ANavPlayerController::FinishBuilding()
{
	if (!bBuildMode)
	{
		return;
	}

	if (PendingFootprint.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("Need at least 3 points to finish a building (have %d)"), PendingFootprint.Num());
		return;
	}

	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("No BuildingGenerator found in the level"));
		return;
	}

	const FString NewId = FString::Printf(TEXT("NEW_%d"), NextNewBuildingIndex);
	APolyBuilding* NewBuilding = Generator->SpawnBuildingFromData(NewId, NewId, PendingFootprint, 5, 300.f, FVector2D::ZeroVector);

	if (!NewBuilding)
	{
		// Spawn was rejected (e.g. self-intersecting outline) - keep the drawn points and preview
		// so the user can see what's there; Esc still cancels and starts over.
		UE_LOG(LogTemp, Warning, TEXT("Could not finish this building - see error above. Press Esc to discard and redraw."));
		return;
	}

	NextNewBuildingIndex++;
	FlushPersistentDebugLines(GetWorld());
	PendingFootprint.Reset();
}

void ANavPlayerController::CancelBuilding()
{
	FlushPersistentDebugLines(GetWorld());
	PendingFootprint.Reset();
}