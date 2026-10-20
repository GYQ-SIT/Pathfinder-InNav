#include "DebugPanelWidget.h"

#include "NavPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

TSharedRef<SWidget> UDebugPanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// The dock frame paints the panel background; this root only adds the usual panel padding.
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_Debug"));
		Root->SetBrushColor(FLinearColor::Transparent);
		Root->SetPadding(FMargin(14.f));
		WidgetTree->RootWidget = Root;

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DebugStack"));
		Root->SetContent(Stack);

		InfoText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_DebugInfo"));
		InfoText->SetText(FText::FromString(TEXT("Debug information")));
		InfoText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(InfoText);
		InfoText->SetAutoWrapText(true);
		Stack->AddChildToVerticalBox(InfoText);

		LastErrorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_DebugLastError"));
		LastErrorText->SetText(FText::FromString(TEXT("Last Error: None")));
		LastErrorText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(LastErrorText);
		LastErrorText->SetAutoWrapText(true);
		Stack->AddChildToVerticalBox(LastErrorText)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	}
	return Super::RebuildWidget();
}

void UDebugPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindToController();
}

void UDebugPanelWidget::NativeDestruct()
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->OnActionFailed.RemoveDynamic(this, &UDebugPanelWidget::HandleActionFailed);
	}
	Super::NativeDestruct();
}

void UDebugPanelWidget::BindToController()
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->OnActionFailed.AddUniqueDynamic(this, &UDebugPanelWidget::HandleActionFailed);
	}
}

void UDebugPanelWidget::HandleActionFailed(FString Reason, EActionFailureSeverity Severity)
{
	LastActionFailedMessage = Reason;
	LastActionFailedSeverity = Severity;
	RefreshTexts();
}

void UDebugPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// Smooth the frame time so the FPS figure does not flicker.
	SmoothedDeltaTime = SmoothedDeltaTime <= 0.f ? InDeltaTime : FMath::Lerp(SmoothedDeltaTime, InDeltaTime, 0.1f);
	RefreshTexts();
}

void UDebugPanelWidget::RefreshTexts()
{
	if (InfoText)
	{
		const float FPS = SmoothedDeltaTime > 0.f ? 1.f / SmoothedDeltaTime : 0.f;
		const FString Lines = InfoProvider ? InfoProvider() : FString();
		InfoText->SetText(FText::FromString(FString::Printf(TEXT("%s%sFPS: %.1f\nFrame Time: %.2f ms"),
			*Lines, Lines.IsEmpty() ? TEXT("") : TEXT("\n"), FPS, SmoothedDeltaTime * 1000.f)));
	}
	if (LastErrorText)
	{
		if (LastActionFailedMessage.IsEmpty())
		{
			LastErrorText->SetText(FText::FromString(TEXT("Last Error: None")));
			LastErrorText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		}
		else
		{
			LastErrorText->SetText(FText::FromString(TEXT("Last Error: ") + LastActionFailedMessage));
			// Yellow: the action was rejected but nothing was lost. Red: existing data may have been cleared, or a
			// save may have silently failed - see EActionFailureSeverity.
			LastErrorText->SetColorAndOpacity(FSlateColor(
				LastActionFailedSeverity == EActionFailureSeverity::Critical ? FLinearColor::Red : FLinearColor::Yellow));
		}
	}
}

FString UDebugPanelWidget::GetInfoTextForTests() const
{
	return InfoText ? InfoText->GetText().ToString() : FString();
}

FString UDebugPanelWidget::GetLastErrorTextForTests() const
{
	return LastErrorText ? LastErrorText->GetText().ToString() : FString();
}

FLinearColor UDebugPanelWidget::GetLastErrorColorForTests() const
{
	return LastErrorText ? LastErrorText->GetColorAndOpacity().GetSpecifiedColor() : FLinearColor::Black;
}
