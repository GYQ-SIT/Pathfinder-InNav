#if WITH_DEV_AUTOMATION_TESTS

#include "ExteriorDockLayout.h"

#include "DockHostUMG.h"
#include "EditScreenWidget.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Layout/ArrangedChildren.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExteriorDockDefaultLayoutTest, "InNavi.Dock.Exterior.DefaultLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExteriorDockDefaultLayoutTest::RunTest(const FString& Parameters)
{
	FDockLayout L;
	ExteriorDock::BuildDefaultLayout(L);
	FString Err;
	TestTrue(*FString::Printf(TEXT("default layout is valid (%s)"), *Err), L.Validate(Err));

	TestTrue(TEXT("Map is visible"), L.ContainsPanel(ExteriorDock::Map));
	TestTrue(TEXT("Building is visible"), L.ContainsPanel(ExteriorDock::Building));
	TestTrue(TEXT("Audio is hidden by default"), L.IsHidden(ExteriorDock::Audio));
	TestTrue(TEXT("Debug is hidden by default"), L.IsHidden(ExteriorDock::Debug));
	FDockLocation Loc;
	TestTrue(TEXT("Map is in the left area"), L.FindPanel(ExteriorDock::Map, Loc) && Loc.Kind == FDockLocation::EKind::Area && Loc.Side == EDockSide::Left);
	FDockLocation BuildingLoc;
	TestTrue(TEXT("Building is in the right area"), L.FindPanel(ExteriorDock::Building, BuildingLoc) && BuildingLoc.Kind == FDockLocation::EKind::Area && BuildingLoc.Side == EDockSide::Right);

	// Showing the hidden panels brings them back along the bottom, as tabs of one group.
	L.ShowPanel(ExteriorDock::Audio, ExteriorDock::DefaultSideFor(ExteriorDock::Audio));
	L.ShowPanel(ExteriorDock::Debug, ExteriorDock::DefaultSideFor(ExteriorDock::Debug));
	FDockLocation A, D;
	TestTrue(TEXT("both are placed"), L.FindPanel(ExteriorDock::Audio, A) && L.FindPanel(ExteriorDock::Debug, D));
	TestTrue(TEXT("Audio and Debug share the bottom group"), A.Kind == FDockLocation::EKind::Area && A.Side == EDockSide::Bottom && A.Group == D.Group);
	TestTrue(TEXT("valid after showing"), L.Validate(Err));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExteriorDockPersistenceTest, "InNavi.Dock.Exterior.Persistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExteriorDockPersistenceTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / TEXT("ExteriorDockLayoutTest.json");
	const FVector2D Viewport(1920.0, 1080.0);

	FDockLayout Layout;
	ExteriorDock::BuildDefaultLayout(Layout);
	Layout.ShowPanel(ExteriorDock::Debug, EDockSide::Bottom);
	Layout.FloatPanel(ExteriorDock::Map, FVector2D(300.0, 200.0), FVector2D(320.0, 400.0));
	TestTrue(TEXT("saves"), ExteriorDock::SaveToFile(Layout, Path, Viewport));

	FDockLayout Loaded;
	TestTrue(TEXT("loads"), ExteriorDock::LoadFromFile(Loaded, Path, Viewport));
	FDockLocation Loc;
	TestTrue(TEXT("Map is still floating"), Loaded.FindPanel(ExteriorDock::Map, Loc) && Loc.Kind == FDockLocation::EKind::Floating);
	TestTrue(TEXT("Debug is still shown"), Loaded.ContainsPanel(ExteriorDock::Debug));
	TestTrue(TEXT("Audio is still hidden"), Loaded.IsHidden(ExteriorDock::Audio));
	FString Err;
	TestTrue(TEXT("loaded layout is valid"), Loaded.Validate(Err));

	// A file that predates a panel still gets that panel placed (visible ones) rather than lost.
	FDockLayout Partial;
	Partial.DockPanelToEdge(ExteriorDock::Audio, EDockSide::Bottom);
	TestTrue(TEXT("saves partial"), ExteriorDock::SaveToFile(Partial, Path, Viewport));
	FDockLayout FromPartial;
	TestTrue(TEXT("loads partial"), ExteriorDock::LoadFromFile(FromPartial, Path, Viewport));
	TestTrue(TEXT("Map was missing and is shown at its default side"), FromPartial.ContainsPanel(ExteriorDock::Map));
	TestTrue(TEXT("so was Building"), FromPartial.ContainsPanel(ExteriorDock::Building));
	TestFalse(TEXT("Debug was missing but is hidden by default"), FromPartial.ContainsPanel(ExteriorDock::Debug));

	// The interior and exterior layouts live in different files.
	TestTrue(TEXT("separate user files"), ExteriorDock::UserLayoutPath() != FPaths::ProjectSavedDir() / TEXT("InteriorDockLayout.json"));

	FFileHelper::SaveStringToFile(TEXT("not json at all"), *Path);
	FDockLayout Bad;
	TestFalse(TEXT("garbage is rejected"), ExteriorDock::LoadFromFile(Bad, Path, Viewport));
	IFileManager::Get().Delete(*Path, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExteriorScreenDockSmokeTest, "InNavi.Dock.Exterior.ScreenSmoke", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExteriorScreenDockSmokeTest::RunTest(const FString& Parameters)
{
	// Builds the real exterior screen (no player controller) and checks the dock comes up with its panels.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UEditScreenWidget* Screen = CreateWidget<UEditScreenWidget>(World, UEditScreenWidget::StaticClass());
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

	// The screen loads the user's saved arrangement on construction; start from the team default so the
	// checks below do not depend on whatever is saved on this machine.
	const TSharedRef<FDockLayout> L = Dock->GetLayout();
	ExteriorDock::BuildDefaultLayout(*L);
	Dock->NotifyLayoutChanged();

	FString Err;
	TestTrue(*FString::Printf(TEXT("layout valid (%s)"), *Err), L->Validate(Err));
	TestTrue(TEXT("Map is shown"), Dock->IsPanelShown(ExteriorDock::Map));
	TestTrue(TEXT("Building is shown"), Dock->IsPanelShown(ExteriorDock::Building));
	TestFalse(TEXT("Audio is hidden"), Dock->IsPanelShown(ExteriorDock::Audio));
	TestFalse(TEXT("Debug is hidden"), Dock->IsPanelShown(ExteriorDock::Debug));

	FArrangedChildren Arranged(EVisibility::Visible);
	Slate->ArrangeChildren(FGeometry::MakeRoot(FVector2f(1920.f, 1080.f), FSlateLayoutTransform()), Arranged);
	TestTrue(TEXT("the screen arranges children"), Arranged.Num() > 0);

	// Every panel the screen registered can be shown (it has content), and reset hides the extras again.
	for (const FName Panel : ExteriorDock::AllPanels())
	{
		L->ShowPanel(Panel, ExteriorDock::DefaultSideFor(Panel));
	}
	Dock->NotifyLayoutChanged();
	for (const FName Panel : ExteriorDock::AllPanels())
	{
		TestTrue(*FString::Printf(TEXT("%s shown"), *Panel.ToString()), Dock->IsPanelShown(Panel));
	}
	Arranged.Empty();
	Slate->ArrangeChildren(FGeometry::MakeRoot(FVector2f(1920.f, 1080.f), FSlateLayoutTransform()), Arranged);
	TestTrue(TEXT("still arranges with all panels shown"), Arranged.Num() > 0);
	ExteriorDock::BuildDefaultLayout(*L);
	Dock->NotifyLayoutChanged();
	TestFalse(TEXT("reset hides Audio again"), Dock->IsPanelShown(ExteriorDock::Audio));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
