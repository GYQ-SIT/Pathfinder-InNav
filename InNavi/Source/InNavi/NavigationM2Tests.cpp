#if WITH_DEV_AUTOMATION_TESTS

#include "NavigationRouteVisualization.h"
#include "NavigationPanelWidget.h"
#include "InteriorScreenWidget.h"
#include "InteriorDockLayout.h"
#include "DockHostUMG.h"
#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "PolyBuilding.h"
#include "NavPlayerController.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "DrawDebugHelpers.h"
#include "Algo/Reverse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationM2ToggleTest, "InNavi.Navigation.M2.PanelToggle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavigationM2ToggleTest::RunTest(const FString& Parameters)
{
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>();
	Screen->Initialize();
	Screen->TakeWidget();
	UDockHostWidget* Dock = Screen->GetDockHost();
	UCheckBox* Check = Cast<UCheckBox>(Screen->FindScreenWidget(TEXT("Chk_WindowNavigation")));
	if (!TestNotNull(TEXT("Dock host exists"), Dock) || !TestNotNull(TEXT("Window menu navigation checkbox exists"), Check)) return false;

	// The screen loads whatever layout this machine saved; start from the team default (Navigation hidden).
	InteriorDock::BuildDefaultLayout(*Dock->GetLayout());
	Dock->NotifyLayoutChanged();
	const FName Navigation = InteriorDock::Navigation;
	TestFalse(TEXT("Navigation starts hidden"), Dock->IsPanelShown(Navigation));

	// The Window menu's actual event path opens and closes the dock panel, and the check box follows.
	Check->SetIsChecked(true);
	Check->OnCheckStateChanged.Broadcast(true);
	TestTrue(TEXT("Window menu opens the navigation dock"), Dock->IsPanelShown(Navigation));
	TestTrue(TEXT("Window checkbox stays checked"), Check->IsChecked());
	Check->SetIsChecked(false);
	Check->OnCheckStateChanged.Broadcast(false);
	TestFalse(TEXT("Window menu closes the same dock"), Dock->IsPanelShown(Navigation));
	TestFalse(TEXT("Window checkbox stays unchecked"), Check->IsChecked());
	for (int32 i = 0; i < 4; ++i)
	{
		const bool bOpen = (i % 2) == 0;
		Check->SetIsChecked(bOpen);
		Check->OnCheckStateChanged.Broadcast(bOpen);
	}
	TestFalse(TEXT("Repeated toggling reliably ends closed"), Dock->IsPanelShown(Navigation));

	UButton* RouteToggle = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_NavToggleRoute")));
	if (TestNotNull(TEXT("Route visibility has its own control"), RouteToggle))
	{
		TestFalse(TEXT("Route toggle starts disabled without navigation data"), RouteToggle->GetIsEnabled());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavigationM2WorldRouteTest, "InNavi.Navigation.M2.WorldRouteDisplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavigationM2WorldRouteTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ABuildingGenerator* Generator = World->SpawnActor<ABuildingGenerator>();
	AInteriorGenerator* Interior = World->SpawnActor<AInteriorGenerator>();
	Interior->DispatchBeginPlay();
	Generator->LoadBuildingsFromFile(FPaths::ProjectDir() / TEXT("Data/buildings.json"));
	Interior->LoadInteriorForBuilding(TEXT("E1"));
	APolyBuilding* Building = Generator->FindBuildingById(TEXT("E1"));
	if (!TestNotNull(TEXT("Test building exists"), Building))
	{
		World->DestroyWorld(false);
		return false;
	}
	UNavigationPanelWidget* Panel = NewObject<UNavigationPanelWidget>();
	Panel->SetNavigationContext(Generator, Interior);
	Building->BuildingOffset = FVector2D(17, -23);
	const float Scale = Generator->GetScale();
	TArray<FNavigationWorldRoutePoint> Input;
	for (int32 I = 0; I < 4; ++I)
	{
		FNavigationWorldRoutePoint Point;
		Point.WorldPosition = FVector((117 + I * 10) * Scale, 277 * Scale, I < 2 ? 25 + I * 15 : 355 + I * 15);
		Point.Floor = I < 2 ? 1 : 2;
		if (I == 1 || I == 2)
		{
			Point.ConnectorId = TEXT("Lift-A");
			Point.ConnectorType = EInteriorNavNodeType::Elevator;
		}
		Input.Add(Point);
	}
	TestTrue(TEXT("Person A world route is accepted"), Panel->ApplyWorldRouteResult(TEXT("E1"), Input));
	const TArray<FNavigationRoutePoint>& Route = Panel->GetRoutePoints();
	TestEqual(TEXT("All ordered points retained"), Route.Num(), 4);
	if (Route.Num() == 4)
	{
		TestTrue(TEXT("World coordinates convert using building scale and offset"),
			Route[0].Position.Equals(FVector2D(100, 300)));
		TestTrue(TEXT("Original world height retained"), Route[3].WorldPosition.Equals(Input[3].WorldPosition));
		const FNavigation3DView Lower = UNavigationRouteVisualization::BuildView(Route,
			&Panel->GetStart(), &Panel->GetDestination(), Building->BuildingOffset, Scale, 300, 1);
		TestTrue(TEXT("Start marker appears on viewed floor"), Lower.Start.IsSet());
		TestFalse(TEXT("Other floor destination is hidden"), Lower.Destination.IsSet());
		TestEqual(TEXT("Lower floor shows route plus local transition arrow"), Lower.Segments.Num(), 2);
		if (Lower.Segments.Num() == 2)
		{
			TestTrue(TEXT("Route direction and exact Z are preserved"), Lower.Segments[0].From.Equals(Input[0].WorldPosition + FVector(0, 0, 15))
				&& Lower.Segments[0].To.Equals(Input[1].WorldPosition + FVector(0, 0, 15)));
			TestTrue(TEXT("Floor transition arrow points upward"), Lower.Segments[1].bTransition
				&& Lower.Segments[1].To.Z > Lower.Segments[1].From.Z);
		}
		TestTrue(TEXT("Connector departure label identifies next floor"), Lower.Labels.ContainsByPredicate(
			[](const FNavigationDisplayLabel& Label) { return Label.Text.Contains(TEXT("Lift-A to Floor 2")); }));
		const FNavigation3DView Upper = UNavigationRouteVisualization::BuildView(Route,
			&Panel->GetStart(), &Panel->GetDestination(), Building->BuildingOffset, Scale, 300, 2);
		TestTrue(TEXT("Destination appears on its floor"), Upper.Destination.IsSet());
		TestFalse(TEXT("Lower floor start stays hidden"), Upper.Start.IsSet());
		TestTrue(TEXT("Arrival label identifies previous floor"), Upper.Labels.ContainsByPredicate(
			[](const FNavigationDisplayLabel& Label) { return Label.Text.Contains(TEXT("from Floor 1")); }));

		ANavPlayerController* Owner = World->SpawnActor<ANavPlayerController>();
		TestFalse(TEXT("Controller permits rendering its route components"), Owner->IsHidden());
		UNavigationRouteVisualization* Renderer = NewObject<UNavigationRouteVisualization>(Owner);
		Renderer->RegisterComponent();
		DrawDebugLine(World, FVector::ZeroVector, FVector(100, 0, 0), FColor::Green, true, -1.f);
		ULineBatchComponent* EditorGuides = World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent);
		const int32 EditorGuideCount = EditorGuides ? EditorGuides->BatchedLines.Num() : 0;
		TestTrue(TEXT("Editor guide exists before route redraw"), EditorGuideCount > 0);
		Renderer->ShowView(Lower);
		TestTrue(TEXT("Route contains arrow geometry as well as its segments"), Renderer->BatchedLines.Num() > Lower.Segments.Num());
		static_cast<ULineBatchComponent*>(Renderer)->TickComponent(0.1f, LEVELTICK_All, nullptr);
		TestTrue(TEXT("Route survives ticks without expiring"), !Renderer->BatchedLines.IsEmpty());
		Renderer->ClearRoute();
		TestTrue(TEXT("Clearing removes all route geometry"), Renderer->BatchedLines.IsEmpty());
		TestEqual(TEXT("Route redraw/clear preserves editor drawing guides"), EditorGuides ? EditorGuides->BatchedLines.Num() : 0, EditorGuideCount);
		Renderer->DestroyComponent();

		TArray<FNavigationRoutePoint> ReverseRoute = Route;
		Algo::Reverse(ReverseRoute);
		const FNavigation3DView Downward = UNavigationRouteVisualization::BuildView(ReverseRoute,
			nullptr, nullptr, Building->BuildingOffset, Scale, 300, 2);
		TestTrue(TEXT("Descending connector arrow points downward"), Downward.Segments.ContainsByPredicate(
			[](const FNavigationDisplaySegment& Segment) { return Segment.bTransition && Segment.To.Z < Segment.From.Z; }));
	}
	Input[0].Floor = 999;
	TestFalse(TEXT("Invalid world route is rejected"), Panel->ApplyWorldRouteResult(TEXT("E1"), Input));
	TestEqual(TEXT("Rejected input preserves previous route"), Panel->GetRoutePoints().Num(), 4);
	TestFalse(TEXT("World route for another building is rejected"), Panel->ApplyWorldRouteResult(TEXT("E2"), Input));
	Panel->RequestClearRoute();
	TestTrue(TEXT("Clear route removes imported data and endpoints"), Panel->GetRoutePoints().IsEmpty()
		&& !Panel->HasStart() && !Panel->HasDestination());

	TArray<FNavigationRoutePoint> OutsideRoute;
	FNavigationRoutePoint Point;
	Point.Position = FVector2D(80, 300); Point.Floor = 0; OutsideRoute.Add(Point);
	Point.Position = FVector2D(90, 300); OutsideRoute.Add(Point);
	Point.Floor = 1; OutsideRoute.Add(Point);
	Point.Position = FVector2D(100, 300); OutsideRoute.Add(Point);
	const FNavigation3DView OutdoorView = UNavigationRouteVisualization::BuildView(OutsideRoute,
		&OutsideRoute[0], &OutsideRoute.Last(), FVector2D::ZeroVector, 10, 300, 1);
	TestEqual(TEXT("Floor 1 shows outdoor and indoor route segments"), OutdoorView.Segments.Num(), 2);
	TestTrue(TEXT("Outdoor start uses ground level"), OutdoorView.Start.IsSet() && OutdoorView.Start.GetValue().Z == 15);
	TestTrue(TEXT("Exterior transition has an entry label"), OutdoorView.Labels.ContainsByPredicate(
		[](const FNavigationDisplayLabel& Label) { return Label.Text.Contains(TEXT("Enter building")); }));
	const FNavigation3DView HiddenOutdoor = UNavigationRouteVisualization::BuildView(OutsideRoute,
		&OutsideRoute[0], &OutsideRoute.Last(), FVector2D::ZeroVector, 10, 300, 2);
	TestTrue(TEXT("Outdoor route is hidden on upper floors"), HiddenOutdoor.Segments.IsEmpty() && HiddenOutdoor.Labels.IsEmpty());
	World->DestroyWorld(false);
	return true;
}

#endif
