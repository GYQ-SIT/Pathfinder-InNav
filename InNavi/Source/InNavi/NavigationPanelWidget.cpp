#include "NavigationPanelWidget.h"

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "PolyBuilding.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"

namespace
{
	FString ConnectorTypeName(EInteriorNavNodeType Type)
	{
		switch (Type)
		{
		case EInteriorNavNodeType::Elevator: return TEXT("elevator");
		case EInteriorNavNodeType::Stairs: return TEXT("stairs");
		case EInteriorNavNodeType::Escalator: return TEXT("escalator");
		default: return TEXT("connector");
		}
	}

	// Area-weighted centroid ("center of mass") of a simple polygon - the standard formula, correct
	// for any convex OR concave shape, not just a vertex average or bounding-box center (either of
	// which can land badly off-center, or even outside the shape entirely, on an irregular room).
	// Falls back to a plain vertex average only for a degenerate footprint (fewer than 3 points, or
	// zero signed area) where the area formula can't be used at all.
	FVector2D ComputePolygonCentroid(const TArray<FVector2D>& Footprint)
	{
		if (Footprint.Num() == 0)
		{
			return FVector2D::ZeroVector;
		}

		auto VertexAverage = [&Footprint]() -> FVector2D
		{
			FVector2D Sum = FVector2D::ZeroVector;
			for (const FVector2D& Point : Footprint)
			{
				Sum += Point;
			}
			return Sum / Footprint.Num();
		};

		if (Footprint.Num() < 3)
		{
			return VertexAverage();
		}

		double SignedArea = 0.0;
		double Cx = 0.0;
		double Cy = 0.0;
		for (int32 Index = 0; Index < Footprint.Num(); ++Index)
		{
			const FVector2D& P0 = Footprint[Index];
			const FVector2D& P1 = Footprint[(Index + 1) % Footprint.Num()];
			const double Cross = (double)P0.X * P1.Y - (double)P1.X * P0.Y;
			SignedArea += Cross;
			Cx += (P0.X + P1.X) * Cross;
			Cy += (P0.Y + P1.Y) * Cross;
		}
		SignedArea *= 0.5;
		if (FMath::IsNearlyZero(SignedArea))
		{
			return VertexAverage();
		}

		Cx /= (6.0 * SignedArea);
		Cy /= (6.0 * SignedArea);
		return FVector2D((float)Cx, (float)Cy);
	}
}

TSharedRef<SWidget> UNavigationPanelWidget::RebuildWidget()
{
	// No UI of its own any more - UInteriorScreenWidget's own Navigation dock panel is the
	// presentation layer (see RefreshNavigationPanelDisplay). Still needs a root widget since this
	// stays a UUserWidget added to the viewport by ANavPlayerController.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("NavigationCanvas"));
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Canvas;
	}

	return Super::RebuildWidget();
}

FReply UNavigationPanelWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && SelectionMode != ENavigationSelectionMode::None)
	{
		CancelSelection();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UNavigationPanelWidget::ToggleSelectStart()
{
	UE_LOG(LogTemp, Warning, TEXT("[Navigation] Set Start requested; previous mode=%d"),
		static_cast<int32>(SelectionMode));

	if (SelectionMode == ENavigationSelectionMode::Start)
	{
		CancelSelection();
	}
	else
	{
		SetSelectionMode(ENavigationSelectionMode::Start);
	}
}

void UNavigationPanelWidget::ToggleSelectDestination()
{
	UE_LOG(LogTemp, Warning, TEXT("[Navigation] Set Destination requested; previous mode=%d"),
		static_cast<int32>(SelectionMode));

	if (SelectionMode == ENavigationSelectionMode::Destination)
	{
		CancelSelection();
	}
	else
	{
		SetSelectionMode(ENavigationSelectionMode::Destination);
	}
}

void UNavigationPanelWidget::RequestClearRoute()
{
	ClearAll();
	SetStatusMessage(TEXT("Route and selected points cleared."), ENavigationMessageKind::Success);
}

void UNavigationPanelWidget::SetNavigationContext(ABuildingGenerator* InGenerator, AInteriorGenerator* InInterior)
{
	Generator = InGenerator;
	Interior = InInterior;
	UE_LOG(LogTemp, Warning, TEXT("[Navigation] Context set: Generator=%s Interior=%s BuildingId=%s Floor=%d"),
		Generator ? TEXT("valid") : TEXT("NULL"),
		Interior ? TEXT("valid") : TEXT("NULL"),
		Interior ? *Interior->GetLoadedBuildingId() : TEXT("<none>"),
		Interior ? Interior->GetActiveFloor() : INDEX_NONE);
	BroadcastStateChanged();
}

void UNavigationPanelWidget::SelectPoint(FVector2D FootprintPosition, int32 Floor)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[Navigation] SelectPoint called: position=(%.2f, %.2f) floor=%d mode=%d"),
		FootprintPosition.X, FootprintPosition.Y, Floor,
		static_cast<int32>(SelectionMode));

	if (SelectionMode == ENavigationSelectionMode::None)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Navigation] SelectPoint ignored: no selection mode is active"));
		return;
	}
	if (!Generator || !Interior || Interior->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[Navigation] SelectPoint rejected: Generator=%s Interior=%s BuildingId=%s"),
			Generator ? TEXT("valid") : TEXT("NULL"),
			Interior ? TEXT("valid") : TEXT("NULL"),
			Interior ? *Interior->GetLoadedBuildingId() : TEXT("<none>"));
		SetStatusMessage(TEXT("Load a building interior first."), ENavigationMessageKind::Error);
		return;
	}
	APolyBuilding* Building = Generator->FindBuildingById(Interior->GetLoadedBuildingId());
	if (!Building)
	{
		UE_LOG(LogTemp, Error, TEXT("[Navigation] SelectPoint rejected: building '%s' was not found"),
			*Interior->GetLoadedBuildingId());
		SetStatusMessage(TEXT("The selected building is unavailable."), ENavigationMessageKind::Error);
		return;
	}
	FString Error;
	if (!FNavigationRoutePlanner::IsValidPoint(*Building, *Interior, Generator->GetScale(), Floor,
		FootprintPosition, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Navigation] SelectPoint rejected by validation: %s"), *Error);
		SetStatusMessage(Error, ENavigationMessageKind::Error);
		return;
	}

	FSelectedEndpoint& Endpoint = SelectionMode == ENavigationSelectionMode::Start ? Start : Destination;
	Endpoint.bValid = true;
	Endpoint.Point.Position = FootprintPosition;
	Endpoint.Point.Floor = Floor;
	Endpoint.BuildingId = Building->BuildingId;
	Endpoint.bSetViaDropdown = false;
	RoutePoints.Reset();
	bLastFindRouteFailed = false;
	const bool bWasSelectingStart = SelectionMode == ENavigationSelectionMode::Start;
	const FString EndpointName = bWasSelectingStart ? TEXT("Start") : TEXT("Destination");
	const FString AcceptedMessage = bWasSelectingStart
		? FString::Printf(TEXT("Start set on Floor %d."), Floor)
		: FString::Printf(TEXT("Destination set on Floor %d."), Floor);
	SelectionMode = ENavigationSelectionMode::None;
	UE_LOG(LogTemp, Warning, TEXT("[Navigation] Point accepted as %s: position=(%.2f, %.2f), floor=%d"),
		*EndpointName,
		FootprintPosition.X, FootprintPosition.Y, Floor);
	SetStatusMessage(AcceptedMessage, ENavigationMessageKind::Success);
}

TArray<UNavigationPanelWidget::FNavigationRoomOption> UNavigationPanelWidget::GetSelectableRooms() const
{
	TArray<FNavigationRoomOption> Options;
	if (!Generator || !Interior || Interior->GetLoadedBuildingId().IsEmpty())
	{
		return Options;
	}
	APolyBuilding* Building = Generator->FindBuildingById(Interior->GetLoadedBuildingId());
	if (!Building)
	{
		return Options;
	}

	for (int32 Floor = 1; Floor <= Building->GetFloorCount(); ++Floor)
	{
		for (AInteriorRoom* Room : Interior->GetRoomsOnFloor(Floor))
		{
			// Walls/standalone partitions share the same class and id namespace as real rooms (see
			// AInteriorRoom::IsClosedLoop) - only closed-loop ones are actual rooms a person can
			// stand in.
			if (!Room || !Room->IsClosedLoop())
			{
				continue;
			}
			FNavigationRoomOption Option;
			Option.RoomName = Room->RoomName.IsEmpty() ? Room->RoomId : Room->RoomName;
			Option.Floor = Floor;
			Option.Centroid = ComputePolygonCentroid(Room->GetFootprint());
			Options.Add(Option);
		}
	}
	return Options;
}

bool UNavigationPanelWidget::SetStartToRoom(const FString& RoomOptionLabel)
{
	return SetEndpointToRoom(RoomOptionLabel, true);
}

bool UNavigationPanelWidget::SetDestinationToRoom(const FString& RoomOptionLabel)
{
	return SetEndpointToRoom(RoomOptionLabel, false);
}

bool UNavigationPanelWidget::SetEndpointToRoom(const FString& RoomOptionLabel, bool bIsStart)
{
	const TArray<FNavigationRoomOption> Options = GetSelectableRooms();
	const FNavigationRoomOption* Found = Options.FindByPredicate([&RoomOptionLabel](const FNavigationRoomOption& Option)
	{
		return Option.GetDisplayLabel() == RoomOptionLabel;
	});
	if (!Found)
	{
		SetStatusMessage(TEXT("Could not find that room."), ENavigationMessageKind::Error);
		return false;
	}

	APolyBuilding* Building = Generator ? Generator->FindBuildingById(Interior->GetLoadedBuildingId()) : nullptr;
	FString Error;
	if (!Building || !FNavigationRoutePlanner::IsValidPoint(*Building, *Interior, Generator->GetScale(),
		Found->Floor, Found->Centroid, Error))
	{
		SetStatusMessage(FString::Printf(TEXT("'%s''s center isn't reachable (%s) - try clicking a point inside it instead."),
			*Found->RoomName, *Error), ENavigationMessageKind::Error);
		return false;
	}

	// A dropdown pick is a direct, immediate set - cancel any in-progress click-to-pick rather than
	// leaving it dangling.
	SelectionMode = ENavigationSelectionMode::None;

	FSelectedEndpoint& Endpoint = bIsStart ? Start : Destination;
	Endpoint.bValid = true;
	Endpoint.Point.Position = Found->Centroid;
	Endpoint.Point.Floor = Found->Floor;
	Endpoint.BuildingId = Building->BuildingId;
	Endpoint.bSetViaDropdown = true;
	RoutePoints.Reset();
	bLastFindRouteFailed = false;
	SetStatusMessage(FString::Printf(TEXT("%s set to '%s'."), bIsStart ? TEXT("Start") : TEXT("Destination"), *Found->RoomName),
		ENavigationMessageKind::Success);
	return true;
}

void UNavigationPanelWidget::RequestFindRoute()
{
	if (!Generator || !Interior || !Start.bValid || !Destination.bValid
		|| Start.BuildingId != Destination.BuildingId
		|| Start.BuildingId != Interior->GetLoadedBuildingId())
	{
		SetStatusMessage(TEXT("Set a start and destination in the loaded building."),
			ENavigationMessageKind::Error);
		return;
	}
	APolyBuilding* Building = Generator->FindBuildingById(Start.BuildingId);
	if (!Building)
	{
		SetStatusMessage(TEXT("The selected building is unavailable."), ENavigationMessageKind::Error);
		return;
	}

	FNavigationRouteResult Result;
	if (!FNavigationRoutePlanner::FindRoute(*Building, *Interior, Generator->GetScale(),
		Start.Point.Floor, Start.Point.Position, Destination.Point.Floor,
		Destination.Point.Position, Result))
	{
		RoutePoints.Reset();
		bLastFindRouteFailed = true;
		SetStatusMessage(Result.Error, ENavigationMessageKind::Warning);
		return;
	}

	RoutePoints = MoveTemp(Result.Points);
	bLastFindRouteFailed = false;
	SetStatusMessage(FString::Printf(TEXT("Route found: %d points."), RoutePoints.Num()),
		ENavigationMessageKind::Success);
}

void UNavigationPanelWidget::ClearAll()
{
	SelectionMode = ENavigationSelectionMode::None;
	Start = FSelectedEndpoint();
	Destination = FSelectedEndpoint();
	RoutePoints.Reset();
	bLastFindRouteFailed = false;
	StatusMessage.Empty();
	StatusKind = ENavigationMessageKind::Instruction;
	BroadcastStateChanged();
}

void UNavigationPanelWidget::CancelSelection()
{
	if (SelectionMode != ENavigationSelectionMode::None)
	{
		SelectionMode = ENavigationSelectionMode::None;
		SetStatusMessage(TEXT("Selection cancelled."), ENavigationMessageKind::Warning);
	}
}

void UNavigationPanelWidget::SetSelectionMode(ENavigationSelectionMode NewMode)
{
	SelectionMode = NewMode;
	StatusMessage.Empty();
	StatusKind = ENavigationMessageKind::Instruction;
	BroadcastStateChanged();
}

void UNavigationPanelWidget::SetStatusMessage(const FString& Message, ENavigationMessageKind Kind)
{
	StatusMessage = Message;
	StatusKind = Kind;
	BroadcastStateChanged();
}

void UNavigationPanelWidget::BroadcastStateChanged()
{
	OnStateChanged.Broadcast();
}

bool UNavigationPanelWidget::CanFindRoute() const
{
	return Start.bValid && Destination.bValid && Interior
		&& Start.BuildingId == Destination.BuildingId
		&& Start.BuildingId == Interior->GetLoadedBuildingId();
}

bool UNavigationPanelWidget::CanClearRoute() const
{
	return IsSelectingPoint() || Start.bValid || Destination.bValid || !RoutePoints.IsEmpty();
}

FString UNavigationPanelWidget::GetDisplayStatusMessage() const
{
	if (!StatusMessage.IsEmpty())
	{
		return StatusMessage;
	}
	switch (SelectionMode)
	{
	case ENavigationSelectionMode::Start:
		return TEXT("Select a start on the current floor.");
	case ENavigationSelectionMode::Destination:
		return TEXT("Select a destination on the current floor.");
	default:
		return TEXT("Choose Set Start or Set Destination.");
	}
}

FString UNavigationPanelWidget::BuildRouteDetails() const
{
	FString Details;
	if (Start.bValid)
	{
		Details += FString::Printf(TEXT("Start: Floor %d\n"), Start.Point.Floor);
	}
	if (Destination.bValid)
	{
		Details += FString::Printf(TEXT("Destination: Floor %d\n"), Destination.Point.Floor);
	}

	const int32 ActiveFloor = Interior ? Interior->GetActiveFloor() : INDEX_NONE;
	int32 NextFloor = INDEX_NONE;
	int32 PreviousFloor = INDEX_NONE;
	for (int32 I = 1; I < RoutePoints.Num(); ++I)
	{
		const FNavigationRoutePoint& From = RoutePoints[I - 1];
		const FNavigationRoutePoint& To = RoutePoints[I];
		if (From.Floor == To.Floor)
		{
			continue;
		}
		const FString Connector = ConnectorTypeName(To.ConnectorType);
		const FString ConnectorLabel = To.ConnectorId.IsEmpty()
			? Connector : Connector + TEXT(" ") + To.ConnectorId;
		Details += FString::Printf(TEXT("Take %s from Floor %d to Floor %d.\n"),
			*ConnectorLabel, From.Floor, To.Floor);
		if (From.Floor == ActiveFloor && NextFloor == INDEX_NONE)
		{
			NextFloor = To.Floor;
		}
		if (To.Floor == ActiveFloor)
		{
			PreviousFloor = From.Floor;
		}
	}
	if (NextFloor != INDEX_NONE)
	{
		Details += FString::Printf(TEXT("Route continues to Floor %d."), NextFloor);
	}
	else if (PreviousFloor != INDEX_NONE)
	{
		Details += FString::Printf(TEXT("Route arrives from Floor %d."), PreviousFloor);
	}
	Details.RemoveFromEnd(TEXT("\n"));
	return Details;
}

FLinearColor UNavigationPanelWidget::GetStatusColor() const
{
	if (StatusMessage.IsEmpty())
	{
		return FLinearColor(0.55f, 0.82f, 1.f, 1.f);
	}
	switch (StatusKind)
	{
	case ENavigationMessageKind::Success: return FLinearColor(0.3f, 1.f, 0.45f, 1.f);
	case ENavigationMessageKind::Error: return FLinearColor(1.f, 0.3f, 0.3f, 1.f);
	case ENavigationMessageKind::Warning: return FLinearColor(1.f, 0.65f, 0.15f, 1.f);
	case ENavigationMessageKind::Route: return FLinearColor(0.45f, 0.92f, 1.f, 1.f);
	default: return FLinearColor(0.55f, 0.82f, 1.f, 1.f);
	}
}

bool UNavigationPanelWidget::IsPointerOverPanel() const
{
	// The full-screen user widget is SelfHitTestInvisible so WBP_InteriorScreen remains
	// interactive underneath it. Check general visibility rather than requiring the
	// exact Visible enum value; only HitTestBorder (UInteriorScreenWidget's own Navigation
	// dock panel Border - see SetPanelBorderForHitTest) should own pointer input.
	return IsVisible() && HitTestBorder && FSlateApplication::IsInitialized()
		&& HitTestBorder->GetCachedGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos());
}
