// Fill out your copyright notice in the Description page of Project Settings.


#include "NavPlayerController.h"
#include "NavigationPanelWidget.h"
#include "MobileAppWidget.h"
#include "MobileSearchPageWidget.h"
#include "MobileSplashWidget.h"
#include "ConfirmDialogWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "TimerManager.h"
#include "NavigationRouteVisualization.h"
#include "InteriorFloorPlanWidget.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Pawn.h"
#include "PolyBuilding.h"
#include "InteriorBuildingPreviewPivot.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "ProjectLibrary.h"
#include "InteriorRoom.h"
#include "InteriorConnectorMarker.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "DrawDebugHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#if PLATFORM_DESKTOP
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#endif
#include "Framework/Application/SlateApplication.h"
#include "UObject/UObjectIterator.h"
#include "HAL/FileManager.h"
#include "WavFileLoader.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"

// ---- Helper: is Point inside the (possibly concave) polygon Poly? (even-odd ray-casting test) ----
static bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Poly)
{
	bool bInside = false;
	const int32 N = Poly.Num();
	for (int32 i = 0, j = N - 1; i < N; j = i++)
	{
		const FVector2D& Pi = Poly[i];
		const FVector2D& Pj = Poly[j];
		if (((Pi.Y > Point.Y) != (Pj.Y > Point.Y)) &&
			(Point.X < (Pj.X - Pi.X) * (Point.Y - Pi.Y) / (Pj.Y - Pi.Y) + Pi.X))
		{
			bInside = !bInside;
		}
	}
	return bInside;
}

ANavPlayerController::ANavPlayerController()
{
	// AController is hidden by default. This controller now owns visible route components.
	SetHidden(false);
	RouteVisualization = CreateDefaultSubobject<UNavigationRouteVisualization>(TEXT("NavigationRouteVisualization"));
	SelectedBuilding = nullptr;
	Generator = nullptr;
	InteriorGen = nullptr;
}
void ANavPlayerController::ClearCampusInteriorPreview()
{
	for (TPair<FString, FCampusPreviewRooms>& Entry : CampusPreview)
	{
		for (const TWeakObjectPtr<AInteriorRoom>& Room : Entry.Value.Rooms)
		{
			if (Room.IsValid())
			{
				Room->Destroy();
			}
		}
	}

	CampusPreview.Empty();
}

void ANavPlayerController::HideCampusInteriorPreview()
{
	bCampusPreviewAllowed = false;
	for (TPair<FString, FCampusPreviewRooms>& Entry : CampusPreview)
	{
		for (const TWeakObjectPtr<AInteriorRoom>& Room : Entry.Value.Rooms)
		{
			if (Room.IsValid())
			{
				Room->SetActorHiddenInGame(true);
			}
		}
		Entry.Value.bShown = false;
	}
}

void ANavPlayerController::BuildCampusInteriorPreview()
{
	ClearCampusInteriorPreview();

	if (!Generator || !GetWorld())
	{
		return;
	}

	int32 TotalRooms = 0;

	for (APolyBuilding* Building : Generator->GetAllBuildings())
	{
		if (!IsValid(Building))
		{
			continue;
		}

		const TSharedPtr<FJsonObject> InteriorData =
			Generator->GetInteriorDataForBuilding(Building->BuildingId);

		FCampusPreviewRooms& BuildingPreview = CampusPreview.FindOrAdd(Building->BuildingId);

		const TArray<TSharedPtr<FJsonValue>>* Floors = nullptr;

		if (!InteriorData.IsValid() ||
			!InteriorData->TryGetArrayField(TEXT("floors"), Floors))
		{
			continue;
		}

		for (const TSharedPtr<FJsonValue>& FloorValue : *Floors)
		{
			const TSharedPtr<FJsonObject> Floor = FloorValue->AsObject();

			if (!Floor.IsValid())
			{
				continue;
			}

			int32 Level = 1;
			Floor->TryGetNumberField(TEXT("level"), Level);

			float WallHeight = 280.f;
			float WallThickness = 20.f;

			Floor->TryGetNumberField(TEXT("wallHeight"), WallHeight);
			Floor->TryGetNumberField(TEXT("wallThickness"), WallThickness);

			const TArray<TSharedPtr<FJsonValue>>* Rooms = nullptr;

			if (!Floor->TryGetArrayField(TEXT("rooms"), Rooms))
			{
				continue;
			}

			for (const TSharedPtr<FJsonValue>& RoomValue : *Rooms)
			{
				const TSharedPtr<FJsonObject> RoomData =
					RoomValue->AsObject();

				if (!RoomData.IsValid())
				{
					continue;
				}

				const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;

				if (!RoomData->TryGetArrayField(TEXT("footprint"), Points))
				{
					continue;
				}

				TArray<FVector2D> Footprint;

				for (const TSharedPtr<FJsonValue>& PointValue : *Points)
				{
					const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;

					if (PointValue.IsValid() &&
						PointValue->TryGetArray(Pair) &&
						Pair->Num() >= 2)
					{
						Footprint.Add(FVector2D(
							(*Pair)[0]->AsNumber(),
							(*Pair)[1]->AsNumber()
						));
					}
				}

				bool bClosedLoop = true;
				RoomData->TryGetBoolField(TEXT("closed"), bClosedLoop);

				if (Footprint.Num() < (bClosedLoop ? 3 : 2))
				{
					continue;
				}

				AInteriorRoom* PreviewRoom =
					GetWorld()->SpawnActor<AInteriorRoom>(
						FVector::ZeroVector,
						FRotator::ZeroRotator
					);

				if (!IsValid(PreviewRoom))
				{
					continue;
				}

				RoomData->TryGetStringField(
					TEXT("id"), PreviewRoom->RoomId);

				RoomData->TryGetStringField(
					TEXT("name"), PreviewRoom->RoomName);

				PreviewRoom->FloorLevel = Level;

				// Preview only: do not interfere with building selection.
				PreviewRoom->SetActorEnableCollision(false);

				const float FloorBaseZ =
					(Level - 1) * Building->GetFloorHeight();

				TArray<FInteriorOpening> PreviewOpenings;

				PreviewRoom->BuildWalls(
					Footprint,
					Generator->GetScale(),
					Building->BuildingOffset,
					FloorBaseZ,
					WallHeight,
					WallThickness,
					PreviewOpenings,
					bClosedLoop
				);

				PreviewRoom->SetActorEnableCollision(false);

				// Out of sight until the camera is close enough for UpdateBuildingTransparency to show them.
				PreviewRoom->SetActorHiddenInGame(true);

				// Drawn after the walls of the buildings whatever the camera does, so the rooms and the walls around them do not trade places.
				// The see-through sort order of rooms: after the walls of every building (those count from 0).
				constexpr int32 CampusRoomSortPriority = 1000;
				TArray<UPrimitiveComponent*> RoomParts;
				PreviewRoom->GetComponents(RoomParts);
				for (UPrimitiveComponent* RoomPart : RoomParts)
				{
					RoomPart->SetTranslucentSortPriority(CampusRoomSortPriority);
				}

				BuildingPreview.Rooms.Add(PreviewRoom);
				++TotalRooms;
			}
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[Campus XRay] Created %d preview rooms"),
		TotalRooms);

	UpdateBuildingTransparency();
}
void ANavPlayerController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	LoadExteriorCameraDefaultFromDisk();

	NavigationPanel = CreateWidget<UNavigationPanelWidget>(this, UNavigationPanelWidget::StaticClass());
	if (NavigationPanel)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Navigation] Navigation panel created successfully"));
		NavigationPanel->AddToViewport(100);
		NavigationPanel->SetVisibility(ESlateVisibility::Collapsed);
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
		// Keeps the 3D preview's route/start/destination markers live as the user picks points and
		// finds/clears a route - see RefreshRoute3DVisualization's own comment.
		NavigationPanel->OnStateChanged.AddUniqueDynamic(this, &ANavPlayerController::RefreshRoute3DVisualization);
		NavigationPanel->OnGuidedStepChanged.AddUniqueDynamic(this, &ANavPlayerController::FollowNavigationStep);
	}

	if (TActorIterator<ABuildingGenerator> It(GetWorld()); It)
	{
		Generator = *It;
	}

	if (TActorIterator<AInteriorGenerator> It(GetWorld()); It)
	{
		InteriorGen = *It;
	}
	if (NavigationPanel)
	{
		NavigationPanel->SetNavigationContext(Generator, InteriorGen);
	}
	UE_LOG(LogTemp, Warning, TEXT("[Navigation] BeginPlay references: Generator=%s Interior=%s"),
		Generator ? TEXT("valid") : TEXT("NULL"),
		InteriorGen ? TEXT("valid") : TEXT("NULL"));

	// -MobileApp (always on a phone): the start screen is put up by the level a moment after this, so wait a little before taking everything down.
#if PLATFORM_ANDROID || PLATFORM_IOS
	const bool bPhone = true;
#else
	const bool bPhone = FParse::Param(FCommandLine::Get(), TEXT("MobileApp"));
#endif
	if (bPhone)
	{
		// Black over everything at once, so that the desktop start screen the level puts up never shows; MobileApp builds the app under it.
		MobileSplash = CreateWidget<UMobileSplashWidget>(this, UMobileSplashWidget::StaticClass());
		if (MobileSplash)
		{
			MobileSplash->AddToViewport(UMobileSplashWidget::ZOrder);
		}
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, this, &ANavPlayerController::MobileApp, 0.5f, false);
	}
}

void ANavPlayerController::MobileApp()
{
	// Everything the level put up goes, but not the splash that covers it.
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UUserWidget::StaticClass(), true);
	for (UUserWidget* Widget : Widgets)
	{
		if (Widget != MobileSplash)
		{
			Widget->RemoveFromParent();
		}
	}
	if (UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(this, UMobileAppWidget::StaticClass()))
	{
		// The look (theme and text size) is read before the screen is built, so it is built in it.
		Screen->UsePreferencesFile(FMobilePreferences::DefaultFile());
		Screen->AddToViewport();
		// The saved places are kept in a file; for looking at the tab, -MobileSave=E2-01-101,PLM-01-102 saves some without one.
		FString PreviewIds;
		if (FParse::Value(FCommandLine::Get(), TEXT("MobileSave="), PreviewIds, false))
		{
			Screen->PreviewSave(PreviewIds);
		}
		else
		{
			Screen->UseSavedFile(FMobileSavedPlaces::DefaultFile());
		}
		// For trying a screen straight away: -MobileTab=Directory and -MobileState=Route.
		FString Tab, State;
		if (FParse::Value(FCommandLine::Get(), TEXT("MobileTab="), Tab))
		{
			Screen->SetActiveTab(Tab == TEXT("Directory") ? UMobileAppWidget::ETab::Directory : Tab == TEXT("Saved") ? UMobileAppWidget::ETab::Saved : Tab == TEXT("Profile") ? UMobileAppWidget::ETab::Profile : UMobileAppWidget::ETab::Explore);
		}
		if (FParse::Value(FCommandLine::Get(), TEXT("MobileState="), State))
		{
			Screen->PreviewState(State == TEXT("Place") ? EMobileSheetState::Place : State == TEXT("Route") ? EMobileSheetState::Route : State == TEXT("Navigating") ? EMobileSheetState::Navigating : State == TEXT("Arrived") ? EMobileSheetState::Arrived : EMobileSheetState::Search);
		}
		// For trying the search page: -MobileQuery=lounge, -MobileBrowse, -MobileFilters, and the sheet's height with -MobileSnap=Full.
		FString Query, SnapName;
		if (UMobileSearchPageWidget* SearchPage = Screen->GetSearchPage())
		{
			if (FParse::Value(FCommandLine::Get(), TEXT("MobileQuery="), Query))
			{
				SearchPage->SetQuery(Query);
			}
			if (FParse::Param(FCommandLine::Get(), TEXT("MobileBrowse")))
			{
				SearchPage->BrowseAll();
			}
			if (FParse::Param(FCommandLine::Get(), TEXT("MobileFilters")))
			{
				SearchPage->SetFiltersOpen(true);
			}
		}
		// For the route page: -MobileOpen=From|To|Building|Level opens that drop-down.
		FString OpenName;
		if (FParse::Value(FCommandLine::Get(), TEXT("MobileOpen="), OpenName))
		{
			Screen->PreviewOpenSelect(OpenName);
		}
		// For the map: -MobileOverview, -Mobile3D, -MobileLevelList, and -MobileMapLevel=2 (a level of the building shown).
		FString MapLevelText;
		if (FParse::Param(FCommandLine::Get(), TEXT("MobileOverview")))
		{
			Screen->ToggleOverview();
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("Mobile3D")))
		{
			Screen->SetMap3D(true);
		}
		if (FParse::Value(FCommandLine::Get(), TEXT("MobileMapLevel="), MapLevelText))
		{
			Screen->ChooseMapLevel(FCString::Atoi(*MapLevelText));
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("MobileLevelList")))
		{
			Screen->SetLevelPopupOpen(true);
		}
		if (FParse::Value(FCommandLine::Get(), TEXT("MobileSnap="), SnapName))
		{
			Screen->SetSnap(SnapName == TEXT("Full") ? EMobileSheetSnap::Full : SnapName == TEXT("Collapsed") ? EMobileSheetSnap::Collapsed : EMobileSheetSnap::Half);
			Screen->SnapSheetNow();
		}
	}
	// The app is built under the splash: the splash goes as soon as the logo has had its time.
	if (MobileSplash)
	{
		MobileSplash->SetAppReady();
	}
}

bool ANavPlayerController::IsSelectingNavigationPoint() const
{
	return NavigationPanel && NavigationPanel->IsSelectingPoint();
}

void ANavPlayerController::SelectOutdoorPointUnderCursor()
{
	if (!NavigationPanel || !Generator || Generator->GetScale() <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// A click on a building is not a click on the ground: the ray into the ground behind it would give a spot the user did not point at.
	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);
	const bool bOnBuilding = Hit.bBlockingHit && Cast<APolyBuilding>(Hit.GetActor()) != nullptr;

	FVector Origin = FVector::ZeroVector, Direction = FVector::DownVector;
	if (!bOnBuilding && !DeprojectMousePositionToWorld(Origin, Direction))
	{
		return;
	}
	FVector2D CampusPosition = FVector2D::ZeroVector;
	if (!bOnBuilding)
	{
		// The ground is the plane the buildings stand on, Z = 0.
		if (FMath::Abs(Direction.Z) < KINDA_SMALL_NUMBER)
		{
			return;
		}
		const FVector OnGround = FMath::RayPlaneIntersection(Origin, Direction, FPlane(FVector::ZeroVector, FVector::UpVector));
		CampusPosition = FVector2D(OnGround.X, OnGround.Y) / Generator->GetScale();
	}
	NavigationPanel->SelectOutdoorGroundPoint(CampusPosition, bOnBuilding);
}

void ANavPlayerController::SelectNavigationPoint(FVector2D FootprintPosition, int32 Floor)
{
	if (bInteriorInspectionEnabled) return;
	UE_LOG(LogTemp, Warning,
		TEXT("[Navigation] Controller forwarding point: position=(%.2f, %.2f), floor=%d, panel=%s"),
		FootprintPosition.X, FootprintPosition.Y, Floor,
		NavigationPanel ? TEXT("valid") : TEXT("NULL"));

	if (NavigationPanel)
	{
		NavigationPanel->SelectPoint(FootprintPosition, Floor);
	}
}

void ANavPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

#if PLATFORM_DESKTOP
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Game && GetLocalPlayer())
	{
		BindGameWindowCloseGuard(GetWorld()->GetGameViewport());
	}
#endif

	InputComponent->BindAction("SelectBuilding", IE_Pressed, this, &ANavPlayerController::OnClick);
	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ANavPlayerController::ToggleBuildMode);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ANavPlayerController::ToggleShapeTestMode);
	InputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &ANavPlayerController::CycleInteriorFloorUp);
	InputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &ANavPlayerController::CycleInteriorFloorDown);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ANavPlayerController::AddInteriorFloor);
	InputComponent->BindKey(EKeys::X, IE_Pressed, this, &ANavPlayerController::RemoveActiveInteriorFloor);
	InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ANavPlayerController::ToggleDrawRoomMode);
	InputComponent->BindKey(EKeys::L, IE_Pressed, this, &ANavPlayerController::ToggleDrawWallMode);
	InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &ANavPlayerController::ToggleSelectMode);
	// Not "D" - DefaultPawn's free-fly movement already uses WASD, so "D" would double up as
	// "strafe right" every time this mode gets toggled.
	InputComponent->BindKey(EKeys::K, IE_Pressed, this, &ANavPlayerController::ToggleAddDoorMode);
	InputComponent->BindKey(EKeys::BackSpace, IE_Pressed, this, &ANavPlayerController::DeleteSelectionKeyHandler);
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ANavPlayerController::TogglePrefabRectangleMode);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ANavPlayerController::TogglePrefabTriangleMode);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ANavPlayerController::TogglePrefabCircleMode);
	InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ANavPlayerController::ToggleElevatorMode);
	InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &ANavPlayerController::ToggleStairsMode);
	InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &ANavPlayerController::ToggleEscalatorMode);
	InputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &ANavPlayerController::GrowSelectedRoom);
	InputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &ANavPlayerController::ShrinkSelectedRoom);
	InputComponent->BindKey(EKeys::Nine, IE_Pressed, this, &ANavPlayerController::StartRotateCCW);
	InputComponent->BindKey(EKeys::Nine, IE_Released, this, &ANavPlayerController::StopRotateCCW);
	InputComponent->BindKey(EKeys::Zero, IE_Pressed, this, &ANavPlayerController::StartRotateCW);
	InputComponent->BindKey(EKeys::Zero, IE_Released, this, &ANavPlayerController::StopRotateCW);
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ANavPlayerController::OnConfirmPressed);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ANavPlayerController::OnCancelPressed);
	InputComponent->BindKey(EKeys::N, IE_Pressed, this, &ANavPlayerController::ToggleSnapEnabled);
	InputComponent->BindKey(FInputChord(EKeys::S, /*Shift*/false, /*Ctrl*/true, /*Alt*/false, /*Cmd*/false), IE_Pressed, this, &ANavPlayerController::SaveCurrentProjectKeyHandler);
	InputComponent->BindKey(FInputChord(EKeys::Z, false, true, false, false), IE_Pressed, this, &ANavPlayerController::UndoInteriorKeyHandler);
	InputComponent->BindKey(FInputChord(EKeys::Y, false, true, false, false), IE_Pressed, this, &ANavPlayerController::RedoInteriorKeyHandler);
	InputComponent->BindKey(FInputChord(EKeys::C, false, true, false, false),IE_Pressed,this,&ANavPlayerController::CopyInteriorKeyHandler);
	InputComponent->BindKey(FInputChord(EKeys::V, false, true, false, false), IE_Pressed, this, &ANavPlayerController::PasteInteriorKeyHandler);
	InputComponent->BindKey(EKeys::G, IE_Pressed, this, &ANavPlayerController::LogCameraTransform);
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ANavPlayerController::ZoomIn3DPreview);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ANavPlayerController::ZoomOut3DPreview);
	// Separate from the "SelectBuilding" action above - carries the 3D Edit mode room-drag gesture
	// (see OnLeftMouseDown3DEdit/Up), while OnClick still fires for the plain-click (select/draw/etc.)
	// side of the same button.
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ANavPlayerController::OnLeftMouseDown3DEdit);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ANavPlayerController::OnLeftMouseUp3DEdit);
	// Isolate: the right button turns the model (the left one pans it, above).
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ANavPlayerController::OnRightMouseDownInspection);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &ANavPlayerController::OnRightMouseUpInspection);
}

void ANavPlayerController::OnClick()
{
	if (bInteriorInspectionEnabled) return;
	UE_LOG(LogTemp, Warning, TEXT("[Navigation] PlayerController OnClick: selecting=%s build=%s"),
		IsSelectingNavigationPoint() ? TEXT("true") : TEXT("false"),
		bBuildMode ? TEXT("true") : TEXT("false"));

	// Ignore clicks that happen on the navigation panel buttons.
	if (NavigationPanel &&
		NavigationPanel->IsPointerOverPanel())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Navigation] PlayerController OnClick ignored: pointer is over navigation panel"));
		return;
	}

	// Ignore clicks that land on a dock panel (see SetPointerOverUICallback).
	if (IsPointerOverUI())
	{
		return;
	}

	// The 2D floor-plan widget normally handles navigation-point clicks. Keep
	// the original 3D deprojection as a fallback because GameAndUI input can
	// route the same mouse click to the PlayerController without delivering it
	// to the custom floor-plan widget.
	if (IsSelectingNavigationPoint())
	{
		// On the exterior screen no building is open: the click picks a spot on the ground.
		if (NavigationPanel && NavigationPanel->IsOutdoorMode())
		{
			SelectOutdoorPointUnderCursor();
			return;
		}

		// In 3D preview the floor-plan widget is Collapsed, so there is nothing to hit-test there: the click is
		// turned into a point on the active floor instead, by casting the mouse ray onto that floor's plane (a
		// hit on a wall or a floor edge would shift the point sideways on an oblique camera). The panel then
		// treats it exactly like a click on the 2D plan - the same rules decide whether the point is valid.
		if (bIsIn3DPreviewMode)
		{
			const int32 ActiveFloor = InteriorGen ? InteriorGen->GetActiveFloor() : 0;
			FVector2D FootprintPoint;
			const FHitResult NoHit;
			if (ActiveFloor >= 1 && Get3DFloorFootprintPoint(NoHit, FootprintPoint))
			{
				SelectNavigationPoint(FootprintPoint, ActiveFloor);
			}
			return;
		}

		// The floor plan is a 2D UMG widget, so navigation clicks must be converted
		// using that widget's cached screen geometry. The old fallback deprojected
		// the click through the 3D camera, which made the result depend on the camera
		// and caused most visible floor clicks to be rejected as outside the building.
		const FVector2D CursorScreenPosition = FSlateApplication::Get().GetCursorPos();
		for (TObjectIterator<UInteriorFloorPlanWidget> It; It; ++It)
		{
			UInteriorFloorPlanWidget* FloorPlan = *It;
			if (!FloorPlan || FloorPlan->GetWorld() != GetWorld() || !FloorPlan->IsVisible())
			{
				continue;
			}

			const FGeometry& FloorGeometry = FloorPlan->GetCachedGeometry();
			if (!FloorGeometry.IsUnderLocation(CursorScreenPosition))
			{
				continue;
			}

			if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Navigation] 2D controller fallback rejected: interior is not loaded"));
				return;
			}

			const int32 Floor = InteriorGen->GetActiveFloor();
			if (Floor < 1)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Navigation] 2D controller fallback rejected: active floor=%d"), Floor);
				return;
			}

			const FVector2D LocalPosition =
				FloorGeometry.AbsoluteToLocal(CursorScreenPosition);
			const FVector2D FootprintPosition =
				FloorPlan->LocalPositionToFootprintPoint(
					LocalPosition,
					FloorGeometry.GetLocalSize());

			UE_LOG(LogTemp, Warning,
				TEXT("[Navigation] 2D click routed by controller: local=(%.2f, %.2f) footprint=(%.2f, %.2f), floor=%d"),
				LocalPosition.X, LocalPosition.Y,
				FootprintPosition.X, FootprintPosition.Y, Floor);

			SelectNavigationPoint(FootprintPosition, Floor);
			return;
		}
	
		// Do not fall back to the 3D camera for a 2D floor-plan click. If the
		// widget cannot be found or the cursor is outside it, report the problem
		// instead of producing a misleading random footprint position.
		UE_LOG(LogTemp, Error,
			TEXT("[Navigation] Navigation click did not hit a visible 2D floor-plan widget; 3D fallback skipped"));
		return;

	}

	// Build mode uses the mouse click to add a building footprint point.
	if (bBuildMode)
	{
		AddFootprintPointUnderCursor();
	}
	else if (
		bDrawRoomMode ||
		bDrawWallMode ||
		bSelectMode ||
		bAddDoorMode ||
		ActivePrefabShape != 0 ||
		ActiveConnectorType != 0)
	{
		// In 2D, interior-editing clicks are handled by InteriorFloorPlanWidget. In 3D, Edit mode
		// routes a click through the full dispatch (draw/prefab/connector/door/select - see
		// Handle3DEditClick). View mode is more restricted - looking without editing is still useful
		// (Select mode lets you click a room/door/connector to see it in the Properties panel), but
		// none of the mutating modes make sense there, so it goes straight to selection instead of
		// Handle3DEditClick's full dispatch. bSelectMode is the only mode flag not itself gated to
		// Edit mode (see the mode toolbar buttons), so a stray click while some other mode happens to
		// still be active from before switching to View is correctly still a no-op here.
		if (bIsIn3DPreviewMode)
		{
			if (bIsIn3DEditMode)
			{
				Handle3DEditClick();
			}
			else if (bSelectMode)
			{
				FHitResult Hit;
				GetHitResultUnderCursor(ECC_Visibility, false, Hit);
				Select3DEditTarget(Hit);
			}
		}
	}
	else
	{
		// If no special mode is active,
		// select a building in the 3D view.
		SelectBuildingUnderCursor();
	}
}

bool ANavPlayerController::IsPointerOverInteriorFloorPlan() const
{
	const FVector2D Cursor = FSlateApplication::Get().GetCursorPos();
	for (TObjectIterator<UInteriorFloorPlanWidget> It; It; ++It)
	{
		// A plan that is no longer on screen has let go of its Slate widget, even if the object has not been collected yet.
		const UInteriorFloorPlanWidget* FloorPlan = *It;
		if (FloorPlan && FloorPlan->GetWorld() == GetWorld() && FloorPlan->GetCachedWidget().IsValid() && FloorPlan->IsVisible()
			&& FloorPlan->GetCachedGeometry().IsUnderLocation(Cursor))
		{
			return true;
		}
	}
	return false;
}

void ANavPlayerController::SelectBuildingUnderCursor()
{
	// The yellow "selected building" highlight only makes sense when browsing the exterior building
	// cluster - not while previewing one specific building's interior in 3D, where the same click
	// would otherwise land on that building's own (now visible, collidable) exterior wall.
	if (bIsIn3DPreviewMode || IsPointerOverInteriorFloorPlan())
	{
		return;
	}

	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	APolyBuilding* ClickedBuilding = Hit.bBlockingHit ? Cast<APolyBuilding>(Hit.GetActor()) : nullptr;

	// Clicking empty space, or re-clicking the already-selected building, deselects it - previously
	// this only ever added or switched a selection, with no way to clear it once nothing new was hit.
	if (!ClickedBuilding || ClickedBuilding == SelectedBuilding)
	{
		if (SelectedBuilding)
		{
			SelectedBuilding->SetHighlighted(false);
			SelectedBuilding = nullptr;
			OnBuildingSelected.Broadcast(FString());
		}
		return;
	}

	if (SelectedBuilding)
	{
		SelectedBuilding->SetHighlighted(false);
	}

	ClickedBuilding->SetHighlighted(true);
	SelectedBuilding = ClickedBuilding;
	OnBuildingSelected.Broadcast(ClickedBuilding->BuildingId);
	// Only exterior building selection gets this sound - deselecting (above) and every other kind of
	// selection in the app (rooms/doors/connectors in the interior editor) don't go through here.
	PlayBuildingSelectSound();

	UE_LOG(LogTemp, Warning, TEXT("Selected building: %s"), *ClickedBuilding->BuildingName);
}

void ANavPlayerController::ToggleBuildMode()
{
	if (bInteriorInspectionEnabled) return;
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter build mode: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("Cannot enter build mode: no BuildingGenerator found in the level."), EActionFailureSeverity::Warning);
		return;
	}

	if (bShapeTestMode)
	{
		// Modes are mutually exclusive - only one should interpret Enter/Esc at a time.
		bShapeTestMode = false;
		OnShapeModeChanged.Broadcast(false);
	}
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitAddDoorMode();
	ExitConnectorMode();

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
	if (bInteriorInspectionEnabled) return;
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
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitAddDoorMode();
	ExitConnectorMode();

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
namespace
{
	// How the exterior camera reveals a building, by its distance (cm) from the bounding box of the building.
	constexpr float CampusRevealMinimumGap = 100.f;          // the far distance stays at least this much beyond the near one
	constexpr float CampusRevealHysteresis = 0.05f;          // the rooms of a building that shows them stay until its opacity is this much above the limit
	constexpr float CampusRevealNearOpacity = 0.12f;         // the wall/roof opacity at that closest distance
	constexpr float CampusRevealRoomsBelowOpacity = 0.8f;    // the rooms inside show once the walls are this clear (any earlier they would show through solid walls)
}

void ANavPlayerController::UpdateBuildingTransparency()
{
	if (!Generator)
		return;

	if (bInteriorInspectionEnabled || bIsIn3DPreviewMode)
	{
		for (APolyBuilding* Building : Generator->GetAllBuildings())
		{
			if (IsValid(Building))
			{
				Building->SetBuildingOpacity(1.0f);
			}
		}
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
		return;

	const FVector CameraLocation =
		PlayerCameraManager
		? PlayerCameraManager->GetCameraLocation()
		: ControlledPawn->GetActorLocation();

	// Every building is judged by how far the camera is from it: far away it is the whole, solid building with its roof; coming closer its walls
	// and roof fade out, and once they can be looked through the rooms inside show.
	int32 SortOrder = 0;
	for (APolyBuilding* Building : Generator->GetAllBuildings())
	{
		if (!IsValid(Building))
			continue;

		// A fixed order for everything see-through, so that buildings that touch do not trade places as the camera moves.
		Building->SetTranslucentSortOrder(SortOrder++);

		FVector Origin, Extent;
		Building->GetActorBounds(false, Origin, Extent);

		// Distance to the bounding box of the building
		const FVector ClosestPoint = CameraLocation.BoundToBox(
			Origin - Extent,
			Origin + Extent
		);

		const float Distance =
			FVector::Distance(CameraLocation, ClosestPoint);

		const FBuildingRevealChoice* Choice = BuildingReveal.Find(Building->BuildingId);
		const bool bMadeTransparent = Choice && Choice->bTransparent;
		const int32 RoomFloor = Choice ? Choice->RoomFloor : 0;

		// Made transparent from the Building panel wins over everything; otherwise the camera distance decides, if see-through is on at all.
		const float Opacity = bMadeTransparent
			? CampusRevealNearOpacity
			: (bCampusSeeThroughEnabled
				? FMath::GetMappedRangeValueClamped(
					FVector2D(CampusRevealFar, CampusRevealNear),
					FVector2D(1.0f, CampusRevealNearOpacity),
					Distance)
				: 1.0f);

		Building->SetBuildingOpacity(Opacity);

		if (FCampusPreviewRooms* Preview = CampusPreview.Find(Building->BuildingId))
		{
			const bool bShowRooms = bCampusPreviewAllowed && !Building->IsHidden()
				&& (bMadeTransparent || Opacity < CampusRevealRoomsBelowOpacity + (Preview->bShown ? CampusRevealHysteresis : 0.f));
			if (Preview->bShown != bShowRooms || Preview->AppliedFloor != RoomFloor)
			{
				Preview->bShown = bShowRooms;
				Preview->AppliedFloor = RoomFloor;
				for (const TWeakObjectPtr<AInteriorRoom>& Room : Preview->Rooms)
				{
					if (Room.IsValid())
					{
						Room->SetActorHiddenInGame(!(bShowRooms && (RoomFloor <= 0 || Room->FloorLevel == RoomFloor)));
					}
				}
			}
		}
	}
}

void ANavPlayerController::SetCampusRevealFarDistance(float Centimetres)
{
	CampusRevealFar = FMath::Clamp(Centimetres, 2.f * CampusRevealMinimumGap, 50000.f);
	CampusRevealNear = FMath::Min(CampusRevealNear, CampusRevealFar - CampusRevealMinimumGap);
	UpdateBuildingTransparency();
}

void ANavPlayerController::SetCampusRevealNearDistance(float Centimetres)
{
	CampusRevealNear = FMath::Clamp(Centimetres, CampusRevealMinimumGap, 50000.f - CampusRevealMinimumGap);
	CampusRevealFar = FMath::Max(CampusRevealFar, CampusRevealNear + CampusRevealMinimumGap);
	UpdateBuildingTransparency();
}

int32 ANavPlayerController::CountShownCampusRoomsForTests(const FString& BuildingId) const
{
	int32 Shown = 0;
	if (const FCampusPreviewRooms* Preview = CampusPreview.Find(BuildingId))
	{
		for (const TWeakObjectPtr<AInteriorRoom>& Room : Preview->Rooms)
		{
			if (Room.IsValid() && !Room->IsHidden())
			{
				++Shown;
			}
		}
	}
	return Shown;
}

void ANavPlayerController::SetCampusSeeThroughEnabled(bool bEnabled)
{
	bCampusSeeThroughEnabled = bEnabled;
	UpdateBuildingTransparency();
}

bool ANavPlayerController::IsBuildingMadeTransparent(const FString& BuildingId) const
{
	const FBuildingRevealChoice* Choice = BuildingReveal.Find(BuildingId);
	return Choice && Choice->bTransparent;
}

void ANavPlayerController::SetBuildingMadeTransparent(const FString& BuildingId, bool bTransparent)
{
	BuildingReveal.FindOrAdd(BuildingId).bTransparent = bTransparent;
	UpdateBuildingTransparency();
}

int32 ANavPlayerController::GetBuildingRoomFloor(const FString& BuildingId) const
{
	const FBuildingRevealChoice* Choice = BuildingReveal.Find(BuildingId);
	return Choice ? Choice->RoomFloor : 0;
}

void ANavPlayerController::SetBuildingRoomFloor(const FString& BuildingId, int32 Floor)
{
	BuildingReveal.FindOrAdd(BuildingId).RoomFloor = FMath::Max(0, Floor);
	UpdateBuildingTransparency();
}

void ANavPlayerController::ToggleDrawRoomMode()
{
	if (bInteriorInspectionEnabled) return;
	if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter draw room mode: no building interior currently loaded"));
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try drawing a room."), EActionFailureSeverity::Warning);
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
	if (bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitAddDoorMode();
	ExitConnectorMode();

	bDrawRoomMode = true;
	OnDrawRoomModeChanged.Broadcast(true);
	OnEditModeChanged.Broadcast();

	UE_LOG(LogTemp, Warning, TEXT("Draw room mode ON (floor %d): click to add footprint points, Enter to finish, Esc to cancel"), InteriorGen->GetActiveFloor());
}

void ANavPlayerController::ToggleDrawWallMode()
{
	if (bInteriorInspectionEnabled) return;
	if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter draw wall mode: no building interior currently loaded"));
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try drawing a wall."), EActionFailureSeverity::Warning);
		return;
	}

	if (bDrawWallMode)
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
	if (bDrawRoomMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitAddDoorMode();
	ExitConnectorMode();

	bDrawWallMode = true;
	OnDrawWallModeChanged.Broadcast(true);
	OnEditModeChanged.Broadcast();

	UE_LOG(LogTemp, Warning, TEXT("Draw wall mode ON (floor %d): click to add points, Enter to finish, Esc to cancel"), InteriorGen->GetActiveFloor());
}

void ANavPlayerController::ExitDrawRoomMode()
{
	if (bDrawRoomMode)
	{
		CancelRoom();
		bDrawRoomMode = false;
		OnDrawRoomModeChanged.Broadcast(false);
		OnEditModeChanged.Broadcast();
	}
	if (bDrawWallMode)
	{
		CancelWall();
		bDrawWallMode = false;
		OnDrawWallModeChanged.Broadcast(false);
		OnEditModeChanged.Broadcast();
	}

	UE_LOG(LogTemp, Warning, TEXT("Draw room/wall mode OFF"));
}

void ANavPlayerController::ExitSelectMode()
{
	// A 3D room drag needs select mode to have started it (see OnLeftMouseDown3DEdit) - if select
	// mode is turned off mid-drag (self-toggle, Esc, or another mode taking over), finish the drag
	// the same way a mouse-up would, rather than leaving it stuck with SetIgnoreLookInput(true) never
	// balanced back to false. No-op if no drag is in progress.
	OnLeftMouseUp3DEdit();

	if (bSelectMode)
	{
		bSelectMode = false;
		OnSelectModeChanged.Broadcast(false);
		OnEditModeChanged.Broadcast();
		UE_LOG(LogTemp, Warning, TEXT("Select mode OFF"));
	}
}

void ANavPlayerController::ToggleSelectMode()
{
	if (bInteriorInspectionEnabled) return;
	if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter select mode: no building interior currently loaded"));
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try selecting a room/wall."), EActionFailureSeverity::Warning);
		return;
	}

	if (bSelectMode)
	{
		ExitSelectMode();
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
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitPrefabRoomMode();
	ExitAddDoorMode();
	ExitConnectorMode();

	bSelectMode = true;
	OnSelectModeChanged.Broadcast(true);
	OnEditModeChanged.Broadcast();

	UE_LOG(LogTemp, Warning, TEXT("Select mode ON (floor %d): click a room/wall on the 2D canvas to select it (Backspace deletes, =/- resizes), Esc/Q to exit"), InteriorGen->GetActiveFloor());
}

void ANavPlayerController::ToggleRoomColours()
{
	bRoomColoursVisible = !bRoomColoursVisible;
	if (InteriorGen)
	{
		InteriorGen->SetRoomColoursVisible(bRoomColoursVisible);
	}
	OnRoomColoursChanged.Broadcast(bRoomColoursVisible);
}

void ANavPlayerController::ToggleSnapEnabled()
{
	bSnapEnabled = !bSnapEnabled;
	UE_LOG(LogTemp, Warning, TEXT("Snapping %s"), bSnapEnabled ? TEXT("ON") : TEXT("OFF"));
	OnSnapEnabledChanged.Broadcast(bSnapEnabled);
}

void ANavPlayerController::ExitPrefabRoomMode()
{
	if (ActivePrefabShape != 0)
	{
		ActivePrefabShape = 0;
		OnEditModeChanged.Broadcast();
		UE_LOG(LogTemp, Warning, TEXT("Prefab room placement OFF"));
	}
}

void ANavPlayerController::TogglePrefabRoomMode(int32 ShapeId)
{
	if (bInteriorInspectionEnabled) return;
	if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot place a prefab room: no building interior currently loaded"));
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try placing a room."), EActionFailureSeverity::Warning);
		return;
	}

	if (ActivePrefabShape == ShapeId)
	{
		ExitPrefabRoomMode();
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
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitAddDoorMode();
	ExitConnectorMode();

	ActivePrefabShape = ShapeId;
	OnEditModeChanged.Broadcast();
	UE_LOG(LogTemp, Warning, TEXT("Prefab room placement ON (shape %d): click the 2D canvas to place, press the same key again to stop"), ShapeId);
}

void ANavPlayerController::TogglePrefabRectangleMode()
{
	TogglePrefabRoomMode(1);
}

void ANavPlayerController::TogglePrefabTriangleMode()
{
	TogglePrefabRoomMode(2);
}

void ANavPlayerController::TogglePrefabCircleMode()
{
	TogglePrefabRoomMode(3);
}

void ANavPlayerController::ExitConnectorMode()
{
	if (ActiveConnectorType != 0)
	{
		ActiveConnectorType = 0;
		OnEditModeChanged.Broadcast();
		UE_LOG(LogTemp, Warning, TEXT("Connector placement OFF"));
	}
}

void ANavPlayerController::ToggleConnectorMode(EInteriorNavNodeType Type)
{
	if (bInteriorInspectionEnabled) return;
	if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot place a connector: no building interior currently loaded"));
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try placing an elevator/stairs/escalator."), EActionFailureSeverity::Warning);
		return;
	}

	// Matches ActiveConnectorType's convention: 0 = none, otherwise the int32 value of the
	// EInteriorNavNodeType being placed (Elevator=1/Stairs=2/Escalator=3 - PathNode=0 is never
	// placed this way, so it never collides with "none").
	const int32 RequestedType = static_cast<int32>(Type);
	if (ActiveConnectorType == RequestedType)
	{
		ExitConnectorMode();
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
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitAddDoorMode();

	ActiveConnectorType = RequestedType;
	OnEditModeChanged.Broadcast();
	UE_LOG(LogTemp, Warning, TEXT("Connector placement ON (type %d): click the 2D canvas to place, press the same key again to stop"), RequestedType);
}

void ANavPlayerController::ToggleElevatorMode()
{
	ToggleConnectorMode(EInteriorNavNodeType::Elevator);
}

void ANavPlayerController::ToggleStairsMode()
{
	ToggleConnectorMode(EInteriorNavNodeType::Stairs);
}

void ANavPlayerController::ToggleEscalatorMode()
{
	ToggleConnectorMode(EInteriorNavNodeType::Escalator);
}

void ANavPlayerController::ExitAddDoorMode()
{
	if (bAddDoorMode)
	{
		bAddDoorMode = false;
		OnEditModeChanged.Broadcast();
		UE_LOG(LogTemp, Warning, TEXT("Add-door mode OFF"));
	}
}

void ANavPlayerController::ToggleAddDoorMode()
{
	if (bInteriorInspectionEnabled) return;
	if (!InteriorGen || InteriorGen->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot enter add-door mode: no building interior currently loaded"));
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try adding a door."), EActionFailureSeverity::Warning);
		return;
	}

	if (bAddDoorMode)
	{
		ExitAddDoorMode();
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
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitConnectorMode();

	bAddDoorMode = true;
	OnEditModeChanged.Broadcast();
	UE_LOG(LogTemp, Warning, TEXT("Add-door mode ON (floor %d): click a wall edge on the 2D canvas to add a door, Esc/K to exit"), InteriorGen->GetActiveFloor());
}

FString ANavPlayerController::AddDoorToWall(const FString& RoomId, int32 EdgeIndex, float OffsetWorldUnits)
{
	if (bInteriorInspectionEnabled) return FString();
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."), EActionFailureSeverity::Warning);
		return FString();
	}

	const FString NewDoorId = InteriorGen->AddDoorToRoom(RoomId, EdgeIndex, OffsetWorldUnits);
	if (NewDoorId.IsEmpty())
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not add a door to '%s'."), *RoomId), EActionFailureSeverity::Warning);
	}
	else
	{
		bHasUnsavedChanges = true;
		// Selected immediately, like PlacePrefabRoomAt does for rooms, so it can be deleted right
		// away with Backspace if it's wrong.
		SelectDoor(RoomId, NewDoorId);
	}
	return NewDoorId;
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
	else if (bDrawWallMode)
	{
		FinishWall();
	}
}

void ANavPlayerController::OnCancelPressed()
{
	if (bInteriorInspectionEnabled) { SetInteriorInspectionEnabled(false); return; }
	if (NavigationPanel && NavigationPanel->GetSelectionMode() != ENavigationSelectionMode::None)
	{
		NavigationPanel->CancelSelection();
		return;
	}

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
	else if (bDrawWallMode)
	{
		CancelWall();
	}
	else if (bSelectMode)
	{
		ExitSelectMode();
	}
	else if (ActivePrefabShape != 0)
	{
		ExitPrefabRoomMode();
	}
	else if (bAddDoorMode)
	{
		ExitAddDoorMode();
	}
	else if (ActiveConnectorType != 0)
	{
		ExitConnectorMode();
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
		OnActionFailed.Broadcast(TEXT("Click on the ground, not on another building."), EActionFailureSeverity::Warning);
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
		OnActionFailed.Broadcast(FString::Printf(TEXT("Need at least 3 points to finish a building (have %d)."), PendingFootprint.Num()), EActionFailureSeverity::Warning);
		return;
	}

	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("No BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."), EActionFailureSeverity::Warning);
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
		OnActionFailed.Broadcast(TEXT("Could not create the building - check that the id isn't already used and the outline doesn't cross itself."), EActionFailureSeverity::Warning);
		return;
	}

	if (bUsingAutoId)
	{
		NextNewBuildingIndex++;
	}
	MarkBuildingCreated(NewBuilding);
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

void ANavPlayerController::AddRoomPointAtFootprintPosition(FVector2D FootprintPoint)
{
	if (!bDrawRoomMode && !bDrawWallMode)
	{
		return;
	}

	if (InteriorGen && Generator)
	{
		if (APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId()))
		{
			if (!IsPointInPolygon(FootprintPoint, Building->GetFootprint()))
			{
				UE_LOG(LogTemp, Warning, TEXT("Click inside the building's outline to add a point, not outside it"));
				OnActionFailed.Broadcast(TEXT("Click inside the building's outline, not outside it."), EActionFailureSeverity::Warning);
				return;
			}
		}
	}

	// Vertex snapping: if this click landed close to an existing room/wall's corner, snap to it
	// exactly instead of the raw click position - same tolerance as dragging a room (see
	// UInteriorFloorPlanWidget), applied after the building-outline check above so snapping can't
	// rescue a click that's genuinely outside the building.
	if (InteriorGen && bSnapEnabled)
	{
		// Fixed WORLD distance (80cm) divided by Scale, not a bare footprint-unit constant - same
		// "footprint units assumed Scale~10" reasoning as PlacePrefabRoomAt (see its own comment); a
		// bare constant here would snap across nearly a whole room's width at Scale=100.
		const float SnapToleranceFootprintUnits = 80.f / GetBuildingScale();
		FVector2D SnapOffset;
		if (InteriorGen->FindSnapOffset(InteriorGen->GetActiveFloor(), { FootprintPoint }, FString(), SnapToleranceFootprintUnits, SnapOffset))
		{
			FootprintPoint += SnapOffset;
		}
	}

	PendingRoomFootprint.Add(FootprintPoint);
	OnRoomPointCountChanged.Broadcast(PendingRoomFootprint.Num());

	UE_LOG(LogTemp, Warning, TEXT("Room point %d added: (%.1f, %.1f)"), PendingRoomFootprint.Num(), FootprintPoint.X, FootprintPoint.Y);
}

void ANavPlayerController::SelectRoom(const FString& RoomId)
{
	SelectEntrance(FString());
	// Tracked (rather than an early-return once SelectedRoomId itself is unchanged) so
	// RefreshSelectionHighlight still fires when this call's only real effect was clearing a
	// previously-selected door/connector - e.g. deselecting by clicking empty space while a connector,
	// not a room, was selected (SelectedRoomId was already empty on both sides of that call).
	bool bChanged = false;

	if (!SelectedDoorId.IsEmpty())
	{
		SelectedDoorRoomId.Empty();
		SelectedDoorId.Empty();
		OnSelectedDoorChanged.Broadcast(FString(), FString());
		bChanged = true;
	}
	if (!SelectedConnectorGroupId.IsEmpty())
	{
		SelectedConnectorGroupId.Empty();
		OnSelectedConnectorChanged.Broadcast(FString());
		bChanged = true;
	}

	if (SelectedRoomId != RoomId)
	{
		SelectedRoomId = RoomId;
		OnSelectedRoomChanged.Broadcast(SelectedRoomId);
		bChanged = true;
	}

	if (bChanged)
	{
		RefreshSelectionHighlight();
	}
}

void ANavPlayerController::SelectDoor(const FString& RoomId, const FString& DoorId)
{
	SelectEntrance(FString());
	if (!SelectedRoomId.IsEmpty())
	{
		SelectedRoomId.Empty();
		OnSelectedRoomChanged.Broadcast(FString());
	}
	if (!SelectedConnectorGroupId.IsEmpty())
	{
		SelectedConnectorGroupId.Empty();
		OnSelectedConnectorChanged.Broadcast(FString());
	}

	SelectedDoorRoomId = RoomId;
	SelectedDoorId = DoorId;
	OnSelectedDoorChanged.Broadcast(SelectedDoorRoomId, SelectedDoorId);
	RefreshSelectionHighlight();
}

void ANavPlayerController::SelectConnector(const FString& GroupId)
{
	SelectEntrance(FString());
	// See SelectRoom's own comment on why this is tracked rather than an early return.
	bool bChanged = false;

	if (!SelectedRoomId.IsEmpty())
	{
		SelectedRoomId.Empty();
		OnSelectedRoomChanged.Broadcast(FString());
		bChanged = true;
	}
	if (!SelectedDoorId.IsEmpty())
	{
		SelectedDoorRoomId.Empty();
		SelectedDoorId.Empty();
		OnSelectedDoorChanged.Broadcast(FString(), FString());
		bChanged = true;
	}

	if (SelectedConnectorGroupId != GroupId)
	{
		SelectedConnectorGroupId = GroupId;
		OnSelectedConnectorChanged.Broadcast(SelectedConnectorGroupId);
		bChanged = true;
	}

	if (bChanged)
	{
		RefreshSelectionHighlight();
	}
}

bool ANavPlayerController::SetSelectedRoomName(const FString& NewName)
{
	if (bInteriorInspectionEnabled) return false;
	if (SelectedRoomId.IsEmpty() || !InteriorGen)
	{
		return false;
	}

	const bool bSuccess = InteriorGen->SetRoomName(SelectedRoomId, NewName);
	if (bSuccess)
	{
		bHasUnsavedChanges = true;
		OnInteriorContentChanged.Broadcast();
	}
	else
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not rename '%s' to '%s' - that name is already in use."), *SelectedRoomId, *NewName), EActionFailureSeverity::Warning);
	}
	return bSuccess;
}

FString ANavPlayerController::GetSelectedRoomName() const
{
	if (SelectedRoomId.IsEmpty() || !InteriorGen)
	{
		return FString();
	}

	return InteriorGen->GetRoomName(SelectedRoomId);
}

bool ANavPlayerController::GetSelectedRoomBounds(FVector2D& OutPosition, FVector2D& OutSize) const
{
	const AInteriorRoom* Room = (!SelectedRoomId.IsEmpty() && InteriorGen) ? InteriorGen->GetRoomById(SelectedRoomId) : nullptr;
	if (!Room || Room->GetFootprint().IsEmpty())
	{
		return false;
	}
	FVector2D Min(DBL_MAX, DBL_MAX), Max(-DBL_MAX, -DBL_MAX);
	for (const FVector2D& Point : Room->GetFootprint())
	{
		Min = FVector2D(FMath::Min(Min.X, Point.X), FMath::Min(Min.Y, Point.Y));
		Max = FVector2D(FMath::Max(Max.X, Point.X), FMath::Max(Max.Y, Point.Y));
	}
	OutPosition = Min;
	OutSize = Max - Min;
	return true;
}

bool ANavPlayerController::SetSelectedRoomTags(const TArray<FString>& InTags)
{
	if (bInteriorInspectionEnabled) return false;
	if (!InteriorGen || !InteriorGen->IsRoomClosedLoop(SelectedRoomId)) return false;
	const TArray<FString> Before = InteriorGen->GetRoomTags(SelectedRoomId);
	if (!InteriorGen->SetRoomTags(SelectedRoomId, InTags)) return false;
	if (Before != InteriorGen->GetRoomTags(SelectedRoomId))
	{
		bHasUnsavedChanges = true;
		OnInteriorContentChanged.Broadcast();
	}
	return true;
}

TArray<FString> ANavPlayerController::GetRoomTags(const FString& RoomId) const
{
	return InteriorGen ? InteriorGen->GetRoomTags(RoomId) : TArray<FString>();
}

TArray<FString> ANavPlayerController::GetSelectedRoomTags() const
{
	return InteriorGen ? InteriorGen->GetRoomTags(SelectedRoomId) : TArray<FString>();
}

bool ANavPlayerController::SetSelectedRoomType(const FString& NewType)
{
	if (bInteriorInspectionEnabled) return false;
	if (!InteriorGen || !InteriorGen->IsRoomClosedLoop(SelectedRoomId)) return false;
	const FString Before = InteriorGen->GetRoomType(SelectedRoomId);
	if (!InteriorGen->SetRoomType(SelectedRoomId, NewType)) return false;
	if (Before != InteriorGen->GetRoomType(SelectedRoomId))
	{
		bHasUnsavedChanges = true;
		OnInteriorContentChanged.Broadcast();
	}
	return true;
}

bool ANavPlayerController::GenerateFloorForActiveFloor(FString& OutMessage)
{
	if (bInteriorInspectionEnabled) { OutMessage = TEXT("Turn off Isolate & Rotate to edit floors."); return false; }
	OutMessage.Empty();
	if (!InteriorGen)
	{
		OutMessage = TEXT("No interior is open.");
		OnActionFailed.Broadcast(OutMessage, EActionFailureSeverity::Warning);
		return false;
	}

	FFloorGenerationResult Result;
	if (!InteriorGen->GenerateFloor(FMath::Rand(), Result))
	{
		OutMessage = Result.Error;
		OnActionFailed.Broadcast(OutMessage, EActionFailureSeverity::Warning);
		return false;
	}

	OutMessage = FString::Printf(TEXT("Generated %d rooms and %d lifts/stairs"), Result.RoomsCreated, Result.ConnectorsPlaced + Result.ConnectorsReused);
	UE_LOG(LogTemp, Warning, TEXT("GenerateFloor: %s, %d doors, %d rooms did not fit"), *OutMessage, Result.DoorsCreated, Result.RoomsSkipped);
	bHasUnsavedChanges = true;
	OnInteriorContentChanged.Broadcast();
	return true;
}

FString ANavPlayerController::GetSelectedRoomType() const
{
	return InteriorGen ? InteriorGen->GetRoomType(SelectedRoomId) : FString();
}

FString ANavPlayerController::GetRoomType(const FString& RoomId) const
{
	return InteriorGen ? InteriorGen->GetRoomType(RoomId) : FString();
}

bool ANavPlayerController::IsSelectedRoomClosedLoop() const
{
	if (SelectedRoomId.IsEmpty() || !InteriorGen)
	{
		return false;
	}

	return InteriorGen->IsRoomClosedLoop(SelectedRoomId);
}

bool ANavPlayerController::GetSelectedDoorOpening(FInteriorOpening& OutOpening)
{
	if (SelectedDoorId.IsEmpty() || !InteriorGen)
	{
		return false;
	}

	return InteriorGen->GetDoorOpening(SelectedDoorRoomId, SelectedDoorId, OutOpening);
}

bool ANavPlayerController::SetSelectedDoorConnectsTo(const FString& NewConnectsToLabel)
{
	if (bInteriorInspectionEnabled) return false;
	if (SelectedDoorId.IsEmpty() || !InteriorGen)
	{
		return false;
	}

	// NewConnectsToLabel is whatever the dropdown showed (see GetConnectableTargetIds) - a room's
	// current name, "corridor", or the special "Outside" target. Resolve room names back to stable
	// room ids, so a later rename doesn't silently break this door's connection.
	FString ResolvedConnectsTo = NewConnectsToLabel;

	if (NewConnectsToLabel.Equals(TEXT("Outside"), ESearchCase::IgnoreCase))
	{
		if (InteriorGen->GetActiveFloor() != 1)
		{
			OnActionFailed.Broadcast(
				TEXT("An outside exit must be assigned to a Floor 1 door."),
				EActionFailureSeverity::Warning);
			return false;
		}

		// Store a stable value for the route planner and JSON.
		ResolvedConnectsTo = TEXT("outside");
	}
	else if (NewConnectsToLabel != TEXT("corridor"))
	{
		bool bResolved = false;

		for (const AInteriorRoom* Room :
			InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
		{
			if (Room && Room->RoomName == NewConnectsToLabel)
			{
				ResolvedConnectsTo = Room->RoomId;
				bResolved = true;
				break;
			}
		}

		if (!bResolved)
		{
			OnActionFailed.Broadcast(FString::Printf(TEXT("Could not find a room named '%s'."), *NewConnectsToLabel), EActionFailureSeverity::Warning);
			return false;
		}
	}

	const bool bSuccess = InteriorGen->SetDoorConnectsTo(
		SelectedDoorRoomId,
		SelectedDoorId,
		ResolvedConnectsTo);

	if (bSuccess)
	{
		bHasUnsavedChanges = true;
	}
	else
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not update door '%s'."), *SelectedDoorId), EActionFailureSeverity::Warning);
	}

	return bSuccess;
}

TArray<FString> ANavPlayerController::GetConnectableTargetIds() const
{
	TArray<FString> Result;
	if (SelectedDoorId.IsEmpty() || !InteriorGen)
	{
		return Result;
	}

	Result.Add(TEXT("corridor"));

	// A room's door leads to the corridor (the walkable space of the building that is not inside a room) or to another room. The way
	// out of the building is a main door in the outer wall, not a room door - so "Outside" is no longer offered. A door an older project
	// already set to outside keeps showing it, so it is not silently blanked.
	FInteriorOpening Current;
	if (InteriorGen->GetDoorOpening(SelectedDoorRoomId, SelectedDoorId, Current) && Current.ConnectsTo.Equals(TEXT("outside"), ESearchCase::IgnoreCase))
	{
		Result.Add(TEXT("Outside"));
	}

	for (const AInteriorRoom* Room :
		InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
	{
		if (Room && Room->IsClosedLoop() && Room->RoomId != SelectedDoorRoomId)
		{
			// Shown (and picked from) by name, not raw id - the id is what actually gets stored (see
			// SetSelectedDoorConnectsTo), so a later rename doesn't silently break this door's
			// connection. RoomName is never actually empty (see CreateRoomFromFootprint), it falls
			// back to the room's own id if never given a real name.
			Result.Add(Room->RoomName);
		}
	}

	return Result;
}

FString ANavPlayerController::ResolveConnectsToDisplayName(
	const FString& ConnectsToId) const
{
	if (ConnectsToId.Equals(TEXT("outside"), ESearchCase::IgnoreCase))
	{
		return TEXT("Outside");
	}

	// A door with nothing set (older projects) leads to the corridor.
	if (ConnectsToId.IsEmpty())
	{
		return TEXT("corridor");
	}
	if (ConnectsToId == TEXT("corridor") || !InteriorGen)
	{
		return ConnectsToId;
	}

	const FString RoomName = InteriorGen->GetRoomName(ConnectsToId);
	// Falls back to the raw id if the room can't be found (e.g. it was since deleted) - better to
	// show a stale-but-visible id than silently blank out the field.
	return RoomName.IsEmpty() ? ConnectsToId : RoomName;
}

bool ANavPlayerController::DeleteRoom(const FString& RoomId)
{
	if (bInteriorInspectionEnabled) return false;
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."), EActionFailureSeverity::Warning);
		return false;
	}

	// RoomId is frequently called as DeleteRoom(SelectedRoomId) - a const FString& binds directly to
	// that member, not a copy, so SelectRoom(FString()) below (which assigns SelectedRoomId = "" to
	// clear the selection) would silently clobber RoomId itself out from under this function if it
	// were used afterwards. Snapshot it by value up front so everything below stays correct.
	const FString DeletedRoomId = RoomId;

	const bool bSuccess = InteriorGen->DeleteRoom(DeletedRoomId);
	if (!bSuccess)
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not delete '%s' - it may no longer exist."), *DeletedRoomId), EActionFailureSeverity::Warning);
	}
	else
	{
		bHasUnsavedChanges = true;
		if (SelectedRoomId == DeletedRoomId)
		{
			SelectRoom(FString());
		}
		if (SelectedDoorRoomId == DeletedRoomId)
		{
			// The room (and every door on it) is gone - the selected door reference is now stale.
			SelectedDoorRoomId.Empty();
			SelectedDoorId.Empty();
			OnSelectedDoorChanged.Broadcast(FString(), FString());
		}
		if (NavigationPanel)
		{
			NavigationPanel->NotifyRoomDeleted(DeletedRoomId);
		}
		OnInteriorContentChanged.Broadcast();
	}
	return bSuccess;
}

bool ANavPlayerController::DeleteDoor(const FString& RoomId, const FString& DoorId)
{
	if (bInteriorInspectionEnabled) return false;
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."), EActionFailureSeverity::Warning);
		return false;
	}

	const bool bSuccess = InteriorGen->DeleteDoor(RoomId, DoorId);
	if (!bSuccess)
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not delete door '%s'."), *DoorId), EActionFailureSeverity::Warning);
	}
	else
	{
		bHasUnsavedChanges = true;
		if (SelectedDoorRoomId == RoomId && SelectedDoorId == DoorId)
		{
			SelectedDoorRoomId.Empty();
			SelectedDoorId.Empty();
			OnSelectedDoorChanged.Broadcast(FString(), FString());
		}
	}
	return bSuccess;
}

bool ANavPlayerController::MoveDoor(const FString& RoomId, const FString& DoorId, float NewOffsetWorldUnits)
{
	if (bInteriorInspectionEnabled) return false;
	if (!InteriorGen)
	{
		return false;
	}

	const bool bSuccess = InteriorGen->MoveDoor(RoomId, DoorId, NewOffsetWorldUnits);
	if (bSuccess)
	{
		bHasUnsavedChanges = true;
	}
	return bSuccess;
}

bool ANavPlayerController::DeleteSelection()
{
	if (bInteriorInspectionEnabled) return false;
	// See NudgeSelectedRoomSize's own comment - same View-mode edit-proofing.
	if (bIsIn3DPreviewMode && !bIsIn3DEditMode)
	{
		return false;
	}

	if (!SelectedEntranceId.IsEmpty())
	{
		const FString Id = SelectedEntranceId;
		if (!InteriorGen || !InteriorGen->RemoveEntrance(Id, &SelectedEntranceSourceBuildingId))
		{
			return false;
		}
		bHasUnsavedChanges = true;
		SelectEntrance(FString());
		OnInteriorContentChanged.Broadcast();
		return true;
	}

	if (!SelectedDoorId.IsEmpty())
	{
		return DeleteDoor(SelectedDoorRoomId, SelectedDoorId);
	}

	if (!SelectedConnectorGroupId.IsEmpty())
	{
		return DeleteSelectedConnector();
	}

	if (SelectedRoomId.IsEmpty())
	{
		return false;
	}

	return DeleteRoom(SelectedRoomId);
}

bool ANavPlayerController::AddEntranceAtFootprintPosition(const FVector2D& FootprintPoint, float ToleranceFootprintUnits)
{
	if (bInteriorInspectionEnabled) return false;
	if (!InteriorGen)
	{
		return false;
	}
	FString Error;
	const FString Id = InteriorGen->AddEntranceAtPoint(FootprintPoint, ToleranceFootprintUnits, Error);
	if (Id.IsEmpty())
	{
		OnActionFailed.Broadcast(Error, EActionFailureSeverity::Warning);
		return false;
	}
	bHasUnsavedChanges = true;
	OnInteriorContentChanged.Broadcast();
	return true;
}

bool ANavPlayerController::SelectEntranceNear(const FVector2D& FootprintPoint, float ToleranceFootprintUnits)
{
	const auto* Entrance = InteriorGen ? InteriorGen->FindEntranceNear(FootprintPoint, ToleranceFootprintUnits) : nullptr;
	if (!Entrance)
	{
		return false;
	}
	SelectEntrance(Entrance->Id, Entrance->SourceBuildingId);
	return true;
}

FString ANavPlayerController::GetBuildingDisplayName(const FString& BuildingId) const
{
	const APolyBuilding* Building = Generator ? Generator->FindBuildingById(BuildingId) : nullptr;
	return Building && !Building->BuildingName.IsEmpty() ? Building->BuildingName : BuildingId;
}

const FInteriorEntrance* ANavPlayerController::GetSelectedEntrance() const
{
	return InteriorGen && !SelectedEntranceId.IsEmpty()
		? InteriorGen->FindEntranceById(SelectedEntranceId, &SelectedEntranceSourceBuildingId) : nullptr;
}

void ANavPlayerController::SelectEntrance(const FString& EntranceId, const FString& SourceBuildingId)
{
	if (EntranceId == SelectedEntranceId && SourceBuildingId == SelectedEntranceSourceBuildingId)
	{
		return;
	}
	if (!EntranceId.IsEmpty())
	{
		// A door in the outer wall is selected on its own, like a room, a door or a connector.
		if (!SelectedRoomId.IsEmpty())
		{
			SelectedRoomId.Empty();
			OnSelectedRoomChanged.Broadcast(FString());
		}
		if (!SelectedDoorId.IsEmpty())
		{
			SelectedDoorRoomId.Empty();
			SelectedDoorId.Empty();
			OnSelectedDoorChanged.Broadcast(FString(), FString());
		}
		if (!SelectedConnectorGroupId.IsEmpty())
		{
			SelectedConnectorGroupId.Empty();
			OnSelectedConnectorChanged.Broadcast(FString());
		}
	}
	SelectedEntranceId = EntranceId;
	SelectedEntranceSourceBuildingId = SourceBuildingId;
	OnSelectedEntranceChanged.Broadcast(SelectedEntranceId);
}

bool ANavPlayerController::DeleteSelectedConnector()
{
	if (bInteriorInspectionEnabled) return false;
	if (SelectedConnectorGroupId.IsEmpty() || !InteriorGen)
	{
		return false;
	}

	const bool bSuccess = InteriorGen->DeleteConnectorGroup(SelectedConnectorGroupId);
	if (bSuccess)
	{
		bHasUnsavedChanges = true;
		SelectedConnectorGroupId.Empty();
		OnSelectedConnectorChanged.Broadcast(FString());
	}
	else
	{
		OnActionFailed.Broadcast(TEXT("Could not delete that connector."), EActionFailureSeverity::Warning);
	}
	return bSuccess;
}

EInteriorNavNodeType ANavPlayerController::GetSelectedConnectorType() const
{
	if (!SelectedConnectorGroupId.IsEmpty() && InteriorGen)
	{
		for (const FInteriorNavNode& Node : InteriorGen->GetNavNodes())
		{
			if (Node.ConnectorGroupId == SelectedConnectorGroupId)
			{
				return Node.Type;
			}
		}
	}

	return EInteriorNavNodeType::PathNode;
}

TArray<int32> ANavPlayerController::GetSelectedConnectorFloors() const
{
	TArray<int32> Result;
	if (!SelectedConnectorGroupId.IsEmpty() && InteriorGen)
	{
		for (const FInteriorNavNode& Node : InteriorGen->GetNavNodes())
		{
			if (Node.ConnectorGroupId == SelectedConnectorGroupId)
			{
				return Node.ConnectsFloors;
			}
		}
	}

	return Result;
}

bool ANavPlayerController::SetSelectedConnectorFloors(const TArray<int32>& NewFloors)
{
	if (bInteriorInspectionEnabled) return false;
	if (SelectedConnectorGroupId.IsEmpty() || !InteriorGen)
	{
		return false;
	}

	const bool bSuccess = InteriorGen->SetConnectorFloors(SelectedConnectorGroupId, NewFloors);
	if (bSuccess)
	{
		bHasUnsavedChanges = true;
	}
	else
	{
		OnActionFailed.Broadcast(TEXT("Could not update that connector's floors - check each newly-added floor exists, is inside the building, and isn't blocked by a wall."), EActionFailureSeverity::Warning);
	}
	return bSuccess;
}

void ANavPlayerController::DeleteSelectionKeyHandler()
{
	DeleteSelection();
}

bool ANavPlayerController::ResizeRoom(const FString& RoomId, float NewWidth, float NewDepth)
{
	if (bInteriorInspectionEnabled) return false;
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."), EActionFailureSeverity::Warning);
		return false;
	}

	const bool bSuccess = InteriorGen->ResizeRoom(RoomId, NewWidth, NewDepth);
	if (!bSuccess)
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not resize '%s'."), *RoomId), EActionFailureSeverity::Warning);
	}
	else
	{
		bHasUnsavedChanges = true;
	}
	return bSuccess;
}

bool ANavPlayerController::MoveRoomTo(const FString& RoomId, const TArray<FVector2D>& NewFootprint)
{
	if (bInteriorInspectionEnabled) return false;
	if (!InteriorGen)
	{
		return false;
	}

	const bool bSuccess = InteriorGen->MoveRoom(RoomId, NewFootprint);
	if (bSuccess)
	{
		bHasUnsavedChanges = true;
	}
	return bSuccess;
}

bool ANavPlayerController::CommitRoomMove(const FString& RoomId, const TArray<FVector2D>& FallbackFootprint)
{
	if (bInteriorInspectionEnabled) return false;
	if (!InteriorGen)
	{
		return false;
	}

	const bool bSuccess = InteriorGen->CommitRoomMove(RoomId, FallbackFootprint);
	if (!bSuccess)
	{
		OnActionFailed.Broadcast(TEXT("Can't place a room on top of another room/wall - moved it back."), EActionFailureSeverity::Warning);
	}
	bHasUnsavedChanges = true;
	return bSuccess;
}

float ANavPlayerController::GetBuildingScale() const
{
	return Generator ? Generator->GetScale() : 1.f;
}

AInteriorRoom* ANavPlayerController::PlacePrefabRoomAt(FVector2D FootprintCenter)
{
	if (bInteriorInspectionEnabled) return nullptr;
	if (ActivePrefabShape == 0)
	{
		return nullptr;
	}

	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."), EActionFailureSeverity::Warning);
		return nullptr;
	}

	if (Generator)
	{
		if (APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId()))
		{
			if (!IsPointInPolygon(FootprintCenter, Building->GetFootprint()))
			{
				UE_LOG(LogTemp, Warning, TEXT("Click inside the building's outline to place a room, not outside it"));
				OnActionFailed.Broadcast(TEXT("Click inside the building's outline, not outside it."), EActionFailureSeverity::Warning);
				return nullptr;
			}
		}
	}

	// Default sizes are deliberately small - ResizeRoom (below) is how they grow from here. Expressed
	// as a fixed WORLD size (cm) divided by the building's own Scale, not a bare footprint-unit
	// constant - footprint-space is pre-scale, so a bare constant here would produce a room 10x too
	// big for a database-loaded building (Scale=100) versus a hand-authored JSON one (Scale=10). See
	// project memory for the full audit of this "footprint units assumed Scale~10" bug family.
	const float Scale = GetBuildingScale();

	TArray<FVector2D> Footprint;
	switch (ActivePrefabShape)
	{
	case 1: // Rectangle: 6m x 4.5m in world space.
	{
		const float HalfWidth = 300.f / Scale;
		const float HalfDepth = 225.f / Scale;
		Footprint.Add(FootprintCenter + FVector2D(-HalfWidth, -HalfDepth));
		Footprint.Add(FootprintCenter + FVector2D(HalfWidth, -HalfDepth));
		Footprint.Add(FootprintCenter + FVector2D(HalfWidth, HalfDepth));
		Footprint.Add(FootprintCenter + FVector2D(-HalfWidth, HalfDepth));
		break;
	}
	case 2: // Triangle: equilateral, 3m radius in world space, pointing up.
	{
		const float Radius = 300.f / Scale;
		constexpr int32 Sides = 3;
		for (int32 i = 0; i < Sides; i++)
		{
			const float AngleDegrees = -90.f + (360.f * i) / Sides;
			const float AngleRad = FMath::DegreesToRadians(AngleDegrees);
			Footprint.Add(FootprintCenter + FVector2D(Radius * FMath::Cos(AngleRad), Radius * FMath::Sin(AngleRad)));
		}
		break;
	}
	case 3: // Circle: approximated as a 24-sided regular polygon, 3m radius in world space.
	{
		const float Radius = 300.f / Scale;
		constexpr int32 Sides = 24;
		for (int32 i = 0; i < Sides; i++)
		{
			const float AngleRad = FMath::DegreesToRadians((360.f * i) / Sides);
			Footprint.Add(FootprintCenter + FVector2D(Radius * FMath::Cos(AngleRad), Radius * FMath::Sin(AngleRad)));
		}
		break;
	}
	default:
		return nullptr;
	}
	
	FString CreateError;
	AInteriorRoom* NewRoom = InteriorGen->CreateRoomFromFootprint(Footprint, FString(), FString(), true, false, /*bAllowOverlap=*/false, CreateError);
	if (!NewRoom)
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not place the prefab room: %s"), *CreateError);
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not place the room there - %s"), *CreateError), EActionFailureSeverity::Warning);
		return nullptr;
	}

	bHasUnsavedChanges = true;
	// Selecting it immediately means =/- (or a future resize UI) can act on it right away without
	// an extra click.
	SelectRoom(NewRoom->RoomId);
	OnInteriorContentChanged.Broadcast();
	return NewRoom;
}

FString ANavPlayerController::PlaceConnectorAt(FVector2D FootprintPosition)
{
	if (bInteriorInspectionEnabled) return FString();
	if (ActiveConnectorType == 0)
	{
		return FString();
	}

	if (!InteriorGen || !Generator)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator/BuildingGenerator placed in the level."), EActionFailureSeverity::Warning);
		return FString();
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		OnActionFailed.Broadcast(TEXT("Load a building's interior first, then try placing an elevator/stairs/escalator."), EActionFailureSeverity::Warning);
		return FString();
	}

	const EInteriorNavNodeType Type = static_cast<EInteriorNavNodeType>(ActiveConnectorType);
	const int32 FloorCount = Building->GetFloorCount();
	const int32 ActiveFloorLevel = InteriorGen->GetActiveFloor();

	// An elevator defaults to every floor (real elevators go direct to any floor); stairs/escalator
	// default to just the immediately adjacent floor(s) - one side only at the top/bottom of the
	// building. Either default is freely editable afterward (not built yet - see the connector
	// properties panel, still to come).
	TArray<int32> ConnectsToFloors;
	if (Type == EInteriorNavNodeType::Elevator)
	{
		for (int32 Level = 1; Level <= FloorCount; Level++)
		{
			ConnectsToFloors.Add(Level);
		}
	}
	else
	{
		if (ActiveFloorLevel - 1 >= 1)
		{
			ConnectsToFloors.Add(ActiveFloorLevel - 1);
		}
		ConnectsToFloors.Add(ActiveFloorLevel);
		if (ActiveFloorLevel + 1 <= FloorCount)
		{
			ConnectsToFloors.Add(ActiveFloorLevel + 1);
		}
	}

	const FString GroupId = InteriorGen->PlaceConnectorNode(Type, FootprintPosition, ConnectsToFloors);
	if (GroupId.IsEmpty())
	{
		OnActionFailed.Broadcast(TEXT("Could not place a connector there - check it's inside the building and not on top of a wall."), EActionFailureSeverity::Warning);
		return FString();
	}

	bHasUnsavedChanges = true;
	return GroupId;
}

AInteriorRoom* ANavPlayerController::FinishRoom(const FString& RoomId, const FString& RoomName, bool bIsPublic, bool bRequiresKey)
{
	if (bInteriorInspectionEnabled) return nullptr;
	if (!bDrawRoomMode)
	{
		return nullptr;
	}

	if (PendingRoomFootprint.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("Need at least 3 points to finish a room (have %d)"), PendingRoomFootprint.Num());
		OnActionFailed.Broadcast(FString::Printf(TEXT("Need at least 3 points to finish a room (have %d)."), PendingRoomFootprint.Num()), EActionFailureSeverity::Warning);
		return nullptr;
	}

	if (!InteriorGen)
	{
		UE_LOG(LogTemp, Error, TEXT("No InteriorGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator found in the level."), EActionFailureSeverity::Warning);
		return nullptr;
	}

	FString CreateError;
	AInteriorRoom* NewRoom = InteriorGen->CreateRoomFromFootprint(PendingRoomFootprint, RoomId, RoomName, bIsPublic, bRequiresKey, /*bAllowOverlap=*/false, CreateError);

	if (!NewRoom)
	{
		// Spawn was rejected - keep the drawn points and preview so the user can see what's there;
		// Esc cancels.
		UE_LOG(LogTemp, Warning, TEXT("Could not finish this room: %s Press Esc to discard and redraw."), *CreateError);
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not create the room - %s"), *CreateError), EActionFailureSeverity::Warning);
		return nullptr;
	}

	bHasUnsavedChanges = true;
	FlushPersistentDebugLines(GetWorld());
	PendingRoomFootprint.Reset();
	OnRoomPointCountChanged.Broadcast(0);
	OnInteriorContentChanged.Broadcast();
	return NewRoom;
}

void ANavPlayerController::CancelRoom()
{
	FlushPersistentDebugLines(GetWorld());
	PendingRoomFootprint.Reset();
	OnRoomPointCountChanged.Broadcast(0);
}

AInteriorRoom* ANavPlayerController::FinishWall(const FString& WallId)
{
	if (bInteriorInspectionEnabled) return nullptr;
	if (!bDrawWallMode)
	{
		return nullptr;
	}

	if (PendingRoomFootprint.Num() < 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("Need at least 2 points to finish a wall (have %d)"), PendingRoomFootprint.Num());
		OnActionFailed.Broadcast(FString::Printf(TEXT("Need at least 2 points to finish a wall (have %d)."), PendingRoomFootprint.Num()), EActionFailureSeverity::Warning);
		return nullptr;
	}

	if (!InteriorGen)
	{
		UE_LOG(LogTemp, Error, TEXT("No InteriorGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator found in the level."), EActionFailureSeverity::Warning);
		return nullptr;
	}

	FString CreateError;
	AInteriorRoom* NewWall = InteriorGen->CreateWallFromPoints(PendingRoomFootprint, WallId, CreateError);

	if (!NewWall)
	{
		// Spawn was rejected - keep the drawn points and preview so the user can see what's there;
		// Esc cancels.
		UE_LOG(LogTemp, Warning, TEXT("Could not finish this wall: %s Press Esc to discard and redraw."), *CreateError);
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not create the wall - %s"), *CreateError), EActionFailureSeverity::Warning);
		return nullptr;
	}

	bHasUnsavedChanges = true;
	FlushPersistentDebugLines(GetWorld());
	PendingRoomFootprint.Reset();
	OnRoomPointCountChanged.Broadcast(0);
	return NewWall;
}

void ANavPlayerController::CancelWall()
{
	FlushPersistentDebugLines(GetWorld());
	PendingRoomFootprint.Reset();
	OnRoomPointCountChanged.Broadcast(0);
}

APolyBuilding* ANavPlayerController::CreateRectangleBuilding(const FString& Id, const FString& Name, FVector2D Position, float Width, float Depth, float RotationDegrees, int32 FloorCount, float FloorHeight)
{
	if (bInteriorInspectionEnabled) return nullptr;
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRectangleBuilding: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."), EActionFailureSeverity::Warning);
		return nullptr;
	}

	APolyBuilding* Result = Generator->CreateRectangleBuilding(Id, Name, Position, Width, Depth, RotationDegrees, FloorCount, FloorHeight);
	if (!Result)
	{
		OnActionFailed.Broadcast(TEXT("Could not create the building - check that the id isn't already used and the dimensions/floor count are valid."), EActionFailureSeverity::Warning);
	}
	else
	{
		MarkBuildingCreated(Result);
	}
	return Result;
}

APolyBuilding* ANavPlayerController::CreateRegularPolygonBuilding(const FString& Id, const FString& Name, FVector2D Position, int32 Sides, float Radius, float RotationDegrees, int32 FloorCount, float FloorHeight)
{
	if (bInteriorInspectionEnabled) return nullptr;
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateRegularPolygonBuilding: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."), EActionFailureSeverity::Warning);
		return nullptr;
	}

	APolyBuilding* Result = Generator->CreateRegularPolygonBuilding(Id, Name, Position, Sides, Radius, RotationDegrees, FloorCount, FloorHeight);
	if (!Result)
	{
		OnActionFailed.Broadcast(TEXT("Could not create the building - check that the id isn't already used and the sides/radius/floor count are valid."), EActionFailureSeverity::Warning);
	}
	else
	{
		MarkBuildingCreated(Result);
	}
	return Result;
}

FString ANavPlayerController::BrowseForBuildingDataFile()
{
#if !PLATFORM_DESKTOP
	// The native file dialog exists on desktop only; the phone app has no import.
	OnActionFailed.Broadcast(TEXT("Could not open the file picker on this system."), EActionFailureSeverity::Warning);
	return FString();
#else
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		UE_LOG(LogTemp, Error, TEXT("BrowseForBuildingDataFile: desktop platform not available"));
		OnActionFailed.Broadcast(TEXT("Could not open the file picker on this system."), EActionFailureSeverity::Warning);
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

	// Start in the project's Data folder (where the sample .db ships) instead of wherever the OS
	// dialog last was - ProjectDir() is a relative path in a packaged build, which the native dialog
	// can't resolve, so it has to be made absolute first.
	const FString DefaultDataDir = GetProjectsDirectory();

	TArray<FString> OutFiles;
	const bool bPicked = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		TEXT("Import building data"),
		DefaultDataDir,
		TEXT(""),
		// Both formats in one filter (not two separate filter entries) so this stays a single "Open"
		// flow the user doesn't have to pre-choose a format for - see
		// LoadBuildingDataFromPathOrDatabase, which the picked path should be handed to instead of
		// either format-specific loader directly.
		TEXT("Building data (*.json, *.db)|*.json;*.db"),
		EFileDialogFlags::None,
		OutFiles);

	if (!bPicked || OutFiles.Num() == 0)
	{
		// User cancelled the dialog - not a failure, nothing to report.
		return FString();
	}

	return OutFiles[0];
#endif
}

int32 ANavPlayerController::LoadBuildingDataFromPath(const FString& FilePath)
{
	return FMath::Max(0, LoadBuildingData(FilePath, false));
}

int32 ANavPlayerController::LoadBuildingDataFromDatabase(const FString& DatabasePath)
{
	return FMath::Max(0, LoadBuildingData(DatabasePath, true));
}

int32 ANavPlayerController::LoadBuildingDataFromPathOrDatabase(const FString& FilePath)
{
	return FMath::Max(0, LoadBuildingData(FilePath, FPaths::GetExtension(FilePath).Equals(TEXT("db"), ESearchCase::IgnoreCase)));
}

bool ANavPlayerController::TryLoadBuildingDataFromPathOrDatabase(const FString& FilePath)
{
	return LoadBuildingData(FilePath, FPaths::GetExtension(FilePath).Equals(TEXT("db"), ESearchCase::IgnoreCase)) != INDEX_NONE;
}

int32 ANavPlayerController::LoadBuildingData(const FString& FilePath, bool bDatabase)
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadBuildingData: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."), EActionFailureSeverity::Warning);
		return INDEX_NONE;
	}
	if (FilePath.IsEmpty())
	{
		OnActionFailed.Broadcast(TEXT("No file selected."), EActionFailureSeverity::Warning);
		return INDEX_NONE;
	}

	FString Problem;
	const int32 Count = Generator->ReplaceBuildingsFromPath(FilePath, bDatabase, [this]()
	{
		SetInteriorInspectionEnabled(false);
		if (InteriorGen && !InteriorGen->GetLoadedBuildingId().IsEmpty())
		{
			RestoreExteriorCameraView();
		}
		if (NavigationPanel)
		{
			NavigationPanel->ClearAll();
			NavigationPanel->SetVisibility(ESlateVisibility::Collapsed);
		}
		SelectedBuilding = nullptr;
	}, Problem);
	if (Count == INDEX_NONE)
	{
		OnActionFailed.Broadcast(Problem, EActionFailureSeverity::Critical);
		return INDEX_NONE;
	}

	// Ctrl+S writes JSON, so a database-backed project starts without a save path.
	CurrentProjectFilePath = bDatabase ? FString() : FilePath;
	UnsavedBuildings.Reset();
	BuildingReveal.Reset();
	bHasUnsavedChanges = false;
	if (bDatabase)
	{
		Generator->LoadRoadsAndPOIsFromDatabase(FilePath);
		Generator->LoadContextBuildingsFromDatabase(FilePath);
	}
	FocusCameraOnAllBuildings();
	BuildCampusInteriorPreview();
	return Count;
}

void ANavPlayerController::StartNewProject()
{
	SetInteriorInspectionEnabled(false);
	SetIn3DPreviewMode(false);
	ClearCampusInteriorPreview();
	bCampusPreviewAllowed = true;
	BuildingReveal.Reset();
	UnsavedBuildings.Reset();
	if (NavigationPanel)
	{
		NavigationPanel->ClearAll();
		NavigationPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (InteriorGen)
	{
		InteriorGen->ClearLoadedInterior();
	}
	if (Generator)
	{
		Generator->ClearAllBuildings();
		Generator->ClearAllRoadsAndPOIs();
		Generator->ClearAllContextBuildings();
		Generator->SetProjectInfo(FString(), FString());
	}

	CurrentProjectFilePath.Empty();
	bHasUnsavedChanges = false;
	NextNewBuildingIndex = 1;

	// Defensively reset any in-progress mode/selection state too, in case New is ever reachable
	// mid-edit rather than only from the start screen.
	bBuildMode = false;
	PendingFootprint.Reset();
	bDrawRoomMode = false;
	bDrawWallMode = false;
	bSelectMode = false;
	ActivePrefabShape = 0;
	bAddDoorMode = false;
	SelectedRoomId.Empty();
	SelectedDoorRoomId.Empty();
	SelectedDoorId.Empty();
	PendingRoomFootprint.Reset();
	SelectedBuilding = nullptr;

	FocusCameraOnAllBuildings();

	UE_LOG(LogTemp, Warning, TEXT("Started a new, blank project"));
}

void ANavPlayerController::FocusCameraOnBuilding(APolyBuilding* Building)
{
	if (!Building)
	{
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	if (!bHasSavedCameraView)
	{
		SavedCameraLocation = ControlledPawn->GetActorLocation();
		SavedCameraRotation = GetControlRotation();
		bHasSavedCameraView = true;
	}

	const int32 Level = InteriorGen ? InteriorGen->GetActiveFloor() : 1;
	PositionCameraForFloor(Building, Level);
}

void ANavPlayerController::FocusCameraOnBuildingFromExterior(APolyBuilding* Building)
{
	APawn* ControlledPawn = GetPawn();
	if (!Building || !ControlledPawn)
	{
		return;
	}

	FVector2D Center;
	float Span;
	GetBuildingFootprintBounds(Building, Center, Span);
	if (Span <= 0.f && Center.IsZero())
	{
		return;
	}

	// Aim at the middle of the building and back off along the view direction far enough to see all of it.
	// The current heading (yaw) is kept so the view does not spin; only the pitch is set.
	const float BuildingHeight = Building->GetFloorCount() * Building->GetFloorHeight();
	const FVector Target(Center.X, Center.Y, BuildingHeight * 0.5f);
	const FRotator Rotation(-35.f, GetControlRotation().Yaw, 0.f);
	const float Distance = FMath::Max(Span, BuildingHeight) * 1.8f + 500.f;

	ControlledPawn->SetActorLocationAndRotation(Target - Rotation.Vector() * Distance, Rotation);
	SetControlRotation(Rotation);
}

void ANavPlayerController::GetBuildingFootprintBounds(APolyBuilding* Building, FVector2D& OutCenter, float& OutSpan) const
{
	OutCenter = FVector2D::ZeroVector;
	OutSpan = 0.f;
	if (!Building)
	{
		return;
	}

	const float Scale = Generator ? Generator->GetScale() : 1.f;
	const TArray<FVector2D>& Footprint = Building->GetFootprint();
	if (Footprint.Num() == 0)
	{
		return;
	}

	FVector2D MinPoint(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D MaxPoint(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	for (const FVector2D& Point : Footprint)
	{
		const FVector2D WorldPoint = (Point + Building->BuildingOffset) * Scale;
		MinPoint.X = FMath::Min(MinPoint.X, WorldPoint.X);
		MinPoint.Y = FMath::Min(MinPoint.Y, WorldPoint.Y);
		MaxPoint.X = FMath::Max(MaxPoint.X, WorldPoint.X);
		MaxPoint.Y = FMath::Max(MaxPoint.Y, WorldPoint.Y);
	}

	OutCenter = (MinPoint + MaxPoint) * 0.5f;
	OutSpan = FMath::Max(MaxPoint.X - MinPoint.X, MaxPoint.Y - MinPoint.Y);
}

void ANavPlayerController::ResetFloorFraming(APolyBuilding* Building)
{
	// Always framed from the whole building's own outer footprint, not the current floor's own rooms -
	// framing tightly around whatever's drawn on just this floor meant a sparsely-drawn floor (or one
	// with a single small room) zoomed in dramatically tighter than its neighbors, which felt jarring
	// switching floor to floor. GetBuildingFootprintBounds doesn't vary with Level at all, so this
	// (deliberately) gives every floor of a given building the same distance/center.
	FVector2D Center;
	float Span;
	GetBuildingFootprintBounds(Building, Center, Span);
	ThreeDPreviewCameraDistance = FMath::Clamp(Span * 1.8f, ThreeDPreviewMinCameraDistance, ThreeDPreviewMaxCameraDistance);
	ThreeDPreviewYaw = ThreeDPreviewDefaultYaw;
	ThreeDPreviewPitch = ThreeDPreviewDefaultPitch;
}

void ANavPlayerController::PositionCameraForFloor(APolyBuilding* Building, int32 Level)
{
	if (!Building)
	{
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	FVector2D Center;
	float Span;
	GetBuildingFootprintBounds(Building, Center, Span);
	if (Span <= 0.f && Center.IsZero())
	{
		return;
	}

	// Just above the selected floor's own ceiling (where its now-hidden slab would be), not above
	// the whole building - keeps a consistent close-up framing no matter how many floors sit above
	// whichever one is currently being edited.
	const float FloorCeilingZ = Level * Building->GetFloorHeight();
	const float ViewHeight = FloorCeilingZ + FMath::Max(Span * 1.5f, 500.f) + 1000.f;

	const FVector NewLocation(Center.X, Center.Y, ViewHeight);
	// Not perfectly vertical (-90) on purpose: dead-straight-down makes vertical walls render
	// edge-on (effectively invisible, since they'd have zero apparent width) - a slight tilt keeps
	// them visible as a readable floor plan while still reading as "top-down".
	const FRotator NewRotation(-80.f, 0.f, 0.f);

	ControlledPawn->SetActorLocationAndRotation(NewLocation, NewRotation);
	SetControlRotation(NewRotation);

	// Only that floor's own ceiling (or the roof, if it's the top floor) needs to hide - the camera
	// now sits just above it, so nothing further up blocks the view.
	Building->SetVisibleInteriorLevel(Level);
	InteriorBuildingHiddenById = Building->BuildingId;
}

void ANavPlayerController::PositionCameraFor3DPreview(APolyBuilding* Building, int32 Level)
{
	if (!Building)
	{
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	FVector2D Center;
	float Span;
	GetBuildingFootprintBounds(Building, Center, Span);
	if (Span <= 0.f && Center.IsZero())
	{
		return;
	}

	// Look at the floor's own vertical middle, not its ceiling - a proper 3D view of the floor
	// itself, unlike PositionCameraForFloor's near-top-down floor-plan pose.
	const float FloorHeight = Building->GetFloorHeight();
	const float TargetZ = (Level - 1) * FloorHeight + FloorHeight * 0.5f;
	const FVector Target(Center.X, Center.Y, TargetZ);

	// Oblique/dimetric angle - ThreeDPreviewYaw/Pitch/CameraDistance are persistent state, set by
	// ResetFloorFraming (and Distance also by ZoomIn/Out3DPreview) - this function just reads whatever
	// they currently are. Free WASD/mouse-look then move the actual pawn away from this pose until the
	// next reset (floor switch, or the ResetCamera3DPreview button).
	const FRotator NewRotation(ThreeDPreviewPitch, ThreeDPreviewYaw, 0.f);
	const FVector NewLocation = Target - NewRotation.Vector() * ThreeDPreviewCameraDistance;

	ControlledPawn->SetActorLocationAndRotation(NewLocation, NewRotation);
	SetControlRotation(NewRotation);

	// Same reasoning as PositionCameraForFloor - hide the viewed floor's own ceiling (and everything
	// above it) so the oblique view down isn't blocked.
	Building->SetVisibleInteriorLevel(Level);
	InteriorBuildingHiddenById = Building->BuildingId;
}

void ANavPlayerController::SetOtherBuildingsHidden(bool bNewHidden)
{
	if (!Generator || !InteriorGen)
	{
		return;
	}

	const FString LoadedId = InteriorGen->GetLoadedBuildingId();
	for (APolyBuilding* Building : Generator->GetAllBuildings())
	{
		if (Building && Building->BuildingId != LoadedId)
		{
			const bool bVisible = !bNewHidden && Generator->MatchesBuildingFilter(*Building);
			Building->SetActorHiddenInGame(!bVisible);
			Building->SetActorEnableCollision(bVisible);
		}
	}
}

void ANavPlayerController::ZoomIn3DPreview()
{
	if (bInteriorInspectionEnabled)
	{
		if (!IsPointerOverUI())
			FrameInspectionCamera(0.9f);
		return;
	}

	if (IsPointerOverUI())
		return;

	if (APawn* ControlledPawn = GetPawn())
	{
		ControlledPawn->AddActorWorldOffset(
			GetControlRotation().Vector() * ThreeDPreviewZoomStep,
			true
		);

		UpdateBuildingTransparency();
	}
}

void ANavPlayerController::ZoomOut3DPreview()
{
	if (bInteriorInspectionEnabled)
	{
		if (!IsPointerOverUI())
			FrameInspectionCamera(1.1f);
		return;
	}

	if (IsPointerOverUI())
		return;

	if (APawn* ControlledPawn = GetPawn())
	{
		ControlledPawn->AddActorWorldOffset(
			-GetControlRotation().Vector() * ThreeDPreviewZoomStep,
			true
		);

		UpdateBuildingTransparency();
	}
}

void ANavPlayerController::ResetCamera3DPreview()
{
	if (bInteriorInspectionEnabled && IsValid(InspectionPivot))
	{
		InspectionPivot->ResetRotation();
		InspectionCameraDistance = 0.f;
		FrameInspectionCamera();
		RefreshRoute3DVisualization();
		return;
	}
	if (!bIsIn3DPreviewMode || !Generator || !InteriorGen)
	{
		return;
	}

	if (APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId()))
	{
		const int32 Level = InteriorGen->GetActiveFloor();
		ResetFloorFraming(Building);
		PositionCameraFor3DPreview(Building, Level);
	}
}

void ANavPlayerController::IncreaseMoveSpeed3DPreview()
{
	if (bInteriorInspectionEnabled) return;
	if (!bIsIn3DPreviewMode)
	{
		return;
	}
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(ControlledPawn->GetMovementComponent()))
		{
			Movement->MaxSpeed = FMath::Clamp(Movement->MaxSpeed + ThreeDPreviewMoveSpeedStep,
				ThreeDPreviewMinMoveSpeed, ThreeDPreviewMaxMoveSpeed);
		}
	}
}

void ANavPlayerController::DecreaseMoveSpeed3DPreview()
{
	if (bInteriorInspectionEnabled) return;
	if (!bIsIn3DPreviewMode)
	{
		return;
	}
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(ControlledPawn->GetMovementComponent()))
		{
			Movement->MaxSpeed = FMath::Clamp(Movement->MaxSpeed - ThreeDPreviewMoveSpeedStep,
				ThreeDPreviewMinMoveSpeed, ThreeDPreviewMaxMoveSpeed);
		}
	}
}

void ANavPlayerController::ApplyExteriorCameraSpeed(float NewSpeed)
{
	APawn* ControlledPawn = GetPawn();
	UFloatingPawnMovement* Movement = ControlledPawn ? Cast<UFloatingPawnMovement>(ControlledPawn->GetMovementComponent()) : nullptr;
	if (!Movement)
	{
		return;
	}
	Movement->MaxSpeed = FMath::Clamp(NewSpeed, ExteriorCameraMinSpeed, ExteriorCameraMaxSpeed);

	// UFloatingPawnMovement's Acceleration/Deceleration are flat values (engine default 4000/8000,
	// tuned for the stock 1200 MaxSpeed) - NOT scaled to MaxSpeed automatically. Left alone, reaching
	// a much higher MaxSpeed (this screen's campus-span-scaled default, or a manually typed value) at
	// a flat 4000 accel takes far longer than anyone actually holds a WASD key for, so raising MaxSpeed
	// alone made no felt difference. Scale both proportionally so the pawn keeps reaching full speed in
	// the same ~0.3s/0.15s the engine default itself implies (1200/4000 and 1200/8000) no matter what
	// MaxSpeed is currently set to.
	Movement->Acceleration = Movement->MaxSpeed / 0.3f;
	Movement->Deceleration = Movement->MaxSpeed / 0.15f;
}

void ANavPlayerController::IncreaseCameraSpeedExterior()
{
	ApplyExteriorCameraSpeed(GetCameraMoveSpeed() * ExteriorCameraSpeedFactor);
}

void ANavPlayerController::DecreaseCameraSpeedExterior()
{
	ApplyExteriorCameraSpeed(GetCameraMoveSpeed() / ExteriorCameraSpeedFactor);
}

void ANavPlayerController::SetCameraSpeedExterior(float NewSpeed)
{
	ApplyExteriorCameraSpeed(NewSpeed);
}

void ANavPlayerController::SetMoveSpeed3DPreview(float NewSpeed)
{
	if (bInteriorInspectionEnabled) return;
	if (!bIsIn3DPreviewMode)
	{
		return;
	}
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(ControlledPawn->GetMovementComponent()))
		{
			Movement->MaxSpeed = FMath::Clamp(NewSpeed, ThreeDPreviewMinMoveSpeed, ThreeDPreviewMaxMoveSpeed);
		}
	}
}

float ANavPlayerController::GetCameraMoveSpeed() const
{
	if (const APawn* ControlledPawn = GetPawn())
	{
		if (const UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(ControlledPawn->GetMovementComponent()))
		{
			return Movement->MaxSpeed;
		}
	}
	return 0.f;
}

void ANavPlayerController::SetIn3DPreviewMode(bool bEnable)
{
	if (!bEnable) SetInteriorInspectionEnabled(false);
	if (bEnable == bIsIn3DPreviewMode || !Generator || !InteriorGen)
	{
		return;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return;
	}

	bIsIn3DPreviewMode = bEnable;
	UpdateBuildingTransparency();
	if (bEnable)
	{
		UpdateBuildingTransparency();
	}
	else
	{
		Building->SetBuildingOpacity(1.0f);
	}
	if (bEnable)
	{
		ResetFloorFraming(Building);
		PositionCameraFor3DPreview(Building, InteriorGen->GetActiveFloor());
		SetOtherBuildingsHidden(true);
		// Building itself stays visible - PositionCameraFor3DPreview's own SetVisibleInteriorLevel call
		// (see APolyBuilding::ApplySlabVisibility) now hides every OTHER floor's own wall ring and
		// ceiling/roof cap, leaving only the viewed floor's own exterior wall + floor slab showing.
		// WASD/mouse-look stay enabled (the pawn's own default free-fly, unchanged) rather than being
		// locked to a fixed angle - ResetCamera3DPreview (a UI button) snaps back to the default
		// best-fit pose if the user wanders off, instead of constraining movement in the first place.
	}
	else
	{
		// Finish (or revert) any drag still in progress before leaving 3D preview entirely - same
		// reasoning as ExitSelectMode's own call, otherwise SetIgnoreLookInput(true) from a
		// mid-drag exit would never get balanced back to false.
		OnLeftMouseUp3DEdit();
		SetOtherBuildingsHidden(false);
		RepositionCameraToActiveFloor();
		// Always leave 3D preview back in View mode, not however Edit/View happened to be left - no
		// separate OnEdit3DModeChanged broadcast needed, the View/Edit button is already hidden by the
		// OnPreviewModeChanged broadcast below and just re-syncs to View next time 3D is re-entered.
		bIsIn3DEditMode = false;
	}

	// Shows the route/start/destination markers immediately on entry if one already exists (rather
	// than waiting for the next OnStateChanged), and clears them on the way out - the function itself
	// checks bIsIn3DPreviewMode, so this one call handles both.
	RefreshRoute3DVisualization();

	OnPreviewModeChanged.Broadcast(bIsIn3DPreviewMode);
}

void ANavPlayerController::SetIn3DEditMode(bool bEnable)
{
	if (bInteriorInspectionEnabled) return;
	if (!bIsIn3DPreviewMode || bEnable == bIsIn3DEditMode)
	{
		return;
	}

	if (!bEnable)
	{
		// Same reasoning as ExitSelectMode/SetIn3DPreviewMode's own call - switching back to View
		// mid-drag must still finish (or revert) it and balance SetIgnoreLookInput.
		OnLeftMouseUp3DEdit();
	}

	bIsIn3DEditMode = bEnable;
	OnEdit3DModeChanged.Broadcast(bIsIn3DEditMode);
}

void ANavPlayerController::RefreshSelectionHighlight()
{
	if (AInteriorRoom* PrevRoom = Cast<AInteriorRoom>(HighlightedSelectionActor))
	{
		PrevRoom->SetHighlighted(false);
	}
	else if (AInteriorConnectorMarker* PrevMarker = Cast<AInteriorConnectorMarker>(HighlightedSelectionActor))
	{
		PrevMarker->SetHighlighted(false);
	}
	HighlightedSelectionActor = nullptr;

	if (!InteriorGen)
	{
		return;
	}

	if (!SelectedConnectorGroupId.IsEmpty())
	{
		const int32 ActiveFloor = InteriorGen->GetActiveFloor();
		for (const FInteriorNavNode& Node : InteriorGen->GetNavNodes())
		{
			if (Node.ConnectorGroupId == SelectedConnectorGroupId && Node.Level == ActiveFloor && Node.MarkerActor)
			{
				Node.MarkerActor->SetHighlighted(true);
				HighlightedSelectionActor = Node.MarkerActor;
				break;
			}
		}
		return;
	}

	// A selected door highlights its owning room - there's no separate 3D visual for "just this door".
	const FString RoomIdToHighlight = !SelectedDoorId.IsEmpty() ? SelectedDoorRoomId : SelectedRoomId;
	if (RoomIdToHighlight.IsEmpty())
	{
		return;
	}

	for (AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
	{
		if (Room && Room->RoomId == RoomIdToHighlight)
		{
			Room->SetHighlighted(true);
			HighlightedSelectionActor = Room;
			break;
		}
	}
}

bool ANavPlayerController::Get3DFloorFootprintPoint(const FHitResult& Hit, FVector2D& OutPoint) const
{
	if (!Generator || !InteriorGen)
	{
		return false;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return false;
	}

	const float Scale = Generator->GetScale();
	if (Scale <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	if (Hit.bBlockingHit)
	{
		OutPoint = FVector2D(Hit.Location.X, Hit.Location.Y) / Scale - Building->BuildingOffset;
		return true;
	}

	// Fallback: the trace missed every collidable mesh (e.g. a click just outside the building
	// outline, or through a gap) - intersect the camera ray with the active floor's own Z-plane
	// instead of silently doing nothing.
	FVector RayOrigin, RayDirection;
	if (!DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return false;
	}

	if (FMath::IsNearlyZero(RayDirection.Z))
	{
		// Ray is parallel to the floor plane - no intersection.
		return false;
	}

	const float PlaneZ = (InteriorGen->GetActiveFloor() - 1) * Building->GetFloorHeight();
	const FVector WorldPoint = FMath::RayPlaneIntersection(RayOrigin, RayDirection, FPlane(FVector(0.f, 0.f, PlaneZ), FVector::UpVector));
	OutPoint = FVector2D(WorldPoint.X, WorldPoint.Y) / Scale - Building->BuildingOffset;
	return true;
}

bool ANavPlayerController::FindNearestRoomEdge3D(AInteriorRoom* Room, const FVector& WorldHit, int32& OutEdgeIndex, float& OutOffsetWorldUnits, float MaxDistanceWorldUnits, bool bCenterOffsetForDoorWidth) const
{
	if (!Room || !Generator || !InteriorGen)
	{
		return false;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return false;
	}

	const TArray<FVector2D>& Footprint = Room->GetFootprint();
	const int32 N = Footprint.Num();
	if (N < 2)
	{
		return false;
	}

	const float Scale = Generator->GetScale();
	TArray<FVector2D> WorldPoints;
	WorldPoints.Reserve(N);
	for (const FVector2D& Point : Footprint)
	{
		WorldPoints.Add((Point + Building->BuildingOffset) * Scale);
	}

	constexpr float DoorWidthWorldUnits = 90.f; // matches AInteriorGenerator::AddDoorToRoom's fixed door width
	const FVector2D HitXY(WorldHit.X, WorldHit.Y);
	const int32 NumEdges = Room->IsClosedLoop() ? N : N - 1;

	float BestDistance = TNumericLimits<float>::Max();
	int32 BestEdge = INDEX_NONE;
	float BestOffset = 0.f;

	for (int32 i = 0; i < NumEdges; i++)
	{
		const FVector2D& A = WorldPoints[i];
		const FVector2D& B = WorldPoints[(i + 1) % N];

		const FVector2D AB = B - A;
		const float LengthSq = AB.SizeSquared();
		const float T = (LengthSq > KINDA_SMALL_NUMBER) ? FMath::Clamp(FVector2D::DotProduct(HitXY - A, AB) / LengthSq, 0.f, 1.f) : 0.f;
		const FVector2D ClosestPoint = A + AB * T;
		const float Distance = FVector2D::Distance(HitXY, ClosestPoint);

		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestEdge = i;
			const float EdgeLengthWorldUnits = FVector2D::Distance(A, B);
			const float RawOffsetWorldUnits = T * EdgeLengthWorldUnits;
			BestOffset = bCenterOffsetForDoorWidth
				? FMath::Clamp(RawOffsetWorldUnits - DoorWidthWorldUnits * 0.5f, 0.f, FMath::Max(0.f, EdgeLengthWorldUnits - DoorWidthWorldUnits))
				: RawOffsetWorldUnits;
		}
	}

	if (BestEdge == INDEX_NONE || (MaxDistanceWorldUnits >= 0.f && BestDistance > MaxDistanceWorldUnits))
	{
		return false;
	}

	OutEdgeIndex = BestEdge;
	OutOffsetWorldUnits = BestOffset;
	return true;
}

const FInteriorOpening* ANavPlayerController::FindDoorAtEdgeOffset(AInteriorRoom* Room, int32 EdgeIndex, float OffsetWorldUnits) const
{
	if (!Room)
	{
		return nullptr;
	}

	for (const FInteriorOpening& Opening : Room->GetOpenings())
	{
		if (Opening.Type == EInteriorOpeningType::Door && Opening.EdgeIndex == EdgeIndex &&
			OffsetWorldUnits >= Opening.Offset && OffsetWorldUnits <= Opening.Offset + Opening.Width)
		{
			return &Opening;
		}
	}

	return nullptr;
}

void ANavPlayerController::Select3DEditTarget(const FHitResult& Hit)
{
	if (!Hit.bBlockingHit)
	{
		SelectRoom(FString());
		return;
	}

	if (AInteriorConnectorMarker* Marker = Cast<AInteriorConnectorMarker>(Hit.GetActor()))
	{
		SelectConnector(Marker->GetOwnerGroupId());
		return;
	}

	if (AInteriorRoom* Room = Cast<AInteriorRoom>(Hit.GetActor()))
	{
		int32 EdgeIndex;
		float OffsetWorldUnits;
		constexpr float DoorPickToleranceWorldUnits = 15.f;
		if (FindNearestRoomEdge3D(Room, Hit.Location, EdgeIndex, OffsetWorldUnits, DoorPickToleranceWorldUnits))
		{
			if (const FInteriorOpening* Door = FindDoorAtEdgeOffset(Room, EdgeIndex, OffsetWorldUnits))
			{
				SelectDoor(Room->RoomId, Door->Id);
				return;
			}
		}
		SelectRoom(Room->RoomId);
		return;
	}

	// Hit the building shell or some other uninteresting actor.
	SelectRoom(FString());
}

void ANavPlayerController::Handle3DEditClick()
{
	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	if (bDrawRoomMode || bDrawWallMode)
	{
		FVector2D FootprintPoint;
		if (Get3DFloorFootprintPoint(Hit, FootprintPoint))
		{
			const int32 PrevPointCount = PendingRoomFootprint.Num();
			AddRoomPointAtFootprintPosition(FootprintPoint);
			if (PendingRoomFootprint.Num() > PrevPointCount)
			{
				DrawPendingRoomPointMarker3D();
			}
		}
		return;
	}

	if (ActivePrefabShape != 0)
	{
		FVector2D FootprintPoint;
		if (Get3DFloorFootprintPoint(Hit, FootprintPoint))
		{
			PlacePrefabRoomAt(FootprintPoint);
		}
		return;
	}

	if (ActiveConnectorType != 0)
	{
		FVector2D FootprintPoint;
		if (Get3DFloorFootprintPoint(Hit, FootprintPoint))
		{
			PlaceConnectorAt(FootprintPoint);
		}
		return;
	}

	if (bAddDoorMode)
	{
		if (AInteriorRoom* Room = Cast<AInteriorRoom>(Hit.GetActor()))
		{
			int32 EdgeIndex;
			float OffsetWorldUnits;
			if (FindNearestRoomEdge3D(Room, Hit.Location, EdgeIndex, OffsetWorldUnits, /*MaxDistanceWorldUnits=*/-1.f, /*bCenterOffsetForDoorWidth=*/true))
			{
				AddDoorToWall(Room->RoomId, EdgeIndex, OffsetWorldUnits);
			}
		}
		return;
	}

	if (bSelectMode)
	{
		Select3DEditTarget(Hit);
	}
}

void ANavPlayerController::DrawPendingRoomPointMarker3D()
{
	if (!Generator || !InteriorGen || PendingRoomFootprint.Num() == 0)
	{
		return;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return;
	}

	const float Scale = Generator->GetScale();
	// A little above the floor slab (see AInteriorRoom::RebuildMesh's own FloorSlabZOffset) so the
	// marker doesn't z-fight with it and stays visible from the 3D preview's oblique angle.
	const float Z = (InteriorGen->GetActiveFloor() - 1) * Building->GetFloorHeight() + 10.f;

	auto ToWorld = [&](const FVector2D& FootprintPoint) -> FVector
	{
		return FVector((FootprintPoint.X + Building->BuildingOffset.X) * Scale, (FootprintPoint.Y + Building->BuildingOffset.Y) * Scale, Z);
	};

	const FVector NewWorld = ToWorld(PendingRoomFootprint.Last());
	if (PendingRoomFootprint.Num() > 1)
	{
		const FVector PrevWorld = ToWorld(PendingRoomFootprint[PendingRoomFootprint.Num() - 2]);
		DrawDebugLine(GetWorld(), PrevWorld, NewWorld, FColor::Green, true, -1.f, 0, 5.f);
	}
	DrawDebugSphere(GetWorld(), NewWorld, 20.f, 8, FColor::Green, true);
}

void ANavPlayerController::RefreshRoute3DVisualization()
{
	RouteVisualization->ClearRoute();
	const FNavigationRouteFocus Focus = NavigationPanel ? NavigationPanel->GetGuidedFocus() : FNavigationRouteFocus();

	// The exterior screen draws the whole route on the campus.
	if (NavigationPanel && NavigationPanel->IsOutdoorMode())
	{
		RouteVisualization->ShowView(UNavigationRouteVisualization::BuildCampusView(
			NavigationPanel->GetRoutePoints(),
			NavigationPanel->HasStart() ? &NavigationPanel->GetStart() : nullptr,
			NavigationPanel->HasDestination() ? &NavigationPanel->GetDestination() : nullptr,
			NavigationPanel->IsRouteGuidanceVisible(), &NavigationPanel->GetStops(), &NavigationPanel->GetRouteLegs(), &Focus));
		return;
	}

	if (!bIsIn3DPreviewMode || !Generator || !InteriorGen || !NavigationPanel)
	{
		return;
	}

	APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	if (!Building)
	{
		return;
	}

	FNavigation3DView Display = UNavigationRouteVisualization::BuildView(
		NavigationPanel->GetRoutePoints(),
		NavigationPanel->HasStart() ? &NavigationPanel->GetStart() : nullptr,
		NavigationPanel->HasDestination() ? &NavigationPanel->GetDestination() : nullptr,
		Building->BuildingOffset, Generator->GetScale(), Building->GetFloorHeight(), InteriorGen->GetActiveFloor(),
		NavigationPanel->IsRouteGuidanceVisible(), Building->BuildingId, &NavigationPanel->GetStops(), &NavigationPanel->GetRouteLegs(), &Focus);
	if (bInteriorInspectionEnabled && IsValid(InspectionPivot)) Display = UNavigationRouteVisualization::TransformView(Display, InspectionPivot->GetDisplayTransform());
	RouteVisualization->ShowView(Display);
}

void ANavPlayerController::FollowNavigationStep()
{
	if (bInteriorInspectionEnabled) return;
	const FNavigationRoutePoint* Point = NavigationPanel ? NavigationPanel->GetGuidedPoint() : nullptr;
	APolyBuilding* Building = Point && Generator ? Generator->FindBuildingById(Point->BuildingId) : nullptr;
	if (!Point || !Building)
	{
		return;
	}
	// A step outside belongs to the exterior screen and one inside a building to the interior screen: if the one that is up is the
	// wrong one it hands over (and the other screen follows the step when it comes up).
	// (The other screen follows the step as it takes over, and is not asked again.)
	if (!bHandingOverStep && NavigationStepScreenOwner.IsValid() && NavigationStepScreenCallback)
	{
		TGuardValue<bool> HandingOver(bHandingOverStep, true);
		if (NavigationStepScreenCallback(*Point))
		{
			return;
		}
	}
	// The exterior screen: no building is open, the camera goes to the spot.
	if (NavigationPanel->IsOutdoorMode())
	{
		const FNavigationRouteFocus Focus = NavigationPanel->GetGuidedFocus();
		const TArray<FNavigationRoutePoint>& Route = NavigationPanel->GetRoutePoints();
		FocusCameraOnCampusPoint(*Point, Focus.LastPointIndex != Focus.FirstPointIndex && Route.IsValidIndex(Focus.LastPointIndex) ? &Route[Focus.LastPointIndex] : nullptr);
		return;
	}
	if (!InteriorGen)
	{
		return;
	}
	// Inside: the building of the step, on its floor (an outdoor spot is shown on Floor 1, whose map has the outside area). Another
	// building is opened the way the building switcher opens it, which keeps edits not yet saved. Opening one leaves 3D, so 3D is
	// put back if it was on.
	const bool bWas3D = bIsIn3DPreviewMode;
	if (InteriorGen->GetLoadedBuildingId() != Point->BuildingId && LoadInteriorForBuilding(Point->BuildingId) < 0)
	{
		return;
	}
	const int32 Floor = FMath::Max(1, Point->Floor);
	if (InteriorGen->GetActiveFloor() != Floor)
	{
		SetActiveFloor(Floor);
	}
	if (bWas3D && !bIsIn3DPreviewMode)
	{
		SetIn3DPreviewMode(true);
	}
	RefreshRoute3DVisualization();
}

void ANavPlayerController::FocusCameraOnCampusPoint(const FNavigationRoutePoint& Point, const FNavigationRoutePoint* Next)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !Generator)
	{
		return;
	}
	auto WorldOf = [this](const FNavigationRoutePoint& Spot) -> FVector
	{
		if (Spot.bHasWorldPosition)
		{
			return Spot.WorldPosition;
		}
		const APolyBuilding* Owner = Generator->FindBuildingById(Spot.BuildingId);
		return Owner ? FVector((Spot.Position.X + Owner->BuildingOffset.X) * Generator->GetScale(), (Spot.Position.Y + Owner->BuildingOffset.Y) * Generator->GetScale(),
			FMath::Max(0, Spot.Floor - 1) * Owner->GetFloorHeight()) : FVector::ZeroVector;
	};
	// A view of a person's size, in world units (cm): near enough to see the spot and what is around it, far enough to take in the
	// stretch of the route that goes on from it, and never so far that the spot is lost.
	constexpr float MinDistance = 8000.f;
	constexpr float MaxDistance = 15000.f;
	constexpr float Pitch = -45.f;
	const FVector Here = WorldOf(Point);
	const FVector There = Next ? WorldOf(*Next) : Here;
	float Distance = FMath::Max(MinDistance, FVector::Dist2D(Here, There) * 1.3f + 1500.f);
	// Aimed between the spot and where the step ends when both fit; when the stretch is too long for that, at the spot.
	FVector Target = (Here + There) * 0.5f;
	if (Distance > MaxDistance)
	{
		Distance = MaxDistance;
		Target = Here;
	}
	const FRotator Rotation(Pitch, GetControlRotation().Yaw, 0.f);
	ControlledPawn->SetActorLocationAndRotation(Target - Rotation.Vector() * Distance, Rotation);
	SetControlRotation(Rotation);
}

void ANavPlayerController::SetPointerOverUICallback(const UObject* ScreenOwner, TFunction<bool(const FVector2D&)> Callback)
{
	PointerOverUIOwner = ScreenOwner;
	PointerOverUICallback = MoveTemp(Callback);
}

void ANavPlayerController::SetNavigationStepScreenCallback(const UObject* ScreenOwner, TFunction<bool(const FNavigationRoutePoint&)> Callback)
{
	NavigationStepScreenOwner = ScreenOwner;
	NavigationStepScreenCallback = MoveTemp(Callback);
}

void ANavPlayerController::ClearNavigationStepScreenCallback(const UObject* ScreenOwner)
{
	if (NavigationStepScreenOwner.Get() == ScreenOwner)
	{
		NavigationStepScreenOwner = nullptr;
		NavigationStepScreenCallback = nullptr;
	}
}

void ANavPlayerController::SyncLoadedInterior()
{
	if (InteriorGen)
	{
		InteriorGen->SyncInteriorToBuildingGenerator();
	}
}

void ANavPlayerController::ClearPointerOverUICallback(const UObject* ScreenOwner)
{
	if (PointerOverUIOwner.Get() == ScreenOwner)
	{
		PointerOverUIOwner = nullptr;
		PointerOverUICallback = nullptr;
	}
}

bool ANavPlayerController::IsPointerOverUI() const
{
	return PointerOverUIOwner.IsValid() && PointerOverUICallback && PointerOverUICallback(FSlateApplication::Get().GetCursorPos());
}

void ANavPlayerController::OnLeftMouseDown3DEdit()
{
	if (bInteriorInspectionEnabled) { BeginInspectionDrag(EInspectionDrag::Pan); return; }
	if (!bIsIn3DPreviewMode || !bIsIn3DEditMode || !bSelectMode || !InteriorGen || IsPointerOverUI())
	{
		return;
	}

	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	AInteriorRoom* Room = Hit.bBlockingHit ? Cast<AInteriorRoom>(Hit.GetActor()) : nullptr;
	if (!Room)
	{
		return;
	}

	FVector2D StartFootprintPosition;
	if (!Get3DFloorFootprintPoint(Hit, StartFootprintPosition))
	{
		return;
	}

	// The free-fly pawn's own mouse-look (see SetIn3DPreviewMode's own comment on why it's left
	// enabled) turns the camera on any mouse movement, not just while a dedicated look button is held
	// - without suppressing it here, dragging a room (which moves the mouse while holding the same
	// LeftMouseButton) would also spin the camera underneath the drag. Balanced by the matching
	// SetIgnoreLookInput(false) in OnLeftMouseUp3DEdit below.
	SetIgnoreLookInput(true);

	bIsDragging3DRoom = true;
	Dragging3DRoom = Room;
	Dragging3DRoomId = Room->RoomId;
	DragStart3DRoomFootprint = Room->GetFootprint();
	DragStart3DFootprintPosition = StartFootprintPosition;
}

void ANavPlayerController::OnLeftMouseUp3DEdit()
{
	if (InspectionDrag == EInspectionDrag::Pan) InspectionDrag = EInspectionDrag::None;
	if (!bIsDragging3DRoom)
	{
		return;
	}

	bIsDragging3DRoom = false;
	SetIgnoreLookInput(false);
	if (Dragging3DRoom.IsValid()) CommitRoomMove(Dragging3DRoomId, DragStart3DRoomFootprint);
	Dragging3DRoom.Reset();
	Dragging3DRoomId.Empty();
	DragStart3DRoomFootprint.Reset();
}

void ANavPlayerController::RepositionCameraToActiveFloor()
{
	if (bInteriorInspectionEnabled && IsValid(InspectionPivot) && Generator && InteriorGen)
	{
		if (APolyBuilding* Building = Generator->FindBuildingById(InspectionBuildingId)) Building->SetVisibleInteriorLevel(InteriorGen->GetActiveFloor());
		return;
	}
	if (!InteriorGen || !Generator)
	{
		return;
	}

	if (APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId()))
	{
		if (bIsIn3DPreviewMode)
		{
			// Floor switch: reset back to the building's own default distance/angle (see
			// ResetFloorFraming) rather than keeping whatever free-fly/zoom the user left the camera at
			// - PositionCameraFor3DPreview's own Level then aims it at that floor's own vertical middle
			// and hides its ceiling, same framing horizontally, just looking at a different floor.
			ResetFloorFraming(Building);
			PositionCameraFor3DPreview(Building, InteriorGen->GetActiveFloor());
		}
		else
		{
			PositionCameraForFloor(Building, InteriorGen->GetActiveFloor());
		}
	}
}

void ANavPlayerController::SetRoadCategoryVisible(ERoadCategory Category, bool bVisible)
{
	if (bInteriorInspectionEnabled) return;
	if (Generator)
	{
		Generator->SetRoadCategoryVisible(Category, bVisible);
	}
}

void ANavPlayerController::SetAllRoadsVisible(bool bVisible)
{
	if (bInteriorInspectionEnabled) return;
	if (Generator)
	{
		Generator->SetAllRoadsVisible(bVisible);
	}
}

void ANavPlayerController::SetRoadsAndPOIsWithinCampusOnly(bool bEnable)
{
	if (bInteriorInspectionEnabled) return;
	if (!Generator)
	{
		return;
	}
	if (!bEnable)
	{
		Generator->SetRoadsAndPOIsWithinCampusOnly(false, FVector2D::ZeroVector, 0.f);
		return;
	}

	FVector2D Center;
	float Span;
	if (!ComputeAllBuildingsBounds(Center, Span))
	{
		return;
	}

	// First-pass radius: half the campus's own bounding span, plus a fixed buffer so roads/POIs right
	// at the campus edge (entrances, perimeter paths) aren't clipped off - tune
	// CampusFilterPaddingWorldUnits if this feels too tight/loose once tried in the editor.
	constexpr float CampusFilterPaddingWorldUnits = 5000.f; // 50m at Scale=100
	const float Radius = Span * 0.5f + CampusFilterPaddingWorldUnits;
	Generator->SetRoadsAndPOIsWithinCampusOnly(true, Center, Radius);
}

void ANavPlayerController::SetCampusBuildingsVisible(bool bVisible)
{
	if (bInteriorInspectionEnabled) return;
	if (Generator)
	{
		Generator->SetCampusBuildingsVisible(bVisible);
		SetBuildingFilter(Generator->GetBuildingFilterText(), Generator->GetBuildingFilterMinimumFloors(), Generator->GetBuildingFilterMaximumFloors());
	}
}

void ANavPlayerController::SetBuildingFilter(const FString& Query, int32 MinimumFloors, int32 MaximumFloors)
{
	if (bInteriorInspectionEnabled) return;
	if (!Generator) return;
	Generator->SetBuildingFilter(Query, MinimumFloors, MaximumFloors);
	if (SelectedBuilding && SelectedBuilding->IsHidden())
	{
		SelectedBuilding->SetHighlighted(false);
		SelectedBuilding = nullptr;
		OnBuildingSelected.Broadcast(FString());
	}
}

FString ANavPlayerController::GetBuildingFilterText() const { return Generator ? Generator->GetBuildingFilterText() : FString(); }
int32 ANavPlayerController::GetBuildingFilterMinimumFloors() const { return Generator ? Generator->GetBuildingFilterMinimumFloors() : 0; }
int32 ANavPlayerController::GetBuildingFilterMaximumFloors() const { return Generator ? Generator->GetBuildingFilterMaximumFloors() : 0; }
bool ANavPlayerController::AreCampusBuildingsVisible() const { return !Generator || Generator->AreCampusBuildingsVisible(); }
int32 ANavPlayerController::GetEditableBuildingCount() const { return Generator ? Generator->GetBuildingCount() : 0; }
uint64 ANavPlayerController::GetBuildingResultsRevision() const { return Generator ? Generator->GetBuildingResultsRevision() : 0; }

bool ANavPlayerController::GetSelectedBuildingSummary(FBuildingSummary& OutSummary) const
{
	const FString Id = GetSelectedBuildingId();
	return Generator && !Id.IsEmpty() && Generator->GetBuildingSummary(Id, OutSummary);
}

TArray<ANavPlayerController::FBuildingListItem> ANavPlayerController::GetFilteredBuildingList() const
{
	TArray<FBuildingListItem> Items;
	if (Generator)
	{
		for (const APolyBuilding* Building : Generator->GetFilteredBuildings())
		{
			if (IsValid(Building))
			{
				Items.Add({ Building->BuildingId, Building->BuildingName, Building->GetFloorCount() });
			}
		}
	}
	Items.Sort([](const FBuildingListItem& A, const FBuildingListItem& B)
	{
		const int32 ByName = A.Name.Compare(B.Name, ESearchCase::IgnoreCase);
		return ByName != 0 ? ByName < 0 : A.Id < B.Id;
	});
	return Items;
}

const TMap<FString, FString>& ANavPlayerController::GetFilteredBuildingOptions() const
{
	static const TMap<FString, FString> Empty;
	return Generator ? Generator->GetFilteredBuildingOptions() : Empty;
}

bool ANavPlayerController::SelectBuildingById(const FString& BuildingId)
{
	APolyBuilding* Building = Generator ? Generator->FindBuildingById(BuildingId) : nullptr;
	if (!Building || Building->IsHidden() || bIsIn3DPreviewMode) return false;
	if (SelectedBuilding) SelectedBuilding->SetHighlighted(false);
	SelectedBuilding = Building;
	Building->SetHighlighted(true);
	OnBuildingSelected.Broadcast(BuildingId);
	FocusCameraOnBuildingFromExterior(Building);
	return true;
}

void ANavPlayerController::SetNonCampusBuildingsVisible(bool bVisible)
{
	if (bInteriorInspectionEnabled) return;
	if (Generator)
	{
		Generator->SetNonCampusBuildingsVisible(bVisible);
	}
}

bool ANavPlayerController::SetSelectedBuildingType(const FString& NewType)
{
	const FString Id = GetSelectedBuildingId();
	if (bInteriorInspectionEnabled || !Generator || Id.IsEmpty())
	{
		return false;
	}
	const FString Before = Generator->GetBuildingType(Id);
	if (!Generator->SetBuildingType(Id, NewType))
	{
		return false;
	}
	if (Before != Generator->GetBuildingType(Id))
	{
		bHasUnsavedChanges = true;
	}
	return true;
}

FString ANavPlayerController::GetSelectedBuildingType() const
{
	return Generator ? Generator->GetBuildingType(GetSelectedBuildingId()) : BuildingTypes::Default();
}

void ANavPlayerController::SetBuildingTypeVisible(const FString& Type, bool bVisible)
{
	if (bInteriorInspectionEnabled) return;
	if (Generator)
	{
		Generator->SetBuildingTypeVisible(Type, bVisible);
	}
}

bool ANavPlayerController::IsBuildingTypeVisible(const FString& Type) const
{
	return !Generator || Generator->IsBuildingTypeVisible(Type);
}

void ANavPlayerController::ResetBuildingTypeFilter()
{
	if (bInteriorInspectionEnabled) return;
	if (Generator)
	{
		Generator->ResetBuildingTypeFilter();
	}
}

void ANavPlayerController::LogCameraTransform()
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}
	const FVector Location = ControlledPawn->GetActorLocation();
	const FRotator Rotation = GetControlRotation();
	const FString Message = FString::Printf(TEXT("Camera Location=(X=%.1f, Y=%.1f, Z=%.1f) Rotation=(Pitch=%.1f, Yaw=%.1f, Roll=%.1f)"),
		Location.X, Location.Y, Location.Z, Rotation.Pitch, Rotation.Yaw, Rotation.Roll);
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Yellow, Message);
	}
}

bool ANavPlayerController::ComputeAllBuildingsBounds(FVector2D& OutCenter, float& OutSpan) const
{
	if (!Generator)
	{
		return false;
	}
	const TArray<APolyBuilding*> Buildings = Generator->GetAllBuildings();
	if (Buildings.Num() == 0)
	{
		return false;
	}

	const float Scale = Generator->GetScale();
	FVector2D MinPoint(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D MaxPoint(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	for (const APolyBuilding* Building : Buildings)
	{
		if (!Building)
		{
			continue;
		}
		for (const FVector2D& Point : Building->GetFootprint())
		{
			const FVector2D WorldPoint = (Point + Building->BuildingOffset) * Scale;
			MinPoint.X = FMath::Min(MinPoint.X, WorldPoint.X);
			MinPoint.Y = FMath::Min(MinPoint.Y, WorldPoint.Y);
			MaxPoint.X = FMath::Max(MaxPoint.X, WorldPoint.X);
			MaxPoint.Y = FMath::Max(MaxPoint.Y, WorldPoint.Y);
		}
	}

	OutCenter = (MinPoint + MaxPoint) * 0.5f;
	OutSpan = FMath::Max(MaxPoint.X - MinPoint.X, MaxPoint.Y - MinPoint.Y);
	return true;
}

void ANavPlayerController::FocusCameraOnAllBuildings()
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !Generator)
	{
		return;
	}

	FVector2D Center;
	float Span;
	if (!ComputeAllBuildingsBounds(Center, Span))
	{
		// Nothing to frame yet (e.g. a brand new blank project) - just pull the camera up above the
		// origin instead of leaving it wherever PlayerStart put it.
		const FVector DefaultLocation(0.f, 0.f, 3000.f);
		const FRotator DefaultRotation(-50.f, 0.f, 0.f);
		ControlledPawn->SetActorLocationAndRotation(DefaultLocation, DefaultRotation);
		SetControlRotation(DefaultRotation);
		return;
	}

	// The free-fly pawn's WASD speed defaults to whatever ADefaultPawn ships with (1200 cm/s) -
	// tuned for buildings.json's own small hand-placed campus (spans roughly 1000 units), but a
	// database-loaded campus at real-world scale (see ABuildingGenerator::LoadBuildingsFromDatabase)
	// can span hundreds of real meters, at which point that speed reads as "barely moving". Scale it
	// to cross the whole loaded span in about 4 seconds instead - ApplyExteriorCameraSpeed clamps to
	// the same range IncreaseCameraSpeedExterior/DecreaseCameraSpeedExterior use (so a manual
	// adjustment and a fresh load never disagree on the floor/ceiling) and also scales
	// Acceleration/Deceleration to match, see its own comment.
	ApplyExteriorCameraSpeed(Span / 4.f);

	// Pull the camera back-and-up along the pitch's own forward vector (same "Target -
	// Rotation.Vector() * Distance" pattern PositionCameraForFloor/PositionCameraFor3DPreview use) so
	// the pitch actually looks DOWN AT Center - placing it directly above Center with the same
	// rotation points the view down-and-forward from directly overhead, past the buildings entirely,
	// which reads as "not tilted down" even though the pitch value is right. Pitch/Yaw/distance ratio
	// are user-tunable now (see SetCurrentViewAsDefaultExterior) rather than fixed constants.
	const FVector Target(Center.X, Center.Y, 0.f);
	const FRotator NewRotation(ExteriorDefaultViewPitch, ExteriorDefaultViewYaw, 0.f);
	const float ViewDistance = FMath::Max(Span * ExteriorDefaultViewDistanceRatio, ExteriorDefaultViewMinDistance);
	const FVector NewLocation = Target - NewRotation.Vector() * ViewDistance;

	ControlledPawn->SetActorLocationAndRotation(NewLocation, NewRotation);
	SetControlRotation(NewRotation);
}

void ANavPlayerController::ResetExteriorCameraToDefaultView()
{
	FocusCameraOnAllBuildings();
}

void ANavPlayerController::SetCurrentViewAsDefaultExterior()
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	FVector2D Center;
	float Span;
	if (!ComputeAllBuildingsBounds(Center, Span) || Span <= KINDA_SMALL_NUMBER)
	{
		OnActionFailed.Broadcast(TEXT("Load some buildings first, then set the current view as the default."), EActionFailureSeverity::Warning);
		return;
	}

	const FRotator Rotation = GetControlRotation();
	const FVector Target(Center.X, Center.Y, 0.f);
	const float Distance = FVector::Dist(ControlledPawn->GetActorLocation(), Target);

	ExteriorDefaultViewPitch = Rotation.Pitch;
	ExteriorDefaultViewYaw = Rotation.Yaw;
	ExteriorDefaultViewDistanceRatio = Distance / Span;

	SaveExteriorCameraDefaultToDisk();
	UE_LOG(LogTemp, Warning, TEXT("Set exterior default view: Pitch=%.2f Yaw=%.2f DistanceRatio=%.3f"),
		ExteriorDefaultViewPitch, ExteriorDefaultViewYaw, ExteriorDefaultViewDistanceRatio);
}

bool ANavPlayerController::TryLoadExteriorCameraDefaultFromFile(const FString& FilePath)
{
	FString FileContents;
	if (!FFileHelper::LoadFileToString(FileContents, *FilePath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContents);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	double Pitch = 0.0, Yaw = 0.0, Ratio = 0.0;
	if (Root->TryGetNumberField(TEXT("defaultViewPitch"), Pitch))
	{
		ExteriorDefaultViewPitch = static_cast<float>(Pitch);
	}
	if (Root->TryGetNumberField(TEXT("defaultViewYaw"), Yaw))
	{
		ExteriorDefaultViewYaw = static_cast<float>(Yaw);
	}
	if (Root->TryGetNumberField(TEXT("defaultViewDistanceRatio"), Ratio) && Ratio > 0.0)
	{
		ExteriorDefaultViewDistanceRatio = static_cast<float>(Ratio);
	}
	return true;
}

void ANavPlayerController::LoadExteriorCameraDefaultFromDisk()
{
	if (TryLoadExteriorCameraDefaultFromFile(FPaths::ProjectSavedDir() / TEXT("ExteriorEditorLayout.json")))
	{
		return;
	}

	// No per-user save yet (first launch on this machine) - fall back to the team's own shipped
	// default instead of the hardcoded ExteriorDefaultView.../member-initializer values, same
	// Data/DefaultLayouts/ convention UInteriorScreenWidget::LoadDockLayoutFromDisk already uses for
	// its own team-default fallback.
	TryLoadExteriorCameraDefaultFromFile(FPaths::ProjectDir() / TEXT("Data") / TEXT("DefaultLayouts") / TEXT("DefaultExteriorLayout.json"));
}

void ANavPlayerController::SaveExteriorCameraDefaultToDisk() const
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("defaultViewPitch"), ExteriorDefaultViewPitch);
	Root->SetNumberField(TEXT("defaultViewYaw"), ExteriorDefaultViewYaw);
	Root->SetNumberField(TEXT("defaultViewDistanceRatio"), ExteriorDefaultViewDistanceRatio);

	FString OutputString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root, Writer);

	FFileHelper::SaveStringToFile(OutputString, *(FPaths::ProjectSavedDir() / TEXT("ExteriorEditorLayout.json")));
}

FString ANavPlayerController::GetProjectsDirectory() const
{
	if (!ProjectsDirectoryOverride.IsEmpty())
	{
		return ProjectsDirectoryOverride;
	}
	// ProjectDir() is a relative path in a packaged build, which the native dialogs cannot resolve.
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data"));
}

FString ANavPlayerController::GetProjectFilePathForName(const FString& Name) const
{
	return ProjectLibrary::FilePathForName(GetProjectsDirectory(), Name);
}

bool ANavPlayerController::CreateNewProject(const FString& Name, const FString& Description, FString& OutError)
{
	OutError.Reset();
	const FString TrimmedName = Name.TrimStartAndEnd();
	if (TrimmedName.IsEmpty())
	{
		OutError = TEXT("Enter a project name.");
		return false;
	}
	const FString FilePath = GetProjectFilePathForName(TrimmedName);
	if (FilePath.IsEmpty())
	{
		OutError = TEXT("The name needs at least one letter or number.");
		return false;
	}
	if (IFileManager::Get().FileExists(*FilePath))
	{
		OutError = FString::Printf(TEXT("A project named \"%s\" already exists. Choose another name."), *FPaths::GetBaseFilename(FilePath));
		return false;
	}
	if (!Generator)
	{
		OutError = TEXT("The level has no building generator, so a project cannot be created.");
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
	StartNewProject();
	Generator->SetProjectInfo(TrimmedName, Description.TrimStartAndEnd());
	if (!SaveCurrentProjectAs(FilePath))
	{
		OutError = TEXT("The project file could not be written - check that the Data folder can be written to.");
		return false;
	}
	return true;
}

void ANavPlayerController::RestoreExteriorCameraView()
{
	// Leaving interior editing entirely (any Back path) needs to unwind 3D preview mode first if it
	// was left active - otherwise SetOtherBuildingsHidden(true)'s hide never gets undone (every other
	// building stays hidden even back on the exterior cluster view) and SetIgnoreMoveInput/
	// SetIgnoreLookInput stay latched forever (the PlayerController outlives this screen, so a later
	// reload would come up with the camera still locked). Must run before InteriorGen->
	// ClearLoadedInterior() below, since SetIn3DPreviewMode(false) still needs to look up the
	// currently-loaded building.
	if (bIsIn3DPreviewMode)
	{
		SetIn3DPreviewMode(false);
	}

	// The navigation points and route are kept: the exterior screen has its own Navigation panel (outdoor mode) and the same
	// state is used there. Only a click-to-pick in progress ends here.
	if (NavigationPanel)
	{
		NavigationPanel->CancelSelection();
		NavigationPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	// Leaving interior editing entirely - cancel any in-progress draw room/wall/delete/prefab mode
	// so it doesn't keep intercepting clicks/Enter/Esc on whatever screen comes next.
	if (bDrawRoomMode || bDrawWallMode)
	{
		ExitDrawRoomMode();
	}
	ExitSelectMode();
	ExitPrefabRoomMode();
	ExitAddDoorMode();
	ExitConnectorMode();

	// Reset EVERY building's VisibleInteriorLevel, not just InteriorBuildingHiddenById (whichever one
	// was interior-edited most recently) - switching which building is being edited mid-session (the
	// building switcher, or entering/leaving 3D preview for a different building) can leave an EARLIER
	// building's VisibleInteriorLevel stuck non-zero too, since only the latest one ever got tracked.
	// Harmless to call on a building that's already fully visible.
	if (Generator)
	{
		for (APolyBuilding* Building : Generator->GetAllBuildings())
		{
			if (Building)
			{
				Building->SetVisibleInteriorLevel(0);
			}
		}
	}
	InteriorBuildingHiddenById.Empty();
	if (Generator) Generator->ApplyBuildingFilter();

	// Leaving interior editing entirely - destroy whatever rooms were spawned for it (safe: the
	// data itself lives in BuildingGenerator's InteriorDataById, not in these actors, so nothing is
	// lost; LoadInteriorForBuilding respawns them fresh from that data next time this building's
	// interior is opened). Clear the selection too - it would otherwise point at an actor that no
	// longer exists.
	if (InteriorGen)
	{
		InteriorGen->ClearLoadedInterior();
	}
	SelectRoom(FString());

	if (bHasSavedCameraView)
	{
		if (APawn* ControlledPawn = GetPawn())
		{
			ControlledPawn->SetActorLocationAndRotation(SavedCameraLocation, SavedCameraRotation);
			SetControlRotation(SavedCameraRotation);
		}
		bHasSavedCameraView = false;
	}

	if (!bCampusPreviewAllowed)
	{
		bCampusPreviewAllowed = true;
		BuildCampusInteriorPreview();
	}
}

bool ANavPlayerController::SaveCurrentProject()
{
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveCurrentProject: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."), EActionFailureSeverity::Warning);
		return false;
	}

	const bool bNewerFormat = Generator->GetLoadedFileVersion() > ABuildingGenerator::ProjectFileVersion;
	if (CurrentProjectFilePath.IsEmpty() || bNewerFormat)
	{
		if (bNewerFormat)
		{
			OnActionFailed.Broadcast(TEXT("This project uses a newer format. Choose a different file name to save a compatible copy."), EActionFailureSeverity::Warning);
		}
		const FString ChosenPath = BrowseForSaveLocation();
		if (ChosenPath.IsEmpty())
		{
			// User cancelled the dialog - not a failure, nothing to report.
			return false;
		}
		return SaveCurrentProjectAs(ChosenPath);
	}
	return SaveCurrentProjectAs(CurrentProjectFilePath);
}

FString ANavPlayerController::BrowseForSaveLocation()
{
#if !PLATFORM_DESKTOP
	OnActionFailed.Broadcast(TEXT("Could not open the save dialog on this system."), EActionFailureSeverity::Warning);
	return FString();
#else
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		UE_LOG(LogTemp, Error, TEXT("BrowseForSaveLocation: desktop platform not available"));
		OnActionFailed.Broadcast(TEXT("Could not open the save dialog on this system."), EActionFailureSeverity::Warning);
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
#endif
}

bool ANavPlayerController::SaveCurrentProjectAs(const FString& FilePath)
{
	if (bIsDragging3DRoom) OnLeftMouseUp3DEdit();
	for (TObjectIterator<UInteriorFloorPlanWidget> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld() && It->GetOwningPlayer() == this) It->FinishEditDrag();
	}
	OnBeforeProjectSave.Broadcast();
	if (!Generator)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveCurrentProjectAs: no BuildingGenerator found in the level"));
		OnActionFailed.Broadcast(TEXT("No BuildingGenerator found in the level."), EActionFailureSeverity::Warning);
		return false;
	}

	if (FilePath.IsEmpty())
	{
		OnActionFailed.Broadcast(TEXT("No save location chosen."), EActionFailureSeverity::Warning);
		return false;
	}

	if (InteriorGen)
	{
		InteriorGen->SyncInteriorToBuildingGenerator();
	}

	FString Problem;
	const bool bSuccess = Generator->SaveBuildingsToFile(FilePath, Problem);
	if (bSuccess)
	{
		Generator->RefreshBuildingBaselines();
		UnsavedBuildings.Reset();
		CurrentProjectFilePath = FilePath;
		bHasUnsavedChanges = false;
		UE_LOG(LogTemp, Warning, TEXT("Saved to %s"), *FilePath);
		OnProjectSaved.Broadcast();
	}
	else
	{
		OnActionFailed.Broadcast(Problem.IsEmpty() ? TEXT("Could not save - see the Output Log for details.") : Problem, EActionFailureSeverity::Critical);
	}
	return bSuccess;
}

void ANavPlayerController::BindGameWindowCloseGuard(UGameViewportClient* Viewport)
{
	if (!Viewport || Viewport->OnWindowCloseRequested().IsBound()) return;
	GuardedGameViewport = Viewport;
	Viewport->OnWindowCloseRequested().BindUObject(this, &ANavPlayerController::HandleGameWindowCloseRequested);
}

void ANavPlayerController::UnbindGameWindowCloseGuard()
{
	if (auto* Viewport = GuardedGameViewport.Get(); Viewport && Viewport->OnWindowCloseRequested().IsBoundToObject(this))
	{
		Viewport->OnWindowCloseRequested().Unbind();
	}
	GuardedGameViewport.Reset();
	if (auto* Dialog = WindowCloseDialog.Get()) Dialog->RemoveFromParent();
	WindowCloseDialog.Reset();
}

bool ANavPlayerController::HandleGameWindowCloseRequested()
{
	if (!HasUnsavedChanges()) return true;
	if (!WindowCloseDialog.IsValid())
	{
		if (auto* Dialog = CreateWidget<UConfirmDialogWidget>(this, UConfirmDialogWidget::StaticClass()))
		{
			WindowCloseDialog = Dialog;
			Dialog->OnSaveAndLeave.AddUniqueDynamic(this, &ANavPlayerController::SaveAndCloseGameWindow);
			Dialog->OnDiscardAndLeave.AddUniqueDynamic(this, &ANavPlayerController::DiscardAndCloseGameWindow);
			Dialog->OnCancel.AddUniqueDynamic(this, &ANavPlayerController::CancelGameWindowClose);
			Dialog->AddToViewport(1000);
		}
	}
	return false;
}

void ANavPlayerController::SaveAndCloseGameWindow()
{
	const bool bSaved = SaveCurrentProject();
	WindowCloseDialog.Reset();
	if (bSaved) CloseGameWindow();
}

void ANavPlayerController::DiscardAndCloseGameWindow()
{
	WindowCloseDialog.Reset();
	DiscardUnsavedChanges();
	if (!HasUnsavedChanges()) CloseGameWindow();
}

void ANavPlayerController::CancelGameWindowClose()
{
	WindowCloseDialog.Reset();
}

void ANavPlayerController::CloseGameWindow()
{
	if (auto* Viewport = GuardedGameViewport.Get())
	{
		if (auto Window = Viewport->GetWindow()) Window->RequestDestroyWindow();
	}
}

void ANavPlayerController::MarkBuildingCreated(APolyBuilding* Building)
{
	UnsavedBuildings.Add(Building);
	bHasUnsavedChanges = true;
}

void ANavPlayerController::DiscardUnsavedChanges()
{
	SetInteriorInspectionEnabled(false);
	SetIn3DPreviewMode(false);
	if (InteriorGen)
	{
		InteriorGen->ClearLoadedInterior();
	}
	for (auto It = UnsavedBuildings.CreateIterator(); It; ++It)
	{
		APolyBuilding* Building = It->Get();
		if (!Building)
		{
			It.RemoveCurrent();
		}
		else if (Generator && Generator->RemoveBuilding(*Building))
		{
			if (SelectedBuilding == Building)
			{
				SelectedBuilding = nullptr;
				OnBuildingSelected.Broadcast(FString());
			}
			BuildingReveal.Remove(Building->BuildingId);
			It.RemoveCurrent();
		}
	}
	if (Generator)
	{
		Generator->RestoreBuildingBaselines();
	}
	if (!bCampusPreviewAllowed)
	{
		RestoreExteriorCameraView();
	}
	else
	{
		BuildCampusInteriorPreview();
	}
	bHasUnsavedChanges = !UnsavedBuildings.IsEmpty();
	if (bHasUnsavedChanges)
	{
		OnActionFailed.Broadcast(TEXT("Some new buildings could not be discarded."), EActionFailureSeverity::Warning);
	}
}

void ANavPlayerController::SaveCurrentProjectKeyHandler()
{
	SaveCurrentProject();
}
void ANavPlayerController::UndoInteriorKeyHandler()
{
	UndoInteriorEdit();
}
void ANavPlayerController::RedoInteriorKeyHandler()
{
	RedoInteriorEdit();
}

bool ANavPlayerController::UndoInteriorEdit()
{
	if (bInteriorInspectionEnabled) return false;
	if (InteriorGen && InteriorGen->Undo())
	{
		if (NavigationPanel) NavigationPanel->NotifyInteriorChanged();
		SelectRoom(FString());
		bHasUnsavedChanges = true;
		return true;
	}
	return false;
}

bool ANavPlayerController::RedoInteriorEdit()
{
	if (bInteriorInspectionEnabled) return false;
	if (InteriorGen && InteriorGen->Redo())
	{
		if (NavigationPanel) NavigationPanel->NotifyInteriorChanged();
		SelectRoom(FString());
		bHasUnsavedChanges = true;
		return true;
	}
	return false;
}
void ANavPlayerController::CopyInteriorKeyHandler()
{
	// If no room is selected, copy the selected whole building instead.
	if (SelectedRoomId.IsEmpty() && SelectedBuilding && Generator)
	{
		// Make sure the latest interior edits are stored in BuildingGenerator
		// before copying the whole building.
		if (InteriorGen)
		{
			InteriorGen->SyncInteriorToBuildingGenerator();
		}

		CopiedBuildingId = SelectedBuilding->BuildingId;
		CopiedBuildingName = SelectedBuilding->BuildingName;
		CopiedBuildingFootprint = SelectedBuilding->GetFootprint();
		CopiedBuildingFloorCount = SelectedBuilding->GetFloorCount();
		CopiedBuildingFloorHeight = SelectedBuilding->GetFloorHeight();
		CopiedBuildingOffset = SelectedBuilding->BuildingOffset;
		CopiedBuildingInterior =
			Generator->GetInteriorDataForBuilding(CopiedBuildingId);

		bHasCopiedBuilding = true;
		bHasCopiedRoom = false;

		UE_LOG(LogTemp, Log,
			TEXT("Copied whole building: %s"),
			*CopiedBuildingId);

		return;
	}
	if (!InteriorGen || SelectedRoomId.IsEmpty())
	{
		return;
	}

	AInteriorRoom* Room = InteriorGen->GetRoomById(SelectedRoomId);

	if (!Room)
	{
		return;
	}

	CopiedRoomId = Room->RoomId;
	CopiedRoomFootprint = Room->GetFootprint();
	CopiedRoomOpenings = Room->GetOpenings();
	CopiedRoomScale = GetBuildingScale();
	bCopiedRoomClosed = Room->IsClosedLoop();
	CopiedRoomName = Room->RoomName;
	CopiedRoomTags = Room->RoomTags;
	CopiedRoomType = Room->RoomType;
	bCopiedRoomIsPublic = Room->bIsPublic;
	bCopiedRoomRequiresKey = Room->bRequiresKey;
	bHasCopiedRoom = true;

	UE_LOG(LogTemp, Log, TEXT("Copied room: %s"), *CopiedRoomId);
}

TArray<FVector2D> ANavPlayerController::PastedFootprint(const TArray<FVector2D>& Footprint, const FVector2D& BuildingOffset, float Scale, const FVector2D& CursorWorld)
{
	TArray<FVector2D> Result = Footprint;
	if (Result.IsEmpty() || Scale <= 0.f)
	{
		return Result;
	}
	FVector2D Center = FVector2D::ZeroVector;
	for (const FVector2D& Point : Result)
	{
		Center += Point;
	}
	Center /= Result.Num();
	const FVector2D Move = CursorWorld / Scale - BuildingOffset - Center;
	for (FVector2D& Point : Result)
	{
		Point += Move;
	}
	return Result;
}

void ANavPlayerController::PasteInteriorKeyHandler()
{
	if (bInteriorInspectionEnabled) return;
	// Paste a whole building when the building clipboard is active.
	if (bHasCopiedBuilding && Generator && !CopiedBuildingFootprint.IsEmpty())
	{
		FHitResult Hit;
		GetHitResultUnderCursor(ECC_Visibility, false, Hit);

		if (!Hit.bBlockingHit)
		{
			UE_LOG(LogTemp, Warning, TEXT("Building paste failed: cursor did not hit the ground."));
			return;
		}

		const float Scale = Generator->GetScale();

		// Move the copied outline so the building's centre is under the mouse (the offset it stands at counts, see PastedFootprint).
		const TArray<FVector2D> NewFootprint = PastedFootprint(
			CopiedBuildingFootprint, CopiedBuildingOffset, Scale, FVector2D(Hit.Location.X, Hit.Location.Y));
		// How far the outline moved, which the interior has to move by as well.
		const FVector2D MoveOffset = NewFootprint[0] - CopiedBuildingFootprint[0];

		const FString NewId =
			Generator->GenerateUniqueBuildingId(CopiedBuildingId);

		const FString NewName =
			CopiedBuildingName + TEXT(" Copy");

		APolyBuilding* NewBuilding =
			Generator->SpawnBuildingFromData(
				NewId,
				NewName,
				NewFootprint,
				CopiedBuildingFloorCount,
				CopiedBuildingFloorHeight,
				CopiedBuildingOffset
			);

		if (!NewBuilding)
		{
			UE_LOG(LogTemp, Warning, TEXT("Building paste failed."));
			return;
		}

		if (CopiedBuildingInterior.IsValid() && InteriorGen)
		{
			TSharedPtr<FJsonObject> TranslatedInterior =
				InteriorGen->CreateTranslatedInteriorCopy(
					CopiedBuildingInterior,
					MoveOffset
				);

			if (TranslatedInterior.IsValid())
			{
				Generator->SetInteriorDataForBuilding(
					NewId,
					TranslatedInterior
				);
			}
		}

		MarkBuildingCreated(NewBuilding);
		if (bCampusPreviewAllowed && CopiedBuildingInterior.IsValid())
		{
			BuildCampusInteriorPreview();
		}

		UE_LOG(LogTemp, Log,
			TEXT("Pasted whole building: %s"),
			*NewId);

		return;
	}
	if (!InteriorGen || !bHasCopiedRoom || CopiedRoomFootprint.IsEmpty())
	{
		return;
	}

	TArray<FVector2D> NewFootprint = CopiedRoomFootprint;
	for (FVector2D& Point : NewFootprint) Point *= CopiedRoomScale / GetBuildingScale();
	bool bFoundPastePosition = false;
	// Move the pasted copy slightly so it does not sit exactly on top.
	const FVector2D CursorScreenPosition = FSlateApplication::Get().GetCursorPos();

	for (TObjectIterator<UInteriorFloorPlanWidget> It; It; ++It)
	{
		UInteriorFloorPlanWidget* FloorPlan = *It;

		if (!FloorPlan ||
			FloorPlan->GetWorld() != GetWorld() ||
			!FloorPlan->IsVisible())
		{
			continue;
		}

		const FGeometry& FloorGeometry = FloorPlan->GetCachedGeometry();

		if (!FloorGeometry.IsUnderLocation(CursorScreenPosition))
		{
			continue;
		}

		const FVector2D LocalPosition =
			FloorGeometry.AbsoluteToLocal(CursorScreenPosition);

		const FVector2D PastePosition =
			FloorPlan->LocalPositionToFootprintPoint(
				LocalPosition,
				FloorGeometry.GetLocalSize());
		FVector2D Center = FVector2D::ZeroVector;

		for (const FVector2D& Point : NewFootprint)
		{
			Center += Point;
		}

		Center /= NewFootprint.Num();

		const FVector2D Offset = PastePosition - Center;

		for (FVector2D& Point : NewFootprint)
		{
			Point += Offset;
			
		}
		bFoundPastePosition = true;
		break;
	}
	FString CreateError;
	AInteriorRoom* NewRoom = bCopiedRoomClosed ? InteriorGen->CreateRoomFromFootprintWithTags(
		NewFootprint,
		FString(),                 // Empty = generate a new unique ID
		CopiedRoomName,
		bCopiedRoomIsPublic,
		bCopiedRoomRequiresKey,
		/*bAllowOverlap=*/false,
		CreateError,
		CopiedRoomTags,
		CopiedRoomType,
		CopiedRoomOpenings
	) : InteriorGen->CreateWallFromPoints(NewFootprint, FString(), CreateError, CopiedRoomOpenings, CopiedRoomName);

	if (NewRoom)
	{
		SelectRoom(NewRoom->RoomId);
		bHasUnsavedChanges = true;

		UE_LOG(LogTemp, Log,
			TEXT("Pasted room: %s"),
			*NewRoom->RoomId);
	}
	else
	{
		// Previously silent on failure (e.g. pasting on top of the copied room's own old spot, or
		// anything else CreateRoomFromFootprint rejects) - the user would see nothing happen with no
		// indication why.
		UE_LOG(LogTemp, Warning, TEXT("Could not paste the room: %s"), *CreateError);
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not paste the room - %s"), *CreateError), EActionFailureSeverity::Warning);
	}
}
// ---- Permanent Interior pass-throughs (see header) ----

int32 ANavPlayerController::LoadInteriorForBuilding(const FString& BuildingId)
{
	return LoadInteriorInternal(BuildingId, /*bKeepEditsOfCurrent=*/true);
}

int32 ANavPlayerController::LoadInteriorForBuildingAfterDiscard(const FString& BuildingId)
{
	return LoadInteriorInternal(BuildingId, /*bKeepEditsOfCurrent=*/false);
}

int32 ANavPlayerController::LoadInteriorInternal(const FString& BuildingId, bool bKeepEditsOfCurrent)
{
	if (!InteriorGen)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadInteriorForBuilding: no InteriorGenerator placed in the level"));
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."), EActionFailureSeverity::Warning);
		return -1;
	}

	// Switching straight to a different building's interior (e.g. via the building switcher) while
	// still in 3D preview mode for the PREVIOUS building would otherwise leave that previous building
	// hidden forever (SetOtherBuildingsHidden's hide never gets undone) and move/look input still
	// ignored - unwind it first, same reasoning as RestoreExteriorCameraView. Must run before
	// InteriorGen->LoadInteriorForBuilding below overwrites which building is "loaded".
	if (bIsIn3DPreviewMode)
	{
		SetIn3DPreviewMode(false);
	}

	// The exterior "this building is selected" yellow tint (APolyBuilding::SetHighlighted) never
	// meant anything once you're inside editing/viewing that building's own interior - clearing it
	// here (not just on deselect/reselect in SelectBuildingUnderCursor) was invisible in the 2D floor
	// plan (its opaque Background border hides the real exterior mesh underneath entirely), so this
	// went unnoticed until 3D preview mode exposed the same still-tinted wall mesh directly.
	if (SelectedBuilding)
	{
		SelectedBuilding->SetHighlighted(false);
		SelectedBuilding = nullptr;
		OnBuildingSelected.Broadcast(FString());
	}

	// What "Discard" will put this building back to (kept if one already exists - see CaptureBuildingBaseline).
	if (Generator)
	{
		Generator->CaptureBuildingBaseline(BuildingId);
	}

	// The building being left is stored first so its edits are not lost - except after Discard, which has just put its stored data
	// back: writing the live copy over it would bring the discarded edits back.
	if (bKeepEditsOfCurrent && !InteriorGen->GetLoadedBuildingId().IsEmpty() && InteriorGen->GetLoadedBuildingId() != BuildingId)
	{
		InteriorGen->SyncInteriorToBuildingGenerator();
	}
	// Hide campus preview rooms while editing an individual building.
	HideCampusInteriorPreview();
	const int32 Result = InteriorGen->LoadInteriorForBuilding(BuildingId);
	if (Result < 0)
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not load the interior for '%s' - no building with that id exists."), *BuildingId), EActionFailureSeverity::Warning);
	}
	else if (Generator)
	{
		FocusCameraOnBuilding(Generator->FindBuildingById(BuildingId));
		if (NavigationPanel)
		{
			NavigationPanel->NotifyViewedBuildingChanged();
			// The navigation widget fills the viewport and sits above WBP_InteriorScreen.
			// Keep the outer widget out of hit testing so its empty canvas does not block
			// the room-editing controls below it. Its visible children (the navigation
			// panel and buttons) remain hit-testable.
			NavigationPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			UE_LOG(LogTemp, Warning,
				TEXT("[Navigation] Navigation panel made visible after loading interior: building=%s floor=%d"),
				*BuildingId, InteriorGen->GetActiveFloor());
		}
	}
	return Result;
}

void ANavPlayerController::SetActiveFloor(int32 Level)
{
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."), EActionFailureSeverity::Warning);
		return;
	}

	InteriorGen->SetActiveFloor(Level);
	RepositionCameraToActiveFloor();
	// A selected connector's marker is a separate actor per floor (see
	// AInteriorGenerator::SetActiveFloor's own per-floor hide/show) - re-sync which one is tinted so
	// the 3D Edit mode highlight follows the connector onto whichever of its floors is now visible,
	// instead of staying stuck on the floor it was originally selected on.
	RefreshSelectionHighlight();
	// The route/start/destination markers are per-floor too (see RefreshRoute3DVisualization's own
	// active-floor filtering) - redraw for whichever floor is now active.
	RefreshRoute3DVisualization();
	OnActiveFloorChanged.Broadcast();
}

int32 ANavPlayerController::GetActiveFloor() const
{
	return InteriorGen ? InteriorGen->GetActiveFloor() : 0;
}

int32 ANavPlayerController::AddFloor()
{
	if (bInteriorInspectionEnabled) return -1;
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."), EActionFailureSeverity::Warning);
		return -1;
	}

	const int32 NewLevel = InteriorGen->AddFloor();
	if (NewLevel < 0)
	{
		OnActionFailed.Broadcast(TEXT("Could not add a floor - load a building's interior first."), EActionFailureSeverity::Warning);
	}
	else
	{
		bHasUnsavedChanges = true;
		RepositionCameraToActiveFloor();
		OnActiveFloorChanged.Broadcast();
	}
	return NewLevel;
}

bool ANavPlayerController::RemoveFloor(int32 Level)
{
	if (bInteriorInspectionEnabled) return false;
	if (!InteriorGen)
	{
		OnActionFailed.Broadcast(TEXT("No InteriorGenerator placed in the level."), EActionFailureSeverity::Warning);
		return false;
	}

	const bool bSuccess = InteriorGen->RemoveFloor(Level);
	if (!bSuccess)
	{
		OnActionFailed.Broadcast(TEXT("Could not remove that floor - it may not exist, or it's the last remaining floor."), EActionFailureSeverity::Warning);
	}
	else
	{
		bHasUnsavedChanges = true;
		RepositionCameraToActiveFloor();
		OnActiveFloorChanged.Broadcast();
	}
	return bSuccess;
}

FString ANavPlayerController::GetLoadedBuildingId() const
{
	return InteriorGen ? InteriorGen->GetLoadedBuildingId() : FString();
}

float ANavPlayerController::GetLoadedBuildingFloorHeight() const
{
	if (Generator && InteriorGen)
	{
		if (const APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId()))
		{
			return Building->GetFloorHeight();
		}
	}
	return 300.f;
}

int32 ANavPlayerController::GetLoadedBuildingFloorCount() const
{
	if (!InteriorGen || !Generator)
	{
		return 0;
	}

	const APolyBuilding* Building = Generator->FindBuildingById(InteriorGen->GetLoadedBuildingId());
	return Building ? Building->GetFloorCount() : 0;
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

TArray<FString> ANavPlayerController::GetRoomIdsOnFloor(int32 Level) const
{
	TArray<FString> RoomIds;
	if (!InteriorGen)
	{
		return RoomIds;
	}

	RoomIds.Reserve(InteriorGen->GetRoomsOnFloor(Level).Num());
	for (const AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(Level))
	{
		if (Room)
		{
			RoomIds.Add(Room->RoomId);
		}
	}
	return RoomIds;
}

FString ANavPlayerController::GetRoomName(const FString& RoomId) const
{
	return InteriorGen ? InteriorGen->GetRoomName(RoomId) : FString();
}

bool ANavPlayerController::IsRoomClosedLoop(const FString& RoomId) const
{
	return InteriorGen && InteriorGen->IsRoomClosedLoop(RoomId);
}

void ANavPlayerController::SetRoomHidden(const FString& RoomId, bool bNewHidden)
{
	if (bNewHidden)
	{
		HiddenRoomIds.Add(RoomId);
	}
	else
	{
		HiddenRoomIds.Remove(RoomId);
	}
}

void ANavPlayerController::SetPathNodeHidden(const FString& NodeId, bool bNewHidden)
{
	if (bNewHidden)
	{
		HiddenPathNodeIds.Add(NodeId);
	}
	else
	{
		HiddenPathNodeIds.Remove(NodeId);
	}
}

void ANavPlayerController::SetIONodeHidden(const FString& NodeId, bool bNewHidden)
{
	if (bNewHidden)
	{
		HiddenIONodeIds.Add(NodeId);
	}
	else
	{
		HiddenIONodeIds.Remove(NodeId);
	}
}

FString ANavPlayerController::GetSelectedBuildingId() const
{
	return SelectedBuilding ? SelectedBuilding->BuildingId : FString();
}

TArray<FString> ANavPlayerController::GetAllBuildingNames() const
{
	TArray<FString> Names;
	if (!Generator)
	{
		return Names;
	}

	for (const FString& Id : Generator->GetAllBuildingIds())
	{
		Names.Add(Generator->GetBuildingName(Id));
	}
	return Names;
}

FString ANavPlayerController::FindBuildingIdByName(const FString& Name) const
{
	return Generator ? Generator->FindBuildingIdByName(Name) : FString();
}

FString ANavPlayerController::GetLoadedBuildingName() const
{
	if (!InteriorGen || !Generator)
	{
		return FString();
	}
	return Generator->GetBuildingName(InteriorGen->GetLoadedBuildingId());
}

bool ANavPlayerController::SetLoadedBuildingName(const FString& NewName)
{
	if (bInteriorInspectionEnabled) return false;
	if (!InteriorGen || !Generator)
	{
		return false;
	}

	const FString LoadedId = InteriorGen->GetLoadedBuildingId();
	if (LoadedId.IsEmpty())
	{
		return false;
	}

	const bool bSuccess = Generator->SetBuildingName(LoadedId, NewName);
	if (bSuccess)
	{
		bHasUnsavedChanges = true;
	}
	else
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not rename '%s' to '%s' - that name is already in use."), *LoadedId, *NewName), EActionFailureSeverity::Warning);
	}
	return bSuccess;
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

void ANavPlayerController::GrowSelectedRoom()
{
	NudgeSelectedRoomSize(20.f);
}

void ANavPlayerController::ShrinkSelectedRoom()
{
	NudgeSelectedRoomSize(-20.f);
}

void ANavPlayerController::NudgeSelectedRoomSize(float Delta)
{
	// 3D View mode is genuinely edit-proof, not just "safe because the 2D widget that would trigger
	// this is hidden" - a room/connector selected before entering 3D preview would otherwise still
	// respond to a stray =/- press with nothing on screen to show it happened.
	if (bIsIn3DPreviewMode && !bIsIn3DEditMode)
	{
		return;
	}

	// Grow/Shrink work on whichever kind of thing is currently selected - a connector (elevator/
	// stairs/escalator) has no width/depth of its own, just a uniform SizeScale multiplier, so this
	// branches off to a completely different code path (Delta's 20-unit convention below doesn't
	// apply there at all - ConnectorScaleStep is its own, separate step size).
	if (!SelectedConnectorGroupId.IsEmpty() && InteriorGen)
	{
		const float ScaleDelta = (Delta > 0.f) ? ConnectorScaleStep : -ConnectorScaleStep;
		if (InteriorGen->ScaleConnectorGroup(SelectedConnectorGroupId, ScaleDelta))
		{
			bHasUnsavedChanges = true;
		}
		return;
	}

	if (SelectedRoomId.IsEmpty() || !InteriorGen)
	{
		return;
	}

	for (AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
	{
		if (!Room || Room->RoomId != SelectedRoomId)
		{
			continue;
		}

		const TArray<FVector2D>& Footprint = Room->GetFootprint();
		if (Footprint.Num() == 0)
		{
			return;
		}

		FVector2D MinPoint(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
		FVector2D MaxPoint(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
		for (const FVector2D& Point : Footprint)
		{
			MinPoint.X = FMath::Min(MinPoint.X, Point.X);
			MinPoint.Y = FMath::Min(MinPoint.Y, Point.Y);
			MaxPoint.X = FMath::Max(MaxPoint.X, Point.X);
			MaxPoint.Y = FMath::Max(MaxPoint.Y, Point.Y);
		}

		const float NewWidth = FMath::Max(10.f, (MaxPoint.X - MinPoint.X) + Delta);
		const float NewDepth = FMath::Max(10.f, (MaxPoint.Y - MinPoint.Y) + Delta);
		ResizeRoom(SelectedRoomId, NewWidth, NewDepth);
		return;
	}
}

void ANavPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (ActiveAudioPlaybacks.Num() > 0)
	{
		PruneFinishedAudioPlaybacks();
	}
	if (bInteriorInspectionEnabled)
	{
		TickInteriorInspection();
		return;
	}

	if (bIsDragging3DRoom && (!IsInputKeyDown(EKeys::LeftMouseButton) || !Dragging3DRoom.IsValid())) OnLeftMouseUp3DEdit();
	if (bIsDragging3DRoom)
	{
		FHitResult Hit;
		GetHitResultUnderCursor(ECC_Visibility, false, Hit);

		FVector2D CurrentFootprintPosition;
		if (Get3DFloorFootprintPoint(Hit, CurrentFootprintPosition))
		{
			const FVector2D Delta = CurrentFootprintPosition - DragStart3DFootprintPosition;

			TArray<FVector2D> NewFootprint;
			NewFootprint.Reserve(DragStart3DRoomFootprint.Num());
			for (const FVector2D& Point : DragStart3DRoomFootprint)
			{
				NewFootprint.Add(Point + Delta);
			}

			MoveRoomTo(Dragging3DRoomId, NewFootprint);
		}
	}

	if (bRotatingCCW != bRotatingCW)
	{
		const float Direction = bRotatingCCW ? -1.f : 1.f;
		RotateSelectedRoom(Direction * RotationDegreesPerSecond * DeltaSeconds);
	}
	if (!bIsIn3DPreviewMode && !bInteriorInspectionEnabled)
	{
		UpdateBuildingTransparency();
	}
}

void ANavPlayerController::RotateSelectedRoom(float DeltaDegrees)
{
	// See NudgeSelectedRoomSize's own comment - same View-mode edit-proofing.
	if (bIsIn3DPreviewMode && !bIsIn3DEditMode)
	{
		return;
	}

	// Same room-vs-connector branch as NudgeSelectedRoomSize - see its own comment. Unlike room
	// rotation, connector rotation has no deferred/mid-gesture-allowed check (see
	// AInteriorGenerator::RotateConnectorGroup's own comment), so there's no snapshot/commit dance to
	// set up here - BeginRotationGestureIfNeeded/EndRotationGestureIfFinished simply don't do
	// anything when a connector (not a room) is selected.
	if (!SelectedConnectorGroupId.IsEmpty() && InteriorGen)
	{
		if (InteriorGen->RotateConnectorGroup(SelectedConnectorGroupId, DeltaDegrees))
		{
			bHasUnsavedChanges = true;
		}
		return;
	}

	if (SelectedRoomId.IsEmpty() || !InteriorGen)
	{
		return;
	}

	if (InteriorGen->RotateRoom(SelectedRoomId, DeltaDegrees))
	{
		bHasUnsavedChanges = true;
	}
}

void ANavPlayerController::StartRotateCCW()
{
	if (bInteriorInspectionEnabled) return;
	BeginRotationGestureIfNeeded();
	bRotatingCCW = true;
}

void ANavPlayerController::StopRotateCCW()
{
	bRotatingCCW = false;
	EndRotationGestureIfFinished();
}

void ANavPlayerController::StartRotateCW()
{
	if (bInteriorInspectionEnabled) return;
	BeginRotationGestureIfNeeded();
	bRotatingCW = true;
}

void ANavPlayerController::StopRotateCW()
{
	bRotatingCW = false;
	EndRotationGestureIfFinished();
}

void ANavPlayerController::BeginRotationGestureIfNeeded()
{
	// Only snapshot at the true start of a gesture (neither key held yet) - if the other rotation
	// key is already down, we're still mid-gesture and the original snapshot must be kept.
	if (bRotatingCCW || bRotatingCW || !InteriorGen)
	{
		return;
	}

	// Connector rotation is fully validated every tick (see RotateConnectorGroup), nothing deferred
	// to check on release, so there's no snapshot to take - and SelectedRoomId is empty whenever a
	// connector is selected anyway (the two are mutually exclusive, see SelectConnector), so the
	// SelectedRoomId.IsEmpty() check below would already no-op here regardless. This explicit branch
	// just makes that reasoning obvious rather than relying on it silently falling through.

	if (SelectedRoomId.IsEmpty())
	{
		return;
	}

	for (const AInteriorRoom* Room : InteriorGen->GetRoomsOnFloor(InteriorGen->GetActiveFloor()))
	{
		if (Room && Room->RoomId == SelectedRoomId)
		{
			RotationStartFootprint = Room->GetFootprint();
			break;
		}
	}
}

void ANavPlayerController::EndRotationGestureIfFinished()
{
	if (bInteriorInspectionEnabled) return;
	if (!bRotatingCCW && !bRotatingCW &&
		!SelectedConnectorGroupId.IsEmpty() &&
		InteriorGen)
	{
		InteriorGen->RecordHistorySnapshot();
		return;
	}
	// Only commit once both keys are up - releasing one while the other is still held just means
	// switching direction mid-gesture, not the end of it.
	if (bRotatingCCW || bRotatingCW || SelectedRoomId.IsEmpty() || !InteriorGen || RotationStartFootprint.Num() == 0)
	{
		return;
	}

	InteriorGen->CommitRoomRotation(SelectedRoomId, RotationStartFootprint);
	RotationStartFootprint.Reset();
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
	else
	{
		MarkBuildingCreated(NewBuilding);
	}
}

FString ANavPlayerController::GetAudioFolderPath()
{
	// FPaths::ProjectDir() is relative (something like "../../../InNavi/"), not absolute -
	// ConvertRelativePathToFull isn't strictly required by FFileHelper::LoadFileToArray (which
	// resolves a relative path against the working directory just fine), but costs nothing and keeps
	// this robust against whatever future code ends up needing a real absolute path here too.
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data") / TEXT("Audio"));
}

TArray<FString> ANavPlayerController::GetAvailableAudioFiles() const
{
	TArray<FString> FoundFiles;
	// Not "FolderPath" - that name collides with a class member somewhere in this class's own
	// inheritance chain (C4458 hides-class-member, a compile error in this project).
	const FString AudioFolder = GetAudioFolderPath();
	IFileManager::Get().FindFiles(FoundFiles, *(AudioFolder / TEXT("*.wav")), true, false);
	FoundFiles.Sort();
	return FoundFiles;
}

bool ANavPlayerController::PlayAudioFile(const FString& FileName)
{
	if (IsAudioFilePlaying(FileName))
	{
		return false;
	}

	UAudioComponent* AudioComponent = PlayWavFile(FileName, /*bDestroyWhenFinished=*/false);
	if (!AudioComponent)
	{
		OnActionFailed.Broadcast(FString::Printf(TEXT("Could not play audio file '%s'."), *FileName), EActionFailureSeverity::Warning);
		return false;
	}

	FActiveAudioPlayback& Playback = ActiveAudioPlaybacks.AddDefaulted_GetRef();
	Playback.FileName = FileName;
	Playback.AudioComponent = AudioComponent;
	return true;
}

void ANavPlayerController::StopAllAudio()
{
	for (FActiveAudioPlayback& Playback : ActiveAudioPlaybacks)
	{
		if (Playback.AudioComponent)
		{
			Playback.AudioComponent->Stop();
			Playback.AudioComponent->DestroyComponent();
		}
	}
	ActiveAudioPlaybacks.Reset();
}

bool ANavPlayerController::IsAudioFilePlaying(const FString& FileName) const
{
	for (const FActiveAudioPlayback& Playback : ActiveAudioPlaybacks)
	{
		if (Playback.FileName == FileName)
		{
			return true;
		}
	}
	return false;
}

void ANavPlayerController::PlayButtonClickSound()
{
	if (!bButtonClickSoundEnabled)
	{
		return;
	}

	PlayShortSoundEffect(TEXT("click_button.wav"));
}

void ANavPlayerController::PlayBuildingSelectSound()
{
	if (!bBuildingSelectSoundEnabled)
	{
		return;
	}

	PlayShortSoundEffect(TEXT("select_ui.wav"));
}

void ANavPlayerController::PlayQuitConfirmSound()
{
	if (!bQuitConfirmSoundEnabled)
	{
		return;
	}

	PlayShortSoundEffect(TEXT("quit_confirm.wav"));
}

void ANavPlayerController::SetAllUiSoundsEnabled(bool bEnabled)
{
	bButtonClickSoundEnabled = bEnabled;
	bBuildingSelectSoundEnabled = bEnabled;
	bQuitConfirmSoundEnabled = bEnabled;
}

void ANavPlayerController::SetMasterVolume(float NewVolume)
{
	MasterVolume = FMath::Clamp(NewVolume, 0.f, 1.f);

	for (const FActiveAudioPlayback& Playback : ActiveAudioPlaybacks)
	{
		if (Playback.AudioComponent)
		{
			Playback.AudioComponent->SetVolumeMultiplier(MasterVolume);
		}
	}
}

void ANavPlayerController::PlayShortSoundEffect(const TCHAR* FileName)
{
	PlayWavFile(FileName, /*bDestroyWhenFinished=*/true);
}

UAudioComponent* ANavPlayerController::PlayWavFile(const FString& FileName, bool bDestroyWhenFinished)
{
	const FString FullPath = GetAudioFolderPath() / FileName;
	if (!FPaths::FileExists(FullPath))
	{
		return nullptr;
	}

	USoundWaveProcedural* SoundWave = LoadWavFileAsSoundWave(FullPath, this);
	if (!SoundWave)
	{
		return nullptr;
	}

	if (!AudioActor)
	{
		AudioActor = GetWorld()->SpawnActor<AActor>();
	}
	if (!AudioActor)
	{
		return nullptr;
	}

	UAudioComponent* AudioComponent = NewObject<UAudioComponent>(AudioActor);
	AudioComponent->bAutoActivate = false;
	// AudioActor sits at the world origin (see its own comment) - miles from wherever the camera
	// actually is on this campus-scale map. A spatialized 3D sound would attenuate with distance from
	// the listener and be effectively silent from way out here; this is a UI/global sound (BGM,
	// button click, ...), not something tied to a position in the world, so play it at a flat volume
	// regardless of where AudioActor happens to be.
	AudioComponent->bAllowSpatialization = false;
	AudioComponent->SetSound(SoundWave);
	AudioComponent->SetVolumeMultiplier(MasterVolume);
	AudioComponent->RegisterComponent();
	AudioComponent->Play();

	// See this function's own header comment - USoundWaveProcedural never signals "finished" on its
	// own, so this is what actually stops the voice (and, for a fire-and-forget effect, frees the
	// component) once the clip's real length has elapsed.
	const float Duration = FMath::Max(SoundWave->Duration, 0.05f);
	FTimerHandle StopTimerHandle;
	TWeakObjectPtr<UAudioComponent> WeakAudioComponent = AudioComponent;
	GetWorldTimerManager().SetTimer(StopTimerHandle, [WeakAudioComponent, bDestroyWhenFinished]()
	{
		if (!WeakAudioComponent.IsValid())
		{
			return;
		}
		WeakAudioComponent->Stop();
		if (bDestroyWhenFinished)
		{
			WeakAudioComponent->DestroyComponent();
		}
	}, Duration, false);

	return AudioComponent;
}

void ANavPlayerController::PruneFinishedAudioPlaybacks()
{
	for (int32 i = ActiveAudioPlaybacks.Num() - 1; i >= 0; i--)
	{
		FActiveAudioPlayback& Playback = ActiveAudioPlaybacks[i];
		if (!Playback.AudioComponent || Playback.AudioComponent->IsPlaying())
		{
			continue;
		}

		Playback.AudioComponent->DestroyComponent();
		ActiveAudioPlaybacks.RemoveAt(i);
	}
}
