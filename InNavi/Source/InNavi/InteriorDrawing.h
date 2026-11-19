#pragma once

#include "CoreMinimal.h"

// What Shift does while a room, wall or shape is being drawn. Pure functions on footprint points, kept apart from the widget and the controller so
// the same rule draws the preview, places the point and is tested without a screen.
namespace InteriorDrawing
{
	// Shift while clicking out a room or wall: the new point goes straight along X or Y from the previous point, whichever way the pointer went
	// further, so the line is exactly horizontal or vertical.
	inline FVector2D AxisLocked(FVector2D Anchor, FVector2D Point)
	{
		const FVector2D Delta = Point - Anchor;
		return FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y) ? FVector2D(Point.X, Anchor.Y) : FVector2D(Anchor.X, Point.Y);
	}

	// Shift while dragging out a shape from Start to End: End is moved so the box has the shape's own proportions (Aspect = height / width: 1 for
	// a square or a circle, about 0.866 for an equilateral triangle). The longer pull wins and the direction of the drag is kept, so the box grows
	// from the corner the user pressed.
	inline FVector2D ProportionalCorner(FVector2D Start, FVector2D End, double Aspect)
	{
		if (Aspect <= UE_KINDA_SMALL_NUMBER)
		{
			return End;
		}
		const FVector2D Delta = End - Start;
		const double Width = FMath::Max(FMath::Abs(Delta.X), FMath::Abs(Delta.Y) / Aspect);
		const double SignX = Delta.X < 0.0 ? -1.0 : 1.0;
		const double SignY = Delta.Y < 0.0 ? -1.0 : 1.0;
		return Start + FVector2D(SignX * Width, SignY * Width * Aspect);
	}
}
