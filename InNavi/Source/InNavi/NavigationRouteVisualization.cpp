#include "NavigationRouteVisualization.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

namespace
{
	constexpr float RouteDisplayLift = 15.f;
	const FColor NavigationRouteColor(255, 140, 0);
	const FColor NavigationTransitionColor(70, 210, 255);

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
	FVector2D BuildingOffset, float Scale, float FloorHeight, int32 ActiveFloor)
{
	FNavigation3DView View;
	auto WorldPoint = [&](const FNavigationRoutePoint& Point)
	{
		return NavigationWorldPoint(Point, BuildingOffset, Scale, FloorHeight);
	};
	for (int32 I = 1; I < Route.Num(); ++I)
	{
		const FNavigationRoutePoint& From = Route[I - 1];
		const FNavigationRoutePoint& To = Route[I];
		if (From.Floor == To.Floor)
		{
			if (NavigationPointVisible(From.Floor, ActiveFloor)
				&& !WorldPoint(From).Equals(WorldPoint(To), KINDA_SMALL_NUMBER))
			{
				View.Segments.Add({ WorldPoint(From), WorldPoint(To), false });
			}
			continue;
		}
		const bool bExterior = From.Floor == 0 || To.Floor == 0;
		// Show a local connector indicator, not a straight path through hidden floors/walls.
		for (bool bDeparting : { true, false })
		{
			const FNavigationRoutePoint& Local = bDeparting ? From : To;
			const FNavigationRoutePoint& Other = bDeparting ? To : From;
			if (!NavigationPointVisible(Local.Floor, ActiveFloor))
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
				Text = From.Floor == 0 ? TEXT("Enter building - Floor 1") : TEXT("Exit building - Outside");
			}
			else
			{
				const FString Connector = NavigationConnectorLabel(To);
				Text = bDeparting
					? FString::Printf(TEXT("%s to %s"), *Connector, *NavigationFloorLabel(Other.Floor))
					: FString::Printf(TEXT("Arrive via %s from %s"), *Connector, *NavigationFloorLabel(Other.Floor));
				const FVector Upper = Position + FVector(0, 0, 70.f);
				const bool bUpward = To.Floor > From.Floor;
				View.Segments.Add({ bUpward ? Position : Upper, bUpward ? Upper : Position, true });
			}
			View.Labels.Add({ Position + FVector(0, 0, 95.f), Text, NavigationTransitionColor });
		}
	}
	if (Start && NavigationPointVisible(Start->Floor, ActiveFloor))
	{
		View.Start = WorldPoint(*Start);
		View.Labels.Add({ View.Start.GetValue() + FVector(0, 0, 55), TEXT("Start"), FColor::Green });
	}
	if (Destination && NavigationPointVisible(Destination->Floor, ActiveFloor))
	{
		View.Destination = WorldPoint(*Destination);
		View.Labels.Add({ View.Destination.GetValue() + FVector(0, 0, 55), TEXT("Destination"), FColor::Red });
	}
	return View;
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
		const FLinearColor Color(Segment.bTransition ? NavigationTransitionColor : NavigationRouteColor);
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
