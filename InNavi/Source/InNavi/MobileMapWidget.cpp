#include "MobileMapWidget.h"
#include "MobileIcons.h"
#include "MobileMapScale.h"

#include "MobileMapLabels.h"
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

	// How far a point is from the nearest wall of a polygon, in its units.
	double DistanceToPolygonEdge(const FVector2D& P, const TArray<FVector2D>& Poly)
	{
		double Nearest = TNumericLimits<double>::Max();
		for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
		{
			const FVector2D Edge = Poly[i] - Poly[j];
			const double LengthSquared = Edge.SizeSquared();
			const double T = LengthSquared > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(P - Poly[j], Edge) / LengthSquared, 0.0, 1.0) : 0.0;
			Nearest = FMath::Min(Nearest, FVector2D::Distance(P, Poly[j] + Edge * T));
		}
		return Nearest;
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

	// The cone that shows which way the device faces: a wedge opening from the dot towards Direction (a unit vector on the screen).
	TArray<FVector2D> HeadingCone(const FVector2D& Centre2, const FVector2D& Direction)
	{
		constexpr double Reach = 46.0, HalfAngle = 0.56; // about 32 degrees each side
		TArray<FVector2D> Points;
		Points.Add(Centre2);
		const double Base = FMath::Atan2(Direction.Y, Direction.X);
		for (int32 i = 0; i <= 6; ++i)
		{
			const double Angle = Base - HalfAngle + 2.0 * HalfAngle * i / 6.0;
			Points.Add(Centre2 + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Reach);
		}
		return Points;
	}

	FVector2D LabelSize(const FString& Text, float PixelSize, bool bBold = true)
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", FMath::RoundToInt(PixelSize * StartStyle::PixelsToFont));
		FVector2D Size(Text.Len() * PixelSize * 0.55, PixelSize);
		if (FSlateApplication::IsInitialized())
		{
			Size = FVector2D(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font));
		}
		return Size;
	}

	// Text with a soft outline in the colour of the page, so a label stays readable over a room's colour or its edge.
	void DrawLabel(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FString& Text, const FVector2D& CentreAt, float PixelSize, const FLinearColor& Colour, bool bHalo = true, bool bBold = true)
	{
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", FMath::RoundToInt(PixelSize * StartStyle::PixelsToFont));
		const FVector2D Size = LabelSize(Text, PixelSize, bBold);
		if (bHalo)
		{
			Font.OutlineSettings.OutlineSize = 2;
			Font.OutlineSettings.OutlineColor = White.CopyWithNewOpacity(0.9f);
		}
		const FVector2D TopLeft = CentreAt - Size * 0.5;
		FSlateDrawElement::MakeText(Out, Layer, Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TopLeft))), Text, Font, ESlateDrawEffect::None, Colour);
	}

	// A label waiting to be placed: the text, where it is centred, and how important it is (see MobileMapLabels).
	struct FMapLabel
	{
		FString Text;
		FVector2D At = FVector2D::ZeroVector;
		float PixelSize = 10.f;
		FLinearColor Colour = FLinearColor::Black;
		double Priority = 0.0;
		int32 Layer = 0;
		// The names of rooms are drawn light, the names of buildings and places on the route bold.
		bool bBold = true;
	};

	// Draws the labels that fit: the important ones first, none over another or over an obstacle.
	void DrawChosenLabels(FSlateWindowElementList& Out, const FGeometry& Geometry, const TArray<FMapLabel>& Labels, const TArray<FBox2D>& Obstacles, double WidgetWidth, double WidgetHeight)
	{
		TArray<MobileMapLabels::FCandidate> Candidates;
		for (const FMapLabel& Label : Labels)
		{
			const FVector2D Size = LabelSize(Label.Text, Label.PixelSize, Label.bBold);
			MobileMapLabels::FCandidate Candidate;
			Candidate.Box = FBox2D(Label.At - Size * 0.5, Label.At + Size * 0.5);
			Candidate.Priority = Label.Priority;
			Candidates.Add(Candidate);
		}
		const TArray<bool> Chosen = MobileMapLabels::Choose(Candidates, Obstacles, FBox2D(FVector2D::ZeroVector, FVector2D(WidgetWidth, WidgetHeight)));
		for (int32 Index = 0; Index < Labels.Num(); ++Index)
		{
			if (Chosen[Index])
			{
				DrawLabel(Out, Labels[Index].Layer, Geometry, Labels[Index].Text, Labels[Index].At, Labels[Index].PixelSize, Labels[Index].Colour, true, Labels[Index].bBold);
			}
		}
	}

	// How big a polygon looks on the screen.
	FBox2D BoundsOf(const TArray<FVector2D>& Poly)
	{
		FBox2D Box(ForceInit);
		for (const FVector2D& P : Poly)
		{
			Box += P;
		}
		return Box;
	}

	// The size of a room's name on the map and its colour: light and grey, so the plan is not covered in black text.
	constexpr float RoomLabelPixels = 9.f;

	// How tall the tilted view stands the plan, in design pixels.
	constexpr double SlabHeight = 7.0;
}

void UMobileMapWidget::SetSource(IMobileNavSource* InSource, const FString& InSiteId)
{
	Source = InSource;
	SiteId = InSiteId;
	CampusMap = Source ? Source->GetCampusMap(SiteId) : FMobileCampusMap();
	bPlanNorthOnX = Source && Source->IsPlanNorthOnX();
}

void UMobileMapWidget::SetPlanNorthOnX(bool bInNorthOnX)
{
	if (bPlanNorthOnX != bInNorthOnX)
	{
		bPlanNorthOnX = bInNorthOnX;
		InvalidateLayoutAndVolatility();
	}
}

void UMobileMapWidget::SetMapsForTest(const FMobileLevelMap& Level, const FMobileCampusMap& Campus)
{
	LevelMap = Level;
	CampusMap = Campus;
	bOverview = false;
	ResetView();
}

void UMobileMapWidget::ShowLevel(const FString& BuildingId, int32 Level)
{
	// The screen asks again whenever anything on the map changes; the view starts again only for another building (or from the overview): another level
	// of the same building keeps it, so the person does not lose their place when they change floor.
	const bool bSameBuilding = !bOverview && LevelMap.BuildingId == BuildingId;
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
	if (!bSameBuilding)
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

void UMobileMapWidget::SetRoute(const FMobileRoute& InRoute)
{
	// A different route is looked at from its own middle; the screen sets the same one again whenever anything changes, and that must not move the view.
	const bool bChanged = Route.bFound != InRoute.bFound || Route.Points.Num() != InRoute.Points.Num()
		|| (!Route.Points.IsEmpty() && (!Route.Points[0].Position.Equals(InRoute.Points[0].Position) || !Route.Points.Last().Position.Equals(InRoute.Points.Last().Position)));
	Route = InRoute;
	if (bChanged)
	{
		LatchedFocus.Reset();
	}
}
void UMobileMapWidget::ClearRoute() { SetRoute(FMobileRoute()); }
void UMobileMapWidget::SetStops(const TArray<FMobileRoutePoint>& InStops) { Stops = InStops; }

int32 UMobileMapWidget::GetStopsOnLevel() const
{
	int32 Count = 0;
	for (const FMobileRoutePoint& Stop : Stops)
	{
		Count += (Stop.BuildingId == LevelMap.BuildingId && Stop.Level == LevelMap.Level) ? 1 : 0;
	}
	return Count;
}
void UMobileMapWidget::SetHighlightPlace(const FString& PlaceId)
{
	const bool bChanged = HighlightPlaceId != PlaceId;
	HighlightPlaceId = PlaceId;
	if (!bChanged || PlaceId.IsEmpty() || bOverview)
	{
		return;
	}
	// A place chosen from the list that is out of sight (the map starts in the middle of a long building) is brought into view; one tapped on the map is
	// already in view, and the view stays where it is.
	for (const FMobileMapRoom& Room : LevelMap.Rooms)
	{
		if (Room.PlaceId == PlaceId)
		{
			const FVector2D At = ToWidget(Centre(Room.Polygon), ViewSize.X, ViewSize.Y);
			const double Edge = 30.0;
			const bool bInView = At.X > Edge && At.X < ViewSize.X - Edge && At.Y > InsetTop + Edge && At.Y < ViewSize.Y - InsetBottom - Edge;
			if (!bInView)
			{
				LatchedFocus = Centre(Room.Polygon);
				Pan = FVector2D::ZeroVector;
			}
			return;
		}
	}
}
void UMobileMapWidget::SetPosition(const FMobileRoutePoint& InPosition) { Position = InPosition; bHasPosition = true; }
void UMobileMapWidget::ClearPosition() { bHasPosition = false; }

void UMobileMapWidget::SetGpsPosition(TOptional<FVector2D> InPosition)
{
	if (GpsPosition != InPosition)
	{
		GpsPosition = InPosition;
		InvalidateLayoutAndVolatility();
	}
}

void UMobileMapWidget::SetGpsHeading(TOptional<double> Degrees)
{
	// A change of less than a degree is not worth drawing again.
	const bool bChanged = GpsHeading.IsSet() != Degrees.IsSet() || (Degrees && FMath::Abs(FMath::FindDeltaAngleDegrees(*GpsHeading, *Degrees)) >= 1.0);
	if (bChanged)
	{
		GpsHeading = Degrees;
		if (GpsPosition) InvalidateLayoutAndVolatility();
	}
}

TOptional<FVector2D> UMobileMapWidget::GetGpsHeadingOnScreen(const FVector2D& PlanPoint) const
{
	if (!GpsHeading || ViewSize.X <= 0.0 || ViewSize.Y <= 0.0) return {};
	// The plan has north on X and east on Y (GeoProjection), so a heading clockwise from north points to (cos, sin).
	const double Radians = FMath::DegreesToRadians(*GpsHeading);
	const double Step = 5.0 * LevelMap.UnitsPerMetre;
	const double Lift = bShow3D ? SlabHeight : 0.0;
	const FVector2D From = ToWidgetPolygon({ PlanPoint }, ViewSize.X, ViewSize.Y, Lift)[0];
	const FVector2D To = ToWidgetPolygon({ PlanPoint + FVector2D(FMath::Cos(Radians), FMath::Sin(Radians)) * Step }, ViewSize.X, ViewSize.Y, Lift)[0];
	const FVector2D Direction = To - From;
	return Direction.SizeSquared() > 1.e-9 ? TOptional<FVector2D>(Direction.GetSafeNormal()) : TOptional<FVector2D>();
}

TOptional<FVector2D> UMobileMapWidget::GetGpsLevelPosition() const
{
	if (!GpsPosition || bOverview || LevelMap.Outline.Num() < 3) return {};
	const auto* Building = CampusMap.Buildings.FindByPredicate([&](const FMobileCampusBuilding& B) { return B.BuildingId == LevelMap.BuildingId; });
	if (!Building) return {};
	const FVector2D Local = *GpsPosition - Building->Offset;
	const double Margin = 8.0 * LevelMap.UnitsPerMetre;
	return PointInPolygon(Local, LevelMap.Outline) || DistanceToPolygonEdge(Local, LevelMap.Outline) <= Margin ? TOptional<FVector2D>(Local) : TOptional<FVector2D>();
}

void UMobileMapWidget::CenterOnGps()
{
	const TOptional<FVector2D> Level = GetGpsLevelPosition();
	const TOptional<FVector2D> At = bOverview ? GpsPosition : Level;
	if (At && ViewSize.X > 0.0 && ViewSize.Y > 0.0)
	{
		const double Lift = bShow3D ? SlabHeight : 0.0;
		Pan += ViewCentre() - ToWidget(*At, ViewSize.X, ViewSize.Y) + FVector2D(0.0, Lift);
		InvalidateLayoutAndVolatility();
	}
}
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

FVector2D UMobileMapWidget::Turn(const FVector2D& P, const FVector2D& About) const
{
	if (FMath::IsNearlyZero(Rotation))
	{
		return P;
	}
	const double Cos = FMath::Cos(Rotation);
	const double Sin = FMath::Sin(Rotation);
	const FVector2D D = P - About;
	return About + FVector2D(D.X * Cos - D.Y * Sin, D.X * Sin + D.Y * Cos);
}

FBox2D UMobileMapWidget::RawPlanBounds() const
{
	FBox2D Bounds(ForceInit);
	auto Add = [&Bounds](const TArray<FVector2D>& Poly)
	{
		for (const FVector2D& P : Poly)
		{
			Bounds += P;
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

FVector2D UMobileMapWidget::ViewCentre() const
{
	return FVector2D(ViewSize.X * 0.5, InsetTop + FMath::Max(ViewSize.Y - InsetTop - InsetBottom - (bShow3D ? SlabHeight : 0.0), 10.0) * 0.5);
}

FVector2D UMobileMapWidget::ProjectFlat(const FVector2D& P) const
{
	// Plans with north on X and east on Y (the real projects) are drawn with north up: east to the right (the screen's x) and north up (minus the screen's y).
	const FVector2D Flat = bPlanNorthOnX ? FVector2D(P.Y, -P.X) : P;
	if (!bShow3D)
	{
		return Flat;
	}
	// A tilted view: turned a quarter, and squashed, so the plan reads as seen from an angle.
	return FVector2D((Flat.X - Flat.Y) * 0.7071, (Flat.X + Flat.Y) * 0.7071 * 0.5);
}

FBox2D UMobileMapWidget::PlanBounds() const
{
	FBox2D Bounds(ForceInit);
	auto Add = [&](const TArray<FVector2D>& Poly)
	{
		for (const FVector2D& P : Poly)
		{
			Bounds += ProjectFlat(P);
		}
	};
	if (bOverview)
	{
		for (const FMobileCampusBuilding& Building : CampusMap.Buildings) Add(Building.Outline);
		for (const FMobileCampusFeature& Feature : CampusMap.Features) Add(Feature.Outline);
		if (Route.bFound) for (const auto& Point : Route.Points) Bounds += ProjectFlat(MobileCampusPosition(Point, CampusMap));
	}
	else
	{
		Add(LevelMap.Outline);
	}
	return Bounds;
}

bool UMobileMapWidget::RouteBoxOnLevel(FBox2D& OutBox) const
{
	OutBox = FBox2D(ForceInit);
	int32 Count = 0;
	for (const FMobileRoutePoint& Point : Route.Points)
	{
		if (Point.BuildingId == LevelMap.BuildingId && Point.Level == LevelMap.Level)
		{
			OutBox += Point.Position;
			++Count;
		}
	}
	return Count >= 2;
}

FVector2D UMobileMapWidget::FocusPoint() const
{
	if (LatchedFocus)
	{
		return *LatchedFocus;
	}
	FVector2D Focus = RawPlanBounds().GetCenter();
	FBox2D RouteBox;
	if (RouteBoxOnLevel(RouteBox))
	{
		// A route on this level: the middle of it.
		Focus = RouteBox.GetCenter();
	}
	else
	{
		bool bFound = false;
		if (!HighlightPlaceId.IsEmpty())
		{
			for (const FMobileMapRoom& Room : LevelMap.Rooms)
			{
				if (Room.PlaceId == HighlightPlaceId)
				{
					Focus = Centre(Room.Polygon);
					bFound = true;
					break;
				}
			}
		}
		if (!bFound && bHasPosition && Position.BuildingId == LevelMap.BuildingId && Position.Level == LevelMap.Level)
		{
			Focus = Position.Position;
		}
		else if (!bFound && LevelMap.Level == 1 && LevelMap.bHasEntrance)
		{
			Focus = LevelMap.Entrance;
		}
	}
	LatchedFocus = Focus;
	return Focus;
}

int32 UMobileMapWidget::GetContextBuildingCount() const
{
	if (bOverview)
	{
		return 0;
	}
	int32 Count = 0;
	for (const FMobileCampusBuilding& Building : CampusMap.Buildings)
	{
		Count += Building.BuildingId != LevelMap.BuildingId ? 1 : 0;
	}
	return Count;
}

UMobileMapWidget::FStart UMobileMapWidget::WorkOutStart(double WidgetWidth, double WidgetHeight) const
{
	FStart Result;
	Result.Bounds = PlanBounds();
	if (!Result.Bounds.bIsValid)
	{
		return Result;
	}
	Result.bValid = true;
	const double Margin = 16.0;
	Result.AvailW = FMath::Max(WidgetWidth - 2.0 * Margin, 10.0);
	Result.AvailH = FMath::Max(WidgetHeight - InsetTop - InsetBottom - (bShow3D ? SlabHeight : 0.0), 10.0);
	const FVector2D Size = Result.Bounds.GetSize();
	Result.Fit = FMath::Min(Result.AvailW / FMath::Max(Size.X, 1e-6), Result.AvailH / FMath::Max(Size.Y, 1e-6));
	Result.Start = Result.Fit;
	if (!bOverview)
	{
		// A big building starts at about 100 metres across the screen; if a route is on the level and fits that, the whole route is shown.
		double Start = MobileMapScale::StartScale(Result.Fit, Result.AvailW, LevelMap.UnitsPerMetre);
		FBox2D RouteBox;
		if (Start > Result.Fit && RouteBoxOnLevel(RouteBox))
		{
			const double Pad = 10.0 * LevelMap.UnitsPerMetre;
			// The route's size along the screen's width and height: north is up, so the plan's X is the screen's height.
			const FVector2D RouteSize = (bPlanNorthOnX ? FVector2D(RouteBox.GetSize().Y, RouteBox.GetSize().X) : RouteBox.GetSize()) + FVector2D(2.0 * Pad, 2.0 * Pad);
			Start = FMath::Max(Result.Fit, FMath::Min(Start, FMath::Min(Result.AvailW / RouteSize.X, Result.AvailH / RouteSize.Y)));
		}
		Result.Start = Start;
	}
	return Result;
}

double UMobileMapWidget::GetMinZoom() const
{
	const FStart Found = WorkOutStart(ViewSize.X, ViewSize.Y);
	return Found.bValid ? MobileMapScale::MinZoomFor(Found.Fit, Found.Start, MinZoom) : MinZoom;
}

void UMobileMapWidget::ComputeFit(double WidgetWidth, double WidgetHeight, double& OutScale, FVector2D& OutOffset) const
{
	OutScale = 1.0;
	OutOffset = FVector2D::ZeroVector;
	const FStart Found = WorkOutStart(WidgetWidth, WidgetHeight);
	if (!Found.bValid)
	{
		return;
	}
	const double Margin = 16.0;
	const FVector2D Size = Found.Bounds.GetSize();
	const FVector2D Centre(WidgetWidth * 0.5, InsetTop + Found.AvailH * 0.5);
	FVector2D FitOffset;
	if (Found.Start > Found.Fit * 1.0001)
	{
		// A big building: the focus is in the middle of the part that is not covered (turned like the plan is, so turning keeps it there).
		const FVector2D About = FMath::IsNearlyZero(Rotation) ? FVector2D::ZeroVector : RawPlanBounds().GetCenter();
		FitOffset = Centre - ProjectFlat(Turn(FocusPoint(), About)) * Found.Start;
	}
	else
	{
		// The plan is centred in the part that is not covered.
		FitOffset = FVector2D(Margin + (Found.AvailW - Size.X * Found.Fit) * 0.5, InsetTop + (Found.AvailH - Size.Y * Found.Fit) * 0.5) - Found.Bounds.Min * Found.Fit;
	}
	// Then zoomed about the middle of that part, and moved by the person's pan.
	OutScale = Found.Start * Zoom;
	OutOffset = (FitOffset - Centre) * Zoom + Centre + Pan;
}

FVector2D UMobileMapWidget::ToWidget(const FVector2D& PlanPoint, double WidgetWidth, double WidgetHeight) const
{
	double Scale;
	FVector2D Offset;
	ComputeFit(WidgetWidth, WidgetHeight, Scale, Offset);
	const FVector2D About = FMath::IsNearlyZero(Rotation) ? FVector2D::ZeroVector : RawPlanBounds().GetCenter();
	return ProjectFlat(Turn(PlanPoint, About)) * Scale + Offset;
}

TArray<FVector2D> UMobileMapWidget::ToWidgetPolygon(const TArray<FVector2D>& Plan, double WidgetWidth, double WidgetHeight, double Lift) const
{
	double Scale;
	FVector2D Offset;
	ComputeFit(WidgetWidth, WidgetHeight, Scale, Offset);
	TArray<FVector2D> Result;
	Result.Reserve(Plan.Num());
	const FVector2D About = FMath::IsNearlyZero(Rotation) ? FVector2D::ZeroVector : RawPlanBounds().GetCenter();
	for (const FVector2D& P : Plan)
	{
		Result.Add(ProjectFlat(Turn(P, About)) * Scale + Offset - FVector2D(0.0, Lift));
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
		// Names are placed after everything is drawn (see DrawChosenLabels): the ones on the route first, then the biggest buildings.
		TArray<FMapLabel> Labels;
		TSet<FString> RouteBuildings;
		if (Route.bFound)
		{
			for (const FMobileRoutePoint& Point : Route.Points) RouteBuildings.Add(Point.BuildingId);
		}
		for (const FMobileCampusFeature& Feature : CampusMap.Features)
		{
			const TArray<FVector2D> Shape = ToWidgetPolygon(Feature.Outline, W, H, 0.0);
			const bool bWater = Feature.Name == TEXT("Pond");
			FillPolygon(OutDrawElements, Layer, AllottedGeometry, Shape, bWater ? MapColours.Water : MapColours.Grass);
			Labels.Add({ Feature.Name, Centre(Shape), 11.f, Grey, -1.0, Layer + 1 });
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
			const FBox2D Extent = BoundsOf(Top);
			Labels.Add({ Building.Name, Centre(Top), 15.f, Ink, RouteBuildings.Contains(Building.BuildingId) ? 1.0e9 : Extent.GetSize().X * Extent.GetSize().Y, Layer + 4 });
		}
		DrawChosenLabels(OutDrawElements, AllottedGeometry, Labels, TArray<FBox2D>(), W, H);
		if (Route.bFound && Route.Points.Num() >= 2)
		{
			TArray<FVector2D> CampusRouteLine;
			for (const auto& Point : Route.Points) CampusRouteLine.Add(ToWidgetPolygon({MobileCampusPosition(Point, CampusMap)}, W, H, Lift)[0]);
			FSlateDrawElement::MakeLines(OutDrawElements, Layer + 6, AllottedGeometry.ToPaintGeometry(), CampusRouteLine, ESlateDrawEffect::None, Blue, true, 4.f);
			FillPolygon(OutDrawElements, Layer + 7, AllottedGeometry, Circle(CampusRouteLine[0], 6.0), Blue);
			FillPolygon(OutDrawElements, Layer + 7, AllottedGeometry, Circle(CampusRouteLine.Last(), 7.0), Red);
		}
		if (GpsPosition || bHasPosition)
		{
			const FVector2D Point = GpsPosition ? *GpsPosition : MobileCampusPosition(Position, CampusMap);
			const auto At = ToWidgetPolygon({Point}, W, H, Lift)[0];
			if (GpsPosition) if (const TOptional<FVector2D> Facing = GetGpsHeadingOnScreen(Point)) FillPolygon(OutDrawElements, Layer + 8, AllottedGeometry, HeadingCone(At, *Facing), FLinearColor(0.12f, 0.39f, 0.94f, 0.35f));
			FillPolygon(OutDrawElements, Layer + 8, AllottedGeometry, Circle(At, 8.0), FLinearColor::White);
			FillPolygon(OutDrawElements, Layer + 9, AllottedGeometry, Circle(At, 5.0), Blue);
		}
		return Layer + 10;
	}
	if (LevelMap.Outline.IsEmpty())
	{
		return Layer;
	}

	// The rest of the site round the building, faint and nameless: something to judge the size of the building by, as a street map has. They are drawn
	// in the building's own frame (the campus map is in the site's: its offset takes one to the other).
	{
		const FMobileCampusBuilding* Current = CampusMap.Buildings.FindByPredicate([&](const FMobileCampusBuilding& B) { return B.BuildingId == LevelMap.BuildingId; });
		const FVector2D Shift = Current ? Current->Offset : FVector2D::ZeroVector;
		const FLinearColor ContextFill = FMath::Lerp(MapColours.BuildingTop, MapColours.Slab, 0.18f);
		const FLinearColor ContextEdge = FMath::Lerp(MapColours.BuildingTop, MapColours.Edge, 0.35f);
		auto ToLocal = [&](const TArray<FVector2D>& Campus)
		{
			TArray<FVector2D> Local;
			Local.Reserve(Campus.Num());
			for (const FVector2D& P : Campus) Local.Add(P - Shift);
			return ToWidgetPolygon(Local, W, H, 0.0);
		};
		for (const FMobileCampusFeature& Feature : CampusMap.Features)
		{
			FillPolygon(OutDrawElements, Layer, AllottedGeometry, ToLocal(Feature.Outline), Feature.Name == TEXT("Pond") ? MapColours.Water : MapColours.Grass);
		}
		for (const FMobileCampusBuilding& Building : CampusMap.Buildings)
		{
			if (Building.BuildingId == LevelMap.BuildingId)
			{
				continue;
			}
			const TArray<FVector2D> Shape = ToLocal(Building.Outline);
			FillPolygon(OutDrawElements, Layer, AllottedGeometry, Shape, ContextFill);
			OutlinePolygon(OutDrawElements, Layer, AllottedGeometry, Shape, ContextEdge, 1.f);
		}
	}

	// The floor: in the tilted view it stands on a slab.
	if (bShow3D)
	{
		FillPolygon(OutDrawElements, Layer, AllottedGeometry, ToWidgetPolygon(LevelMap.Outline, W, H, 0.0), MapColours.Slab);
	}
	const TArray<FVector2D> Floor = ToWidgetPolygon(LevelMap.Outline, W, H, Lift);
	FillPolygon(OutDrawElements, Layer + 1, AllottedGeometry, Floor, MapColours.Floor);
	OutlinePolygon(OutDrawElements, Layer + 3, AllottedGeometry, Floor, MapColours.Edge, 2.5f);

	// The rooms. Their names are placed after everything is drawn (see DrawChosenLabels): the chosen place first, then the biggest rooms; a
	// room too small to read a name in (until the map is zoomed in) gets none.
	TArray<FMapLabel> RoomLabels;
	// How big things are on the floor now: design pixels for a metre.
	double PlanScale;
	FVector2D PlanOffset;
	ComputeFit(W, H, PlanScale, PlanOffset);
	const double PixelsPerMetre = PlanScale * LevelMap.UnitsPerMetre;
	for (const FMobileMapRoom& Room : LevelMap.Rooms)
	{
		const TArray<FVector2D> Shape = ToWidgetPolygon(Room.Polygon, W, H, Lift);
		const bool bHighlight = !Room.PlaceId.IsEmpty() && Room.PlaceId == HighlightPlaceId;
		FillPolygon(OutDrawElements, Layer + 2, AllottedGeometry, Shape, bHighlight ? MapColours.Highlight : RoomColour(Room.Category, Room.bAvailable));
		OutlinePolygon(OutDrawElements, Layer + 3, AllottedGeometry, Shape, bHighlight ? Blue : MapColours.RoomEdge, bHighlight ? 3.f : 1.2f);
		const FBox2D Extent = BoundsOf(Shape);
		// The name of a room is the short one (without the code after the comma), and only if it fits in the room: zooming in is what makes the names
		// appear. The place looked at is always named, in full. A room with no name of its own (its id) is not named.
		if (bHighlight)
		{
			RoomLabels.Add({ Room.Name, Centre(Shape), 11.f, Blue, 1.0e9, Layer + 4, true });
		}
		else if (!MobileMapScale::LooksLikeRawId(Room.Name))
		{
			const FString Short = MobileMapScale::ShortName(Room.Name);
			if (MobileMapScale::LabelFits(LabelSize(Short, RoomLabelPixels, false).X, Extent.GetSize().X))
			{
				RoomLabels.Add({ Short, Centre(Shape), RoomLabelPixels, Grey, Extent.GetSize().X * Extent.GetSize().Y, Layer + 4, false });
			}
		}
	}

	// The lifts and stairs. The circle is as big as a staircase is on the floor at this scale (within limits), the picture of the kind of connector is in it only
	// when it is big enough to be seen, and of the ones that would overlap only the most important is drawn: the ones by the route, then lifts, escalators, stairs.
	// The circles are kept clear of the room names.
	const double MarkerDiameter = MobileMapScale::MarkerDiameter(PixelsPerMetre);
	const bool bGlyph = MobileMapScale::ShowsGlyph(MarkerDiameter);
	TArray<FBox2D> Obstacles;
	{
		TArray<FVector2D> MarkerAt;
		TArray<MobileMapLabels::FCandidate> Markers;
		for (const FMobileMapConnector& Connector : LevelMap.Connectors)
		{
			const FVector2D At = ToWidgetPolygon({ Connector.Position }, W, H, Lift)[0];
			MarkerAt.Add(At);
			MobileMapLabels::FCandidate Candidate;
			Candidate.Box = FBox2D(At - FVector2D(MarkerDiameter * 0.5, MarkerDiameter * 0.5), At + FVector2D(MarkerDiameter * 0.5, MarkerDiameter * 0.5));
			Candidate.Priority = Connector.Kind == EMobileConnectorKind::Lift ? 3.0 : (Connector.Kind == EMobileConnectorKind::Escalator ? 2.0 : 1.0);
			for (const FMobileRoutePoint& Point : Route.Points)
			{
				if (Point.BuildingId == LevelMap.BuildingId && Point.Level == LevelMap.Level && FVector2D::Distance(Point.Position, Connector.Position) < 3.0 * LevelMap.UnitsPerMetre)
				{
					Candidate.Priority += 100.0;
					break;
				}
			}
			Markers.Add(Candidate);
		}
		const TArray<bool> Shown = MobileMapLabels::Choose(Markers, TArray<FBox2D>(), FBox2D(FVector2D::ZeroVector, FVector2D(W, H)), 1.0);
		for (int32 Index = 0; Index < LevelMap.Connectors.Num(); ++Index)
		{
			if (!Shown[Index])
			{
				continue;
			}
			const FMobileMapConnector& Connector = LevelMap.Connectors[Index];
			const FVector2D At = MarkerAt[Index];
			Obstacles.Add(FBox2D(At - FVector2D(MarkerDiameter * 0.5 + 1.0, MarkerDiameter * 0.5 + 1.0), At + FVector2D(MarkerDiameter * 0.5 + 1.0, MarkerDiameter * 0.5 + 1.0)));
			FillPolygon(OutDrawElements, Layer + 4, AllottedGeometry, Circle(At, MarkerDiameter * 0.5), Connector.Kind == EMobileConnectorKind::Lift ? StartStyle::Rgb(30, 100, 240) : (Connector.Kind == EMobileConnectorKind::Stairs ? StartStyle::Rgb(100, 116, 139) : StartStyle::Rgb(13, 148, 136)));
			if (!bGlyph)
			{
				continue;
			}
			// The design team's picture of the kind of connector (white, in the circle); a kind whose picture is not imported yet keeps its letter.
			const TCHAR* IconName = Connector.Kind == EMobileConnectorKind::Lift ? TEXT("Lift") : (Connector.Kind == EMobileConnectorKind::Stairs ? TEXT("Stairs") : TEXT("Escalator"));
			if (UTexture2D* IconTexture = MobileIcons::LoadTexture(IconName))
			{
				const double IconSize = MarkerDiameter * 0.62;
				FSlateBrush IconBrush;
				IconBrush.SetResourceObject(IconTexture);
				IconBrush.ImageSize = FVector2f(IconTexture->GetSizeX(), IconTexture->GetSizeY());
				IconBrush.DrawAs = ESlateBrushDrawType::Image;
				FSlateDrawElement::MakeBox(OutDrawElements, Layer + 5, AllottedGeometry.ToPaintGeometry(FVector2f(IconSize, IconSize), FSlateLayoutTransform(FVector2f(At - FVector2D(IconSize * 0.5, IconSize * 0.5)))),
					&IconBrush, ESlateDrawEffect::None, FLinearColor::White);
			}
			else
			{
				DrawLabel(OutDrawElements, Layer + 5, AllottedGeometry, Connector.Kind == EMobileConnectorKind::Lift ? TEXT("L") : (Connector.Kind == EMobileConnectorKind::Stairs ? TEXT("S") : TEXT("E")), At, 9.f, FLinearColor::White, false);
			}
		}
	}

	DrawChosenLabels(OutDrawElements, AllottedGeometry, RoomLabels, Obstacles, W, H);

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

	// The stops on the way, numbered, under the end of the route.
	for (int32 StopIndex = 0; StopIndex < Stops.Num(); ++StopIndex)
	{
		const FMobileRoutePoint& Stop = Stops[StopIndex];
		if (Stop.BuildingId == LevelMap.BuildingId && Stop.Level == LevelMap.Level)
		{
			const FVector2D At = ToWidgetPolygon({ Stop.Position }, W, H, Lift)[0];
			FillPolygon(OutDrawElements, Layer + 7, AllottedGeometry, Circle(At, 11.0), FLinearColor::White);
			FillPolygon(OutDrawElements, Layer + 8, AllottedGeometry, Circle(At, 9.0), Blue);
			DrawLabel(OutDrawElements, Layer + 9, AllottedGeometry, FString::FromInt(StopIndex + 1), At, 11.f, OnBlue, false);
		}
	}

	// Where the device is, by GPS: on whichever level is shown (the GPS does not know the floor), so a wide pale halo around a smaller dot, not a point.
	if (const TOptional<FVector2D> Gps = GetGpsLevelPosition())
	{
		const FVector2D At = ToWidgetPolygon({ *Gps }, W, H, Lift)[0];
		if (const TOptional<FVector2D> Facing = GetGpsHeadingOnScreen(*Gps)) FillPolygon(OutDrawElements, Layer + 9, AllottedGeometry, HeadingCone(At, *Facing), FLinearColor(0.12f, 0.39f, 0.94f, 0.35f));
		FillPolygon(OutDrawElements, Layer + 9, AllottedGeometry, Circle(At, 20.0), FLinearColor(0.12f, 0.39f, 0.94f, 0.16f));
		FillPolygon(OutDrawElements, Layer + 10, AllottedGeometry, Circle(At, 7.5), FLinearColor(1.f, 1.f, 1.f, 0.9f));
		FillPolygon(OutDrawElements, Layer + 11, AllottedGeometry, Circle(At, 5.5), FLinearColor(0.12f, 0.39f, 0.94f, 0.85f));
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
	LatchedFocus.Reset();
}

void UMobileMapWidget::ZoomAround(const FVector2D& Local, double Factor)
{
	const double NewZoom = FMath::Clamp(Zoom * Factor, GetMinZoom(), MaxZoom);
	const double Actual = NewZoom / Zoom;
	// The point under the finger (or the middle) stays where it is: the plan is scaled about the middle of the uncovered part, then moved.
	const FVector2D Centre = ViewCentre();
	Pan = (Local - Centre) - (Local - Centre - Pan) * Actual;
	Zoom = NewZoom;
	ClampPan();
}

void UMobileMapWidget::ZoomBy(double Delta)
{
	const double Target = FMath::Clamp(Zoom + Delta, GetMinZoom(), MaxZoom);
	ZoomAround(ViewCentre(), Target / Zoom);
}

void UMobileMapWidget::SetRotation(double Radians)
{
	// Kept within a half turn either way.
	double Turned = FMath::Fmod(Radians, UE_DOUBLE_TWO_PI);
	if (Turned > UE_DOUBLE_PI) Turned -= UE_DOUBLE_TWO_PI;
	if (Turned < -UE_DOUBLE_PI) Turned += UE_DOUBLE_TWO_PI;
	if (FMath::IsNearlyEqual(Turned, Rotation, 1e-9))
	{
		return;
	}
	Rotation = Turned;
	if (OnRotationChanged)
	{
		OnRotationChanged(Rotation);
	}
}

void UMobileMapWidget::ResetRotation()
{
	// Back to north about the middle of the view: what is there stays there (the plan turns about its own middle, which on a big building is far from it).
	RotateAround(ViewCentre(), -Rotation);
}

void UMobileMapWidget::RotateAround(const FVector2D& Local, double DeltaRadians)
{
	if (FMath::IsNearlyZero(DeltaRadians, 1e-9))
	{
		return;
	}
	// The plan turns about its own middle, which stays where it is on the screen; so the middle is moved as a point turning about Local, and what is
	// under Local stays under it. (The tilted view only turns about the middle: a turn of the tilted picture is not a turn of the screen.)
	const bool bKeepUnder = !bShow3D && RawPlanBounds().bIsValid;
	FVector2D MiddleBefore = FVector2D::ZeroVector;
	if (bKeepUnder)
	{
		MiddleBefore = ToWidget(RawPlanBounds().GetCenter(), ViewSize.X, ViewSize.Y);
	}
	SetRotation(Rotation + DeltaRadians);
	if (bKeepUnder)
	{
		const double Cos = FMath::Cos(DeltaRadians);
		const double Sin = FMath::Sin(DeltaRadians);
		const FVector2D D = MiddleBefore - Local;
		const FVector2D MiddleWanted = Local + FVector2D(D.X * Cos - D.Y * Sin, D.X * Sin + D.Y * Cos);
		// Where the middle is now (on a big building turning moves it, as the view is built around the focus, not the middle).
		const FVector2D MiddleNow = ToWidget(RawPlanBounds().GetCenter(), ViewSize.X, ViewSize.Y);
		PanBy(MiddleWanted - MiddleNow);
	}
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
		TwistSoFar = 0.0;
		bTwisting = false;
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
			if (Event.IsShiftDown())
			{
				// A mouse has no second finger: Shift and a drag turn the map about the middle of the view, as far as the pointer goes round it.
				const FVector2D Centre = ViewCentre();
				const FVector2D From = Before - Centre;
				const FVector2D To = Local - Centre;
				if (From.Size() > 4.0 && To.Size() > 4.0)
				{
					double Delta = FMath::Atan2(To.Y, To.X) - FMath::Atan2(From.Y, From.X);
					if (Delta > UE_DOUBLE_PI) Delta -= UE_DOUBLE_TWO_PI;
					if (Delta < -UE_DOUBLE_PI) Delta += UE_DOUBLE_TWO_PI;
					RotateAround(Centre, Delta);
				}
			}
			else
			{
				PanBy(Local - Before);
			}
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
		// The line between the fingers turning is a twist, which turns the map once it is past a few degrees.
		if (OldDistance > 20.0 && NewDistance > 20.0)
		{
			const FVector2D OldLine = Before - Other;
			const FVector2D NewLine = Local - Other;
			double Twist = FMath::Atan2(NewLine.Y, NewLine.X) - FMath::Atan2(OldLine.Y, OldLine.X);
			if (Twist > UE_DOUBLE_PI) Twist -= UE_DOUBLE_TWO_PI;
			if (Twist < -UE_DOUBLE_PI) Twist += UE_DOUBLE_TWO_PI;
			if (!bTwisting)
			{
				TwistSoFar += Twist;
				bTwisting = FMath::Abs(TwistSoFar) > FMath::DegreesToRadians(8.0);
			}
			else
			{
				RotateAround(NewMiddle, Twist);
			}
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
