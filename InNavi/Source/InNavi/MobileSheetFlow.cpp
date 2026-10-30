#include "MobileSheetFlow.h"

bool FMobileSheetFlow::Handle(EMobileSheetEvent Event)
{
	struct FTransition
	{
		EMobileSheetState From;
		EMobileSheetEvent On;
		EMobileSheetState To;
	};
	static const FTransition Transitions[] = {
		{ EMobileSheetState::Search, EMobileSheetEvent::PlaceChosen, EMobileSheetState::Place },
		{ EMobileSheetState::Place, EMobileSheetEvent::DirectionsRequested, EMobileSheetState::Route },
		{ EMobileSheetState::Place, EMobileSheetEvent::Back, EMobileSheetState::Search },
		{ EMobileSheetState::Route, EMobileSheetEvent::NavigationStarted, EMobileSheetState::Navigating },
		{ EMobileSheetState::Route, EMobileSheetEvent::Back, EMobileSheetState::Place },
		{ EMobileSheetState::Navigating, EMobileSheetEvent::ArrivedAtDestination, EMobileSheetState::Arrived },
		{ EMobileSheetState::Navigating, EMobileSheetEvent::ExitNavigation, EMobileSheetState::Route },
		{ EMobileSheetState::Arrived, EMobileSheetEvent::FindAnotherPlace, EMobileSheetState::Search },
		{ EMobileSheetState::Arrived, EMobileSheetEvent::ViewDestination, EMobileSheetState::Place },
	};
	for (const FTransition& Transition : Transitions)
	{
		if (Transition.From == State && Transition.On == Event)
		{
			State = Transition.To;
			return true;
		}
	}
	return false;
}

const TCHAR* FMobileSheetFlow::StateName(EMobileSheetState State)
{
	switch (State)
	{
	case EMobileSheetState::Search: return TEXT("Search");
	case EMobileSheetState::Place: return TEXT("Place");
	case EMobileSheetState::Route: return TEXT("Route");
	case EMobileSheetState::Navigating: return TEXT("Navigating");
	case EMobileSheetState::Arrived: return TEXT("Arrived");
	}
	return TEXT("");
}

namespace MobileSheet
{
	namespace
	{
		constexpr double CollapsedFraction = 0.26;
		constexpr double HalfFraction = 0.44;
		constexpr double FullFraction = 0.88;
	}

	double SnapHeight(EMobileSheetSnap Snap, double ScreenHeight, double TabBarHeight, double HeaderHeight)
	{
		const double Workspace = FMath::Max(ScreenHeight - TabBarHeight - HeaderHeight, 100.0);
		switch (Snap)
		{
		case EMobileSheetSnap::Collapsed: return Workspace * CollapsedFraction;
		case EMobileSheetSnap::Half: return Workspace * HalfFraction;
		case EMobileSheetSnap::Full: return Workspace * FullFraction;
		}
		return Workspace * HalfFraction;
	}

	EMobileSheetSnap NearestSnap(double Height, double ScreenHeight, double TabBarHeight, double HeaderHeight)
	{
		EMobileSheetSnap Best = EMobileSheetSnap::Collapsed;
		double BestDistance = TNumericLimits<double>::Max();
		for (EMobileSheetSnap Candidate : { EMobileSheetSnap::Collapsed, EMobileSheetSnap::Half, EMobileSheetSnap::Full })
		{
			const double Distance = FMath::Abs(Height - SnapHeight(Candidate, ScreenHeight, TabBarHeight, HeaderHeight));
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Candidate;
			}
		}
		return Best;
	}

	EMobileSheetSnap NextSnap(EMobileSheetSnap Snap)
	{
		switch (Snap)
		{
		// A tap on the handle, as in the prototype: open all the way, or from all the way back to the middle.
		case EMobileSheetSnap::Collapsed: return EMobileSheetSnap::Full;
		case EMobileSheetSnap::Half: return EMobileSheetSnap::Full;
		case EMobileSheetSnap::Full: return EMobileSheetSnap::Half;
		}
		return EMobileSheetSnap::Half;
	}

	EMobileSheetSnap SnapForState(EMobileSheetState State)
	{
		return State == EMobileSheetState::Navigating ? EMobileSheetSnap::Collapsed : EMobileSheetSnap::Half;
	}
}
