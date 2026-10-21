#if WITH_DEV_AUTOMATION_TESTS

#include "MobileAppWidget.h"
#include "MobileSheetFlow.h"

#include "Engine/World.h"
#include "Input/Events.h"
#include "Layout/Geometry.h"
#include "Misc/AutomationTest.h"

// The sheet's handle seen through pointer events. An Android emulator reports one click as a touch and, with it, the mouse cursor's moves
// (a different pointer); the sheet must follow the pointer that pressed and ignore the other.

namespace
{
	FPointerEvent Touch(const FVector2D& At, bool bPressed)
	{
		return FPointerEvent(0, 0, FVector2f(At), FVector2f(At), 1.f, bPressed);
	}

	FPointerEvent MouseMove(const FVector2D& At, bool bPressed)
	{
		TSet<FKey> Pressed;
		if (bPressed)
		{
			Pressed.Add(EKeys::LeftMouseButton);
		}
		return FPointerEvent(10, FVector2f(At), FVector2f(At), FVector2f::ZeroVector, Pressed, FModifierKeysState());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSheetPointerTest, "InNavi.Mobile.SheetPointer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileSheetPointerTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UMobileAppWidget* Screen = CreateWidget<UMobileAppWidget>(World, UMobileAppWidget::StaticClass());
	if (!TestNotNull(TEXT("the screen is created"), Screen)) return false;
	Screen->TakeWidget();
	Screen->SetActiveTab(UMobileAppWidget::ETab::Directory);
	Screen->ApplyViewportSize(375.0, 812.0);
	Screen->SetSnap(EMobileSheetSnap::Half);
	Screen->SnapSheetNow();
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(375.f, 812.f), FSlateLayoutTransform());
	const FMobileLayoutSnapshot Start = Screen->GetLayoutSnapshot();
	const FVector2D Handle(187.0, Start.SheetTop + 10.0);
	// Where the mouse cursor is: somewhere else (the other pointer's position need not match the finger's).
	const FVector2D Cursor(187.0, 40.0);

	// A tap on the handle with the cursor moving about: one tap, one step (half to full), and nothing is left pressed.
	Screen->HandlePointerDown(Geometry, Touch(Handle, true));
	TestTrue(TEXT("a touch on the handle starts a drag"), Screen->IsDraggingSheet());
	Screen->HandlePointerMove(Geometry, MouseMove(Cursor, false));
	Screen->HandlePointerMove(Geometry, MouseMove(Cursor + FVector2D(0.0, 5.0), false));
	Screen->HandlePointerUp(Geometry, Touch(Handle, false));
	TestFalse(TEXT("the touch ending ends it"), Screen->IsDraggingSheet());
	TestEqual(TEXT("the cursor's moves did not make it a drag: a tap goes to the next height"), Screen->GetSnap(), EMobileSheetSnap::Full);

	// The mouse's press (the same click, as the mouse) does not start a second drag or end the first.
	Screen->SetSnap(EMobileSheetSnap::Half);
	Screen->SnapSheetNow();
	Screen->HandlePointerDown(Geometry, Touch(Handle, true));
	Screen->HandlePointerDown(Geometry, MouseMove(Cursor, true));
	Screen->HandlePointerUp(Geometry, MouseMove(Cursor, false));
	TestTrue(TEXT("the mouse's release does not end the touch's drag"), Screen->IsDraggingSheet());
	Screen->HandlePointerUp(Geometry, Touch(Handle, false));
	TestFalse(TEXT("the touch's release does"), Screen->IsDraggingSheet());
	TestEqual(TEXT("as one tap"), Screen->GetSnap(), EMobileSheetSnap::Full);

	// A real drag by the touch still moves it, wherever the cursor is.
	Screen->SetSnap(EMobileSheetSnap::Half);
	Screen->SnapSheetNow();
	const double HalfHeight = Screen->GetSheetHeight();
	Screen->HandlePointerDown(Geometry, Touch(Handle, true));
	Screen->HandlePointerMove(Geometry, Touch(Handle - FVector2D(0.0, 120.0), true));
	Screen->HandlePointerMove(Geometry, MouseMove(Cursor, false));
	TestTrue(TEXT("the sheet follows the finger"), Screen->GetSheetHeight() > HalfHeight + 100.0);
	Screen->HandlePointerUp(Geometry, Touch(Handle - FVector2D(0.0, 120.0), false));

	// With the mouse alone (a desktop) the mouse is the pointer that presses, moves and releases.
	Screen->SetSnap(EMobileSheetSnap::Half);
	Screen->SnapSheetNow();
	Screen->HandlePointerDown(Geometry, MouseMove(Handle, true));
	TestTrue(TEXT("a mouse press on the handle starts a drag"), Screen->IsDraggingSheet());
	Screen->HandlePointerMove(Geometry, MouseMove(Handle - FVector2D(0.0, 120.0), true));
	TestTrue(TEXT("and its move moves the sheet"), Screen->GetSheetHeight() > HalfHeight + 100.0);
	Screen->HandlePointerUp(Geometry, MouseMove(Handle - FVector2D(0.0, 120.0), false));
	TestFalse(TEXT("its release ends it"), Screen->IsDraggingSheet());
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
