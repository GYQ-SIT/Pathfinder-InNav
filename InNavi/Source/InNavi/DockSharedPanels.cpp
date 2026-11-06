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

		bool AllIn(const TArray<FName>& Names, const TSet<FName>& Set)
		{
			for (const FName Name : Names)
			{
				if (!Set.Contains(Name))
				{
					return false;
				}
			}
			return true;
		}

		// Moves one panel (not part of a container) to where it was.
		bool ApplyPanel(FDockLayout& Layout, FName Panel, const FPlacement& Wanted)
		{
			bool bChanged = false;
			if (Wanted.bHidden)
			{
				return Layout.ContainsPanel(Panel) && Layout.HidePanel(Panel);
			}

			FDockLocation Location;
			if (!Layout.FindPanel(Panel, Location))
			{
				// Shown here again: back where it was on this screen, then moved on to where it is on the other.
				Layout.ShowPanel(Panel, Wanted.Side);
				bChanged = true;
				if (!Layout.FindPanel(Panel, Location))
				{
					return bChanged;
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
				return bSame ? bChanged : (Layout.FloatPanel(Panel, Wanted.FloatPosition, Wanted.FloatSize) || bChanged);
			}

			// Docked: on the same edge is the same place (what else is docked there is that screen's own).
			if (Location.Kind == FDockLocation::EKind::Area && Location.Side == Wanted.Side)
			{
				return bChanged;
			}
			FDockLocation NeighborLocation;
			if (!Wanted.Neighbor.IsNone() && Wanted.Neighbor != Panel && Layout.FindPanel(Wanted.Neighbor, NeighborLocation)
				&& NeighborLocation.Kind == FDockLocation::EKind::Area && NeighborLocation.Side == Wanted.Side)
			{
				return Layout.DockPanel(Panel, Wanted.Neighbor, EDockDropZone::Center) || bChanged;
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
			return bChanged;
		}
	}

	TSet<FName> SharedPanels()
	{
		return InteriorDock::KnownPanels().Intersect(ExteriorDock::KnownPanels());
	}

	FState Capture(const FDockLayout& Layout, const TSet<FName>& Panels)
	{
		FState State;

		// The containers (a floating window, an edge area) that hold shared panels only.
		for (const FDockFloatingWindow& Window : Layout.GetFloating())
		{
			if (Window.Root.IsValid() && AllIn(FDockLayout::PanelsOfTree(*Window.Root), Panels))
			{
				FContainer Container;
				Container.bFloating = true;
				Container.Position = Window.Position;
				Container.Size = Window.Size;
				Container.Tree = FDockLayout::CloneTree(*Window.Root);
				State.Containers.Add(MoveTemp(Container));
			}
		}
		for (int32 Index = 0; Index < DockSideCount; ++Index)
		{
			const EDockSide Side = static_cast<EDockSide>(Index);
			const TSharedPtr<FDockNode> Root = Layout.GetAreaRoot(Side);
			if (Root.IsValid() && AllIn(FDockLayout::PanelsOfTree(*Root), Panels))
			{
				FContainer Container;
				Container.Side = Side;
				Container.Extent = Layout.GetAreaExtent(Side);
				Container.Tree = FDockLayout::CloneTree(*Root);
				State.Containers.Add(MoveTemp(Container));
			}
		}

		// Each panel by itself, for the ones that are among panels of their own screen.
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
			State.Panels.Add(Panel, Placement);
		}
		return State;
	}

	bool Apply(FDockLayout& Layout, const FState& State, const TSet<FName>& Panels)
	{
		bool bChanged = false;
		TSet<FName> Done;

		// The containers first, whole.
		for (const FContainer& Container : State.Containers)
		{
			if (!Container.Tree.IsValid())
			{
				continue;
			}
			const TArray<FName> Held = FDockLayout::PanelsOfTree(*Container.Tree);
			if (!AllIn(Held, Panels))
			{
				continue;
			}
			const FString Shape = FDockLayout::DescribeTree(*Container.Tree);
			if (Container.bFloating)
			{
				bool bSame = false;
				for (const FDockFloatingWindow& Window : Layout.GetFloating())
				{
					if (Window.Root.IsValid() && FDockLayout::DescribeTree(*Window.Root) == Shape
						&& NearlyEqual(Window.Position, Container.Position) && NearlyEqual(Window.Size, Container.Size))
					{
						bSame = true;
						break;
					}
				}
				if (!bSame)
				{
					bChanged |= Layout.PlaceFloatingTree(*Container.Tree, Container.Position, Container.Size);
				}
			}
			else
			{
				const TSharedPtr<FDockNode> Current = Layout.GetAreaRoot(Container.Side);
				// An area that has panels of this screen's own is not replaced: what is shared in it is taken over panel by panel below.
				if (Current.IsValid() && !AllIn(FDockLayout::PanelsOfTree(*Current), Panels))
				{
					continue;
				}
				if (Current.IsValid() && FDockLayout::DescribeTree(*Current) == Shape && FMath::Abs(Layout.GetAreaExtent(Container.Side) - Container.Extent) < 1.f)
				{
					Done.Append(Held);
					continue;
				}
				if (Current.IsValid())
				{
					// The shared panels the area had that the new arrangement does not: out of the area, and remembered as hidden.
					const TSet<FName> Kept(Held);
					for (const FName Old : FDockLayout::PanelsOfTree(*Current))
					{
						if (!Kept.Contains(Old))
						{
							Layout.HidePanel(Old);
						}
					}
				}
				bChanged |= Layout.PlaceAreaTree(Container.Side, *Container.Tree, Container.Extent);
			}
			Done.Append(Held);
		}

		// The other panels, one by one.
		for (const TPair<FName, FPlacement>& Entry : State.Panels)
		{
			if (Panels.Contains(Entry.Key) && !Done.Contains(Entry.Key))
			{
				bChanged |= ApplyPanel(Layout, Entry.Key, Entry.Value);
			}
		}
		return bChanged;
	}

	void Publish(const FDockLayout& Layout)
	{
		Published() = Capture(Layout, SharedPanels());
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
		Published() = FState();
	}
}
