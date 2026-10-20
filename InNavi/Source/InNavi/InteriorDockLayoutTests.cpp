#if WITH_DEV_AUTOMATION_TESTS

#include "InteriorDockLayout.h"

#include "Components/TextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "InteriorScreenWidget.h"
#include "DockHostUMG.h"
#include "HAL/FileManager.h"
#include "Layout/ArrangedChildren.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

// File-scope `using namespace` would leak into other files in a unity build, so keep it inside a namespace.
namespace InteriorDockTests
{
using namespace InteriorDock;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteriorDockDefaultLayoutTest, "InNavi.Dock.Interior.DefaultLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteriorDockDefaultLayoutTest::RunTest(const FString& Parameters)
{
	FDockLayout L;
	BuildDefaultLayout(L);
	FString Err;
	TestTrue(*FString::Printf(TEXT("default layout is valid (%s)"), *Err), L.Validate(Err));

	for (const FName Panel : { Map, Toolbar, Floor, Layers, Properties })
	{
		TestTrue(*FString::Printf(TEXT("%s is visible by default"), *Panel.ToString()), L.ContainsPanel(Panel));
	}
	for (const FName Panel : { Navigation, InteriorDock::Audio, Debug })
	{
		TestTrue(*FString::Printf(TEXT("%s is hidden by default"), *Panel.ToString()), L.IsHidden(Panel));
	}

	const TSharedPtr<FDockNode> Left = L.GetAreaRoot(EDockSide::Left);
	TestTrue(TEXT("left: Map over Toolbar"), Left.IsValid() && Left->IsSplit() && Left->Orientation == EDockOrientation::Vertical && Left->Children.Num() == 2);
	if (Left.IsValid() && Left->IsSplit() && Left->Children.Num() == 2)
	{
		TestTrue(TEXT("Map first"), Left->Children[0]->Tabs.Contains(Map));
		TestNearlyEqual(TEXT("Map share"), Left->Ratios[0], 0.29f, 1e-3f);
	}
	const TSharedPtr<FDockNode> Right = L.GetAreaRoot(EDockSide::Right);
	TestTrue(TEXT("right: three stacked groups"), Right.IsValid() && Right->IsSplit() && Right->Children.Num() == 3);
	TestFalse(TEXT("no top area"), L.GetAreaRoot(EDockSide::Top).IsValid());
	TestFalse(TEXT("no bottom area until Audio/Debug are shown"), L.GetAreaRoot(EDockSide::Bottom).IsValid());

	// Showing Navigation puts it beside Properties as a tab; Audio/Debug return to the bottom.
	TestTrue(TEXT("show Navigation"), L.ShowPanel(Navigation, DefaultSideFor(Navigation)));
	FDockLocation Nav, Props;
	L.FindPanel(Navigation, Nav);
	L.FindPanel(Properties, Props);
	TestTrue(TEXT("Navigation is a tab of Properties' group"), Nav.Group == Props.Group);

	TestTrue(TEXT("show Audio"), L.ShowPanel(InteriorDock::Audio, DefaultSideFor(InteriorDock::Audio)));
	TestTrue(TEXT("Audio is in the bottom area"), L.GetAreaRoot(EDockSide::Bottom).IsValid());
	TestTrue(TEXT("show Debug"), L.ShowPanel(Debug, DefaultSideFor(Debug)));
	FDockLocation AudioLoc, DebugLoc;
	L.FindPanel(InteriorDock::Audio, AudioLoc);
	L.FindPanel(Debug, DebugLoc);
	TestTrue(TEXT("Debug joins Audio's tab group"), AudioLoc.Group == DebugLoc.Group);
	TestTrue(TEXT("still valid"), L.Validate(Err));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteriorDockPersistenceTest, "InNavi.Dock.Interior.Persistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteriorDockPersistenceTest::RunTest(const FString& Parameters)
{
	const FVector2D Viewport(1920.0, 1080.0);
	const FString Path = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / TEXT("InteriorDockLayoutTest.json");
	IFileManager::Get().Delete(*Path, false, true, true);

	FDockLayout Original;
	BuildDefaultLayout(Original);
	Original.DockPanel(Debug, Properties, EDockDropZone::Left);
	Original.ShowPanel(Navigation, EDockSide::Right);
	TestTrue(TEXT("saves"), SaveToFile(Original, Path, Viewport));

	FDockLayout Restored;
	TestTrue(TEXT("loads"), LoadFromFile(Restored, Path, Viewport));
	FString Err;
	TestTrue(TEXT("restored layout is valid"), Restored.Validate(Err));
	for (const FName Panel : AllPanels())
	{
		TestEqual(*FString::Printf(TEXT("%s visibility survives"), *Panel.ToString()), Restored.ContainsPanel(Panel), Original.ContainsPanel(Panel));
		TestEqual(*FString::Printf(TEXT("%s hidden state survives"), *Panel.ToString()), Restored.IsHidden(Panel), Original.IsHidden(Panel));
	}

	// A file that predates a panel still gets that panel placed (visible ones) rather than lost.
	FDockLayout Partial;
	Partial.DockPanelToEdge(Floor, EDockSide::Right);
	TestTrue(TEXT("saves partial"), SaveToFile(Partial, Path, Viewport));
	FDockLayout FromPartial;
	TestTrue(TEXT("loads partial"), LoadFromFile(FromPartial, Path, Viewport));
	TestTrue(TEXT("Toolbar was missing and is shown at its default side"), FromPartial.ContainsPanel(Toolbar));
	TestFalse(TEXT("Audio was missing but is hidden by default"), FromPartial.ContainsPanel(InteriorDock::Audio));
	TestTrue(TEXT("valid"), FromPartial.Validate(Err));

	// An old-format or garbage file is ignored.
	FFileHelper::SaveStringToFile(TEXT("{\"layoutVersion\":2,\"panels\":{}}"), *Path);
	FDockLayout Old;
	TestFalse(TEXT("version 2 is rejected"), LoadFromFile(Old, Path, Viewport));
	FFileHelper::SaveStringToFile(TEXT("not json at all"), *Path);
	TestFalse(TEXT("garbage is rejected"), LoadFromFile(Old, Path, Viewport));
	TestFalse(TEXT("missing file"), LoadFromFile(Old, Path + TEXT(".nope"), Viewport));

	IFileManager::Get().Delete(*Path, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockUMGWrapperTest, "InNavi.Dock.Interior.UMGWrapper", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDockUMGWrapperTest::RunTest(const FString& Parameters)
{
	UDockHostWidget* Wrapper = NewObject<UDockHostWidget>(GetTransientPackage());
	BuildDefaultLayout(*Wrapper->GetLayout());

	// Registering before the Slate widget exists must work (the owner builds its panels first).
	for (const FName Panel : AllPanels())
	{
		UTextBlock* Content = NewObject<UTextBlock>(GetTransientPackage());
		Content->SetText(FText::FromName(Panel));
		Wrapper->RegisterPanel(Panel, FText::FromName(Panel), Content);
	}

	const TSharedRef<SWidget> Slate = Wrapper->TakeWidget();
	TestEqual(TEXT("the wrapper never swallows clicks itself"), Slate->GetVisibility(), EVisibility::SelfHitTestInvisible);

	FArrangedChildren Arranged(EVisibility::Visible);
	Slate->ArrangeChildren(FGeometry::MakeRoot(FVector2f(1920.f, 1080.f), FSlateLayoutTransform()), Arranged);
	// left: 2 groups + splitter, right: 3 groups + 2 splitters, plus the two edge handles
	TestEqual(TEXT("default layout arranges 5 groups, 3 splitters and 2 edge handles"), Arranged.Num(), 10);

	TestTrue(TEXT("Floor is shown"), Wrapper->IsPanelShown(Floor));
	TestFalse(TEXT("Audio is not"), Wrapper->IsPanelShown(InteriorDock::Audio));

	// Hiding and showing through the model plus NotifyLayoutChanged keeps working after the widget exists.
	Wrapper->GetLayout()->ShowPanel(InteriorDock::Audio, EDockSide::Bottom);
	Wrapper->NotifyLayoutChanged();
	Arranged.Empty();
	Slate->ArrangeChildren(FGeometry::MakeRoot(FVector2f(1920.f, 1080.f), FSlateLayoutTransform()), Arranged);
	TestEqual(TEXT("showing Audio adds the bottom group and its edge handle"), Arranged.Num(), 12);

	// A late registration also works.
	UTextBlock* Late = NewObject<UTextBlock>(GetTransientPackage());
	Wrapper->RegisterPanel(FName(TEXT("Late")), FText::FromString(TEXT("Late")), Late);
	TestFalse(TEXT("a panel with no layout position is not shown"), Wrapper->IsPanelShown(FName(TEXT("Late"))));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteriorScreenDockSmokeTest, "InNavi.Dock.Interior.ScreenSmoke", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteriorScreenDockSmokeTest::RunTest(const FString& Parameters)
{
	// Builds the real interior editor widget (no player controller, no building) and checks that the migrated
	// dock system comes up with every panel in place and a sane arrangement.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UInteriorScreenWidget* Screen = CreateWidget<UInteriorScreenWidget>(World, UInteriorScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("screen created"), Screen))
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	UDockHostWidget* Dock = Screen->GetDockHost();
	if (!TestNotNull(TEXT("screen owns a dock host"), Dock))
	{
		return false;
	}

	// The screen loads whatever layout this machine saved; start from the team default so the checks below do
	// not depend on it.
	const TSharedRef<FDockLayout> L = Dock->GetLayout();
	BuildDefaultLayout(*L);
	Dock->NotifyLayoutChanged();

	FString Err;
	TestTrue(*FString::Printf(TEXT("layout valid (%s)"), *Err), L->Validate(Err));
	for (const FName Panel : AllPanels())
	{
		const bool bShownByDefault = Panel == Map || Panel == Toolbar || Panel == Floor || Panel == Layers || Panel == Properties;
		TestEqual(*FString::Printf(TEXT("%s shown state"), *Panel.ToString()), Dock->IsPanelShown(Panel), bShownByDefault);
	}

	FArrangedChildren Arranged(EVisibility::Visible);
	Slate->ArrangeChildren(FGeometry::MakeRoot(FVector2f(1920.f, 1080.f), FSlateLayoutTransform()), Arranged);
	TestTrue(TEXT("the screen arranges children"), Arranged.Num() > 0);

	// Showing a hidden panel and resetting both work on the real screen.
	L->ShowPanel(Navigation, EDockSide::Right);
	Dock->NotifyLayoutChanged();
	TestTrue(TEXT("Navigation shown"), Dock->IsPanelShown(Navigation));
	BuildDefaultLayout(*L);
	Dock->NotifyLayoutChanged();
	TestFalse(TEXT("reset hides Navigation again"), Dock->IsPanelShown(Navigation));
	return true;
}

} // namespace InteriorDockTests

#endif // WITH_DEV_AUTOMATION_TESTS
