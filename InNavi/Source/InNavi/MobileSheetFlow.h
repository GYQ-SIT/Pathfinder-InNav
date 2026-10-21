#pragma once

#include "CoreMinimal.h"

// The mobile app's bottom sheet follows one flow, like the design team's prototype: look for a place, see it, plan the route, walk it,
// arrive. This is that flow and the sheet's heights as plain logic with no widgets, so the screens only send events and show the state
// (and so it can be tested by itself).

enum class EMobileSheetState : uint8
{
	// "Where to?": the search box and the list of places.
	Search,
	// One place: its details, Get directions.
	Place,
	// "Your route": from, to, minutes, metres, the steps.
	Route,
	// Walking it: the next step and what is left.
	Navigating,
	// "You have arrived".
	Arrived,
};

enum class EMobileSheetEvent : uint8
{
	PlaceChosen,
	DirectionsRequested,
	NavigationStarted,
	ArrivedAtDestination,
	Back,
	ExitNavigation,
	FindAnotherPlace,
	ViewDestination,
};

class INNAVI_API FMobileSheetFlow
{
public:
	EMobileSheetState GetState() const { return State; }

	// True and the state changes if the event means something in the current state; otherwise false and nothing changes.
	bool Handle(EMobileSheetEvent Event);

	void Reset() { State = EMobileSheetState::Search; }

	static const TCHAR* StateName(EMobileSheetState State);

private:
	EMobileSheetState State = EMobileSheetState::Search;
};

// The three heights the sheet rests at, in design pixels (the phone screen is 375 wide).
enum class EMobileSheetSnap : uint8
{
	Collapsed,
	Half,
	Full,
};

namespace MobileSheet
{
	// How tall the sheet is at each rest height, for a screen ScreenHeight tall whose bottom TabBarHeight is the tab bar and whose top
	// TopMargin stays clear (the status bar, the top controls).
	INNAVI_API double SnapHeight(EMobileSheetSnap Snap, double ScreenHeight, double TabBarHeight, double TopMargin);

	// The rest height closest to a height the sheet was dragged to.
	INNAVI_API EMobileSheetSnap NearestSnap(double Height, double ScreenHeight, double TabBarHeight, double TopMargin);

	// A tap on the handle: collapsed, then half, then full, then back to collapsed.
	INNAVI_API EMobileSheetSnap NextSnap(EMobileSheetSnap Snap);

	// Where the sheet comes to rest when the flow moves to a state: the map matters most while walking.
	INNAVI_API EMobileSheetSnap SnapForState(EMobileSheetState State);
}
