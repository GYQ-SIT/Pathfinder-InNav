#pragma once

#include "CoreMinimal.h"
#include "DockLayoutModel.h"

// The interior screen and the exterior screen each have their own dock layout (and their own file), but some panels are on both of them: Audio,
// Map, Debug, Navigation, Validation. A panel of that kind is one panel to the person, so where it is - shown or hidden, on which edge, or
// floating where - is the same on both screens: the screen that is left publishes it, and the screen that opens takes it over.
namespace DockSharedPanels
{
	// Where one shared panel is, as the screen left it.
	struct INNAVI_API FPlacement
	{
		bool bHidden = false;
		FDockHint::EKind Kind = FDockHint::EKind::Docked;
		EDockSide Side = EDockSide::Left;
		float Extent = 0.f;                 // the thickness of that edge area (pixels)
		FName Neighbor = NAME_None;         // another panel of its tab group, if it had one
		FVector2D FloatPosition = FVector2D::ZeroVector;
		FVector2D FloatSize = FVector2D(320.f, 360.f);
	};
	using FState = TMap<FName, FPlacement>;

	// The panels both screens have (the interior's panel names and the exterior's, as both know them).
	INNAVI_API TSet<FName> SharedPanels();

	// The placement of each of Panels in Layout (panels the layout has never placed are left out).
	INNAVI_API FState Capture(const FDockLayout& Layout, const TSet<FName>& Panels);

	// Moves the panels of State that are in Panels to where State says, in Layout. Panels already there are not touched; the other panels of the
	// layout keep their places. Returns whether anything changed. The layout stays valid.
	INNAVI_API bool Apply(FDockLayout& Layout, const FState& State, const TSet<FName>& Panels);

	// What the screen last left, kept for the screen that opens next (one per run of the program, so the two screens agree whatever is on disk).
	// Publish records the shared panels of a layout; ApplyPublished moves the shared panels of a layout to the published placement, if there is one.
	INNAVI_API void Publish(const FDockLayout& Layout);
	INNAVI_API bool ApplyPublished(FDockLayout& Layout);
	INNAVI_API bool HasPublished();
	// Forgets what was published (tests).
	INNAVI_API void ForgetPublished();
}
