#if WITH_DEV_AUTOMATION_TESTS

#include "DockLayoutModel.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	// (flags are written inline in each test macro below)

	FName P(const TCHAR* Name) { return FName(Name); }

	FString DescribeNode(const FDockNode* N)
	{
		if (!N)
		{
			return TEXT("-");
		}
		if (N->IsTabGroup())
		{
			FString S = TEXT("[");
			for (int32 i = 0; i < N->Tabs.Num(); ++i)
			{
				S += (i > 0 ? TEXT(",") : TEXT(""));
				S += N->Tabs[i].ToString();
			}
			return S + TEXT("]");
		}
		FString S = N->Orientation == EDockOrientation::Horizontal ? TEXT("H(") : TEXT("V(");
		for (int32 i = 0; i < N->Children.Num(); ++i)
		{
			S += (i > 0 ? TEXT(",") : TEXT(""));
			S += DescribeNode(N->Children[i].Get());
		}
		return S + TEXT(")");
	}

	FString DescribeArea(const FDockLayout& L, EDockSide Side)
	{
		return DescribeNode(L.GetAreaRoot(Side).Get());
	}

	bool CheckValid(FAutomationTestBase& T, const FDockLayout& L, const TCHAR* Step)
	{
		FString Err;
		const bool bValid = L.Validate(Err);
		if (!bValid)
		{
			T.AddError(FString::Printf(TEXT("[%s] layout invalid: %s"), Step, *Err));
		}
		return bValid;
	}

	bool NodesEquivalent(const FDockNode* A, const FDockNode* B, FString& Why)
	{
		if (!A || !B)
		{
			if (A != B)
			{
				Why = TEXT("one tree is empty");
				return false;
			}
			return true;
		}
		if (A->Type != B->Type)
		{
			Why = TEXT("node type differs");
			return false;
		}
		if (A->IsTabGroup())
		{
			if (A->Tabs != B->Tabs || A->ActiveTab != B->ActiveTab)
			{
				Why = FString::Printf(TEXT("tabs differ: %s vs %s"), *DescribeNode(A), *DescribeNode(B));
				return false;
			}
			return true;
		}
		if (A->Orientation != B->Orientation || A->Children.Num() != B->Children.Num())
		{
			Why = FString::Printf(TEXT("split differs: %s vs %s"), *DescribeNode(A), *DescribeNode(B));
			return false;
		}
		for (int32 i = 0; i < A->Children.Num(); ++i)
		{
			if (!FMath::IsNearlyEqual(A->Ratios[i], B->Ratios[i], 1e-4f))
			{
				Why = FString::Printf(TEXT("ratio %d differs: %f vs %f"), i, A->Ratios[i], B->Ratios[i]);
				return false;
			}
			if (!NodesEquivalent(A->Children[i].Get(), B->Children[i].Get(), Why))
			{
				return false;
			}
		}
		return true;
	}

	bool LayoutsEquivalent(const FDockLayout& A, const FDockLayout& B, const TArray<FName>& AllPanels, FString& Why)
	{
		for (int32 s = 0; s < DockSideCount; ++s)
		{
			const EDockSide Side = (EDockSide)s;
			if (!NodesEquivalent(A.GetAreaRoot(Side).Get(), B.GetAreaRoot(Side).Get(), Why))
			{
				Why = FString::Printf(TEXT("area %d: %s"), s, *Why);
				return false;
			}
			if (A.GetAreaRoot(Side).IsValid() && !FMath::IsNearlyEqual(A.GetAreaExtent(Side), B.GetAreaExtent(Side), 0.05f))
			{
				Why = FString::Printf(TEXT("area %d extent %f vs %f"), s, A.GetAreaExtent(Side), B.GetAreaExtent(Side));
				return false;
			}
		}
		if (A.GetFloating().Num() != B.GetFloating().Num())
		{
			Why = TEXT("floating window count differs");
			return false;
		}
		for (int32 i = 0; i < A.GetFloating().Num(); ++i)
		{
			const FDockFloatingWindow& WA = A.GetFloating()[i];
			const FDockFloatingWindow& WB = B.GetFloating()[i];
			if (!NodesEquivalent(WA.Root.Get(), WB.Root.Get(), Why)
				|| !WA.Position.Equals(WB.Position, 0.05) || !WA.Size.Equals(WB.Size, 0.05))
			{
				Why = FString::Printf(TEXT("floating %d differs (%s)"), i, *Why);
				return false;
			}
		}
		for (const FName Panel : AllPanels)
		{
			if (A.IsHidden(Panel) != B.IsHidden(Panel))
			{
				Why = FString::Printf(TEXT("hidden state of %s differs"), *Panel.ToString());
				return false;
			}
			const FDockHint* HA = A.GetHint(Panel);
			const FDockHint* HB = B.GetHint(Panel);
			if (HA && HB)
			{
				if (HA->Kind != HB->Kind || HA->Side != HB->Side || HA->NeighborPanel != HB->NeighborPanel || HA->Zone != HB->Zone
					|| !FMath::IsNearlyEqual(HA->Extent, HB->Extent, 0.05f))
				{
					Why = FString::Printf(TEXT("hint of %s differs"), *Panel.ToString());
					return false;
				}
			}
		}
		return true;
	}

	FString JsonToString(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	TSharedPtr<FJsonObject> StringToJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Obj);
		return Obj;
	}

	TSet<FName> KnownSet(const TArray<FName>& Names)
	{
		TSet<FName> Set;
		for (const FName N : Names)
		{
			Set.Add(N);
		}
		return Set;
	}

	TSharedPtr<FJsonObject> JsonTabs(const TArray<FString>& Names, int32 Active = 0)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("type"), TEXT("tabs"));
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& N : Names)
		{
			Arr.Add(MakeShared<FJsonValueString>(N));
		}
		Obj->SetArrayField(TEXT("tabs"), Arr);
		Obj->SetNumberField(TEXT("active"), Active);
		return Obj;
	}

	TSharedPtr<FJsonObject> JsonSplit(const TCHAR* Orientation, const TArray<TSharedPtr<FJsonObject>>& Children, const TArray<double>& Ratios)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("type"), TEXT("split"));
		Obj->SetStringField(TEXT("orientation"), Orientation);
		TArray<TSharedPtr<FJsonValue>> ChildArr;
		for (const TSharedPtr<FJsonObject>& C : Children)
		{
			ChildArr.Add(MakeShared<FJsonValueObject>(C));
		}
		Obj->SetArrayField(TEXT("children"), ChildArr);
		TArray<TSharedPtr<FJsonValue>> RatioArr;
		for (const double R : Ratios)
		{
			RatioArr.Add(MakeShared<FJsonValueNumber>(R));
		}
		Obj->SetArrayField(TEXT("ratios"), RatioArr);
		return Obj;
	}

	TSharedPtr<FJsonObject> JsonLayoutWithLeftRoot(const TSharedPtr<FJsonObject>& Root, double Version = FDockLayout::JsonLayoutVersion)
	{
		TSharedPtr<FJsonObject> Layout = MakeShared<FJsonObject>();
		Layout->SetNumberField(TEXT("layoutVersion"), Version);
		TSharedPtr<FJsonObject> Areas = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> Left = MakeShared<FJsonObject>();
		Left->SetNumberField(TEXT("extent"), 0.2);
		Left->SetObjectField(TEXT("root"), Root);
		Areas->SetObjectField(TEXT("Left"), Left);
		Layout->SetObjectField(TEXT("areas"), Areas);
		return Layout;
	}

	const FVector2D HD(1920.0, 1080.0);
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockEdgeAndTabsTest, "InNavi.Dock.EdgeAndTabs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockEdgeAndTabsTest::RunTest(const FString& Parameters)
{
	FDockLayout L;
	TestTrue(TEXT("dock A to the left edge"), L.DockPanelToEdge(P(TEXT("A")), EDockSide::Left));
	TestEqual(TEXT("left area holds [A]"), DescribeArea(L, EDockSide::Left), FString(TEXT("[A]")));
	TestNearlyEqual(TEXT("new area gets the default extent"), L.GetAreaExtent(EDockSide::Left), L.DefaultExtent[(int32)EDockSide::Left], 0.01f);
	TestFalse(TEXT("A is already alone on that edge"), L.DockPanelToEdge(P(TEXT("A")), EDockSide::Left));
	CheckValid(*this, L, TEXT("edge"));

	TestTrue(TEXT("B becomes a tab of A"), L.DockPanel(P(TEXT("B")), P(TEXT("A")), EDockDropZone::Center));
	TestEqual(TEXT("tabs"), DescribeArea(L, EDockSide::Left), FString(TEXT("[A,B]")));
	FDockLocation Location;
	TestTrue(TEXT("find B"), L.FindPanel(P(TEXT("B")), Location));
	TestEqual(TEXT("B is active after being added"), Location.Group->ActiveTab, 1);

	TestTrue(TEXT("activate A"), L.SetActiveTab(P(TEXT("A"))));
	L.FindPanel(P(TEXT("A")), Location);
	TestEqual(TEXT("A is active"), Location.Group->ActiveTab, 0);

	TestTrue(TEXT("move B to the front"), L.MoveTab(P(TEXT("B")), 0));
	TestEqual(TEXT("reordered"), DescribeArea(L, EDockSide::Left), FString(TEXT("[B,A]")));
	L.FindPanel(P(TEXT("A")), Location);
	TestEqual(TEXT("the active tab is still A after reordering"), Location.Group->Tabs[Location.Group->ActiveTab], P(TEXT("A")));

	TestTrue(TEXT("docking into its own group just activates"), L.DockPanel(P(TEXT("B")), P(TEXT("A")), EDockDropZone::Center));
	TestEqual(TEXT("still two tabs"), DescribeArea(L, EDockSide::Left), FString(TEXT("[B,A]")));

	TestFalse(TEXT("unknown target"), L.DockPanel(P(TEXT("C")), P(TEXT("Nope")), EDockDropZone::Center));
	TestFalse(TEXT("self target"), L.DockPanel(P(TEXT("A")), P(TEXT("A")), EDockDropZone::Left));
	TestFalse(TEXT("no zone"), L.DockPanel(P(TEXT("C")), P(TEXT("A")), EDockDropZone::None));
	TestFalse(TEXT("C was not added by a failed dock"), L.ContainsPanel(P(TEXT("C"))));
	CheckValid(*this, L, TEXT("tabs"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockSplitsAndFlattenTest, "InNavi.Dock.SplitsAndFlatten", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockSplitsAndFlattenTest::RunTest(const FString& Parameters)
{
	FDockLayout L;
	L.DockPanelToEdge(P(TEXT("A")), EDockSide::Left);

	TestTrue(TEXT("B below A"), L.DockPanel(P(TEXT("B")), P(TEXT("A")), EDockDropZone::Bottom));
	TestEqual(TEXT("vertical split"), DescribeArea(L, EDockSide::Left), FString(TEXT("V([A],[B])")));
	CheckValid(*this, L, TEXT("B"));

	TestTrue(TEXT("C below B"), L.DockPanel(P(TEXT("C")), P(TEXT("B")), EDockDropZone::Bottom));
	TestEqual(TEXT("same orientation is flattened into one split"), DescribeArea(L, EDockSide::Left), FString(TEXT("V([A],[B],[C])")));
	const FDockNode* Root = L.GetAreaRoot(EDockSide::Left).Get();
	TestNearlyEqual(TEXT("A keeps half"), Root->Ratios[0], 0.5f, 1e-4f);
	TestNearlyEqual(TEXT("B takes a quarter"), Root->Ratios[1], 0.25f, 1e-4f);
	TestNearlyEqual(TEXT("C takes a quarter"), Root->Ratios[2], 0.25f, 1e-4f);

	TestTrue(TEXT("D right of A"), L.DockPanel(P(TEXT("D")), P(TEXT("A")), EDockDropZone::Right));
	TestEqual(TEXT("different orientation nests"), DescribeArea(L, EDockSide::Left), FString(TEXT("V(H([A],[D]),[B],[C])")));

	TestTrue(TEXT("E below D"), L.DockPanel(P(TEXT("E")), P(TEXT("D")), EDockDropZone::Bottom));
	TestEqual(TEXT("three levels deep"), DescribeArea(L, EDockSide::Left), FString(TEXT("V(H([A],V([D],[E])),[B],[C])")));
	CheckValid(*this, L, TEXT("nested"));

	TestTrue(TEXT("F left of B inserts before it"), L.DockPanel(P(TEXT("F")), P(TEXT("B")), EDockDropZone::Left));
	TestEqual(TEXT("F beside B"), DescribeArea(L, EDockSide::Left), FString(TEXT("V(H([A],V([D],[E])),H([F],[B]),[C])")));
	CheckValid(*this, L, TEXT("F"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockUndockCollapsesTest, "InNavi.Dock.UndockCollapses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockUndockCollapsesTest::RunTest(const FString& Parameters)
{
	FDockLayout L;
	L.DockPanelToEdge(P(TEXT("A")), EDockSide::Left);
	L.DockPanel(P(TEXT("B")), P(TEXT("A")), EDockDropZone::Bottom);
	L.DockPanel(P(TEXT("D")), P(TEXT("A")), EDockDropZone::Right);
	L.DockPanel(P(TEXT("E")), P(TEXT("D")), EDockDropZone::Bottom);
	TestEqual(TEXT("start"), DescribeArea(L, EDockSide::Left), FString(TEXT("V(H([A],V([D],[E])),[B])")));

	TestTrue(TEXT("hide A"), L.HidePanel(P(TEXT("A"))));
	TestEqual(TEXT("the single-child horizontal split collapses and flattens into the parent"), DescribeArea(L, EDockSide::Left), FString(TEXT("V([D],[E],[B])")));
	const FDockNode* Root = L.GetAreaRoot(EDockSide::Left).Get();
	TestNearlyEqual(TEXT("D"), Root->Ratios[0], 0.25f, 1e-4f);
	TestNearlyEqual(TEXT("E"), Root->Ratios[1], 0.25f, 1e-4f);
	TestNearlyEqual(TEXT("B"), Root->Ratios[2], 0.5f, 1e-4f);
	CheckValid(*this, L, TEXT("hide A"));

	TestTrue(TEXT("hide D"), L.HidePanel(P(TEXT("D"))));
	TestTrue(TEXT("hide E"), L.HidePanel(P(TEXT("E"))));
	TestEqual(TEXT("one panel left"), DescribeArea(L, EDockSide::Left), FString(TEXT("[B]")));
	TestTrue(TEXT("hide B"), L.HidePanel(P(TEXT("B"))));
	TestFalse(TEXT("the area disappears when empty"), L.GetAreaRoot(EDockSide::Left).IsValid());
	TestEqual(TEXT("nothing is placed"), L.GetAllPanels().Num(), 0);
	TestFalse(TEXT("hiding something not placed"), L.HidePanel(P(TEXT("B"))));
	CheckValid(*this, L, TEXT("all hidden"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockMovePanelsTest, "InNavi.Dock.MovePanels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockMovePanelsTest::RunTest(const FString& Parameters)
{
	FDockLayout L;
	L.DockPanelToEdge(P(TEXT("A")), EDockSide::Left);
	L.DockPanelToEdge(P(TEXT("B")), EDockSide::Right);
	L.DockPanel(P(TEXT("C")), P(TEXT("B")), EDockDropZone::Center);

	TestTrue(TEXT("move A next to B's tabs"), L.DockPanel(P(TEXT("A")), P(TEXT("B")), EDockDropZone::Top));
	TestFalse(TEXT("the now-empty left area is gone"), L.GetAreaRoot(EDockSide::Left).IsValid());
	TestEqual(TEXT("A above B,C"), DescribeArea(L, EDockSide::Right), FString(TEXT("V([A],[B,C])")));
	TestEqual(TEXT("each panel exactly once"), L.GetAllPanels().Num(), 3);

	TestTrue(TEXT("tear C off to the bottom of its own group"), L.DockPanel(P(TEXT("C")), P(TEXT("B")), EDockDropZone::Bottom));
	TestEqual(TEXT("C split below B"), DescribeArea(L, EDockSide::Right), FString(TEXT("V([A],[B],[C])")));

	TestTrue(TEXT("move B to the top edge"), L.DockPanelToEdge(P(TEXT("B")), EDockSide::Top));
	TestEqual(TEXT("right area shrinks"), DescribeArea(L, EDockSide::Right), FString(TEXT("V([A],[C])")));
	TestEqual(TEXT("top area"), DescribeArea(L, EDockSide::Top), FString(TEXT("[B]")));
	CheckValid(*this, L, TEXT("moves"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockEdgeExtensionTest, "InNavi.Dock.EdgeExtension", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockEdgeExtensionTest::RunTest(const FString& Parameters)
{
	FDockLayout L;
	L.DockPanelToEdge(P(TEXT("A")), EDockSide::Left);
	L.DockPanelToEdge(P(TEXT("B")), EDockSide::Left);
	TestEqual(TEXT("B becomes the new outermost column"), DescribeArea(L, EDockSide::Left), FString(TEXT("H([B],[A])")));
	TestNearlyEqual(TEXT("the area grows by the default extent"), L.GetAreaExtent(EDockSide::Left), 560.f, 0.01f);
	TestNearlyEqual(TEXT("existing content keeps its width"), L.GetAreaRoot(EDockSide::Left)->Ratios[1], 0.5f, 1e-4f);

	L.DockPanelToEdge(P(TEXT("C")), EDockSide::Left);
	TestEqual(TEXT("flattened, not nested"), DescribeArea(L, EDockSide::Left), FString(TEXT("H([C],[B],[A])")));
	TestNearlyEqual(TEXT("extent"), L.GetAreaExtent(EDockSide::Left), 840.f, 0.01f);
	const FDockNode* Root = L.GetAreaRoot(EDockSide::Left).Get();
	TestNearlyEqual(TEXT("new column share"), Root->Ratios[0], 1.f / 3.f, 1e-3f);

	L.DockPanelToEdge(P(TEXT("D")), EDockSide::Right);
	L.DockPanelToEdge(P(TEXT("E")), EDockSide::Top);
	L.DockPanelToEdge(P(TEXT("F")), EDockSide::Bottom);
	TestNearlyEqual(TEXT("right"), L.GetAreaExtent(EDockSide::Right), 280.f, 0.01f);
	TestNearlyEqual(TEXT("top"), L.GetAreaExtent(EDockSide::Top), 200.f, 0.01f);

	L.DockPanelToEdge(P(TEXT("G")), EDockSide::Top);
	TestEqual(TEXT("top stacks vertically, newest outermost"), DescribeArea(L, EDockSide::Top), FString(TEXT("V([G],[E])")));
	CheckValid(*this, L, TEXT("edges"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockResizeSplitTest, "InNavi.Dock.ResizeSplit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockResizeSplitTest::RunTest(const FString& Parameters)
{
	FDockLayout L;
	L.DockPanelToEdge(P(TEXT("A")), EDockSide::Left);
	L.DockPanel(P(TEXT("B")), P(TEXT("A")), EDockDropZone::Right);

	FDockMetrics M;
	FDockComputedLayout C;
	L.ComputeLayout(HD, M, C);
	TestEqual(TEXT("one splitter"), C.Splitters.Num(), 1);
	if (C.Splitters.Num() != 1)
	{
		return false;
	}
	const FDockComputedSplitter S = C.Splitters[0];
	TestNearlyEqual(TEXT("children share the area minus the splitter"), S.AvailablePx, 280.f - M.SplitterThickness, 0.01f);

	FDockNode* Split = const_cast<FDockNode*>(S.SplitNode);
	TestTrue(TEXT("drag right by 20"), L.ResizeSplit(Split, 0, 20.f, S.AvailablePx, 100.f));
	TestNearlyEqual(TEXT("left child"), Split->Ratios[0], (137.f + 20.f) / S.AvailablePx, 1e-3f);
	TestNearlyEqual(TEXT("ratios still sum to 1"), Split->Ratios[0] + Split->Ratios[1], 1.f, 1e-5f);

	TestTrue(TEXT("huge drag right"), L.ResizeSplit(Split, 0, 5000.f, S.AvailablePx, 100.f));
	TestNearlyEqual(TEXT("right child stops at its minimum"), Split->Ratios[1] * S.AvailablePx, 100.f, 0.01f);
	TestTrue(TEXT("huge drag left"), L.ResizeSplit(Split, 0, -5000.f, S.AvailablePx, 100.f));
	TestNearlyEqual(TEXT("left child stops at its minimum"), Split->Ratios[0] * S.AvailablePx, 100.f, 0.01f);

	TestFalse(TEXT("bad divider"), L.ResizeSplit(Split, 5, 10.f, S.AvailablePx, 100.f));
	TestFalse(TEXT("null split"), L.ResizeSplit(nullptr, 0, 10.f, S.AvailablePx, 100.f));
	CheckValid(*this, L, TEXT("resize"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockComputeLayoutTest, "InNavi.Dock.ComputeLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockComputeLayoutTest::RunTest(const FString& Parameters)
{
	FDockLayout L;
	L.DockPanelToEdge(P(TEXT("A")), EDockSide::Left);
	L.DockPanel(P(TEXT("B")), P(TEXT("A")), EDockDropZone::Bottom);
	L.SetAreaExtent(EDockSide::Left, 300.f);
	L.DockPanelToEdge(P(TEXT("C")), EDockSide::Right);
	L.SetAreaExtent(EDockSide::Right, 250.f);

	FDockMetrics M;
	M.TopMargin = 30.f;
	FDockComputedLayout C;
	L.ComputeLayout(HD, M, C);

	const FBox2D Left = C.AreaRects[(int32)EDockSide::Left];
	const FBox2D Right = C.AreaRects[(int32)EDockSide::Right];
	TestNearlyEqual(TEXT("left area x"), (float)Left.Max.X, 300.f, 0.01f);
	TestNearlyEqual(TEXT("left area starts below the menu bar"), (float)Left.Min.Y, 30.f, 0.01f);
	TestNearlyEqual(TEXT("left area reaches the bottom"), (float)Left.Max.Y, 1080.f, 0.01f);
	TestNearlyEqual(TEXT("right area x"), (float)Right.Min.X, 1670.f, 0.01f);
	TestFalse(TEXT("top is empty"), C.bAreaVisible[(int32)EDockSide::Top]);
	TestEqual(TEXT("three groups"), C.Groups.Num(), 3);
	TestEqual(TEXT("one splitter between A and B"), C.Splitters.Num(), 1);

	FDockLocation LocA, LocB;
	L.FindPanel(P(TEXT("A")), LocA);
	L.FindPanel(P(TEXT("B")), LocB);
	const FDockComputedGroup* GA = C.FindGroup(LocA.Group);
	const FDockComputedGroup* GB = C.FindGroup(LocB.Group);
	TestNotNull(TEXT("group A"), GA);
	TestNotNull(TEXT("group B"), GB);
	if (GA && GB)
	{
		TestNearlyEqual(TEXT("A top"), (float)GA->Rect.Min.Y, 30.f, 0.01f);
		TestNearlyEqual(TEXT("A and B are the same height"), (float)GA->Rect.GetSize().Y, (float)GB->Rect.GetSize().Y, 0.01f);
		TestNearlyEqual(TEXT("B ends at the bottom"), (float)GB->Rect.Max.Y, 1080.f, 0.01f);
		TestNearlyEqual(TEXT("splitter fills the gap"), (float)C.Splitters[0].Rect.Min.Y, (float)GA->Rect.Max.Y, 0.01f);
		TestNearlyEqual(TEXT("tab bar height"), (float)GA->TabBarRect.GetSize().Y, M.TabBarHeight, 0.01f);
		TestNearlyEqual(TEXT("content starts under the tab bar"), (float)GA->ContentRect.Min.Y, (float)GA->TabBarRect.Max.Y, 0.01f);
		TestEqual(TEXT("one tab rect"), GA->TabRects.Num(), 1);
	}

	// A floating window dragged far off screen is pulled back so a strip of it stays reachable.
	L.FloatPanel(P(TEXT("F")), FVector2D(-5000.0, -5000.0), FVector2D(10000.0, 10000.0));
	L.ComputeLayout(HD, M, C);
	TestEqual(TEXT("one floating rect"), C.FloatingRects.Num(), 1);
	if (C.FloatingRects.Num() == 1)
	{
		TestTrue(TEXT("still reachable horizontally"), C.FloatingRects[0].Max.X >= M.FloatingMinVisible - 0.01f);
		TestTrue(TEXT("not above the usable area"), C.FloatingRects[0].Min.Y >= 30.0 - 0.01);
		TestTrue(TEXT("not larger than the screen"), C.FloatingRects[0].GetSize().X <= 1920.01 && C.FloatingRects[0].GetSize().Y <= 1050.01);
	}

	// A tiny viewport never produces negative or non-finite sizes.
	L.DockPanel(P(TEXT("D")), P(TEXT("A")), EDockDropZone::Right);
	L.DockPanel(P(TEXT("E")), P(TEXT("D")), EDockDropZone::Bottom);
	const FVector2D Tiny(120.0, 60.0);
	L.ComputeLayout(Tiny, M, C);
	for (const FDockComputedGroup& G : C.Groups)
	{
		TestTrue(TEXT("tiny viewport: group size is non-negative and finite"),
			G.Rect.GetSize().X >= -0.01 && G.Rect.GetSize().Y >= -0.01 && FMath::IsFinite(G.Rect.Min.X) && FMath::IsFinite(G.Rect.Max.Y));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockHitTestTest, "InNavi.Dock.HitTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockHitTestTest::RunTest(const FString& Parameters)
{
	FDockLayout L;
	L.DockPanelToEdge(P(TEXT("A")), EDockSide::Left);
	L.DockPanelToEdge(P(TEXT("B")), EDockSide::Right);

	FDockMetrics M;
	M.TopMargin = 30.f;
	FDockComputedLayout C;
	L.ComputeLayout(HD, M, C);

	auto Hit = [&](double X, double Y, const TCHAR* Dragged)
	{
		return L.HitTestDropTarget(FVector2D(X, Y), P(Dragged), HD, M, C);
	};

	FDockDropTarget T = Hit(140, 555, TEXT("B"));
	TestTrue(TEXT("middle of A's content is a Center drop"), T.IsValid() && T.Kind == FDockDropTarget::EKind::Group && T.Zone == EDockDropZone::Center && T.TargetPanel == P(TEXT("A")));

	T = Hit(10, 500, TEXT("B"));
	TestTrue(TEXT("near A's left edge splits to the Left"), T.Zone == EDockDropZone::Left);
	TestNearlyEqual(TEXT("left preview is the left half"), (float)T.PreviewRect.Max.X, 140.f, 0.01f);

	T = Hit(270, 500, TEXT("B"));
	TestTrue(TEXT("near A's right edge"), T.Zone == EDockDropZone::Right);
	T = Hit(140, 100, TEXT("B"));
	TestTrue(TEXT("near A's top"), T.Zone == EDockDropZone::Top);
	T = Hit(140, 1075, TEXT("B"));
	TestTrue(TEXT("near A's bottom"), T.Zone == EDockDropZone::Bottom);
	T = Hit(140, 40, TEXT("B"));
	TestTrue(TEXT("the tab bar is always Center"), T.Zone == EDockDropZone::Center);

	T = Hit(140, 555, TEXT("A"));
	TestFalse(TEXT("a lone panel never targets itself"), T.IsValid());

	T = Hit(1000, 40, TEXT("A"));
	TestTrue(TEXT("top screen edge"), T.Kind == FDockDropTarget::EKind::Edge && T.Side == EDockSide::Top);
	TestNearlyEqual(TEXT("top preview thickness"), (float)T.PreviewRect.GetSize().Y, 200.f, 0.01f);
	T = Hit(1000, 1075, TEXT("A"));
	TestTrue(TEXT("bottom screen edge"), T.Kind == FDockDropTarget::EKind::Edge && T.Side == EDockSide::Bottom);
	T = Hit(1000, 500, TEXT("A"));
	TestTrue(TEXT("the middle of the canvas floats the panel"), T.Kind == FDockDropTarget::EKind::Float);
	TestTrue(TEXT("floating preview is centred under the cursor"), FMath::IsNearlyEqual((float)T.PreviewRect.GetCenter().X, 1000.f, 0.5f));
	TestTrue(TEXT("floating preview stays on screen"), T.PreviewRect.Min.X >= 0.0 && T.PreviewRect.Max.X <= HD.X && T.PreviewRect.Min.Y >= 30.0 && T.PreviewRect.Max.Y <= HD.Y);
	T = Hit(1000, 1075 - 40, TEXT("A"));
	TestTrue(TEXT("just above the bottom guide still floats, clamped on screen"), T.Kind == FDockDropTarget::EKind::Float && T.PreviewRect.Max.Y <= HD.Y + 0.01);
	T = Hit(150, 500, TEXT("B"));
	TestFalse(TEXT("over a docked group is never a float drop"), T.Kind == FDockDropTarget::EKind::Float);
	T = Hit(5, 20, TEXT("A"));
	TestFalse(TEXT("above the menu bar is no target"), T.IsValid());

	// A floating window is hit before whatever is under it.
	L.FloatPanel(P(TEXT("C")), FVector2D(700.0, 300.0), FVector2D(400.0, 300.0));
	L.ComputeLayout(HD, M, C);
	T = Hit(900, 450, TEXT("A"));
	TestTrue(TEXT("floating window group is targeted"), T.Kind == FDockDropTarget::EKind::Group && T.TargetPanel == P(TEXT("C")));

	// Dragging a tab out of a multi-tab group may target that same group's sides, but not its Center.
	L.DockPanel(P(TEXT("D")), P(TEXT("A")), EDockDropZone::Center);
	L.ComputeLayout(HD, M, C);
	T = Hit(140, 555, TEXT("D"));
	TestFalse(TEXT("center of its own multi-tab group is a no-op"), T.IsValid());
	T = Hit(10, 500, TEXT("D"));
	TestTrue(TEXT("but its left side is allowed and targets the other tab"), T.Zone == EDockDropZone::Left && T.TargetPanel == P(TEXT("A")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockHideShowTest, "InNavi.Dock.HideShow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockHideShowTest::RunTest(const FString& Parameters)
{
	FDockLayout L;
	L.DockPanelToEdge(P(TEXT("A")), EDockSide::Left);
	L.DockPanel(P(TEXT("B")), P(TEXT("A")), EDockDropZone::Bottom);
	L.DockPanel(P(TEXT("C")), P(TEXT("B")), EDockDropZone::Bottom);
	TestEqual(TEXT("start"), DescribeArea(L, EDockSide::Left), FString(TEXT("V([A],[B],[C])")));

	TestTrue(TEXT("hide B"), L.HidePanel(P(TEXT("B"))));
	TestTrue(TEXT("B is hidden"), L.IsHidden(P(TEXT("B"))));
	TestFalse(TEXT("and not placed"), L.ContainsPanel(P(TEXT("B"))));
	TestTrue(TEXT("show B"), L.ShowPanel(P(TEXT("B")), EDockSide::Right));
	TestEqual(TEXT("B returns between A and C"), DescribeArea(L, EDockSide::Left), FString(TEXT("V([A],[B],[C])")));
	TestFalse(TEXT("no longer hidden"), L.IsHidden(P(TEXT("B"))));
	TestFalse(TEXT("showing a visible panel"), L.ShowPanel(P(TEXT("B")), EDockSide::Right));

	// A tab returns to the same group.
	L.DockPanel(P(TEXT("D")), P(TEXT("A")), EDockDropZone::Center);
	L.HidePanel(P(TEXT("D")));
	L.ShowPanel(P(TEXT("D")), EDockSide::Right);
	FDockLocation LD, LA;
	L.FindPanel(P(TEXT("D")), LD);
	L.FindPanel(P(TEXT("A")), LA);
	TestTrue(TEXT("D is back in A's group"), LD.Group == LA.Group);

	// The only panel of an area comes back to that edge with the area's old thickness.
	L.HidePanel(P(TEXT("D")));
	L.HidePanel(P(TEXT("A")));
	L.HidePanel(P(TEXT("B")));
	L.SetAreaExtent(EDockSide::Left, 333.f);
	L.HidePanel(P(TEXT("C")));
	TestFalse(TEXT("area is empty"), L.GetAreaRoot(EDockSide::Left).IsValid());
	TestTrue(TEXT("show C"), L.ShowPanel(P(TEXT("C")), EDockSide::Right));
	TestEqual(TEXT("back on the left"), DescribeArea(L, EDockSide::Left), FString(TEXT("[C]")));
	TestNearlyEqual(TEXT("its old thickness is restored"), L.GetAreaExtent(EDockSide::Left), 333.f, 0.01f);

	// A floating panel returns to its old window rect.
	L.FloatPanel(P(TEXT("F")), FVector2D(100.0, 150.0), FVector2D(300.0, 400.0));
	L.HidePanel(P(TEXT("F")));
	TestEqual(TEXT("no windows"), L.GetFloating().Num(), 0);
	L.ShowPanel(P(TEXT("F")), EDockSide::Right);
	TestEqual(TEXT("window is back"), L.GetFloating().Num(), 1);
	if (L.GetFloating().Num() == 1)
	{
		TestTrue(TEXT("position"), L.GetFloating()[0].Position.Equals(FVector2D(100.0, 150.0), 0.01));
		TestTrue(TEXT("size"), L.GetFloating()[0].Size.Equals(FVector2D(300.0, 400.0), 0.01));
	}

	// Two tabs hidden one after the other come back together in either order.
	for (int32 Order = 0; Order < 2; ++Order)
	{
		FDockLayout T;
		T.DockPanelToEdge(P(TEXT("X")), EDockSide::Bottom);
		T.DockPanel(P(TEXT("Y")), P(TEXT("X")), EDockDropZone::Center);
		T.HidePanel(P(TEXT("Y")));
		T.HidePanel(P(TEXT("X")));
		const TCHAR* First = Order == 0 ? TEXT("X") : TEXT("Y");
		const TCHAR* Second = Order == 0 ? TEXT("Y") : TEXT("X");
		T.ShowPanel(P(First), EDockSide::Right);
		T.ShowPanel(P(Second), EDockSide::Right);
		FDockLocation LX, LY;
		T.FindPanel(P(TEXT("X")), LX);
		T.FindPanel(P(TEXT("Y")), LY);
		TestTrue(*FString::Printf(TEXT("order %d: X and Y share a group"), Order), LX.Group != nullptr && LX.Group == LY.Group);
		CheckValid(*this, T, TEXT("hide/show tabs"));
	}

	TestTrue(TEXT("a never-seen panel docks to the fallback edge"), L.ShowPanel(P(TEXT("Z")), EDockSide::Bottom));
	TestEqual(TEXT("bottom"), DescribeArea(L, EDockSide::Bottom), FString(TEXT("[Z]")));
	CheckValid(*this, L, TEXT("hide/show"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockFloatingTest, "InNavi.Dock.Floating", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockFloatingTest::RunTest(const FString& Parameters)
{
	FDockLayout L;
	L.DockPanelToEdge(P(TEXT("A")), EDockSide::Left);
	L.DockPanelToEdge(P(TEXT("B")), EDockSide::Right);

	TestTrue(TEXT("float A"), L.FloatPanel(P(TEXT("A")), FVector2D(200.0, 200.0), FVector2D(300.0, 300.0)));
	TestFalse(TEXT("left area vanished"), L.GetAreaRoot(EDockSide::Left).IsValid());
	TestEqual(TEXT("one window"), L.GetFloating().Num(), 1);

	TestTrue(TEXT("floating an already-floating lone panel only moves it"), L.FloatPanel(P(TEXT("A")), FVector2D(50.0, 60.0), FVector2D(350.0, 320.0)));
	TestEqual(TEXT("still one window"), L.GetFloating().Num(), 1);
	TestTrue(TEXT("moved"), L.GetFloating()[0].Position.Equals(FVector2D(50.0, 60.0), 0.01));

	TestTrue(TEXT("tab B into the floating window"), L.DockPanel(P(TEXT("B")), P(TEXT("A")), EDockDropZone::Center));
	TestEqual(TEXT("window holds [A,B]"), DescribeNode(L.GetFloating()[0].Root.Get()), FString(TEXT("[A,B]")));
	TestFalse(TEXT("right area vanished"), L.GetAreaRoot(EDockSide::Right).IsValid());

	L.FloatPanel(P(TEXT("C")), FVector2D(400.0, 100.0), FVector2D(300.0, 300.0));
	TestEqual(TEXT("two windows"), L.GetFloating().Num(), 2);
	TestEqual(TEXT("raising the first window"), L.BringFloatingToFront(0), 1);
	TestEqual(TEXT("it is now last"), DescribeNode(L.GetFloating()[1].Root.Get()), FString(TEXT("[A,B]")));

	TestTrue(TEXT("move"), L.MoveFloating(0, FVector2D(10.0, 10.0)));
	TestTrue(TEXT("resize"), L.ResizeFloating(0, FVector2D(10.0, 10.0)));
	TestTrue(TEXT("a minimum size is enforced"), L.GetFloating()[0].Size.X >= 50.0 && L.GetFloating()[0].Size.Y >= 50.0);
	TestFalse(TEXT("bad index"), L.MoveFloating(7, FVector2D::ZeroVector));

	TestTrue(TEXT("dock the floating tab group member back to an edge"), L.DockPanelToEdge(P(TEXT("B")), EDockSide::Top));
	TestEqual(TEXT("A alone in its window"), DescribeNode(L.GetFloating()[1].Root.Get()), FString(TEXT("[A]")));
	CheckValid(*this, L, TEXT("floating"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockJsonRoundTripTest, "InNavi.Dock.JsonRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockJsonRoundTripTest::RunTest(const FString& Parameters)
{
	const TArray<FName> All = { P(TEXT("A")), P(TEXT("B")), P(TEXT("C")), P(TEXT("D")), P(TEXT("E")), P(TEXT("F")), P(TEXT("G")), P(TEXT("H")) };
	const TSet<FName> Known = KnownSet(All);

	FDockLayout L;
	L.DockPanelToEdge(P(TEXT("A")), EDockSide::Left);
	L.DockPanel(P(TEXT("B")), P(TEXT("A")), EDockDropZone::Bottom);
	L.DockPanel(P(TEXT("C")), P(TEXT("A")), EDockDropZone::Center);
	L.DockPanelToEdge(P(TEXT("D")), EDockSide::Right);
	L.FloatPanel(P(TEXT("E")), FVector2D(600.0, 200.0), FVector2D(400.0, 300.0));
	L.DockPanelToEdge(P(TEXT("F")), EDockSide::Top);
	L.DockPanel(P(TEXT("G")), P(TEXT("D")), EDockDropZone::Right);
	L.HidePanel(P(TEXT("G")));
	L.FloatPanel(P(TEXT("H")), FVector2D(100.0, 100.0), FVector2D(250.0, 250.0));
	L.HidePanel(P(TEXT("H")));
	L.SetActiveTab(P(TEXT("A")));
	CheckValid(*this, L, TEXT("source layout"));

	const FString Text = JsonToString(L.ToJson(HD));
	FDockLayout R;
	TArray<FName> Missing;
	TestTrue(TEXT("round trip through text"), R.FromJson(StringToJson(Text), HD, Known, Missing));
	TestEqual(TEXT("nothing missing"), Missing.Num(), 0);
	CheckValid(*this, R, TEXT("restored"));
	FString Why;
	TestTrue(*FString::Printf(TEXT("restored layout matches: %s"), *Why), LayoutsEquivalent(L, R, All, Why));
	if (!Why.IsEmpty())
	{
		AddError(Why);
	}

	// Same fractions on a different resolution.
	FDockLayout Small;
	TestTrue(TEXT("load at another resolution"), Small.FromJson(StringToJson(Text), FVector2D(1280.0, 720.0), Known, Missing));
	TestNearlyEqual(TEXT("left extent scales with the width"), Small.GetAreaExtent(EDockSide::Left), L.GetAreaExtent(EDockSide::Left) * 1280.f / 1920.f, 0.1f);
	TestNearlyEqual(TEXT("top extent scales with the height"), Small.GetAreaExtent(EDockSide::Top), L.GetAreaExtent(EDockSide::Top) * 720.f / 1080.f, 0.1f);

	// Version mismatch leaves the target untouched.
	TSharedPtr<FJsonObject> Old = StringToJson(Text);
	Old->SetNumberField(TEXT("layoutVersion"), 2);
	FDockLayout Untouched;
	Untouched.DockPanelToEdge(P(TEXT("A")), EDockSide::Bottom);
	TestFalse(TEXT("old version is rejected"), Untouched.FromJson(Old, HD, Known, Missing));
	TestEqual(TEXT("and the layout is unchanged"), DescribeArea(Untouched, EDockSide::Bottom), FString(TEXT("[A]")));
	TestFalse(TEXT("null json"), Untouched.FromJson(nullptr, HD, Known, Missing));
	TestFalse(TEXT("missing areas"), Untouched.FromJson(StringToJson(TEXT("{\"layoutVersion\":3}")), HD, Known, Missing));

	// Unknown panels are dropped, known-but-absent ones reported.
	TSet<FName> Narrow = KnownSet({ P(TEXT("A")), P(TEXT("Q")) });
	FDockLayout N;
	TestTrue(TEXT("load with a smaller known set"), N.FromJson(StringToJson(Text), HD, Narrow, Missing));
	TArray<FName> Placed = N.GetAllPanels();
	TestEqual(TEXT("only A survives"), Placed.Num(), 1);
	TestTrue(TEXT("Q is reported missing"), Missing.Contains(P(TEXT("Q"))) && Missing.Num() == 1);
	CheckValid(*this, N, TEXT("narrow"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockJsonRepairTest, "InNavi.Dock.JsonRepair", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockJsonRepairTest::RunTest(const FString& Parameters)
{
	const TSet<FName> Known = KnownSet({ P(TEXT("A")), P(TEXT("B")), P(TEXT("C")) });
	TArray<FName> Missing;

	{
		FDockLayout L;
		TSharedPtr<FJsonObject> Root = JsonSplit(TEXT("horizontal"), { JsonTabs({ TEXT("A") }), JsonTabs({ TEXT("B") }) }, { 0.0, -3.0 });
		TestTrue(TEXT("zero/negative ratios load"), L.FromJson(JsonLayoutWithLeftRoot(Root), HD, Known, Missing));
		const FDockNode* R = L.GetAreaRoot(EDockSide::Left).Get();
		TestTrue(TEXT("split survives"), R && R->IsSplit());
		if (R && R->IsSplit())
		{
			TestNearlyEqual(TEXT("repaired to equal shares"), R->Ratios[0], 0.5f, 1e-3f);
		}
		CheckValid(*this, L, TEXT("bad ratios"));
	}
	{
		FDockLayout L;
		TSharedPtr<FJsonObject> Root = JsonSplit(TEXT("vertical"), { JsonTabs({ TEXT("A") }) }, { 1.0 });
		TestTrue(TEXT("single-child split loads"), L.FromJson(JsonLayoutWithLeftRoot(Root), HD, Known, Missing));
		TestEqual(TEXT("collapsed to its child"), DescribeArea(L, EDockSide::Left), FString(TEXT("[A]")));
	}
	{
		FDockLayout L;
		TSharedPtr<FJsonObject> Inner = JsonSplit(TEXT("horizontal"), { JsonTabs({ TEXT("B") }), JsonTabs({ TEXT("C") }) }, { 0.5, 0.5 });
		TSharedPtr<FJsonObject> Root = JsonSplit(TEXT("horizontal"), { JsonTabs({ TEXT("A") }), Inner }, { 0.5, 0.5 });
		TestTrue(TEXT("same-orientation nesting loads"), L.FromJson(JsonLayoutWithLeftRoot(Root), HD, Known, Missing));
		TestEqual(TEXT("flattened"), DescribeArea(L, EDockSide::Left), FString(TEXT("H([A],[B],[C])")));
		CheckValid(*this, L, TEXT("nesting"));
	}
	{
		FDockLayout L;
		TSharedPtr<FJsonObject> Root = JsonSplit(TEXT("vertical"), { JsonTabs({ TEXT("A"), TEXT("Nope") }, 1), JsonTabs({ TEXT("A"), TEXT("B") }, 9) }, { 0.5, 0.5 });
		TestTrue(TEXT("duplicates and unknown panels load"), L.FromJson(JsonLayoutWithLeftRoot(Root), HD, Known, Missing));
		TestEqual(TEXT("first A wins, unknown dropped"), DescribeArea(L, EDockSide::Left), FString(TEXT("V([A],[B])")));
		CheckValid(*this, L, TEXT("duplicates"));
		TestTrue(TEXT("C is reported missing"), Missing.Contains(P(TEXT("C"))));
	}
	{
		FDockLayout L;
		TSharedPtr<FJsonObject> Bogus = MakeShared<FJsonObject>();
		Bogus->SetStringField(TEXT("type"), TEXT("bogus"));
		TestFalse(TEXT("unknown node type is rejected"), L.FromJson(JsonLayoutWithLeftRoot(Bogus), HD, Known, Missing));
		TSharedPtr<FJsonObject> BadOrientation = JsonSplit(TEXT("diagonal"), { JsonTabs({ TEXT("A") }), JsonTabs({ TEXT("B") }) }, { 0.5, 0.5 });
		TestFalse(TEXT("unknown orientation is rejected"), L.FromJson(JsonLayoutWithLeftRoot(BadOrientation), HD, Known, Missing));
	}
	{
		// A pathologically deep tree is refused instead of recursing forever.
		FDockLayout L;
		TSharedPtr<FJsonObject> Node = JsonTabs({ TEXT("A") });
		for (int32 i = 0; i < 40; ++i)
		{
			Node = JsonSplit(i % 2 == 0 ? TEXT("horizontal") : TEXT("vertical"), { Node, JsonTabs({ TEXT("B") }) }, { 0.5, 0.5 });
		}
		TestFalse(TEXT("depth bomb is rejected"), L.FromJson(JsonLayoutWithLeftRoot(Node), HD, Known, Missing));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockFuzzInvariantsTest, "InNavi.Dock.FuzzInvariants", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockFuzzInvariantsTest::RunTest(const FString& Parameters)
{
	TArray<FName> All;
	for (int32 i = 0; i < 8; ++i)
	{
		All.Add(FName(*FString::Printf(TEXT("P%d"), i)));
	}
	const TSet<FName> Known = KnownSet(All);
	const FVector2D Viewports[] = { FVector2D(1920.0, 1080.0), FVector2D(1280.0, 720.0), FVector2D(800.0, 600.0), FVector2D(300.0, 200.0) };

	FRandomStream Rand(20261019);
	FDockLayout L;
	for (int32 i = 0; i < All.Num(); ++i)
	{
		L.DockPanelToEdge(All[i], (EDockSide)(i % DockSideCount));
	}

	FDockMetrics M;
	M.TopMargin = 30.f;

	auto RandomPanel = [&]() { return All[Rand.RandRange(0, All.Num() - 1)]; };

	for (int32 Step = 0; Step < 3000; ++Step)
	{
		const int32 Op = Rand.RandRange(0, 9);
		FString OpName;
		switch (Op)
		{
		case 0:
		case 1:
			OpName = TEXT("DockPanel");
			L.DockPanel(RandomPanel(), RandomPanel(), (EDockDropZone)Rand.RandRange(1, 5));
			break;
		case 2:
			OpName = TEXT("DockPanelToEdge");
			L.DockPanelToEdge(RandomPanel(), (EDockSide)Rand.RandRange(0, 3));
			break;
		case 3:
			OpName = TEXT("FloatPanel");
			L.FloatPanel(RandomPanel(), FVector2D(Rand.FRandRange(-500.f, 1800.f), Rand.FRandRange(-100.f, 900.f)), FVector2D(Rand.FRandRange(60.f, 900.f), Rand.FRandRange(60.f, 900.f)));
			break;
		case 4:
			OpName = TEXT("HidePanel");
			L.HidePanel(RandomPanel());
			break;
		case 5:
			OpName = TEXT("ShowPanel");
			L.ShowPanel(RandomPanel(), (EDockSide)Rand.RandRange(0, 3));
			break;
		case 6:
			OpName = TEXT("SetActiveTab");
			L.SetActiveTab(RandomPanel());
			break;
		case 7:
			OpName = TEXT("MoveTab");
			L.MoveTab(RandomPanel(), Rand.RandRange(-1, 4));
			break;
		case 8:
		{
			OpName = TEXT("ResizeSplit");
			FDockComputedLayout Tmp;
			L.ComputeLayout(Viewports[0], M, Tmp);
			if (Tmp.Splitters.Num() > 0)
			{
				const FDockComputedSplitter& S = Tmp.Splitters[Rand.RandRange(0, Tmp.Splitters.Num() - 1)];
				L.ResizeSplit(const_cast<FDockNode*>(S.SplitNode), S.DividerIndex, Rand.FRandRange(-600.f, 600.f), S.AvailablePx, 100.f);
			}
			break;
		}
		default:
			OpName = TEXT("SetAreaExtent");
			L.SetAreaExtent((EDockSide)Rand.RandRange(0, 3), Rand.FRandRange(50.f, 1500.f));
			break;
		}

		const FString Where = FString::Printf(TEXT("step %d (%s)"), Step, *OpName);
		if (!CheckValid(*this, L, *Where))
		{
			return false;
		}

		// Every panel is in exactly one of: placed, hidden.
		for (const FName Panel : All)
		{
			const bool bPlaced = L.ContainsPanel(Panel);
			const bool bHidden = L.IsHidden(Panel);
			if (bPlaced == bHidden)
			{
				AddError(FString::Printf(TEXT("[%s] panel %s is %s"), *Where, *Panel.ToString(), bPlaced ? TEXT("both placed and hidden") : TEXT("neither placed nor hidden")));
				return false;
			}
		}

		// Geometry sanity.
		const FVector2D Viewport = Viewports[Step % UE_ARRAY_COUNT(Viewports)];
		FDockComputedLayout C;
		L.ComputeLayout(Viewport, M, C);
		auto BoxOk = [](const FBox2D& B)
		{
			return FMath::IsFinite(B.Min.X) && FMath::IsFinite(B.Min.Y) && FMath::IsFinite(B.Max.X) && FMath::IsFinite(B.Max.Y)
				&& B.Max.X >= B.Min.X - 0.01 && B.Max.Y >= B.Min.Y - 0.01;
		};
		auto Inside = [](const FBox2D& Inner, const FBox2D& Outer)
		{
			return Inner.Min.X >= Outer.Min.X - 0.05 && Inner.Min.Y >= Outer.Min.Y - 0.05 && Inner.Max.X <= Outer.Max.X + 0.05 && Inner.Max.Y <= Outer.Max.Y + 0.05;
		};
		const FBox2D Screen(FVector2D(0.0, 0.0), Viewport);
		for (int32 s = 0; s < DockSideCount; ++s)
		{
			if (C.bAreaVisible[s] && (!BoxOk(C.AreaRects[s]) || !Inside(C.AreaRects[s], Screen)))
			{
				AddError(FString::Printf(TEXT("[%s] area %d rect is invalid or off screen"), *Where, s));
				return false;
			}
		}
		for (const FDockComputedGroup& G : C.Groups)
		{
			const FBox2D& Parent = G.FloatingIndex == INDEX_NONE ? C.AreaRects[(int32)G.Side] : C.FloatingRects[G.FloatingIndex];
			if (!BoxOk(G.Rect) || !BoxOk(G.TabBarRect) || !BoxOk(G.ContentRect) || !Inside(G.Rect, Parent) || !Inside(G.ContentRect, G.Rect))
			{
				AddError(FString::Printf(TEXT("[%s] a group rect is invalid or outside its container"), *Where));
				return false;
			}
			for (const FBox2D& Tab : G.TabRects)
			{
				if (!BoxOk(Tab) || !Inside(Tab, G.TabBarRect))
				{
					AddError(FString::Printf(TEXT("[%s] a tab rect is outside its tab bar"), *Where));
					return false;
				}
			}
		}
		for (const FDockComputedSplitter& S : C.Splitters)
		{
			if (!BoxOk(S.Rect))
			{
				AddError(FString::Printf(TEXT("[%s] a splitter rect is invalid"), *Where));
				return false;
			}
		}

		// Persistence and cloning must reproduce the same layout.
		if (Step % 25 == 0)
		{
			FDockLayout Restored;
			TArray<FName> Missing;
			if (!Restored.FromJson(StringToJson(JsonToString(L.ToJson(HD))), HD, Known, Missing))
			{
				AddError(FString::Printf(TEXT("[%s] FromJson rejected our own output"), *Where));
				return false;
			}
			FString Why;
			if (!LayoutsEquivalent(L, Restored, All, Why))
			{
				AddError(FString::Printf(TEXT("[%s] JSON round trip changed the layout: %s"), *Where, *Why));
				return false;
			}
			if (Missing.Num() != 0)
			{
				AddError(FString::Printf(TEXT("[%s] round trip reported %d missing panels"), *Where, Missing.Num()));
				return false;
			}
			const TSharedRef<FDockLayout> Copy = L.Clone();
			if (!LayoutsEquivalent(L, *Copy, All, Why))
			{
				AddError(FString::Printf(TEXT("[%s] Clone differs: %s"), *Where, *Why));
				return false;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockFloatingWindowTest, "InNavi.Dock.FloatingWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockFloatingWindowTest::RunTest(const FString& Parameters)
{
	FDockLayout L;
	FDockMetrics M;
	M.TopMargin = 30.f;
	FDockComputedLayout C;

	// A floating window has a drag strip on top; its tab group sits below the strip.
	L.FloatPanel(P(TEXT("F")), FVector2D(100.0, 150.0), FVector2D(300.0, 400.0));
	L.ComputeLayout(HD, M, C);
	if (!TestEqual(TEXT("one window"), C.FloatingRects.Num(), 1) || !TestEqual(TEXT("one grip"), C.FloatingGripRects.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("window rect"), C.FloatingRects[0].Min.Equals(FVector2D(100.0, 150.0), 0.01) && C.FloatingRects[0].Max.Equals(FVector2D(400.0, 550.0), 0.01));
	TestTrue(TEXT("grip is the top strip"), C.FloatingGripRects[0].Min.Equals(FVector2D(100.0, 150.0), 0.01) && C.FloatingGripRects[0].Max.Equals(FVector2D(400.0, 150.0 + M.FloatingGripHeight), 0.01));
	if (TestEqual(TEXT("one group"), C.Groups.Num(), 1))
	{
		TestNearlyEqual(TEXT("group starts below the strip"), (float)C.Groups[0].Rect.Min.Y, 150.f + M.FloatingGripHeight, 0.01f);
		TestNearlyEqual(TEXT("group reaches the bottom of the window"), (float)C.Groups[0].Rect.Max.Y, 550.f, 0.01f);
	}

	// Over empty canvas a drop floats; the new window's strip is under the cursor and it stays on screen.
	L.DockPanelToEdge(P(TEXT("A")), EDockSide::Left);
	L.ComputeLayout(HD, M, C);
	FDockDropTarget T = L.HitTestDropTarget(FVector2D(1000.0, 500.0), P(TEXT("A")), HD, M, C, FVector2D(300.0, 400.0));
	TestTrue(TEXT("empty canvas floats"), T.Kind == FDockDropTarget::EKind::Float);
	TestTrue(TEXT("requested size"), T.FloatSize.Equals(FVector2D(300.0, 400.0), 0.01));
	TestNearlyEqual(TEXT("strip centred under the cursor (x)"), (float)(T.FloatPosition.X + T.FloatSize.X * 0.5), 1000.f, 0.01f);
	TestTrue(TEXT("cursor is inside the strip"), T.FloatPosition.Y <= 500.0 && T.FloatPosition.Y + M.FloatingGripHeight >= 500.0);

	T = L.HitTestDropTarget(FVector2D(1900.0, 1000.0), P(TEXT("A")), HD, M, C, FVector2D(300.0, 400.0));
	if (T.Kind == FDockDropTarget::EKind::Float)
	{
		TestTrue(TEXT("near a corner the window is pulled back on screen"), T.PreviewRect.Max.X <= HD.X + 0.01 && T.PreviewRect.Max.Y <= HD.Y + 0.01 && T.PreviewRect.Min.X >= -0.01);
	}
	T = L.HitTestDropTarget(FVector2D(150.0, 160.0), P(TEXT("A")), HD, M, C, FVector2D(300.0, 400.0));
	TestTrue(TEXT("over a floating window's drag strip the drop adds a tab to it"), T.Kind == FDockDropTarget::EKind::Group && T.Zone == EDockDropZone::Center && T.TargetPanel == P(TEXT("F")));
	T = L.HitTestDropTarget(FVector2D(100.0, 600.0), P(TEXT("F")), HD, M, C);
	TestFalse(TEXT("over a docked area's own rect (not a group) nothing floats"), T.Kind == FDockDropTarget::EKind::Float);

	// A tiny request is raised to the minimum window size.
	T = L.HitTestDropTarget(FVector2D(1000.0, 500.0), P(TEXT("A")), HD, M, C, FVector2D(10.0, 10.0));
	TestTrue(TEXT("minimum window size"), T.Kind == FDockDropTarget::EKind::Float && T.FloatSize.Equals(M.FloatingMinSize, 0.01));

	// A lone panel's own window doesn't hide what is under it; another window's strip drops as a tab.
	{
		FDockLayout L2;
		L2.DockPanelToEdge(P(TEXT("X")), EDockSide::Left);
		L2.FloatPanel(P(TEXT("W1")), FVector2D(300.0, 200.0), FVector2D(300.0, 300.0));
		L2.FloatPanel(P(TEXT("W2")), FVector2D(450.0, 250.0), FVector2D(300.0, 300.0)); // on top of W1
		FDockComputedLayout C2;
		L2.ComputeLayout(HD, M, C2);
		// Over W2's strip: a Center drop onto W2.
		FDockDropTarget T2 = L2.HitTestDropTarget(FVector2D(600.0, 255.0), P(TEXT("X")), HD, M, C2);
		TestTrue(TEXT("another window's strip adds a tab"), T2.Kind == FDockDropTarget::EKind::Group && T2.Zone == EDockDropZone::Center && T2.TargetPanel == P(TEXT("W2")));
		// Dragging W2 itself over W1 (W2's own window is transparent to the drop): targets W1 beneath.
		T2 = L2.HitTestDropTarget(FVector2D(350.0, 400.0), P(TEXT("W2")), HD, M, C2);
		TestTrue(TEXT("W1 is targeted"), T2.Kind == FDockDropTarget::EKind::Group && T2.TargetPanel == P(TEXT("W1")));
		T2 = L2.HitTestDropTarget(FVector2D(500.0, 300.0), P(TEXT("W2")), HD, M, C2);
		TestTrue(TEXT("through its own window onto W1's body"), T2.Kind == FDockDropTarget::EKind::Group && T2.TargetPanel == P(TEXT("W1")));
		T2 = L2.HitTestDropTarget(FVector2D(700.0, 520.0), P(TEXT("W2")), HD, M, C2);
		TestTrue(TEXT("over only its own window it floats instead"), T2.Kind == FDockDropTarget::EKind::Float);
	}

	// Applying the target: A leaves the left edge and the area disappears.
	T = L.HitTestDropTarget(FVector2D(1000.0, 500.0), P(TEXT("A")), HD, M, C, FVector2D(300.0, 400.0));
	TestTrue(TEXT("float A"), L.FloatPanel(P(TEXT("A")), T.FloatPosition, T.FloatSize));
	TestFalse(TEXT("left area is gone"), L.GetAreaRoot(EDockSide::Left).IsValid());
	TestEqual(TEXT("two windows"), L.GetFloating().Num(), 2);
	TestTrue(TEXT("the new window is on top"), L.GetFloating().Last().Root->Tabs.Contains(P(TEXT("A"))));

	// Move / resize / raise.
	TestTrue(TEXT("move"), L.MoveFloating(0, FVector2D(10.0, 20.0)));
	TestTrue(TEXT("resize"), L.ResizeFloating(0, FVector2D(333.0, 222.0)));
	TestTrue(TEXT("window 0 moved and resized"), L.GetFloating()[0].Position.Equals(FVector2D(10.0, 20.0), 0.01) && L.GetFloating()[0].Size.Equals(FVector2D(333.0, 222.0), 0.01));
	const int32 NewIndex = L.BringFloatingToFront(0);
	TestEqual(TEXT("raised to the top"), NewIndex, 1);
	TestTrue(TEXT("and it is the F window"), L.GetFloating()[1].Root->Tabs.Contains(P(TEXT("F"))));

	CheckValid(*this, L, TEXT("floating"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
