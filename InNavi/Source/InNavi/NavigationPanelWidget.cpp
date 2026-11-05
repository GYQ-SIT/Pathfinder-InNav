#include "NavigationPanelWidget.h"
#include "NavigationInstructions.h"

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "NavigationSearchUtils.h"
#include "Algo/Reverse.h"
#include "RoomTypes.h"
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
	// No UI of its own any more - the Navigation dock panel (UNavigationDockPanelWidget) is the
	// presentation layer (see its RefreshDisplay). Still needs a root widget since this
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

void UNavigationPanelWidget::ToggleSelectStops()
{
	if (SelectionMode == ENavigationSelectionMode::Stop)
	{
		SelectionMode = ENavigationSelectionMode::None;
		SetStatusMessage(FString::Printf(TEXT("Stops: %d. Press Find Route to search."), Stops.Num()), ENavigationMessageKind::Success);
	}
	else if (Stops.Num() >= MaxRouteStops)
	{
		SetStatusMessage(FString::Printf(TEXT("A route can have up to %d stops."), MaxRouteStops), ENavigationMessageKind::Warning);
	}
	else
	{
		SetSelectionMode(ENavigationSelectionMode::Stop);
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
	if (Generator != InGenerator || Interior != InInterior)
	{
		ClearAll();
	}
	if (Generator) Generator->OnNavigationLayoutChanged.RemoveAll(this);
	if (Interior) Interior->OnNavigationLayoutChanged.RemoveAll(this);
	Generator = InGenerator;
	Interior = InInterior;
	if (Generator) Generator->OnNavigationLayoutChanged.AddUObject(this, &UNavigationPanelWidget::HandleLayoutChanged);
	if (Interior) Interior->OnNavigationLayoutChanged.AddUObject(this, &UNavigationPanelWidget::HandleLayoutChanged);
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

	if (SelectionMode == ENavigationSelectionMode::Stop)
	{
		// The mode stays on, for the next stop.
		FNavigationStop Stop;
		Stop.Point.Position = FootprintPosition;
		Stop.Point.Floor = SelectedFloor;
		Stop.BuildingId = Building->BuildingId;
		StampPoint(Stop.Point, Stop.BuildingId);
		AddPlace(MoveTemp(Stop));
		return;
	}

	FSelectedEndpoint& Endpoint = SelectionMode == ENavigationSelectionMode::Start ? Start : Destination;
	const bool bWasSelectingStart = SelectionMode == ENavigationSelectionMode::Start;
	const FString EndpointName = bWasSelectingStart ? TEXT("Start") : TEXT("Destination");
	FNavigationRoutePoint SelectedPoint;
	SelectedPoint.Position = FootprintPosition;
	SelectedPoint.Floor = SelectedFloor;
	// The mode stays on: the next click changes the place again, until the button is pressed again (or Esc).
	SetEndpoint(Endpoint, SelectedPoint, Building->BuildingId, EndpointName,
		SelectedFloor == OutdoorFloor ? TEXT("outside") : FString::Printf(TEXT("indoors on Floor %d"), SelectedFloor),
		false);
	AddKeepPickingHint();
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
	StampPoint(Endpoint.Point, BuildingId);
	Endpoint.BuildingId = BuildingId;
	Endpoint.bSetViaDropdown = bSetViaDropdown;
	Endpoint.RoomOptionLabel = RoomOptionLabel;
	Endpoint.RoomId = RoomId;
	ClearRouteResult();
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
	// Outside, the list is each building's main doors; it is rebuilt when asked to (see SetOutdoorMode,
	// NotifyViewedBuildingChanged, NotifyInteriorChanged) or when the number of buildings changes.
	if (bOutdoorMode)
	{
		const int32 BuildingCount = Generator ? Generator->GetAllBuildings().Num() : 0;
		if (!bIndexedOutdoor || IndexedRoomSearchRevision == MAX_uint64 || IndexedBuildingCount != BuildingCount)
		{
			BuildOutdoorOptions();
		}
		return;
	}

	APolyBuilding* Building = Generator && Interior
		? Generator->FindBuildingById(Interior->GetLoadedBuildingId()) : nullptr;
	const uint64 Revision = Interior ? Interior->GetRoomSearchRevision() : 0;
	// The null test is written out as well as IsValid: the analyzer does not see through IsValid.
	const int32 FloorCount = Building && IsValid(Building) ? Building->GetFloorCount() : 0;
	if (!bIndexedOutdoor && IndexedRoomSearchRevision == Revision && IndexedBuilding.Get() == Building && IndexedFloorCount == FloorCount)
	{
		return;
	}
	bIndexedOutdoor = false;
	IndexedRoomSearchRevision = Revision;
	IndexedBuilding = Building;
	IndexedFloorCount = FloorCount;
	CachedRooms.Reset();
	SortedDestinationIndices.Reset();
	if (!Building || !IsValid(Building) || !Interior)
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
			Option.RoomType = Room->RoomType;
			Option.RoomName = Room->RoomName.IsEmpty() ? Room->RoomId : Room->RoomName;
			Option.Floor = Floor;
			Option.Centroid = ComputePolygonCentroid(Room->GetFootprint());
			// The id is not shown (a room is picked by name and floor); it can still be searched for.
			Option.DisplayLabel = FString::Printf(TEXT("%s (Floor %d)"), *Option.RoomName, Floor);
			Option.SearchableText = Option.DisplayLabel + TEXT(" ") + Option.RoomId + TEXT(" ") + FString::Join(Option.Tags, TEXT(" "));
			// Other says nothing, and would make a search for the word "other" match every untyped room.
			if (Option.RoomType != RoomTypes::Default())
			{
				Option.SearchableText += TEXT(" ") + Option.RoomType;
			}
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

TArray<UNavigationPanelWidget::FNavigationRoomOption> UNavigationPanelWidget::SearchRooms(const FString& Query) const
{
	RefreshRoomSearchIndex();
	TArray<FString> Words;
	Query.ParseIntoArrayWS(Words);
	TArray<FNavigationRoomOption> Options;
	for (const FNavigationRoomOption& Option : CachedRooms)
	{
		if (NavigationSearch::MatchesWords(Words, Option.SearchableText)) Options.Add(Option);
	}
	return Options;
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
	if (Found->Floor == OutdoorFloor)
	{
		return SetEndpointToOutdoorOption(*Found, bIsStart);
	}

	FNavigationRoutePoint SelectedPoint;
	FString BuildingId, Error;
	if (!MakeOptionPoint(*Found, SelectedPoint, BuildingId, Error))
	{
		SetStatusMessage(Error, ENavigationMessageKind::Error);
		return false;
	}

	// A dropdown pick is a direct, immediate set - cancel any in-progress click-to-pick rather than
	// leaving it dangling.
	SelectionMode = ENavigationSelectionMode::None;

	FSelectedEndpoint& Endpoint = bIsStart ? Start : Destination;
	SetEndpoint(Endpoint, SelectedPoint, BuildingId,
		bIsStart ? TEXT("Start") : TEXT("Destination"),
		FString::Printf(TEXT("to '%s' indoors on Floor %d"), *Found->RoomName, Found->Floor),
		true, RoomOptionLabel, Found->RoomId);
	return true;
}

namespace
{
	bool SamePlace(const FNavigationRoutePoint& A, const FNavigationRoutePoint& B)
	{
		return A.BuildingId == B.BuildingId && A.Floor == B.Floor && A.Position.Equals(B.Position, 0.01);
	}
}

// The legs of a trip from the Start through the stops to the Destination, planned one after another.
struct UNavigationPanelWidget::FJourney
{
	struct FWaypoint
	{
		FNavigationRoutePoint Point;
		FString Label;
	};

	TArray<FWaypoint> Waypoints;
	// The leg being planned, and what the legs before it found.
	int32 NextLeg = 0;
	TArray<FNavigationRoutePoint> Points;
	TArray<FNavigationRouteLeg> Legs;

	int32 LegCount() const { return Waypoints.Num() - 1; }
	bool Done() const { return NextLeg >= LegCount(); }
	const FWaypoint& From() const { return Waypoints[NextLeg]; }
	const FWaypoint& To() const { return Waypoints[NextLeg + 1]; }

	// Adds the route found for the leg being planned; it begins where the leg before it ended.
	void AddLeg(const FNavigationRouteResult& Result)
	{
		const int32 Shared = !Points.IsEmpty() && !Result.Points.IsEmpty() && SamePlace(Points.Last(), Result.Points[0]) ? 1 : 0;
		FNavigationRouteLeg Leg;
		Leg.FromLabel = From().Label;
		Leg.ToLabel = To().Label;
		Leg.FirstPointIndex = Points.Num() - Shared;
		for (int32 I = Shared; I < Result.Points.Num(); ++I)
		{
			Points.Add(Result.Points[I]);
		}
		Leg.LastPointIndex = Points.Num() - 1;
		Legs.Add(MoveTemp(Leg));
		++NextLeg;
	}
};

auto UNavigationPanelWidget::MakeJourney() const -> TSharedRef<FJourney>
{
	TSharedRef<FJourney> Trip = MakeShared<FJourney>();
	TArray<FJourney::FWaypoint> All;
	All.Add({ Start.Point, EndpointPhrase(Start) });
	for (const FNavigationStop& Stop : Stops)
	{
		All.Add({ Stop.Point, EndpointPhrase(StopAsEndpoint(Stop)) });
	}
	All.Add({ Destination.Point, EndpointPhrase(Destination) });
	// Two waypoints at the same place would make a leg of no length: the later one is left out, and the Destination keeps its
	// label. (Without stops the route is the planner's own, whatever the two points are.)
	Trip->Waypoints.Add(All[0]);
	for (int32 I = 1; I < All.Num(); ++I)
	{
		if (All.Num() > 2 && SamePlace(All[I].Point, Trip->Waypoints.Last().Point))
		{
			if (I == All.Num() - 1)
			{
				Trip->Waypoints.Last().Label = All[I].Label;
			}
			continue;
		}
		Trip->Waypoints.Add(All[I]);
	}
	if (Trip->Waypoints.Num() < 2)
	{
		Trip->Waypoints = { All[0], All.Last() };
	}
	return Trip;
}

void UNavigationPanelWidget::ApplyJourney(const FJourney& Trip)
{
	if (Trip.Points.IsEmpty())
	{
		ClearRouteResult();
		bLastFindRouteFailed = true;
		SetStatusMessage(TEXT("No route was returned."), ENavigationMessageKind::Warning);
		return;
	}
	RoutePoints = Trip.Points;
	RouteLegs = Trip.Legs;
	bRouteGuidanceVisible = true;
	bLastFindRouteFailed = false;
	SetStatusMessage(Stops.IsEmpty()
		? FString::Printf(TEXT("Route found: %d points."), RoutePoints.Num())
		: FString::Printf(TEXT("Route found through %d stop%s (%d points)."), Stops.Num(), Stops.Num() == 1 ? TEXT("") : TEXT("s"), RoutePoints.Num()),
		ENavigationMessageKind::Success);
	StartGuidance();
}

void UNavigationPanelWidget::FailJourney(const FJourney& Trip, const FNavigationRouteResult& LegResult)
{
	ClearRouteResult();
	bLastFindRouteFailed = true;
	const FString Reason = !LegResult.Error.IsEmpty() ? LegResult.Error
		: FString(TEXT("No route found. Check main/link entrances and floor connectors."));
	SetStatusMessage(Trip.LegCount() <= 1 ? Reason
		: FString::Printf(TEXT("Unable to find a route for leg %d of %d (%s -> %s): %s"),
			Trip.NextLeg + 1, Trip.LegCount(), *Trip.From().Label, *Trip.To().Label, *Reason),
		ENavigationMessageKind::Warning);
}

bool UNavigationPanelWidget::CanStartFindRoute()
{
	if (!RefreshSelectedPlaces()) return false;
	if (!CanFindRoute())
	{
		SetStatusMessage(TEXT("Set at least two places for the route."), ENavigationMessageKind::Error);
		return false;
	}
	PromoteEnds();
	APolyBuilding* Building = Generator->FindBuildingById(Start.BuildingId);
	if (!Building)
	{
		SetStatusMessage(TEXT("The selected building is unavailable."), ENavigationMessageKind::Error);
		return false;
	}
	return true;
}

void UNavigationPanelWidget::RequestFindRoute()
{
	if (!CanStartFindRoute())
	{
		return;
	}
	StopRouteJob();
	const TSharedRef<FJourney> Trip = MakeJourney();
	while (!Trip->Done())
	{
		FNavigationRouteResult LegResult;
		if (!FNavigationRoutePlanner::FindCampusRoute(*Generator, *Interior, Trip->From().Point, Trip->To().Point, LegResult))
		{
			FailJourney(*Trip, LegResult);
			return;
		}
		Trip->AddLeg(LegResult);
	}
	ApplyJourney(*Trip);
}

void UNavigationPanelWidget::StartFindRoute()
{
	if (!CanStartFindRoute())
	{
		return;
	}
	StopRouteJob();
	// A search is for the places as they are: the map is not for picking them any more (and the Go view has no button to stop it).
	SelectionMode = ENavigationSelectionMode::None;
	JobStart = Start.Point;
	JobDestination = Destination.Point;
	bLastFindRouteFailed = false;
	Journey = MakeJourney();
	RouteJob = MakeShared<FNavigationRouteJob>(*Generator, *Interior, Journey->From().Point, Journey->To().Point);
	RouteTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UNavigationPanelWidget::TickFindRoute));
	BroadcastStateChanged();
}

bool UNavigationPanelWidget::StepFindRoute(double BudgetSeconds)
{
	if (!RouteJob || !Journey)
	{
		return true;
	}
	// The result belongs to the points the search was started for; if either has been changed or cleared, it is no use. (The
	// stops, and edits of the interior, stop the search where they are made.)
	auto SameEndpoint = [](const FSelectedEndpoint& Endpoint, const FNavigationRoutePoint& Point)
	{
		return Endpoint.bValid && Endpoint.Point.BuildingId == Point.BuildingId && Endpoint.Point.Floor == Point.Floor
			&& Endpoint.Point.Position.Equals(Point.Position);
	};
	if (!Generator || !Interior || !SameEndpoint(Start, JobStart) || !SameEndpoint(Destination, JobDestination))
	{
		StopRouteJob();
		BroadcastStateChanged();
		return true;
	}
	if (!RouteJob->Step(BudgetSeconds))
	{
		return false;
	}
	const TSharedPtr<FNavigationRouteJob> Finished = RouteJob;
	const TSharedPtr<FJourney> Trip = Journey;
	if (!Finished->Succeeded())
	{
		StopRouteJob();
		FailJourney(*Trip, Finished->GetResult());
		return true;
	}
	Trip->AddLeg(Finished->GetResult());
	if (!Trip->Done())
	{
		RouteJob = MakeShared<FNavigationRouteJob>(*Generator, *Interior, Trip->From().Point, Trip->To().Point);
		return false;
	}
	StopRouteJob();
	ApplyJourney(*Trip);
	return true;
}

void UNavigationPanelWidget::CancelFindRoute()
{
	if (StopRouteJob())
	{
		SetStatusMessage(TEXT("Route search cancelled."), ENavigationMessageKind::Warning);
	}
}

bool UNavigationPanelWidget::StopRouteJob()
{
	if (RouteTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RouteTicker);
		RouteTicker.Reset();
	}
	const bool bWasRunning = RouteJob.IsValid();
	RouteJob.Reset();
	Journey.Reset();
	return bWasRunning;
}

bool UNavigationPanelWidget::TickFindRoute(float DeltaTime)
{
	// About half a frame at 60 fps, so the editor keeps answering while this works.
	return !StepFindRoute(0.008);
}

float UNavigationPanelWidget::GetFindRouteProgress() const
{
	if (!RouteJob || !Journey)
	{
		return 0.f;
	}
	return (Journey->NextLeg + RouteJob->GetProgress()) / FMath::Max(1, Journey->LegCount());
}

FString UNavigationPanelWidget::GetFindRouteProgressText() const
{
	if (!RouteJob || !Journey)
	{
		return FString();
	}
	return Journey->LegCount() > 1
		? FString::Printf(TEXT("Leg %d of %d: %s"), Journey->NextLeg + 1, Journey->LegCount(), *RouteJob->GetPhase())
		: RouteJob->GetPhase();
}

void UNavigationPanelWidget::BeginDestroy()
{
	if (Generator) Generator->OnNavigationLayoutChanged.RemoveAll(this);
	if (Interior) Interior->OnNavigationLayoutChanged.RemoveAll(this);
	StopRouteJob();
	Super::BeginDestroy();
}

void UNavigationPanelWidget::StopsChanged(const FString& Message, ENavigationMessageKind Kind)
{
	// A search and a route are for the stops they were made with.
	StopRouteJob();
	ClearRouteResult();
	bLastFindRouteFailed = false;
	SetStatusMessage(Message, Kind);
}

FString UNavigationPanelWidget::GetStopLabel(int32 StopIndex) const
{
	if (!Stops.IsValidIndex(StopIndex))
	{
		return FString();
	}
	const FNavigationStop& Stop = Stops[StopIndex];
	// A place picked on the map has no name: say where it is.
	if (Stop.RoomOptionLabel.IsEmpty())
	{
		return EndpointPhrase(StopAsEndpoint(Stop));
	}
	// A main door picked from the list already names its building ("T1 · Main door").
	return Stop.Point.Floor == OutdoorFloor ? Stop.RoomOptionLabel : BuildingDisplayName(Stop.BuildingId) + TEXT(" · ") + Stop.RoomOptionLabel;
}

bool UNavigationPanelWidget::AddStopToRoom(const FString& RoomOptionLabel)
{
	if (Stops.Num() >= MaxRouteStops)
	{
		SetStatusMessage(FString::Printf(TEXT("A route can have up to %d stops."), MaxRouteStops), ENavigationMessageKind::Warning);
		return false;
	}
	FNavigationStop Stop;
	return MakeStopFromLabel(RoomOptionLabel, Stop) && AddStop(MoveTemp(Stop));
}

bool UNavigationPanelWidget::AddPlaceToRoom(const FString& RoomOptionLabel)
{
	FNavigationStop Place;
	return MakeStopFromLabel(RoomOptionLabel, Place) && AddPlace(MoveTemp(Place));
}

bool UNavigationPanelWidget::MakeStopFromLabel(const FString& RoomOptionLabel, FNavigationStop& OutStop)
{
	const TArray<FNavigationRoomOption>& Options = GetSelectableRooms();
	const FNavigationRoomOption* Found = Options.FindByPredicate([&RoomOptionLabel](const FNavigationRoomOption& Option)
	{
		return Option.GetDisplayLabel() == RoomOptionLabel;
	});
	if (!Found)
	{
		SetStatusMessage(TEXT("Could not find that place."), ENavigationMessageKind::Error);
		return false;
	}
	FNavigationStop Stop;
	FString Error;
	if (!MakeOptionPoint(*Found, Stop.Point, Stop.BuildingId, Error))
	{
		SetStatusMessage(Error, ENavigationMessageKind::Error);
		return false;
	}
	StampPoint(Stop.Point, Stop.BuildingId);
	Stop.RoomOptionLabel = RoomOptionLabel;
	Stop.RoomId = Found->RoomId;
	OutStop = MoveTemp(Stop);
	return true;
}

bool UNavigationPanelWidget::ReplaceStop(int32 StopIndex, const FString& RoomOptionLabel)
{
	FNavigationStop Stop;
	if (!Stops.IsValidIndex(StopIndex) || !MakeStopFromLabel(RoomOptionLabel, Stop))
	{
		return false;
	}
	// Not where the route already is next to it: that would be a leg of no length.
	const FNavigationRoutePoint* Before = StopIndex > 0 ? &Stops[StopIndex - 1].Point : (Start.bValid ? &Start.Point : nullptr);
	const FNavigationRoutePoint* After = StopIndex + 1 < Stops.Num() ? &Stops[StopIndex + 1].Point : (Destination.bValid ? &Destination.Point : nullptr);
	if ((Before && SamePlace(*Before, Stop.Point)) || (After && SamePlace(*After, Stop.Point)))
	{
		SetStatusMessage(TEXT("The route is already there: choose another place for the stop."), ENavigationMessageKind::Warning);
		return false;
	}
	Stops[StopIndex] = MoveTemp(Stop);
	StopsChanged(FString::Printf(TEXT("Stop %d is now %s."), StopIndex + 1, *GetStopLabel(StopIndex)), ENavigationMessageKind::Success);
	return true;
}

FString UNavigationPanelWidget::PlaceText(const FSelectedEndpoint& Endpoint) const
{
	if (!Endpoint.bValid)
	{
		return FString();
	}
	if (Endpoint.bSetViaDropdown && !Endpoint.RoomOptionLabel.IsEmpty())
	{
		// In another building than the open one a place carries its building; in it, or outside, the list names it fully.
		const FString Other = OtherBuildingLabel(Endpoint);
		return Other.IsEmpty() ? Endpoint.RoomOptionLabel : Other;
	}
	// A point picked on the map is a custom point: outside, or on a floor (with its building when that is not the open one). The longer
	// phrase, with the building it is near, is for the status and the directions.
	const bool bOutside = Endpoint.Point.Floor == OutdoorFloor;
	FString Text = bOutside ? FString(TEXT("Custom Point (External)")) : FString::Printf(TEXT("Custom Point (Floor %d)"), Endpoint.Point.Floor);
	if (!bOutside && Interior && Endpoint.BuildingId != Interior->GetLoadedBuildingId())
	{
		Text = BuildingDisplayName(Endpoint.BuildingId) + TEXT(" · ") + Text;
	}
	return Text;
}

bool UNavigationPanelWidget::IsPlaceSet(int32 Row) const
{
	if (Row == 0)
	{
		return Start.bValid;
	}
	return Row == GetPlaceCount() - 1 ? Destination.bValid : Stops.IsValidIndex(Row - 1);
}

FString UNavigationPanelWidget::GetPlaceRole(int32 Row) const
{
	if (Row == 0)
	{
		return TEXT("Start");
	}
	return Row == GetPlaceCount() - 1 ? FString(TEXT("Destination")) : FString::Printf(TEXT("Stop %d"), Row);
}

FString UNavigationPanelWidget::GetPlaceText(int32 Row) const
{
	if (Row < 0 || Row >= GetPlaceCount())
	{
		return FString();
	}
	return PlaceText(Row == 0 ? Start : (Row == GetPlaceCount() - 1 ? Destination : StopAsEndpoint(Stops[Row - 1])));
}

bool UNavigationPanelWidget::SetPlaceToRoom(int32 Row, const FString& RoomOptionLabel)
{
	if (Row < 0 || Row >= GetPlaceCount())
	{
		return false;
	}
	if (Row == 0)
	{
		return SetStartToRoom(RoomOptionLabel);
	}
	return Row == GetPlaceCount() - 1 ? SetDestinationToRoom(RoomOptionLabel) : ReplaceStop(Row - 1, RoomOptionLabel);
}

bool UNavigationPanelWidget::ClearPlace(int32 Row)
{
	if (Row < 0 || Row >= GetPlaceCount() || !IsPlaceSet(Row))
	{
		return false;
	}
	if (Row > 0 && Row < GetPlaceCount() - 1)
	{
		return RemoveStop(Row - 1);
	}
	const FString Role = GetPlaceRole(Row);
	(Row == 0 ? Start : Destination) = FSelectedEndpoint();
	StopsChanged(FString::Printf(TEXT("%s cleared."), *Role), ENavigationMessageKind::Warning);
	return true;
}

TArray<UNavigationPanelWidget::FSelectedEndpoint> UNavigationPanelWidget::PlacesAsRows() const
{
	TArray<FSelectedEndpoint> Rows;
	Rows.Add(Start);
	for (const FNavigationStop& Stop : Stops)
	{
		Rows.Add(StopAsEndpoint(Stop));
	}
	Rows.Add(Destination);
	return Rows;
}

void UNavigationPanelWidget::ApplyRows(TArray<FSelectedEndpoint> Rows, TArray<int32>* Tags)
{
	// A place that is not set cannot be between the ends: it goes, and the places next to it close up.
	for (int32 I = Rows.Num() - 2; I >= 1; --I)
	{
		if (!Rows[I].bValid)
		{
			Rows.RemoveAt(I);
			if (Tags)
			{
				Tags->RemoveAt(I);
			}
		}
	}
	while (Rows.Num() < 2)
	{
		Rows.Add(FSelectedEndpoint());
		if (Tags)
		{
			Tags->Add(INDEX_NONE);
		}
	}
	Start = Rows[0];
	Destination = Rows.Last();
	Stops.Reset();
	for (int32 I = 1; I < Rows.Num() - 1; ++I)
	{
		FNavigationStop Stop;
		Stop.Point = Rows[I].Point;
		Stop.BuildingId = Rows[I].BuildingId;
		Stop.RoomOptionLabel = Rows[I].RoomOptionLabel;
		Stop.RoomId = Rows[I].RoomId;
		Stops.Add(MoveTemp(Stop));
	}
}

int32 UNavigationPanelWidget::MovePlace(int32 Row, int32 Offset)
{
	TArray<FSelectedEndpoint> Rows = PlacesAsRows();
	const int32 To = Row + Offset;
	if (!Rows.IsValidIndex(Row) || !Rows.IsValidIndex(To) || Offset == 0 || !Rows[Row].bValid)
	{
		return INDEX_NONE;
	}
	TArray<int32> Tags;
	for (int32 I = 0; I < Rows.Num(); ++I)
	{
		Tags.Add(I);
	}
	Rows.Swap(Row, To);
	Tags.Swap(Row, To);
	ApplyRows(MoveTemp(Rows), &Tags);
	StopsChanged(TEXT("Order changed. Press Find Route to search again."), ENavigationMessageKind::Warning);
	return Tags.IndexOfByKey(Row);
}

void UNavigationPanelWidget::ReversePlaces()
{
	TArray<FSelectedEndpoint> Rows = PlacesAsRows();
	Algo::Reverse(Rows);
	ApplyRows(MoveTemp(Rows));
	StopsChanged(TEXT("Route reversed. Press Find Route to search again."), ENavigationMessageKind::Warning);
}

bool UNavigationPanelWidget::AddStop(FNavigationStop Stop)
{
	if (Stops.Num() >= MaxRouteStops)
	{
		// Nothing more can be added, so a click mode for stops has nothing left to do.
		if (SelectionMode == ENavigationSelectionMode::Stop)
		{
			SelectionMode = ENavigationSelectionMode::None;
		}
		SetStatusMessage(FString::Printf(TEXT("A route can have up to %d stops."), MaxRouteStops), ENavigationMessageKind::Warning);
		return false;
	}
	// A stop where the route already is would be a leg of no length.
	const FNavigationRoutePoint* Before = !Stops.IsEmpty() ? &Stops.Last().Point : (Start.bValid ? &Start.Point : nullptr);
	if ((Before && SamePlace(*Before, Stop.Point)) || (Destination.bValid && SamePlace(Destination.Point, Stop.Point)))
	{
		SetStatusMessage(TEXT("The route is already there: choose another place for the stop."), ENavigationMessageKind::Warning);
		return false;
	}
	Stops.Add(MoveTemp(Stop));
	// While places are being clicked, say what to do next (and the mode ends by itself at the limit).
	if (SelectionMode == ENavigationSelectionMode::Stop && Stops.Num() >= MaxRouteStops)
	{
		SelectionMode = ENavigationSelectionMode::None;
	}
	StopsChanged(FString::Printf(TEXT("Added stop %d of %d: %s.%s"), Stops.Num(), MaxRouteStops, *GetStopLabel(Stops.Num() - 1),
		SelectionMode == ENavigationSelectionMode::Stop ? TEXT(" Click the next one, or press Esc when done.") : TEXT("")),
		ENavigationMessageKind::Success);
	return true;
}

bool UNavigationPanelWidget::AddPlace(FNavigationStop Place)
{
	// A Destination that is set becomes a stop, so it takes one of the stops' places; one that is not set is only filled.
	if (Destination.bValid && Stops.Num() >= MaxRouteStops)
	{
		if (SelectionMode == ENavigationSelectionMode::Stop)
		{
			SelectionMode = ENavigationSelectionMode::None;
		}
		SetStatusMessage(FString::Printf(TEXT("A route can have up to %d stops."), MaxRouteStops), ENavigationMessageKind::Warning);
		return false;
	}
	// A place where the route already ends would be a leg of no length.
	const FNavigationRoutePoint* Before = Destination.bValid ? &Destination.Point
		: (!Stops.IsEmpty() ? &Stops.Last().Point : (Start.bValid ? &Start.Point : nullptr));
	if (Before && SamePlace(*Before, Place.Point))
	{
		SetStatusMessage(TEXT("The route is already there: choose another place."), ENavigationMessageKind::Warning);
		return false;
	}
	const bool bMovedDestination = Destination.bValid;
	// Start, the stops, the Destination, then the new place; the rows that are not set between the ends close up.
	TArray<FSelectedEndpoint> Rows = PlacesAsRows();
	Rows.Add(StopAsEndpoint(Place));
	ApplyRows(MoveTemp(Rows));
	if (SelectionMode == ENavigationSelectionMode::Stop && Stops.Num() >= MaxRouteStops)
	{
		SelectionMode = ENavigationSelectionMode::None;
	}
	const FString Now = GetPlaceText(GetPlaceCount() - 1);
	StopsChanged(FString::Printf(TEXT("The Destination is now %s.%s%s"), *Now,
		bMovedDestination ? *FString::Printf(TEXT(" The one before it is stop %d."), Stops.Num()) : TEXT(""),
		SelectionMode == ENavigationSelectionMode::Stop ? TEXT(" Click the next one, or press Esc when done.") : TEXT("")),
		ENavigationMessageKind::Success);
	return true;
}

bool UNavigationPanelWidget::RemoveStop(int32 StopIndex)
{
	if (!Stops.IsValidIndex(StopIndex))
	{
		return false;
	}
	const FString Removed = GetStopLabel(StopIndex);
	Stops.RemoveAt(StopIndex);
	StopsChanged(FString::Printf(TEXT("Removed stop: %s. Press Find Route to search again."), *Removed), ENavigationMessageKind::Warning);
	return true;
}

bool UNavigationPanelWidget::MoveStop(int32 FromIndex, int32 ToIndex)
{
	if (!Stops.IsValidIndex(FromIndex) || !Stops.IsValidIndex(ToIndex) || FromIndex == ToIndex)
	{
		return false;
	}
	FNavigationStop Moved = MoveTemp(Stops[FromIndex]);
	Stops.RemoveAt(FromIndex);
	Stops.Insert(MoveTemp(Moved), ToIndex);
	StopsChanged(TEXT("Stop order changed. Press Find Route to search again."), ENavigationMessageKind::Warning);
	return true;
}

void UNavigationPanelWidget::ClearStops()
{
	if (Stops.IsEmpty())
	{
		return;
	}
	Stops.Reset();
	StopsChanged(TEXT("Stops cleared. Press Find Route to search again."), ENavigationMessageKind::Warning);
}

bool UNavigationPanelWidget::MakeOptionPoint(const FNavigationRoomOption& Option, FNavigationRoutePoint& OutPoint, FString& OutBuildingId, FString& OutError) const
{
	const bool bOutdoor = Option.Floor == OutdoorFloor;
	const APolyBuilding* Building = !Generator || !Interior ? nullptr
		: Generator->FindBuildingById(bOutdoor ? Option.BuildingId : Interior->GetLoadedBuildingId());
	FVector2D Position = Option.Centroid;
	FString Reason;
	const AInteriorRoom* Room = Building && !bOutdoor ? Interior->GetRoomById(Option.RoomId) : nullptr;
	const bool bValid = Building && (bOutdoor
		? FNavigationRoutePlanner::IsValidPoint(*Building, *Interior, Generator->GetScale(), Option.Floor, Position, Reason)
		: Room && FNavigationRoutePlanner::FindRoomDestination(*Generator, *Interior, Building->BuildingId,
			Option.Floor, Room->GetFootprint(), Position, &Option.Centroid));
	if (!bValid)
	{
		OutError = bOutdoor
			? FString::Printf(TEXT("'%s' cannot be used as a point (%s)."), *Option.DisplayLabel, *Reason)
			: FString::Printf(TEXT("No point with enough clearance could be found inside '%s'."), *Option.RoomName);
		return false;
	}
	OutPoint = FNavigationRoutePoint();
	OutPoint.Position = Position;
	OutPoint.Floor = Option.Floor;
	OutBuildingId = Building->BuildingId;
	return true;
}

void UNavigationPanelWidget::StampPoint(FNavigationRoutePoint& Point, const FString& BuildingId) const
{
	Point.BuildingId = BuildingId;
	if (const APolyBuilding* Building = Generator ? Generator->FindBuildingById(BuildingId) : nullptr)
	{
		const FVector2D XY = (Point.Position + Building->BuildingOffset) * Generator->GetScale();
		Point.WorldPosition = FVector(XY.X, XY.Y, FMath::Max(0, Point.Floor - 1) * Building->GetFloorHeight());
		Point.bHasWorldPosition = true;
	}
}

UNavigationPanelWidget::FSelectedEndpoint UNavigationPanelWidget::StopAsEndpoint(const FNavigationStop& Stop) const
{
	FSelectedEndpoint Endpoint;
	Endpoint.bValid = true;
	Endpoint.Point = Stop.Point;
	Endpoint.BuildingId = Stop.BuildingId;
	Endpoint.bSetViaDropdown = !Stop.RoomOptionLabel.IsEmpty();
	Endpoint.RoomOptionLabel = Stop.RoomOptionLabel;
	Endpoint.RoomId = Stop.RoomId;
	return Endpoint;
}

void UNavigationPanelWidget::ApplyRouteResult(const FNavigationRouteResult& Result)
{
	if (Result.Points.IsEmpty())
	{
		ClearRouteResult();
		bLastFindRouteFailed = true;
		SetStatusMessage(Result.Error.IsEmpty() ? TEXT("No route was returned.") : Result.Error,
			ENavigationMessageKind::Warning);
		return;
	}

	RoutePoints = Result.Points;
	// A route that was not planned through stops is one leg from the Start to the Destination.
	FNavigationRouteLeg Leg;
	Leg.FirstPointIndex = 0;
	Leg.LastPointIndex = RoutePoints.Num() - 1;
	Leg.FromLabel = Start.bValid ? EndpointPhrase(Start) : FString(TEXT("Start"));
	Leg.ToLabel = Destination.bValid ? EndpointPhrase(Destination) : FString(TEXT("Destination"));
	RouteLegs = { MoveTemp(Leg) };
	bRouteGuidanceVisible = true;
	bLastFindRouteFailed = false;
	SetStatusMessage(FString::Printf(TEXT("Route found: %d points."), RoutePoints.Num()),
		ENavigationMessageKind::Success);
	StartGuidance();
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
		const FString OwnerId = Input.BuildingId.IsEmpty() ? BuildingId : Input.BuildingId;
		const APolyBuilding* Owner = Generator->FindBuildingById(OwnerId);
		if (!FMath::IsFinite(Input.WorldPosition.X) || !FMath::IsFinite(Input.WorldPosition.Y)
			|| !FMath::IsFinite(Input.WorldPosition.Z) || Input.Floor < OutdoorFloor
			|| !Owner || Input.Floor > Owner->GetFloorCount())
		{
			SetStatusMessage(TEXT("The 3D route contains an invalid position or floor."), ENavigationMessageKind::Error);
			return false;
		}
		FNavigationRoutePoint Point;
		Point.Position = FVector2D(Input.WorldPosition.X, Input.WorldPosition.Y) / Generator->GetScale()
			- Owner->BuildingOffset;
		Point.BuildingId = OwnerId;
		Point.Floor = Input.Floor;
		Point.ConnectorId = Input.ConnectorId;
		Point.ConnectorType = Input.ConnectorType;
		Point.WorldPosition = Input.WorldPosition;
		Point.bHasWorldPosition = true;
		Result.Points.Add(Point);
	}
	Start = FSelectedEndpoint();
	Start.bValid = true;
	Start.BuildingId = Result.Points[0].BuildingId;
	Start.Point = Result.Points[0];
	Destination = FSelectedEndpoint();
	Destination.bValid = true;
	Destination.BuildingId = Result.Points.Last().BuildingId;
	Destination.Point = Result.Points.Last();
	// An imported route is the whole journey; stops from before are not on it.
	Stops.Reset();
	SelectionMode = ENavigationSelectionMode::None;
	ApplyRouteResult(Result);
	return true;
}

void UNavigationPanelWidget::ClearAll()
{
	bEndpointsNeedRefresh = false;
	StopRouteJob();
	bRouteGuidanceVisible = true;
	SelectionMode = ENavigationSelectionMode::None;
	Start = FSelectedEndpoint();
	Destination = FSelectedEndpoint();
	Stops.Reset();
	ClearRouteResult();
	bLastFindRouteFailed = false;
	StatusMessage.Empty();
	StatusKind = ENavigationMessageKind::Instruction;
	BroadcastStateChanged();
}

void UNavigationPanelWidget::NotifyRoomDeleted(const FString& RoomId)
{
	bool bChanged = false;
	if (Start.bValid && Start.bSetViaDropdown && Start.RoomId == RoomId && Interior && Start.BuildingId == Interior->GetLoadedBuildingId())
	{
		Start = FSelectedEndpoint();
		bChanged = true;
	}
	if (Destination.bValid && Destination.bSetViaDropdown && Destination.RoomId == RoomId && Interior && Destination.BuildingId == Interior->GetLoadedBuildingId())
	{
		Destination = FSelectedEndpoint();
		bChanged = true;
	}
	for (int32 StopIndex = Stops.Num() - 1; StopIndex >= 0; --StopIndex)
	{
		if (Stops[StopIndex].RoomId == RoomId && Interior && Stops[StopIndex].BuildingId == Interior->GetLoadedBuildingId() && Stops[StopIndex].Point.Floor != OutdoorFloor)
		{
			Stops.RemoveAt(StopIndex);
			bChanged = true;
		}
	}
	if (bChanged)
	{
		StopRouteJob();
		ClearRouteResult();
		bLastFindRouteFailed = false;
		SetStatusMessage(TEXT("Start, Destination or a stop was cleared - the room it was set to no longer exists."), ENavigationMessageKind::Warning);
	}
}

void UNavigationPanelWidget::CancelSelection()
{
	if (SelectionMode != ENavigationSelectionMode::None)
	{
		// Ending the clicks for stops is finishing them, not undoing them.
		const bool bWasAddingStops = SelectionMode == ENavigationSelectionMode::Stop;
		SelectionMode = ENavigationSelectionMode::None;
		SetStatusMessage(bWasAddingStops ? FString::Printf(TEXT("Stops: %d. Press Find Route to search."), Stops.Num()) : FString(TEXT("Selection cancelled.")),
			bWasAddingStops ? ENavigationMessageKind::Success : ENavigationMessageKind::Warning);
	}
}

void UNavigationPanelWidget::SetSelectionMode(ENavigationSelectionMode NewMode)
{
	SelectionMode = NewMode;
	StatusMessage.Empty();
	StatusKind = ENavigationMessageKind::Instruction;
	if (NewMode != ENavigationSelectionMode::None)
	{
		OnPickingStarted.Broadcast();
	}
	BroadcastStateChanged();
}

void UNavigationPanelWidget::AddKeepPickingHint()
{
	if (SelectionMode == ENavigationSelectionMode::Start || SelectionMode == ENavigationSelectionMode::Destination)
	{
		SetStatusMessage(StatusMessage + TEXT(" Click again to change it, or press + again to stop."), StatusKind);
	}
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
	if (!Interior || !Generator || GetSetPlaceCount() < 2)
	{
		return false;
	}
	// Every place that is set is in a building that is there.
	if ((Start.bValid && !IsValid(Generator->FindBuildingById(Start.BuildingId)))
		|| (Destination.bValid && !IsValid(Generator->FindBuildingById(Destination.BuildingId))))
	{
		return false;
	}
	for (const FNavigationStop& Stop : Stops)
	{
		if (!IsValid(Generator->FindBuildingById(Stop.BuildingId)))
		{
			return false;
		}
	}
	return true;
}

int32 UNavigationPanelWidget::GetSetPlaceCount() const
{
	return (Start.bValid ? 1 : 0) + Stops.Num() + (Destination.bValid ? 1 : 0);
}

void UNavigationPanelWidget::PromoteEnds()
{
	// With two places or more, a route is from the first to the last: an end that is not set is the stop next to it.
	if (!Start.bValid && !Stops.IsEmpty())
	{
		Start = StopAsEndpoint(Stops[0]);
		Stops.RemoveAt(0);
	}
	if (!Destination.bValid && !Stops.IsEmpty())
	{
		Destination = StopAsEndpoint(Stops.Last());
		Stops.RemoveAt(Stops.Num() - 1);
	}
}

FString UNavigationPanelWidget::GetStartRoomLabel() const
{
	return WasStartSetViaDropdown() && EndpointIsInCurrentList(Start) ? Start.RoomOptionLabel : FString();
}

FString UNavigationPanelWidget::GetDestinationRoomLabel() const
{
	return WasDestinationSetViaDropdown() && EndpointIsInCurrentList(Destination) ? Destination.RoomOptionLabel : FString();
}

FString UNavigationPanelWidget::OtherBuildingLabel(const FSelectedEndpoint& Endpoint) const
{
	if (!Endpoint.bValid || !Endpoint.bSetViaDropdown || !Interior || !Generator || EndpointIsInCurrentList(Endpoint))
	{
		return FString();
	}
	// A main door already names its building.
	if (Endpoint.Point.Floor == OutdoorFloor)
	{
		return Endpoint.RoomOptionLabel;
	}
	const APolyBuilding* Building = Generator->FindBuildingById(Endpoint.BuildingId);
	const FString BuildingName = (Building && !Building->BuildingName.IsEmpty()) ? Building->BuildingName : Endpoint.BuildingId;
	// The label is "Name [Id] (Floor N)"; this entry shows the building, then the name and the floor, without the id.
	const FString RoomLabel = Endpoint.RoomOptionLabel.Replace(*FString::Printf(TEXT(" [%s]"), *Endpoint.RoomId), TEXT(""));
	return BuildingName + TEXT(" · ") + RoomLabel;
}

FString UNavigationPanelWidget::GetStartOtherBuildingLabel() const
{
	return OtherBuildingLabel(Start);
}

FString UNavigationPanelWidget::GetDestinationOtherBuildingLabel() const
{
	return OtherBuildingLabel(Destination);
}

bool UNavigationPanelWidget::EndpointIsInCurrentList(const FSelectedEndpoint& Endpoint) const
{
	if (!Endpoint.bValid || !Interior)
	{
		return false;
	}
	// Outside the list holds main doors (points outdoors); inside it holds the open building's rooms.
	if (bOutdoorMode)
	{
		return Endpoint.Point.Floor == OutdoorFloor;
	}
	return Endpoint.Point.Floor != OutdoorFloor && Endpoint.BuildingId == Interior->GetLoadedBuildingId();
}

void UNavigationPanelWidget::SetOutdoorMode(bool bOutdoor)
{
	if (bOutdoorMode == bOutdoor)
	{
		return;
	}
	bOutdoorMode = bOutdoor;
	SelectionMode = ENavigationSelectionMode::None;
	IndexedRoomSearchRevision = MAX_uint64;
	BroadcastStateChanged();
}

void UNavigationPanelWidget::SelectOutdoorGroundPoint(FVector2D CampusPosition, bool bOnBuilding)
{
	if (SelectionMode == ENavigationSelectionMode::None || !Generator)
	{
		return;
	}
	if (bOnBuilding)
	{
		SetStatusMessage(TEXT("That is a building. Click the ground next to it, or pick its main door in the list."), ENavigationMessageKind::Error);
		return;
	}

	// The point belongs to the building whose outside area it is in (the nearest one if several), and must not be inside any building.
	const APolyBuilding* Owner = nullptr;
	float OwnerDistance = TNumericLimits<float>::Max();
	for (const APolyBuilding* Building : Generator->GetAllBuildings())
	{
		if (!Building)
		{
			continue;
		}
		const FVector2D Local = CampusPosition - Building->BuildingOffset;
		if (IsInsideOrOnFootprint(Local, Building->GetFootprint()))
		{
			SetStatusMessage(TEXT("That spot is inside a building. Click the ground outside, or use the room list."), ENavigationMessageKind::Error);
			return;
		}
		if (!IsOutdoorPoint(*Building, Local))
		{
			continue;
		}
		const TArray<FVector2D>& Outline = Building->GetFootprint();
		float Nearest = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Outline.Num(); ++i)
		{
			const FVector2D Closest = FMath::ClosestPointOnSegment2D(Local, Outline[i], Outline[(i + 1) % Outline.Num()]);
			Nearest = FMath::Min(Nearest, static_cast<float>(FVector2D::Distance(Local, Closest)));
		}
		if (Nearest < OwnerDistance)
		{
			OwnerDistance = Nearest;
			Owner = Building;
		}
	}
	if (!Owner)
	{
		SetStatusMessage(TEXT("Too far from every building. Click the ground closer to one."), ENavigationMessageKind::Error);
		return;
	}

	FNavigationRoutePoint Point;
	Point.Position = CampusPosition - Owner->BuildingOffset;
	Point.Floor = OutdoorFloor;
	if (SelectionMode == ENavigationSelectionMode::Stop)
	{
		FNavigationStop Stop;
		Stop.Point = Point;
		Stop.BuildingId = Owner->BuildingId;
		StampPoint(Stop.Point, Stop.BuildingId);
		AddPlace(MoveTemp(Stop));
		return;
	}
	FSelectedEndpoint& Endpoint = SelectionMode == ENavigationSelectionMode::Start ? Start : Destination;
	const FString EndpointName = SelectionMode == ENavigationSelectionMode::Start ? TEXT("Start") : TEXT("Destination");
	// The mode stays on, as for a click indoors.
	SetEndpoint(Endpoint, Point, Owner->BuildingId, EndpointName, TEXT("outside"), false);
	AddKeepPickingHint();
}

void UNavigationPanelWidget::BuildOutdoorOptions() const
{
	IndexedRoomSearchRevision = 0;
	bIndexedOutdoor = true;
	CachedRooms.Reset();
	SortedDestinationIndices.Reset();
	IndexedBuildingCount = 0;
	if (!Generator || Generator->GetScale() <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const float Scale = Generator->GetScale();
	TArray<APolyBuilding*> Buildings = Generator->GetAllBuildings();
	IndexedBuildingCount = Buildings.Num();
	Buildings.Sort([](const APolyBuilding& A, const APolyBuilding& B) { return A.BuildingName.Compare(B.BuildingName, ESearchCase::IgnoreCase) < 0; });
	for (const APolyBuilding* Building : Buildings)
	{
		if (!Building)
		{
			continue;
		}
		const TArray<FVector2D>& Outline = Building->GetFootprint();
		TArray<FInteriorEntrance> Doors;
		for (const FInteriorEntrance& Entrance : Generator->GetStoredEntrances(Building->BuildingId))
		{
			if (Entrance.Kind == EInteriorEntranceKind::Main && Outline.IsValidIndex(Entrance.EdgeIndex))
			{
				Doors.Add(Entrance);
			}
		}
		const FString BuildingName = Building->BuildingName.IsEmpty() ? Building->BuildingId : Building->BuildingName;
		for (int32 DoorNumber = 0; DoorNumber < Doors.Num(); ++DoorNumber)
		{
			const FInteriorEntrance& Door = Doors[DoorNumber];
			const FVector2D A = Outline[Door.EdgeIndex];
			const FVector2D B = Outline[(Door.EdgeIndex + 1) % Outline.Num()];
			const FVector2D Direction = (B - A).GetSafeNormal();
			// Offset and Width are world units along the wall; a footprint unit is Scale of them.
			const FVector2D Middle = A + Direction * ((Door.Offset + Door.Width * 0.5f) / Scale);
			// A metre out from the wall, on the side that is not the building (a door in the wall is not a valid spot to stand).
			const float OutDistance = 100.f / Scale;
			const FVector2D Normal(Direction.Y, -Direction.X);
			FVector2D Outside = Middle + Normal * OutDistance;
			if (IsInsideOrOnFootprint(Outside, Outline))
			{
				Outside = Middle - Normal * OutDistance;
			}
			FNavigationRoomOption Option;
			Option.RoomId = Door.Id;
			Option.RoomName = Doors.Num() > 1 ? FString::Printf(TEXT("Main door %d"), DoorNumber + 1) : FString(TEXT("Main door"));
			Option.Floor = OutdoorFloor;
			Option.BuildingId = Building->BuildingId;
			Option.Centroid = Outside;
			Option.DisplayLabel = FString::Printf(TEXT("%s · %s"), *BuildingName, *Option.RoomName);
			Option.SearchableText = Option.DisplayLabel + TEXT(" entrance gate outside");
			SortedDestinationIndices.Add(CachedRooms.Num());
			CachedRooms.Add(MoveTemp(Option));
		}
	}
}

bool UNavigationPanelWidget::SetEndpointToOutdoorOption(const FNavigationRoomOption& Option, bool bIsStart)
{
	FNavigationRoutePoint Point;
	FString BuildingId, Error;
	if (!MakeOptionPoint(Option, Point, BuildingId, Error))
	{
		SetStatusMessage(Error, ENavigationMessageKind::Error);
		return false;
	}
	SelectionMode = ENavigationSelectionMode::None;
	FSelectedEndpoint& Endpoint = bIsStart ? Start : Destination;
	SetEndpoint(Endpoint, Point, BuildingId, bIsStart ? TEXT("Start") : TEXT("Destination"),
		FString::Printf(TEXT("to '%s'"), *Option.DisplayLabel), true, Option.DisplayLabel, Option.RoomId);
	return true;
}

void UNavigationPanelWidget::NotifyViewedBuildingChanged()
{
	SelectionMode = ENavigationSelectionMode::None;
	IndexedRoomSearchRevision = MAX_uint64;
	BroadcastStateChanged();
}

void UNavigationPanelWidget::HandleLayoutChanged()
{
	// Interior view loads suppress their own notifications. A real exterior floor-count
	// change still invalidates geometry, including when it corrects an inconsistent imported shell.
	NotifyInteriorChanged();
}

void UNavigationPanelWidget::NotifyInteriorChanged()
{
	IndexedRoomSearchRevision = MAX_uint64;
	bEndpointsNeedRefresh = true;
	const bool bWasSearching = StopRouteJob();
	const bool bHadRoute = !RoutePoints.IsEmpty();
	ClearRouteResult();
	GuidedStep = 0;
	bLastFindRouteFailed = false;
	if (bHadRoute || bWasSearching)
	{
		SetStatusMessage(TEXT("Layout changed. Press Find Route to update the route."), ENavigationMessageKind::Warning);
		OnGuidedStepChanged.Broadcast();
	}
}

bool UNavigationPanelWidget::RefreshSelectedPlaces()
{
	if (!bEndpointsNeedRefresh) return true;
	bEndpointsNeedRefresh = false;
	auto Refresh = [this](FSelectedEndpoint& Endpoint)
	{
		if (!Endpoint.bValid) return;
		const auto* Building = Generator ? Generator->FindBuildingById(Endpoint.BuildingId) : nullptr;
		if (!Building) { Endpoint = {}; return; }
		if (Endpoint.bSetViaDropdown && Endpoint.Point.Floor != OutdoorFloor)
		{
			bool bFound = false;
			if (Interior && Interior->GetLoadedBuildingId() == Endpoint.BuildingId)
			{
				if (const auto* Room = Interior->GetRoomById(Endpoint.RoomId); Room && Room->IsClosedLoop())
				{
					const FVector2D Center = ComputePolygonCentroid(Room->GetFootprint());
					bFound = FNavigationRoutePlanner::FindRoomDestination(*Generator, *Interior, Endpoint.BuildingId,
						Room->FloorLevel, Room->GetFootprint(), Endpoint.Point.Position, &Center);
					Endpoint.Point.Floor = Room->FloorLevel;
					Endpoint.RoomOptionLabel = FString::Printf(TEXT("%s (Floor %d)"), *Room->RoomName, Room->FloorLevel);
				}
			}
			// Interior is named in the condition as well as used in the initializer: the static analyser does not see through the conditional.
			else if (const auto Data = Interior ? Generator->GetInteriorDataForBuilding(Endpoint.BuildingId) : nullptr; Data && Interior)
			{
				const TArray<TSharedPtr<FJsonValue>>* Floors = nullptr;
				if (Data->TryGetArrayField(TEXT("floors"), Floors)) for (const auto& Value : *Floors)
				{
					const auto Floor = Value->AsObject(); if (!Floor) continue;
					int32 Level = 1; Floor->TryGetNumberField(TEXT("level"), Level);
					const TArray<TSharedPtr<FJsonValue>>* Rooms = nullptr;
					if (Floor->TryGetArrayField(TEXT("rooms"), Rooms)) for (const auto& RoomValue : *Rooms)
					{
						const auto Room = RoomValue->AsObject(); FString Id, Name; bool bClosed = true;
						if (!Room || !Room->TryGetStringField(TEXT("id"), Id) || Id != Endpoint.RoomId) continue;
						Room->TryGetBoolField(TEXT("closed"), bClosed); if (!bClosed) continue;
						const TArray<TSharedPtr<FJsonValue>>* Points = nullptr; TArray<FVector2D> Polygon;
						if (Room->TryGetArrayField(TEXT("footprint"), Points)) for (const auto& P : *Points)
						{
							const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
							if (P->TryGetArray(XY) && XY->Num() >= 2) Polygon.Add({ (*XY)[0]->AsNumber(), (*XY)[1]->AsNumber() });
						}
						const FVector2D Center = ComputePolygonCentroid(Polygon);
						if (!FNavigationRoutePlanner::FindRoomDestination(*Generator, *Interior, Endpoint.BuildingId,
							Level, Polygon, Endpoint.Point.Position, &Center)) continue;
						Endpoint.Point.Floor = Level;
						Room->TryGetStringField(TEXT("name"), Name); if (Name.IsEmpty()) Name = Id;
						Endpoint.RoomOptionLabel = FString::Printf(TEXT("%s (Floor %d)"), *Name, Level); bFound = true; break;
					}
					if (bFound) break;
				}
			}
			if (!bFound) { Endpoint = {}; return; }
		}
		else if (Endpoint.bSetViaDropdown)
		{
			const auto Doors = Interior && Interior->GetLoadedBuildingId() == Endpoint.BuildingId
				? Interior->GetEntrances() : Generator->GetStoredEntrances(Endpoint.BuildingId);
			const auto* Door = Doors.FindByPredicate([&](const FInteriorEntrance& D) { return D.Id == Endpoint.RoomId && D.Kind == EInteriorEntranceKind::Main; });
			const auto& Outline = Building->GetFootprint();
			if (!Door || !Outline.IsValidIndex(Door->EdgeIndex) || Generator->GetScale() <= KINDA_SMALL_NUMBER) { Endpoint = {}; return; }
			const auto A = Outline[Door->EdgeIndex], B = Outline[(Door->EdgeIndex + 1) % Outline.Num()];
			const auto Direction = (B - A).GetSafeNormal(); const FVector2D Normal(Direction.Y, -Direction.X);
			const auto Middle = A + Direction * ((Door->Offset + Door->Width * 0.5) / Generator->GetScale());
			Endpoint.Point.Position = Middle + Normal * (100.0 / Generator->GetScale());
			if (IsInsideOrOnFootprint(Endpoint.Point.Position, Outline)) Endpoint.Point.Position = Middle - Normal * (100.0 / Generator->GetScale());
		}
		StampPoint(Endpoint.Point, Endpoint.BuildingId);
	};
	auto RefreshRequired = [&](FSelectedEndpoint& Selection, const FString& Role)
	{
		auto Refreshed = Selection;
		Refresh(Refreshed);
		if (Selection.bValid && !Refreshed.bValid)
		{
			bEndpointsNeedRefresh = true;
			SetStatusMessage(FString::Printf(TEXT("%s (%s) is unavailable. Replace or remove it before finding a route."),
				*Role, *EndpointPhrase(Selection)), ENavigationMessageKind::Warning);
			return false;
		}
		Selection = MoveTemp(Refreshed);
		return true;
	};
	if (!RefreshRequired(Start, TEXT("Start")) || !RefreshRequired(Destination, TEXT("Destination"))) return false;
	for (int32 I = Stops.Num() - 1; I >= 0; --I)
	{
		auto Endpoint = StopAsEndpoint(Stops[I]);
		if (!RefreshRequired(Endpoint, FString::Printf(TEXT("Stop %d"), I + 1))) return false;
		Stops[I].Point = Endpoint.Point;
		Stops[I].RoomOptionLabel = Endpoint.RoomOptionLabel;
	}
	return true;
}

bool UNavigationPanelWidget::CanClearRoute() const
{
	return IsSelectingPoint() || Start.bValid || Destination.bValid || !Stops.IsEmpty() || !RoutePoints.IsEmpty();
}

FString UNavigationPanelWidget::GetDisplayStatusMessage() const
{
	if (RouteJob)
	{
		return FString::Printf(TEXT("Finding route... %d%%\n%s"), FMath::RoundToInt(GetFindRouteProgress() * 100.f), *GetFindRouteProgressText());
	}
	if (!StatusMessage.IsEmpty())
	{
		return StatusMessage;
	}
	switch (SelectionMode)
	{
	case ENavigationSelectionMode::Start:
		return bOutdoorMode ? TEXT("Click the ground next to a building to set the start.")
			: TEXT("Select a start indoors, or in the blue outside area on Floor 1.");
	case ENavigationSelectionMode::Destination:
		return bOutdoorMode ? TEXT("Click the ground next to a building to set the destination.")
			: TEXT("Select a destination indoors, or in the blue outside area on Floor 1.");
	case ENavigationSelectionMode::Stop:
		return bOutdoorMode ? TEXT("Click the ground next to a building to add a stop. Click more places for more stops; press Esc when done.")
			: TEXT("Click places indoors, or in the blue outside area on Floor 1, to add stops. Press Esc when done.");
	default:
		return TEXT("Choose Set Start or Set Destination.");
	}
}

FString UNavigationPanelWidget::BuildingDisplayName(const FString& BuildingId) const
{
	const APolyBuilding* Building = Generator ? Generator->FindBuildingById(BuildingId) : nullptr;
	return (Building && !Building->BuildingName.IsEmpty()) ? Building->BuildingName : BuildingId;
}

FString UNavigationPanelWidget::EndpointPhrase(const FSelectedEndpoint& Endpoint) const
{
	const FString Building = BuildingDisplayName(Endpoint.BuildingId);
	if (Endpoint.Point.Floor == OutdoorFloor)
	{
		// A main door picked from the list already names its building ("T1 · Main door").
		if (Endpoint.bSetViaDropdown && !Endpoint.RoomOptionLabel.IsEmpty())
		{
			return Endpoint.RoomOptionLabel;
		}
		return Building.IsEmpty() ? FString(TEXT("a spot outside")) : FString::Printf(TEXT("a spot outside near %s"), *Building);
	}
	const FString Floor = FString::Printf(TEXT("Floor %d"), Endpoint.Point.Floor);
	if (Endpoint.bSetViaDropdown && !Endpoint.RoomOptionLabel.IsEmpty())
	{
		// The label is "Room name (Floor N)"; the floor is said after the building instead.
		FString Room = Endpoint.RoomOptionLabel;
		const int32 FloorStart = Room.Find(TEXT(" (Floor "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (FloorStart != INDEX_NONE)
		{
			Room.LeftInline(FloorStart);
		}
		return Building.IsEmpty() ? FString::Printf(TEXT("%s, %s"), *Room, *Floor) : FString::Printf(TEXT("%s in %s, %s"), *Room, *Building, *Floor);
	}
	return Building.IsEmpty() ? FString::Printf(TEXT("a spot on %s"), *Floor) : FString::Printf(TEXT("a spot on %s of %s"), *Floor, *Building);
}

TArray<UNavigationPanelWidget::FNavigationRouteStep> UNavigationPanelWidget::BuildRouteSteps() const
{
	TArray<FNavigationRouteStep> Steps;
	if (RoutePoints.IsEmpty())
	{
		return Steps;
	}
	// Each step is at a point of the route (PointIndex): where the person is when it applies.
	auto AddStep = [&Steps](const FString& Text, int32 PointIndex)
	{
		FNavigationRouteStep Step;
		Step.Text = Text;
		Step.PointIndex = PointIndex;
		Steps.Add(MoveTemp(Step));
	};
	if (Start.bValid)
	{
		AddStep(FString::Printf(TEXT("Start at %s."), *EndpointPhrase(Start)), 0);
	}
	// Leg by leg: what happens between the points of a leg, and at its end (a stop) that the stop is reached. A route that was not
	// planned in legs is taken as one.
	const int32 LegCount = RouteLegs.IsEmpty() ? 1 : RouteLegs.Num();
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		const int32 FirstPoint = RouteLegs.IsEmpty() ? 0 : RouteLegs[Leg].FirstPointIndex;
		const int32 LastPoint = FMath::Min(RouteLegs.IsEmpty() ? RoutePoints.Num() - 1 : RouteLegs[Leg].LastPointIndex, RoutePoints.Num() - 1);
		const auto Instructions = NavigationInstructions::Build(RoutePoints,
			[this](const FNavigationRoutePoint& Point)
			{
				if (Point.bHasWorldPosition) return Point.WorldPosition;
				auto Stamped = Point; StampPoint(Stamped, Point.BuildingId.IsEmpty() && Interior ? Interior->GetLoadedBuildingId() : Point.BuildingId);
				return Stamped.bHasWorldPosition ? Stamped.WorldPosition : FVector(Point.Position, 0);
			},
			[this](const FString& Id) { return BuildingDisplayName(Id.IsEmpty() && Interior ? Interior->GetLoadedBuildingId() : Id); }, FirstPoint, LastPoint);
		for (int32 I = 0; I < Instructions.Num(); ++I)
		{
			const auto& Instruction = Instructions[I];
			// The start already occupies this point. Include the initial walk there rather than adding
			// a second instruction at the same position with an empty highlighted stretch.
			if (Leg == 0 && I == 0 && Start.bValid && Instruction.Kind == ENavigationInstructionKind::Straight && Instruction.PointIndex == 0)
				Steps[0].Text += TEXT(" ") + Instruction.Text;
			else AddStep(Instruction.Text, Instruction.PointIndex);
		}
		if (Leg + 1 < LegCount && !RouteLegs.IsEmpty())
		{
			AddStep(FString::Printf(TEXT("Stop %d: %s."), Leg + 1, *RouteLegs[Leg].ToLabel), LastPoint);
		}
	}
	if (Destination.bValid)
	{
		AddStep(FString::Printf(TEXT("Arrive at %s."), *EndpointPhrase(Destination)), RoutePoints.Num() - 1);
	}
	if (!Steps.IsEmpty())
	{
		Steps.Last().bFinal = true;
	}
	return Steps;
}

void UNavigationPanelWidget::StartGuidance()
{
	GuidedStep = 0;
	bStepByStep = true;
	OnGuidedStepChanged.Broadcast();
	BroadcastStateChanged();
}

void UNavigationPanelWidget::SetStepByStep(bool bOn)
{
	if (bStepByStep == bOn)
	{
		return;
	}
	bStepByStep = bOn;
	OnGuidedStepChanged.Broadcast();
	BroadcastStateChanged();
}

bool UNavigationPanelWidget::MoveGuidedStep(int32 Offset)
{
	const int32 Count = BuildRouteSteps().Num();
	const int32 NewStep = GuidedStep + Offset;
	if (!bStepByStep || NewStep < 0 || NewStep >= Count)
	{
		return false;
	}
	GuidedStep = NewStep;
	OnGuidedStepChanged.Broadcast();
	BroadcastStateChanged();
	return true;
}

bool UNavigationPanelWidget::NextStep()
{
	return MoveGuidedStep(1);
}

bool UNavigationPanelWidget::PreviousStep()
{
	return MoveGuidedStep(-1);
}

bool UNavigationPanelWidget::IsStepByStep() const
{
	return bStepByStep && !RoutePoints.IsEmpty();
}

int32 UNavigationPanelWidget::GetGuidedStepIndex() const
{
	return FMath::Max(0, GuidedStep);
}

const FNavigationRoutePoint* UNavigationPanelWidget::GetGuidedPoint() const
{
	if (!IsStepByStep())
	{
		return nullptr;
	}
	const TArray<FNavigationRouteStep> Steps = BuildRouteSteps();
	if (!Steps.IsValidIndex(GuidedStep) || !RoutePoints.IsValidIndex(Steps[GuidedStep].PointIndex))
	{
		return nullptr;
	}
	return &RoutePoints[Steps[GuidedStep].PointIndex];
}

FNavigationRouteFocus UNavigationPanelWidget::GetGuidedFocus() const
{
	FNavigationRouteFocus Focus;
	if (!IsStepByStep())
	{
		return Focus;
	}
	// The stretch of the route from where this step is to where the next one is (the last step is just a place).
	const TArray<FNavigationRouteStep> Steps = BuildRouteSteps();
	if (!Steps.IsValidIndex(GuidedStep) || !RoutePoints.IsValidIndex(Steps[GuidedStep].PointIndex))
	{
		return Focus;
	}
	Focus.FirstPointIndex = Steps[GuidedStep].PointIndex;
	int32 Next = GuidedStep + 1;
	// An entry or door announcement can share a point with the walking instruction after it.
	// Highlight the next actual stretch rather than making every segment faint at that location.
	while (Steps.IsValidIndex(Next) && Steps[Next].PointIndex <= Focus.FirstPointIndex) ++Next;
	Focus.LastPointIndex = Steps.IsValidIndex(Next) && RoutePoints.IsValidIndex(Steps[Next].PointIndex)
		? Steps[Next].PointIndex : Focus.FirstPointIndex;
	return Focus;
}

FString UNavigationPanelWidget::BuildRouteDetails() const
{
	const TArray<FNavigationRouteStep> Steps = BuildRouteSteps();
	FString Details;
	if (Steps.IsEmpty())
	{
		// No route yet: show what has been chosen so far.
		if (Start.bValid)
		{
			Details += FString::Printf(TEXT("Start: %s\n"), *EndpointPhrase(Start));
		}
		for (int32 StopIndex = 0; StopIndex < Stops.Num(); ++StopIndex)
		{
			Details += FString::Printf(TEXT("Stop %d: %s\n"), StopIndex + 1, *EndpointPhrase(StopAsEndpoint(Stops[StopIndex])));
		}
		if (Destination.bValid)
		{
			Details += FString::Printf(TEXT("Destination: %s\n"), *EndpointPhrase(Destination));
		}
	}
	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		Details += FString::Printf(TEXT("Step %d: %s\n"), Index + 1, *Steps[Index].Text);
	}
	Details.RemoveFromEnd(TEXT("\n"));
	return Details;
}

FLinearColor UNavigationPanelWidget::GetStatusColor() const
{
	if (StatusMessage.IsEmpty() || RouteJob)
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
	// exact Visible enum value; only the Navigation dock panel (PointerOverPanelCallback, or
	// HitTestBorder - see SetPanelBorderForHitTest) should own pointer input.
	if (PointerOverPanelCallback)
	{
		return IsVisible() && FSlateApplication::IsInitialized() && PointerOverPanelCallback(FSlateApplication::Get().GetCursorPos());
	}
	return IsVisible() && HitTestBorder && FSlateApplication::IsInitialized()
		&& HitTestBorder->GetCachedGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos());
}
