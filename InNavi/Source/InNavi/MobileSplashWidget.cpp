#include "MobileSplashWidget.h"

#include "MobileAppWidget.h"
#include "MobileStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	// 0 to 1, soft at both ends.
	float SplashSoftStep(double X)
	{
		X = FMath::Clamp(X, 0.0, 1.0);
		return static_cast<float>(X * X * (3.0 - 2.0 * X));
	}

	// A rounded box with only an outline: the logo mark of the start screen.
	FSlateBrush SplashOutline(const FLinearColor& Colour, float Radius, float SplashWidth)
	{
		return MobileStyle::Rounded(FLinearColor::Transparent, Colour, Radius, SplashWidth);
	}
}

float UMobileSplashWidget::LogoOpacity(double Seconds)
{
	return SplashSoftStep((Seconds - BlackSeconds) / FadeInSeconds);
}

float UMobileSplashWidget::CoverOpacity(double Seconds, double LeaveSeconds)
{
	if (LeaveSeconds < 0.0)
	{
		return 1.f;
	}
	return 1.f - SplashSoftStep((Seconds - LeaveSeconds) / FadeOutSeconds);
}

TSharedRef<SWidget> UMobileSplashWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		UOverlay* SplashRoot = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SplashRoot"));
		Tree->RootWidget = SplashRoot;

		// The black. It is a hit-testable widget, so that no touch goes through to what is under the splash.
		UBorder* SplashBlack = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SplashBlack"));
		SplashBlack->SetBrushColor(FLinearColor::Black);
		SplashBlack->SetVisibility(ESlateVisibility::Visible);
		UOverlaySlot* SplashBlackSlot = SplashRoot->AddChildToOverlay(SplashBlack);
		SplashBlackSlot->SetHorizontalAlignment(HAlign_Fill);
		SplashBlackSlot->SetVerticalAlignment(VAlign_Fill);

		// The logo: the blue outlined square with the two-column symbol of the start screen, and the name under it.
		const FLinearColor LogoBlue = StartStyle::Rgb(52, 142, 255);
		USizeBox* SplashMarkSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SplashMarkSize"));
		SplashMarkSize->SetWidthOverride(88.f);
		SplashMarkSize->SetHeightOverride(88.f);
		UBorder* SplashMarkFrame = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SplashMark"));
		SplashMarkFrame->SetBrush(SplashOutline(LogoBlue, 20.f, 2.5f));
		SplashMarkFrame->SetBrushColor(FLinearColor::White);
		SplashMarkFrame->SetHorizontalAlignment(HAlign_Center);
		SplashMarkFrame->SetVerticalAlignment(VAlign_Center);
		USizeBox* SplashSymbolSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SplashSymbolSize"));
		SplashSymbolSize->SetWidthOverride(40.f);
		SplashSymbolSize->SetHeightOverride(40.f);
		UOverlay* SplashSymbol = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SplashSymbol"));
		UBorder* SplashSymbolBox = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SplashSymbolBox"));
		SplashSymbolBox->SetBrush(SplashOutline(LogoBlue, 4.f, 3.5f));
		SplashSymbolBox->SetBrushColor(FLinearColor::White);
		UOverlaySlot* SplashSymbolBoxSlot = SplashSymbol->AddChildToOverlay(SplashSymbolBox);
		SplashSymbolBoxSlot->SetHorizontalAlignment(HAlign_Fill);
		SplashSymbolBoxSlot->SetVerticalAlignment(VAlign_Fill);
		USizeBox* SplashLineSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SplashSymbolLineSize"));
		SplashLineSize->SetWidthOverride(3.5f);
		UBorder* SplashLine = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SplashSymbolLine"));
		SplashLine->SetBrushColor(LogoBlue);
		SplashLineSize->SetContent(SplashLine);
		UOverlaySlot* SplashLineSlot = SplashSymbol->AddChildToOverlay(SplashLineSize);
		SplashLineSlot->SetHorizontalAlignment(HAlign_Center);
		SplashLineSlot->SetVerticalAlignment(VAlign_Fill);
		SplashSymbolSize->SetContent(SplashSymbol);
		SplashMarkFrame->SetContent(SplashSymbolSize);
		SplashMarkSize->SetContent(SplashMarkFrame);

		UHorizontalBox* SplashName = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SplashName"));
		SplashName->AddChildToHorizontalBox(MobileStyle::MakeText(Tree, TEXT("Txt_SplashIn"), TEXT("In"), 40.f, FLinearColor::White, true));
		SplashName->AddChildToHorizontalBox(MobileStyle::MakeText(Tree, TEXT("Txt_SplashNav"), TEXT("Nav"), 40.f, StartStyle::Rgb(92, 172, 255), true));

		UVerticalBox* SplashGroup = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SplashLogo"));
		UVerticalBoxSlot* SplashMarkSlot = SplashGroup->AddChildToVerticalBox(SplashMarkSize);
		SplashMarkSlot->SetHorizontalAlignment(HAlign_Center);
		SplashMarkSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));
		SplashGroup->AddChildToVerticalBox(SplashName)->SetHorizontalAlignment(HAlign_Center);
		LogoGroup = SplashGroup;

		// Laid out in a space 375 wide, and blown up to the window's width (NativeTick), as the app is.
		USizeBox* SplashWidth = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SplashWidth"));
		SplashWidth->SetWidthOverride(static_cast<float>(UMobileAppWidget::DesignWidth));
		SplashWidth->SetContent(SplashGroup);
		LogoScale = Tree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("SplashScale"));
		LogoScale->SetStretch(EStretch::UserSpecified);
		LogoScale->SetUserSpecifiedScale(1.f);
		LogoScale->SetContent(SplashWidth);
		UOverlaySlot* SplashScaleSlot = SplashRoot->AddChildToOverlay(LogoScale);
		SplashScaleSlot->SetHorizontalAlignment(HAlign_Center);
		SplashScaleSlot->SetVerticalAlignment(VAlign_Center);
	}
	return Super::RebuildWidget();
}

void UMobileSplashWidget::ApplyOpacities()
{
	if (LogoGroup)
	{
		LogoGroup->SetRenderOpacity(LogoOpacity(Elapsed));
	}
	SetRenderOpacity(CoverOpacity(Elapsed, LeaveAt));
}

void UMobileSplashWidget::SetAppReady()
{
	if (LeaveAt < 0.0)
	{
		LeaveAt = FMath::Max(Elapsed, EarliestLeaveSeconds());
	}
}

void UMobileSplashWidget::Advance(double DeltaSeconds)
{
	if (bFinished)
	{
		return;
	}
	Elapsed += FMath::Clamp(DeltaSeconds, 0.0, LongestFrameSeconds);
	ApplyOpacities();
	if (LeaveAt >= 0.0 && Elapsed >= LeaveAt + FadeOutSeconds)
	{
		bFinished = true;
		RemoveFromParent();
	}
}

void UMobileSplashWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const FVector2D Size = MyGeometry.GetLocalSize();
	if (LogoScale && Size.X > 1.0)
	{
		LogoScale->SetUserSpecifiedScale(static_cast<float>(Size.X / UMobileAppWidget::DesignWidth));
	}
	Advance(InDeltaTime);
}
