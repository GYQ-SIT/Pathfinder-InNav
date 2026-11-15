#include "MobileReorderHandle.h"

#include "MobileIcons.h"
#include "MobileStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Input/Events.h"

using namespace MobileStyle;

namespace
{
	// A press that moves less than this (design pixels) is a tap, not a drag.
	constexpr float ReorderTapSlop = 4.f;
}

TSharedRef<SWidget> UMobileReorderHandle::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		// 28 wide and 40 tall, as the prototype's grip; the icon is three bars (the design team's Reorder).
		UBorder* Face = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ReorderFace"));
		Face->SetBrushColor(FLinearColor::Transparent);
		Face->SetHorizontalAlignment(HAlign_Center);
		Face->SetVerticalAlignment(VAlign_Center);
		Face->SetVisibility(ESlateVisibility::Visible);
		UWidget* Icon = MobileIcons::Picture(Tree, TEXT("Reorder"), 20.f, Grey);
		Face->SetContent(Icon ? Icon : static_cast<UWidget*>(MakeText(Tree, TEXT("Txt_ReorderBars"), TEXT("≡"), 22.f, Grey, true)));
		USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ReorderBox"));
		Box->SetWidthOverride(28.f);
		Box->SetHeightOverride(40.f);
		Box->SetContent(Face);
		Tree->RootWidget = Box;
	}
	return Super::RebuildWidget();
}

void UMobileReorderHandle::SimulateDrag(float DeltaY)
{
	if (OnDragged) OnDragged(DeltaY);
	if (OnDropped) OnDropped(DeltaY);
}

bool UMobileReorderHandle::IsMine(const FPointerEvent& Event) const
{
	return bDragging && Event.GetPointerIndex() == PointerIndex && Event.IsTouchEvent() == bDragIsTouch;
}

float UMobileReorderHandle::DeltaOf(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	// The pointer's screen position, in design pixels: the screen is laid out in a space the scale box blows up, and the geometry knows by how much.
	const float Scale = FMath::Max(Geometry.GetAccumulatedLayoutTransform().GetScale(), 0.01f);
	return (Event.GetScreenSpacePosition().Y - StartY) / Scale;
}

FReply UMobileReorderHandle::Begin(const FPointerEvent& Event)
{
	if (bDragging)
	{
		return FReply::Handled();
	}
	bDragging = true;
	bMoved = false;
	bDragIsTouch = Event.IsTouchEvent();
	PointerIndex = Event.GetPointerIndex();
	StartY = Event.GetScreenSpacePosition().Y;
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UMobileReorderHandle::Move(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!IsMine(Event))
	{
		return FReply::Unhandled();
	}
	const float Delta = DeltaOf(Geometry, Event);
	if (!bMoved && FMath::Abs(Delta) < ReorderTapSlop)
	{
		return FReply::Handled();
	}
	bMoved = true;
	if (OnDragged)
	{
		OnDragged(Delta);
	}
	return FReply::Handled();
}

FReply UMobileReorderHandle::End(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!IsMine(Event))
	{
		return FReply::Unhandled();
	}
	const float Delta = DeltaOf(Geometry, Event);
	const bool bWasMoved = bMoved;
	bDragging = false;
	bMoved = false;
	PointerIndex = INDEX_NONE;
	if (bWasMoved && OnDropped)
	{
		OnDropped(Delta);
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply UMobileReorderHandle::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton ? Begin(InMouseEvent) : FReply::Unhandled();
}

FReply UMobileReorderHandle::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return Move(InGeometry, InMouseEvent);
}

FReply UMobileReorderHandle::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return End(InGeometry, InMouseEvent);
}

FReply UMobileReorderHandle::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	return Begin(InGestureEvent);
}

FReply UMobileReorderHandle::NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	return Move(InGeometry, InGestureEvent);
}

FReply UMobileReorderHandle::NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	return End(InGeometry, InGestureEvent);
}
