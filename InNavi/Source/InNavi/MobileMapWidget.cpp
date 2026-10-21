#include "MobileMapWidget.h"

#include "MobileStyle.h"
#include "PolygonGeometryUtils.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

using namespace MobileStyle;

namespace
{
	// What the map's colours are: soft by kind of room, the way the prototype's plans are.
	FLinearColor RoomColour(const FString& Category, bool bAvailable)
	{
		FLinearColor Colour = StartStyle::Rgb(226, 232, 240);
		if (Category == TEXT("Learning")) Colour = StartStyle::Rgb(219, 234, 254);
		else if (Category == TEXT("Facilities")) Colour = StartStyle::Rgb(209, 250, 229);
		else if (Category == TEXT("Dining")) Colour = StartStyle::Rgb(254, 243, 199);
		return bAvailable ? Colour : Colour * 0.8f;
	}

	bool PointInPolygon(const FVector2D& P, const TArray<FVector2D>& Poly)
	{
		bool bInside = false;
		for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
		{
			if ((Poly[i].Y > P.Y) != (Poly[j].Y > P.Y) && P.X < (Poly[j].X - Poly[i].X) * (P.Y - Poly[i].Y) / (Poly[j].Y - Poly[i].Y) + Poly[i].X)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	FVector2D Centre(const TArray<FVector2D>& Poly)
	{
		FVector2D Sum = FVector2D::ZeroVector;
		for (const FVector2D& P : Poly)
		{
			Sum += P;
		}
		return Poly.IsEmpty() ? FVector2D::ZeroVector : Sum / Poly.Num();
	}

	// A filled polygon (an ear-clipped one, so an L-shaped room is fine) in widget coordinates.
	void FillPolygon(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const TArray<FVector2D>& Poly, const FLinearColor& Colour)
	{
		TArray<int32> Triangles;
		TriangulatePolygon(Poly, Triangles);
		if (Triangles.IsEmpty())
		{
			return;
		}
		TArray<FSlateVertex> Vertices;
		Vertices.Reserve(Poly.Num());
		const FColor Tint = Colour.ToFColor(true);
		for (const FVector2D& Point : Poly)
		{
			Vertices.AddZeroed();
			FSlateVertex& Vertex = Vertices.Last();
			Vertex.Position = FVector2f(Geometry.LocalToAbsolute(Point));
			Vertex.Color = Tint;
		}
		TArray<SlateIndex> Indices;
		for (const int32 Index : Triangles)
		{
			Indices.Add(static_cast<SlateIndex>(Index));
		}
		FSlateDrawElement::MakeCustomVerts(Out, Layer, FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"))->GetRenderingResource(), Vertices, Indices, nullptr, 0, 0);
	}

	void OutlinePolygon(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const TArray<FVector2D>& Poly, const FLinearColor& Colour, float Thickness)
	{
		TArray<FVector2D> Points = Poly;
		if (!Points.IsEmpty())
		{
			Points.Add(Poly[0]);
		}
		FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Colour, true, Thickness);
	}

	TArray<FVector2D> Circle(const FVector2D& Centre2, double Radius)
	{
		TArray<FVector2D> Points;
		for (int32 i = 0; i < 20; ++i)
		{
			const double Angle = i * UE_DOUBLE_TWO_PI / 20.0;
			Points.Add(Centre2 + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Points;
	}

	void DrawLabel(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FString& Text, const FVector2D& CentreAt, float PixelSize, const FLinearColor& Colour)
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", FMath::RoundToInt(PixelSize * StartStyle::PixelsToFont));
		FVector2D Size(Text.Len() * PixelSize * 0.55, PixelSize);
		if (FSlateApplication::IsInitialized())
		{
			Size = FVector2D(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font));
		}
		const FVector2D TopLeft = CentreAt - Size * 0.5;
		FSlateDrawElement::MakeText(Out, Layer, Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TopLeft))), Text, Font, ESlateDrawEffect::None, Colour);
	}

	// How tall the tilted view stands the plan, in design pixels.
	constexpr double SlabHeight = 7.0;
}

void UMobileMapWidget::SetSource(IMobileNavSource* InSource, const FString& InSiteId)
{
	Source = InSource;
	SiteId = InSiteId;
	CampusMap = Source ? Source->GetCampusMap(SiteId) : FMobileCampusMap();
}

void UMobileMapWidget::ShowLevel(const FString& BuildingId, int32 Level)
{
	bOverview = false;
	FMobileLevelMap Loaded;
	if (Source && Source->GetLevelMap(BuildingId, Level, Loaded))
	{
		LevelMap = MoveTemp(Loaded);
	}
	else
	{
		LevelMap = FMobileLevelMap();
	}
}

void UMobileMapWidget::ShowOverview()
{
	bOverview = true;
}

void UMobileMapWidget::SetRoute(const FMobileRoute& InRoute) { Route = InRoute; }
void UMobileMapWidget::ClearRoute() { Route = FMobileRoute(); }
void UMobileMapWidget::SetHighlightPlace(const FString& PlaceId) { HighlightPlaceId = PlaceId; }
void UMobileMapWidget::SetPosition(const FMobileRoutePoint& InPosition) { Position = InPosition; bHasPosition = true; }
void UMobileMapWidget::ClearPosition() { bHasPosition = false; }
void UMobileMapWidget::Set3D(bool bInShow3D) { bShow3D = bInShow3D; }

void UMobileMapWidget::SetFitInsets(double Top, double Bottom)
{
	InsetTop = Top;
	InsetBottom = Bottom;
}

int32 UMobileMapWidget::GetRoutePointsOnLevel() const
{
	int32 Count = 0;
	for (const FMobileRoutePoint& Point : Route.Points)
	{
		Count += (Point.BuildingId == LevelMap.BuildingId && Point.Level == LevelMap.Level) ? 1 : 0;
	}
	return Count;
}

FVector2D UMobileMapWidget::Project(const FVector2D& P) const
{
	if (!bShow3D)
	{
		return P;
	}
	// A tilted view: turned a quarter, and squashed, so the plan reads as seen from an angle.
	return FVector2D((P.X - P.Y) * 0.7071, (P.X + P.Y) * 0.7071 * 0.5);
}

FBox2D UMobileMapWidget::PlanBounds() const
{
	FBox2D Bounds(ForceInit);
	auto Add = [&](const TArray<FVector2D>& Poly)
	{
		for (const FVector2D& P : Poly)
		{
			Bounds += Project(P);
		}
	};
	if (bOverview)
	{
		for (const FMobileCampusBuilding& Building : CampusMap.Buildings) Add(Building.Outline);
		for (const FMobileCampusFeature& Feature : CampusMap.Features) Add(Feature.Outline);
	}
	else
	{
		Add(LevelMap.Outline);
	}
	return Bounds;
}

void UMobileMapWidget::ComputeFit(double WidgetWidth, double WidgetHeight, double& OutScale, FVector2D& OutOffset) const
{
	const FBox2D Bounds = PlanBounds();
	OutScale = 1.0;
	OutOffset = FVector2D::ZeroVector;
	if (!Bounds.bIsValid)
	{
		return;
	}
	const double Margin = 16.0;
	const double AvailW = FMath::Max(WidgetWidth - 2.0 * Margin, 10.0);
	const double AvailH = FMath::Max(WidgetHeight - InsetTop - InsetBottom - (bShow3D ? SlabHeight : 0.0), 10.0);
	const FVector2D Size = Bounds.GetSize();
	OutScale = FMath::Min(AvailW / FMath::Max(Size.X, 1e-6), AvailH / FMath::Max(Size.Y, 1e-6));
	// The plan is centred in the part that is not covered.
	OutOffset = FVector2D(Margin + (AvailW - Size.X * OutScale) * 0.5, InsetTop + (AvailH - Size.Y * OutScale) * 0.5) - Bounds.Min * OutScale;
}

FVector2D UMobileMapWidget::ToWidget(const FVector2D& PlanPoint, double WidgetWidth, double WidgetHeight) const
{
	double Scale;
	FVector2D Offset;
	ComputeFit(WidgetWidth, WidgetHeight, Scale, Offset);
	return Project(PlanPoint) * Scale + Offset;
}

TArray<FVector2D> UMobileMapWidget::ToWidgetPolygon(const TArray<FVector2D>& Plan, double WidgetWidth, double WidgetHeight, double Lift) const
{
	double Scale;
	FVector2D Offset;
	ComputeFit(WidgetWidth, WidgetHeight, Scale, Offset);
	TArray<FVector2D> Result;
	Result.Reserve(Plan.Num());
	for (const FVector2D& P : Plan)
	{
		Result.Add(Project(P) * Scale + Offset - FVector2D(0.0, Lift));
	}
	return Result;
}

FBox2D UMobileMapWidget::GetPlanExtentInWidget(double WidgetWidth, double WidgetHeight) const
{
	FBox2D Extent(ForceInit);
	auto Add = [&](const TArray<FVector2D>& Poly)
	{
		for (const FVector2D& P : ToWidgetPolygon(Poly, WidgetWidth, WidgetHeight, 0.0))
		{
			Extent += P;
		}
	};
	if (bOverview)
	{
		for (const FMobileCampusBuilding& Building : CampusMap.Buildings) Add(Building.Outline);
		for (const FMobileCampusFeature& Feature : CampusMap.Features) Add(Feature.Outline);
	}
	else
	{
		Add(LevelMap.Outline);
	}
	return Extent;
}

FString UMobileMapWidget::PlaceAt(const FVector2D& LocalPosition, double WidgetWidth, double WidgetHeight) const
{
	if (bOverview)
	{
		return FString();
	}
	for (const FMobileMapRoom& Room : LevelMap.Rooms)
	{
		if (!Room.PlaceId.IsEmpty() && PointInPolygon(LocalPosition, ToWidgetPolygon(Room.Polygon, WidgetWidth, WidgetHeight, 0.0)))
		{
			return Room.PlaceId;
		}
	}
	return FString();
}

FString UMobileMapWidget::BuildingAt(const FVector2D& LocalPosition, double WidgetWidth, double WidgetHeight) const
{
	if (bOverview)
	{
		for (const FMobileCampusBuilding& Building : CampusMap.Buildings)
		{
			if (PointInPolygon(LocalPosition, ToWidgetPolygon(Building.Outline, WidgetWidth, WidgetHeight, 0.0)))
			{
				return Building.BuildingId;
			}
		}
	}
	return FString();
}

void UMobileMapWidget::Tap(const FVector2D& LocalPosition, double WidgetWidth, double WidgetHeight)
{
	if (bOverview)
	{
		const FString Building = BuildingAt(LocalPosition, WidgetWidth, WidgetHeight);
		if (!Building.IsEmpty() && OnBuildingTapped)
		{
			OnBuildingTapped(Building);
		}
		return;
	}
	const FString Place = PlaceAt(LocalPosition, WidgetWidth, WidgetHeight);
	if (!Place.IsEmpty() && OnPlaceTapped)
	{
		OnPlaceTapped(Place);
	}
}

FReply UMobileMapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const bool bHit = bOverview ? !BuildingAt(Local, InGeometry.GetLocalSize().X, InGeometry.GetLocalSize().Y).IsEmpty() : !PlaceAt(Local, InGeometry.GetLocalSize().X, InGeometry.GetLocalSize().Y).IsEmpty();
	Tap(Local, InGeometry.GetLocalSize().X, InGeometry.GetLocalSize().Y);
	return bHit ? FReply::Handled() : FReply::Unhandled();
}

FReply UMobileMapWidget::NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	const FVector2D Local = InGeometry.AbsoluteToLocal(InGestureEvent.GetScreenSpacePosition());
	const bool bHit = bOverview ? !BuildingAt(Local, InGeometry.GetLocalSize().X, InGeometry.GetLocalSize().Y).IsEmpty() : !PlaceAt(Local, InGeometry.GetLocalSize().X, InGeometry.GetLocalSize().Y).IsEmpty();
	Tap(Local, InGeometry.GetLocalSize().X, InGeometry.GetLocalSize().Y);
	return bHit ? FReply::Handled() : FReply::Unhandled();
}

int32 UMobileMapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const double W = AllottedGeometry.GetLocalSize().X, H = AllottedGeometry.GetLocalSize().Y;
	const double Lift = bShow3D ? SlabHeight : 0.0;
	int32 Layer = LayerId;

	if (bOverview)
	{
		for (const FMobileCampusFeature& Feature : CampusMap.Features)
		{
			const TArray<FVector2D> Shape = ToWidgetPolygon(Feature.Outline, W, H, 0.0);
			const bool bWater = Feature.Name == TEXT("Pond");
			FillPolygon(OutDrawElements, Layer, AllottedGeometry, Shape, bWater ? StartStyle::Rgb(186, 220, 245) : StartStyle::Rgb(209, 235, 200));
			DrawLabel(OutDrawElements, Layer + 1, AllottedGeometry, Feature.Name, Centre(Shape), 11.f, Grey);
		}
		for (const FMobileCampusBuilding& Building : CampusMap.Buildings)
		{
			if (bShow3D)
			{
				FillPolygon(OutDrawElements, Layer + 1, AllottedGeometry, ToWidgetPolygon(Building.Outline, W, H, 0.0), StartStyle::Rgb(148, 163, 184));
			}
			const TArray<FVector2D> Top = ToWidgetPolygon(Building.Outline, W, H, Lift);
			FillPolygon(OutDrawElements, Layer + 2, AllottedGeometry, Top, StartStyle::Rgb(245, 247, 250));
			OutlinePolygon(OutDrawElements, Layer + 3, AllottedGeometry, Top, StartStyle::Rgb(100, 116, 139), 2.f);
			DrawLabel(OutDrawElements, Layer + 4, AllottedGeometry, Building.Name, Centre(Top), 15.f, Ink);
		}
		return Layer + 5;
	}
	if (LevelMap.Outline.IsEmpty())
	{
		return Layer;
	}

	// The floor: in the tilted view it stands on a slab.
	if (bShow3D)
	{
		FillPolygon(OutDrawElements, Layer, AllottedGeometry, ToWidgetPolygon(LevelMap.Outline, W, H, 0.0), StartStyle::Rgb(148, 163, 184));
	}
	const TArray<FVector2D> Floor = ToWidgetPolygon(LevelMap.Outline, W, H, Lift);
	FillPolygon(OutDrawElements, Layer + 1, AllottedGeometry, Floor, StartStyle::Rgb(248, 250, 252));
	OutlinePolygon(OutDrawElements, Layer + 3, AllottedGeometry, Floor, StartStyle::Rgb(100, 116, 139), 2.5f);

	// The rooms.
	for (const FMobileMapRoom& Room : LevelMap.Rooms)
	{
		const TArray<FVector2D> Shape = ToWidgetPolygon(Room.Polygon, W, H, Lift);
		const bool bHighlight = !Room.PlaceId.IsEmpty() && Room.PlaceId == HighlightPlaceId;
		FillPolygon(OutDrawElements, Layer + 2, AllottedGeometry, Shape, bHighlight ? StartStyle::Rgb(191, 219, 254) : RoomColour(Room.Category, Room.bAvailable));
		OutlinePolygon(OutDrawElements, Layer + 3, AllottedGeometry, Shape, bHighlight ? Blue : StartStyle::Rgb(148, 163, 184), bHighlight ? 3.f : 1.2f);
		DrawLabel(OutDrawElements, Layer + 4, AllottedGeometry, Room.Name, Centre(Shape), 10.f, Ink);
	}

	// The lifts and stairs.
	for (const FMobileMapConnector& Connector : LevelMap.Connectors)
	{
		const FVector2D At = ToWidgetPolygon({ Connector.Position }, W, H, Lift)[0];
		FillPolygon(OutDrawElements, Layer + 4, AllottedGeometry, Circle(At, 8.0), Connector.Kind == EMobileConnectorKind::Lift ? StartStyle::Rgb(30, 100, 240) : (Connector.Kind == EMobileConnectorKind::Stairs ? StartStyle::Rgb(100, 116, 139) : StartStyle::Rgb(13, 148, 136)));
		DrawLabel(OutDrawElements, Layer + 5, AllottedGeometry, Connector.Kind == EMobileConnectorKind::Lift ? TEXT("L") : (Connector.Kind == EMobileConnectorKind::Stairs ? TEXT("S") : TEXT("E")), At, 9.f, White);
	}

	// The way in, on the ground level.
	if (LevelMap.Level == 1)
	{
		const FVector2D Door = ToWidgetPolygon({ LevelMap.Entrance }, W, H, Lift)[0];
		FSlateDrawElement::MakeLines(OutDrawElements, Layer + 4, AllottedGeometry.ToPaintGeometry(), TArray<FVector2D>{ Door - FVector2D(10.0, 0.0), Door + FVector2D(10.0, 0.0) }, ESlateDrawEffect::None, StartStyle::Rgb(34, 197, 94), true, 5.f);
	}

	// The route, where it is on this level.
	TArray<FVector2D> RouteLine;
	auto FlushLine = [&]()
	{
		if (RouteLine.Num() >= 2)
		{
			FSlateDrawElement::MakeLines(OutDrawElements, Layer + 6, AllottedGeometry.ToPaintGeometry(), RouteLine, ESlateDrawEffect::None, Blue, true, 5.f);
		}
		RouteLine.Reset();
	};
	for (const FMobileRoutePoint& Point : Route.Points)
	{
		if (Point.BuildingId == LevelMap.BuildingId && Point.Level == LevelMap.Level)
		{
			RouteLine.Add(ToWidgetPolygon({ Point.Position }, W, H, Lift)[0]);
		}
		else
		{
			FlushLine();
		}
	}
	FlushLine();
	if (!Route.Points.IsEmpty())
	{
		const FMobileRoutePoint& Start = Route.Points[0];
		if (Start.BuildingId == LevelMap.BuildingId && Start.Level == LevelMap.Level)
		{
			const FVector2D At = ToWidgetPolygon({ Start.Position }, W, H, Lift)[0];
			FillPolygon(OutDrawElements, Layer + 7, AllottedGeometry, Circle(At, 7.0), White);
			FillPolygon(OutDrawElements, Layer + 8, AllottedGeometry, Circle(At, 4.5), Blue);
		}
		const FMobileRoutePoint& End = Route.Points.Last();
		if (End.BuildingId == LevelMap.BuildingId && End.Level == LevelMap.Level)
		{
			const FVector2D At = ToWidgetPolygon({ End.Position }, W, H, Lift)[0];
			FillPolygon(OutDrawElements, Layer + 7, AllottedGeometry, Circle(At, 9.0), White);
			FillPolygon(OutDrawElements, Layer + 8, AllottedGeometry, Circle(At, 6.5), Red);
		}
	}

	// Where the person is.
	if (bHasPosition && Position.BuildingId == LevelMap.BuildingId && Position.Level == LevelMap.Level)
	{
		const FVector2D At = ToWidgetPolygon({ Position.Position }, W, H, Lift)[0];
		FillPolygon(OutDrawElements, Layer + 9, AllottedGeometry, Circle(At, 12.0), FLinearColor(0.12f, 0.39f, 0.94f, 0.25f));
		FillPolygon(OutDrawElements, Layer + 10, AllottedGeometry, Circle(At, 7.5), White);
		FillPolygon(OutDrawElements, Layer + 11, AllottedGeometry, Circle(At, 5.5), Blue);
	}
	return Layer + 12;
}

UMobileMapWidget::UMobileMapWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// A user widget does not take clicks itself by default; the map must, to hear a tap on a room.
	SetVisibility(ESlateVisibility::Visible);
}

FReply UMobileMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// A press on a room or a building is ours (so its release is too); elsewhere it is not.
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const bool bHit = bOverview ? !BuildingAt(Local, InGeometry.GetLocalSize().X, InGeometry.GetLocalSize().Y).IsEmpty() : !PlaceAt(Local, InGeometry.GetLocalSize().X, InGeometry.GetLocalSize().Y).IsEmpty();
	return bHit ? FReply::Handled() : FReply::Unhandled();
}

FReply UMobileMapWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	const FVector2D Local = InGeometry.AbsoluteToLocal(InGestureEvent.GetScreenSpacePosition());
	const bool bHit = bOverview ? !BuildingAt(Local, InGeometry.GetLocalSize().X, InGeometry.GetLocalSize().Y).IsEmpty() : !PlaceAt(Local, InGeometry.GetLocalSize().X, InGeometry.GetLocalSize().Y).IsEmpty();
	return bHit ? FReply::Handled() : FReply::Unhandled();
}
