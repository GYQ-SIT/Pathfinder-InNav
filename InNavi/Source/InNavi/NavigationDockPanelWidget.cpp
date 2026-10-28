#include "NavigationDockPanelWidget.h"

#include "NavPlayerController.h"
#include "NavigationPanelWidget.h"
#include "StartScreenStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
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

	void SetParentSizeBoxWidth(UWidget* Child, float Width)
	{
		if (Child)
		{
			if (USizeBox* SizeBox = Cast<USizeBox>(Child->GetParent()))
			{
				SizeBox->SetWidthOverride(Width);
			}
		}
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

		ContextText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_NavContext"));
		ContextText->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.78f, 0.88f, 1.f)));
		WrapText(ContextText);
		ApplyStandardFont(ContextText);
		Stack->AddChildToVerticalBox(ContextText)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

		// Room dropdowns - an alternative to clicking a point on the map. Populated with every real place that can be
		// picked, so one pick can jump straight to a room on a different floor than the one currently shown.
		auto AddRoomDropdown = [this, Stack](const TCHAR* Name, const TCHAR* LabelText, UBorder*& OutBorder, UTextBlock*& OutLabel) -> UComboBoxString*
		{
			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString(Name) + TEXT("Label")));
			Label->SetText(FText::FromString(FString(LabelText)));
			Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(Label);
			Stack->AddChildToVerticalBox(Label)->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
			OutLabel = Label;

			UComboBoxString* ComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), FName(Name));
			ComboBox->OnGenerateWidgetEvent.BindDynamic(this, &UNavigationDockPanelWidget::HandleGenerateComboBoxItem);
			StartStyle::DarkCombo(ComboBox);
			USizeBox* ComboSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			ComboSizeBox->SetWidthOverride(ContentWidth);
			ComboSizeBox->SetContent(ComboBox);

			// Frame that tints green/red when this endpoint was set by picking from THIS dropdown, rather than by clicking
			// the map - see RefreshDisplay. Transparent by default so an unset or click-set dropdown shows no frame.
			UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(FString(Name) + TEXT("Border")));
			Border->SetPadding(FMargin(3.f));
			Border->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
			Border->SetContent(ComboSizeBox);
			Stack->AddChildToVerticalBox(Border)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
			OutBorder = Border;
			return ComboBox;
		};

		// One search box for both dropdowns: it narrows the Start list and the Destination list alike, across every floor of
		// the open building (or every main door outside).
		DestinationSearchBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_DestinationSearch"));
		ApplyStandardFont(DestinationSearchBox);
		DestinationSearchBox->SetWidgetStyle(StartStyle::Field(13.f));
		DestinationSearchBox->SetHintText(FText::FromString(TEXT("Search by name, ID or tag")));
		DestinationSearchBox->OnTextChanged.AddUniqueDynamic(this, &UNavigationDockPanelWidget::HandleSearchChanged);
		USizeBox* SearchSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SearchSize->SetWidthOverride(ContentWidth);
		SearchSize->SetContent(DestinationSearchBox);
		Stack->AddChildToVerticalBox(SearchSize)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		DestinationSearchResultsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_DestinationSearchResults"));
		WrapText(DestinationSearchResultsText);
		ApplyStandardFont(DestinationSearchResultsText);
		Stack->AddChildToVerticalBox(DestinationSearchResultsText)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		StartRoomComboBox = AddRoomDropdown(TEXT("Cbx_NavStartRoom"), TEXT("Start Room"), StartRoomBorder, StartLabelText);
		StartRoomComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UNavigationDockPanelWidget::HandleStartRoomChanged);

		DestinationRoomComboBox = AddRoomDropdown(TEXT("Cbx_NavDestinationRoom"), TEXT("Destination Room"), DestinationRoomBorder, DestinationLabelText);
		DestinationRoomComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UNavigationDockPanelWidget::HandleDestinationRoomChanged);

		// Stops: places to pass on the way. The list is the same as the Destination's (and the one search narrows it).
		UTextBlock* UnusedStopLabel = nullptr;
		StopRoomComboBox = AddRoomDropdown(TEXT("Cbx_NavStopRoom"), TEXT("Stop to Add (optional)"), StopRoomBorder, UnusedStopLabel);
		StopRoomComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UNavigationDockPanelWidget::HandleStopRoomChanged);

		auto AddButton = [this, Stack](const TCHAR* Name, const TCHAR* Label, UTextBlock*& OutLabel) -> UButton*
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
			Stack->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
			OutLabel = Text;
			return Button;
		};

		UTextBlock* UnusedFindRouteLabel = nullptr;
		UTextBlock* UnusedClearRouteLabel = nullptr;
		SetStartButton = AddButton(TEXT("Btn_NavSetStart"), TEXT("Click to Set Start"), SetStartButtonLabel);
		SetStartButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnSetStartClicked);
		SetDestinationButton = AddButton(TEXT("Btn_NavSetDestination"), TEXT("Click to Set Destination"), SetDestinationButtonLabel);
		SetDestinationButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnSetDestinationClicked);
		AddStopButton = AddButton(TEXT("Btn_NavAddStop"), TEXT("Add Stop"), AddStopButtonLabel);
		AddStopButton->SetIsEnabled(false);
		AddStopButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnAddStopClicked);
		StopsComboBox = AddRoomDropdown(TEXT("Cbx_NavStops"), TEXT("Stops in Order"), StopsBorder, StopsLabelText);
		StopsComboBox->SetIsEnabled(false);
		StopsComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UNavigationDockPanelWidget::HandleStopSelected);
		UHorizontalBox* StopActions = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row_NavStopActions"));
		auto AddStopAction = [this, StopActions](const TCHAR* Name, const TCHAR* Label) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetStyle(StartStyle::SecondaryButton());
			Button->SetBackgroundColor(FLinearColor::White);
			Button->SetIsEnabled(false);
			Button->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::PlayButtonClickSound);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString(Name) + TEXT("Label")));
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetJustification(ETextJustify::Center);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			WrapText(Text);
			ApplyStandardFont(Text);
			USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			SizeBox->SetMinDesiredHeight(NavButtonHeight);
			SizeBox->SetContent(Text);
			Button->SetContent(SizeBox);
			UHorizontalBoxSlot* Slot = StopActions->AddChildToHorizontalBox(Button);
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Slot->SetPadding(FMargin(2.f, 0.f));
			return Button;
		};
		MoveStopUpButton = AddStopAction(TEXT("Btn_NavMoveStopUp"), TEXT("Up"));
		MoveStopUpButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnMoveStopUpClicked);
		MoveStopDownButton = AddStopAction(TEXT("Btn_NavMoveStopDown"), TEXT("Down"));
		MoveStopDownButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnMoveStopDownClicked);
		RemoveStopButton = AddStopAction(TEXT("Btn_NavRemoveStop"), TEXT("Remove"));
		RemoveStopButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnRemoveStopClicked);
		StopActionsSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Row_NavStopActionsSize"));
		StopActionsSize->SetWidthOverride(ContentWidth);
		StopActionsSize->SetContent(StopActions);
		Stack->AddChildToVerticalBox(StopActionsSize)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		FindRouteButton = AddButton(TEXT("Btn_NavFindRoute"), TEXT("Find Route"), UnusedFindRouteLabel);
		FindRouteButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnFindRouteClicked);
		// Only while a route is being searched for: how far it is, and a way to stop it.
		FindRouteProgress = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("Bar_NavFindRouteProgress"));
		FindRouteProgress->SetFillColorAndOpacity(FLinearColor(0.3f, 0.85f, 0.4f, 1.f));
		FindRouteProgress->SetPercent(0.f);
		USizeBox* ProgressSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Bar_NavFindRouteProgressSize"));
		ProgressSize->SetWidthOverride(ContentWidth);
		ProgressSize->SetHeightOverride(10.f);
		ProgressSize->SetContent(FindRouteProgress);
		ProgressSize->SetVisibility(ESlateVisibility::Collapsed);
		Stack->AddChildToVerticalBox(ProgressSize)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		UTextBlock* UnusedCancelLabel = nullptr;
		CancelFindRouteButton = AddButton(TEXT("Btn_NavCancelFindRoute"), TEXT("Cancel Search"), UnusedCancelLabel);
		CancelFindRouteButton->SetVisibility(ESlateVisibility::Collapsed);
		CancelFindRouteButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnCancelFindRouteClicked);
		ClearRouteButton = AddButton(TEXT("Btn_NavClearRoute"), TEXT("Clear Route"), UnusedClearRouteLabel);
		ClearRouteButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnClearRouteClicked);
		ToggleRouteButton = AddButton(TEXT("Btn_NavToggleRoute"), TEXT("Hide Route"), ToggleRouteButtonLabel);
		ToggleRouteButton->SetIsEnabled(false);
		ToggleRouteButton->SetToolTipText(FText::FromString(TEXT("Show or hide route lines, direction arrows, markers and instructions without clearing your route")));
		ToggleRouteButton->OnClicked.AddUniqueDynamic(this, &UNavigationDockPanelWidget::OnToggleRouteClicked);

		StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_NavStatus"));
		WrapText(StatusText);
		ApplyStandardFont(StatusText);
		Stack->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		RouteDetailsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_NavRouteDetails"));
		RouteDetailsText->SetColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.92f, 1.f, 1.f)));
		WrapText(RouteDetailsText);
		ApplyStandardFont(RouteDetailsText);
		Stack->AddChildToVerticalBox(RouteDetailsText);

		RouteStepsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box_NavRouteSteps"));
		RouteStepsBox->SetVisibility(ESlateVisibility::Collapsed);
		Stack->AddChildToVerticalBox(RouteStepsBox);
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
	SetButtonWidth(SetStartButton, Width);
	SetButtonWidth(SetDestinationButton, Width);
	SetButtonWidth(FindRouteButton, Width);
	SetButtonWidth(AddStopButton, Width);
	SetParentSizeBoxWidth(StopRoomComboBox, Width);
	SetParentSizeBoxWidth(StopsComboBox, Width);
	if (StopActionsSize)
	{
		StopActionsSize->SetWidthOverride(Width);
	}
	SetButtonWidth(CancelFindRouteButton, Width);
	SetParentSizeBoxWidth(FindRouteProgress, Width);
	SetButtonWidth(ClearRouteButton, Width);
	SetButtonWidth(ToggleRouteButton, Width);
	SetParentSizeBoxWidth(DestinationSearchBox, Width);
	SetParentSizeBoxWidth(StartRoomComboBox, Width);
	SetParentSizeBoxWidth(DestinationRoomComboBox, Width);
}

void UNavigationDockPanelWidget::OnSetStartClicked()
{
	if (ANavPlayerController* PC = GetController())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->ToggleSelectStart();
		}
	}
}

void UNavigationDockPanelWidget::OnSetDestinationClicked()
{
	if (ANavPlayerController* PC = GetController())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->ToggleSelectDestination();
		}
	}
}

void UNavigationDockPanelWidget::OnFindRouteClicked()
{
	if (ANavPlayerController* PC = GetController())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->StartFindRoute();
		}
	}
}

void UNavigationDockPanelWidget::OnCancelFindRouteClicked()
{
	if (ANavPlayerController* PC = GetController())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->CancelFindRoute();
		}
	}
}

void UNavigationDockPanelWidget::OnAddStopClicked()
{
	ANavPlayerController* PC = GetController();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
	if (!Nav || !StopRoomComboBox || StopRoomComboBox->GetSelectedOption().IsEmpty())
	{
		return;
	}
	if (Nav->AddStopToRoom(StopRoomComboBox->GetSelectedOption()))
	{
		// A ComboBoxString only reports a changed pick: clearing it lets the same place be added again.
		StopRoomComboBox->ClearSelection();
		RefreshStops();
		// The stop just added is the one chosen in the list.
		StopsComboBox->SetSelectedIndex(Nav->GetStops().Num() - 1);
		UpdateStopControls();
	}
}

void UNavigationDockPanelWidget::OnMoveStopUpClicked()
{
	MoveSelectedStop(-1);
}

void UNavigationDockPanelWidget::OnMoveStopDownClicked()
{
	MoveSelectedStop(1);
}

void UNavigationDockPanelWidget::MoveSelectedStop(int32 Offset)
{
	ANavPlayerController* PC = GetController();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
	const int32 Index = StopsComboBox ? StopsComboBox->GetSelectedIndex() : INDEX_NONE;
	if (Nav && Index != INDEX_NONE && Nav->MoveStop(Index, Index + Offset))
	{
		RefreshStops();
		StopsComboBox->SetSelectedIndex(Index + Offset);
		UpdateStopControls();
	}
}

void UNavigationDockPanelWidget::OnRemoveStopClicked()
{
	ANavPlayerController* PC = GetController();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
	const int32 Index = StopsComboBox ? StopsComboBox->GetSelectedIndex() : INDEX_NONE;
	if (Nav && Index != INDEX_NONE && Nav->RemoveStop(Index))
	{
		// The list is rebuilt with the entry that took its place (or the last) chosen.
		RefreshStops();
	}
}

void UNavigationDockPanelWidget::HandleStopRoomChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType != ESelectInfo::Direct)
	{
		UpdateStopControls();
	}
}

void UNavigationDockPanelWidget::HandleStopSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType != ESelectInfo::Direct)
	{
		UpdateStopControls();
	}
}

void UNavigationDockPanelWidget::RefreshStops()
{
	ANavPlayerController* PC = GetController();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
	if (!Nav || !StopsComboBox)
	{
		return;
	}
	const TArray<FNavigationStop>& Stops = Nav->GetStops();
	if (StopsLabelText)
	{
		StopsLabelText->SetText(FText::FromString(FString::Printf(TEXT("Stops in Order (%d/%d)"), Stops.Num(), UNavigationPanelWidget::MaxRouteStops)));
	}
	FString Signature;
	for (int32 Index = 0; Index < Stops.Num(); ++Index)
	{
		Signature += Nav->GetStopLabel(Index) + TEXT("\n");
	}
	if (Signature != ShownStops)
	{
		const int32 Previous = StopsComboBox->GetSelectedIndex();
		ShownStops = Signature;
		StopsComboBox->ClearOptions();
		for (int32 Index = 0; Index < Stops.Num(); ++Index)
		{
			StopsComboBox->AddOption(FString::Printf(TEXT("%d. %s"), Index + 1, *Nav->GetStopLabel(Index)));
		}
		if (!Stops.IsEmpty())
		{
			StopsComboBox->SetSelectedIndex(FMath::Clamp(Previous, 0, Stops.Num() - 1));
		}
	}
	StopsComboBox->SetIsEnabled(!Stops.IsEmpty());
	UpdateStopControls();
}

void UNavigationDockPanelWidget::UpdateStopControls()
{
	ANavPlayerController* PC = GetController();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
	const int32 Count = Nav ? Nav->GetStops().Num() : 0;
	const int32 Chosen = StopsComboBox ? StopsComboBox->GetSelectedIndex() : INDEX_NONE;
	if (AddStopButton)
	{
		AddStopButton->SetIsEnabled(StopRoomComboBox && !StopRoomComboBox->GetSelectedOption().IsEmpty() && Count < UNavigationPanelWidget::MaxRouteStops);
	}
	if (AddStopButtonLabel)
	{
		AddStopButtonLabel->SetText(FText::FromString(FString::Printf(TEXT("Add Stop (%d/%d)"), Count, UNavigationPanelWidget::MaxRouteStops)));
	}
	if (MoveStopUpButton)
	{
		MoveStopUpButton->SetIsEnabled(Chosen > 0);
	}
	if (MoveStopDownButton)
	{
		MoveStopDownButton->SetIsEnabled(Chosen >= 0 && Chosen + 1 < Count);
	}
	if (RemoveStopButton)
	{
		RemoveStopButton->SetIsEnabled(Chosen >= 0 && Chosen < Count);
	}
}

void UNavigationDockPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	ANavPlayerController* PC = GetController();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
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

void UNavigationDockPanelWidget::OnClearRouteClicked()
{
	if (ANavPlayerController* PC = GetController())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->RequestClearRoute();
		}
	}

	// A ComboBoxString only fires OnSelectionChanged when the picked option actually changes - if we left the old room
	// name showing, picking that SAME room again after Clear Route would silently do nothing. Clearing the visible
	// selection here lets any room (including the one just cleared) be picked again right away.
	if (StartRoomComboBox)
	{
		StartRoomComboBox->ClearSelection();
	}
	if (DestinationRoomComboBox)
	{
		DestinationRoomComboBox->ClearSelection();
	}
}

void UNavigationDockPanelWidget::OnToggleRouteClicked()
{
	if (ANavPlayerController* PC = GetController())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->ToggleRouteGuidance();
		}
	}
}

void UNavigationDockPanelWidget::RefreshDisplay()
{
	ANavPlayerController* PC = GetController();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
	if (!Nav)
	{
		return;
	}

	const ENavigationSelectionMode Mode = Nav->GetSelectionMode();
	static const FLinearColor InactiveButtonColor = StartStyle::Rgb(27, 41, 63);
	static const FLinearColor ActiveStartColor = StartStyle::Rgb(26, 138, 84);
	static const FLinearColor ActiveDestinationColor = StartStyle::Rgb(180, 52, 62);
	// The buttons keep their outlined shape and take the state's colour as their fill.
	auto PaintButton = [](UButton* Button, const FLinearColor& Color)
	{
		Button->SetStyle(Color == InactiveButtonColor ? StartStyle::SecondaryButton() : StartStyle::StatusButton(Color));
	};
	static const FLinearColor DropdownUnsetColor(0.f, 0.f, 0.f, 0.f);

	// Whichever of the button / dropdown-frame actually set the point gets the color; the other one stays neutral, so a
	// glance says whether the current Start/Destination came from clicking the map or from the dropdown.
	const bool bStartActivelyPicking = Mode == ENavigationSelectionMode::Start;
	const bool bStartSetViaDropdown = Nav->WasStartSetViaDropdown();
	const bool bStartSetViaClick = Nav->HasStart() && !bStartSetViaDropdown;
	if (SetStartButton)
	{
		PaintButton(SetStartButton, bStartActivelyPicking || bStartSetViaClick ? ActiveStartColor : InactiveButtonColor);
	}
	if (SetStartButtonLabel)
	{
		SetStartButtonLabel->SetText(FText::FromString(bStartActivelyPicking
			? TEXT("Click a Start Location (Cancel)") : TEXT("Click to Set Start")));
	}
	if (StartRoomBorder)
	{
		StartRoomBorder->SetBrushColor(bStartSetViaDropdown ? ActiveStartColor : DropdownUnsetColor);
	}

	// A Start / Destination picked in another building is an extra entry in its dropdown; rebuild the lists when that changes.
	if (Nav->GetStartOtherBuildingLabel() != ShownOtherBuildingStart || Nav->GetDestinationOtherBuildingLabel() != ShownOtherBuildingDestination)
	{
		RefreshRoomDropdowns();
	}
	// Keep the dropdown's own displayed text in sync with who actually set Start last - without this, picking a room from
	// the dropdown and then overwriting Start by clicking the map left the old room's name still showing.
	if (StartRoomComboBox)
	{
		if (!ShownOtherBuildingStart.IsEmpty())
		{
			StartRoomComboBox->SetSelectedOption(ShownOtherBuildingStart);
		}
		else if (bStartSetViaDropdown)
		{
			StartRoomComboBox->SetSelectedOption(Nav->GetStartRoomLabel());
		}
		else
		{
			StartRoomComboBox->ClearSelection();
		}
	}

	const bool bDestActivelyPicking = Mode == ENavigationSelectionMode::Destination;
	const bool bDestSetViaDropdown = Nav->WasDestinationSetViaDropdown();
	const bool bDestSetViaClick = Nav->HasDestination() && !bDestSetViaDropdown;
	if (SetDestinationButton)
	{
		PaintButton(SetDestinationButton, bDestActivelyPicking || bDestSetViaClick ? ActiveDestinationColor : InactiveButtonColor);
	}
	if (SetDestinationButtonLabel)
	{
		SetDestinationButtonLabel->SetText(FText::FromString(bDestActivelyPicking
			? TEXT("Click a Destination (Cancel)") : TEXT("Click to Set Destination")));
	}
	if (DestinationRoomBorder)
	{
		DestinationRoomBorder->SetBrushColor(bDestSetViaDropdown ? ActiveDestinationColor : DropdownUnsetColor);
	}
	if (DestinationRoomComboBox)
	{
		if (!ShownOtherBuildingDestination.IsEmpty())
		{
			DestinationRoomComboBox->SetSelectedOption(ShownOtherBuildingDestination);
		}
		else if (bDestSetViaDropdown)
		{
			DestinationRoomComboBox->SetSelectedOption(Nav->GetDestinationRoomLabel());
		}
		else
		{
			DestinationRoomComboBox->ClearSelection();
		}
	}
	if (FindRouteButton)
	{
		FindRouteButton->SetIsEnabled(Nav->CanFindRoute() && !Nav->IsFindingRoute());
		// Green once a route's actually been found, red if the last attempt came back empty-handed, gray otherwise
		// (never tried, or cleared since).
		FLinearColor FindRouteColor = InactiveButtonColor;
		if (!Nav->GetRoutePoints().IsEmpty())
		{
			FindRouteColor = ActiveStartColor;
		}
		else if (Nav->DidLastFindRouteFail())
		{
			FindRouteColor = ActiveDestinationColor;
		}
		PaintButton(FindRouteButton, FindRouteColor);
	}
	if (ClearRouteButton)
	{
		ClearRouteButton->SetIsEnabled(Nav->CanClearRoute());
	}
	RefreshStops();
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
	// Outside there is no open building: the dropdowns hold each building's main door and a click picks a spot on the ground.
	const bool bOutdoor = Nav->IsOutdoorMode();
	if (ContextText)
	{
		const FString BuildingId = PC->GetLoadedBuildingId();
		ContextText->SetText(FText::FromString(bOutdoor
			? TEXT("Outdoors: pick a building's main door, or click the ground.")
			: (BuildingId.IsEmpty()
				? FString(TEXT("No building loaded"))
				: FString::Printf(TEXT("Building: %s  |  Viewing Floor: %d"), *BuildingId, PC->GetActiveFloor()))));
	}
	if (StartLabelText)
	{
		StartLabelText->SetText(FText::FromString(bOutdoor ? TEXT("Start (main door)") : TEXT("Start Room")));
	}
	if (DestinationLabelText)
	{
		DestinationLabelText->SetText(FText::FromString(bOutdoor ? TEXT("Destination (main door)") : TEXT("Destination Room")));
	}
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Nav->GetDisplayStatusMessage()));
		StatusText->SetColorAndOpacity(FSlateColor(Nav->GetStatusColor()));
	}
	// The route found is shown as steps (each its own line, the last one in green); until there is a route, the plain text shows
	// what has been chosen so far.
	const bool bGuidanceVisible = Nav->IsRouteGuidanceVisible();
	const TArray<UNavigationPanelWidget::FNavigationRouteStep> Steps = Nav->BuildRouteSteps();
	if (RouteStepsBox)
	{
		FString Signature;
		for (const UNavigationPanelWidget::FNavigationRouteStep& Step : Steps)
		{
			Signature += Step.Text + (Step.bFinal ? TEXT("\x1f1\n") : TEXT("\x1f0\n"));
		}
		if (Signature != ShownRouteSteps)
		{
			ShownRouteSteps = Signature;
			RouteStepsBox->ClearChildren();
			static const FLinearColor StepColor(0.45f, 0.92f, 1.f, 1.f);
			static const FLinearColor FinalStepColor(0.35f, 0.95f, 0.45f, 1.f);
			for (int32 Index = 0; Index < Steps.Num(); ++Index)
			{
				UTextBlock* StepText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
				StepText->SetText(FText::FromString(FString::Printf(TEXT("Step %d: %s"), Index + 1, *Steps[Index].Text)));
				StepText->SetColorAndOpacity(FSlateColor(Steps[Index].bFinal ? FinalStepColor : StepColor));
				WrapText(StepText);
				ApplyStandardFont(StepText);
				RouteStepsBox->AddChildToVerticalBox(StepText)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
			}
		}
		RouteStepsBox->SetVisibility(bGuidanceVisible && !Steps.IsEmpty() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (RouteDetailsText)
	{
		RouteDetailsText->SetText(FText::FromString(Steps.IsEmpty() ? Nav->BuildRouteDetails() : FString()));
		RouteDetailsText->SetVisibility(bGuidanceVisible && Steps.IsEmpty() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

namespace
{
	using FRoomOption = UNavigationPanelWidget::FNavigationRoomOption;

	// Fills a dropdown with the places the search matches and puts its displayed text back (ClearOptions wipes it, but does not
	// touch the navigation state). A place picked from the dropdown stays shown whatever the search says: another building's
	// place (OtherBuildingLabel) as the first entry, and the open building's own place as one too if the search filters it out.
	void FillRoomDropdown(UComboBoxString* Combo, const TArray<FRoomOption>& Matches, const FString& OtherBuildingLabel, bool bSetViaDropdown, const FString& OwnLabel)
	{
		const FString Previous = Combo->GetSelectedOption();
		Combo->ClearOptions();
		if (!OtherBuildingLabel.IsEmpty())
		{
			Combo->AddOption(OtherBuildingLabel);
		}
		else if (bSetViaDropdown && !OwnLabel.IsEmpty() && !Matches.ContainsByPredicate([&OwnLabel](const FRoomOption& Option) { return Option.GetDisplayLabel() == OwnLabel; }))
		{
			Combo->AddOption(OwnLabel);
		}
		for (const FRoomOption& Option : Matches)
		{
			Combo->AddOption(Option.GetDisplayLabel());
		}
		if (!OtherBuildingLabel.IsEmpty())
		{
			Combo->SetSelectedOption(OtherBuildingLabel);
		}
		else if (bSetViaDropdown)
		{
			Combo->SetSelectedOption(OwnLabel);
		}
		else if (Combo->FindOptionIndex(Previous) != INDEX_NONE)
		{
			Combo->SetSelectedOption(Previous);
		}
	}
}

void UNavigationDockPanelWidget::RefreshRoomDropdowns()
{
	if (!StartRoomComboBox || !DestinationRoomComboBox)
	{
		return;
	}
	ANavPlayerController* PC = GetController();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
	if (!Nav)
	{
		return;
	}

	// One search narrows both lists. Start keeps the building's order (floor by floor), Destination is by name.
	const FString Query = DestinationSearchBox ? DestinationSearchBox->GetText().ToString() : FString();
	const TArray<FRoomOption> StartMatches = Nav->SearchRooms(Query);
	const TArray<FRoomOption> DestinationMatches = Nav->SearchDestinations(Query);

	ShownOtherBuildingStart = Nav->GetStartOtherBuildingLabel();
	FillRoomDropdown(StartRoomComboBox, StartMatches, ShownOtherBuildingStart, Nav->WasStartSetViaDropdown(), Nav->GetStartRoomLabel());
	ShownOtherBuildingDestination = Nav->GetDestinationOtherBuildingLabel();
	FillRoomDropdown(DestinationRoomComboBox, DestinationMatches, ShownOtherBuildingDestination, Nav->WasDestinationSetViaDropdown(), Nav->GetDestinationRoomLabel());

	StartRoomComboBox->SetIsEnabled(!StartMatches.IsEmpty());
	DestinationRoomComboBox->SetIsEnabled(!DestinationMatches.IsEmpty());
	if (StopRoomComboBox)
	{
		const FString PreviousStop = StopRoomComboBox->GetSelectedOption();
		StopRoomComboBox->ClearOptions();
		for (const FRoomOption& Option : DestinationMatches)
		{
			StopRoomComboBox->AddOption(Option.GetDisplayLabel());
		}
		if (StopRoomComboBox->FindOptionIndex(PreviousStop) != INDEX_NONE)
		{
			StopRoomComboBox->SetSelectedOption(PreviousStop);
		}
		StopRoomComboBox->SetIsEnabled(!DestinationMatches.IsEmpty());
		UpdateStopControls();
	}
	if (DestinationSearchResultsText)
	{
		DestinationSearchResultsText->SetText(FText::FromString(DestinationMatches.IsEmpty()
			? TEXT("No matching places. Try another name or tag.")
			: (Nav->IsOutdoorMode()
				? FString::Printf(TEXT("%d main doors on the campus. Choose one below."), DestinationMatches.Num())
				: FString::Printf(TEXT("%d rooms in this building. Choose one below."), DestinationMatches.Num()))));
	}
}

void UNavigationDockPanelWidget::HandleStartRoomChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// Only react to the user's own pick, not this panel's own SetSelectedOption calls. The extra entry for a Start in another
	// building is only a display of the current Start; picking it changes nothing.
	if (SelectionType == ESelectInfo::Direct || (!ShownOtherBuildingStart.IsEmpty() && SelectedItem == ShownOtherBuildingStart))
	{
		return;
	}
	if (ANavPlayerController* PC = GetController())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->SetStartToRoom(SelectedItem);
		}
	}
}

void UNavigationDockPanelWidget::HandleDestinationRoomChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct || (!ShownOtherBuildingDestination.IsEmpty() && SelectedItem == ShownOtherBuildingDestination))
	{
		return;
	}
	if (ANavPlayerController* PC = GetController())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->SetDestinationToRoom(SelectedItem);
		}
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
