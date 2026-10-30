#include "MobileNavigatePageWidget.h"

#include "MobileStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace MobileStyle;

TSharedRef<SWidget> UMobileNavigatePageWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		UVerticalBox* Page = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NavigatePage"));
		Tree->RootWidget = Page;

		// Where to, and Exit.
		UHorizontalBox* Top = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("NavigateTop"));
		DestinationText = MakeText(Tree, TEXT("Txt_NavigateDestination"), TEXT(""), 12.f, Grey, true);
		UHorizontalBoxSlot* DestinationSlot = Top->AddChildToHorizontalBox(DestinationText);
		DestinationSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		DestinationSlot->SetVerticalAlignment(VAlign_Center);
		UButton* Exit = MakeButton(Tree, TEXT("Btn_Exit"), TEXT("Exit"), BlueSoft, Blue, 8.f, 28.f, nullptr, 14.f);
		Exit->OnClicked.AddUniqueDynamic(this, &UMobileNavigatePageWidget::HandleExitClicked);
		Top->AddChildToHorizontalBox(Exit)->SetVerticalAlignment(VAlign_Center);
		Page->AddChildToVerticalBox(Top)->SetPadding(FMargin(16.f, 0.f, 16.f, 0.f));

		// The instruction, and how far along.
		InstructionText = MakeText(Tree, TEXT("Txt_NavigateInstruction"), TEXT(""), 20.f, Ink, true);
		InstructionText->SetAutoWrapText(true);
		Page->AddChildToVerticalBox(InstructionText)->SetPadding(FMargin(16.f, 8.f, 16.f, 0.f));
		UHorizontalBox* Info = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("NavigateInfo"));
		ProgressText = MakeText(Tree, TEXT("Txt_NavigateProgress"), TEXT(""), 12.f, Grey, true);
		Info->AddChildToHorizontalBox(ProgressText);
		RemainingText = MakeText(Tree, TEXT("Txt_NavigateRemaining"), TEXT(""), 12.f, Blue, true);
		Info->AddChildToHorizontalBox(RemainingText)->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		LevelText = MakeText(Tree, TEXT("Txt_NavigateLevel"), TEXT(""), 12.f, Grey);
		Info->AddChildToHorizontalBox(LevelText)->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		Page->AddChildToVerticalBox(Info)->SetPadding(FMargin(16.f, 4.f, 16.f, 0.f));

		UButton* Next = MakeButton(Tree, TEXT("Btn_NextInstruction"), TEXT("Next instruction"), Blue, OnBlue, 12.f, 48.f, &NextLabel);
		Next->OnClicked.AddUniqueDynamic(this, &UMobileNavigatePageWidget::HandleNextClicked);
		Page->AddChildToVerticalBox(Next)->SetPadding(FMargin(16.f, 10.f, 16.f, 12.f));

		Refresh();
	}
	return Super::RebuildWidget();
}

void UMobileNavigatePageWidget::SetNavigation(const FMobilePlace& InDestination, const FMobileRoute& InRoute, int32 InStepIndex)
{
	Destination = InDestination;
	Route = InRoute;
	StepIndex = InStepIndex;
	Refresh();
}

UWidget* UMobileNavigatePageWidget::FindNamedWidget(const FName& Name) const
{
	return WidgetTree ? WidgetTree->FindWidget(Name) : nullptr;
}

double UMobileNavigatePageWidget::MetresRemaining(const FMobileRoute& InRoute, int32 InStepIndex)
{
	double Sum = 0.0;
	for (int32 i = FMath::Max(InStepIndex, 0); i < InRoute.Steps.Num(); ++i)
	{
		Sum += InRoute.Steps[i].Metres;
	}
	return Sum;
}

void UMobileNavigatePageWidget::Refresh()
{
	if (!InstructionText)
	{
		return;
	}
	DestinationText->SetText(FText::FromString(FString::Printf(TEXT("To %s · %s"), *Destination.Name, *Destination.GroupLabel())));
	const int32 Count = Route.Steps.Num();
	const int32 Index = FMath::Clamp(StepIndex, 0, FMath::Max(Count - 1, 0));
	if (Count == 0)
	{
		InstructionText->SetText(FText::FromString(TEXT("No route to follow.")));
		ProgressText->SetText(FText::GetEmpty());
		RemainingText->SetText(FText::GetEmpty());
		LevelText->SetText(FText::GetEmpty());
		return;
	}
	const FMobileStep& Step = Route.Steps[Index];
	InstructionText->SetText(FText::FromString(Step.Text));
	ProgressText->SetText(FText::FromString(FString::Printf(TEXT("Step %d of %d"), Index + 1, Count)));
	RemainingText->SetText(FText::FromString(FString::Printf(TEXT("%d m remaining"), FMath::RoundToInt(MetresRemaining(Route, Index)))));
	LevelText->SetText(FText::FromString(Step.BuildingId + TEXT(" · ") + Step.LevelLabel));
	// The last step is the arrival: the button says so.
	NextLabel->SetText(FText::FromString(Index >= Count - 1 ? TEXT("I have arrived") : TEXT("Next instruction")));
}

void UMobileNavigatePageWidget::HandleExitClicked()
{
	if (OnExit)
	{
		OnExit();
	}
}

void UMobileNavigatePageWidget::HandleNextClicked()
{
	if (OnNext)
	{
		OnNext();
	}
}

FString UMobileNavigatePageWidget::GetDestinationString() const { return DestinationText ? DestinationText->GetText().ToString() : FString(); }
FString UMobileNavigatePageWidget::GetInstructionString() const { return InstructionText ? InstructionText->GetText().ToString() : FString(); }
FString UMobileNavigatePageWidget::GetProgressString() const { return ProgressText ? ProgressText->GetText().ToString() : FString(); }
FString UMobileNavigatePageWidget::GetRemainingString() const { return RemainingText ? RemainingText->GetText().ToString() : FString(); }
FString UMobileNavigatePageWidget::GetNextLabelString() const { return NextLabel ? NextLabel->GetText().ToString() : FString(); }
