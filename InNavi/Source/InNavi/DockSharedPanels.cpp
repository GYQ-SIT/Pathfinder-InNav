#include "DockSharedPanels.h"

#include "ExteriorDockLayout.h"
#include "InteriorDockLayout.h"

namespace DockSharedPanels
{
	namespace
	{
		FState& Published()
		{
			static FState State;
			return State;
		}

		bool NearlyEqual(const FVector2D& A, const FVector2D& B)
		{
			return FMath::Abs(A.X - B.X) < 1.0 && FMath::Abs(A.Y - B.Y) < 1.0;
		}
	}

	TSet<FName> SharedPanels()
	{
		return InteriorDock::KnownPanels().Intersect(ExteriorDock::KnownPanels());
	}

	FState Capture(const FDockLayout& Layout, const TSet<FName>& Panels)
	{
		FState State;
		for (const FName Panel : Panels)
		{
			FPlacement Placement;
			FDockLocation Location;
			if (Layout.FindPanel(Panel, Location))
			{
				if (Location.Kind == FDockLocation::EKind::Floating && Layout.GetFloating().IsValidIndex(Location.FloatingIndex))
				{
					const FDockFloatingWindow& Window = Layout.GetFloating()[Location.FloatingIndex];
					Placement.Kind = FDockHint::EKind::Floating;
					Placement.FloatPosition = Window.Position;
					Placement.FloatSize = Window.Size;
				}
				else if (Location.Kind == FDockLocation::EKind::Area)
				{
					Placement.Kind = FDockHint::EKind::Docked;
					Placement.Side = Location.Side;
					Placement.Extent = Layout.GetAreaExtent(Location.Side);
				}
				else
				{
					continue;
				}
				if (Location.Group)
				{
					for (const FName Tab : Location.Group->Tabs)
					{
						if (Tab != Panel)
						{
							Placement.Neighbor = Tab;
							break;
						}
					}
				}
			}
			else if (const FDockHint* Hint = Layout.GetHint(Panel))
			{
				// Hidden: it stays hidden on the other screen too, and remembers there where it was.
				Placement.bHidden = true;
				Placement.Kind = Hint->Kind;
				Placement.Side = Hint->Side;
				Placement.Extent = Hint->Extent;
				Placement.FloatPosition = Hint->FloatPosition;
				Placement.FloatSize = Hint->FloatSize;
			}
			else
			{
				continue;
			}
			State.Add(Panel, Placement);
		}
		return State;
	}

	bool Apply(FDockLayout& Layout, const FState& State, const TSet<FName>& Panels)
	{
		bool bChanged = false;
		for (const TPair<FName, FPlacement>& Entry : State)
		{
			const FName Panel = Entry.Key;
			const FPlacement& Wanted = Entry.Value;
			if (!Panels.Contains(Panel))
			{
				continue;
			}
			if (Wanted.bHidden)
			{
				bChanged |= Layout.ContainsPanel(Panel) && Layout.HidePanel(Panel);
				continue;
			}

			FDockLocation Location;
			if (!Layout.FindPanel(Panel, Location))
			{
				// Shown here again: back where it was on this screen, then moved on to where it is on the other.
				Layout.ShowPanel(Panel, Wanted.Side);
				bChanged = true;
				if (!Layout.FindPanel(Panel, Location))
				{
					continue;
				}
			}

			if (Wanted.Kind == FDockHint::EKind::Floating)
			{
				bool bSame = false;
				if (Location.Kind == FDockLocation::EKind::Floating && Layout.GetFloating().IsValidIndex(Location.FloatingIndex))
				{
					const FDockFloatingWindow& Window = Layout.GetFloating()[Location.FloatingIndex];
					bSame = NearlyEqual(Window.Position, Wanted.FloatPosition) && NearlyEqual(Window.Size, Wanted.FloatSize);
				}
				if (!bSame)
				{
					bChanged |= Layout.FloatPanel(Panel, Wanted.FloatPosition, Wanted.FloatSize);
				}
				continue;
			}

			// Docked: on the same edge is the same place (what else is docked there is that screen's own).
			if (Location.Kind == FDockLocation::EKind::Area && Location.Side == Wanted.Side)
			{
				continue;
			}
			FDockLocation NeighborLocation;
			if (!Wanted.Neighbor.IsNone() && Wanted.Neighbor != Panel && Layout.FindPanel(Wanted.Neighbor, NeighborLocation)
				&& NeighborLocation.Kind == FDockLocation::EKind::Area && NeighborLocation.Side == Wanted.Side)
			{
				bChanged |= Layout.DockPanel(Panel, Wanted.Neighbor, EDockDropZone::Center);
				continue;
			}
			const bool bAreaWasEmpty = !Layout.GetAreaRoot(Wanted.Side).IsValid();
			if (Layout.DockPanelToEdge(Panel, Wanted.Side))
			{
				bChanged = true;
				if (bAreaWasEmpty && Wanted.Extent > 0.f)
				{
					Layout.SetAreaExtent(Wanted.Side, Wanted.Extent);
				}
			}
		}
		return bChanged;
	}

	void Publish(const FDockLayout& Layout)
	{
		for (const TPair<FName, FPlacement>& Entry : Capture(Layout, SharedPanels()))
		{
			Published().Add(Entry.Key, Entry.Value);
		}
	}

	bool ApplyPublished(FDockLayout& Layout)
	{
		return !Published().IsEmpty() && Apply(Layout, Published(), SharedPanels());
	}

	bool HasPublished()
	{
		return !Published().IsEmpty();
	}

	void ForgetPublished()
	{
		Published().Reset();
	}
}
