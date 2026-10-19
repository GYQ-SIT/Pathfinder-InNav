#include "DockLayoutModel.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	constexpr float MinRatio = 0.02f;
	constexpr int32 MaxTreeDepth = 16;

	bool BoxContains(const FBox2D& Box, const FVector2D& P)
	{
		return P.X >= Box.Min.X && P.X <= Box.Max.X && P.Y >= Box.Min.Y && P.Y <= Box.Max.Y;
	}

	FBox2D MakeBox(float MinX, float MinY, float MaxX, float MaxY)
	{
		return FBox2D(FVector2D(MinX, MinY), FVector2D(MaxX, MaxY));
	}

	FName FirstPanelIn(const FDockNode& Node)
	{
		if (Node.IsTabGroup())
		{
			return Node.Tabs.Num() > 0 ? Node.Tabs[0] : NAME_None;
		}
		for (const TSharedPtr<FDockNode>& Child : Node.Children)
		{
			if (Child.IsValid())
			{
				const FName Found = FirstPanelIn(*Child);
				if (!Found.IsNone())
				{
					return Found;
				}
			}
		}
		return NAME_None;
	}

	void CollectPanels(const FDockNode& Node, TArray<FName>& Out)
	{
		if (Node.IsTabGroup())
		{
			Out.Append(Node.Tabs);
			return;
		}
		for (const TSharedPtr<FDockNode>& Child : Node.Children)
		{
			if (Child.IsValid())
			{
				CollectPanels(*Child, Out);
			}
		}
	}

	TSharedPtr<FDockNode> CloneNode(const TSharedPtr<FDockNode>& Src)
	{
		if (!Src.IsValid())
		{
			return nullptr;
		}
		TSharedPtr<FDockNode> Copy = MakeShared<FDockNode>();
		Copy->Id = Src->Id;
		Copy->Type = Src->Type;
		Copy->Orientation = Src->Orientation;
		Copy->Ratios = Src->Ratios;
		Copy->Tabs = Src->Tabs;
		Copy->ActiveTab = Src->ActiveTab;
		for (const TSharedPtr<FDockNode>& Child : Src->Children)
		{
			Copy->Children.Add(CloneNode(Child));
		}
		return Copy;
	}

	// A panel may appear only once in the whole layout. Keeps the first occurrence.
	void DedupeTabs(const TSharedPtr<FDockNode>& Node, TSet<FName>& Seen)
	{
		if (!Node.IsValid())
		{
			return;
		}
		if (Node->IsTabGroup())
		{
			const FName ActiveName = Node->Tabs.IsValidIndex(Node->ActiveTab) ? Node->Tabs[Node->ActiveTab] : NAME_None;
			TArray<FName> Kept;
			for (const FName Tab : Node->Tabs)
			{
				if (Tab.IsNone() || Seen.Contains(Tab))
				{
					continue;
				}
				Seen.Add(Tab);
				Kept.Add(Tab);
			}
			const int32 NewActive = Kept.IndexOfByKey(ActiveName);
			Node->Tabs = Kept;
			Node->ActiveTab = NewActive != INDEX_NONE ? NewActive : 0;
			return;
		}
		for (const TSharedPtr<FDockNode>& Child : Node->Children)
		{
			DedupeTabs(Child, Seen);
		}
	}

	// Removes empty groups, collapses single-child splits, flattens a split nested in a split of the same
	// orientation, and makes ratios valid (positive, sum 1). Sets Slot to null if nothing is left.
	void NormalizeNode(TSharedPtr<FDockNode>& Slot)
	{
		if (!Slot.IsValid())
		{
			return;
		}

		FDockNode& Node = *Slot;
		if (Node.IsTabGroup())
		{
			if (Node.Tabs.Num() == 0)
			{
				Slot.Reset();
				return;
			}
			Node.ActiveTab = FMath::Clamp(Node.ActiveTab, 0, Node.Tabs.Num() - 1);
			return;
		}

		const bool bRatiosMatch = Node.Ratios.Num() == Node.Children.Num();
		const float EqualShare = Node.Children.Num() > 0 ? 1.f / (float)Node.Children.Num() : 1.f;

		TArray<TSharedPtr<FDockNode>> NewChildren;
		TArray<float> NewRatios; // <= 0 means unknown, filled in below
		for (int32 i = 0; i < Node.Children.Num(); ++i)
		{
			TSharedPtr<FDockNode> Child = Node.Children[i];
			NormalizeNode(Child);
			if (!Child.IsValid())
			{
				continue;
			}

			const bool bRatioUsable = bRatiosMatch && FMath::IsFinite(Node.Ratios[i]) && Node.Ratios[i] > 0.f;
			if (Child->IsSplit() && Child->Orientation == Node.Orientation)
			{
				const float Share = bRatioUsable ? Node.Ratios[i] : EqualShare;
				for (int32 j = 0; j < Child->Children.Num(); ++j)
				{
					NewChildren.Add(Child->Children[j]);
					NewRatios.Add(Child->Ratios[j] * Share);
				}
			}
			else
			{
				NewChildren.Add(Child);
				NewRatios.Add(bRatioUsable ? Node.Ratios[i] : -1.f);
			}
		}

		if (NewChildren.Num() == 0)
		{
			Slot.Reset();
			return;
		}
		if (NewChildren.Num() == 1)
		{
			Slot = NewChildren[0];
			return;
		}

		float KnownSum = 0.f;
		int32 KnownCount = 0;
		for (const float R : NewRatios)
		{
			if (R > 0.f)
			{
				KnownSum += R;
				++KnownCount;
			}
		}
		const float Fill = KnownCount > 0 ? KnownSum / (float)KnownCount : 1.f;
		for (float& R : NewRatios)
		{
			if (R <= 0.f)
			{
				R = Fill;
			}
		}

		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			float Sum = 0.f;
			for (const float R : NewRatios)
			{
				Sum += R;
			}
			for (float& R : NewRatios)
			{
				R = FMath::Max(R / Sum, MinRatio);
			}
		}
		float FinalSum = 0.f;
		for (const float R : NewRatios)
		{
			FinalSum += R;
		}
		for (float& R : NewRatios)
		{
			R /= FinalSum;
		}

		Node.Children = NewChildren;
		Node.Ratios = NewRatios;
	}

	bool ValidateNode(const TSharedPtr<FDockNode>& Node, const FDockNode* Parent, TSet<FName>& Seen, FString& Err, int32 Depth)
	{
		if (!Node.IsValid())
		{
			Err = TEXT("null node inside a tree");
			return false;
		}
		if (Depth > MaxTreeDepth)
		{
			Err = TEXT("tree too deep");
			return false;
		}
		if (Node->IsTabGroup())
		{
			if (Node->Tabs.Num() == 0)
			{
				Err = TEXT("empty tab group");
				return false;
			}
			if (!Node->Tabs.IsValidIndex(Node->ActiveTab))
			{
				Err = FString::Printf(TEXT("active tab %d out of range (%d tabs)"), Node->ActiveTab, Node->Tabs.Num());
				return false;
			}
			for (const FName Tab : Node->Tabs)
			{
				if (Tab.IsNone())
				{
					Err = TEXT("tab with no name");
					return false;
				}
				if (Seen.Contains(Tab))
				{
					Err = FString::Printf(TEXT("panel %s appears more than once"), *Tab.ToString());
					return false;
				}
				Seen.Add(Tab);
			}
			return true;
		}

		if (Node->Children.Num() < 2)
		{
			Err = TEXT("split with fewer than 2 children");
			return false;
		}
		if (Node->Ratios.Num() != Node->Children.Num())
		{
			Err = TEXT("split ratios/children count mismatch");
			return false;
		}
		if (Parent && Parent->IsSplit() && Parent->Orientation == Node->Orientation)
		{
			Err = TEXT("split nested in a split of the same orientation");
			return false;
		}
		float Sum = 0.f;
		for (const float R : Node->Ratios)
		{
			if (!FMath::IsFinite(R) || R <= 0.f)
			{
				Err = TEXT("non-positive or non-finite ratio");
				return false;
			}
			Sum += R;
		}
		if (FMath::Abs(Sum - 1.f) > 1e-3f)
		{
			Err = FString::Printf(TEXT("ratios sum to %f"), Sum);
			return false;
		}
		for (const TSharedPtr<FDockNode>& Child : Node->Children)
		{
			if (!ValidateNode(Child, Node.Get(), Seen, Err, Depth + 1))
			{
				return false;
			}
		}
		return true;
	}

	// Splits Avail pixels between children by Ratios, honoring MinChild where possible.
	TArray<float> ComputeChildLengths(const TArray<float>& Ratios, float Avail, float MinChild)
	{
		const int32 N = Ratios.Num();
		TArray<float> Out;
		Out.SetNumZeroed(N);
		if (N == 0)
		{
			return Out;
		}
		if ((float)N * MinChild > Avail)
		{
			for (float& L : Out)
			{
				L = Avail / (float)N;
			}
			return Out;
		}

		TArray<bool> Pinned;
		Pinned.SetNumZeroed(N);
		for (int32 Iteration = 0; Iteration < N; ++Iteration)
		{
			float PinnedTotal = 0.f;
			float FreeRatioSum = 0.f;
			for (int32 i = 0; i < N; ++i)
			{
				if (Pinned[i])
				{
					PinnedTotal += MinChild;
				}
				else
				{
					FreeRatioSum += Ratios[i];
				}
			}
			const float Remaining = Avail - PinnedTotal;
			bool bNewlyPinned = false;
			for (int32 i = 0; i < N; ++i)
			{
				if (Pinned[i])
				{
					Out[i] = MinChild;
					continue;
				}
				Out[i] = FreeRatioSum > 0.f ? Remaining * Ratios[i] / FreeRatioSum : Remaining / (float)N;
				if (Out[i] < MinChild)
				{
					Pinned[i] = true;
					bNewlyPinned = true;
				}
			}
			if (!bNewlyPinned)
			{
				break;
			}
		}
		for (int32 i = 0; i < N; ++i)
		{
			if (Pinned[i])
			{
				Out[i] = MinChild;
			}
		}
		return Out;
	}

	void LayoutNode(const FDockNode& Node, const FBox2D& Rect, const FDockMetrics& M, EDockSide Side, int32 FloatingIndex, FDockComputedLayout& Out)
	{
		const FVector2D Size = Rect.GetSize();
		if (Node.IsTabGroup())
		{
			FDockComputedGroup G;
			G.Node = &Node;
			G.Rect = Rect;
			G.Side = Side;
			G.FloatingIndex = FloatingIndex;

			const float TabBarHeight = FMath::Min(M.TabBarHeight, (float)Size.Y);
			G.TabBarRect = MakeBox((float)Rect.Min.X, (float)Rect.Min.Y, (float)Rect.Max.X, (float)Rect.Min.Y + TabBarHeight);
			G.ContentRect = MakeBox((float)Rect.Min.X, (float)Rect.Min.Y + TabBarHeight, (float)Rect.Max.X, (float)Rect.Max.Y);

			const int32 N = Node.Tabs.Num();
			if (N > 0)
			{
				const float TabWidth = FDockLayout::ComputeTabWidth((float)Size.X, N, M);
				for (int32 i = 0; i < N; ++i)
				{
					const float X = (float)Rect.Min.X + TabWidth * (float)i;
					G.TabRects.Add(MakeBox(X, (float)Rect.Min.Y, X + TabWidth, (float)Rect.Min.Y + TabBarHeight));
				}
			}
			Out.Groups.Add(G);
			return;
		}

		const int32 N = Node.Children.Num();
		const bool bHorizontal = Node.Orientation == EDockOrientation::Horizontal;
		const float Length = bHorizontal ? (float)Size.X : (float)Size.Y;
		const float Avail = FMath::Max(0.f, Length - (float)(N - 1) * M.SplitterThickness);
		const float MinChild = bHorizontal ? (float)M.MinPanelSize.X : (float)M.MinPanelSize.Y;
		const TArray<float> Lengths = ComputeChildLengths(Node.Ratios, Avail, MinChild);

		float Cursor = bHorizontal ? (float)Rect.Min.X : (float)Rect.Min.Y;
		for (int32 i = 0; i < N; ++i)
		{
			FBox2D ChildRect;
			if (bHorizontal)
			{
				ChildRect = MakeBox(Cursor, (float)Rect.Min.Y, Cursor + Lengths[i], (float)Rect.Max.Y);
			}
			else
			{
				ChildRect = MakeBox((float)Rect.Min.X, Cursor, (float)Rect.Max.X, Cursor + Lengths[i]);
			}
			if (Node.Children[i].IsValid())
			{
				LayoutNode(*Node.Children[i], ChildRect, M, Side, FloatingIndex, Out);
			}
			Cursor += Lengths[i];

			if (i + 1 < N)
			{
				FDockComputedSplitter S;
				S.SplitNode = &Node;
				S.DividerIndex = i;
				S.Orientation = Node.Orientation;
				S.AvailablePx = Avail;
				S.Side = Side;
				S.FloatingIndex = FloatingIndex;
				S.Rect = bHorizontal
					? MakeBox(Cursor, (float)Rect.Min.Y, Cursor + M.SplitterThickness, (float)Rect.Max.Y)
					: MakeBox((float)Rect.Min.X, Cursor, (float)Rect.Max.X, Cursor + M.SplitterThickness);
				Out.Splitters.Add(S);
				Cursor += M.SplitterThickness;
			}
		}
	}

	const TCHAR* SideToString(EDockSide Side)
	{
		switch (Side)
		{
		case EDockSide::Left: return TEXT("Left");
		case EDockSide::Right: return TEXT("Right");
		case EDockSide::Top: return TEXT("Top");
		default: return TEXT("Bottom");
		}
	}

	bool SideFromString(const FString& S, EDockSide& Out)
	{
		for (int32 i = 0; i < DockSideCount; ++i)
		{
			if (S == SideToString((EDockSide)i))
			{
				Out = (EDockSide)i;
				return true;
			}
		}
		return false;
	}

	const TCHAR* ZoneToString(EDockDropZone Zone)
	{
		switch (Zone)
		{
		case EDockDropZone::Left: return TEXT("Left");
		case EDockDropZone::Right: return TEXT("Right");
		case EDockDropZone::Top: return TEXT("Top");
		case EDockDropZone::Bottom: return TEXT("Bottom");
		case EDockDropZone::Center: return TEXT("Center");
		default: return TEXT("None");
		}
	}

	EDockDropZone ZoneFromString(const FString& S)
	{
		for (int32 i = 0; i <= (int32)EDockDropZone::Center; ++i)
		{
			if (S == ZoneToString((EDockDropZone)i))
			{
				return (EDockDropZone)i;
			}
		}
		return EDockDropZone::None;
	}

	bool IsHorizontalStrip(EDockSide Side)
	{
		return Side == EDockSide::Left || Side == EDockSide::Right;
	}

	TSharedPtr<FJsonObject> NodeToJson(const FDockNode& Node)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (Node.IsTabGroup())
		{
			Obj->SetStringField(TEXT("type"), TEXT("tabs"));
			TArray<TSharedPtr<FJsonValue>> Tabs;
			for (const FName Tab : Node.Tabs)
			{
				Tabs.Add(MakeShared<FJsonValueString>(Tab.ToString()));
			}
			Obj->SetArrayField(TEXT("tabs"), Tabs);
			Obj->SetNumberField(TEXT("active"), Node.ActiveTab);
			return Obj;
		}

		Obj->SetStringField(TEXT("type"), TEXT("split"));
		Obj->SetStringField(TEXT("orientation"), Node.Orientation == EDockOrientation::Horizontal ? TEXT("horizontal") : TEXT("vertical"));
		TArray<TSharedPtr<FJsonValue>> Ratios;
		for (const float R : Node.Ratios)
		{
			Ratios.Add(MakeShared<FJsonValueNumber>(R));
		}
		Obj->SetArrayField(TEXT("ratios"), Ratios);
		TArray<TSharedPtr<FJsonValue>> Children;
		for (const TSharedPtr<FDockNode>& Child : Node.Children)
		{
			if (Child.IsValid())
			{
				Children.Add(MakeShared<FJsonValueObject>(NodeToJson(*Child)));
			}
		}
		Obj->SetArrayField(TEXT("children"), Children);
		return Obj;
	}

	// Returns null (and leaves bOk true) when the node has nothing left after dropping unknown panels.
	TSharedPtr<FDockNode> NodeFromJson(const TSharedPtr<FJsonObject>& Obj, const TSet<FName>& Known, int32 Depth, int32& IdCounter, bool& bOk)
	{
		if (!bOk || !Obj.IsValid())
		{
			bOk = false;
			return nullptr;
		}
		if (Depth > MaxTreeDepth)
		{
			bOk = false;
			return nullptr;
		}

		FString Type;
		if (!Obj->TryGetStringField(TEXT("type"), Type))
		{
			bOk = false;
			return nullptr;
		}

		TSharedPtr<FDockNode> Node = MakeShared<FDockNode>();
		Node->Id = IdCounter++;

		if (Type == TEXT("tabs"))
		{
			Node->Type = FDockNode::EType::TabGroup;
			const TArray<TSharedPtr<FJsonValue>>* TabsArray = nullptr;
			if (!Obj->TryGetArrayField(TEXT("tabs"), TabsArray))
			{
				bOk = false;
				return nullptr;
			}
			double ActiveRaw = 0.0;
			Obj->TryGetNumberField(TEXT("active"), ActiveRaw);
			const int32 ActiveIndex = (int32)ActiveRaw;
			FName ActiveName = NAME_None;
			for (int32 i = 0; i < TabsArray->Num(); ++i)
			{
				FString Name;
				if (!(*TabsArray)[i].IsValid() || !(*TabsArray)[i]->TryGetString(Name))
				{
					continue;
				}
				const FName Tab(*Name);
				if (!Known.Contains(Tab))
				{
					continue;
				}
				Node->Tabs.Add(Tab);
				if (i == ActiveIndex)
				{
					ActiveName = Tab;
				}
			}
			if (Node->Tabs.Num() == 0)
			{
				return nullptr;
			}
			const int32 ActiveResolved = Node->Tabs.IndexOfByKey(ActiveName);
			Node->ActiveTab = ActiveResolved != INDEX_NONE ? ActiveResolved : 0;
			return Node;
		}

		if (Type == TEXT("split"))
		{
			Node->Type = FDockNode::EType::Split;
			FString Orientation;
			if (!Obj->TryGetStringField(TEXT("orientation"), Orientation))
			{
				bOk = false;
				return nullptr;
			}
			if (Orientation == TEXT("horizontal"))
			{
				Node->Orientation = EDockOrientation::Horizontal;
			}
			else if (Orientation == TEXT("vertical"))
			{
				Node->Orientation = EDockOrientation::Vertical;
			}
			else
			{
				bOk = false;
				return nullptr;
			}

			const TArray<TSharedPtr<FJsonValue>>* ChildrenArray = nullptr;
			if (!Obj->TryGetArrayField(TEXT("children"), ChildrenArray))
			{
				bOk = false;
				return nullptr;
			}
			const TArray<TSharedPtr<FJsonValue>>* RatiosArray = nullptr;
			const bool bHasRatios = Obj->TryGetArrayField(TEXT("ratios"), RatiosArray) && RatiosArray->Num() == ChildrenArray->Num();

			for (int32 i = 0; i < ChildrenArray->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* ChildObj = nullptr;
				if (!(*ChildrenArray)[i].IsValid() || !(*ChildrenArray)[i]->TryGetObject(ChildObj))
				{
					bOk = false;
					return nullptr;
				}
				TSharedPtr<FDockNode> Child = NodeFromJson(*ChildObj, Known, Depth + 1, IdCounter, bOk);
				if (!bOk)
				{
					return nullptr;
				}
				if (!Child.IsValid())
				{
					continue;
				}
				double Ratio = -1.0;
				if (bHasRatios)
				{
					(*RatiosArray)[i]->TryGetNumber(Ratio);
				}
				Node->Children.Add(Child);
				Node->Ratios.Add((float)Ratio);
			}
			return Node->Children.Num() > 0 ? Node : nullptr;
		}

		bOk = false;
		return nullptr;
	}
}

const FDockComputedGroup* FDockComputedLayout::FindGroup(const FDockNode* Node) const
{
	for (const FDockComputedGroup& G : Groups)
	{
		if (G.Node == Node)
		{
			return &G;
		}
	}
	return nullptr;
}

FDockLayout::FDockLayout()
{
	DefaultExtent[(int32)EDockSide::Left] = 280.f;
	DefaultExtent[(int32)EDockSide::Right] = 280.f;
	DefaultExtent[(int32)EDockSide::Top] = 200.f;
	DefaultExtent[(int32)EDockSide::Bottom] = 200.f;
}

TSharedPtr<FDockNode> FDockLayout::MakeGroup(FName Panel)
{
	TSharedPtr<FDockNode> Group = MakeShared<FDockNode>();
	Group->Id = NextNodeId++;
	Group->Type = FDockNode::EType::TabGroup;
	Group->Tabs.Add(Panel);
	Group->ActiveTab = 0;
	return Group;
}

bool FDockLayout::FindInSlot(TSharedPtr<FDockNode>* Slot, FDockNode* Parent, int32 IndexInParent, FName Panel, FNodeRef& OutRef)
{
	if (!Slot || !Slot->IsValid())
	{
		return false;
	}
	FDockNode* Node = Slot->Get();
	if (Node->IsTabGroup())
	{
		if (Node->Tabs.Contains(Panel))
		{
			OutRef.Slot = Slot;
			OutRef.Parent = Parent;
			OutRef.IndexInParent = IndexInParent;
			return true;
		}
		return false;
	}
	for (int32 i = 0; i < Node->Children.Num(); ++i)
	{
		if (FindInSlot(&Node->Children[i], Node, i, Panel, OutRef))
		{
			return true;
		}
	}
	return false;
}

bool FDockLayout::FindGroupSlot(FName Panel, FNodeRef& OutRef)
{
	for (int32 s = 0; s < DockSideCount; ++s)
	{
		if (FindInSlot(&Areas[s].Root, nullptr, INDEX_NONE, Panel, OutRef))
		{
			return true;
		}
	}
	for (int32 i = 0; i < Floating.Num(); ++i)
	{
		if (FindInSlot(&Floating[i].Root, nullptr, INDEX_NONE, Panel, OutRef))
		{
			return true;
		}
	}
	return false;
}

bool FDockLayout::FindPanel(FName Panel, FDockLocation& OutLocation) const
{
	FDockLayout* Mutable = const_cast<FDockLayout*>(this);
	FNodeRef Ref;
	if (!Mutable->FindGroupSlot(Panel, Ref))
	{
		return false;
	}

	OutLocation = FDockLocation();
	OutLocation.Group = Ref.Slot->Get();
	OutLocation.TabIndex = OutLocation.Group->Tabs.IndexOfByKey(Panel);

	// Which top-level root is this group under? Walk up by searching each root for the group pointer.
	struct FContains
	{
		static bool Run(const FDockNode* Node, const FDockNode* Target)
		{
			if (!Node)
			{
				return false;
			}
			if (Node == Target)
			{
				return true;
			}
			for (const TSharedPtr<FDockNode>& Child : Node->Children)
			{
				if (Run(Child.Get(), Target))
				{
					return true;
				}
			}
			return false;
		}
	};
	for (int32 s = 0; s < DockSideCount; ++s)
	{
		if (FContains::Run(Areas[s].Root.Get(), OutLocation.Group))
		{
			OutLocation.Kind = FDockLocation::EKind::Area;
			OutLocation.Side = (EDockSide)s;
			return true;
		}
	}
	for (int32 i = 0; i < Floating.Num(); ++i)
	{
		if (FContains::Run(Floating[i].Root.Get(), OutLocation.Group))
		{
			OutLocation.Kind = FDockLocation::EKind::Floating;
			OutLocation.FloatingIndex = i;
			return true;
		}
	}
	return false;
}

bool FDockLayout::ContainsPanel(FName Panel) const
{
	FDockLocation Location;
	return !Panel.IsNone() && FindPanel(Panel, Location);
}

TArray<FName> FDockLayout::GetAllPanels() const
{
	TArray<FName> Out;
	for (int32 s = 0; s < DockSideCount; ++s)
	{
		if (Areas[s].Root.IsValid())
		{
			CollectPanels(*Areas[s].Root, Out);
		}
	}
	for (const FDockFloatingWindow& W : Floating)
	{
		if (W.Root.IsValid())
		{
			CollectPanels(*W.Root, Out);
		}
	}
	return Out;
}

bool FDockLayout::RemovePanelTab(FName Panel)
{
	FNodeRef Ref;
	if (!FindGroupSlot(Panel, Ref))
	{
		return false;
	}
	FDockNode& Group = **Ref.Slot;
	const int32 Index = Group.Tabs.IndexOfByKey(Panel);
	const FName ActiveName = Group.Tabs.IsValidIndex(Group.ActiveTab) ? Group.Tabs[Group.ActiveTab] : NAME_None;
	Group.Tabs.RemoveAt(Index);
	if (Group.Tabs.Num() > 0)
	{
		const int32 Active = Group.Tabs.IndexOfByKey(ActiveName);
		Group.ActiveTab = Active != INDEX_NONE ? Active : FMath::Min(Index, Group.Tabs.Num() - 1);
	}
	else
	{
		Group.ActiveTab = 0;
	}
	return true;
}

void FDockLayout::Normalize()
{
	TSet<FName> Seen;
	for (int32 s = 0; s < DockSideCount; ++s)
	{
		DedupeTabs(Areas[s].Root, Seen);
	}
	for (FDockFloatingWindow& W : Floating)
	{
		DedupeTabs(W.Root, Seen);
	}

	for (int32 s = 0; s < DockSideCount; ++s)
	{
		NormalizeNode(Areas[s].Root);
		if (!FMath::IsFinite(Areas[s].Extent) || Areas[s].Extent <= 0.f)
		{
			Areas[s].Extent = DefaultExtent[s];
		}
	}

	for (int32 i = Floating.Num() - 1; i >= 0; --i)
	{
		NormalizeNode(Floating[i].Root);
		if (!Floating[i].Root.IsValid())
		{
			Floating.RemoveAt(i);
			continue;
		}
		FDockFloatingWindow& W = Floating[i];
		if (!FMath::IsFinite(W.Position.X) || !FMath::IsFinite(W.Position.Y))
		{
			W.Position = FVector2D::ZeroVector;
		}
		if (!FMath::IsFinite(W.Size.X) || !FMath::IsFinite(W.Size.Y) || W.Size.X < 50.0 || W.Size.Y < 50.0)
		{
			W.Size = FVector2D(FMath::Max(50.0, FMath::IsFinite(W.Size.X) ? W.Size.X : 320.0), FMath::Max(50.0, FMath::IsFinite(W.Size.Y) ? W.Size.Y : 360.0));
		}
	}

	for (const FName Panel : Seen)
	{
		HiddenHints.Remove(Panel);
	}
}

bool FDockLayout::Validate(FString& OutError) const
{
	TSet<FName> Seen;
	for (int32 s = 0; s < DockSideCount; ++s)
	{
		if (Areas[s].Root.IsValid() && !ValidateNode(Areas[s].Root, nullptr, Seen, OutError, 0))
		{
			OutError = FString::Printf(TEXT("%s area: %s"), SideToString((EDockSide)s), *OutError);
			return false;
		}
		if (Areas[s].Root.IsValid() && (!FMath::IsFinite(Areas[s].Extent) || Areas[s].Extent <= 0.f))
		{
			OutError = FString::Printf(TEXT("%s area has an invalid extent"), SideToString((EDockSide)s));
			return false;
		}
	}
	for (int32 i = 0; i < Floating.Num(); ++i)
	{
		if (!Floating[i].Root.IsValid())
		{
			OutError = TEXT("floating window with no content");
			return false;
		}
		if (!ValidateNode(Floating[i].Root, nullptr, Seen, OutError, 0))
		{
			OutError = FString::Printf(TEXT("floating window %d: %s"), i, *OutError);
			return false;
		}
	}
	for (const TPair<FName, FDockHint>& Pair : HiddenHints)
	{
		if (Seen.Contains(Pair.Key))
		{
			OutError = FString::Printf(TEXT("panel %s is both visible and hidden"), *Pair.Key.ToString());
			return false;
		}
	}
	return true;
}

void FDockLayout::Clear()
{
	for (FArea& A : Areas)
	{
		A.Root.Reset();
		A.Extent = 0.f;
	}
	Floating.Reset();
	HiddenHints.Reset();
}

TSharedRef<FDockLayout> FDockLayout::Clone() const
{
	TSharedRef<FDockLayout> Copy = MakeShared<FDockLayout>();
	for (int32 s = 0; s < DockSideCount; ++s)
	{
		Copy->DefaultExtent[s] = DefaultExtent[s];
		Copy->Areas[s].Root = CloneNode(Areas[s].Root);
		Copy->Areas[s].Extent = Areas[s].Extent;
	}
	for (const FDockFloatingWindow& W : Floating)
	{
		FDockFloatingWindow C;
		C.Root = CloneNode(W.Root);
		C.Position = W.Position;
		C.Size = W.Size;
		Copy->Floating.Add(C);
	}
	Copy->HiddenHints = HiddenHints;
	Copy->NextNodeId = NextNodeId;
	return Copy;
}

bool FDockLayout::DockPanel(FName Panel, FName TargetPanel, EDockDropZone Zone)
{
	if (Panel.IsNone() || TargetPanel.IsNone() || Panel == TargetPanel || Zone == EDockDropZone::None)
	{
		return false;
	}

	FDockLocation TargetLocation;
	if (!FindPanel(TargetPanel, TargetLocation))
	{
		return false;
	}

	FDockLocation PanelLocation;
	const bool bPanelPlaced = FindPanel(Panel, PanelLocation);
	if (Zone == EDockDropZone::Center && bPanelPlaced && PanelLocation.Group == TargetLocation.Group)
	{
		return SetActiveTab(Panel);
	}

	if (bPanelPlaced)
	{
		RemovePanelTab(Panel);
		Normalize();
	}
	HiddenHints.Remove(Panel);

	// Nodes may have been collapsed by Normalize - look the target up again.
	FNodeRef Ref;
	if (!FindGroupSlot(TargetPanel, Ref))
	{
		return false;
	}

	if (Zone == EDockDropZone::Center)
	{
		FDockNode& Group = **Ref.Slot;
		Group.Tabs.Add(Panel);
		Group.ActiveTab = Group.Tabs.Num() - 1;
		Normalize();
		return true;
	}

	const EDockOrientation Orient = (Zone == EDockDropZone::Left || Zone == EDockDropZone::Right) ? EDockOrientation::Horizontal : EDockOrientation::Vertical;
	const bool bBefore = Zone == EDockDropZone::Left || Zone == EDockDropZone::Top;
	TSharedPtr<FDockNode> NewGroup = MakeGroup(Panel);

	FDockNode* Parent = Ref.Parent;
	if (Parent && Parent->IsSplit() && Parent->Orientation == Orient)
	{
		const int32 Index = Ref.IndexInParent;
		const float TargetRatio = Parent->Ratios[Index];
		Parent->Ratios[Index] = TargetRatio * 0.5f;
		const int32 InsertAt = bBefore ? Index : Index + 1;
		Parent->Children.Insert(NewGroup, InsertAt);
		Parent->Ratios.Insert(TargetRatio * 0.5f, InsertAt);
	}
	else
	{
		TSharedPtr<FDockNode> TargetGroup = *Ref.Slot;
		TSharedPtr<FDockNode> Split = MakeShared<FDockNode>();
		Split->Id = NextNodeId++;
		Split->Type = FDockNode::EType::Split;
		Split->Orientation = Orient;
		if (bBefore)
		{
			Split->Children = { NewGroup, TargetGroup };
		}
		else
		{
			Split->Children = { TargetGroup, NewGroup };
		}
		Split->Ratios = { 0.5f, 0.5f };
		*Ref.Slot = Split;
	}

	Normalize();
	return true;
}

void FDockLayout::PlaceInEdgeArea(FName Panel, EDockSide Side)
{
	FArea& Area = Areas[(int32)Side];
	TSharedPtr<FDockNode> NewGroup = MakeGroup(Panel);
	const float Add = DefaultExtent[(int32)Side];

	if (!Area.Root.IsValid())
	{
		Area.Root = NewGroup;
		Area.Extent = Add;
		return;
	}

	const EDockOrientation Orient = IsHorizontalStrip(Side) ? EDockOrientation::Horizontal : EDockOrientation::Vertical;
	const bool bBefore = Side == EDockSide::Left || Side == EDockSide::Top;
	const float OldExtent = FMath::Max(Area.Extent, 1.f);
	const float NewExtent = OldExtent + Add;
	const float OldShare = OldExtent / NewExtent;
	const float NewShare = Add / NewExtent;

	FDockNode* Root = Area.Root.Get();
	if (Root->IsSplit() && Root->Orientation == Orient)
	{
		for (float& R : Root->Ratios)
		{
			R *= OldShare;
		}
		if (bBefore)
		{
			Root->Children.Insert(NewGroup, 0);
			Root->Ratios.Insert(NewShare, 0);
		}
		else
		{
			Root->Children.Add(NewGroup);
			Root->Ratios.Add(NewShare);
		}
	}
	else
	{
		TSharedPtr<FDockNode> Split = MakeShared<FDockNode>();
		Split->Id = NextNodeId++;
		Split->Type = FDockNode::EType::Split;
		Split->Orientation = Orient;
		if (bBefore)
		{
			Split->Children = { NewGroup, Area.Root };
			Split->Ratios = { NewShare, OldShare };
		}
		else
		{
			Split->Children = { Area.Root, NewGroup };
			Split->Ratios = { OldShare, NewShare };
		}
		Area.Root = Split;
	}
	Area.Extent = NewExtent;
}

bool FDockLayout::DockPanelToEdge(FName Panel, EDockSide Side)
{
	if (Panel.IsNone())
	{
		return false;
	}

	FDockLocation Location;
	const bool bPlaced = FindPanel(Panel, Location);
	if (bPlaced && Location.Kind == FDockLocation::EKind::Area && Location.Side == Side)
	{
		const FArea& Area = Areas[(int32)Side];
		if (Area.Root.IsValid() && Area.Root->IsTabGroup() && Area.Root->Tabs.Num() == 1)
		{
			return false; // already the only thing on that edge
		}
	}

	if (bPlaced)
	{
		RemovePanelTab(Panel);
		Normalize();
	}
	HiddenHints.Remove(Panel);
	PlaceInEdgeArea(Panel, Side);
	Normalize();
	return true;
}

bool FDockLayout::FloatPanel(FName Panel, const FVector2D& Position, const FVector2D& Size)
{
	if (Panel.IsNone())
	{
		return false;
	}

	FDockLocation Location;
	if (FindPanel(Panel, Location))
	{
		if (Location.Kind == FDockLocation::EKind::Floating)
		{
			FDockFloatingWindow& W = Floating[Location.FloatingIndex];
			if (W.Root.Get() == Location.Group && Location.Group->Tabs.Num() == 1)
			{
				W.Position = Position;
				W.Size = Size;
				Normalize();
				return true;
			}
		}
		RemovePanelTab(Panel);
		Normalize();
	}
	HiddenHints.Remove(Panel);

	FDockFloatingWindow W;
	W.Root = MakeGroup(Panel);
	W.Position = Position;
	W.Size = Size;
	Floating.Add(W);
	Normalize();
	return true;
}

void FDockLayout::RecordHint(FName Panel)
{
	FDockLocation Location;
	FNodeRef Ref;
	if (!FindPanel(Panel, Location) || !FindGroupSlot(Panel, Ref))
	{
		return;
	}

	FDockHint Hint;
	if (Location.Kind == FDockLocation::EKind::Floating)
	{
		Hint.Kind = FDockHint::EKind::Floating;
		Hint.FloatPosition = Floating[Location.FloatingIndex].Position;
		Hint.FloatSize = Floating[Location.FloatingIndex].Size;
	}
	else
	{
		Hint.Kind = FDockHint::EKind::Docked;
		Hint.Side = Location.Side;
		Hint.Extent = Areas[(int32)Location.Side].Extent;
	}

	const FDockNode& Group = **Ref.Slot;
	if (Group.Tabs.Num() > 1)
	{
		const int32 Index = Location.TabIndex;
		Hint.NeighborPanel = (Index + 1 < Group.Tabs.Num()) ? Group.Tabs[Index + 1] : Group.Tabs[Index - 1];
		Hint.Zone = EDockDropZone::Center;
	}
	else if (Ref.Parent)
	{
		const FDockNode& Parent = *Ref.Parent;
		const bool bHorizontal = Parent.Orientation == EDockOrientation::Horizontal;
		const int32 Index = Ref.IndexInParent;
		if (Index + 1 < Parent.Children.Num())
		{
			Hint.NeighborPanel = FirstPanelIn(*Parent.Children[Index + 1]);
			Hint.Zone = bHorizontal ? EDockDropZone::Left : EDockDropZone::Top;
		}
		else if (Index > 0)
		{
			Hint.NeighborPanel = FirstPanelIn(*Parent.Children[Index - 1]);
			Hint.Zone = bHorizontal ? EDockDropZone::Right : EDockDropZone::Bottom;
		}
	}
	HiddenHints.Add(Panel, Hint);
}

bool FDockLayout::HidePanel(FName Panel)
{
	if (!ContainsPanel(Panel))
	{
		return false;
	}
	RecordHint(Panel);
	RemovePanelTab(Panel);
	Normalize();
	return true;
}

bool FDockLayout::ShowPanel(FName Panel, EDockSide FallbackSide)
{
	if (Panel.IsNone() || ContainsPanel(Panel))
	{
		return false;
	}

	const FDockHint* Found = HiddenHints.Find(Panel);
	if (!Found)
	{
		return DockPanelToEdge(Panel, FallbackSide);
	}

	const FDockHint Hint = *Found;
	if (!Hint.NeighborPanel.IsNone() && Hint.Zone != EDockDropZone::None && ContainsPanel(Hint.NeighborPanel))
	{
		return DockPanel(Panel, Hint.NeighborPanel, Hint.Zone);
	}
	if (Hint.Kind == FDockHint::EKind::Floating)
	{
		return FloatPanel(Panel, Hint.FloatPosition, Hint.FloatSize);
	}

	const bool bAreaWasEmpty = !Areas[(int32)Hint.Side].Root.IsValid();
	const bool bOk = DockPanelToEdge(Panel, Hint.Side);
	if (bOk && bAreaWasEmpty && Hint.Extent > 0.f)
	{
		Areas[(int32)Hint.Side].Extent = Hint.Extent;
	}
	return bOk;
}

bool FDockLayout::SetActiveTab(FName Panel)
{
	FNodeRef Ref;
	if (!FindGroupSlot(Panel, Ref))
	{
		return false;
	}
	FDockNode& Group = **Ref.Slot;
	Group.ActiveTab = Group.Tabs.IndexOfByKey(Panel);
	return true;
}

bool FDockLayout::MoveTab(FName Panel, int32 NewIndex)
{
	FNodeRef Ref;
	if (!FindGroupSlot(Panel, Ref))
	{
		return false;
	}
	FDockNode& Group = **Ref.Slot;
	const int32 Old = Group.Tabs.IndexOfByKey(Panel);
	const int32 Clamped = FMath::Clamp(NewIndex, 0, Group.Tabs.Num() - 1);
	if (Old == Clamped)
	{
		return false;
	}
	const FName ActiveName = Group.Tabs[Group.ActiveTab];
	Group.Tabs.RemoveAt(Old);
	Group.Tabs.Insert(Panel, Clamped);
	Group.ActiveTab = Group.Tabs.IndexOfByKey(ActiveName);
	return true;
}

bool FDockLayout::ResizeSplit(FDockNode* Split, int32 DividerIndex, float DeltaPx, float AvailablePx, float MinChildPx)
{
	if (!Split || !Split->IsSplit() || DividerIndex < 0 || DividerIndex + 1 >= Split->Children.Num()
		|| Split->Ratios.Num() != Split->Children.Num() || AvailablePx <= 0.f)
	{
		return false;
	}

	const float Left = Split->Ratios[DividerIndex] * AvailablePx;
	const float Right = Split->Ratios[DividerIndex + 1] * AvailablePx;
	const float Total = Left + Right;
	const float Min = FMath::Min(MinChildPx, Total * 0.5f);
	const float NewLeft = FMath::Clamp(Left + DeltaPx, Min, Total - Min);
	Split->Ratios[DividerIndex] = NewLeft / AvailablePx;
	Split->Ratios[DividerIndex + 1] = (Total - NewLeft) / AvailablePx;
	return true;
}

bool FDockLayout::SetAreaExtent(EDockSide Side, float ExtentPx)
{
	FArea& Area = Areas[(int32)Side];
	if (!Area.Root.IsValid() || !FMath::IsFinite(ExtentPx))
	{
		return false;
	}
	Area.Extent = FMath::Max(1.f, ExtentPx);
	return true;
}

bool FDockLayout::MoveFloating(int32 Index, const FVector2D& Position)
{
	if (!Floating.IsValidIndex(Index))
	{
		return false;
	}
	Floating[Index].Position = Position;
	return true;
}

bool FDockLayout::ResizeFloating(int32 Index, const FVector2D& Size)
{
	if (!Floating.IsValidIndex(Index))
	{
		return false;
	}
	Floating[Index].Size = FVector2D(FMath::Max(50.0, Size.X), FMath::Max(50.0, Size.Y));
	return true;
}

int32 FDockLayout::BringFloatingToFront(int32 Index)
{
	if (!Floating.IsValidIndex(Index))
	{
		return INDEX_NONE;
	}
	const FDockFloatingWindow Window = Floating[Index];
	Floating.RemoveAt(Index);
	return Floating.Add(Window);
}

float FDockLayout::ComputeTabWidth(float BarWidth, int32 TabCount, const FDockMetrics& M)
{
	if (TabCount <= 0)
	{
		return 0.f;
	}
	const float Share = BarWidth / (float)TabCount;
	return FMath::Min(FMath::Clamp(Share, M.TabMinWidth, M.TabMaxWidth), Share);
}

void FDockLayout::ComputeLayout(const FVector2D& Viewport, const FDockMetrics& M, FDockComputedLayout& Out) const
{
	Out = FDockComputedLayout();

	const float W = FMath::Max(1.f, (float)Viewport.X);
	const float H = FMath::Max(1.f, (float)Viewport.Y);
	const float UsableTop = FMath::Clamp(M.TopMargin, 0.f, H);
	const float UsableBottom = FMath::Clamp(H - M.BottomMargin, UsableTop, H);
	const float UsableHeight = UsableBottom - UsableTop;

	float Extent[DockSideCount] = { 0.f, 0.f, 0.f, 0.f };
	for (int32 s = 0; s < DockSideCount; ++s)
	{
		if (!Areas[s].Root.IsValid())
		{
			continue;
		}
		const float Limit = (IsHorizontalStrip((EDockSide)s) ? W : UsableHeight) * M.MaxEdgeFraction;
		Extent[s] = FMath::Clamp(Areas[s].Extent, FMath::Min(M.MinEdgeExtent, Limit), Limit);
	}
	auto ShrinkPair = [](float& A, float& B, float Total)
	{
		const float Sum = A + B;
		const float Max = Total * 0.9f;
		if (Sum > Max && Sum > 0.f)
		{
			const float K = Max / Sum;
			A *= K;
			B *= K;
		}
	};
	ShrinkPair(Extent[(int32)EDockSide::Left], Extent[(int32)EDockSide::Right], W);
	ShrinkPair(Extent[(int32)EDockSide::Top], Extent[(int32)EDockSide::Bottom], UsableHeight);

	const float LeftE = Extent[(int32)EDockSide::Left];
	const float RightE = Extent[(int32)EDockSide::Right];
	const float TopE = Extent[(int32)EDockSide::Top];
	const float BottomE = Extent[(int32)EDockSide::Bottom];
	const float MidTop = UsableTop + TopE;
	const float MidBottom = UsableBottom - BottomE;

	Out.AreaRects[(int32)EDockSide::Top] = MakeBox(0.f, UsableTop, W, UsableTop + TopE);
	Out.AreaRects[(int32)EDockSide::Bottom] = MakeBox(0.f, UsableBottom - BottomE, W, UsableBottom);
	Out.AreaRects[(int32)EDockSide::Left] = MakeBox(0.f, MidTop, LeftE, MidBottom);
	Out.AreaRects[(int32)EDockSide::Right] = MakeBox(W - RightE, MidTop, W, MidBottom);

	for (int32 s = 0; s < DockSideCount; ++s)
	{
		Out.bAreaVisible[s] = Areas[s].Root.IsValid() && Extent[s] > 0.f;
		if (Out.bAreaVisible[s])
		{
			LayoutNode(*Areas[s].Root, Out.AreaRects[s], M, (EDockSide)s, INDEX_NONE, Out);
		}
	}

	for (int32 i = 0; i < Floating.Num(); ++i)
	{
		const FDockFloatingWindow& Window = Floating[i];
		const float SizeX = FMath::Clamp((float)Window.Size.X, 50.f, W);
		const float SizeY = FMath::Clamp((float)Window.Size.Y, 50.f, FMath::Max(50.f, UsableHeight));
		const float X = FMath::Clamp((float)Window.Position.X, M.FloatingMinVisible - SizeX, W - M.FloatingMinVisible);
		const float Y = FMath::Clamp((float)Window.Position.Y, UsableTop, FMath::Max(UsableTop, UsableBottom - M.FloatingMinVisible));
		const FBox2D Rect = MakeBox(X, Y, X + SizeX, Y + SizeY);
		Out.FloatingRects.Add(Rect);
		const float Grip = FMath::Min(M.FloatingGripHeight, SizeY);
		Out.FloatingGripRects.Add(MakeBox(X, Y, X + SizeX, Y + Grip));
		if (Window.Root.IsValid())
		{
			LayoutNode(*Window.Root, MakeBox(X, Y + Grip, X + SizeX, Y + SizeY), M, EDockSide::Left, i, Out);
		}
	}
}

FDockDropTarget FDockLayout::HitTestDropTarget(const FVector2D& Point, FName DraggedPanel, const FVector2D& Viewport, const FDockMetrics& M, const FDockComputedLayout& C, const FVector2D& FloatSize) const
{
	FDockDropTarget Result;

	auto EvaluateGroup = [&](const FDockComputedGroup& G, bool bForceCenter = false) -> FDockDropTarget
	{
		FDockDropTarget T;
		const FDockNode& Node = *G.Node;
		if (Node.Tabs.Num() == 1 && Node.Tabs[0] == DraggedPanel)
		{
			return T; // dropping a lone panel onto itself does nothing
		}

		EDockDropZone Zone = EDockDropZone::Center;
		if (!bForceCenter && !BoxContains(G.TabBarRect, Point))
		{
			const FVector2D Size = G.Rect.GetSize();
			if (Size.X > 0.0 && Size.Y > 0.0)
			{
				const float U = (float)((Point.X - G.Rect.Min.X) / Size.X);
				const float V = (float)((Point.Y - G.Rect.Min.Y) / Size.Y);
				const float Distances[4] = { U, 1.f - U, V, 1.f - V };
				const EDockDropZone Zones[4] = { EDockDropZone::Left, EDockDropZone::Right, EDockDropZone::Top, EDockDropZone::Bottom };
				int32 Best = 0;
				for (int32 i = 1; i < 4; ++i)
				{
					if (Distances[i] < Distances[Best])
					{
						Best = i;
					}
				}
				if (Distances[Best] < 0.25f)
				{
					Zone = Zones[Best];
				}
			}
		}

		// Needs a panel in this group to dock relative to - never the dragged one itself.
		FName Target = NAME_None;
		if (Node.Tabs.IsValidIndex(Node.ActiveTab) && Node.Tabs[Node.ActiveTab] != DraggedPanel)
		{
			Target = Node.Tabs[Node.ActiveTab];
		}
		else
		{
			for (const FName Tab : Node.Tabs)
			{
				if (Tab != DraggedPanel)
				{
					Target = Tab;
					break;
				}
			}
		}
		if (Target.IsNone())
		{
			return T;
		}
		if (Zone == EDockDropZone::Center && Node.Tabs.Contains(DraggedPanel))
		{
			return T; // already a tab of this group
		}

		const float MinX = (float)G.Rect.Min.X, MinY = (float)G.Rect.Min.Y, MaxX = (float)G.Rect.Max.X, MaxY = (float)G.Rect.Max.Y;
		const float MidX = (MinX + MaxX) * 0.5f, MidY = (MinY + MaxY) * 0.5f;
		switch (Zone)
		{
		case EDockDropZone::Left: T.PreviewRect = MakeBox(MinX, MinY, MidX, MaxY); break;
		case EDockDropZone::Right: T.PreviewRect = MakeBox(MidX, MinY, MaxX, MaxY); break;
		case EDockDropZone::Top: T.PreviewRect = MakeBox(MinX, MinY, MaxX, MidY); break;
		case EDockDropZone::Bottom: T.PreviewRect = MakeBox(MinX, MidY, MaxX, MaxY); break;
		default: T.PreviewRect = G.Rect; break;
		}
		T.Kind = FDockDropTarget::EKind::Group;
		T.Group = G.Node;
		T.TargetPanel = Target;
		T.Zone = Zone;
		return T;
	};

	// Floating windows first, topmost (last) first. A window swallows the hit even where it has no group
	// under the point (e.g. a splitter), so nothing underneath is targeted through it.
	for (int32 i = C.FloatingRects.Num() - 1; i >= 0; --i)
	{
		if (!BoxContains(C.FloatingRects[i], Point))
		{
			continue;
		}
		// The dragged panel's own window vanishes when it is dropped (the panel is moving out of it), so it
		// must not hide whatever lies beneath it.
		if (Floating.IsValidIndex(i) && Floating[i].Root.IsValid() && Floating[i].Root->IsTabGroup()
			&& Floating[i].Root->Tabs.Num() == 1 && Floating[i].Root->Tabs[0] == DraggedPanel)
		{
			continue;
		}
		const bool bOnGrip = C.FloatingGripRects.IsValidIndex(i) && BoxContains(C.FloatingGripRects[i], Point);
		const FDockComputedGroup* First = nullptr;
		for (const FDockComputedGroup& G : C.Groups)
		{
			if (G.FloatingIndex != i)
			{
				continue;
			}
			if (BoxContains(G.Rect, Point))
			{
				return EvaluateGroup(G);
			}
			if (!First)
			{
				First = &G;
			}
		}
		// The drag strip acts like the window's tab bar: dropping on it adds a tab to the window's first group.
		if (bOnGrip && First)
		{
			return EvaluateGroup(*First, true);
		}
		return Result;
	}

	for (const FDockComputedGroup& G : C.Groups)
	{
		if (G.FloatingIndex == INDEX_NONE && BoxContains(G.Rect, Point))
		{
			return EvaluateGroup(G);
		}
	}

	// Outer screen edges.
	const float W = FMath::Max(1.f, (float)Viewport.X);
	const float H = FMath::Max(1.f, (float)Viewport.Y);
	const float UsableTop = FMath::Clamp(M.TopMargin, 0.f, H);
	const float UsableBottom = FMath::Clamp(H - M.BottomMargin, UsableTop, H);
	if (Point.X < 0.0 || Point.X > W || Point.Y < UsableTop || Point.Y > UsableBottom)
	{
		return Result;
	}

	const float Distances[DockSideCount] = {
		(float)Point.X,                // Left
		W - (float)Point.X,            // Right
		(float)Point.Y - UsableTop,    // Top
		UsableBottom - (float)Point.Y  // Bottom
	};
	int32 BestSide = INDEX_NONE;
	for (int32 s = 0; s < DockSideCount; ++s)
	{
		if (Distances[s] <= M.EdgeGuideSize && (BestSide == INDEX_NONE || Distances[s] < Distances[BestSide]))
		{
			BestSide = s;
		}
	}
	if (BestSide == INDEX_NONE)
	{
		// Not over any docked chrome (floating windows and area groups were handled above, and an area's
		// own rect - even over a splitter - is not canvas): this is empty canvas, so the drop floats.
		for (int32 s = 0; s < DockSideCount; ++s)
		{
			if (C.bAreaVisible[s] && BoxContains(C.AreaRects[s], Point))
			{
				return Result;
			}
		}
		const float SizeX = FMath::Clamp((float)FloatSize.X, (float)M.FloatingMinSize.X, W);
		const float SizeY = FMath::Clamp((float)FloatSize.Y, (float)M.FloatingMinSize.Y, FMath::Max((float)M.FloatingMinSize.Y, UsableBottom - UsableTop));
		const float X = FMath::Clamp((float)Point.X - SizeX * 0.5f, 0.f, FMath::Max(0.f, W - SizeX));
		const float Y = FMath::Clamp((float)Point.Y - M.FloatingGripHeight * 0.5f, UsableTop, FMath::Max(UsableTop, UsableBottom - SizeY));
		Result.Kind = FDockDropTarget::EKind::Float;
		Result.FloatPosition = FVector2D(X, Y);
		Result.FloatSize = FVector2D(SizeX, SizeY);
		Result.PreviewRect = MakeBox(X, Y, X + SizeX, Y + SizeY);
		return Result;
	}

	const FArea& Area = Areas[BestSide];
	if (Area.Root.IsValid() && Area.Root->IsTabGroup() && Area.Root->Tabs.Num() == 1 && Area.Root->Tabs[0] == DraggedPanel)
	{
		return Result;
	}

	const float Limit = (IsHorizontalStrip((EDockSide)BestSide) ? W : (UsableBottom - UsableTop)) * M.MaxEdgeFraction;
	const float Ext = FMath::Min(DefaultExtent[BestSide], Limit);
	switch ((EDockSide)BestSide)
	{
	case EDockSide::Left: Result.PreviewRect = MakeBox(0.f, UsableTop, Ext, UsableBottom); break;
	case EDockSide::Right: Result.PreviewRect = MakeBox(W - Ext, UsableTop, W, UsableBottom); break;
	case EDockSide::Top: Result.PreviewRect = MakeBox(0.f, UsableTop, W, UsableTop + Ext); break;
	default: Result.PreviewRect = MakeBox(0.f, UsableBottom - Ext, W, UsableBottom); break;
	}
	Result.Kind = FDockDropTarget::EKind::Edge;
	Result.Side = (EDockSide)BestSide;
	return Result;
}

TSharedPtr<FJsonObject> FDockLayout::ToJson(const FVector2D& Viewport) const
{
	const float W = FMath::Max(1.f, (float)Viewport.X);
	const float H = FMath::Max(1.f, (float)Viewport.Y);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("layoutVersion"), JsonLayoutVersion);

	TSharedPtr<FJsonObject> AreasObject = MakeShared<FJsonObject>();
	for (int32 s = 0; s < DockSideCount; ++s)
	{
		const FArea& Area = Areas[s];
		if (!Area.Root.IsValid())
		{
			continue;
		}
		TSharedPtr<FJsonObject> AreaObject = MakeShared<FJsonObject>();
		AreaObject->SetNumberField(TEXT("extent"), Area.Extent / (IsHorizontalStrip((EDockSide)s) ? W : H));
		AreaObject->SetObjectField(TEXT("root"), NodeToJson(*Area.Root));
		AreasObject->SetObjectField(SideToString((EDockSide)s), AreaObject);
	}
	Root->SetObjectField(TEXT("areas"), AreasObject);

	TArray<TSharedPtr<FJsonValue>> FloatingArray;
	for (const FDockFloatingWindow& Window : Floating)
	{
		if (!Window.Root.IsValid())
		{
			continue;
		}
		TSharedPtr<FJsonObject> WindowObject = MakeShared<FJsonObject>();
		WindowObject->SetNumberField(TEXT("x"), Window.Position.X / W);
		WindowObject->SetNumberField(TEXT("y"), Window.Position.Y / H);
		WindowObject->SetNumberField(TEXT("w"), Window.Size.X / W);
		WindowObject->SetNumberField(TEXT("h"), Window.Size.Y / H);
		WindowObject->SetObjectField(TEXT("root"), NodeToJson(*Window.Root));
		FloatingArray.Add(MakeShared<FJsonValueObject>(WindowObject));
	}
	Root->SetArrayField(TEXT("floating"), FloatingArray);

	TSharedPtr<FJsonObject> HiddenObject = MakeShared<FJsonObject>();
	for (const TPair<FName, FDockHint>& Pair : HiddenHints)
	{
		const FDockHint& Hint = Pair.Value;
		TSharedPtr<FJsonObject> HintObject = MakeShared<FJsonObject>();
		HintObject->SetStringField(TEXT("kind"), Hint.Kind == FDockHint::EKind::Floating ? TEXT("floating") : TEXT("docked"));
		HintObject->SetStringField(TEXT("side"), SideToString(Hint.Side));
		HintObject->SetNumberField(TEXT("extent"), Hint.Extent / (IsHorizontalStrip(Hint.Side) ? W : H));
		HintObject->SetStringField(TEXT("neighbor"), Hint.NeighborPanel.IsNone() ? FString() : Hint.NeighborPanel.ToString());
		HintObject->SetStringField(TEXT("zone"), ZoneToString(Hint.Zone));
		HintObject->SetNumberField(TEXT("x"), Hint.FloatPosition.X / W);
		HintObject->SetNumberField(TEXT("y"), Hint.FloatPosition.Y / H);
		HintObject->SetNumberField(TEXT("w"), Hint.FloatSize.X / W);
		HintObject->SetNumberField(TEXT("h"), Hint.FloatSize.Y / H);
		HiddenObject->SetObjectField(Pair.Key.ToString(), HintObject);
	}
	Root->SetObjectField(TEXT("hidden"), HiddenObject);
	return Root;
}

bool FDockLayout::FromJson(const TSharedPtr<FJsonObject>& Root, const FVector2D& Viewport, const TSet<FName>& KnownPanels, TArray<FName>& OutMissingPanels)
{
	OutMissingPanels.Reset();
	if (!Root.IsValid())
	{
		return false;
	}
	double Version = 0.0;
	if (!Root->TryGetNumberField(TEXT("layoutVersion"), Version) || (int32)Version != JsonLayoutVersion)
	{
		return false;
	}

	const float W = FMath::Max(1.f, (float)Viewport.X);
	const float H = FMath::Max(1.f, (float)Viewport.Y);

	TSharedRef<FDockLayout> Temp = MakeShared<FDockLayout>();
	Temp->NextNodeId = NextNodeId;
	for (int32 s = 0; s < DockSideCount; ++s)
	{
		Temp->DefaultExtent[s] = DefaultExtent[s];
	}

	bool bOk = true;
	int32 IdCounter = Temp->NextNodeId;

	const TSharedPtr<FJsonObject>* AreasObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("areas"), AreasObject))
	{
		return false;
	}
	for (int32 s = 0; s < DockSideCount; ++s)
	{
		const TSharedPtr<FJsonObject>* AreaObject = nullptr;
		if (!(*AreasObject)->TryGetObjectField(SideToString((EDockSide)s), AreaObject))
		{
			continue;
		}
		const TSharedPtr<FJsonObject>* RootObject = nullptr;
		if (!(*AreaObject)->TryGetObjectField(TEXT("root"), RootObject))
		{
			return false;
		}
		TSharedPtr<FDockNode> Node = NodeFromJson(*RootObject, KnownPanels, 0, IdCounter, bOk);
		if (!bOk)
		{
			return false;
		}
		if (!Node.IsValid())
		{
			continue;
		}
		double Fraction = 0.0;
		(*AreaObject)->TryGetNumberField(TEXT("extent"), Fraction);
		Temp->Areas[s].Root = Node;
		Temp->Areas[s].Extent = (float)Fraction * (IsHorizontalStrip((EDockSide)s) ? W : H);
	}

	const TArray<TSharedPtr<FJsonValue>>* FloatingArray = nullptr;
	if (Root->TryGetArrayField(TEXT("floating"), FloatingArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *FloatingArray)
		{
			const TSharedPtr<FJsonObject>* WindowObject = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(WindowObject))
			{
				return false;
			}
			const TSharedPtr<FJsonObject>* RootObject = nullptr;
			if (!(*WindowObject)->TryGetObjectField(TEXT("root"), RootObject))
			{
				return false;
			}
			TSharedPtr<FDockNode> Node = NodeFromJson(*RootObject, KnownPanels, 0, IdCounter, bOk);
			if (!bOk)
			{
				return false;
			}
			if (!Node.IsValid())
			{
				continue;
			}
			double X = 0.0, Y = 0.0, Wf = 0.3, Hf = 0.4;
			(*WindowObject)->TryGetNumberField(TEXT("x"), X);
			(*WindowObject)->TryGetNumberField(TEXT("y"), Y);
			(*WindowObject)->TryGetNumberField(TEXT("w"), Wf);
			(*WindowObject)->TryGetNumberField(TEXT("h"), Hf);
			FDockFloatingWindow Window;
			Window.Root = Node;
			Window.Position = FVector2D(X * W, Y * H);
			Window.Size = FVector2D(Wf * W, Hf * H);
			Temp->Floating.Add(Window);
		}
	}

	const TSharedPtr<FJsonObject>* HiddenObject = nullptr;
	if (Root->TryGetObjectField(TEXT("hidden"), HiddenObject))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*HiddenObject)->Values)
		{
			const FName Panel(*Pair.Key);
			const TSharedPtr<FJsonObject>* HintObject = nullptr;
			if (!KnownPanels.Contains(Panel) || !Pair.Value.IsValid() || !Pair.Value->TryGetObject(HintObject))
			{
				continue;
			}
			FDockHint Hint;
			FString Text;
			if ((*HintObject)->TryGetStringField(TEXT("kind"), Text))
			{
				Hint.Kind = Text == TEXT("floating") ? FDockHint::EKind::Floating : FDockHint::EKind::Docked;
			}
			if ((*HintObject)->TryGetStringField(TEXT("side"), Text))
			{
				SideFromString(Text, Hint.Side);
			}
			double Number = 0.0;
			if ((*HintObject)->TryGetNumberField(TEXT("extent"), Number))
			{
				Hint.Extent = (float)Number * (IsHorizontalStrip(Hint.Side) ? W : H);
			}
			if ((*HintObject)->TryGetStringField(TEXT("neighbor"), Text) && !Text.IsEmpty())
			{
				Hint.NeighborPanel = FName(*Text);
			}
			if ((*HintObject)->TryGetStringField(TEXT("zone"), Text))
			{
				Hint.Zone = ZoneFromString(Text);
			}
			double X = 0.0, Y = 0.0, Wf = 0.3, Hf = 0.4;
			(*HintObject)->TryGetNumberField(TEXT("x"), X);
			(*HintObject)->TryGetNumberField(TEXT("y"), Y);
			(*HintObject)->TryGetNumberField(TEXT("w"), Wf);
			(*HintObject)->TryGetNumberField(TEXT("h"), Hf);
			Hint.FloatPosition = FVector2D(X * W, Y * H);
			Hint.FloatSize = FVector2D(Wf * W, Hf * H);
			Temp->HiddenHints.Add(Panel, Hint);
		}
	}

	Temp->NextNodeId = IdCounter;
	Temp->Normalize();

	TSet<FName> Present;
	for (const FName Panel : Temp->GetAllPanels())
	{
		Present.Add(Panel);
	}
	for (const FName Panel : KnownPanels)
	{
		if (!Present.Contains(Panel) && !Temp->HiddenHints.Contains(Panel))
		{
			OutMissingPanels.Add(Panel);
		}
	}
	OutMissingPanels.Sort(FNameLexicalLess());

	for (int32 s = 0; s < DockSideCount; ++s)
	{
		Areas[s] = Temp->Areas[s];
	}
	Floating = Temp->Floating;
	HiddenHints = Temp->HiddenHints;
	NextNodeId = Temp->NextNodeId;
	return true;
}
