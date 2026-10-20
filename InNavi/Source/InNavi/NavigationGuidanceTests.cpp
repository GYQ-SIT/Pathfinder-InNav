#if WITH_DEV_AUTOMATION_TESTS

#include "NavigationPanelWidget.h"
#include "NavigationRouteVisualization.h"
#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "PolyBuilding.h"
#include "InteriorScreenWidget.h"
#include "NavPlayerController.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationGuidanceVisibilityTest,
	"InNavi.Navigation.Guidance.Visibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavigationGuidanceVisibilityTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ABuildingGenerator* Generator = World->SpawnActor<ABuildingGenerator>();
	AInteriorGenerator* Interior = World->SpawnActor<AInteriorGenerator>();
	Interior->DispatchBeginPlay();
	Generator->LoadBuildingsFromFile(FPaths::ProjectDir() / TEXT("Data/buildings.json"));
	Interior->LoadInteriorForBuilding(TEXT("E1"));
	APolyBuilding* Building = Generator->FindBuildingById(TEXT("E1"));
	if (!TestNotNull(TEXT("Fixture building exists"), Building))
	{
		World->DestroyWorld(false);
		return false;
	}
	UNavigationPanelWidget* Panel = NewObject<UNavigationPanelWidget>();
	Panel->SetNavigationContext(Generator, Interior);
	TestFalse(TEXT("No guidance can be toggled before selection"), Panel->CanToggleRouteGuidance());
	Panel->ToggleRouteGuidance();
	TestTrue(TEXT("Empty toggle does not hide future selection"), Panel->IsRouteGuidanceVisible());

	TArray<FNavigationWorldRoutePoint> Input;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FNavigationWorldRoutePoint Point;
		const FVector2D XY = (FVector2D(100 + Index * 10, 300) + Building->BuildingOffset) * Generator->GetScale();
		Point.WorldPosition = FVector(XY.X, XY.Y, 20 + Index * 10);
		Point.Floor = 1;
		Input.Add(Point);
	}
	TestTrue(TEXT("Route imported"), Panel->ApplyWorldRouteResult(TEXT("E1"), Input));
	TestTrue(TEXT("Stored guidance enables the toggle"), Panel->CanToggleRouteGuidance());
	const TArray<FNavigationRoutePoint> Saved = Panel->GetRoutePoints();
	const FString Details = Panel->BuildRouteDetails();
	Panel->ToggleRouteGuidance();
	TestFalse(TEXT("Guidance is hidden"), Panel->IsRouteGuidanceVisible());
	TestEqual(TEXT("Hiding retains route points"), Panel->GetRoutePoints().Num(), Saved.Num());
	TestTrue(TEXT("Hiding retains endpoints and exact coordinates"), Panel->HasStart() && Panel->HasDestination()
		&& Panel->GetStart().WorldPosition.Equals(Input[0].WorldPosition)
		&& Panel->GetDestination().WorldPosition.Equals(Input[1].WorldPosition));
	TestEqual(TEXT("Hiding retains route instructions as data"), Panel->BuildRouteDetails(), Details);
	TestTrue(TEXT("Hidden endpoints remain ready for route calculation"), Panel->CanFindRoute());

	auto BuildDisplay = [&]()
	{
		return UNavigationRouteVisualization::BuildView(Panel->GetRoutePoints(), &Panel->GetStart(),
			&Panel->GetDestination(), Building->BuildingOffset, Generator->GetScale(),
			Building->GetFloorHeight(), Interior->GetActiveFloor(), Panel->IsRouteGuidanceVisible());
	};
	const FNavigation3DView Hidden = BuildDisplay();
	TestTrue(TEXT("Hidden display contains no lines, labels or markers"), Hidden.Segments.IsEmpty()
		&& Hidden.Labels.IsEmpty() && !Hidden.Start.IsSet() && !Hidden.Destination.IsSet());
	Interior->SetActiveFloor(2);
	TestFalse(TEXT("Changing floors does not restore hidden guidance"), Panel->IsRouteGuidanceVisible());
	Interior->SetActiveFloor(1);
	Panel->ToggleSelectStart();
	Panel->CancelSelection();
	TestFalse(TEXT("Selection cancellation does not restore guidance"), Panel->IsRouteGuidanceVisible());
	Panel->ToggleRouteGuidance();
	const FNavigation3DView Shown = BuildDisplay();
	TestTrue(TEXT("Showing restores the existing route and markers"), Shown.Segments.Num() == 1
		&& Shown.Start.IsSet() && Shown.Destination.IsSet());
	if (!Shown.Segments.IsEmpty())
	{
		TestTrue(TEXT("Restored route retains travel direction and world heights"),
			Shown.Segments[0].From.Equals(Input[0].WorldPosition + FVector(0, 0, 15))
			&& Shown.Segments[0].To.Equals(Input[1].WorldPosition + FVector(0, 0, 15)));
	}
	AActor* Owner = World->SpawnActor<AActor>();
	UNavigationRouteVisualization* Renderer = NewObject<UNavigationRouteVisualization>(Owner);
	Renderer->RegisterComponent();
	Renderer->ShowView(Shown);
	TestFalse(TEXT("Visible route draws geometry"), Renderer->BatchedLines.IsEmpty());
	Renderer->ShowView(Hidden);
	TestTrue(TEXT("Hiding removes previously drawn geometry"), Renderer->BatchedLines.IsEmpty());
	Renderer->ShowView(Shown);
	TestFalse(TEXT("Showing restores geometry without recalculation"), Renderer->BatchedLines.IsEmpty());
	Renderer->DestroyComponent();

	Panel->SetRouteGuidanceVisible(false);
	TestTrue(TEXT("New route import succeeds"), Panel->ApplyWorldRouteResult(TEXT("E1"), Input));
	TestTrue(TEXT("A newly supplied route is shown"), Panel->IsRouteGuidanceVisible());
	Panel->SetRouteGuidanceVisible(false);
	Panel->ToggleSelectStart();
	Panel->SelectPoint(FVector2D(100, 300), 1);
	TestTrue(TEXT("New endpoint restores selection feedback and invalidates old route"),
		Panel->IsRouteGuidanceVisible() && Panel->HasStart() && Panel->GetRoutePoints().IsEmpty());
	Panel->SetRouteGuidanceVisible(false);
	Panel->RequestClearRoute();
	TestTrue(TEXT("Clear resets visibility, route and endpoints"), Panel->IsRouteGuidanceVisible()
		&& !Panel->CanToggleRouteGuidance() && !Panel->HasStart() && !Panel->HasDestination());
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationMapDirectionTest,
	"InNavi.Navigation.Guidance.MapDirectionArrows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavigationMapDirectionTest::RunTest(const FString& Parameters)
{
	for (const FVector2D& End : { FVector2D(240, 0), FVector2D(-240, 0),
		FVector2D(0, 240), FVector2D(0, -240), FVector2D(180, 180) })
	{
		const TArray<FNavigationDirectionArrow2D> Arrows =
			UNavigationRouteVisualization::BuildDirectionArrows2D(FVector2D::ZeroVector, End);
		TestFalse(TEXT("A usable map segment has direction arrows"), Arrows.IsEmpty());
		const FVector2D Direction = End.GetSafeNormal();
		for (const FNavigationDirectionArrow2D& Arrow : Arrows)
		{
			TestTrue(TEXT("Both arrow wings point behind the tip in travel direction"),
				FVector2D::DotProduct(Arrow.Tip - Arrow.HeadLeft, Direction) > 0
				&& FVector2D::DotProduct(Arrow.Tip - Arrow.HeadRight, Direction) > 0);
			TestTrue(TEXT("Arrow tip stays inside its segment"),
				FVector2D::DotProduct(Arrow.Tip, Direction) > 0
				&& FVector2D::DotProduct(Arrow.Tip, Direction) < End.Size());
			TestTrue(TEXT("Arrow remains readable without scaling with route length"),
				FVector2D::Distance(Arrow.Tip, Arrow.HeadLeft) <= 10.0);
		}
	}
	TestTrue(TEXT("Duplicate points have no arrows"),
		UNavigationRouteVisualization::BuildDirectionArrows2D(FVector2D(5, 5), FVector2D(5, 5)).IsEmpty());
	TestTrue(TEXT("Tiny segments have no crowded arrows"),
		UNavigationRouteVisualization::BuildDirectionArrows2D(FVector2D::ZeroVector, FVector2D(5, 0)).IsEmpty());
	TestEqual(TEXT("Long segments have a bounded arrow count"),
		UNavigationRouteVisualization::BuildDirectionArrows2D(FVector2D::ZeroVector, FVector2D(100000, 0)).Num(), 32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationGuidanceControlTest,
	"InNavi.Navigation.Guidance.Control",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavigationGuidanceControlTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ANavPlayerController* Controller = World->SpawnActor<ANavPlayerController>();
	// Supply a local widget owner without starting gameplay or loading/editing a saved project.
	ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
	Controller->Player = Player;
	Player->PlayerController = Controller;
	Controller->SetAsLocalPlayerController();
	World->AddController(Controller);
	Controller->SetButtonClickSoundEnabled(false);
	UNavigationPanelWidget* Nav = NewObject<UNavigationPanelWidget>();
	FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(ANavPlayerController::StaticClass(), TEXT("NavigationPanel"));
	if (!TestNotNull(TEXT("Controller navigation state is available"), Property))
	{
		World->DestroyWorld(false);
		return false;
	}
	Property->SetObjectPropertyValue_InContainer(Controller, Nav);
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>();
	Screen->Initialize();
	Screen->TakeWidget();
	Screen->SetOwningPlayer(Controller);
	if (!TestTrue(TEXT("Guidance screen resolves its local controller"), Screen->GetOwningPlayer() == Controller))
	{
		World->DestroyWorld(false);
		return false;
	}
	FScriptDelegate Refresh;
	Refresh.BindUFunction(Screen, TEXT("HandleNavigationStateChanged"));
	Nav->OnStateChanged.AddUnique(Refresh);
	UButton* Button = Cast<UButton>(Screen->GetWidgetFromName(TEXT("Btn_NavToggleRoute")));
	UTextBlock* Label = Cast<UTextBlock>(Screen->GetWidgetFromName(TEXT("Btn_NavToggleRouteLabel")));
	UTextBlock* Instructions = Cast<UTextBlock>(Screen->GetWidgetFromName(TEXT("Txt_NavRouteDetails")));
	UButton* PanelToggle = Cast<UButton>(Screen->GetWidgetFromName(TEXT("Btn_NavigationToggle")));
	if (!TestNotNull(TEXT("Guidance button exists"), Button) || !TestNotNull(TEXT("Guidance label exists"), Label)
		|| !TestNotNull(TEXT("Instructions exist"), Instructions) || !TestNotNull(TEXT("Panel toggle exists"), PanelToggle))
	{
		World->DestroyWorld(false);
		return false;
	}
	Nav->ClearAll();
	TestFalse(TEXT("Guidance control is disabled without route or endpoints"), Button->GetIsEnabled());
	FNavigationRouteResult Route;
	FNavigationRoutePoint Point;
	Point.Position = FVector2D(100, 300); Route.Points.Add(Point);
	Point.Position = FVector2D(110, 300); Route.Points.Add(Point);
	Nav->ApplyRouteResult(Route);
	TestTrue(TEXT("New route enables the control through its state event"), Button->GetIsEnabled());
	Button->OnClicked.Broadcast();
	TestFalse(TEXT("Actual button hides guidance"), Nav->IsRouteGuidanceVisible());
	TestEqual(TEXT("Hidden label offers restoring the route"), Label->GetText().ToString(), FString(TEXT("Show Route")));
	TestEqual(TEXT("Hidden instructions are collapsed"), Instructions->GetVisibility(), ESlateVisibility::Collapsed);
	PanelToggle->OnClicked.Broadcast();
	PanelToggle->OnClicked.Broadcast();
	TestFalse(TEXT("Opening and closing the panel preserves hidden guidance"), Nav->IsRouteGuidanceVisible());
	TestEqual(TEXT("Panel visibility does not clear the stored route"), Nav->GetRoutePoints().Num(), 2);
	Button->OnClicked.Broadcast();
	TestTrue(TEXT("Actual button restores guidance"), Nav->IsRouteGuidanceVisible());
	TestEqual(TEXT("Visible label offers hiding the route"), Label->GetText().ToString(), FString(TEXT("Hide Route")));
	TestEqual(TEXT("Showing restores instructions"), Instructions->GetVisibility(), ESlateVisibility::Visible);
	Nav->OnStateChanged.Remove(Refresh);
	World->DestroyWorld(false);
	return true;
}

#endif
