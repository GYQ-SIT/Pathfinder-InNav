#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileMapWidget.h"
#include "MobileMockSource.h"
#include "MobileSheetFlow.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "Layout/Geometry.h"
#include "Misc/AutomationTest.h"

// The map's view: zoom (0.7 to 6), pan, the turn of the whole map, and the gestures that make them (drag, pinch and twist, wheel) once Move is on.

namespace
{
	constexpr double MapViewW = 375.0;
	constexpr double MapViewH = 812.0;

	FGeometry ViewGeometry()
	{
		return FGeometry::MakeRoot(FVector2f(static_cast<float>(MapViewW), static_cast<float>(MapViewH)), FSlateLayoutTransform());
	}

	FPointerEvent Finger(int32 Index, const FVector2D& At, bool bPressed)
	{
		return FPointerEvent(0, Index, FVector2f(At), FVector2f(At), 1.f, bPressed);
	}

	FPointerEvent Mouse(const FVector2D& At, bool bPressed)
	{
		TSet<FKey> Pressed;
		if (bPressed)
		{
			Pressed.Add(EKeys::LeftMouseButton);
		}
		return FPointerEvent(10, FVector2f(At), FVector2f(At), FVector2f::ZeroVector, Pressed, FModifierKeysState());
	}

	FMobileLevelMap LevelOf(FMobileMockSource& Source)
	{
		FMobileLevelMap Map;
		Source.GetLevelMap(TEXT("E2"), 1, Map);
		return Map;
	}

	FVector2D RoomCentre(const FMobileLevelMap& Map, const FString& PlaceId)
	{
		for (const FMobileMapRoom& Room : Map.Rooms)
		{
			if (Room.PlaceId == PlaceId)
			{
				FVector2D Sum = FVector2D::ZeroVector;
				for (const FVector2D& P : Room.Polygon)
				{
					Sum += P;
				}
				return Sum / Room.Polygon.Num();
			}
		}
		return FVector2D::ZeroVector;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapZoomTest, "InNavi.Mobile.MapZoom", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapZoomTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobileMapWidget* Map = CreateWidget<UMobileMapWidget>(World, UMobileMapWidget::StaticClass());
	if (!TestNotNull(TEXT("the map is created"), Map)) return false;
	Map->TakeWidget();
	Map->SetSource(&Source, TEXT("sit-punggol"));
	Map->SetFitInsets(64.0, 300.0);
	Map->SetViewSize(MapViewW, MapViewH);
	Map->ShowLevel(TEXT("E2"), 1);

	// At the start the plan fits.
	TestTrue(TEXT("zoom 1 and no pan"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0) && Map->GetPan().IsNearlyZero());
	const FBox2D Fit = Map->GetPlanExtentInWidget(MapViewW, MapViewH);
	const FVector2D FitCentre = (Fit.Min + Fit.Max) * 0.5;

	// Zoom buttons: 0.2 a step, about the middle of what is uncovered, never past 0.7 and 6.
	Map->ZoomBy(UMobileMapWidget::ZoomStep);
	TestTrue(TEXT("a step in is 1.2"), FMath::IsNearlyEqual(Map->GetZoom(), 1.2));
	const FBox2D In = Map->GetPlanExtentInWidget(MapViewW, MapViewH);
	TestTrue(TEXT("the plan is 1.2 times as big"), FMath::IsNearlyEqual(In.GetSize().X, Fit.GetSize().X * 1.2, 0.5));
	TestTrue(TEXT("about the same middle"), ((In.Min + In.Max) * 0.5 - FitCentre).Size() < 0.5);
	for (int32 i = 0; i < 40; ++i)
	{
		Map->ZoomBy(UMobileMapWidget::ZoomStep);
	}
	TestTrue(TEXT("zoom stops at the largest, 6 (a room fills the map)"), FMath::IsNearlyEqual(Map->GetZoom(), UMobileMapWidget::MaxZoom) && UMobileMapWidget::MaxZoom >= 6.0);
	for (int32 i = 0; i < 60; ++i)
	{
		Map->ZoomBy(-UMobileMapWidget::ZoomStep);
	}
	TestTrue(TEXT("and at 0.7"), FMath::IsNearlyEqual(Map->GetZoom(), UMobileMapWidget::MinZoom));
	Map->ResetView();
	TestTrue(TEXT("reset is the fit again"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0) && Map->GetPan().IsNearlyZero());

	// A zoom about a point keeps what is under it there.
	const FVector2D Study = RoomCentre(LevelOf(Source), TEXT("E2-01-101"));
	const FVector2D Under = Map->ToWidget(Study, MapViewW, MapViewH);
	Map->ZoomAround(Under, 1.6);
	TestTrue(TEXT("the point under the finger stays under it"), (Map->ToWidget(Study, MapViewW, MapViewH) - Under).Size() < 0.5);
	Map->ResetView();

	// A move moves the plan by that much; far too far is held back so some of it stays in view.
	Map->PanBy(FVector2D(20.0, -10.0));
	const FBox2D Moved = Map->GetPlanExtentInWidget(MapViewW, MapViewH);
	TestTrue(TEXT("moved by the pan"), ((Moved.Min - Fit.Min) - FVector2D(20.0, -10.0)).Size() < 0.5);
	Map->PanBy(FVector2D(5000.0, 5000.0));
	const FBox2D Far = Map->GetPlanExtentInWidget(MapViewW, MapViewH);
	TestTrue(TEXT("pushed off to the right and down, some of the plan is still in the uncovered part"), Far.Min.X < MapViewW - 10.0 && Far.Min.Y < (MapViewH - 300.0) - 10.0);
	Map->PanBy(FVector2D(-9000.0, -9000.0));
	const FBox2D Gone = Map->GetPlanExtentInWidget(MapViewW, MapViewH);
	TestTrue(TEXT("and off to the left and up"), Gone.Max.X > 10.0 && Gone.Max.Y > 64.0 + 10.0);
	Map->ResetView();

	// The view starts again for the overview and the tilt, and stays for the same level and for another level of the same building (the person keeps their place when they change floor).
	Map->ZoomBy(0.4);
	Map->ShowLevel(TEXT("E2"), 1);
	TestTrue(TEXT("the same level again keeps the zoom"), FMath::IsNearlyEqual(Map->GetZoom(), 1.4));
	Map->ShowLevel(TEXT("E2"), 2);
	TestTrue(TEXT("another level of the same building keeps the zoom"), FMath::IsNearlyEqual(Map->GetZoom(), 1.4));
	Map->ShowOverview();
	TestTrue(TEXT("the overview too"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0));
	Map->ShowLevel(TEXT("E2"), 2);
	Map->ZoomBy(0.4);
	Map->Set3D(true);
	TestTrue(TEXT("and the tilt"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0));
	Map->Set3D(true);
	Map->ZoomBy(0.4);
	Map->Set3D(true);
	TestTrue(TEXT("the same tilt again keeps it"), FMath::IsNearlyEqual(Map->GetZoom(), 1.4));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapGesturesTest, "InNavi.Mobile.MapGestures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapGesturesTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobileMapWidget* Map = CreateWidget<UMobileMapWidget>(World, UMobileMapWidget::StaticClass());
	if (!TestNotNull(TEXT("the map is created"), Map)) return false;
	Map->TakeWidget();
	Map->SetSource(&Source, TEXT("sit-punggol"));
	Map->SetFitInsets(64.0, 300.0);
	Map->ShowLevel(TEXT("E2"), 1);
	const FGeometry Geometry = ViewGeometry();
	FString Tapped;
	Map->SetOnPlaceTapped([&Tapped](const FString& Id) { Tapped = Id; });
	const FVector2D Study = Map->ToWidget(RoomCentre(LevelOf(Source), TEXT("E2-01-101")), MapViewW, MapViewH);

	// A tap on a room picks it, with gestures off or on.
	Map->HandlePointerDown(Geometry, Finger(0, Study, true));
	Map->HandlePointerUp(Geometry, Finger(0, Study, false));
	TestEqual(TEXT("a tap picks the room"), Tapped, FString(TEXT("E2-01-101")));

	// With gestures off a drag moves nothing, and is not a tap either.
	Tapped.Empty();
	Map->HandlePointerDown(Geometry, Finger(0, Study, true));
	Map->HandlePointerMove(Geometry, Finger(0, Study + FVector2D(40.0, 30.0), true));
	Map->HandlePointerUp(Geometry, Finger(0, Study + FVector2D(40.0, 30.0), false));
	TestTrue(TEXT("gestures off: the view does not move"), Map->GetPan().IsNearlyZero() && FMath::IsNearlyEqual(Map->GetZoom(), 1.0));
	TestTrue(TEXT("and a drag is not a tap"), Tapped.IsEmpty());

	// With gestures on a drag moves the view, and still is not a tap.
	Map->SetGesturesEnabled(true);
	Map->HandlePointerDown(Geometry, Finger(0, Study, true));
	Map->HandlePointerMove(Geometry, Finger(0, Study + FVector2D(20.0, 0.0), true));
	Map->HandlePointerMove(Geometry, Finger(0, Study + FVector2D(40.0, 30.0), true));
	Map->HandlePointerUp(Geometry, Finger(0, Study + FVector2D(40.0, 30.0), false));
	TestTrue(TEXT("a drag moves the view by as much"), (Map->GetPan() - FVector2D(40.0, 30.0)).Size() < 0.5);
	TestTrue(TEXT("and picks nothing"), Tapped.IsEmpty());
	Map->ResetView();

	// The mouse cursor's moves that come with a touch (another pointer, not pressed) are not followed.
	Map->HandlePointerDown(Geometry, Finger(0, Study, true));
	TestTrue(TEXT("a hover is not followed"), !Map->HandlePointerMove(Geometry, Mouse(Study + FVector2D(100.0, 100.0), false)).IsEventHandled());
	Map->HandlePointerMove(Geometry, Finger(0, Study + FVector2D(10.0, 0.0), true));
	TestTrue(TEXT("only the finger moves the view"), (Map->GetPan() - FVector2D(10.0, 0.0)).Size() < 0.5);
	Map->HandlePointerUp(Geometry, Finger(0, Study + FVector2D(10.0, 0.0), false));
	Map->ResetView();

	// Two fingers: apart is in, together is out, and where their middle goes moves the view.
	const FVector2D A(150.0, 300.0), B(250.0, 300.0);
	Map->HandlePointerDown(Geometry, Finger(0, A, true));
	Map->HandlePointerDown(Geometry, Finger(1, B, true));
	Map->HandlePointerMove(Geometry, Finger(1, B + FVector2D(50.0, 0.0), true));
	TestTrue(TEXT("a pinch out zooms in"), Map->GetZoom() > 1.3);
	Map->HandlePointerMove(Geometry, Finger(1, B - FVector2D(50.0, 0.0), true));
	TestTrue(TEXT("and back zooms out"), Map->GetZoom() < 1.1);
	Tapped.Empty();
	Map->HandlePointerUp(Geometry, Finger(1, B, false));
	Map->HandlePointerUp(Geometry, Finger(0, A, false));
	TestTrue(TEXT("a pinch is not a tap"), Tapped.IsEmpty());
	Map->ResetView();

	// The wheel zooms (a desktop), when gestures are on, about the pointer.
	Map->HandleWheel(Geometry, FPointerEvent(10, FVector2f(Study), FVector2f(Study), TSet<FKey>(), EKeys::MouseScrollUp, 3.f, FModifierKeysState()));
	TestTrue(TEXT("the wheel zooms in"), Map->GetZoom() > 1.2);
	Map->SetGesturesEnabled(false);
	Map->ResetView();
	Map->HandleWheel(Geometry, FPointerEvent(10, FVector2f(Study), FVector2f(Study), TSet<FKey>(), EKeys::MouseScrollUp, 3.f, FModifierKeysState()));
	TestTrue(TEXT("but not with gestures off"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0));

	// A mouse alone (a desktop) presses, drags and lets go.
	Map->SetGesturesEnabled(true);
	Map->HandlePointerDown(Geometry, Mouse(Study, true));
	Map->HandlePointerMove(Geometry, Mouse(Study + FVector2D(25.0, 0.0), true));
	Map->HandlePointerUp(Geometry, Mouse(Study + FVector2D(25.0, 0.0), false));
	TestTrue(TEXT("a mouse drag moves the view"), (Map->GetPan() - FVector2D(25.0, 0.0)).Size() < 0.5);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapToolsTest, "InNavi.Mobile.MapTools", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapToolsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (Screen) Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	auto Click = [Screen](const TCHAR* Name) { Cast<UButton>(Screen->FindNamedWidget(Name))->OnClicked.Broadcast(); };
	UMobileMapWidget* Map = Screen->GetMapView();
	if (!TestNotNull(TEXT("the screen has the map"), Map)) return false;

	// As in the prototype's second iteration, the map has no tools: no More, no Move switch, no zoom buttons. A drag moves it from the start, and
	// a pinch or the wheel zooms it.
	for (const TCHAR* Gone : { TEXT("Btn_MapMore"), TEXT("Btn_MapMove"), TEXT("Btn_ZoomIn"), TEXT("Btn_ZoomOut"), TEXT("MapTools") })
	{
		TestNull(*FString::Printf(TEXT("%s is gone"), Gone), Screen->FindNamedWidget(Gone));
	}
	TestTrue(TEXT("a drag moves the map from the start"), Map->AreGesturesEnabled());

	// Zooming stays in its range.
	Screen->ZoomMap(UMobileMapWidget::ZoomStep);
	TestTrue(TEXT("zoom in is a step"), FMath::IsNearlyEqual(Map->GetZoom(), 1.2));
	Screen->ZoomMap(-2.0 * UMobileMapWidget::ZoomStep);
	TestTrue(TEXT("zoom out is a step the other way"), FMath::IsNearlyEqual(Map->GetZoom(), 0.8));
	Screen->ZoomMap(-10.0);
	TestTrue(TEXT("zoom out stops at 0.7"), FMath::IsNearlyEqual(Map->GetZoom(), UMobileMapWidget::MinZoom));
	Screen->ZoomMap(10.0);
	TestTrue(TEXT("zoom in stops at the largest"), FMath::IsNearlyEqual(Map->GetZoom(), UMobileMapWidget::MaxZoom));

	// Locate puts the view back at the fit.
	Click(TEXT("Btn_Locate"));
	TestTrue(TEXT("Locate starts the view again"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0) && Map->GetPan().IsNearlyZero());

	// The controls stay clear of the top bar.
	Screen->ApplyViewportSize(375.0, 812.0);
	const FMobileLayoutSnapshot Layout = Screen->GetLayoutSnapshot();
	TestTrue(TEXT("the controls fit above the half-open sheet"), Layout.SheetTop - UMobileAppWidget::ControlsGap - UMobileAppWidget::ControlsHeight >= Layout.TopBarBottom);

	// Choosing another level of the building keeps the view.
	Screen->ZoomMap(0.4);
	const double ZoomBefore = Map->GetZoom();
	Screen->ChooseMapLevel(2);
	TestTrue(TEXT("another level, the same zoom"), FMath::IsNearlyEqual(Map->GetZoom(), ZoomBefore));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapRotationTest, "InNavi.Mobile.MapRotation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapRotationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FMobileMockSource Source;
	UMobileMapWidget* Map = CreateWidget<UMobileMapWidget>(World, UMobileMapWidget::StaticClass());
	if (!TestNotNull(TEXT("the map is created"), Map)) return false;
	Map->TakeWidget();
	Map->SetSource(&Source, TEXT("sit-punggol"));
	Map->SetFitInsets(64.0, 300.0);
	Map->SetViewSize(MapViewW, MapViewH);
	Map->ShowLevel(TEXT("E2"), 1);
	Map->SetGesturesEnabled(true);
	const FGeometry Geometry = ViewGeometry();
	const FMobileLevelMap Level = LevelOf(Source);
	const FVector2D Study = RoomCentre(Level, TEXT("E2-01-101"));
	TArray<double> Told;
	Map->SetOnRotationChanged([&Told](double R) { Told.Add(R); });

	// Unturned, then turned a quarter: the plan turns about its own middle, so the middle stays put, other points go round it, and the size of the fit does not change.
	TestTrue(TEXT("the map starts unturned"), FMath::IsNearlyZero(Map->GetRotation()));
	const FBox2D Flat = Map->GetPlanExtentInWidget(MapViewW, MapViewH);
	FBox2D Raw(ForceInit);
	for (const FVector2D& P : Level.Outline) Raw += P;
	const FVector2D Middle = Raw.GetCenter();
	const FVector2D MiddleBefore = Map->ToWidget(Middle, MapViewW, MapViewH);
	const FVector2D StudyBefore = Map->ToWidget(Study, MapViewW, MapViewH);
	Map->SetRotation(UE_DOUBLE_HALF_PI);
	TestTrue(TEXT("the turn is told"), Told.Num() == 1 && FMath::IsNearlyEqual(Told.Last(), UE_DOUBLE_HALF_PI));
	TestTrue(TEXT("the middle of the plan stays where it is"), (Map->ToWidget(Middle, MapViewW, MapViewH) - MiddleBefore).Size() < 0.01);
	const FVector2D StudyAfter = Map->ToWidget(Study, MapViewW, MapViewH);
	const FVector2D Before = StudyBefore - MiddleBefore;
	const FVector2D After = StudyAfter - MiddleBefore;
	TestTrue(TEXT("a point goes a quarter turn clockwise about it, at the same distance"), FMath::IsNearlyEqual(Before.Size(), After.Size(), 0.01)
		&& (After - FVector2D(-Before.Y, Before.X)).Size() < 0.01);
	TestTrue(TEXT("the fit does not change with the turn, so it does not zoom"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0));
	TestTrue(TEXT("a plan wider than it is high is now higher than it is wide"), (Flat.GetSize().X > Flat.GetSize().Y) == (Map->GetPlanExtentInWidget(MapViewW, MapViewH).GetSize().X < Map->GetPlanExtentInWidget(MapViewW, MapViewH).GetSize().Y));

	// Taps still find the room where it is drawn now.
	FString Tapped;
	Map->SetOnPlaceTapped([&Tapped](const FString& Id) { Tapped = Id; });
	Map->HandlePointerDown(Geometry, Finger(0, StudyAfter, true));
	Map->HandlePointerUp(Geometry, Finger(0, StudyAfter, false));
	TestEqual(TEXT("a tap on the room where it is now picks it"), Tapped, FString(TEXT("E2-01-101")));

	// The turn stays for another level and for reset view; the compass (ResetRotation) puts it back.
	Map->ShowLevel(TEXT("E2"), 2);
	Map->ResetView();
	TestTrue(TEXT("another level and a reset of the view keep the turn"), FMath::IsNearlyEqual(Map->GetRotation(), UE_DOUBLE_HALF_PI));
	Map->ResetRotation();
	TestTrue(TEXT("the compass turns it back"), FMath::IsNearlyZero(Map->GetRotation()) && FMath::IsNearlyZero(Told.Last()));
	Map->ShowLevel(TEXT("E2"), 1);

	// A turn is kept within a half turn either way.
	Map->SetRotation(3.0 * UE_DOUBLE_PI);
	TestTrue(TEXT("three half turns is one"), FMath::IsNearlyEqual(FMath::Abs(Map->GetRotation()), UE_DOUBLE_PI, 1e-6));
	Map->ResetRotation();

	// What is under the point stays under it while the map turns about it (flat view).
	const FVector2D Under = Map->ToWidget(Study, MapViewW, MapViewH);
	Map->RotateAround(Under, 0.6);
	TestTrue(TEXT("turning about a point keeps what is under it there"), (Map->ToWidget(Study, MapViewW, MapViewH) - Under).Size() < 0.01);
	Map->ResetRotation();
	Map->ResetView();

	// A mouse: Shift and a drag turn the map about the middle of the view; a drag without Shift moves it.
	const FVector2D Centre(MapViewW * 0.5, 64.0 + (MapViewH - 64.0 - 300.0) * 0.5);
	const FModifierKeysState Shift(true, false, false, false, false, false, false, false, false);
	auto ShiftMouse = [&Shift](const FVector2D& At, bool bPressed)
	{
		TSet<FKey> Pressed;
		if (bPressed) Pressed.Add(EKeys::LeftMouseButton);
		return FPointerEvent(10, FVector2f(At), FVector2f(At), FVector2f::ZeroVector, Pressed, Shift);
	};
	Map->HandlePointerDown(Geometry, ShiftMouse(Centre + FVector2D(100.0, 0.0), true));
	Map->HandlePointerMove(Geometry, ShiftMouse(Centre + FVector2D(100.0, 20.0), true));
	Map->HandlePointerMove(Geometry, ShiftMouse(Centre + FVector2D(0.0, 100.0), true));
	Map->HandlePointerUp(Geometry, ShiftMouse(Centre + FVector2D(0.0, 100.0), false));
	TestTrue(TEXT("a quarter of the way round the middle turns the map a quarter, clockwise"), FMath::IsNearlyEqual(Map->GetRotation(), UE_DOUBLE_HALF_PI, 0.02));
	TestTrue(TEXT("and does not move it"), Map->GetPan().Size() < 1.0);
	Map->ResetRotation();

	// Two fingers: a small twist is a pinch that wobbles and turns nothing; a real twist turns the map, and a pinch still zooms.
	const FVector2D A(150.0, 300.0), B(250.0, 300.0);
	Map->HandlePointerDown(Geometry, Finger(0, A, true));
	Map->HandlePointerDown(Geometry, Finger(1, B, true));
	const double SmallTwist = FMath::DegreesToRadians(4.0);
	Map->HandlePointerMove(Geometry, Finger(1, A + FVector2D(100.0 * FMath::Cos(SmallTwist), 100.0 * FMath::Sin(SmallTwist)), true));
	TestTrue(TEXT("a twist of a few degrees turns nothing"), FMath::IsNearlyZero(Map->GetRotation()));
	for (int32 Step = 1; Step <= 8; ++Step)
	{
		const double Angle = FMath::DegreesToRadians(4.0 + 6.0 * Step);
		Map->HandlePointerMove(Geometry, Finger(1, A + FVector2D(100.0 * FMath::Cos(Angle), 100.0 * FMath::Sin(Angle)), true));
	}
	const double Wanted = FMath::DegreesToRadians(52.0);
	TestTrue(TEXT("a real twist turns the map, clockwise, from where it passed the few degrees"), Map->GetRotation() > FMath::DegreesToRadians(35.0) && Map->GetRotation() < Wanted);
	TestTrue(TEXT("and the distance between the fingers was the same, so no zoom"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0, 0.02));
	Map->HandlePointerUp(Geometry, Finger(1, B, false));
	Map->HandlePointerUp(Geometry, Finger(0, A, false));
	Map->ResetRotation();

	// The tilted view turns too, and does not break.
	Map->Set3D(true);
	Map->RotateAround(Centre, 0.5);
	TestTrue(TEXT("the tilted view turns"), FMath::IsNearlyEqual(Map->GetRotation(), 0.5, 1e-6));
	TestTrue(TEXT("and the plan is still in view"), Map->GetPlanExtentInWidget(MapViewW, MapViewH).bIsValid);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapCompassTest, "InNavi.Mobile.MapCompass", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapCompassTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (Screen) Screen->SetSource(MakeShared<FMobileMockSource>(), TEXT("sit-punggol"));
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	UMobileMapWidget* Map = Screen->GetMapView();
	UButton* Compass = Cast<UButton>(Screen->FindNamedWidget(TEXT("Btn_Compass")));
	if (!TestNotNull(TEXT("the map is there"), Map) || !TestNotNull(TEXT("and so is the compass button"), Compass)) return false;

	TestEqual(TEXT("the compass is hidden while the map is as drawn"), Compass->GetVisibility(), ESlateVisibility::Collapsed);
	Map->SetRotation(FMath::DegreesToRadians(40.0));
	TestEqual(TEXT("it shows once the map is turned"), Compass->GetVisibility(), ESlateVisibility::Visible);
	Map->SetRotation(FMath::DegreesToRadians(0.5));
	TestEqual(TEXT("a turn of under a degree does not count"), Compass->GetVisibility(), ESlateVisibility::Collapsed);
	Map->SetRotation(FMath::DegreesToRadians(-70.0));
	TestEqual(TEXT("either way round"), Compass->GetVisibility(), ESlateVisibility::Visible);
	Compass->OnClicked.Broadcast();
	TestTrue(TEXT("a tap on it turns the map back"), FMath::IsNearlyZero(Map->GetRotation()));
	TestEqual(TEXT("and it goes away"), Compass->GetVisibility(), ESlateVisibility::Collapsed);
	World->DestroyWorld(false);
	return true;
}

#endif
