#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileMapWidget.h"
#include "MobileMockSource.h"
#include "MobileSheetFlow.h"

#include "Components/Button.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "Layout/Geometry.h"
#include "Misc/AutomationTest.h"

// The map's view: zoom (0.7 to 2, as in the prototype), pan, and the gestures that make them (drag, pinch, wheel) once Move is on.

namespace
{
	constexpr double W = 375.0;
	constexpr double H = 812.0;

	FGeometry ViewGeometry()
	{
		return FGeometry::MakeRoot(FVector2f(static_cast<float>(W), static_cast<float>(H)), FSlateLayoutTransform());
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
	Map->SetViewSize(W, H);
	Map->ShowLevel(TEXT("E2"), 1);

	// At the start the plan fits.
	TestTrue(TEXT("zoom 1 and no pan"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0) && Map->GetPan().IsNearlyZero());
	const FBox2D Fit = Map->GetPlanExtentInWidget(W, H);
	const FVector2D FitCentre = (Fit.Min + Fit.Max) * 0.5;

	// Zoom buttons: 0.2 a step, about the middle of what is uncovered, never past 0.7 and 2.
	Map->ZoomBy(UMobileMapWidget::ZoomStep);
	TestTrue(TEXT("a step in is 1.2"), FMath::IsNearlyEqual(Map->GetZoom(), 1.2));
	const FBox2D In = Map->GetPlanExtentInWidget(W, H);
	TestTrue(TEXT("the plan is 1.2 times as big"), FMath::IsNearlyEqual(In.GetSize().X, Fit.GetSize().X * 1.2, 0.5));
	TestTrue(TEXT("about the same middle"), ((In.Min + In.Max) * 0.5 - FitCentre).Size() < 0.5);
	for (int32 i = 0; i < 20; ++i)
	{
		Map->ZoomBy(UMobileMapWidget::ZoomStep);
	}
	TestTrue(TEXT("zoom stops at 2"), FMath::IsNearlyEqual(Map->GetZoom(), UMobileMapWidget::MaxZoom));
	for (int32 i = 0; i < 30; ++i)
	{
		Map->ZoomBy(-UMobileMapWidget::ZoomStep);
	}
	TestTrue(TEXT("and at 0.7"), FMath::IsNearlyEqual(Map->GetZoom(), UMobileMapWidget::MinZoom));
	Map->ResetView();
	TestTrue(TEXT("reset is the fit again"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0) && Map->GetPan().IsNearlyZero());

	// A zoom about a point keeps what is under it there.
	const FVector2D Study = RoomCentre(LevelOf(Source), TEXT("E2-01-101"));
	const FVector2D Under = Map->ToWidget(Study, W, H);
	Map->ZoomAround(Under, 1.6);
	TestTrue(TEXT("the point under the finger stays under it"), (Map->ToWidget(Study, W, H) - Under).Size() < 0.5);
	Map->ResetView();

	// A move moves the plan by that much; far too far is held back so some of it stays in view.
	Map->PanBy(FVector2D(20.0, -10.0));
	const FBox2D Moved = Map->GetPlanExtentInWidget(W, H);
	TestTrue(TEXT("moved by the pan"), ((Moved.Min - Fit.Min) - FVector2D(20.0, -10.0)).Size() < 0.5);
	Map->PanBy(FVector2D(5000.0, 5000.0));
	const FBox2D Far = Map->GetPlanExtentInWidget(W, H);
	TestTrue(TEXT("pushed off to the right and down, some of the plan is still in the uncovered part"), Far.Min.X < W - 10.0 && Far.Min.Y < (H - 300.0) - 10.0);
	Map->PanBy(FVector2D(-9000.0, -9000.0));
	const FBox2D Gone = Map->GetPlanExtentInWidget(W, H);
	TestTrue(TEXT("and off to the left and up"), Gone.Max.X > 10.0 && Gone.Max.Y > 64.0 + 10.0);
	Map->ResetView();

	// The view starts again for another level, the overview and the tilt, and stays for the same level.
	Map->ZoomBy(0.4);
	Map->ShowLevel(TEXT("E2"), 1);
	TestTrue(TEXT("the same level again keeps the zoom"), FMath::IsNearlyEqual(Map->GetZoom(), 1.4));
	Map->ShowLevel(TEXT("E2"), 2);
	TestTrue(TEXT("another level starts at the fit"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0));
	Map->ZoomBy(0.4);
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
	const FVector2D Study = Map->ToWidget(RoomCentre(LevelOf(Source), TEXT("E2-01-101")), W, H);

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
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	auto Click = [Screen](const TCHAR* Name) { Cast<UButton>(Screen->FindNamedWidget(Name))->OnClicked.Broadcast(); };
	UMobileMapWidget* Map = Screen->GetMapView();
	if (!TestNotNull(TEXT("the screen has the map"), Map)) return false;

	// More opens the tools; Move starts off; the zoom buttons zoom a step.
	TestFalse(TEXT("the tools are shut at first"), Screen->IsMapToolsOpen());
	TestEqual(TEXT("and not shown"), Screen->FindNamedWidget(TEXT("MapTools"))->GetVisibility(), ESlateVisibility::Collapsed);
	Click(TEXT("Btn_MapMore"));
	TestTrue(TEXT("More opens them"), Screen->IsMapToolsOpen());
	TestEqual(TEXT("and shows them"), Screen->FindNamedWidget(TEXT("MapTools"))->GetVisibility(), ESlateVisibility::Visible);
	TestFalse(TEXT("Move is off"), Screen->AreMapGesturesOn() || Map->AreGesturesEnabled());
	Click(TEXT("Btn_MapMove"));
	TestTrue(TEXT("Move turns the gestures on"), Screen->AreMapGesturesOn() && Map->AreGesturesEnabled());
	Click(TEXT("Btn_ZoomIn"));
	TestTrue(TEXT("zoom in is a step"), FMath::IsNearlyEqual(Map->GetZoom(), 1.2));
	Click(TEXT("Btn_ZoomOut"));
	Click(TEXT("Btn_ZoomOut"));
	TestTrue(TEXT("zoom out is a step the other way"), FMath::IsNearlyEqual(Map->GetZoom(), 0.8));
	Click(TEXT("Btn_ZoomOut"));
	TestFalse(TEXT("at 0.7 zoom out is off"), Cast<UButton>(Screen->FindNamedWidget(TEXT("Btn_ZoomOut")))->GetIsEnabled());
	for (int32 i = 0; i < 10; ++i)
	{
		Click(TEXT("Btn_ZoomIn"));
	}
	TestTrue(TEXT("zoom in stops at 2, and then is off"), FMath::IsNearlyEqual(Map->GetZoom(), 2.0) && !Cast<UButton>(Screen->FindNamedWidget(TEXT("Btn_ZoomIn")))->GetIsEnabled());

	// Locate puts the view back at the fit; the level list and the tools are not open together.
	Click(TEXT("Btn_Locate"));
	TestTrue(TEXT("Locate starts the view again"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0) && Map->GetPan().IsNearlyZero());
	Screen->SetLevelPopupOpen(true);
	TestFalse(TEXT("opening the levels shuts the tools"), Screen->IsMapToolsOpen());
	Click(TEXT("Btn_MapMore"));
	TestFalse(TEXT("and opening the tools shuts the levels"), Screen->IsLevelPopupOpen());

	// The controls are taller by the new button, and still stay clear of the top bar.
	Screen->ApplyViewportSize(375.0, 812.0);
	const FMobileLayoutSnapshot Layout = Screen->GetLayoutSnapshot();
	TestTrue(TEXT("the controls fit above the half-open sheet"), Layout.SheetTop - UMobileAppWidget::ControlsGap - UMobileAppWidget::ControlsHeight >= Layout.TopBarBottom);

	// Choosing another level starts the view again.
	Screen->ZoomMap(0.4);
	Screen->ChooseMapLevel(2);
	TestTrue(TEXT("another level, the fit"), FMath::IsNearlyEqual(Map->GetZoom(), 1.0));
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
