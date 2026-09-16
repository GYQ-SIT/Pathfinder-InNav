// Fill out your copyright notice in the Description page of Project Settings.


#include "NavPlayerController.h"
#include "PolyBuilding.h"
#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
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
	InputComponent->BindKey(EKeys::I, IE_Pressed, this, &ANavPlayerController::LoadInteriorForSelectedBuilding);
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ANavPlayerController::OnConfirmPressed);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ANavPlayerController::OnCancelPressed);
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

	if (bShapeTestMode)
	{
		// Modes are mutually exclusive - only one should interpret Enter/Esc at a time.
		bShapeTestMode = false;
		OnShapeModeChanged.Broadcast(false);
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
		return;
	}

	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("No BuildingGenerator found in the level"));
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
		return;
	}

	if (bUsingAutoId)
	{
		NextNewBuildingIndex++;
	}
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

APolyBuilding* ANavPlayerController::CreateRectangleBuilding(const FString& Id, const FString& Name, FVector2D Position, float Width, float Depth, float RotationDegrees, int32 FloorCount, float FloorHeight)
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRectangleBuilding: no BuildingGenerator found in the level"));
		return nullptr;
	}

	return Generator->CreateRectangleBuilding(Id, Name, Position, Width, Depth, RotationDegrees, FloorCount, FloorHeight);
}

APolyBuilding* ANavPlayerController::CreateRegularPolygonBuilding(const FString& Id, const FString& Name, FVector2D Position, int32 Sides, float Radius, float RotationDegrees, int32 FloorCount, float FloorHeight)
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRegularPolygonBuilding: no BuildingGenerator found in the level"));
		return nullptr;
	}

	return Generator->CreateRegularPolygonBuilding(Id, Name, Position, Sides, Radius, RotationDegrees, FloorCount, FloorHeight);
}

int32 ANavPlayerController::ImportBuildingDataFromFile()
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("ImportBuildingDataFromFile: no BuildingGenerator found in the level"));
		return 0;
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		UE_LOG(LogTemp, Error, TEXT("ImportBuildingDataFromFile: desktop platform not available"));
		return 0;
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
		return 0;
	}

	const int32 Count = Generator->LoadBuildingsFromFile(OutFiles[0]);
	UE_LOG(LogTemp, Warning, TEXT("ImportBuildingDataFromFile: loaded %d buildings from %s"), Count, *OutFiles[0]);
	return Count;
}

void ANavPlayerController::LoadInteriorForSelectedBuilding()
{
	if (!InteriorGen)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadInteriorForSelectedBuilding: no InteriorGenerator placed in the level"));
		return;
	}

	if (!SelectedBuilding)
	{
		UE_LOG(LogTemp, Warning, TEXT("LoadInteriorForSelectedBuilding: click a building first"));
		return;
	}

	InteriorGen->LoadInteriorForBuilding(SelectedBuilding->BuildingId);
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