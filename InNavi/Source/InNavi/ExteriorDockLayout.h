#pragma once

#include "CoreMinimal.h"
#include "DockLayoutModel.h"

// The exterior (building-selection) screen's panels as dock-layout names, its default arrangement, and
// loading/saving the layout. Same mechanism as InteriorDockLayout.h, with its own panel set and its own
// files so the two screens' arrangements never affect each other.
namespace ExteriorDock
{
	INNAVI_API extern const FName Audio;
	INNAVI_API extern const FName Map;
	INNAVI_API extern const FName Building;
	INNAVI_API extern const FName Debug;
	INNAVI_API extern const FName Navigation;

	INNAVI_API const TArray<FName>& AllPanels();
	INNAVI_API TSet<FName> KnownPanels();

	// Where a panel is docked the first time it is shown if there is no saved position for it.
	INNAVI_API EDockSide DefaultSideFor(FName Panel);

	// Map on the left edge and Building (the selected building) on the right, like the design page; Audio and Debug
	// (tabs of one group along the bottom) start hidden and remember that spot for when the Window menu shows them.
	INNAVI_API void BuildDefaultLayout(FDockLayout& Layout);

	// Panels the file did not mention are placed the way the default layout would have: visible ones are
	// shown at their default side, the default-hidden ones stay hidden.
	INNAVI_API void PlaceMissingPanels(FDockLayout& Layout, const TArray<FName>& Missing);

	INNAVI_API FString UserLayoutPath();
	INNAVI_API FString TeamLayoutPath();

	INNAVI_API bool LoadFromFile(FDockLayout& Layout, const FString& Path, const FVector2D& Viewport);
	INNAVI_API bool SaveToFile(const FDockLayout& Layout, const FString& Path, const FVector2D& Viewport);

	// The user's own saved arrangement if there is one, else the team's shipped file, else the built-in
	// default. Never leaves Layout empty.
	INNAVI_API void LoadLayout(FDockLayout& Layout, const FVector2D& Viewport);
}
