#include "NavigationDockPanelWidget.h"

#include "NavPlayerController.h"
#include "NavigationPanelWidget.h"
#include "NavigationRouteVisualization.h"
#include "StartScreenStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	constexpr float NavButtonHeight = 32.f;
	// A row of the places list: the number, the dropdown, the crosshair.
	constexpr float BadgeSize = 26.f;
	constexpr float PickWidth = 30.f;
	constexpr float RowGap = 6.f;

	static_assert(UNavigationDockPanelWidget::MaxSlots == UNavigationPanelWidget::MaxRouteStops + 3, "A row for the start, each stop, one being added, and the destination");

	void WrapText(UTextBlock* TextBlock)
	{
		TextBlock->SetAutoWrapText(true);
	}

	void SetButtonWidth(UButton* Button, float Width)
	{
		if (Button)
		{
			if (USizeBox* SizeBox = Cast<USizeBox>(Button->GetContent()))
			{
				SizeBox->SetWidthOverride(Width);
			}
		}
	}

	const FLinearColor InactiveColor = StartStyle::Rgb(27, 41, 63);
	const FLinearColor StartColor = StartStyle::Rgb(26, 138, 84);
	const FLinearColor DestinationColor = StartStyle::Rgb(180, 52, 62);
	const FLinearColor PickingColor = StartStyle::Rgb(24, 118, 190);
	const FLinearColor PendingColor = StartStyle::Rgb(70, 90, 120);
	const FLinearColor MutedColor(0.7f, 0.78f, 0.88f, 1.f);

	// The buttons keep their outlined shape and take the state's colour as their fill.
	void Paint(UButton* Button, const FLinearColor& Color)
	{
		if (Button)
		{
			Button->SetStyle(Color == InactiveColor ? StartStyle::SecondaryButton() : StartStyle::StatusButton(Color));
		}
	}

	// A place's colour: green at the start, red at the destination, and a stop in the colour of its leg on the map.
	FLinearColor PlaceColor(int32 Row, int32 Count)
	{
		return Row == 0 ? StartColor : (Row == Count - 1 ? DestinationColor : UNavigationRouteVisualization::LegColor2D(Row - 1));
	}

	// The letter or number a place is known by: A and B at the ends, 1, 2 ... between.
	FString PlaceBadge(int32 Row, int32 Count)
	{
		return Row == 0 ? FString(TEXT("A")) : (Row == Count - 1 ? FString(TEXT("B")) : FString::FromInt(Row));
	}

	using FRoomOption = UNavigationPanelWidget::FNavigationRoomOption;

	// Fills a dropdown with the places the search matches, and puts what the row shows back as its first entry and chosen (ClearOptions
	// wipes it, but does not touch the navigation state): a place can be in another building, or picked on the map, and is then not
	// in the list.
	void FillPlaceCombo(UComboBoxString* Combo, const TArray<FRoomOption>& Matches, const FString& Current)
	{
		Combo->ClearOptions();
		if (!Current.IsEmpty())
		{
			Combo->AddOption(Current);
		}
		for (const FRoomOption& Option : Matches)
		{
			if (Option.GetDisplayLabel() != Current)
			{
				Combo->AddOption(Option.GetDisplayLabel());
			}
		}
		if (!Current.IsEmpty())
		{
			Combo->SetSelectedOption(Current);
		}
		else
		{
			Combo->ClearSelection();
		}
	}
}

void UNavigationPlaceButton::Setup(UNavigationDockPanelWidget* InPanel, int32 InSlot, bool bInPick)
{
	Panel = InPanel;
	SlotIndex = InSlot;
	bPick = bInPick;
	OnClicked.AddUniqueDynamic(this, &UNavigationPlaceButton::HandleClicked);
}

void UNavigationPlaceButton::HandleClicked()
{
	if (Panel.IsValid())
	{
		Panel->HandleSlotButton(SlotIndex, bPick);
	}
}

void UNavigationPlaceCombo::Setup(UNavigationDockPanelWidget* InPanel, int32 InSlot)
{
	Panel = InPanel;
	SlotIndex = InSlot;
	OnSelectionChanged.AddUniqueDynamic(this, &UNavigationPlaceCombo::HandleChanged);
}

void UNavigationPlaceCombo::HandleChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (Panel.IsValid())
	{
		Panel->HandleSlotChosen(SlotIndex, SelectedItem, SelectionType);
	}
}

TSharedRef<SWidget> UNavigationDockPanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// The dock frame paints the panel background; this root only adds the usual panel padding.
		RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_Navigation"));
		RootBorder->SetBrushColor(FLinearColor::Transparent);
		RootBorder->SetPadding(FMargin(14.f));
		WidgetTree->RootWidget = RootBorder;

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NavStack"));
		RootBorder->SetContent(Stack);

		// A text, a button, a box: the pieces the views are made of.
		auto MakeText = [this](const TCHAR* Name, float Pixel, const FLinearColor& Color) -> UTextBlock*
		{
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			Text->SetColorAndOpacity(FSlateColor(Color));
			WrapText(Text);
			ApplyStandardFont(Text);
			return Text;
		};
		// A button of the width of the panel (the width is set again by SetContentWidth) with a text on it.
		auto MakeButton = [this](const TCHAR* Name, const TCHAR* Label, UTextBlock*& OutLabel) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetStyle(StartStyle::SecondaryButton());
			Button->SetBackgroundColor(FLinearColor::White);
			Button->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::PlayButtonClickSound);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString(Name) + TEXT("Label")));
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetJustification(ETextJustify::Center);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			WrapText(Text);
			ApplyStandardFont(Text);
			USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			SizeBox->SetWidthOverride(ContentWidth);
			SizeBox->SetMinDesiredHeight(NavButtonHeight);
			SizeBox->SetContent(Text);
			Button->SetContent(SizeBox);
			OutLabel = Text;
			return Button;
		};
		auto AddToStack = [](UVerticalBox* Box, UWidget* Widget, float Bottom)
		{
			Box->AddChildToVerticalBox(Widget)->SetPadding(FMargin(0.f, 0.f, 0.f, Bottom));
		};
		// Buttons of one row, each taking an equal share of it.
		auto MakeRow = [this](const TCHAR* Name, USizeBox*& OutSize) -> UHorizontalBox*
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(Name));
			OutSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			OutSize->SetWidthOverride(ContentWidth);
			OutSize->SetContent(Row);
			return Row;
		};
		auto AddShare = [](UHorizontalBox* Row, UWidget* Widget)
		{
			UHorizontalBoxSlot* BoxSlot = Row->AddChildToHorizontalBox(Widget);
			BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			BoxSlot->SetPadding(FMargin(2.f, 0.f));
		};
		UTextBlock* UnusedLabel = nullptr;

		// ---- The two views, and the tabs that switch between them ----
		UHorizontalBox* Tabs = MakeRow(TEXT("Row_NavTabs"), TabsSize);
		PlanTabButton = MakeButton(TEXT("Btn_NavTabPlan"), TEXT("Plan"), UnusedLabel);
		PlanTabButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnPlanTabClicked);
		AddShare(Tabs, PlanTabButton);
		GoTabButton = MakeButton(TEXT("Btn_NavTabGo"), TEXT("Go"), UnusedLabel);
		GoTabButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnGoTabClicked);
		AddShare(Tabs, GoTabButton);
		AddToStack(Stack, TabsSize, 8.f);

		PlanBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box_NavPlan"));
		GoBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box_NavGo"));
		Stack->AddChildToVerticalBox(PlanBox);
		Stack->AddChildToVerticalBox(GoBox);
		GoBox->SetVisibility(ESlateVisibility::Collapsed);

		// ==== Plan ====
		ContextText = MakeText(TEXT("Txt_NavContext"), 12.f, MutedColor);
		AddToStack(PlanBox, ContextText, 8.f);

		// One search box narrows every dropdown of the list, across every floor of the open building (or every main door outside).
		DestinationSearchBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_DestinationSearch"));
		ApplyStandardFont(DestinationSearchBox);
		DestinationSearchBox->SetWidgetStyle(StartStyle::Field(13.f));
		DestinationSearchBox->SetHintText(FText::FromString(TEXT("Search by name, ID or tag")));
		DestinationSearchBox->OnTextChanged.AddUniqueDynamic(this, &UNavigationDockPanelWidget::HandleSearchChanged);
		USizeBox* SearchSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Txt_DestinationSearchSize"));
		SearchSize->SetWidthOverride(ContentWidth);
		SearchSize->SetContent(DestinationSearchBox);
		AddToStack(PlanBox, SearchSize, 4.f);
		DestinationSearchResultsText = MakeText(TEXT("Txt_DestinationSearchResults"), 12.f, FLinearColor::White);
		AddToStack(PlanBox, DestinationSearchResultsText, 8.f);

		// The places, one row each. The rows are made once, and what each shows is set by RefreshDisplay.
		UVerticalBox* Places = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box_NavPlaces"));
		AddToStack(PlanBox, Places, 2.f);
		for (int32 SlotIndex = 0; SlotIndex < MaxSlots; ++SlotIndex)
		{
			const FString Suffix = FString::FromInt(SlotIndex);
			UNavigationPlaceButton* Badge = WidgetTree->ConstructWidget<UNavigationPlaceButton>(UNavigationPlaceButton::StaticClass(), FName(TEXT("Btn_NavPlaceBadge") + Suffix));
			Badge->Setup(this, SlotIndex, false);
			Badge->SetStyle(StartStyle::StatusButton(PendingColor));
			Badge->SetToolTipText(FText::FromString(TEXT("Choose this place to move or remove it")));
			UTextBlock* BadgeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(TEXT("Txt_NavPlaceBadge") + Suffix));
			BadgeText->SetJustification(ETextJustify::Center);
			BadgeText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(BadgeText);
			USizeBox* BadgeSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			BadgeSizeBox->SetWidthOverride(BadgeSize);
			BadgeSizeBox->SetHeightOverride(BadgeSize);
			BadgeSizeBox->SetContent(BadgeText);
			Badge->SetContent(BadgeSizeBox);

			UTextBlock* Role = MakeText(*(TEXT("Txt_NavPlaceRole") + Suffix), 11.f, MutedColor);
			UNavigationPlaceCombo* Combo = WidgetTree->ConstructWidget<UNavigationPlaceCombo>(UNavigationPlaceCombo::StaticClass(), FName(TEXT("Cbx_NavPlace") + Suffix));
			Combo->Setup(this, SlotIndex);
			Combo->OnGenerateWidgetEvent.BindDynamic(this, &UNavigationDockPanelWidget::HandleGenerateComboBoxItem);
			StartStyle::DarkCombo(Combo);
			USizeBox* ComboSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(TEXT("Cbx_NavPlaceSize") + Suffix));
			ComboSize->SetWidthOverride(FMath::Max(60.f, ContentWidth - BadgeSize - PickWidth - 2.f * RowGap));
			ComboSize->SetContent(Combo);
			// The frame of the dropdown is lit while the row is the one chosen for moving or removing.
			UBorder* Field = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(TEXT("Brd_NavPlace") + Suffix));
			Field->SetPadding(FMargin(2.f));
			Field->SetBrushColor(FLinearColor::Transparent);
			Field->SetContent(ComboSize);
			UVerticalBox* Middle = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			Middle->AddChildToVerticalBox(Role);
			Middle->AddChildToVerticalBox(Field);

			UNavigationPlaceButton* Pick = WidgetTree->ConstructWidget<UNavigationPlaceButton>(UNavigationPlaceButton::StaticClass(), FName(TEXT("Btn_NavPlacePick") + Suffix));
			Pick->Setup(this, SlotIndex, true);
			Pick->SetStyle(StartStyle::SecondaryButton());
			Pick->SetToolTipText(FText::FromString(TEXT("Click a place on the map")));
			UTextBlock* PickText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			PickText->SetText(FText::FromString(TEXT("+")));
			PickText->SetJustification(ETextJustify::Center);
			PickText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(PickText);
			USizeBox* PickSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			PickSize->SetWidthOverride(PickWidth);
			PickSize->SetHeightOverride(NavButtonHeight);
			PickSize->SetContent(PickText);
			Pick->SetContent(PickSize);

			UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(TEXT("Row_NavPlace") + Suffix));
			UHorizontalBoxSlot* BadgeSlot = Line->AddChildToHorizontalBox(Badge);
			BadgeSlot->SetVerticalAlignment(VAlign_Bottom);
			BadgeSlot->SetPadding(FMargin(0.f, 0.f, RowGap, 3.f));
			Line->AddChildToHorizontalBox(Middle)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			UHorizontalBoxSlot* PickSlot = Line->AddChildToHorizontalBox(Pick);
			PickSlot->SetVerticalAlignment(VAlign_Bottom);
			PickSlot->SetPadding(FMargin(RowGap, 0.f, 0.f, 3.f));
			USizeBox* LineSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(TEXT("Row_NavPlaceSize") + Suffix));
			LineSize->SetWidthOverride(ContentWidth);
			LineSize->SetContent(Line);
			// The two ends are always there; the other rows are shown as stops are added.
			LineSize->SetVisibility(SlotIndex < 2 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			AddToStack(Places, LineSize, 2.f);

			SlotSizes.Add(LineSize);
			BadgeButtons.Add(Badge);
			BadgeTexts.Add(BadgeText);
			RoleTexts.Add(Role);
			FieldBorders.Add(Field);
			ComboSizes.Add(ComboSize);
			PlaceCombos.Add(Combo);
			PickButtons.Add(Pick);
		}
		SlotRow.Init(INDEX_NONE, MaxSlots);

		// What can be done with the chosen row.
		UHorizontalBox* Actions = MakeRow(TEXT("Row_NavPlaceActions"), ActionsSize);
		MoveUpButton = MakeButton(TEXT("Btn_NavPlaceUp"), TEXT("Up"), UnusedLabel);
		MoveUpButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnMoveUpClicked);
		AddShare(Actions, MoveUpButton);
		MoveDownButton = MakeButton(TEXT("Btn_NavPlaceDown"), TEXT("Down"), UnusedLabel);
		MoveDownButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnMoveDownClicked);
		AddShare(Actions, MoveDownButton);
		RemoveButton = MakeButton(TEXT("Btn_NavPlaceRemove"), TEXT("Remove"), UnusedLabel);
		RemoveButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnRemoveClicked);
		AddShare(Actions, RemoveButton);
		ActionsSize->SetVisibility(ESlateVisibility::Collapsed);
		AddToStack(PlanBox, ActionsSize, 6.f);

		UHorizontalBox* AddRow = MakeRow(TEXT("Row_NavAddRow"), AddRowSize);
		AddStopButton = MakeButton(TEXT("Btn_NavAddStop"), TEXT("+ Add stop"), AddStopButtonLabel);
		AddStopButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnAddStopClicked);
		AddShare(AddRow, AddStopButton);
		ReverseButton = MakeButton(TEXT("Btn_NavReverse"), TEXT("Reverse"), UnusedLabel);
		ReverseButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnReverseClicked);
		AddShare(AddRow, ReverseButton);
		AddToStack(PlanBox, AddRowSize, 8.f);

		FindRouteButton = MakeButton(TEXT("Btn_NavFindRoute"), TEXT("Find Route"), UnusedLabel);
		FindRouteButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnFindRouteClicked);
		FindRouteButton->SetIsEnabled(false);
		AddToStack(PlanBox, FindRouteButton, 6.f);
		// Only while a route is being searched for: how far it is, and a way to stop it.
		FindRouteProgress = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("Bar_NavFindRouteProgress"));
		FindRouteProgress->SetFillColorAndOpacity(FLinearColor(0.3f, 0.85f, 0.4f, 1.f));
		FindRouteProgress->SetPercent(0.f);
		USizeBox* ProgressSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Bar_NavFindRouteProgressSize"));
		ProgressSize->SetWidthOverride(ContentWidth);
		ProgressSize->SetHeightOverride(10.f);
		ProgressSize->SetContent(FindRouteProgress);
		ProgressSize->SetVisibility(ESlateVisibility::Collapsed);
		AddToStack(PlanBox, ProgressSize, 6.f);
		CancelFindRouteButton = MakeButton(TEXT("Btn_NavCancelFindRoute"), TEXT("Cancel Search"), UnusedLabel);
		CancelFindRouteButton->SetVisibility(ESlateVisibility::Collapsed);
		CancelFindRouteButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnCancelFindRouteClicked);
		AddToStack(PlanBox, CancelFindRouteButton, 6.f);

		HintText = MakeText(TEXT("Txt_NavPlanHint"), 12.f, MutedColor);
		AddToStack(PlanBox, HintText, 6.f);
		StatusText = MakeText(TEXT("Txt_NavStatus"), 13.f, FLinearColor::White);
		AddToStack(PlanBox, StatusText, 6.f);
		RouteDetailsText = MakeText(TEXT("Txt_NavRouteDetails"), 13.f, FLinearColor(0.45f, 0.92f, 1.f, 1.f));
		AddToStack(PlanBox, RouteDetailsText, 6.f);
		ClearRouteButton = MakeButton(TEXT("Btn_NavClearRoute"), TEXT("Clear Route"), UnusedLabel);
		ClearRouteButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnClearRouteClicked);
		AddToStack(PlanBox, ClearRouteButton, 6.f);
		ToggleRouteButton = MakeButton(TEXT("Btn_NavToggleRoute"), TEXT("Hide Route"), ToggleRouteButtonLabel);
		ToggleRouteButton->SetIsEnabled(false);
		ToggleRouteButton->SetToolTipText(FText::FromString(TEXT("Show or hide route lines, direction arrows, markers and instructions without clearing your route")));
		ToggleRouteButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnToggleRouteClicked);
		AddToStack(PlanBox, ToggleRouteButton, 6.f);

		// ==== Go ====
		// The route in a line (a number for each place, in its colour on the map), and how far along it the step is.
		UHorizontalBox* Overview = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row_NavOverview"));
		for (int32 Index = 0; Index < MaxSlots; ++Index)
		{
			UTextBlock* BadgeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			BadgeText->SetJustification(ETextJustify::Center);
			BadgeText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(BadgeText);
			UBorder* Badge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Badge->SetPadding(FMargin(7.f, 1.f));
			Badge->SetContent(BadgeText);
			Badge->SetVisibility(ESlateVisibility::Collapsed);
			Overview->AddChildToHorizontalBox(Badge)->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
			OverviewBadges.Add(Badge);
			OverviewTexts.Add(BadgeText);
		}
		OverviewInfoText = MakeText(TEXT("Txt_NavOverviewInfo"), 12.f, MutedColor);
		UHorizontalBoxSlot* InfoSlot = Overview->AddChildToHorizontalBox(OverviewInfoText);
		InfoSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
		InfoSlot->SetVerticalAlignment(VAlign_Center);
		AddToStack(GoBox, Overview, 8.f);
		StepProgress = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("Bar_NavStepProgress"));
		StepProgress->SetFillColorAndOpacity(FLinearColor(0.1f, 0.62f, 0.95f, 1.f));
		USizeBox* StepProgressSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Bar_NavStepProgressSize"));
		StepProgressSize->SetWidthOverride(ContentWidth);
		StepProgressSize->SetHeightOverride(6.f);
		StepProgressSize->SetContent(StepProgress);
		AddToStack(GoBox, StepProgressSize, 10.f);

		RouteStepsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box_NavRouteSteps"));
		RouteStepsBox->SetVisibility(ESlateVisibility::Collapsed);
		AddToStack(GoBox, RouteStepsBox, 6.f);

		// Previous and Next, and the switch between one step at a time and all of them.
		UHorizontalBox* StepButtons = MakeRow(TEXT("Row_NavStepButtons"), StepButtonsSize);
		PreviousStepButton = MakeButton(TEXT("Btn_NavPreviousStep"), TEXT("Previous"), UnusedLabel);
		PreviousStepButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnPreviousStepClicked);
		AddShare(StepButtons, PreviousStepButton);
		NextStepButton = MakeButton(TEXT("Btn_NavNextStep"), TEXT("Next Step"), NextStepButtonLabel);
		NextStepButton->SetStyle(StartStyle::PrimaryButton());
		NextStepButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnNextStepClicked);
		AddShare(StepButtons, NextStepButton);
		StepButtonsSize->SetVisibility(ESlateVisibility::Collapsed);
		AddToStack(GoBox, StepButtonsSize, 8.f);
		StepModeButton = MakeButton(TEXT("Btn_NavStepMode"), TEXT("Show All Steps"), StepModeButtonLabel);
		StepModeButton->SetVisibility(ESlateVisibility::Collapsed);
		StepModeButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnStepModeClicked);
		AddToStack(GoBox, StepModeButton, 6.f);
		EditRouteButton = MakeButton(TEXT("Btn_NavEditRoute"), TEXT("Edit Route"), UnusedLabel);
		EditRouteButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnEditRouteClicked);
		AddToStack(GoBox, EditRouteButton, 6.f);
	}
	return Super::RebuildWidget();
}

void UNavigationDockPanelWidget::SetContentWidth(float Width)
{
	if (FMath::IsNearlyEqual(Width, ContentWidth, 0.5f))
	{
		return;
	}
	ContentWidth = Width;
	for (USizeBox* Size : { TabsSize, ActionsSize, AddRowSize, StepButtonsSize })
	{
		if (Size)
		{
			Size->SetWidthOverride(Width);
		}
	}
	for (UButton* Button : { FindRouteButton, CancelFindRouteButton, ClearRouteButton, ToggleRouteButton, StepModeButton, EditRouteButton })
	{
		SetButtonWidth(Button, Width);
	}
	for (UButton* Button : { PlanTabButton, GoTabButton, MoveUpButton, MoveDownButton, RemoveButton, AddStopButton, ReverseButton, PreviousStepButton, NextStepButton })
	{
		// The buttons of a row share its width.
		SetButtonWidth(Button, FMath::Max(40.f, Width / 3.f - 4.f));
	}
	for (int32 SlotIndex = 0; SlotIndex < SlotSizes.Num(); ++SlotIndex)
	{
		SlotSizes[SlotIndex]->SetWidthOverride(Width);
		ComboSizes[SlotIndex]->SetWidthOverride(FMath::Max(60.f, Width - BadgeSize - PickWidth - 2.f * RowGap));
	}
	if (UWidget* Parent = DestinationSearchBox ? DestinationSearchBox->GetParent() : nullptr)
	{
		if (USizeBox* SearchSize = Cast<USizeBox>(Parent))
		{
			SearchSize->SetWidthOverride(Width);
		}
	}
	for (UProgressBar* Bar : { FindRouteProgress, StepProgress })
	{
		if (Bar)
		{
			if (USizeBox* Holder = Cast<USizeBox>(Bar->GetParent()))
			{
				Holder->SetWidthOverride(Width);
			}
		}
	}
}

void UNavigationDockPanelWidget::AssignSlots(const UNavigationPanelWidget& Nav)
{
	SlotRow.Init(INDEX_NONE, MaxSlots);
	const int32 Count = Nav.GetPlaceCount();
	int32 SlotIndex = 0;
	for (int32 Row = 0; Row < Count && SlotIndex < MaxSlots; ++Row)
	{
		// The row a stop is added in is just before the destination.
		if (Row == Count - 1 && bAddingStop && SlotIndex < MaxSlots - 1)
		{
			SlotRow[SlotIndex++] = PendingRow;
		}
		SlotRow[SlotIndex++] = Row;
	}
}

void UNavigationDockPanelWidget::HandleSlotButton(int32 SlotIndex, bool bPick)
{
	ANavPlayerController* PC = GetController();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
	if (!Nav || !SlotRow.IsValidIndex(SlotIndex) || SlotRow[SlotIndex] == INDEX_NONE)
	{
		return;
	}
	PlayButtonClickSound();
	const int32 Row = SlotRow[SlotIndex];
	if (!bPick)
	{
		// The number chooses the row (a second time, lets it go) for the buttons under the list.
		if (Row >= 0)
		{
			SelectedRow = SelectedRow == Row ? INDEX_NONE : Row;
			RefreshDisplay();
		}
		return;
	}
	// The crosshair: click the map for this place. The start and the destination each, and the row a stop is being added in.
	if (Row == PendingRow)
	{
		Nav->ToggleSelectStops();
	}
	else if (Row == 0)
	{
		Nav->ToggleSelectStart();
	}
	else if (Row == Nav->GetPlaceCount() - 1)
	{
		Nav->ToggleSelectDestination();
	}
}

void UNavigationDockPanelWidget::HandleSlotChosen(int32 SlotIndex, const FString& Item, ESelectInfo::Type SelectionType)
{
	// Only the user's own pick counts, not this panel's own SetSelectedOption calls.
	ANavPlayerController* PC = GetController();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
	if (SelectionType == ESelectInfo::Direct || !Nav || !SlotRow.IsValidIndex(SlotIndex) || SlotRow[SlotIndex] == INDEX_NONE || Item.IsEmpty())
	{
		return;
	}
	const int32 Row = SlotRow[SlotIndex];
	if (Row == PendingRow)
	{
		// A stop from the list; the row stays open, empty, for the next (the repaint of the new stop refills it). A stop that was not added
		// leaves the choice on the row, which is cleared.
		if (!Nav->AddStopToRoom(Item) && PlaceCombos.IsValidIndex(SlotIndex))
		{
			PlaceCombos[SlotIndex]->ClearSelection();
		}
		return;
	}
	// The entry that shows what the row is now (another building's place, or a spot picked on the map) is only a display of it.
	if (Item == Nav->GetPlaceText(Row))
	{
		return;
	}
	Nav->SetPlaceToRoom(Row, Item);
}

void UNavigationDockPanelWidget::OnPlanTabClicked()
{
	bGoView = false;
	RefreshDisplay();
}

void UNavigationDockPanelWidget::OnGoTabClicked()
{
	bGoView = true;
	RefreshDisplay();
}

void UNavigationDockPanelWidget::OnEditRouteClicked()
{
	bGoView = false;
	RefreshDisplay();
}

namespace
{
	UNavigationPanelWidget* NavOf(const UNavigationDockPanelWidget* Panel, ANavPlayerController* PC)
	{
		return PC ? PC->GetNavigationPanel() : nullptr;
	}
}

void UNavigationDockPanelWidget::OnMoveUpClicked()
{
	if (UNavigationPanelWidget* Nav = NavOf(this, GetController()); Nav && SelectedRow != INDEX_NONE)
	{
		const int32 Moved = Nav->MovePlace(SelectedRow, -1);
		SelectedRow = Moved;
		RefreshDisplay();
	}
}

void UNavigationDockPanelWidget::OnMoveDownClicked()
{
	if (UNavigationPanelWidget* Nav = NavOf(this, GetController()); Nav && SelectedRow != INDEX_NONE)
	{
		const int32 Moved = Nav->MovePlace(SelectedRow, 1);
		SelectedRow = Moved;
		RefreshDisplay();
	}
}

void UNavigationDockPanelWidget::OnRemoveClicked()
{
	if (UNavigationPanelWidget* Nav = NavOf(this, GetController()); Nav && SelectedRow != INDEX_NONE)
	{
		Nav->ClearPlace(SelectedRow);
		SelectedRow = INDEX_NONE;
		RefreshDisplay();
	}
}

void UNavigationDockPanelWidget::OnAddStopClicked()
{
	if (UNavigationPanelWidget* Nav = NavOf(this, GetController()))
	{
		// Opens a row to add stops in (from the list, or by clicking the map from its crosshair), and closes it again.
		bAddingStop = !bAddingStop;
		if (!bAddingStop && Nav->GetSelectionMode() == ENavigationSelectionMode::Stop)
		{
			Nav->CancelSelection();
		}
		RefreshDisplay();
		RefreshRoomDropdowns();
	}
}

void UNavigationDockPanelWidget::OnReverseClicked()
{
	if (UNavigationPanelWidget* Nav = NavOf(this, GetController()))
	{
		Nav->ReversePlaces();
		SelectedRow = INDEX_NONE;
		RefreshDisplay();
	}
}

void UNavigationDockPanelWidget::OnFindRouteClicked()
{
	if (UNavigationPanelWidget* Nav = NavOf(this, GetController()))
	{
		Nav->StartFindRoute();
	}
}

void UNavigationDockPanelWidget::OnCancelFindRouteClicked()
{
	if (UNavigationPanelWidget* Nav = NavOf(this, GetController()))
	{
		Nav->CancelFindRoute();
	}
}

void UNavigationDockPanelWidget::OnClearRouteClicked()
{
	if (UNavigationPanelWidget* Nav = NavOf(this, GetController()))
	{
		Nav->RequestClearRoute();
	}
	SelectedRow = INDEX_NONE;
	bAddingStop = false;
	// A ComboBoxString only fires OnSelectionChanged when the picked option actually changes - clearing what the rows show lets any
	// place (including the one just cleared) be picked again right away.
	for (UNavigationPlaceCombo* Combo : PlaceCombos)
	{
		Combo->ClearSelection();
	}
	RefreshDisplay();
	RefreshRoomDropdowns();
}

void UNavigationDockPanelWidget::OnToggleRouteClicked()
{
	if (UNavigationPanelWidget* Nav = NavOf(this, GetController()))
	{
		Nav->ToggleRouteGuidance();
	}
}

void UNavigationDockPanelWidget::OnNextStepClicked()
{
	if (UNavigationPanelWidget* Nav = NavOf(this, GetController()))
	{
		Nav->NextStep();
	}
}

void UNavigationDockPanelWidget::OnPreviousStepClicked()
{
	if (UNavigationPanelWidget* Nav = NavOf(this, GetController()))
	{
		Nav->PreviousStep();
	}
}

void UNavigationDockPanelWidget::OnStepModeClicked()
{
	if (UNavigationPanelWidget* Nav = NavOf(this, GetController()))
	{
		Nav->SetStepByStep(!Nav->IsStepByStep());
	}
}

void UNavigationDockPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UNavigationPanelWidget* Nav = NavOf(this, GetController());
	if (Nav && Nav->IsFindingRoute())
	{
		if (FindRouteProgress)
		{
			FindRouteProgress->SetPercent(Nav->GetFindRouteProgress());
		}
		if (StatusText)
		{
			StatusText->SetText(FText::FromString(Nav->GetDisplayStatusMessage()));
		}
	}
}

void UNavigationDockPanelWidget::UpdateView()
{
	if (PlanBox)
	{
		PlanBox->SetVisibility(bGoView ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
	if (GoBox)
	{
		GoBox->SetVisibility(bGoView ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	Paint(PlanTabButton, bGoView ? InactiveColor : PickingColor);
	Paint(GoTabButton, bGoView ? PickingColor : InactiveColor);
}

void UNavigationDockPanelWidget::RefreshDisplay()
{
	ANavPlayerController* PC = GetController();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
	if (!Nav)
	{
		return;
	}
	const int32 Count = Nav->GetPlaceCount();
	const bool bHasRoute = !Nav->GetRoutePoints().IsEmpty();
	// A route that has just been found is gone through (Go); with no route there is nothing to go through, and the places are set.
	if (bHasRoute && !bHadRoute)
	{
		bGoView = true;
	}
	if (!bHasRoute)
	{
		bGoView = false;
	}
	bHadRoute = bHasRoute;
	if (SelectedRow >= Count || (SelectedRow != INDEX_NONE && !Nav->IsPlaceSet(SelectedRow)))
	{
		SelectedRow = INDEX_NONE;
	}
	if (Nav->GetStops().Num() >= UNavigationPanelWidget::MaxRouteStops)
	{
		bAddingStop = false;
	}
	UpdateView();
	if (GoTabButton)
	{
		GoTabButton->SetIsEnabled(bHasRoute);
	}

	// ==== Plan ====
	const bool bOutdoor = Nav->IsOutdoorMode();
	const ENavigationSelectionMode Mode = Nav->GetSelectionMode();
	if (ContextText)
	{
		const FString BuildingId = PC->GetLoadedBuildingId();
		ContextText->SetText(FText::FromString(bOutdoor
			? TEXT("Outdoors: pick a building's main door, or click the ground.")
			: (BuildingId.IsEmpty()
				? FString(TEXT("No building loaded"))
				: FString::Printf(TEXT("Building: %s  |  Viewing Floor: %d"), *BuildingId, PC->GetActiveFloor()))));
	}

	AssignSlots(*Nav);
	// The dropdowns are refilled when what the rows show has changed.
	FString Signature;
	for (int32 SlotIndex = 0; SlotIndex < MaxSlots; ++SlotIndex)
	{
		const int32 Row = SlotRow[SlotIndex];
		Signature += FString::Printf(TEXT("%d|%s\n"), Row, Row >= 0 ? *Nav->GetPlaceText(Row) : TEXT(""));
	}
	if (Signature != ShownPlaces)
	{
		ShownPlaces = Signature;
		RefreshRoomDropdowns();
	}
	for (int32 SlotIndex = 0; SlotIndex < MaxSlots; ++SlotIndex)
	{
		const int32 Row = SlotRow[SlotIndex];
		const bool bInUse = Row != INDEX_NONE;
		SlotSizes[SlotIndex]->SetVisibility(bInUse ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (!bInUse)
		{
			continue;
		}
		const bool bPending = Row == PendingRow;
		const bool bEnd = Row == 0 || Row == Count - 1;
		BadgeTexts[SlotIndex]->SetText(FText::FromString(bPending ? FString(TEXT("+")) : PlaceBadge(Row, Count)));
		Paint(BadgeButtons[SlotIndex], bPending ? PendingColor : PlaceColor(Row, Count));
		RoleTexts[SlotIndex]->SetText(FText::FromString(bPending ? FString(TEXT("Add a stop")) : Nav->GetPlaceRole(Row)));
		FieldBorders[SlotIndex]->SetBrushColor(Row == SelectedRow ? FLinearColor::White : FLinearColor::Transparent);
		// The crosshair is for the ends and the row being added; a stop is replaced from the list, or removed and added again.
		PickButtons[SlotIndex]->SetVisibility(bEnd || bPending ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
		const bool bPicking = (Row == 0 && Mode == ENavigationSelectionMode::Start) || (Row == Count - 1 && Mode == ENavigationSelectionMode::Destination)
			|| (bPending && Mode == ENavigationSelectionMode::Stop);
		Paint(PickButtons[SlotIndex], bPicking ? PickingColor : InactiveColor);
	}
	if (ActionsSize)
	{
		ActionsSize->SetVisibility(SelectedRow != INDEX_NONE ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (MoveUpButton)
	{
		MoveUpButton->SetIsEnabled(SelectedRow > 0);
	}
	if (MoveDownButton)
	{
		MoveDownButton->SetIsEnabled(SelectedRow != INDEX_NONE && SelectedRow < Count - 1);
	}
	if (RemoveButton)
	{
		RemoveButton->SetIsEnabled(SelectedRow != INDEX_NONE);
	}
	if (AddStopButtonLabel)
	{
		AddStopButtonLabel->SetText(FText::FromString(bAddingStop ? FString(TEXT("Done")) : FString::Printf(TEXT("+ Add stop (%d/%d)"), Nav->GetStops().Num(), UNavigationPanelWidget::MaxRouteStops)));
	}
	if (AddStopButton)
	{
		AddStopButton->SetIsEnabled(bAddingStop || Nav->GetStops().Num() < UNavigationPanelWidget::MaxRouteStops);
	}
	if (ReverseButton)
	{
		ReverseButton->SetIsEnabled(Nav->GetSetPlaceCount() >= 2);
	}

	if (FindRouteButton)
	{
		FindRouteButton->SetIsEnabled(Nav->CanFindRoute() && !Nav->IsFindingRoute());
		// Green once a route has been found, red if the last attempt came back empty-handed, grey otherwise.
		Paint(FindRouteButton, bHasRoute ? StartColor : (Nav->DidLastFindRouteFail() ? DestinationColor : InactiveColor));
	}
	if (ClearRouteButton)
	{
		ClearRouteButton->SetIsEnabled(Nav->CanClearRoute());
	}
	const bool bFindingRoute = Nav->IsFindingRoute();
	if (CancelFindRouteButton)
	{
		CancelFindRouteButton->SetVisibility(bFindingRoute ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (FindRouteProgress)
	{
		// The bar sits in a size box, which is what is shown or hidden.
		if (UWidget* Holder = FindRouteProgress->GetParent())
		{
			Holder->SetVisibility(bFindingRoute ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
		FindRouteProgress->SetPercent(Nav->GetFindRouteProgress());
	}
	if (ToggleRouteButton)
	{
		ToggleRouteButton->SetIsEnabled(Nav->CanToggleRouteGuidance());
	}
	if (ToggleRouteButtonLabel)
	{
		ToggleRouteButtonLabel->SetText(FText::FromString(Nav->IsRouteGuidanceVisible() ? TEXT("Hide Route") : TEXT("Show Route")));
	}
	if (HintText)
	{
		const int32 SetCount = Nav->GetSetPlaceCount();
		HintText->SetText(FText::FromString(FString::Printf(TEXT("A route needs at least two places (%d set)."), SetCount)));
		HintText->SetVisibility(SetCount < 2 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Nav->GetDisplayStatusMessage()));
		StatusText->SetColorAndOpacity(FSlateColor(Nav->GetStatusColor()));
	}

	// ==== Go ====
	const bool bGuidanceVisible = Nav->IsRouteGuidanceVisible();
	const TArray<UNavigationPanelWidget::FNavigationRouteStep> Steps = Nav->BuildRouteSteps();
	const bool bStepByStep = Nav->IsStepByStep() && !Steps.IsEmpty();
	const int32 CurrentStep = FMath::Clamp(Nav->GetGuidedStepIndex(), 0, FMath::Max(0, Steps.Num() - 1));
	for (int32 Index = 0; Index < OverviewBadges.Num(); ++Index)
	{
		const bool bShown = bHasRoute && Index < Count;
		OverviewBadges[Index]->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bShown)
		{
			OverviewBadges[Index]->SetBrushColor(PlaceColor(Index, Count));
			OverviewTexts[Index]->SetText(FText::FromString(PlaceBadge(Index, Count)));
		}
	}
	if (OverviewInfoText)
	{
		OverviewInfoText->SetText(FText::FromString(FString::Printf(TEXT("%d points"), Nav->GetRoutePoints().Num())));
	}
	if (StepProgress)
	{
		StepProgress->SetPercent(Steps.IsEmpty() ? 0.f : (bStepByStep ? static_cast<float>(CurrentStep + 1) / Steps.Num() : 1.f));
	}
	if (RouteStepsBox)
	{
		FString StepsSignature = bStepByStep ? FString::Printf(TEXT("#%d\x1f"), CurrentStep) : FString(TEXT("*\x1f"));
		for (const UNavigationPanelWidget::FNavigationRouteStep& Step : Steps)
		{
			StepsSignature += Step.Text + (Step.bFinal ? TEXT("\x1f1\n") : TEXT("\x1f0\n"));
		}
		if (StepsSignature != ShownRouteSteps)
		{
			ShownRouteSteps = StepsSignature;
			RouteStepsBox->ClearChildren();
			static const FLinearColor StepColor(0.45f, 0.92f, 1.f, 1.f);
			static const FLinearColor FinalStepColor(0.35f, 0.95f, 0.45f, 1.f);
			auto AddStepText = [&](const FString& Text, const FLinearColor& Color)
			{
				UTextBlock* StepText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
				StepText->SetText(FText::FromString(Text));
				StepText->SetColorAndOpacity(FSlateColor(Color));
				WrapText(StepText);
				ApplyStandardFont(StepText);
				RouteStepsBox->AddChildToVerticalBox(StepText)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
			};
			if (bStepByStep)
			{
				AddStepText(FString::Printf(TEXT("Step %d of %d"), CurrentStep + 1, Steps.Num()), FLinearColor(0.7f, 0.78f, 0.88f, 1.f));
				AddStepText(Steps[CurrentStep].Text, Steps[CurrentStep].bFinal ? FinalStepColor : StepColor);
			}
			else
			{
				for (int32 Index = 0; Index < Steps.Num(); ++Index)
				{
					AddStepText(FString::Printf(TEXT("Step %d: %s"), Index + 1, *Steps[Index].Text), Steps[Index].bFinal ? FinalStepColor : StepColor);
				}
			}
		}
		RouteStepsBox->SetVisibility(bGuidanceVisible && !Steps.IsEmpty() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	// Previous / Next only when going step by step; the switch is there whenever there is a route.
	const bool bHasSteps = bGuidanceVisible && !Steps.IsEmpty();
	if (StepButtonsSize)
	{
		StepButtonsSize->SetVisibility(bHasSteps && bStepByStep ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (PreviousStepButton)
	{
		PreviousStepButton->SetIsEnabled(CurrentStep > 0);
	}
	if (NextStepButton)
	{
		NextStepButton->SetIsEnabled(CurrentStep + 1 < Steps.Num());
	}
	if (NextStepButtonLabel)
	{
		NextStepButtonLabel->SetText(FText::FromString(CurrentStep + 1 < Steps.Num() ? TEXT("Next Step") : TEXT("Arrived")));
	}
	if (StepModeButton)
	{
		StepModeButton->SetVisibility(bHasSteps ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (StepModeButtonLabel)
	{
		StepModeButtonLabel->SetText(FText::FromString(bStepByStep ? TEXT("Show All Steps") : TEXT("Go Step by Step")));
	}
	// A route that has no steps (not planned here) is shown as the text of what was chosen.
	if (RouteDetailsText)
	{
		RouteDetailsText->SetText(FText::FromString(Steps.IsEmpty() && bHasRoute ? Nav->BuildRouteDetails() : FString()));
		RouteDetailsText->SetVisibility(bGuidanceVisible && Steps.IsEmpty() && bHasRoute ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UNavigationDockPanelWidget::RefreshRoomDropdowns()
{
	ANavPlayerController* PC = GetController();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
	if (!Nav || PlaceCombos.IsEmpty())
	{
		return;
	}
	AssignSlots(*Nav);
	// One search narrows every list.
	const FString Query = DestinationSearchBox ? DestinationSearchBox->GetText().ToString() : FString();
	const TArray<FRoomOption> Matches = Nav->SearchDestinations(Query);
	for (int32 SlotIndex = 0; SlotIndex < MaxSlots; ++SlotIndex)
	{
		const int32 Row = SlotRow[SlotIndex];
		if (Row == INDEX_NONE)
		{
			continue;
		}
		const FString Current = Row >= 0 ? Nav->GetPlaceText(Row) : FString();
		FillPlaceCombo(PlaceCombos[SlotIndex], Matches, Current);
		PlaceCombos[SlotIndex]->SetIsEnabled(!Matches.IsEmpty());
	}
	if (DestinationSearchResultsText)
	{
		DestinationSearchResultsText->SetText(FText::FromString(Matches.IsEmpty()
			? TEXT("No matching places. Try another name or tag.")
			: (Nav->IsOutdoorMode()
				? FString::Printf(TEXT("%d main doors on the campus."), Matches.Num())
				: FString::Printf(TEXT("%d rooms in this building."), Matches.Num()))));
	}
}

void UNavigationDockPanelWidget::HandleSearchChanged(const FText& Text)
{
	RefreshRoomDropdowns();
}

UWidget* UNavigationDockPanelWidget::HandleGenerateComboBoxItem(FString Item)
{
	UTextBlock* ItemText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	ItemText->SetText(FText::FromString(Item));
	ItemText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ApplyStandardFont(ItemText);
	constexpr int32 MaxRetained = 400;
	RetainedComboItems.Add(ItemText);
	if (RetainedComboItems.Num() > MaxRetained)
	{
		RetainedComboItems.RemoveAt(0, RetainedComboItems.Num() - MaxRetained);
	}
	return ItemText;
}
