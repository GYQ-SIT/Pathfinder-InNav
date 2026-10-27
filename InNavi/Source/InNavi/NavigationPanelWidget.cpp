#include "NavigationPanelWidget.h"

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "NavigationSearchUtils.h"
#include "PolyBuilding.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"

namespace
{
	constexpr double PointTestEpsilon = 0.0001;

	double Cross2D(const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		return static_cast<double>(B.X - A.X) * (C.Y - A.Y)
			- static_cast<double>(B.Y - A.Y) * (C.X - A.X);
	}

	bool IsOnSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		return FMath::Abs(Cross2D(A, B, Point)) <= PointTestEpsilon
			&& Point.X >= FMath::Min(A.X, B.X) - PointTestEpsilon
			&& Point.X <= FMath::Max(A.X, B.X) + PointTestEpsilon
			&& Point.Y >= FMath::Min(A.Y, B.Y) - PointTestEpsilon
			&& Point.Y <= FMath::Max(A.Y, B.Y) + PointTestEpsilon;
	}

	bool IsInsideOrOnFootprint(const FVector2D& Point, const TArray<FVector2D>& Footprint)
	{
		if (Footprint.Num() < 3)
		{
			return false;
		}

		bool bInside = false;
		for (int32 I = 0, J = Footprint.Num() - 1; I < Footprint.Num(); J = I++)
		{
			const FVector2D& A = Footprint[J];
			const FVector2D& B = Footprint[I];
			if (IsOnSegment(Point, A, B))
			{
				return true;
			}
			if ((A.Y > Point.Y) != (B.Y > Point.Y)
				&& Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	FString LocationName(const FNavigationRoutePoint& Point)
	{
		return Point.Floor == UNavigationPanelWidget::OutdoorFloor
			? TEXT("Outside")
			: FString::Printf(TEXT("Floor %d (indoors)"), Point.Floor);
	}

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

FBox2D UNavigationPanelWidget::GetOutdoorBounds(const APolyBuilding& Building)
{
	FBox2D Bounds(EForceInit::ForceInit);
	for (const FVector2D& Point : Building.GetFootprint())
	{
		Bounds += Point;
	}
	if (!Bounds.bIsValid)
	{
		return Bounds;
	}

	// Keep enough campus space around small and large buildings without making a large building
	// tiny in the floor-plan view.
	const FVector2D Size = Bounds.GetSize();
	const float Padding = FMath::Max(40.f, FMath::Max(Size.X, Size.Y) * 0.25f);
	return Bounds.ExpandBy(Padding);
}

bool UNavigationPanelWidget::IsOutdoorPoint(const APolyBuilding& Building, FVector2D Point)
{
	const FBox2D OutdoorBounds = GetOutdoorBounds(Building);
	return OutdoorBounds.bIsValid && OutdoorBounds.IsInsideOrOn(Point)
		&& !IsInsideOrOnFootprint(Point, Building.GetFootprint());
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

void UNavigationPanelWidget::SetRouteGuidanceVisible(bool bVisible)
{
	if (bRouteGuidanceVisible != bVisible)
	{
		bRouteGuidanceVisible = bVisible;
		BroadcastStateChanged();
	}
}

void UNavigationPanelWidget::ToggleRouteGuidance()
{
	if (CanToggleRouteGuidance())
	{
		SetRouteGuidanceVisible(!bRouteGuidanceVisible);
	}
}

void UNavigationPanelWidget::SetNavigationContext(ABuildingGenerator* InGenerator, AInteriorGenerator* InInterior)
{
	Generator = InGenerator;
	Interior = InInterior;
	IndexedRoomSearchRevision = MAX_uint64;
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
	const bool bInsideBuilding = IsInsideOrOnFootprint(FootprintPosition, Building->GetFootprint());
	int32 SelectedFloor = Floor;
	FString Error;
	if (bInsideBuilding)
	{
		if (!FNavigationRoutePlanner::IsValidPoint(*Building, *Interior, Generator->GetScale(), Floor,
			FootprintPosition, Error))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Navigation] Indoor point rejected by validation: %s"), *Error);
			SetStatusMessage(FString::Printf(TEXT("Invalid indoor area: %s"), *Error), ENavigationMessageKind::Error);
			return;
		}
	}
	else if (Floor == 1 && IsOutdoorPoint(*Building, FootprintPosition))
	{
		SelectedFloor = OutdoorFloor;
	}
	else
	{
		const FString Message = Floor == 1
			? TEXT("Invalid area. Select inside the building or within the highlighted outside area.")
			: TEXT("Invalid area. Outdoor points can only be selected from the Floor 1 map.");
		UE_LOG(LogTemp, Warning, TEXT("[Navigation] Point rejected: %s"), *Message);
		SetStatusMessage(Message, ENavigationMessageKind::Error);
		return;
	}

	FSelectedEndpoint& Endpoint = SelectionMode == ENavigationSelectionMode::Start ? Start : Destination;
	const bool bWasSelectingStart = SelectionMode == ENavigationSelectionMode::Start;
	const FString EndpointName = bWasSelectingStart ? TEXT("Start") : TEXT("Destination");
	FNavigationRoutePoint SelectedPoint;
	SelectedPoint.Position = FootprintPosition;
	SelectedPoint.Floor = SelectedFloor;
	SelectionMode = ENavigationSelectionMode::None;
	SetEndpoint(Endpoint, SelectedPoint, Building->BuildingId, EndpointName,
		SelectedFloor == OutdoorFloor ? TEXT("outside") : FString::Printf(TEXT("indoors on Floor %d"), SelectedFloor),
		false);
	UE_LOG(LogTemp, Warning, TEXT("[Navigation] Point accepted as %s: position=(%.2f, %.2f), floor=%d"),
		*EndpointName,
		FootprintPosition.X, FootprintPosition.Y, SelectedFloor);
}

void UNavigationPanelWidget::SetEndpoint(FSelectedEndpoint& Endpoint, const FNavigationRoutePoint& Point,
	const FString& BuildingId, const FString& EndpointName, const FString& LocationDescription,
	bool bSetViaDropdown, const FString& RoomOptionLabel, const FString& RoomId)
{
	const bool bRouteWasVisible = !RoutePoints.IsEmpty();
	Endpoint.bValid = true;
	Endpoint.Point = Point;
	Endpoint.BuildingId = BuildingId;
	Endpoint.bSetViaDropdown = bSetViaDropdown;
	Endpoint.RoomOptionLabel = RoomOptionLabel;
	Endpoint.RoomId = RoomId;
	RoutePoints.Reset();
	RouteLegs.Reset();
	bRouteGuidanceVisible = true;
	bLastFindRouteFailed = false;
	const FString Message = bRouteWasVisible
		? FString::Printf(TEXT("%s changed to %s. Route cleared; press Find Route again."),
			*EndpointName, *LocationDescription)
		: FString::Printf(TEXT("%s set %s."), *EndpointName, *LocationDescription);
	SetStatusMessage(Message, bRouteWasVisible ? ENavigationMessageKind::Warning : ENavigationMessageKind::Success);
}

void UNavigationPanelWidget::RefreshRoomSearchIndex() const
{
	APolyBuilding* Building = Generator && Interior
		? Generator->FindBuildingById(Interior->GetLoadedBuildingId()) : nullptr;
	const uint64 Revision = Interior ? Interior->GetRoomSearchRevision() : 0;
	const int32 FloorCount = IsValid(Building) ? Building->GetFloorCount() : 0;
	if (IndexedRoomSearchRevision == Revision && IndexedBuilding.Get() == Building && IndexedFloorCount == FloorCount)
	{
		return;
	}
	IndexedRoomSearchRevision = Revision;
	IndexedBuilding = Building;
	IndexedFloorCount = FloorCount;
	CachedRooms.Reset();
	SortedDestinationIndices.Reset();
	if (!IsValid(Building))
	{
		return;
	}

	for (int32 Floor = 1; Floor <= FloorCount; ++Floor)
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
			Option.RoomId = Room->RoomId;
			Option.Tags = Room->RoomTags;
			Option.RoomName = Room->RoomName.IsEmpty() ? Room->RoomId : Room->RoomName;
			Option.Floor = Floor;
			Option.Centroid = ComputePolygonCentroid(Room->GetFootprint());
			Option.DisplayLabel = FString::Printf(TEXT("%s [%s] (Floor %d)"), *Option.RoomName, *Option.RoomId, Floor);
			Option.SearchableText = Option.DisplayLabel + TEXT(" ") + FString::Join(Option.Tags, TEXT(" "));
			SortedDestinationIndices.Add(CachedRooms.Num());
			CachedRooms.Add(MoveTemp(Option));
		}
	}
	// Keep Start's existing floor order; destinations are sorted once, then filtered in that order.
	SortedDestinationIndices.Sort([this](int32 A, int32 B)
		{
			return CachedRooms[A].DisplayLabel.Compare(CachedRooms[B].DisplayLabel, ESearchCase::IgnoreCase) < 0;
		});
}

const TArray<UNavigationPanelWidget::FNavigationRoomOption>& UNavigationPanelWidget::GetSelectableRooms() const
{
	RefreshRoomSearchIndex();
	return CachedRooms;
}

TArray<UNavigationPanelWidget::FNavigationRoomOption> UNavigationPanelWidget::SearchDestinations(const FString& Query) const
{
	RefreshRoomSearchIndex();
	TArray<FString> Words;
	Query.ParseIntoArrayWS(Words);
	TArray<FNavigationRoomOption> Options;
	for (int32 Index : SortedDestinationIndices)
	{
		const FNavigationRoomOption& Option = CachedRooms[Index];
		if (NavigationSearch::MatchesWords(Words, Option.SearchableText)) Options.Add(Option);
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

bool UNavigationPanelWidget::AddStopToRoom(const FString& RoomOptionLabel)
{
	if (Stops.Num() >= MaxRouteStops)
	{
		SetStatusMessage(FString::Printf(TEXT("A route can contain up to %d intermediate stops."), MaxRouteStops),
			ENavigationMessageKind::Warning);
		return false;
	}
	if (!Generator || !Interior || Interior->GetLoadedBuildingId().IsEmpty())
	{
		SetStatusMessage(TEXT("Load a building before adding a route stop."), ENavigationMessageKind::Error);
		return false;
	}

	const TArray<FNavigationRoomOption>& Options = GetSelectableRooms();
	const FNavigationRoomOption* Found = Options.FindByPredicate([&RoomOptionLabel](const FNavigationRoomOption& Option)
		{
			return Option.GetDisplayLabel() == RoomOptionLabel;
		});
	APolyBuilding* Building = Generator->FindBuildingById(Interior->GetLoadedBuildingId());
	if (!Found || !Building)
	{
		SetStatusMessage(TEXT("Could not find that room to add as a stop."), ENavigationMessageKind::Error);
		return false;
	}

	FString Error;
	if (!FNavigationRoutePlanner::IsValidPoint(*Building, *Interior, Generator->GetScale(),
		Found->Floor, Found->Centroid, Error))
	{
		SetStatusMessage(FString::Printf(TEXT("'%s''s center isn't reachable (%s) - try clicking a point inside it instead."),
			*Found->RoomName, *Error), ENavigationMessageKind::Error);
		return false;
	}

	FNavigationStop& Stop = Stops.AddDefaulted_GetRef();
	Stop.Point.Position = Found->Centroid;
	Stop.Point.Floor = Found->Floor;
	Stop.BuildingId = Building->BuildingId;
	Stop.RoomOptionLabel = RoomOptionLabel;
	Stop.RoomId = Found->RoomId;
	SelectionMode = ENavigationSelectionMode::None;
	RoutePoints.Reset();
	RouteLegs.Reset();
	bLastFindRouteFailed = false;
	SetStatusMessage(FString::Printf(TEXT("Added stop %d of %d: %s."),
		Stops.Num(), MaxRouteStops, *RoomOptionLabel), ENavigationMessageKind::Success);
	return true;
}

bool UNavigationPanelWidget::RemoveStop(int32 StopIndex)
{
	if (!Stops.IsValidIndex(StopIndex)) return false;
	const FString RemovedLabel = Stops[StopIndex].RoomOptionLabel;
	Stops.RemoveAt(StopIndex);
	RoutePoints.Reset();
	RouteLegs.Reset();
	bLastFindRouteFailed = false;
	SetStatusMessage(FString::Printf(TEXT("Removed stop: %s. Press Find Route to recalculate."), *RemovedLabel),
		ENavigationMessageKind::Warning);
	return true;
}

bool UNavigationPanelWidget::MoveStop(int32 FromIndex, int32 ToIndex)
{
	if (!Stops.IsValidIndex(FromIndex) || !Stops.IsValidIndex(ToIndex) || FromIndex == ToIndex) return false;
	FNavigationStop MovedStop = MoveTemp(Stops[FromIndex]);
	Stops.RemoveAt(FromIndex);
	Stops.Insert(MoveTemp(MovedStop), ToIndex);
	RoutePoints.Reset();
	RouteLegs.Reset();
	bLastFindRouteFailed = false;
	SetStatusMessage(TEXT("Stop order changed. Press Find Route to recalculate."), ENavigationMessageKind::Warning);
	return true;
}

void UNavigationPanelWidget::ClearStops()
{
	if (Stops.IsEmpty()) return;
	Stops.Reset();
	RoutePoints.Reset();
	RouteLegs.Reset();
	bLastFindRouteFailed = false;
	SetStatusMessage(TEXT("Intermediate stops cleared. Press Find Route to recalculate."),
		ENavigationMessageKind::Warning);
}

bool UNavigationPanelWidget::SetEndpointToRoom(const FString& RoomOptionLabel, bool bIsStart)
{
	const TArray<FNavigationRoomOption>& Options = GetSelectableRooms();
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
	FNavigationRoutePoint SelectedPoint;
	SelectedPoint.Position = Found->Centroid;
	SelectedPoint.Floor = Found->Floor;
	SetEndpoint(Endpoint, SelectedPoint, Building->BuildingId,
		bIsStart ? TEXT("Start") : TEXT("Destination"),
		FString::Printf(TEXT("to '%s' indoors on Floor %d"), *Found->RoomName, Found->Floor),
		true, RoomOptionLabel, Found->RoomId);
	return true;
}

void UNavigationPanelWidget::RequestFindRoute()
{
	if (!Generator || !Interior || !Start.bValid || !Destination.bValid
		|| Start.BuildingId != Destination.BuildingId
		|| Start.BuildingId != Interior->GetLoadedBuildingId())
	{
		RoutePoints.Reset();
		RouteLegs.Reset();
		bLastFindRouteFailed = true;
		SetStatusMessage(TEXT("Set a start and destination in the loaded building."),
			ENavigationMessageKind::Error);
		return;
	}
	APolyBuilding* Building = Generator->FindBuildingById(Start.BuildingId);
	if (!Building)
	{
		RoutePoints.Reset();
		RouteLegs.Reset();
		bLastFindRouteFailed = true;
		SetStatusMessage(TEXT("The selected building is unavailable."), ENavigationMessageKind::Error);
		return;
	}

	for (const FNavigationStop& Stop : Stops)
	{
		if (Stop.BuildingId != Start.BuildingId)
		{
			RoutePoints.Reset();
			RouteLegs.Reset();
			bLastFindRouteFailed = true;
			SetStatusMessage(TEXT("A route stop belongs to a different building. Remove it and add it again."),
				ENavigationMessageKind::Error);
			return;
		}
	}

	TArray<FNavigationRoutePoint> Waypoints;
	Waypoints.Reserve(Stops.Num() + 2);
	Waypoints.Add(Start.Point);
	for (const FNavigationStop& Stop : Stops) Waypoints.Add(Stop.Point);
	Waypoints.Add(Destination.Point);

	TArray<FNavigationRoutePoint> CombinedPoints;
	TArray<FNavigationRouteLeg> PlannedLegs;
	for (int32 LegIndex = 0; LegIndex + 1 < Waypoints.Num(); ++LegIndex)
	{
		FNavigationRouteResult LegResult;
		if (!FNavigationRoutePlanner::FindRoute(*Building, *Interior, Generator->GetScale(),
			Waypoints[LegIndex].Floor, Waypoints[LegIndex].Position,
			Waypoints[LegIndex + 1].Floor, Waypoints[LegIndex + 1].Position, LegResult)
			|| LegResult.Points.IsEmpty())
		{
			RoutePoints.Reset();
			RouteLegs.Reset();
			bLastFindRouteFailed = true;
			const FString FromLabel = LegIndex == 0
				? (Start.bSetViaDropdown ? Start.RoomOptionLabel : TEXT("Start"))
				: Stops[LegIndex - 1].RoomOptionLabel;
			const FString ToLabel = LegIndex < Stops.Num()
				? Stops[LegIndex].RoomOptionLabel
				: (Destination.bSetViaDropdown ? Destination.RoomOptionLabel : TEXT("Destination"));
			const FString PlannerError = LegResult.Error.IsEmpty()
				? TEXT("No route found. Check that a Floor 1 door is marked as Outside.")
				: LegResult.Error;
			SetStatusMessage(FString::Printf(TEXT("Unable to route leg %d (%s -> %s): %s"),
				LegIndex + 1, *FromLabel, *ToLabel, *PlannerError), ENavigationMessageKind::Warning);
			return;
		}

		FNavigationRouteLeg Leg;
		Leg.FromLabel = LegIndex == 0
			? (Start.bSetViaDropdown ? Start.RoomOptionLabel : TEXT("Start"))
			: Stops[LegIndex - 1].RoomOptionLabel;
		Leg.ToLabel = LegIndex < Stops.Num()
			? Stops[LegIndex].RoomOptionLabel
			: (Destination.bSetViaDropdown ? Destination.RoomOptionLabel : TEXT("Destination"));
		int32 FirstPointToAppend = 0;
		if (!CombinedPoints.IsEmpty() && !LegResult.Points.IsEmpty())
		{
			const FNavigationRoutePoint& Previous = CombinedPoints.Last();
			const FNavigationRoutePoint& Current = LegResult.Points[0];
			if (Previous.Floor == Current.Floor && Previous.Position.Equals(Current.Position, KINDA_SMALL_NUMBER))
			{
				FirstPointToAppend = 1;
			}
		}
		Leg.FirstPointIndex = CombinedPoints.Num() - (FirstPointToAppend == 1 ? 1 : 0);
		for (int32 PointIndex = FirstPointToAppend; PointIndex < LegResult.Points.Num(); ++PointIndex)
		{
			CombinedPoints.Add(LegResult.Points[PointIndex]);
		}
		Leg.LastPointIndex = CombinedPoints.Num() - 1;
		if (Leg.FirstPointIndex > Leg.LastPointIndex) Leg.FirstPointIndex = Leg.LastPointIndex;
		PlannedLegs.Add(MoveTemp(Leg));
	}

	RoutePoints = MoveTemp(CombinedPoints);
	RouteLegs = MoveTemp(PlannedLegs);
	bRouteGuidanceVisible = true;
	bLastFindRouteFailed = false;
	const FString SuccessMessage = Stops.IsEmpty()
		? FString::Printf(TEXT("Route found: %d points."), RoutePoints.Num())
		: FString::Printf(TEXT("Route found through %d intermediate stop%s (%d points)."),
			Stops.Num(), Stops.Num() == 1 ? TEXT("") : TEXT("s"), RoutePoints.Num());
	SetStatusMessage(SuccessMessage, ENavigationMessageKind::Success);
}

void UNavigationPanelWidget::ApplyRouteResult(const FNavigationRouteResult& Result)
{
	if (Result.Points.IsEmpty())
	{
		RoutePoints.Reset();
		RouteLegs.Reset();
		bLastFindRouteFailed = true;
		SetStatusMessage(Result.Error.IsEmpty() ? TEXT("No route was returned.") : Result.Error,
			ENavigationMessageKind::Warning);
		return;
	}

	RoutePoints = Result.Points;
	RouteLegs.Reset();
	if (RoutePoints.Num() >= 2)
	{
		FNavigationRouteLeg Leg;
		Leg.FirstPointIndex = 0;
		Leg.LastPointIndex = RoutePoints.Num() - 1;
		Leg.FromLabel = TEXT("Start");
		Leg.ToLabel = TEXT("Destination");
		RouteLegs.Add(MoveTemp(Leg));
	}
	bRouteGuidanceVisible = true;
	bLastFindRouteFailed = false;
	SetStatusMessage(FString::Printf(TEXT("Route found: %d points."), RoutePoints.Num()),
		ENavigationMessageKind::Success);
}

bool UNavigationPanelWidget::ApplyWorldRouteResult(const FString& BuildingId,
	const TArray<FNavigationWorldRoutePoint>& Points)
{
	APolyBuilding* Building = Generator ? Generator->FindBuildingById(BuildingId) : nullptr;
	if (!Interior || Interior->GetLoadedBuildingId() != BuildingId || !Building
		|| Generator->GetScale() <= KINDA_SMALL_NUMBER || Points.Num() < 2)
	{
		SetStatusMessage(TEXT("A 3D route needs the loaded building and at least two points."), ENavigationMessageKind::Error);
		return false;
	}

	FNavigationRouteResult Result;
	for (const FNavigationWorldRoutePoint& Input : Points)
	{
		if (!FMath::IsFinite(Input.WorldPosition.X) || !FMath::IsFinite(Input.WorldPosition.Y)
			|| !FMath::IsFinite(Input.WorldPosition.Z) || Input.Floor < OutdoorFloor
			|| Input.Floor > Building->GetFloorCount())
		{
			SetStatusMessage(TEXT("The 3D route contains an invalid position or floor."), ENavigationMessageKind::Error);
			return false;
		}
		FNavigationRoutePoint Point;
		Point.Position = FVector2D(Input.WorldPosition.X, Input.WorldPosition.Y) / Generator->GetScale()
			- Building->BuildingOffset;
		Point.Floor = Input.Floor;
		Point.ConnectorId = Input.ConnectorId;
		Point.ConnectorType = Input.ConnectorType;
		Point.WorldPosition = Input.WorldPosition;
		Point.bHasWorldPosition = true;
		Result.Points.Add(Point);
	}
	Start = FSelectedEndpoint();
	Start.bValid = true;
	Start.BuildingId = BuildingId;
	Start.Point = Result.Points[0];
	Destination = FSelectedEndpoint();
	Destination.bValid = true;
	Destination.BuildingId = BuildingId;
	Destination.Point = Result.Points.Last();
	Stops.Reset();
	SelectionMode = ENavigationSelectionMode::None;
	ApplyRouteResult(Result);
	return true;
}

void UNavigationPanelWidget::ClearAll()
{
	bRouteGuidanceVisible = true;
	SelectionMode = ENavigationSelectionMode::None;
	Start = FSelectedEndpoint();
	Destination = FSelectedEndpoint();
	Stops.Reset();
	RoutePoints.Reset();
	RouteLegs.Reset();
	bLastFindRouteFailed = false;
	StatusMessage.Empty();
	StatusKind = ENavigationMessageKind::Instruction;
	BroadcastStateChanged();
}

void UNavigationPanelWidget::NotifyRoomDeleted(const FString& RoomId)
{
	bool bChanged = false;
	if (Start.bValid && Start.bSetViaDropdown && Start.RoomId == RoomId)
	{
		Start = FSelectedEndpoint();
		bChanged = true;
	}
	if (Destination.bValid && Destination.bSetViaDropdown && Destination.RoomId == RoomId)
	{
		Destination = FSelectedEndpoint();
		bChanged = true;
	}
	for (int32 StopIndex = Stops.Num() - 1; StopIndex >= 0; --StopIndex)
	{
		if (Stops[StopIndex].RoomId == RoomId)
		{
			Stops.RemoveAt(StopIndex);
			bChanged = true;
		}
	}
	if (bChanged)
	{
		RoutePoints.Reset();
		RouteLegs.Reset();
		bLastFindRouteFailed = false;
		SetStatusMessage(TEXT("A selected endpoint or stop was cleared because its room was deleted."), ENavigationMessageKind::Warning);
	}
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
	return IsSelectingPoint() || Start.bValid || Destination.bValid || !Stops.IsEmpty() || !RoutePoints.IsEmpty();
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
		return TEXT("Select a start indoors, or in the blue outside area on Floor 1.");
	case ENavigationSelectionMode::Destination:
		return TEXT("Select a destination indoors, or in the blue outside area on Floor 1.");
	default:
		return TEXT("Choose Set Start or Set Destination.");
	}
}

FString UNavigationPanelWidget::BuildRouteDetails() const
{
	FString Details;
	if (Start.bValid)
	{
		const FString StartLabel = Start.bSetViaDropdown ? Start.RoomOptionLabel : LocationName(Start.Point);
		Details += FString::Printf(TEXT("Start: %s\n"), *StartLabel);
	}
	for (int32 StopIndex = 0; StopIndex < Stops.Num(); ++StopIndex)
	{
		Details += FString::Printf(TEXT("Stop %d: %s\n"), StopIndex + 1, *Stops[StopIndex].RoomOptionLabel);
	}
	if (Destination.bValid)
	{
		const FString DestinationLabel = Destination.bSetViaDropdown
			? Destination.RoomOptionLabel : LocationName(Destination.Point);
		Details += FString::Printf(TEXT("Destination: %s\n"), *DestinationLabel);
	}

	const int32 ActiveFloor = Interior ? Interior->GetActiveFloor() : INDEX_NONE;
	int32 NextFloor = INDEX_NONE;
	int32 PreviousFloor = INDEX_NONE;
	const int32 LegCount = RouteLegs.IsEmpty() ? (RoutePoints.Num() > 1 ? 1 : 0) : RouteLegs.Num();
	for (int32 LegIndex = 0; LegIndex < LegCount; ++LegIndex)
	{
		const int32 FirstPoint = RouteLegs.IsEmpty() ? 0 : RouteLegs[LegIndex].FirstPointIndex;
		const int32 LastPoint = RouteLegs.IsEmpty() ? RoutePoints.Num() - 1 : RouteLegs[LegIndex].LastPointIndex;
		if (!RoutePoints.IsValidIndex(FirstPoint) || !RoutePoints.IsValidIndex(LastPoint)) continue;
		const FString FromLabel = RouteLegs.IsEmpty() ? TEXT("Start") : RouteLegs[LegIndex].FromLabel;
		const FString ToLabel = RouteLegs.IsEmpty() ? TEXT("Destination") : RouteLegs[LegIndex].ToLabel;
		Details += FString::Printf(TEXT("Leg %d: %s -> %s\n"), LegIndex + 1, *FromLabel, *ToLabel);
		for (int32 I = FMath::Max(FirstPoint + 1, 1); I <= LastPoint; ++I)
		{
			const FNavigationRoutePoint& From = RoutePoints[I - 1];
			const FNavigationRoutePoint& To = RoutePoints[I];
			if (From.Floor == To.Floor) continue;
			if (From.Floor == OutdoorFloor)
			{
				Details += TEXT("  Enter the building, then continue on Floor 1.\n");
			}
			else if (To.Floor == OutdoorFloor)
			{
				Details += TEXT("  Exit the building, then continue outside.\n");
			}
			else
			{
				const FString Connector = ConnectorTypeName(To.ConnectorType);
				const FString ConnectorLabel = To.ConnectorId.IsEmpty()
					? Connector : Connector + TEXT(" ") + To.ConnectorId;
				Details += FString::Printf(TEXT("  Take %s from Floor %d to Floor %d.\n"),
					*ConnectorLabel, From.Floor, To.Floor);
			}
			if (From.Floor == ActiveFloor && NextFloor == INDEX_NONE)
			{
				NextFloor = To.Floor;
			}
			if (To.Floor == ActiveFloor)
			{
				PreviousFloor = From.Floor;
			}
		}
	}
	if (NextFloor != INDEX_NONE)
	{
		Details += NextFloor == OutdoorFloor
			? TEXT("Route continues outside.")
			: FString::Printf(TEXT("Route continues to Floor %d."), NextFloor);
	}
	else if (PreviousFloor != INDEX_NONE)
	{
		Details += PreviousFloor == OutdoorFloor
			? TEXT("Route arrives from outside.")
			: FString::Printf(TEXT("Route arrives from Floor %d."), PreviousFloor);
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
	if (!IsVisible() || !FSlateApplication::IsInitialized()) return false;
	const FVector2D PointerPosition = FSlateApplication::Get().GetCursorPos();
	auto IsOverVisibleWidget = [&PointerPosition](const UWidget* Widget)
		{
			return Widget && Widget->IsVisible() && Widget->GetCachedGeometry().IsUnderLocation(PointerPosition);
		};
	// Hidden docks retain cached geometry. They must not block selection at their former location.
	return IsOverVisibleWidget(HitTestBorder) || IsOverVisibleWidget(HitTestToggle);
}
