#if WITH_DEV_AUTOMATION_TESTS

#include "DockHostWidget.h"

#include "Input/Events.h"
#include "Layout/ArrangedChildren.h"
#include "Misc/AutomationTest.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FName HP(const TCHAR* Name) { return FName(Name); }

	FPointerEvent MakeMouse(float X, float Y, bool bLeftDown = true)
	{
		TSet<FKey> Pressed;
		if (bLeftDown)
		{
			Pressed.Add(EKeys::LeftMouseButton);
		}
		return FPointerEvent(0, FVector2f(X, Y), FVector2f(X, Y), Pressed, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
	}

	TSharedRef<SDockHost> MakeHost(const TSharedRef<FDockLayout>& Layout, const TArray<FName>& Panels)
	{
		TSharedRef<SDockHost> Host = SNew(SDockHost).Layout(Layout);
		for (const FName Panel : Panels)
		{
			Host->RegisterPanel(Panel, FText::FromName(Panel), SNew(STextBlock).Text(FText::FromName(Panel)));
		}
		return Host;
	}

	const FGeometry RootGeometry = FGeometry::MakeRoot(FVector2f(1920.f, 1080.f), FSlateLayoutTransform());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockHostArrangeTest, "InNavi.Dock.Host.Arrange", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockHostArrangeTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FDockLayout> L = MakeShared<FDockLayout>();
	L->DockPanelToEdge(HP(TEXT("A")), EDockSide::Left);
	L->DockPanel(HP(TEXT("B")), HP(TEXT("A")), EDockDropZone::Bottom);
	L->DockPanelToEdge(HP(TEXT("C")), EDockSide::Right);
	L->DockPanel(HP(TEXT("D")), HP(TEXT("C")), EDockDropZone::Center);

	const TSharedRef<SDockHost> Host = MakeHost(L, { HP(TEXT("A")), HP(TEXT("B")), HP(TEXT("C")), HP(TEXT("D")) });

	FArrangedChildren Arranged(EVisibility::Visible);
	Host->ArrangeChildren(RootGeometry, Arranged);
	// groups A, B and the C/D group, one splitter between A and B, and the two edge handles
	TestEqual(TEXT("arranged children"), Arranged.Num(), 6);
	if (Arranged.Num() != 6)
	{
		return false;
	}

	const FDockComputedLayout& C = Host->GetComputedLayout();
	TestEqual(TEXT("three groups"), C.Groups.Num(), 3);
	TestEqual(TEXT("one splitter"), C.Splitters.Num(), 1);
	if (C.Groups.Num() == 3 && C.Splitters.Num() == 1)
	{
		auto Matches = [&](int32 Index, const FBox2D& Rect, const TCHAR* What)
		{
			const FGeometry& G = Arranged[Index].Geometry;
			const FVector2f Pos = G.GetAbsolutePosition();
			const FVector2f Size = G.GetLocalSize();
			TestNearlyEqual(*FString::Printf(TEXT("%s x"), What), Pos.X, (float)Rect.Min.X, 0.01f);
			TestNearlyEqual(*FString::Printf(TEXT("%s y"), What), Pos.Y, (float)Rect.Min.Y, 0.01f);
			TestNearlyEqual(*FString::Printf(TEXT("%s width"), What), Size.X, (float)Rect.GetSize().X, 0.01f);
			TestNearlyEqual(*FString::Printf(TEXT("%s height"), What), Size.Y, (float)Rect.GetSize().Y, 0.01f);
		};
		Matches(0, C.Groups[0].Rect, TEXT("group A"));
		Matches(1, C.Groups[1].Rect, TEXT("group B"));
		Matches(2, C.Splitters[0].Rect, TEXT("splitter"));
		Matches(3, C.Groups[2].Rect, TEXT("group C/D"));
	}

	// The host never grabs clicks itself: only its chrome and content are hit-testable.
	TestEqual(TEXT("host is self-hit-test-invisible"), Host->GetVisibility(), EVisibility::SelfHitTestInvisible);

	// Tab switching and closing go through the model and rebuild the children.
	Host->HandleTabActivated(HP(TEXT("D")));
	FDockLocation Loc;
	L->FindPanel(HP(TEXT("D")), Loc);
	TestEqual(TEXT("D is active"), Loc.Group->Tabs[Loc.Group->ActiveTab], HP(TEXT("D")));

	Host->HandleTabClosed(HP(TEXT("D")));
	TestTrue(TEXT("D is hidden after its tab was closed"), L->IsHidden(HP(TEXT("D"))));
	Arranged.Empty();
	Host->ArrangeChildren(RootGeometry, Arranged);
	TestEqual(TEXT("C still has a group"), Arranged.Num(), 6);

	Host->HandleTabClosed(HP(TEXT("C")));
	Arranged.Empty();
	Host->ArrangeChildren(RootGeometry, Arranged);
	TestEqual(TEXT("right area is gone: A, B, splitter, left edge"), Arranged.Num(), 4);

	// A panel moving between existing groups must not end up with two parents.
	L->ShowPanel(HP(TEXT("C")), EDockSide::Right);
	L->DockPanel(HP(TEXT("A")), HP(TEXT("C")), EDockDropZone::Center);
	Host->NotifyLayoutChanged();
	Arranged.Empty();
	Host->ArrangeChildren(RootGeometry, Arranged);
	FString Err;
	TestTrue(TEXT("layout stays valid"), L->Validate(Err));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockHostDragTest, "InNavi.Dock.Host.Drag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockHostDragTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FDockLayout> L = MakeShared<FDockLayout>();
	L->DockPanelToEdge(HP(TEXT("A")), EDockSide::Left);
	L->DockPanel(HP(TEXT("B")), HP(TEXT("A")), EDockDropZone::Bottom);
	L->DockPanelToEdge(HP(TEXT("C")), EDockSide::Right);
	const TSharedRef<SDockHost> Host = MakeHost(L, { HP(TEXT("A")), HP(TEXT("B")), HP(TEXT("C")) });

	int32 Changed = 0;
	// (OnLayoutChanged is bound at construction in real use; here we just observe the model.)
	FArrangedChildren Arranged(EVisibility::Visible);
	Host->ArrangeChildren(RootGeometry, Arranged);
	const FDockComputedLayout& C = Host->GetComputedLayout();
	if (!TestEqual(TEXT("one splitter"), C.Splitters.Num(), 1))
	{
		return false;
	}

	// --- splitter between A and B (vertical split, 1080 - 6 px shared, 537 each) ---
	const FDockNode* Split = C.Splitters[0].SplitNode;
	TestNearlyEqual(TEXT("available length"), C.Splitters[0].AvailablePx, 1074.f, 0.01f);
	Host->BeginSplitterDrag(Split, 0, MakeMouse(100.f, 540.f));
	TestTrue(TEXT("move"), Host->OnMouseMove(RootGeometry, MakeMouse(100.f, 640.f)).IsEventHandled());
	TestNearlyEqual(TEXT("top child grew by the cursor delta"), Split->Ratios[0], (537.f + 100.f) / 1074.f, 1e-3f);
	TestNearlyEqual(TEXT("ratios sum to 1"), Split->Ratios[0] + Split->Ratios[1], 1.f, 1e-5f);

	Host->OnMouseMove(RootGeometry, MakeMouse(100.f, 5000.f));
	TestNearlyEqual(TEXT("bottom child stops at its minimum height"), Split->Ratios[1] * 1074.f, 80.f, 0.05f);

	Host->OnMouseMove(RootGeometry, MakeMouse(100.f, 560.f));
	TestNearlyEqual(TEXT("dragging back follows the cursor (no drift after clamping)"), Split->Ratios[0], (537.f + 20.f) / 1074.f, 1e-3f);

	TestTrue(TEXT("release"), Host->OnMouseButtonUp(RootGeometry, MakeMouse(100.f, 560.f, false)).IsEventHandled());
	TestFalse(TEXT("moves after release are ignored"), Host->OnMouseMove(RootGeometry, MakeMouse(100.f, 900.f)).IsEventHandled());
	TestNearlyEqual(TEXT("ratio unchanged after release"), Split->Ratios[0], (537.f + 20.f) / 1074.f, 1e-3f);

	// --- left edge area: dragging its inner edge right grows it ---
	const float StartExtent = L->GetAreaExtent(EDockSide::Left);
	Host->BeginAreaEdgeDrag(EDockSide::Left, MakeMouse(StartExtent, 500.f));
	Host->OnMouseMove(RootGeometry, MakeMouse(StartExtent + 100.f, 500.f));
	TestNearlyEqual(TEXT("left area grew"), L->GetAreaExtent(EDockSide::Left), StartExtent + 100.f, 0.01f);
	Host->OnMouseMove(RootGeometry, MakeMouse(0.f, 500.f));
	TestNearlyEqual(TEXT("left area stops at the minimum extent"), L->GetAreaExtent(EDockSide::Left), 120.f, 0.01f);
	Host->OnMouseMove(RootGeometry, MakeMouse(99999.f, 500.f));
	TestNearlyEqual(TEXT("left area stops at its maximum share"), L->GetAreaExtent(EDockSide::Left), 1920.f * 0.6f, 0.5f);
	Host->OnMouseButtonUp(RootGeometry, MakeMouse(0.f, 500.f, false));

	// --- right edge area: dragging left grows it ---
	const float RightStart = L->GetAreaExtent(EDockSide::Right);
	Host->BeginAreaEdgeDrag(EDockSide::Right, MakeMouse(1920.f - RightStart, 500.f));
	Host->OnMouseMove(RootGeometry, MakeMouse(1920.f - RightStart - 80.f, 500.f));
	TestNearlyEqual(TEXT("right area grew"), L->GetAreaExtent(EDockSide::Right), RightStart + 80.f, 0.01f);
	Host->OnMouseButtonUp(RootGeometry, MakeMouse(0.f, 500.f, false));

	FString Err;
	TestTrue(TEXT("layout valid after dragging"), L->Validate(Err));
	(void)Changed;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
