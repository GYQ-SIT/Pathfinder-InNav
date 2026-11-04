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
	FSlateBrush SplashOutline(const FLinearColor& Colour, float Radius, float Width)
	{
		return MobileStyle::Rounded(FLinearColor::Transparent, Colour, Radius, Width);
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
		UOverlay* Root = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SplashRoot"));
		Tree->RootWidget = Root;

		// The black. It is a hit-testable widget, so that no touch goes through to what is under the splash.
		UBorder* Black = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SplashBlack"));
		Black->SetBrushColor(FLinearColor::Black);
		Black->SetVisibility(ESlateVisibility::Visible);
		UOverlaySlot* BlackSlot = Root->AddChildToOverlay(Black);
		BlackSlot->SetHorizontalAlignment(HAlign_Fill);
		BlackSlot->SetVerticalAlignment(VAlign_Fill);

		// The logo: the blue outlined square with the two-column symbol of the start screen, and the name under it.
		const FLinearColor LogoBlue = StartStyle::Rgb(52, 142, 255);
		USizeBox* MarkSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SplashMarkSize"));
		MarkSize->SetWidthOverride(88.f);
		MarkSize->SetHeightOverride(88.f);
		UBorder* MarkFrame = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SplashMark"));
		MarkFrame->SetBrush(SplashOutline(LogoBlue, 20.f, 2.5f));
		MarkFrame->SetBrushColor(FLinearColor::White);
		MarkFrame->SetHorizontalAlignment(HAlign_Center);
		MarkFrame->SetVerticalAlignment(VAlign_Center);
		USizeBox* SymbolSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SplashSymbolSize"));
		SymbolSize->SetWidthOverride(40.f);
		SymbolSize->SetHeightOverride(40.f);
		UOverlay* Symbol = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SplashSymbol"));
		UBorder* SymbolBox = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SplashSymbolBox"));
		SymbolBox->SetBrush(SplashOutline(LogoBlue, 4.f, 3.5f));
		SymbolBox->SetBrushColor(FLinearColor::White);
		UOverlaySlot* SymbolBoxSlot = Symbol->AddChildToOverlay(SymbolBox);
		SymbolBoxSlot->SetHorizontalAlignment(HAlign_Fill);
		SymbolBoxSlot->SetVerticalAlignment(VAlign_Fill);
		USizeBox* LineSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SplashSymbolLineSize"));
		LineSize->SetWidthOverride(3.5f);
		UBorder* Line = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SplashSymbolLine"));
		Line->SetBrushColor(LogoBlue);
		LineSize->SetContent(Line);
		UOverlaySlot* LineSlot = Symbol->AddChildToOverlay(LineSize);
		LineSlot->SetHorizontalAlignment(HAlign_Center);
		LineSlot->SetVerticalAlignment(VAlign_Fill);
		SymbolSize->SetContent(Symbol);
		MarkFrame->SetContent(SymbolSize);
		MarkSize->SetContent(MarkFrame);

		UHorizontalBox* Name = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SplashName"));
		Name->AddChildToHorizontalBox(MobileStyle::MakeText(Tree, TEXT("Txt_SplashIn"), TEXT("In"), 40.f, FLinearColor::White, true));
		Name->AddChildToHorizontalBox(MobileStyle::MakeText(Tree, TEXT("Txt_SplashNav"), TEXT("Nav"), 40.f, StartStyle::Rgb(92, 172, 255), true));

		UVerticalBox* Group = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SplashLogo"));
		UVerticalBoxSlot* MarkSlot = Group->AddChildToVerticalBox(MarkSize);
		MarkSlot->SetHorizontalAlignment(HAlign_Center);
		MarkSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));
		Group->AddChildToVerticalBox(Name)->SetHorizontalAlignment(HAlign_Center);
		LogoGroup = Group;

		// Laid out in a space 375 wide, and blown up to the window's width (NativeTick), as the app is.
		USizeBox* Width = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SplashWidth"));
		Width->SetWidthOverride(static_cast<float>(UMobileAppWidget::DesignWidth));
		Width->SetContent(Group);
		LogoScale = Tree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("SplashScale"));
		LogoScale->SetStretch(EStretch::UserSpecified);
		LogoScale->SetUserSpecifiedScale(1.f);
		LogoScale->SetContent(Width);
		UOverlaySlot* ScaleSlot = Root->AddChildToOverlay(LogoScale);
		ScaleSlot->SetHorizontalAlignment(HAlign_Center);
		ScaleSlot->SetVerticalAlignment(VAlign_Center);
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
