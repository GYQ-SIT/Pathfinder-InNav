#pragma once

#include "CoreMinimal.h"
#include "DockLayoutModel.h"
#include "Layout/Children.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SPanel.h"

DECLARE_DELEGATE_OneParam(FOnDockPanelEvent, FName);

class SDockGroupFrame;
class SDockSplitterBar;

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

	// True when ScreenPosition lies over Panel's group and Panel is that group's active tab (i.e. it is
	// actually on screen right now).
	bool IsScreenPositionOverPanel(FName Panel, const FVector2D& ScreenPosition) const;

	// --- SWidget ---
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual FChildren* GetChildren() override { return &Children; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
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
			AreaEdge
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
			Panel
		};

		EKind Kind = EKind::None;
		// Panel drags only: bActive turns true once the cursor has moved past the click threshold.
		FName DraggedPanel = NAME_None;
		bool bActive = false;
		FVector2D CurrentLocal = FVector2D::ZeroVector;
		FDockDropTarget Target;
		int32 ReorderSlot = INDEX_NONE; // >= 0: reorder within the dragged panel's own tab strip
		FBox2D ReorderPreview;

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

	mutable FDockComputedLayout Computed;
	FDragState Drag;
};
