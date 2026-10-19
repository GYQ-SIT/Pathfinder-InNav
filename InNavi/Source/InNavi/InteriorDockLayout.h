#pragma once

#include "CoreMinimal.h"
#include "DockLayoutModel.h"

// The interior editor's panels as dock-layout names, its default arrangement, and loading/saving the layout.
namespace InteriorDock
{
	INNAVI_API extern const FName Toolbar;
	INNAVI_API extern const FName Floor;
	INNAVI_API extern const FName Properties;
	INNAVI_API extern const FName Layers;
	INNAVI_API extern const FName Navigation;
	INNAVI_API extern const FName Audio;
	INNAVI_API extern const FName Map;
	INNAVI_API extern const FName Debug;

	INNAVI_API const TArray<FName>& AllPanels();
	INNAVI_API TSet<FName> KnownPanels();

	// Where a panel is docked the first time it is shown if there is no saved position for it.
	INNAVI_API EDockSide DefaultSideFor(FName Panel);

	// The team's default arrangement: Map over Toolbar on the left; Floor, Layers and Properties stacked
	// on the right. Navigation (a tab beside Properties), Audio and Debug (bottom) start hidden, and
	// remember those spots for when the Window menu shows them.
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
