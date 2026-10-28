#include "NavigationRouteVisualization.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

namespace
{
	constexpr float RouteDisplayLift = 15.f;
	const FColor NavigationRouteColor(255, 140, 0);
	const FColor NavigationTransitionColor(70, 210, 255);
	const FColor NavigationLegColors[] = {
		FColor(255, 140, 0), FColor(25, 158, 242), FColor(184, 82, 235), FColor(26, 184, 107), FColor(242, 179, 20)
	};

	bool NavigationPointVisible(int32 Floor, int32 ActiveFloor)
	{
		return Floor == ActiveFloor || (ActiveFloor == 1 && Floor == 0);
	}

	FVector NavigationWorldPoint(const FNavigationRoutePoint& Point, FVector2D Offset, float Scale, float Height)
	{
		if (Point.bHasWorldPosition)
		{
			return Point.WorldPosition + FVector(0, 0, RouteDisplayLift);
		}
		const FVector2D XY = (Point.Position + Offset) * Scale;
		return FVector(XY.X, XY.Y, FMath::Max(0, Point.Floor - 1) * Height + RouteDisplayLift);
	}

	FString NavigationFloorLabel(int32 Floor)
	{
		return Floor == 0 ? TEXT("Outside") : FString::Printf(TEXT("Floor %d"), Floor);
	}

	FString NavigationConnectorLabel(const FNavigationRoutePoint& Point)
	{
		FString Type;
		switch (Point.ConnectorType)
		{
		case EInteriorNavNodeType::Elevator: Type = TEXT("Elevator"); break;
		case EInteriorNavNodeType::Stairs: Type = TEXT("Stairs"); break;
		case EInteriorNavNodeType::Escalator: Type = TEXT("Escalator"); break;
		default: Type = TEXT("Connector"); break;
		}
		return Point.ConnectorId.IsEmpty() ? Type : Type + TEXT(" ") + Point.ConnectorId;
	}
}

UNavigationRouteVisualization::UNavigationRouteVisualization()
{
	PrimaryComponentTick.bCanEverTick = true;
	DefaultLifeTime = -1.f;
	bCalculateAccurateBounds = true;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

FNavigation3DView UNavigationRouteVisualization::BuildView(const TArray<FNavigationRoutePoint>& Route,
	const FNavigationRoutePoint* Start, const FNavigationRoutePoint* Destination,
	FVector2D BuildingOffset, float Scale, float FloorHeight, int32 ActiveFloor, bool bGuidanceVisible, const FString& ActiveBuildingId,
	const TArray<FNavigationStop>* Stops, const TArray<FNavigationRouteLeg>* RouteLegs)
{
	FNavigation3DView View;
	if (!bGuidanceVisible) return View;
	auto Visible = [&](const FNavigationRoutePoint& Point) { return IsPointVisible(Point, ActiveBuildingId, ActiveFloor); };
	auto WorldPoint = [&](const FNavigationRoutePoint& Point)
	{
		return NavigationWorldPoint(Point, BuildingOffset, Scale, FloorHeight);
	};
	// The leg a stretch of the route belongs to: the one whose points it ends in.
	auto LegOf = [RouteLegs](int32 SegmentEndPointIndex) -> int32
	{
		if (!RouteLegs) return INDEX_NONE;
		for (int32 LegIndex = 0; LegIndex < RouteLegs->Num(); ++LegIndex)
		{
			const FNavigationRouteLeg& Leg = (*RouteLegs)[LegIndex];
			if (SegmentEndPointIndex > Leg.FirstPointIndex && SegmentEndPointIndex <= Leg.LastPointIndex) return LegIndex;
		}
		return INDEX_NONE;
	};
	for (int32 I = 1; I < Route.Num(); ++I)
	{
		const FNavigationRoutePoint& From = Route[I - 1];
		const FNavigationRoutePoint& To = Route[I];
		if (From.Floor != 0 && To.Floor != 0 && From.BuildingId != To.BuildingId)
		{
			for (bool bDeparting : { true, false })
			{
				const auto& Local = bDeparting ? From : To;
				const auto& Other = bDeparting ? To : From;
				if (Visible(Local)) View.Labels.Add({ WorldPoint(Local) + FVector(0, 0, 95),
					FString::Printf(TEXT("Link door %s %s - Floor %d"), bDeparting ? TEXT("to") : TEXT("from"), *Other.BuildingId, Other.Floor), NavigationTransitionColor });
			}
			continue;
		}
		if (From.Floor == To.Floor)
		{
			if (Visible(From) && Visible(To)
				&& !WorldPoint(From).Equals(WorldPoint(To), KINDA_SMALL_NUMBER))
			{
				View.Segments.Add({ WorldPoint(From), WorldPoint(To), false, LegOf(I) });
			}
			continue;
		}
		const bool bExterior = From.Floor == 0 || To.Floor == 0;
		// Show a local connector indicator, not a straight path through hidden floors/walls.
		for (bool bDeparting : { true, false })
		{
			const FNavigationRoutePoint& Local = bDeparting ? From : To;
			const FNavigationRoutePoint& Other = bDeparting ? To : From;
			if (!Visible(Local))
			{
				continue;
			}
			// The outdoor and indoor sides coincide at ground level: one label suffices.
			if (bExterior && !bDeparting)
			{
				continue;
			}
			const FVector Position = WorldPoint(Local);
			FString Text;
			if (bExterior)
			{
				Text = From.Floor == 0 ? FString::Printf(TEXT("Enter building %s - Floor 1"), *To.BuildingId)
					: FString::Printf(TEXT("Exit building %s - Outside"), *From.BuildingId);
			}
			else
			{
				const FString Connector = NavigationConnectorLabel(To);
				Text = bDeparting
					? FString::Printf(TEXT("%s to %s"), *Connector, *NavigationFloorLabel(Other.Floor))
					: FString::Printf(TEXT("Arrive via %s from %s"), *Connector, *NavigationFloorLabel(Other.Floor));
				const FVector Upper = Position + FVector(0, 0, 70.f);
				const bool bUpward = To.Floor > From.Floor;
				View.Segments.Add({ bUpward ? Position : Upper, bUpward ? Upper : Position, true, LegOf(I) });
			}
			View.Labels.Add({ Position + FVector(0, 0, 95.f), Text, NavigationTransitionColor });
		}
	}
	if (Start && Visible(*Start))
	{
		View.Start = WorldPoint(*Start);
		View.Labels.Add({ View.Start.GetValue() + FVector(0, 0, 55), TEXT("Start"), FColor::Green });
	}
	if (Destination && Visible(*Destination))
	{
		View.Destination = WorldPoint(*Destination);
		View.Labels.Add({ View.Destination.GetValue() + FVector(0, 0, 55), TEXT("Destination"), FColor::Red });
	}
	if (Stops)
	{
		for (int32 StopIndex = 0; StopIndex < Stops->Num(); ++StopIndex)
		{
			// Where the journey reaches the stop is where its leg ends; before there is a route, the point of the stop itself.
			const FNavigationRoutePoint* Reached = RouteLegs && RouteLegs->IsValidIndex(StopIndex) && Route.IsValidIndex((*RouteLegs)[StopIndex].LastPointIndex)
				? &Route[(*RouteLegs)[StopIndex].LastPointIndex] : &(*Stops)[StopIndex].Point;
			if (!Visible(*Reached)) continue;
			const FVector Position = WorldPoint(*Reached);
			const FColor Color = LegColor(StopIndex);
			View.Stops.Add({ Position, StopIndex + 1, Color });
			View.Labels.Add({ Position + FVector(0, 0, 55), FString::Printf(TEXT("Stop %d"), StopIndex + 1), Color });
		}
	}
	return View;
}

FColor UNavigationRouteVisualization::LegColor(int32 LegIndex)
{
	return NavigationLegColors[FMath::Max(0, LegIndex) % UE_ARRAY_COUNT(NavigationLegColors)];
}

FLinearColor UNavigationRouteVisualization::LegColor2D(int32 LegIndex)
{
	const FColor Color = LegColor(LegIndex);
	return FLinearColor(Color.R / 255.f, Color.G / 255.f, Color.B / 255.f, 1.f);
}

FNavigation3DView UNavigationRouteVisualization::BuildCampusView(const TArray<FNavigationRoutePoint>& Route,
	const FNavigationRoutePoint* Start, const FNavigationRoutePoint* Destination, bool bGuidanceVisible,
	const TArray<FNavigationStop>* Stops, const TArray<FNavigationRouteLeg>* RouteLegs)
{
	// Floor 0 is "outside": the outdoor stretches and the entering/leaving labels are what shows. No building is open, so no
	// building is active and the offset, scale and floor height are not used (points carry their world positions).
	FNavigation3DView View = BuildView(Route, Start, Destination, FVector2D::ZeroVector, 1.f, 1.f, 0, bGuidanceVisible, FString(), Stops, RouteLegs);
	if (!bGuidanceVisible)
	{
		return View;
	}
	// An endpoint inside a building is not drawn by the view above (its floor is not shown); mark where it is.
	auto MarkIndoor = [&View](const FNavigationRoutePoint* Point, TOptional<FVector>& OutMarker, const TCHAR* Text, const FColor& Color)
	{
		if (Point && Point->Floor != 0 && Point->bHasWorldPosition)
		{
			OutMarker = Point->WorldPosition + FVector(0, 0, RouteDisplayLift);
			View.Labels.Add({ OutMarker.GetValue() + FVector(0, 0, 55), Text, Color });
		}
	};
	MarkIndoor(Start, View.Start, TEXT("Start"), FColor::Green);
	MarkIndoor(Destination, View.Destination, TEXT("Destination"), FColor::Red);
	// Likewise a stop inside a building, at the point where the route reaches it.
	if (Stops && RouteLegs)
	{
		for (int32 StopIndex = 0; StopIndex < Stops->Num() && RouteLegs->IsValidIndex(StopIndex); ++StopIndex)
		{
			const int32 PointIndex = (*RouteLegs)[StopIndex].LastPointIndex;
			if (!Route.IsValidIndex(PointIndex) || Route[PointIndex].Floor == 0 || !Route[PointIndex].bHasWorldPosition) continue;
			const FVector Position = Route[PointIndex].WorldPosition + FVector(0, 0, RouteDisplayLift);
			const FColor Color = LegColor(StopIndex);
			View.Stops.Add({ Position, StopIndex + 1, Color });
			View.Labels.Add({ Position + FVector(0, 0, 55), FString::Printf(TEXT("Stop %d"), StopIndex + 1), Color });
		}
	}
	return View;
}

bool UNavigationRouteVisualization::IsPointVisible(const FNavigationRoutePoint& Point, const FString& ActiveBuildingId, int32 ActiveFloor)
{
	return NavigationPointVisible(Point.Floor, ActiveFloor) && (Point.Floor == 0 || ActiveBuildingId.IsEmpty()
		|| Point.BuildingId.IsEmpty() || Point.BuildingId == ActiveBuildingId);
}

TArray<FNavigationDirectionArrow2D> UNavigationRouteVisualization::BuildDirectionArrows2D(FVector2D From, FVector2D To)
{
	TArray<FNavigationDirectionArrow2D> Arrows;
	const FVector2D Delta = To - From;
	const double Length = Delta.Size();
	if (!FMath::IsFinite(Length) || Length < 12.0) return Arrows;
	const FVector2D Direction = Delta / Length;
	const FVector2D Side(-Direction.Y, Direction.X);
	const int32 Count = FMath::Clamp(FMath::CeilToInt(Length / 80.0), 1, 32);
	const double HalfLength = FMath::Min(12.0, Length / (Count * 4.0));
	const double HeadSize = FMath::Min(8.0, HalfLength);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector2D Center = FMath::Lerp(From, To, (Index + 0.5) / Count);
		const FVector2D Tip = Center + Direction * HalfLength;
		Arrows.Add({ Tip, Tip - Direction * HeadSize + Side * HeadSize * 0.65,
			Tip - Direction * HeadSize - Side * HeadSize * 0.65 });
	}
	return Arrows;
}

void UNavigationRouteVisualization::ClearRoute()
{
	Flush();
	for (UTextRenderComponent* Label : RouteLabels)
	{
		if (Label)
		{
			Label->DestroyComponent();
		}
	}
	RouteLabels.Reset();
}

void UNavigationRouteVisualization::ShowView(const FNavigation3DView& View)
{
	ClearRoute();
	for (const FNavigationDisplaySegment& Segment : View.Segments)
	{
		const FLinearColor Color(Segment.bTransition ? NavigationTransitionColor
			: (Segment.RouteLegIndex == INDEX_NONE ? NavigationRouteColor : LegColor(Segment.RouteLegIndex)));
		DrawLine(Segment.From, Segment.To, Color, 0, 6.f, -1.f);
		const float Length = FVector::Distance(Segment.From, Segment.To);
		const FVector Direction = (Segment.To - Segment.From).GetSafeNormal();
		const int32 Arrows = FMath::Clamp(FMath::CeilToInt(Length / 250.f), 1, 32);
		for (int32 I = 0; I < Arrows; ++I)
		{
			const FVector Center = FMath::Lerp(Segment.From, Segment.To, (I + 0.5f) / Arrows);
			const float HalfLength = FMath::Min(30.f, Length / (Arrows * 4.f));
			const FVector Tip = Center + Direction * HalfLength;
			FVector Side = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
			if (Side.IsNearlyZero()) Side = FVector::RightVector;
			const float HeadSize = FMath::Min(20.f, HalfLength);
			DrawLine(Center - Direction * HalfLength, Tip, Color, 0, 4.f, -1.f);
			DrawLine(Tip, Tip - Direction * HeadSize + Side * HeadSize * 0.5f, Color, 0, 4.f, -1.f);
			DrawLine(Tip, Tip - Direction * HeadSize - Side * HeadSize * 0.5f, Color, 0, 4.f, -1.f);
		}
	}
	if (View.Start.IsSet()) DrawSphere(View.Start.GetValue(), 25.f, 12, FLinearColor::Green, -1.f, 0, 3.f);
	if (View.Destination.IsSet()) DrawSphere(View.Destination.GetValue(), 25.f, 12, FLinearColor::Red, -1.f, 0, 3.f);
	for (const FNavigationDisplayStop& Stop : View.Stops) DrawSphere(Stop.Position, 20.f, 12, FLinearColor(Stop.Color), -1.f, 0, 3.f);
	if (!GetOwner()) return;
	for (const FNavigationDisplayLabel& Input : View.Labels)
	{
		UTextRenderComponent* Label = NewObject<UTextRenderComponent>(GetOwner());
		Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Label->SetText(FText::FromString(Input.Text));
		Label->SetTextRenderColor(Input.Color);
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetWorldSize(24.f);
		Label->RegisterComponent();
		Label->SetWorldLocation(Input.Position);
		RouteLabels.Add(Label);
	}
}

void UNavigationRouteVisualization::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || RouteLabels.IsEmpty()) return;
	FVector CameraPosition;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraPosition, CameraRotation);
	for (UTextRenderComponent* Label : RouteLabels)
	{
		Label->SetWorldRotation((CameraPosition - Label->GetComponentLocation()).Rotation());
	}
}

void UNavigationRouteVisualization::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	ClearRoute();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
