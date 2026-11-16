#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MobileReorderHandle.generated.h"

// The handle of a row that can be moved up or down (the grip at the right of a stop on the route page): the person puts a finger (or the mouse) on it and
// drags up or down. It reports how far the pointer has gone, in design pixels, while it goes (Dragged) and when it is let go (Dropped); the page decides
// what that means (a row is 56 tall, so 56 is one place). A press that does not move reports nothing. Only the pointer that pressed on it is followed: a
// phone can report both a touch and the mouse cursor for one press (the same rule as for the sheet's handle).
UCLASS()
class INNAVI_API UMobileReorderHandle : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetOnDragged(TFunction<void(float)> InCallback) { OnDragged = MoveTemp(InCallback); }
	void SetOnDropped(TFunction<void(float)> InCallback) { OnDropped = MoveTemp(InCallback); }
	// What a drag of DeltaY (down is plus) does, for the tests: it goes, then it is let go.
	void SimulateDrag(float DeltaY);
	bool IsDragging() const { return bDragging; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;

private:
	FReply Begin(const FPointerEvent& Event);
	FReply Move(const FGeometry& Geometry, const FPointerEvent& Event);
	FReply End(const FGeometry& Geometry, const FPointerEvent& Event);
	float DeltaOf(const FGeometry& Geometry, const FPointerEvent& Event) const;
	bool IsMine(const FPointerEvent& Event) const;

	bool bDragging = false;
	bool bMoved = false;
	bool bDragIsTouch = false;
	int32 PointerIndex = INDEX_NONE;
	float StartY = 0.f;

	TFunction<void(float)> OnDragged;
	TFunction<void(float)> OnDropped;
};
