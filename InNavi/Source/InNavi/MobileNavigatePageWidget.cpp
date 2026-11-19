#include "MobileNavigatePageWidget.h"
#include "LayoutValidation.h"

#include "MobileIcons.h"
#include "MobileStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
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
		// What scrolls (everything but the button), so the button is always on the screen however long the instruction is or short the sheet.
		UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("NavigateScroll"));
		UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NavigateContent"));
		MobileStyle::NoScrollShadow(Scroll);
		Scroll->AddChild(Content);

		// Which destination, when there are several: small grey words at the top.
		StopText = MakeText(Tree, TEXT("Txt_NavigateStop"), TEXT(""), 12.f, Grey);
		StopText->SetVisibility(ESlateVisibility::Collapsed);
		Content->AddChildToVerticalBox(StopText)->SetPadding(FMargin(16.f, 0.f, 16.f, 0.f));

		// Where to, and Exit (blue words, no box).
		UHorizontalBox* Top = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("NavigateTop"));
		DestinationText = MakeText(Tree, TEXT("Txt_NavigateDestination"), TEXT(""), 14.f, Ink, true);
		UHorizontalBoxSlot* DestinationSlot = Top->AddChildToHorizontalBox(DestinationText);
		DestinationSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		DestinationSlot->SetVerticalAlignment(VAlign_Center);
		UButton* Exit = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_Exit"));
		Exit->SetStyle(StartStyle::Button(FLinearColor::Transparent, TapHover, TapHover * 2.f, FLinearColor::Transparent, 8.f / StartStyle::DesignToUi));
		Exit->SetBackgroundColor(FLinearColor::White);
		UBorder* ExitPad = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		ExitPad->SetBrushColor(FLinearColor::Transparent);
		ExitPad->SetPadding(FMargin(8.f, 8.f));
		ExitPad->SetContent(MakeText(Tree, TEXT("Btn_Exit_Label"), TEXT("Exit"), 16.f, Blue, true));
		Exit->SetContent(ExitPad);
		Exit->OnClicked.AddUniqueDynamic(this, &UMobileNavigatePageWidget::HandleExitClicked);
		Top->AddChildToHorizontalBox(Exit)->SetVerticalAlignment(VAlign_Center);
		Content->AddChildToVerticalBox(Top)->SetPadding(FMargin(16.f, 0.f, 8.f, 0.f));

		// The instruction: a dark card with the arrow of the step (blue) and the words (white), as in the prototype whatever the theme.
		InstructionText = MakeText(Tree, TEXT("Txt_NavigateInstruction"), TEXT(""), 16.f, FLinearColor::White, true);
		InstructionText->SetAutoWrapText(true);
		InstructionIcon = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("NavigateStepIcon"));
		InstructionIcon->SetBrushColor(FLinearColor::Transparent);
		InstructionIcon->SetHorizontalAlignment(HAlign_Center);
		InstructionIcon->SetVerticalAlignment(VAlign_Center);
		UHorizontalBox* CardRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("NavigateCardRow"));
		USizeBox* IconSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		IconSize->SetWidthOverride(24.f);
		IconSize->SetHeightOverride(24.f);
		IconSize->SetContent(InstructionIcon);
		CardRow->AddChildToHorizontalBox(IconSize)->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* TextSlot = CardRow->AddChildToHorizontalBox(InstructionText);
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		TextSlot->SetVerticalAlignment(VAlign_Center);
		UBorder* Card = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("NavigateCard"));
		Card->SetBrush(Rounded(StartStyle::Rgb(15, 23, 42), FLinearColor::Transparent, 12.f, 0.f));
		Card->SetBrushColor(FLinearColor::White);
		Card->SetPadding(FMargin(16.f, 14.f));
		Card->SetContent(CardRow);
		Content->AddChildToVerticalBox(Card)->SetPadding(FMargin(16.f, 8.f, 16.f, 0.f));

		// How far along: a thin track with what is walked of it in blue.
		ProgressFill = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("NavigateProgressFill"));
		UBorder* Walked = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Walked->SetBrush(Rounded(Blue, FLinearColor::Transparent, 3.f, 0.f));
		Walked->SetBrushColor(FLinearColor::White);
		ProgressFill->AddChildToHorizontalBox(Walked)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ProgressFill->AddChildToHorizontalBox(Tree->ConstructWidget<USpacer>(USpacer::StaticClass()))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UBorder* Track = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("NavigateTrack"));
		Track->SetBrush(Rounded(FieldEdge, FLinearColor::Transparent, 3.f, 0.f));
		Track->SetBrushColor(FLinearColor::White);
		Track->SetContent(ProgressFill);
		USizeBox* TrackSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		TrackSize->SetHeightOverride(6.f);
		TrackSize->SetContent(Track);
		Content->AddChildToVerticalBox(TrackSize)->SetPadding(FMargin(16.f, 10.f, 16.f, 0.f));

		// "Step 1 of 3" at the left and the metres still to walk at the right (where the step is stays in the page, hidden: the title has the place's).
		UHorizontalBox* Info = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("NavigateInfo"));
		ProgressText = MakeText(Tree, TEXT("Txt_NavigateProgress"), TEXT(""), 12.f, Ink);
		Info->AddChildToHorizontalBox(ProgressText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RemainingText = MakeText(Tree, TEXT("Txt_NavigateRemaining"), TEXT(""), 12.f, Ink);
		Info->AddChildToHorizontalBox(RemainingText);
		Content->AddChildToVerticalBox(Info)->SetPadding(FMargin(16.f, 6.f, 16.f, 0.f));
		// On a step that changes level: what to check before pressing the button.
		HintText = MakeText(Tree, TEXT("Txt_NavigateHint"), TEXT(""), 12.f, Grey);
		HintText->SetAutoWrapText(true);
		HintText->SetVisibility(ESlateVisibility::Collapsed);
		Content->AddChildToVerticalBox(HintText)->SetPadding(FMargin(16.f, 12.f, 16.f, 0.f));
		LevelText = MakeText(Tree, TEXT("Txt_NavigateLevel"), TEXT(""), 12.f, Grey);
		LevelText->SetVisibility(ESlateVisibility::Collapsed);
		Content->AddChildToVerticalBox(LevelText);
		Page->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		UButton* Next = MakeButton(Tree, TEXT("Btn_NextInstruction"), TEXT("Next instruction"), Blue, OnBlue, 8.f, 44.f, &NextLabel);
		Next->OnClicked.AddUniqueDynamic(this, &UMobileNavigatePageWidget::HandleNextClicked);
		Page->AddChildToVerticalBox(Next)->SetPadding(FMargin(16.f, 8.f, 16.f, 12.f));

		Refresh();
	}
	return Super::RebuildWidget();
}

void UMobileNavigatePageWidget::SetJourney(int32 InLegNumber, int32 InLegCount, const FString& InNextName)
{
	LegNumber = InLegNumber;
	LegCount = InLegCount;
	NextName = InNextName;
	Refresh();
}

FString UMobileNavigatePageWidget::GetHintString() const { return HintText && HintText->GetVisibility() != ESlateVisibility::Collapsed ? HintText->GetText().ToString() : FString(); }

FString UMobileNavigatePageWidget::GetStopString() const { return StopText && StopText->GetVisibility() != ESlateVisibility::Collapsed ? StopText->GetText().ToString() : FString(); }

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
	StopText->SetText(FText::FromString(FString::Printf(TEXT("Destination %d of %d"), LegNumber, LegCount)));
	StopText->SetVisibility(LegCount > 1 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	const int32 Count = Route.Steps.Num();
	const int32 Index = FMath::Clamp(StepIndex, 0, FMath::Max(Count - 1, 0));
	if (Count == 0)
	{
		InstructionText->SetText(FText::FromString(TEXT("No route to follow.")));
		ProgressText->SetText(FText::GetEmpty());
		RemainingText->SetText(FText::GetEmpty());
		LevelText->SetText(FText::GetEmpty());
		HintText->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	const FMobileStep& Step = Route.Steps[Index];
	// The arrow of the kind of step: straight on for most, the turns, a check for the arrival, and the lift, the stairs and the escalator for the steps
	// that change level. A picture that is not imported yet (a fresh checkout, a test) is replaced by the straight arrow.
	if (InstructionIcon)
	{
		const TCHAR* IconName = TEXT("Straight");
		switch (Step.Kind)
		{
		case EMobileStepKind::TurnLeft: case EMobileStepKind::TurnAround: IconName = TEXT("TurnLeft"); break;
		case EMobileStepKind::TurnRight: IconName = TEXT("TurnRight"); break;
		case EMobileStepKind::Arrive: IconName = TEXT("Success"); break;
		case EMobileStepKind::TakeLift: IconName = TEXT("Lift"); break;
		case EMobileStepKind::TakeStairs: IconName = TEXT("Stairs"); break;
		case EMobileStepKind::TakeEscalator: IconName = TEXT("Escalator"); break;
		default: break;
		}
		UWidget* Arrow = MobileIcons::Picture(WidgetTree, IconName, 24.f, Blue);
		if (!Arrow && FCString::Strcmp(IconName, TEXT("Straight")) != 0)
		{
			Arrow = MobileIcons::Picture(WidgetTree, TEXT("Straight"), 24.f, Blue);
		}
		InstructionIcon->SetContent(Arrow ? Arrow : static_cast<UWidget*>(MakeText(WidgetTree, FString(), TEXT("\u2191"), 22.f, Blue, true)));
	}
	if (ProgressFill && ProgressFill->GetChildrenCount() == 2)
	{
		// What is walked of the track: the step the person is on, of all of them.
		const float Walked = static_cast<float>(Index + 1) / static_cast<float>(FMath::Max(Count, 1));
		FSlateChildSize WalkedSize(ESlateSizeRule::Fill);
		WalkedSize.Value = FMath::Max(Walked, 0.001f);
		FSlateChildSize LeftSize(ESlateSizeRule::Fill);
		LeftSize.Value = FMath::Max(1.f - Walked, 0.001f);
		Cast<UHorizontalBoxSlot>(ProgressFill->GetSlots()[0])->SetSize(WalkedSize);
		Cast<UHorizontalBoxSlot>(ProgressFill->GetSlots()[1])->SetSize(LeftSize);
	}
	// The last instruction of a leg that is not the last is a stop on the way, not the end.
	const bool bStopReached = LegCount > 1 && LegNumber < LegCount && Index >= Count - 1;
	InstructionText->SetText(FText::FromString(bStopReached ? FString::Printf(TEXT("Stop reached: %s"), *Destination.Name) : Step.Text));
	ProgressText->SetText(FText::FromString(FString::Printf(TEXT("Step %d of %d"), Index + 1, Count)));
	RemainingText->SetText(FText::FromString(FString::Printf(TEXT("%d m remaining"), FMath::RoundToInt(MetresRemaining(Route, Index)))));
	LevelText->SetText(FText::FromString(Step.WhereText()));
	// What the button says, as in the prototype: Continue to the next destination at a stop; I have arrived at the end; Reached L2 on a step that changes level
	// (the level is where the next step is, and the page asks to check the signage); Outside and Inside E2 for leaving and entering a building; else Next instruction.
	const bool bChangesLevel = Step.Kind == EMobileStepKind::TakeLift || Step.Kind == EMobileStepKind::TakeStairs || Step.Kind == EMobileStepKind::TakeEscalator || Step.Kind == EMobileStepKind::ChangeFloor;
	const FMobileStep& After = Route.Steps.IsValidIndex(Index + 1) ? Route.Steps[Index + 1] : Step;
	FString Hint;
	FString Label = TEXT("Next instruction");
	if (bStopReached)
	{
		Label = FString::Printf(TEXT("Continue to %s"), *NextName);
	}
	else if (Index >= Count - 1)
	{
		Label = TEXT("I have arrived");
	}
	else if (bChangesLevel)
	{
		Label = FString::Printf(TEXT("Reached %s"), *After.LevelLabel);
		if (After.Level > 0)
		{
			Hint = FString::Printf(TEXT("Check the building signage, then confirm Level %s."), *LayoutValidation::LevelLabel(After.Level));
		}
	}
	else if (Step.Kind == EMobileStepKind::ExitBuilding)
	{
		Label = TEXT("Outside");
	}
	else if (Step.Kind == EMobileStepKind::EnterBuilding)
	{
		Label = FString::Printf(TEXT("Inside %s"), *(Step.BuildingName.IsEmpty() ? Step.BuildingId : Step.BuildingName));
	}
	NextLabel->SetText(FText::FromString(Label));
	HintText->SetText(FText::FromString(Hint));
	HintText->SetVisibility(Hint.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
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
