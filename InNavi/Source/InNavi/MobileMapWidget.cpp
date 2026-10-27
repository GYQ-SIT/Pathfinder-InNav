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
	FVector2D MobileCampusPosition(const FMobileRoutePoint& Point, const FMobileCampusMap& Campus)
	{
		const auto* Building = Campus.Buildings.FindByPredicate([&](const auto& B) { return B.BuildingId == Point.BuildingId; });
		return Point.Position + (Building ? Building->Offset : FVector2D::ZeroVector);
	}
	// What the map's colours are: soft by kind of room, the way the prototype's plans are.
	FLinearColor RoomColour(const FString& Category, bool bAvailable)
	{
		FLinearColor Colour = MapColours.Room;
		if (Category == TEXT("Learning")) Colour = MapColours.Learning;
		else if (Category == TEXT("Facilities")) Colour = MapColours.Facilities;
		else if (Category == TEXT("Dining")) Colour = MapColours.Dining;
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
	// The screen asks again whenever anything on the map changes; the view starts again only for another level (or from the overview).
	const bool bSame = !bOverview && LevelMap.BuildingId == BuildingId && LevelMap.Level == Level;
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
	if (!bSame)
	{
		ResetView();
	}
}

void UMobileMapWidget::ShowOverview()
{
	if (!bOverview)
	{
		ResetView();
	}
	bOverview = true;
}

void UMobileMapWidget::SetRoute(const FMobileRoute& InRoute) { Route = InRoute; }
void UMobileMapWidget::ClearRoute() { Route = FMobileRoute(); }
void UMobileMapWidget::SetHighlightPlace(const FString& PlaceId) { HighlightPlaceId = PlaceId; }
void UMobileMapWidget::SetPosition(const FMobileRoutePoint& InPosition) { Position = InPosition; bHasPosition = true; }
void UMobileMapWidget::ClearPosition() { bHasPosition = false; }
void UMobileMapWidget::Set3D(bool bInShow3D)
{
	// The tilted plan has other extent, so a pan made on the flat one means nothing on it.
	if (bShow3D != bInShow3D)
	{
		ResetView();
	}
	bShow3D = bInShow3D;
}

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
		if (Route.bFound) for (const auto& Point : Route.Points) Bounds += Project(MobileCampusPosition(Point, CampusMap));
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
	const double FitScale = FMath::Min(AvailW / FMath::Max(Size.X, 1e-6), AvailH / FMath::Max(Size.Y, 1e-6));
	// The plan is centred in the part that is not covered.
	const FVector2D FitOffset = FVector2D(Margin + (AvailW - Size.X * FitScale) * 0.5, InsetTop + (AvailH - Size.Y * FitScale) * 0.5) - Bounds.Min * FitScale;
	// Then zoomed about the middle of that part, and moved by the person's pan.
	const FVector2D Centre(WidgetWidth * 0.5, InsetTop + AvailH * 0.5);
	OutScale = FitScale * Zoom;
	OutOffset = (FitOffset - Centre) * Zoom + Centre + Pan;
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

int32 UMobileMapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const double W = AllottedGeometry.GetLocalSize().X, H = AllottedGeometry.GetLocalSize().Y;
	ViewSize = FVector2D(W, H);
	const double Lift = bShow3D ? SlabHeight : 0.0;
	int32 Layer = LayerId;

	if (bOverview)
	{
		for (const FMobileCampusFeature& Feature : CampusMap.Features)
		{
			const TArray<FVector2D> Shape = ToWidgetPolygon(Feature.Outline, W, H, 0.0);
			const bool bWater = Feature.Name == TEXT("Pond");
			FillPolygon(OutDrawElements, Layer, AllottedGeometry, Shape, bWater ? MapColours.Water : MapColours.Grass);
			DrawLabel(OutDrawElements, Layer + 1, AllottedGeometry, Feature.Name, Centre(Shape), 11.f, Grey);
		}
		for (const FMobileCampusBuilding& Building : CampusMap.Buildings)
		{
			if (bShow3D)
			{
				FillPolygon(OutDrawElements, Layer + 1, AllottedGeometry, ToWidgetPolygon(Building.Outline, W, H, 0.0), MapColours.Slab);
			}
			const TArray<FVector2D> Top = ToWidgetPolygon(Building.Outline, W, H, Lift);
			FillPolygon(OutDrawElements, Layer + 2, AllottedGeometry, Top, MapColours.BuildingTop);
			OutlinePolygon(OutDrawElements, Layer + 3, AllottedGeometry, Top, MapColours.Edge, 2.f);
			DrawLabel(OutDrawElements, Layer + 4, AllottedGeometry, Building.Name, Centre(Top), 15.f, Ink);
		}
		if (Route.bFound && Route.Points.Num() >= 2)
		{
			TArray<FVector2D> CampusRouteLine;
			for (const auto& Point : Route.Points) CampusRouteLine.Add(ToWidgetPolygon({MobileCampusPosition(Point, CampusMap)}, W, H, Lift)[0]);
			FSlateDrawElement::MakeLines(OutDrawElements, Layer + 6, AllottedGeometry.ToPaintGeometry(), CampusRouteLine, ESlateDrawEffect::None, Blue, true, 4.f);
			FillPolygon(OutDrawElements, Layer + 7, AllottedGeometry, Circle(CampusRouteLine[0], 6.0), Blue);
			FillPolygon(OutDrawElements, Layer + 7, AllottedGeometry, Circle(CampusRouteLine.Last(), 7.0), Red);
		}
		if (bHasPosition)
		{
			const auto At = ToWidgetPolygon({MobileCampusPosition(Position, CampusMap)}, W, H, Lift)[0];
			FillPolygon(OutDrawElements, Layer + 8, AllottedGeometry, Circle(At, 8.0), FLinearColor::White);
			FillPolygon(OutDrawElements, Layer + 9, AllottedGeometry, Circle(At, 5.0), Blue);
		}
		return Layer + 10;
	}
	if (LevelMap.Outline.IsEmpty())
	{
		return Layer;
	}

	// The floor: in the tilted view it stands on a slab.
	if (bShow3D)
	{
		FillPolygon(OutDrawElements, Layer, AllottedGeometry, ToWidgetPolygon(LevelMap.Outline, W, H, 0.0), MapColours.Slab);
	}
	const TArray<FVector2D> Floor = ToWidgetPolygon(LevelMap.Outline, W, H, Lift);
	FillPolygon(OutDrawElements, Layer + 1, AllottedGeometry, Floor, MapColours.Floor);
	OutlinePolygon(OutDrawElements, Layer + 3, AllottedGeometry, Floor, MapColours.Edge, 2.5f);

	// The rooms.
	for (const FMobileMapRoom& Room : LevelMap.Rooms)
	{
		const TArray<FVector2D> Shape = ToWidgetPolygon(Room.Polygon, W, H, Lift);
		const bool bHighlight = !Room.PlaceId.IsEmpty() && Room.PlaceId == HighlightPlaceId;
		FillPolygon(OutDrawElements, Layer + 2, AllottedGeometry, Shape, bHighlight ? MapColours.Highlight : RoomColour(Room.Category, Room.bAvailable));
		OutlinePolygon(OutDrawElements, Layer + 3, AllottedGeometry, Shape, bHighlight ? Blue : MapColours.RoomEdge, bHighlight ? 3.f : 1.2f);
		DrawLabel(OutDrawElements, Layer + 4, AllottedGeometry, Room.Name, Centre(Shape), 10.f, Ink);
	}

	// The lifts and stairs.
	for (const FMobileMapConnector& Connector : LevelMap.Connectors)
	{
		const FVector2D At = ToWidgetPolygon({ Connector.Position }, W, H, Lift)[0];
		FillPolygon(OutDrawElements, Layer + 4, AllottedGeometry, Circle(At, 8.0), Connector.Kind == EMobileConnectorKind::Lift ? StartStyle::Rgb(30, 100, 240) : (Connector.Kind == EMobileConnectorKind::Stairs ? StartStyle::Rgb(100, 116, 139) : StartStyle::Rgb(13, 148, 136)));
		DrawLabel(OutDrawElements, Layer + 5, AllottedGeometry, Connector.Kind == EMobileConnectorKind::Lift ? TEXT("L") : (Connector.Kind == EMobileConnectorKind::Stairs ? TEXT("S") : TEXT("E")), At, 9.f, FLinearColor::White);
	}

	// The way in, on the ground level.
	if (LevelMap.Level == 1 && LevelMap.bHasEntrance)
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
			FillPolygon(OutDrawElements, Layer + 7, AllottedGeometry, Circle(At, 7.0), FLinearColor::White);
			FillPolygon(OutDrawElements, Layer + 8, AllottedGeometry, Circle(At, 4.5), Blue);
		}
		const FMobileRoutePoint& End = Route.Points.Last();
		if (End.BuildingId == LevelMap.BuildingId && End.Level == LevelMap.Level)
		{
			const FVector2D At = ToWidgetPolygon({ End.Position }, W, H, Lift)[0];
			FillPolygon(OutDrawElements, Layer + 7, AllottedGeometry, Circle(At, 9.0), FLinearColor::White);
			FillPolygon(OutDrawElements, Layer + 8, AllottedGeometry, Circle(At, 6.5), Red);
		}
	}

	// Where the person is.
	if (bHasPosition && Position.BuildingId == LevelMap.BuildingId && Position.Level == LevelMap.Level)
	{
		const FVector2D At = ToWidgetPolygon({ Position.Position }, W, H, Lift)[0];
		FillPolygon(OutDrawElements, Layer + 9, AllottedGeometry, Circle(At, 12.0), FLinearColor(0.12f, 0.39f, 0.94f, 0.25f));
		FillPolygon(OutDrawElements, Layer + 10, AllottedGeometry, Circle(At, 7.5), FLinearColor::White);
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

// ---- The view: zoom and pan ----

void UMobileMapWidget::ResetView()
{
	Zoom = 1.0;
	Pan = FVector2D::ZeroVector;
}

void UMobileMapWidget::ZoomAround(const FVector2D& Local, double Factor)
{
	const double NewZoom = FMath::Clamp(Zoom * Factor, MinZoom, MaxZoom);
	const double Actual = NewZoom / Zoom;
	// The point under the finger (or the middle) stays where it is: the plan is scaled about the middle of the uncovered part, then moved.
	const FVector2D Centre(ViewSize.X * 0.5, InsetTop + FMath::Max(ViewSize.Y - InsetTop - InsetBottom - (bShow3D ? SlabHeight : 0.0), 10.0) * 0.5);
	Pan = (Local - Centre) - (Local - Centre - Pan) * Actual;
	Zoom = NewZoom;
	ClampPan();
}

void UMobileMapWidget::ZoomBy(double Delta)
{
	const double Target = FMath::Clamp(Zoom + Delta, MinZoom, MaxZoom);
	const FVector2D Centre(ViewSize.X * 0.5, InsetTop + FMath::Max(ViewSize.Y - InsetTop - InsetBottom - (bShow3D ? SlabHeight : 0.0), 10.0) * 0.5);
	ZoomAround(Centre, Target / Zoom);
}

void UMobileMapWidget::PanBy(const FVector2D& Delta)
{
	Pan += Delta;
	ClampPan();
}

void UMobileMapWidget::ClampPan()
{
	const FBox2D Extent = GetPlanExtentInWidget(ViewSize.X, ViewSize.Y);
	if (!Extent.bIsValid)
	{
		return;
	}
	// Some of the plan always stays in the uncovered part (80 pixels of it, or all of it if it is smaller).
	const double KeepX = FMath::Min(80.0, Extent.GetSize().X);
	const double KeepY = FMath::Min(80.0, Extent.GetSize().Y);
	const double Top = InsetTop;
	const double Bottom = ViewSize.Y - InsetBottom;
	FVector2D Shift = FVector2D::ZeroVector;
	if (Extent.Max.X < KeepX) Shift.X = KeepX - Extent.Max.X;
	else if (Extent.Min.X > ViewSize.X - KeepX) Shift.X = (ViewSize.X - KeepX) - Extent.Min.X;
	if (Extent.Max.Y < Top + KeepY) Shift.Y = Top + KeepY - Extent.Max.Y;
	else if (Extent.Min.Y > Bottom - KeepY) Shift.Y = (Bottom - KeepY) - Extent.Min.Y;
	Pan += Shift;
}

// ---- Pointers ----

namespace
{
	// A finger and the mouse are told apart (an emulator sends both for one click).
	int32 PointerKey(const FPointerEvent& Event)
	{
		return static_cast<int32>(Event.GetPointerIndex()) + (Event.IsTouchEvent() ? 1000 : 0);
	}
	constexpr double TapSlop = 6.0;
}

FReply UMobileMapWidget::HandlePointerDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	ViewSize = FVector2D(Geometry.GetLocalSize());
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	if (Pointers.IsEmpty())
	{
		PressLocal = Local;
		bPressMoved = false;
		bMultiTouch = false;
	}
	else
	{
		// A second finger: this is a pinch, not a tap.
		bMultiTouch = true;
		bPressMoved = true;
	}
	Pointers.Add(PointerKey(Event), Local);
	return Pointers.Num() == 1 ? FReply::Handled().CaptureMouse(TakeWidget()) : FReply::Handled();
}

FReply UMobileMapWidget::HandlePointerMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	FVector2D* Known = Pointers.Find(PointerKey(Event));
	if (!Known)
	{
		return FReply::Unhandled();
	}
	ViewSize = FVector2D(Geometry.GetLocalSize());
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const FVector2D Before = *Known;
	*Known = Local;
	if (Pointers.Num() == 1)
	{
		if (!bPressMoved && (Local - PressLocal).Size() > TapSlop)
		{
			bPressMoved = true;
		}
		if (bPressMoved && bGestures)
		{
			PanBy(Local - Before);
		}
	}
	else if (Pointers.Num() == 2 && bGestures)
	{
		// Two fingers: the distance between them zooms, and where their middle goes moves the view.
		FVector2D Other = Local;
		const int32 ThisKey = PointerKey(Event);
		for (const TPair<int32, FVector2D>& Pair : Pointers)
		{
			if (Pair.Key != ThisKey)
			{
				Other = Pair.Value;
			}
		}
		const double OldDistance = (Before - Other).Size();
		const double NewDistance = (Local - Other).Size();
		const FVector2D OldMiddle = (Before + Other) * 0.5;
		const FVector2D NewMiddle = (Local + Other) * 0.5;
		if (OldDistance > 1.0)
		{
			ZoomAround(OldMiddle, NewDistance / OldDistance);
		}
		PanBy(NewMiddle - OldMiddle);
	}
	return FReply::Handled();
}

FReply UMobileMapWidget::HandlePointerUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!Pointers.Contains(PointerKey(Event)))
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const bool bWasOnly = Pointers.Num() == 1;
	Pointers.Remove(PointerKey(Event));
	if (!Pointers.IsEmpty())
	{
		return FReply::Handled();
	}
	// The last pointer went up: a press that did not move is a tap on a room.
	if (bWasOnly && !bPressMoved && !bMultiTouch)
	{
		Tap(Local, Geometry.GetLocalSize().X, Geometry.GetLocalSize().Y);
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply UMobileMapWidget::HandleWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!bGestures)
	{
		return FReply::Unhandled();
	}
	ViewSize = FVector2D(Geometry.GetLocalSize());
	ZoomAround(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()), FMath::Pow(1.12, Event.GetWheelDelta()));
	return FReply::Handled();
}

FReply UMobileMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton ? HandlePointerDown(InGeometry, InMouseEvent) : FReply::Unhandled();
}

FReply UMobileMapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return HandlePointerMove(InGeometry, InMouseEvent);
}

FReply UMobileMapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton ? HandlePointerUp(InGeometry, InMouseEvent) : FReply::Unhandled();
}

FReply UMobileMapWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return HandleWheel(InGeometry, InMouseEvent);
}

FReply UMobileMapWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	return HandlePointerDown(InGeometry, InGestureEvent);
}

FReply UMobileMapWidget::NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	return HandlePointerMove(InGeometry, InGestureEvent);
}

FReply UMobileMapWidget::NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	return HandlePointerUp(InGeometry, InGestureEvent);
}

void UMobileMapWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
	Pointers.Reset();
}
