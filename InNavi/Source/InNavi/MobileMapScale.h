#pragma once

#include "CoreMinimal.h"

// How big things on the phone's map are, and how far in it starts, worked out from the map's scale (design pixels for a metre of floor) instead of being
// fixed. Pure arithmetic, so it is tested by itself (MobileMapScaleTests).
//
// Why: the plan used to be fitted whole into the screen, so a long building (E2/3 is 35 by 153 metres) came out at about 2 pixels a metre, and the lift and
// stairs circles (a fixed 18 pixels, 8 metres of floor at that scale) and the room names were as big as the rooms. Now the circles and names follow the
// scale, and the map starts at a fixed real-world width (about 100 metres across the screen) around where the person is or looks.
namespace MobileMapScale
{
	// A lift or a staircase is about this wide on the floor; its circle on the map is that many metres at the current scale, within the limits.
	constexpr double ConnectorGroundMetres = 3.0;
	constexpr double MinMarkerDiameter = 8.0;
	constexpr double MaxMarkerDiameter = 20.0;
	// The picture of the kind of connector is only drawn in a circle at least this wide (a smaller one is a plain coloured dot).
	constexpr double GlyphFromDiameter = 12.0;
	// The map starts showing this much of the floor across the screen (at least; a small building is shown whole).
	constexpr double StartAcrossMetres = 100.0;
	// A name is drawn only if it is not wider than the room by more than this.
	constexpr double LabelRoomSlack = 1.1;

	inline double MarkerDiameter(double PixelsPerMetre)
	{
		return FMath::Clamp(ConnectorGroundMetres * PixelsPerMetre, MinMarkerDiameter, MaxMarkerDiameter);
	}

	inline bool ShowsGlyph(double Diameter) { return Diameter >= GlyphFromDiameter; }

	// The scale the map starts at, in design pixels for a unit of the plan: the one that fits the whole plan, or the one that shows StartAcrossMetres across the
	// width the map has (AvailableWidth), whichever is the larger. UnitsPerMetre is how many units of the plan make a metre.
	inline double StartScale(double FitScale, double AvailableWidth, double UnitsPerMetre)
	{
		const double Units = FMath::Max(UnitsPerMetre, 1e-6);
		return FMath::Max(FitScale, AvailableWidth / (StartAcrossMetres * Units));
	}

	// The smallest zoom (the multiplier of the start scale): the same 0.7 beyond the fit as before, now counted from the fit, which is below the start scale
	// for a building too big to be shown whole.
	inline double MinZoomFor(double FitScale, double Start, double ZoomOutLimit)
	{
		return Start > 1e-9 ? FMath::Min(ZoomOutLimit, ZoomOutLimit * FitScale / Start) : ZoomOutLimit;
	}

	// "Lectorial 6, E2-01-01" is drawn as "Lectorial 6": the code after the comma is for the list, not for a room on the map.
	inline FString ShortName(const FString& Name)
	{
		int32 Comma = INDEX_NONE;
		if (Name.FindChar(TEXT(','), Comma) && Comma > 0)
		{
			return Name.Left(Comma).TrimStartAndEnd();
		}
		return Name;
	}

	// A room that has no name of its own is called by its id ("SQL-1161271361-01-09"): that is not a name to put on a map.
	inline bool LooksLikeRawId(const FString& Name)
	{
		if (Name.StartsWith(TEXT("SQL-"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		int32 Run = 0, Longest = 0;
		for (const TCHAR Character : Name)
		{
			Run = FChar::IsDigit(Character) ? Run + 1 : 0;
			Longest = FMath::Max(Longest, Run);
		}
		return Longest >= 8;
	}

	inline bool LabelFits(double TextWidth, double RoomWidth) { return TextWidth <= RoomWidth * LabelRoomSlack; }
}
