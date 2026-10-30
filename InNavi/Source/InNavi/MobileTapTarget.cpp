#include "MobileTapTarget.h"

#include "MobileStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"

TSharedRef<SWidget> UMobileTapTarget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("TapButton"));
		Button->SetStyle(StartStyle::Button(FLinearColor::Transparent, FLinearColor(0.f, 0.f, 0.f, 0.04f), MobileStyle::BlueSoft, FLinearColor::Transparent, 0.f));
		// Rows sit in lists that scroll. A button that takes hold of the finger the moment it touches never lets the list scroll; a precise tap
		// clicks only if the finger lifts where it went down, and a drag goes to the scroll box.
		Button->SetTouchMethod(EButtonTouchMethod::PreciseTap);
		Button->OnClicked.AddUniqueDynamic(this, &UMobileTapTarget::HandleClicked);
		WidgetTree->RootWidget = Button;
	}
	return Super::RebuildWidget();
}

void UMobileTapTarget::Setup(UWidget* Content, const FString& InPayload, const FLinearColor& Fill, float CornerRadius)
{
	Payload = InPayload;
	TakeWidget();
	if (Button)
	{
		const FLinearColor Pressed = Fill.A > 0.f ? Fill * 0.9f : MobileStyle::BlueSoft;
		const FLinearColor Hover = Fill.A > 0.f ? Fill * 0.96f : MobileStyle::TapHover;
		Button->SetStyle(StartStyle::Button(Fill, Hover, Pressed, FLinearColor::Transparent, CornerRadius / StartStyle::DesignToUi));
		Button->SetBackgroundColor(FLinearColor::White);
		if (UPanelSlot* ContentSlot = Button->SetContent(Content))
		{
			// The content fills the button, so a row can put its text on the left and its level on the right.
			if (UButtonSlot* AsButtonSlot = Cast<UButtonSlot>(ContentSlot))
			{
				AsButtonSlot->SetHorizontalAlignment(HAlign_Fill);
				AsButtonSlot->SetVerticalAlignment(VAlign_Fill);
			}
		}
	}
}

void UMobileTapTarget::HandleClicked()
{
	Tap();
}

bool UMobileTapTarget::IsScrollFriendly() const
{
	return Button && Button->GetTouchMethod() == EButtonTouchMethod::PreciseTap;
}

void UMobileTapTarget::Tap()
{
	if (OnTapped)
	{
		OnTapped(Payload);
	}
}
