#pragma once

#include "CoreMinimal.h"
#include "Math/Box2D.h"

class FJsonObject;

// Pure data + algorithms for a VS/UE-style docking layout. No UObjects, no Slate/UMG: everything here
// is deterministic and unit-testable. The host widget renders FDockComputedLayout and feeds user input
// back through the operations below - it never reads painted geometry back into the model.
//
// Shape of a layout:
//   - Four edge areas (Left/Right/Top/Bottom). Each holds one tree and a thickness (Extent, pixels).
//     Edge areas overlay the central canvas (the central canvas is NOT part of the tree).
//   - A list of floating windows, each holding one tree plus a position/size. Later entries are on top.
//   - A tree is made of Split nodes (children laid out along one axis, sized by Ratios) and TabGroup
//     leaves (an ordered list of panel names with one active).
//   - Panels are identified by FName so the model is independent of any UI enum and can be saved by name.

enum class EDockSide : uint8
{
	Left,
	Right,
	Top,
	Bottom
};
constexpr int32 DockSideCount = 4;

enum class EDockOrientation : uint8
{
	Horizontal, // children are laid out left to right
	Vertical    // children are laid out top to bottom
};

enum class EDockDropZone : uint8
{
	None,
	Left,
	Right,
	Top,
	Bottom,
	Center
};

struct INNAVI_API FDockNode
{
	enum class EType : uint8
	{
		Split,
		TabGroup
	};

	int32 Id = 0;
	EType Type = EType::TabGroup;

	// Split only.
	EDockOrientation Orientation = EDockOrientation::Horizontal;
	TArray<TSharedPtr<FDockNode>> Children;
	TArray<float> Ratios; // same length as Children, each > 0, sum == 1 after Normalize

	// TabGroup only.
	TArray<FName> Tabs;
	int32 ActiveTab = 0;

	bool IsSplit() const { return Type == EType::Split; }
	bool IsTabGroup() const { return Type == EType::TabGroup; }
};

struct INNAVI_API FDockFloatingWindow
{
	TSharedPtr<FDockNode> Root;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D(320.f, 360.f);
};

// Where a hidden panel used to live, so showing it again puts it back near where it was.
struct INNAVI_API FDockHint
{
	enum class EKind : uint8
	{
		Docked,
		Floating
	};

	EKind Kind = EKind::Docked;
	EDockSide Side = EDockSide::Left;
	float Extent = 0.f; // that edge area's thickness at the time (pixels), 0 = unknown
	FName NeighborPanel = NAME_None;
	EDockDropZone Zone = EDockDropZone::None;
	FVector2D FloatPosition = FVector2D::ZeroVector;
	FVector2D FloatSize = FVector2D(320.f, 360.f);
};

struct INNAVI_API FDockLocation
{
	enum class EKind : uint8
	{
		None,
		Area,
		Floating
	};

	EKind Kind = EKind::None;
	EDockSide Side = EDockSide::Left; // Area only
	int32 FloatingIndex = INDEX_NONE; // Floating only
	const FDockNode* Group = nullptr; // the tab group holding the panel
	int32 TabIndex = INDEX_NONE;
};

struct INNAVI_API FDockMetrics
{
	float TopMargin = 0.f;       // e.g. the menu bar - edge areas start below it
	float BottomMargin = 0.f;
	float TabBarHeight = 26.f;
	float SplitterThickness = 6.f;
	FVector2D MinPanelSize = FVector2D(100.f, 80.f);
	float TabMinWidth = 60.f;
	float TabMaxWidth = 160.f;
	float MinEdgeExtent = 120.f;
	float MaxEdgeFraction = 0.6f; // an edge area may use at most this share of the usable width/height
	float EdgeGuideSize = 36.f;   // distance from the screen edge that counts as "dock to this edge"
	float FloatingMinVisible = 40.f;
};

struct INNAVI_API FDockComputedGroup
{
	const FDockNode* Node = nullptr;
	FBox2D Rect;
	FBox2D TabBarRect;
	FBox2D ContentRect;
	TArray<FBox2D> TabRects; // one per tab, left to right
	// Which edge area / floating window this group is in (Floating: FloatingIndex >= 0).
	EDockSide Side = EDockSide::Left;
	int32 FloatingIndex = INDEX_NONE;
};

struct INNAVI_API FDockComputedSplitter
{
	const FDockNode* SplitNode = nullptr;
	int32 DividerIndex = 0; // sits between child DividerIndex and DividerIndex + 1
	FBox2D Rect;
	EDockOrientation Orientation = EDockOrientation::Horizontal;
	float AvailablePx = 0.f; // length the split's children share (excludes splitters)
	EDockSide Side = EDockSide::Left;
	int32 FloatingIndex = INDEX_NONE;
};

struct INNAVI_API FDockComputedLayout
{
	FBox2D AreaRects[DockSideCount];
	bool bAreaVisible[DockSideCount] = { false, false, false, false };
	TArray<FBox2D> FloatingRects; // parallel to the layout's floating list
	TArray<FDockComputedGroup> Groups;
	TArray<FDockComputedSplitter> Splitters;

	const FDockComputedGroup* FindGroup(const FDockNode* Node) const;
};

struct INNAVI_API FDockDropTarget
{
	enum class EKind : uint8
	{
		None,
		Group, // drop relative to an existing tab group
		Edge   // drop onto an outer screen edge (new/extended edge area)
	};

	EKind Kind = EKind::None;
	const FDockNode* Group = nullptr;
	FName TargetPanel = NAME_None; // a panel inside Group to dock relative to
	EDockDropZone Zone = EDockDropZone::None;
	EDockSide Side = EDockSide::Left;
	FBox2D PreviewRect; // what the host should highlight

	bool IsValid() const { return Kind != EKind::None; }
};

class INNAVI_API FDockLayout
{
public:
	FDockLayout();
	FDockLayout(const FDockLayout&) = delete;
	FDockLayout& operator=(const FDockLayout&) = delete;

	// How thick a freshly created edge area is (pixels).
	float DefaultExtent[DockSideCount];

	// --- Queries ---
	TSharedPtr<FDockNode> GetAreaRoot(EDockSide Side) const { return Areas[(int32)Side].Root; }
	float GetAreaExtent(EDockSide Side) const { return Areas[(int32)Side].Extent; }
	const TArray<FDockFloatingWindow>& GetFloating() const { return Floating; }
	bool ContainsPanel(FName Panel) const;
	bool IsHidden(FName Panel) const { return HiddenHints.Contains(Panel); }
	bool FindPanel(FName Panel, FDockLocation& OutLocation) const;
	TArray<FName> GetAllPanels() const;
	const FDockHint* GetHint(FName Panel) const { return HiddenHints.Find(Panel); }

	// --- Operations. Each returns false (and leaves the layout unchanged) when it doesn't apply. ---

	// Moves Panel (from wherever it is, or from hidden) next to TargetPanel. Center adds it as a tab of
	// the target's group; Left/Right/Top/Bottom split the target's group.
	bool DockPanel(FName Panel, FName TargetPanel, EDockDropZone Zone);

	// Docks Panel against an outer screen edge: starts that edge area if it's empty, otherwise adds a
	// new outermost group and grows the area by DefaultExtent[Side].
	bool DockPanelToEdge(FName Panel, EDockSide Side);

	bool FloatPanel(FName Panel, const FVector2D& Position, const FVector2D& Size);

	// Removes Panel from the layout but remembers where it was.
	bool HidePanel(FName Panel);

	// Puts a hidden (or never-placed) panel back near its last position, or docked to FallbackSide.
	bool ShowPanel(FName Panel, EDockSide FallbackSide);

	bool SetActiveTab(FName Panel);
	bool MoveTab(FName Panel, int32 NewIndex);

	// Moves the divider between child DividerIndex and DividerIndex + 1 of Split by DeltaPx.
	// AvailablePx is the length the children share (FDockComputedSplitter::AvailablePx).
	bool ResizeSplit(FDockNode* Split, int32 DividerIndex, float DeltaPx, float AvailablePx, float MinChildPx);

	bool SetAreaExtent(EDockSide Side, float ExtentPx);
	bool MoveFloating(int32 Index, const FVector2D& Position);
	bool ResizeFloating(int32 Index, const FVector2D& Size);
	// Raises a floating window to the top of the stack; returns its new index.
	int32 BringFloatingToFront(int32 Index);

	// --- Layout and hit testing (pure functions of the model + viewport) ---
	void ComputeLayout(const FVector2D& Viewport, const FDockMetrics& Metrics, FDockComputedLayout& Out) const;

	// What a drag of DraggedPanel would do if released at Point. A panel never targets itself.
	FDockDropTarget HitTestDropTarget(const FVector2D& Point, FName DraggedPanel, const FVector2D& Viewport, const FDockMetrics& Metrics, const FDockComputedLayout& Computed) const;

	// --- Persistence ---
	// Viewport sizes are only used to store extents / floating rects as fractions so a layout saved on
	// one resolution restores sensibly on another.
	TSharedPtr<FJsonObject> ToJson(const FVector2D& Viewport) const;
	// Replaces this layout. Returns false (layout untouched) for a wrong/missing version or malformed
	// shape. Panels not in KnownPanels are dropped; known panels absent from the file are reported in
	// OutMissingPanels so the caller can place them at their defaults.
	bool FromJson(const TSharedPtr<FJsonObject>& Root, const FVector2D& Viewport, const TSet<FName>& KnownPanels, TArray<FName>& OutMissingPanels);

	static constexpr int32 JsonLayoutVersion = 3;

	// --- Maintenance ---
	TSharedRef<FDockLayout> Clone() const;
	void Clear();
	// Restores every invariant (see Validate) after arbitrary edits. Operations call it themselves.
	void Normalize();
	// True when the invariants hold: each panel at most once, no empty groups, splits have >= 2
	// children with positive ratios summing to 1 and never nest a split of the same orientation.
	bool Validate(FString& OutError) const;

private:
	struct FArea
	{
		TSharedPtr<FDockNode> Root;
		float Extent = 0.f;
	};

	struct FNodeRef
	{
		TSharedPtr<FDockNode>* Slot = nullptr; // where the node pointer lives (area root / floating root / parent's child slot)
		FDockNode* Parent = nullptr;
		int32 IndexInParent = INDEX_NONE;
	};

	TSharedPtr<FDockNode> MakeGroup(FName Panel);
	static bool FindInSlot(TSharedPtr<FDockNode>* Slot, FDockNode* Parent, int32 IndexInParent, FName Panel, FNodeRef& OutRef);
	bool FindGroupSlot(FName Panel, FNodeRef& OutRef);
	bool RemovePanelTab(FName Panel);
	void RecordHint(FName Panel);
	void PlaceInEdgeArea(FName Panel, EDockSide Side);

	FArea Areas[DockSideCount];
	TArray<FDockFloatingWindow> Floating;
	TMap<FName, FDockHint> HiddenHints;
	int32 NextNodeId = 1;
};
