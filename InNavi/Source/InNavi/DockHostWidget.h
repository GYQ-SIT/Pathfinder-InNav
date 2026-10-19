#pragma once

#include "CoreMinimal.h"
#include "DockLayoutModel.h"
#include "Layout/Children.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SPanel.h"

DECLARE_DELEGATE_OneParam(FOnDockPanelEvent, FName);

class SDockGroupFrame;
class SDockSplitterBar;
class SDockFloatFrame;

// Which sides of a floating window a resize drag moves (a bit mask); 0 means the whole window is moved.
namespace DockFloatEdge
{
	constexpr uint8 Left = 1;
	constexpr uint8 Right = 2;
	constexpr uint8 Top = 4;
	constexpr uint8 Bottom = 8;
}

// Renders an FDockLayout and turns user input into model operations.
//
// Everything is laid out from the model alone (FDockLayout::ComputeLayout) every time Slate asks for
// the arrangement - nothing is read back from painted geometry, so there is no "wait a few frames for
// layout to settle" step anywhere.
//
// The host itself is SelfHitTestInvisible: only its chrome (tab bars, splitters, panel content) takes
// mouse input, so clicks on the empty central area fall through to whatever is underneath.
class INNAVI_API SDockHost : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SDockHost) {}
		SLATE_ARGUMENT(TSharedPtr<FDockLayout>, Layout)
		SLATE_ARGUMENT(FDockMetrics, Metrics)
		// A tab's close button hid this panel.
		SLATE_EVENT(FOnDockPanelEvent, OnPanelClosed)
		// The user changed the layout (resize finished, tab switched, panel closed) - e.g. to save it.
		SLATE_EVENT(FSimpleDelegate, OnLayoutChanged)
	SLATE_END_ARGS()

	SDockHost();
	void Construct(const FArguments& InArgs);

	// Panel content is created once by the owner and kept alive here; the host only decides where it is
	// shown. bScrollable wraps it in a vertical scroll box so tall content scrolls instead of overflowing.
	void RegisterPanel(FName Id, const FText& Title, const TSharedRef<SWidget>& Content, bool bScrollable = true);
	void UnregisterPanel(FName Id);
	bool IsPanelRegistered(FName Id) const { return Panels.Contains(Id); }

	// Call after editing the model from outside (DockPanel, HidePanel, Clear, FromJson...).
	void NotifyLayoutChanged();

	TSharedRef<FDockLayout> GetLayout() const { return Layout.ToSharedRef(); }
	const FDockMetrics& GetMetrics() const { return Metrics; }
	void SetMetrics(const FDockMetrics& InMetrics);
	const FDockComputedLayout& GetComputedLayout() const { return Computed; }

	// --- Called by the chrome widgets ---
	void HandleTabActivated(FName Panel);
	void HandleTabClosed(FName Panel);
	FReply BeginSplitterDrag(const FDockNode* Split, int32 DividerIndex, const FPointerEvent& MouseEvent);
	FReply BeginAreaEdgeDrag(EDockSide Side, const FPointerEvent& MouseEvent);
	// Pressing a tab: a click just activates it; dragging it past a small threshold lifts the panel so it
	// can be dropped on a group, a group side, a screen edge, or another slot of its own tab strip.
	FReply BeginTabDrag(FName Panel, const FPointerEvent& MouseEvent);
	bool IsDraggingPanel() const { return Drag.Kind == FDragState::EKind::Panel && Drag.bActive; }
	// Pressing a floating window's drag strip (Edges == 0, moves the window) or its outer ring (Edges != 0,
	// resizes it). WindowRoot identifies the window.
	FReply BeginFloatDrag(const FDockNode* WindowRoot, uint8 Edges, const FPointerEvent& MouseEvent);
	// True while this splitter bar / area-edge bar is the one being dragged (the bars use it to stay
	// highlighted for exactly as long as the drag lasts - they never see the mouse release themselves, since
	// the host holds the mouse capture).
	bool IsDraggingBar(const FDockNode* Split, int32 DividerIndex, bool bAreaEdge, EDockSide Side) const;
	bool IsDraggingFloatingWindow() const { return Drag.Kind == FDragState::EKind::Float; }
	// True while the window being moved is carrying a lone panel over something it would dock into.
	bool HasFloatingDropTarget() const { return Drag.Kind == FDragState::EKind::Float && Drag.Target.IsValid(); }

	// True when ScreenPosition lies over Panel's group and Panel is that group's active tab (i.e. it is
	// actually on screen right now).
	bool IsScreenPositionOverPanel(FName Panel, const FVector2D& ScreenPosition) const;

	// True when ScreenPosition is over any docked or floating group or any splitter - i.e. over dock chrome
	// rather than the empty central canvas.
	bool IsScreenPositionOverDock(const FVector2D& ScreenPosition) const;

	// Width of the group currently holding Panel (0 if the panel isn't placed).
	float GetPanelWidth(FName Panel) const;

	// --- SWidget ---
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual FChildren* GetChildren() override { return &Children; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	// Raises a floating window to the front when anything inside it is pressed (never consumes the press).
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	struct FPanelEntry
	{
		FText Title;
		TSharedPtr<SWidget> Root;
	};

	struct FChildEntry
	{
		enum class EKind : uint8
		{
			Group,
			Splitter,
			AreaEdge,
			FloatFrame
		};

		EKind Kind = EKind::Group;
		TSharedPtr<SWidget> Widget;
		const FDockNode* Node = nullptr; // Group: the tab group; Splitter: the split
		int32 DividerIndex = 0;
		EDockSide Side = EDockSide::Left; // AreaEdge
	};

	struct FDragState
	{
		enum class EKind : uint8
		{
			None,
			Splitter,
			AreaEdge,
			Panel,
			Float
		};

		EKind Kind = EKind::None;
		// Panel drags only: bActive turns true once the cursor has moved past the click threshold.
		FName DraggedPanel = NAME_None;
		bool bActive = false;
		FVector2D CurrentLocal = FVector2D::ZeroVector;
		FDockDropTarget Target;
		int32 ReorderSlot = INDEX_NONE; // >= 0: reorder within the dragged panel's own tab strip
		FBox2D ReorderPreview;
		FVector2D FloatSize = FVector2D::ZeroVector; // size a window created by this drag gets

		// Float drags (moving / resizing an existing floating window).
		const FDockNode* FloatRoot = nullptr;
		FBox2D FloatStartRect;
		uint8 FloatEdges = 0;

		const FDockNode* Split = nullptr;
		int32 DividerIndex = 0;
		TArray<float> StartRatios;
		float AvailablePx = 0.f;
		EDockSide Side = EDockSide::Left;
		float StartExtent = 0.f;
		FVector2D StartLocal = FVector2D::ZeroVector;
		bool bVerticalBar = true; // the dragged bar is a vertical line (moves left-right)
	};

	void RebuildChildren();
	void RefreshFrames();
	void UpdatePanelDragTarget(const FVector2D& ViewportSize);
	void FinishPanelDrag();
	void FinishFloatDrag(const FGeometry& Geometry);
	// Repaints every splitter bar (after a bar drag started or ended).
	void InvalidateBars();
	int32 FindFloatingIndex(const FDockNode* WindowRoot) const;
	// Recomputes Computed from the model right now (after an edit made between two arrange passes).
	void RecomputeNow(const FGeometry& Geometry) const;

	TSharedPtr<FDockLayout> Layout;
	FDockMetrics Metrics;
	FOnDockPanelEvent OnPanelClosed;
	FSimpleDelegate OnLayoutChanged;

	TMap<FName, FPanelEntry> Panels;
	TSlotlessChildren<SWidget> Children;
	TArray<FChildEntry> Entries; // same order as Children (paint order)
	TMap<const FDockNode*, TSharedPtr<SDockGroupFrame>> GroupFrames;
	TMap<TPair<const FDockNode*, int32>, TSharedPtr<SDockSplitterBar>> SplitterBars;
	TSharedPtr<SDockSplitterBar> AreaEdgeBars[DockSideCount];
	TMap<const FDockNode*, TSharedPtr<SDockFloatFrame>> FloatFrames;

	mutable FDockComputedLayout Computed;
	FDragState Drag;
};
