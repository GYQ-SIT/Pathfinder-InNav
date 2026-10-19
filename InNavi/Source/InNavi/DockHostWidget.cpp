#include "DockHostWidget.h"

#include "Brushes/SlateColorBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FSlateColorBrush WhiteBrush(FLinearColor::White);

	const FLinearColor FrameColor(0.04f, 0.04f, 0.05f, 0.97f);
	const FLinearColor ContentColor(0.11f, 0.11f, 0.12f, 0.97f);
	const FLinearColor TabBarColor(0.07f, 0.07f, 0.08f, 1.f);
	const FLinearColor TabInactiveColor(0.12f, 0.12f, 0.13f, 1.f);
	const FLinearColor TabHoverColor(0.18f, 0.18f, 0.20f, 1.f);
	const FLinearColor TabActiveColor(0.21f, 0.21f, 0.24f, 1.f);
	const FLinearColor AccentColor(0.25f, 0.55f, 1.f, 1.f);
	const FLinearColor TextColor(0.88f, 0.88f, 0.9f, 1.f);
	const FLinearColor CloseIdleColor(0.55f, 0.55f, 0.58f, 1.f);
	const FLinearColor SplitterIdleColor(0.03f, 0.03f, 0.035f, 0.9f);
	const FLinearColor SplitterHoverColor(0.25f, 0.55f, 1.f, 0.7f);

	constexpr float CloseButtonWidth = 18.f;

	void GatherNode(const FDockNode* Node, TArray<const FDockNode*>& Groups, TArray<TPair<const FDockNode*, int32>>& Dividers)
	{
		if (!Node)
		{
			return;
		}
		if (Node->IsTabGroup())
		{
			Groups.Add(Node);
			return;
		}
		for (int32 i = 0; i < Node->Children.Num(); ++i)
		{
			GatherNode(Node->Children[i].Get(), Groups, Dividers);
			if (i + 1 < Node->Children.Num())
			{
				Dividers.Add(TPair<const FDockNode*, int32>(Node, i));
			}
		}
	}
}

struct FDockTabInfo
{
	FName Id;
	FText Title;
};

// ---------------------------------------------------------------------------------------------
// Tab strip of one group.
// ---------------------------------------------------------------------------------------------
class SDockTabBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDockTabBar) {}
		SLATE_ARGUMENT(TWeakPtr<SDockHost>, Host)
		SLATE_ARGUMENT(FDockMetrics, Metrics)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Host = InArgs._Host;
		Metrics = InArgs._Metrics;
	}

	void SetTabs(const TArray<FDockTabInfo>& InTabs, int32 InActiveIndex)
	{
		Tabs = InTabs;
		ActiveIndex = InActiveIndex;
		HoveredTab = INDEX_NONE;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(100.0, Metrics.TabBarHeight);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const FVector2f Size = AllottedGeometry.GetLocalSize();
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &WhiteBrush, ESlateDrawEffect::None, TabBarColor);

		const int32 N = Tabs.Num();
		const float TabWidth = FDockLayout::ComputeTabWidth(Size.X, N, Metrics);
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

		for (int32 i = 0; i < N; ++i)
		{
			const float X = TabWidth * (float)i;
			const bool bActive = i == ActiveIndex;
			const bool bHovered = i == HoveredTab;
			const FLinearColor Fill = bActive ? TabActiveColor : (bHovered ? TabHoverColor : TabInactiveColor);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2f(FMath::Max(0.f, TabWidth - 1.f), Size.Y), FSlateLayoutTransform(FVector2f(X, 0.f))),
				&WhiteBrush, ESlateDrawEffect::None, Fill);
			if (bActive)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
					AllottedGeometry.ToPaintGeometry(FVector2f(FMath::Max(0.f, TabWidth - 1.f), 2.f), FSlateLayoutTransform(FVector2f(X, 0.f))),
					&WhiteBrush, ESlateDrawEffect::None, AccentColor);
			}

			const FVector2f TextSize = FVector2f(FontMeasure->Measure(Tabs[i].Title, Font));
			const float ClipWidth = FMath::Max(0.f, TabWidth - 8.f - CloseButtonWidth);
			OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry.ToPaintGeometry(FVector2f(ClipWidth, Size.Y), FSlateLayoutTransform(FVector2f(X + 8.f, 0.f)))));
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 3,
				AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(FVector2f(X + 8.f, (Size.Y - TextSize.Y) * 0.5f))),
				Tabs[i].Title, Font, ESlateDrawEffect::None, TextColor);
			OutDrawElements.PopClip();

			const FVector2f Center(X + TabWidth - CloseButtonWidth * 0.5f - 1.f, Size.Y * 0.5f);
			const float R = 3.5f;
			const FLinearColor CloseColor = (bHovered && bHoverClose) ? FLinearColor::White : CloseIdleColor;
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(),
				TArray<FVector2f>{ FVector2f(Center.X - R, Center.Y - R), FVector2f(Center.X + R, Center.Y + R) }, ESlateDrawEffect::None, CloseColor, true, 1.5f);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(),
				TArray<FVector2f>{ FVector2f(Center.X - R, Center.Y + R), FVector2f(Center.X + R, Center.Y - R) }, ESlateDrawEffect::None, CloseColor, true, 1.5f);
		}
		return LayerId + 4;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}
		const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		bool bOnClose = false;
		const int32 Index = TabIndexAt(MyGeometry, Local, bOnClose);
		if (Index == INDEX_NONE)
		{
			return FReply::Unhandled();
		}
		if (const TSharedPtr<SDockHost> Pinned = Host.Pin())
		{
			if (bOnClose)
			{
				Pinned->HandleTabClosed(Tabs[Index].Id);
			}
			else
			{
				Pinned->HandleTabActivated(Tabs[Index].Id);
			}
		}
		return FReply::Handled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		bool bOnClose = false;
		const int32 Index = TabIndexAt(MyGeometry, Local, bOnClose);
		if (Index != HoveredTab || bOnClose != bHoverClose)
		{
			HoveredTab = Index;
			bHoverClose = bOnClose;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		return FReply::Unhandled();
	}

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		if (HoveredTab != INDEX_NONE)
		{
			HoveredTab = INDEX_NONE;
			bHoverClose = false;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}

private:
	int32 TabIndexAt(const FGeometry& Geometry, const FVector2f& Local, bool& bOutOnClose) const
	{
		bOutOnClose = false;
		const FVector2f Size = Geometry.GetLocalSize();
		const float TabWidth = FDockLayout::ComputeTabWidth(Size.X, Tabs.Num(), Metrics);
		if (TabWidth <= 0.f || Local.X < 0.f || Local.Y < 0.f || Local.Y > Size.Y || Local.X >= TabWidth * (float)Tabs.Num())
		{
			return INDEX_NONE;
		}
		const int32 Index = FMath::Clamp((int32)(Local.X / TabWidth), 0, Tabs.Num() - 1);
		bOutOnClose = Local.X > TabWidth * (float)(Index + 1) - CloseButtonWidth;
		return Index;
	}

	TWeakPtr<SDockHost> Host;
	FDockMetrics Metrics;
	TArray<FDockTabInfo> Tabs;
	int32 ActiveIndex = 0;
	int32 HoveredTab = INDEX_NONE;
	bool bHoverClose = false;
};

// ---------------------------------------------------------------------------------------------
// One tab group: tab strip on top, the active panel's content below.
// ---------------------------------------------------------------------------------------------
class SDockGroupFrame : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDockGroupFrame) {}
		SLATE_ARGUMENT(TWeakPtr<SDockHost>, Host)
		SLATE_ARGUMENT(FDockMetrics, Metrics)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(&WhiteBrush)
			.BorderBackgroundColor(FrameColor)
			.Padding(1.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(TabBar, SDockTabBar)
					.Host(InArgs._Host)
					.Metrics(InArgs._Metrics)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SAssignNew(ContentHolder, SBorder)
					.BorderImage(&WhiteBrush)
					.BorderBackgroundColor(ContentColor)
					.Padding(FMargin(4.f))
					.Clipping(EWidgetClipping::ClipToBounds)
				]
			]
		];
	}

	void Update(const TArray<FDockTabInfo>& Tabs, int32 ActiveIndex, const TSharedRef<SWidget>& Content)
	{
		TabBar->SetTabs(Tabs, ActiveIndex);
		ContentHolder->SetContent(Content);
	}

	void ClearContent()
	{
		ContentHolder->SetContent(SNullWidget::NullWidget);
	}

private:
	TSharedPtr<SDockTabBar> TabBar;
	TSharedPtr<SBorder> ContentHolder;
};

// ---------------------------------------------------------------------------------------------
// Draggable bar between two siblings of a split, or on the inner edge of an edge area.
// ---------------------------------------------------------------------------------------------
class SDockSplitterBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDockSplitterBar)
		: _SplitNode(nullptr), _DividerIndex(0), _Side(EDockSide::Left), _bAreaEdge(false), _bVerticalBar(true)
	{}
		SLATE_ARGUMENT(TWeakPtr<SDockHost>, Host)
		SLATE_ARGUMENT(const FDockNode*, SplitNode)
		SLATE_ARGUMENT(int32, DividerIndex)
		SLATE_ARGUMENT(EDockSide, Side)
		SLATE_ARGUMENT(bool, bAreaEdge)
		SLATE_ARGUMENT(bool, bVerticalBar)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Host = InArgs._Host;
		SplitNode = InArgs._SplitNode;
		DividerIndex = InArgs._DividerIndex;
		Side = InArgs._Side;
		bAreaEdge = InArgs._bAreaEdge;
		bVerticalBar = InArgs._bVerticalBar;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(6.0, 6.0); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const FLinearColor Color = (bHovered || bDragging) ? SplitterHoverColor : (bAreaEdge ? FLinearColor::Transparent : SplitterIdleColor);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &WhiteBrush, ESlateDrawEffect::None, Color);
		return LayerId + 1;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}
		const TSharedPtr<SDockHost> Pinned = Host.Pin();
		if (!Pinned.IsValid())
		{
			return FReply::Unhandled();
		}
		bDragging = true;
		Invalidate(EInvalidateWidgetReason::Paint);
		return bAreaEdge ? Pinned->BeginAreaEdgeDrag(Side, MouseEvent) : Pinned->BeginSplitterDrag(SplitNode, DividerIndex, MouseEvent);
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		bDragging = false;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Unhandled(); // the host owns the capture and finishes the drag
	}

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		bHovered = true;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		bHovered = false;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
	{
		return FCursorReply::Cursor(bVerticalBar ? EMouseCursor::ResizeLeftRight : EMouseCursor::ResizeUpDown);
	}

private:
	TWeakPtr<SDockHost> Host;
	const FDockNode* SplitNode = nullptr;
	int32 DividerIndex = 0;
	EDockSide Side = EDockSide::Left;
	bool bAreaEdge = false;
	bool bVerticalBar = true;
	bool bHovered = false;
	bool bDragging = false;
};

// ---------------------------------------------------------------------------------------------
// SDockHost
// ---------------------------------------------------------------------------------------------
SDockHost::SDockHost()
	: Children(this)
{
}

void SDockHost::Construct(const FArguments& InArgs)
{
	Layout = InArgs._Layout.IsValid() ? InArgs._Layout : MakeShared<FDockLayout>();
	Metrics = InArgs._Metrics;
	OnPanelClosed = InArgs._OnPanelClosed;
	OnLayoutChanged = InArgs._OnLayoutChanged;

	SetCanTick(false);
	SetVisibility(EVisibility::SelfHitTestInvisible);
	RebuildChildren();
}

void SDockHost::RegisterPanel(FName Id, const FText& Title, const TSharedRef<SWidget>& Content, bool bScrollable)
{
	FPanelEntry Entry;
	Entry.Title = Title;
	if (bScrollable)
	{
		Entry.Root = SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				Content
			];
	}
	else
	{
		Entry.Root = Content;
	}
	Panels.Add(Id, Entry);
	NotifyLayoutChanged();
}

void SDockHost::UnregisterPanel(FName Id)
{
	if (Panels.Remove(Id) > 0)
	{
		NotifyLayoutChanged();
	}
}

void SDockHost::SetMetrics(const FDockMetrics& InMetrics)
{
	Metrics = InMetrics;
	NotifyLayoutChanged();
}

void SDockHost::NotifyLayoutChanged()
{
	RebuildChildren();
	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::ChildOrder | EInvalidateWidgetReason::Paint);
}

void SDockHost::RebuildChildren()
{
	struct FContainer
	{
		TArray<const FDockNode*> Groups;
		TArray<TPair<const FDockNode*, int32>> Dividers;
	};

	TArray<FContainer> Containers;
	Containers.SetNum(DockSideCount + Layout->GetFloating().Num());
	for (int32 s = 0; s < DockSideCount; ++s)
	{
		GatherNode(Layout->GetAreaRoot((EDockSide)s).Get(), Containers[s].Groups, Containers[s].Dividers);
	}
	for (int32 i = 0; i < Layout->GetFloating().Num(); ++i)
	{
		GatherNode(Layout->GetFloating()[i].Root.Get(), Containers[DockSideCount + i].Groups, Containers[DockSideCount + i].Dividers);
	}

	// A panel's content widget can only have one parent, and a panel may move between frames - so release
	// every frame's content first, then hand it out again in RefreshFrames.
	for (const TPair<const FDockNode*, TSharedPtr<SDockGroupFrame>>& Pair : GroupFrames)
	{
		Pair.Value->ClearContent();
	}

	TMap<const FDockNode*, TSharedPtr<SDockGroupFrame>> NewFrames;
	TMap<TPair<const FDockNode*, int32>, TSharedPtr<SDockSplitterBar>> NewBars;
	Children.Reset();
	Entries.Reset();

	const TWeakPtr<SDockHost> WeakSelf = SharedThis(this);

	auto AddContainer = [&](const FContainer& Container)
	{
		for (const FDockNode* Node : Container.Groups)
		{
			TSharedPtr<SDockGroupFrame> Frame;
			if (const TSharedPtr<SDockGroupFrame>* Found = GroupFrames.Find(Node))
			{
				Frame = *Found;
			}
			else
			{
				Frame = SNew(SDockGroupFrame).Host(WeakSelf).Metrics(Metrics);
			}
			NewFrames.Add(Node, Frame);
			Children.Add(Frame.ToSharedRef());
			FChildEntry Entry;
			Entry.Kind = FChildEntry::EKind::Group;
			Entry.Widget = Frame;
			Entry.Node = Node;
			Entries.Add(Entry);
		}
		for (const TPair<const FDockNode*, int32>& Divider : Container.Dividers)
		{
			TSharedPtr<SDockSplitterBar> Bar;
			if (const TSharedPtr<SDockSplitterBar>* Found = SplitterBars.Find(Divider))
			{
				Bar = *Found;
			}
			else
			{
				Bar = SNew(SDockSplitterBar)
					.Host(WeakSelf)
					.SplitNode(Divider.Key)
					.DividerIndex(Divider.Value)
					.bVerticalBar(Divider.Key->Orientation == EDockOrientation::Horizontal);
			}
			NewBars.Add(Divider, Bar);
			Children.Add(Bar.ToSharedRef());
			FChildEntry Entry;
			Entry.Kind = FChildEntry::EKind::Splitter;
			Entry.Widget = Bar;
			Entry.Node = Divider.Key;
			Entry.DividerIndex = Divider.Value;
			Entries.Add(Entry);
		}
	};

	for (int32 s = 0; s < DockSideCount; ++s)
	{
		AddContainer(Containers[s]);
	}
	for (int32 s = 0; s < DockSideCount; ++s)
	{
		if (!AreaEdgeBars[s])
		{
			const EDockSide Side = (EDockSide)s;
			AreaEdgeBars[s] = SNew(SDockSplitterBar)
				.Host(WeakSelf)
				.Side(Side)
				.bAreaEdge(true)
				.bVerticalBar(Side == EDockSide::Left || Side == EDockSide::Right);
		}
		if (Layout->GetAreaRoot((EDockSide)s).IsValid())
		{
			Children.Add(AreaEdgeBars[s].ToSharedRef());
			FChildEntry Entry;
			Entry.Kind = FChildEntry::EKind::AreaEdge;
			Entry.Widget = AreaEdgeBars[s];
			Entry.Side = (EDockSide)s;
			Entries.Add(Entry);
		}
	}
	for (int32 i = 0; i < Layout->GetFloating().Num(); ++i)
	{
		AddContainer(Containers[DockSideCount + i]);
	}

	GroupFrames = NewFrames;
	SplitterBars = NewBars;
	RefreshFrames();
}

void SDockHost::RefreshFrames()
{
	for (const TPair<const FDockNode*, TSharedPtr<SDockGroupFrame>>& Pair : GroupFrames)
	{
		const FDockNode* Node = Pair.Key;
		if (!Node || Node->Tabs.Num() == 0)
		{
			continue;
		}

		TArray<FDockTabInfo> Tabs;
		for (const FName Tab : Node->Tabs)
		{
			const FPanelEntry* Entry = Panels.Find(Tab);
			Tabs.Add({ Tab, Entry ? Entry->Title : FText::FromName(Tab) });
		}

		const int32 Active = FMath::Clamp(Node->ActiveTab, 0, Node->Tabs.Num() - 1);
		const FPanelEntry* ActiveEntry = Panels.Find(Node->Tabs[Active]);
		const TSharedRef<SWidget> Content = (ActiveEntry && ActiveEntry->Root.IsValid())
			? ActiveEntry->Root.ToSharedRef()
			: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Panel '%s' has no content registered."), *Node->Tabs[Active].ToString()))));
		Pair.Value->Update(Tabs, Active, Content);
	}
}

void SDockHost::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const FVector2f LocalSize = AllottedGeometry.GetLocalSize();
	Layout->ComputeLayout(FVector2D(LocalSize.X, LocalSize.Y), Metrics, Computed);

	auto Place = [&](const TSharedPtr<SWidget>& Widget, const FBox2D& Rect)
	{
		const EVisibility Visibility = Widget->GetVisibility();
		if (!ArrangedChildren.Accepts(Visibility))
		{
			return;
		}
		const FVector2D Size = Rect.GetSize();
		ArrangedChildren.AddWidget(Visibility, AllottedGeometry.MakeChild(Widget.ToSharedRef(),
			FVector2f((float)FMath::Max(0.0, Size.X), (float)FMath::Max(0.0, Size.Y)),
			FSlateLayoutTransform(FVector2f((float)Rect.Min.X, (float)Rect.Min.Y))));
	};

	constexpr float EdgeBarThickness = 8.f;
	for (const FChildEntry& Entry : Entries)
	{
		switch (Entry.Kind)
		{
		case FChildEntry::EKind::Group:
			if (const FDockComputedGroup* Group = Computed.FindGroup(Entry.Node))
			{
				Place(Entry.Widget, Group->Rect);
			}
			break;
		case FChildEntry::EKind::Splitter:
			for (const FDockComputedSplitter& Splitter : Computed.Splitters)
			{
				if (Splitter.SplitNode == Entry.Node && Splitter.DividerIndex == Entry.DividerIndex)
				{
					Place(Entry.Widget, Splitter.Rect);
					break;
				}
			}
			break;
		case FChildEntry::EKind::AreaEdge:
		{
			const int32 s = (int32)Entry.Side;
			if (!Computed.bAreaVisible[s])
			{
				break;
			}
			const FBox2D& Area = Computed.AreaRects[s];
			FBox2D Bar;
			switch (Entry.Side)
			{
			case EDockSide::Left: Bar = FBox2D(FVector2D(Area.Max.X - EdgeBarThickness * 0.5f, Area.Min.Y), FVector2D(Area.Max.X + EdgeBarThickness * 0.5f, Area.Max.Y)); break;
			case EDockSide::Right: Bar = FBox2D(FVector2D(Area.Min.X - EdgeBarThickness * 0.5f, Area.Min.Y), FVector2D(Area.Min.X + EdgeBarThickness * 0.5f, Area.Max.Y)); break;
			case EDockSide::Top: Bar = FBox2D(FVector2D(Area.Min.X, Area.Max.Y - EdgeBarThickness * 0.5f), FVector2D(Area.Max.X, Area.Max.Y + EdgeBarThickness * 0.5f)); break;
			default: Bar = FBox2D(FVector2D(Area.Min.X, Area.Min.Y - EdgeBarThickness * 0.5f), FVector2D(Area.Max.X, Area.Min.Y + EdgeBarThickness * 0.5f)); break;
			}
			Place(Entry.Widget, Bar);
			break;
		}
		}
	}
}

void SDockHost::HandleTabActivated(FName Panel)
{
	if (Layout->SetActiveTab(Panel))
	{
		RefreshFrames();
		OnLayoutChanged.ExecuteIfBound();
	}
}

void SDockHost::HandleTabClosed(FName Panel)
{
	if (Layout->HidePanel(Panel))
	{
		NotifyLayoutChanged();
		OnPanelClosed.ExecuteIfBound(Panel);
		OnLayoutChanged.ExecuteIfBound();
	}
}

FReply SDockHost::BeginSplitterDrag(const FDockNode* Split, int32 DividerIndex, const FPointerEvent& MouseEvent)
{
	if (!Split || !Split->IsSplit())
	{
		return FReply::Unhandled();
	}
	for (const FDockComputedSplitter& S : Computed.Splitters)
	{
		if (S.SplitNode == Split && S.DividerIndex == DividerIndex)
		{
			Drag = FDragState();
			Drag.Kind = FDragState::EKind::Splitter;
			Drag.Split = Split;
			Drag.DividerIndex = DividerIndex;
			Drag.StartRatios = Split->Ratios;
			Drag.AvailablePx = S.AvailablePx;
			Drag.bVerticalBar = Split->Orientation == EDockOrientation::Horizontal;
			Drag.StartLocal = FVector2D(GetCachedGeometry().AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}
	return FReply::Unhandled();
}

FReply SDockHost::BeginAreaEdgeDrag(EDockSide Side, const FPointerEvent& MouseEvent)
{
	if (!Layout->GetAreaRoot(Side).IsValid())
	{
		return FReply::Unhandled();
	}
	Drag = FDragState();
	Drag.Kind = FDragState::EKind::AreaEdge;
	Drag.Side = Side;
	Drag.StartExtent = Layout->GetAreaExtent(Side);
	Drag.bVerticalBar = Side == EDockSide::Left || Side == EDockSide::Right;
	Drag.StartLocal = FVector2D(GetCachedGeometry().AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SDockHost::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Drag.Kind == FDragState::EKind::None)
	{
		return FReply::Unhandled();
	}

	const FVector2D Local = FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	const FVector2D Delta = Local - Drag.StartLocal;

	if (Drag.Kind == FDragState::EKind::Splitter)
	{
		FDockNode* Split = const_cast<FDockNode*>(Drag.Split);
		Split->Ratios = Drag.StartRatios;
		const bool bHorizontal = Split->Orientation == EDockOrientation::Horizontal;
		Layout->ResizeSplit(Split, Drag.DividerIndex, (float)(bHorizontal ? Delta.X : Delta.Y), Drag.AvailablePx,
			bHorizontal ? (float)Metrics.MinPanelSize.X : (float)Metrics.MinPanelSize.Y);
	}
	else
	{
		const FVector2f Size = MyGeometry.GetLocalSize();
		const bool bHorizontalStrip = Drag.Side == EDockSide::Left || Drag.Side == EDockSide::Right;
		const float Sign = (Drag.Side == EDockSide::Left || Drag.Side == EDockSide::Top) ? 1.f : -1.f;
		const float Along = (float)(bHorizontalStrip ? Delta.X : Delta.Y);
		const float Usable = FMath::Max(1.f, (bHorizontalStrip ? Size.X : Size.Y - Metrics.TopMargin - Metrics.BottomMargin));
		const float Limit = Usable * Metrics.MaxEdgeFraction;
		Layout->SetAreaExtent(Drag.Side, FMath::Clamp(Drag.StartExtent + Sign * Along, FMath::Min(Metrics.MinEdgeExtent, Limit), Limit));
	}

	Invalidate(EInvalidateWidgetReason::Layout);
	return FReply::Handled();
}

FReply SDockHost::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Drag.Kind == FDragState::EKind::None || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	Drag = FDragState();
	OnLayoutChanged.ExecuteIfBound();
	return FReply::Handled().ReleaseMouseCapture();
}

FCursorReply SDockHost::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (Drag.Kind != FDragState::EKind::None)
	{
		return FCursorReply::Cursor(Drag.bVerticalBar ? EMouseCursor::ResizeLeftRight : EMouseCursor::ResizeUpDown);
	}
	return FCursorReply::Unhandled();
}

void SDockHost::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	if (Drag.Kind != FDragState::EKind::None)
	{
		Drag = FDragState();
		OnLayoutChanged.ExecuteIfBound();
	}
}
