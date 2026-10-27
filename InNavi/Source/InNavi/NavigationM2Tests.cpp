#if WITH_DEV_AUTOMATION_TESTS

#include "NavigationRouteVisualization.h"
#include "NavigationPanelWidget.h"
#include "InteriorScreenWidget.h"
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
	UButton* Button = Cast<UButton>(Screen->GetWidgetFromName(TEXT("Btn_NavigationToggle")));
	UBorder* Panel = Cast<UBorder>(Screen->GetWidgetFromName(TEXT("Pnl_Navigation")));
	UCheckBox* Check = Cast<UCheckBox>(Screen->GetWidgetFromName(TEXT("Chk_WindowNavigation")));
	UTextBlock* Text = Cast<UTextBlock>(Screen->GetWidgetFromName(TEXT("Txt_NavigationToggle")));
	if (!TestNotNull(TEXT("Visible navigation toggle exists"), Button)
		|| !TestNotNull(TEXT("Navigation dock exists"), Panel)
		|| !TestNotNull(TEXT("Window menu navigation checkbox exists"), Check)
		|| !TestNotNull(TEXT("Toggle label exists"), Text)) return false;
	TestEqual(TEXT("Navigation starts hidden"), Panel->GetVisibility(), ESlateVisibility::Collapsed);
	Button->OnClicked.Broadcast();
	TestEqual(TEXT("Toggle opens the dock"), Panel->GetVisibility(), ESlateVisibility::Visible);
	TestTrue(TEXT("Window checkbox follows toggle"), Check->IsChecked());
	TestEqual(TEXT("Open toggle offers closing"), Text->GetText().ToString(), FString(TEXT("Hide Navigation")));
	// Use the Window menu's actual event path to close, then open with the top-right button.
	Check->SetIsChecked(false);
	Check->OnCheckStateChanged.Broadcast(false);
	TestEqual(TEXT("Window menu closes the same dock"), Panel->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Closed toggle offers opening"), Text->GetText().ToString(), FString(TEXT("Show Navigation")));
	Button->OnClicked.Broadcast();
	Button->OnClicked.Broadcast();
	TestEqual(TEXT("Repeated toggle reliably closes the dock"), Panel->GetVisibility(), ESlateVisibility::Collapsed);
	UButton* RouteToggle = Cast<UButton>(Screen->GetWidgetFromName(TEXT("Btn_NavToggleRoute")));
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

		TArray<FNavigationStop> Stops;
		FNavigationStop FirstStop;
		FirstStop.Point = Route[0];
		Stops.Add(FirstStop);
		TArray<FNavigationRouteLeg> Legs;
		FNavigationRouteLeg FirstLeg;
		FirstLeg.FirstPointIndex = 0;
		FirstLeg.LastPointIndex = 1;
		Legs.Add(FirstLeg);
		FNavigationRouteLeg SecondLeg;
		SecondLeg.FirstPointIndex = 1;
		SecondLeg.LastPointIndex = 3;
		Legs.Add(SecondLeg);
		const FNavigation3DView MultiStopLower = UNavigationRouteVisualization::BuildView(Route,
			&Panel->GetStart(), &Panel->GetDestination(), Building->BuildingOffset, Scale, 300, 1, true, &Stops, &Legs);
		TestEqual(TEXT("Visible intermediate stops become 3D markers"), MultiStopLower.Stops.Num(), 1);
		if (!MultiStopLower.Stops.IsEmpty())
		{
			TestEqual(TEXT("3D stop marker uses one-based numbering"), MultiStopLower.Stops[0].Number, 1);
		}
		TestTrue(TEXT("3D view labels the intermediate stop"), MultiStopLower.Labels.ContainsByPredicate(
			[](const FNavigationDisplayLabel& Label) { return Label.Text == TEXT("Stop 1"); }));
		if (MultiStopLower.Segments.Num() == 2)
		{
			TestEqual(TEXT("First route segment belongs to the first leg"), MultiStopLower.Segments[0].RouteLegIndex, 0);
			TestEqual(TEXT("Floor transition belongs to the second leg"), MultiStopLower.Segments[1].RouteLegIndex, 1);
		}
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
