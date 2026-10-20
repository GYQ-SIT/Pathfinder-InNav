#include "MapPanelWidget.h"

#include "NavPlayerController.h"
#include "RoadSegmentActor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	constexpr float MapRowBottomPadding = 4.f;
}

void UMapPanelWidget::AddSectionHeader(UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
	Text->SetText(FText::FromString(FString(Label)));
	Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ApplyStandardFont(Text);
	Stack->AddChildToVerticalBox(Text)->SetPadding(FMargin(0.f, 8.f, 0.f, 2.f));
}

USizeBox* UMapPanelWidget::WrapInPanelWidth(UWidget* Control)
{
	USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	SizeBox->SetWidthOverride(ContentWidth);
	SizeBox->SetContent(Control);
	return SizeBox;
}

TSharedRef<SWidget> UMapPanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// The dock frame paints the panel background; this root only adds the usual panel padding.
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_Map"));
		Root->SetBrushColor(FLinearColor::Transparent);
		Root->SetPadding(FMargin(14.f));
		WidgetTree->RootWidget = Root;

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MapStack"));
		Root->SetContent(Stack);

		// With only one section the header would say nothing the tab does not already say.
		if (bShowBuildingFilters)
		{
			AddSectionHeader(Stack, TEXT("Txt_MapRoads"), TEXT("Roads"));
		}
		RoadHideAllCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadHideAll"), TEXT("Hide All"), false, MapRowBottomPadding);
		RoadHideAllCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadHideAllCheckChanged);
		RoadPrimaryCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadPrimary"), TEXT("Primary"), true, MapRowBottomPadding);
		RoadPrimaryCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadPrimaryCheckChanged);
		RoadSecondaryCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadSecondary"), TEXT("Secondary"), true, MapRowBottomPadding);
		RoadSecondaryCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadSecondaryCheckChanged);
		RoadResidentialCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadResidential"), TEXT("Residential/Tertiary"), true, MapRowBottomPadding);
		RoadResidentialCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadResidentialCheckChanged);
		RoadCyclewayCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadCycleway"), TEXT("Cycleway"), true, MapRowBottomPadding);
		RoadCyclewayCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadCyclewayCheckChanged);
		RoadFootwayCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadFootway"), TEXT("Footway/Path"), true, MapRowBottomPadding);
		RoadFootwayCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadFootwayCheckChanged);
		RoadOtherCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadOther"), TEXT("Other"), true, MapRowBottomPadding);
		RoadOtherCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadOtherCheckChanged);
		// The region limit only ever affects roads and POIs, so it is one more row of this section. It starts checked;
		// ApplyDefaultsToController makes the controller agree.
		RoadCampusOnlyCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadCampusOnly"), TEXT("Campus Only"), true, MapRowBottomPadding);
		RoadCampusOnlyCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadCampusOnlyCheckChanged);

		if (bShowBuildingFilters)
		{
			AddSectionHeader(Stack, TEXT("Txt_MapBuildings"), TEXT("Buildings"));
			BuildingCampusCheckbox = AddCheckRow(Stack, TEXT("Chk_BuildingCampus"), TEXT("Campus"), true, MapRowBottomPadding);
			BuildingCampusCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnBuildingCampusCheckChanged);
			// Starts unchecked: buildings outside the campus are hidden by default.
			BuildingNonCampusCheckbox = AddCheckRow(Stack, TEXT("Chk_BuildingNonCampus"), TEXT("Non-Campus"), false, MapRowBottomPadding);
			BuildingNonCampusCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnBuildingNonCampusCheckChanged);

			BuildingSearchBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_BuildingSearch"));
			ApplyStandardFont(BuildingSearchBox);
			BuildingSearchBox->SetHintText(FText::FromString(TEXT("Campus building name or ID")));
			BuildingSearchBox->OnTextChanged.AddUniqueDynamic(this, &UMapPanelWidget::HandleBuildingSearchChanged);
			Stack->AddChildToVerticalBox(WrapInPanelWidth(BuildingSearchBox))->SetPadding(FMargin(0.f, 6.f, 0.f, 4.f));

			BuildingFloorFilter = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Cbx_BuildingFloorFilter"));
			for (const TCHAR* Item : { TEXT("Any floor count"), TEXT("1 floor"), TEXT("2-5 floors"), TEXT("6+ floors") })
			{
				BuildingFloorFilter->AddOption(Item);
			}
			BuildingFloorFilter->SetSelectedOption(TEXT("Any floor count"));
			BuildingFloorFilter->OnGenerateWidgetEvent.BindDynamic(this, &UMapPanelWidget::GenerateBuildingFilterItem);
			BuildingFloorFilter->OnSelectionChanged.AddUniqueDynamic(this, &UMapPanelWidget::HandleBuildingFloorFilterChanged);
			Stack->AddChildToVerticalBox(WrapInPanelWidth(BuildingFloorFilter))->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

			FilteredBuildingComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Cbx_FilteredBuildings"));
			FilteredBuildingComboBox->OnGenerateWidgetEvent.BindDynamic(this, &UMapPanelWidget::GenerateBuildingFilterItem);
			FilteredBuildingComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UMapPanelWidget::HandleFilteredBuildingChanged);
			FilteredBuildingComboBox->SetIsEnabled(false);
			Stack->AddChildToVerticalBox(WrapInPanelWidth(FilteredBuildingComboBox));

			BuildingFilterResultsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_BuildingFilterResults"));
			ApplyStandardFont(BuildingFilterResultsText);
			BuildingFilterResultsText->SetAutoWrapText(true);
			Stack->AddChildToVerticalBox(WrapInPanelWidth(BuildingFilterResultsText));

			// A wrapping label in a button whose width follows the panel, so the text wraps inside the button instead
			// of running out of it.
			ResetBuildingFiltersButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_ResetBuildingFilters"));
			UTextBlock* ResetLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			ResetLabel->SetText(FText::FromString(TEXT("Reset Building Filters")));
			ResetLabel->SetJustification(ETextJustify::Center);
			ResetLabel->SetAutoWrapText(true);
			ApplyStandardFont(ResetLabel);
			ResetBuildingFiltersButton->SetContent(ResetLabel);
			ResetBuildingFiltersButton->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::OnResetBuildingFiltersClicked);
			ResetBuildingFiltersButton->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::PlayButtonClickSound);
			Stack->AddChildToVerticalBox(WrapInPanelWidth(ResetBuildingFiltersButton))->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		}
	}
	return Super::RebuildWidget();
}

void UMapPanelWidget::SetContentWidth(float Width)
{
	if (FMath::IsNearlyEqual(Width, ContentWidth, 0.5f))
	{
		return;
	}
	ContentWidth = Width;
	for (UWidget* Control : { (UWidget*)BuildingSearchBox, (UWidget*)BuildingFloorFilter, (UWidget*)FilteredBuildingComboBox,
		(UWidget*)BuildingFilterResultsText, (UWidget*)ResetBuildingFiltersButton })
	{
		if (Control)
		{
			if (USizeBox* SizeBox = Cast<USizeBox>(Control->GetParent()))
			{
				SizeBox->SetWidthOverride(Width);
			}
		}
	}
}

void UMapPanelWidget::Refresh()
{
	if (!bShowBuildingFilters)
	{
		return;
	}
	if (ANavPlayerController* PC = GetController())
	{
		bUpdatingBuildingFilters = true;
		if (BuildingSearchBox)
		{
			BuildingSearchBox->SetText(FText::FromString(PC->GetBuildingFilterText()));
		}
		if (BuildingCampusCheckbox)
		{
			BuildingCampusCheckbox->SetIsChecked(PC->AreCampusBuildingsVisible());
		}
		if (BuildingFloorFilter)
		{
			const int32 Min = PC->GetBuildingFilterMinimumFloors();
			const int32 Max = PC->GetBuildingFilterMaximumFloors();
			BuildingFloorFilter->SetSelectedOption(Min == 1 && Max == 1 ? TEXT("1 floor")
				: Min == 2 && Max == 5 ? TEXT("2-5 floors") : Min == 6 && Max == 0 ? TEXT("6+ floors") : TEXT("Any floor count"));
		}
		bUpdatingBuildingFilters = false;
		ApplyBuildingFilters();
	}
}

void UMapPanelWidget::ApplyDefaultsToController()
{
	if (ANavPlayerController* PC = GetController())
	{
		// By the time the screen is constructed any roads and POIs to filter have already been loaded.
		PC->SetRoadsAndPOIsWithinCampusOnly(true);
		if (bShowBuildingFilters)
		{
			PC->SetNonCampusBuildingsVisible(false);
		}
	}
}

// ---------------------------------------------------------------------------
// Roads
// ---------------------------------------------------------------------------

void UMapPanelWidget::OnRoadHideAllCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetAllRoadsVisible(!bIsChecked);
	}
	// Show the category check boxes the way the master switch left them (checked = shown). SetIsChecked does not fire
	// OnCheckStateChanged again.
	const bool bCategoriesChecked = !bIsChecked;
	for (UCheckBox* Category : { RoadPrimaryCheckbox, RoadSecondaryCheckbox, RoadResidentialCheckbox, RoadCyclewayCheckbox, RoadFootwayCheckbox, RoadOtherCheckbox })
	{
		if (Category)
		{
			Category->SetIsChecked(bCategoriesChecked);
		}
	}
}

void UMapPanelWidget::OnRoadPrimaryCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetRoadCategoryVisible(ERoadCategory::Primary, bIsChecked);
	}
}

void UMapPanelWidget::OnRoadSecondaryCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetRoadCategoryVisible(ERoadCategory::Secondary, bIsChecked);
	}
}

void UMapPanelWidget::OnRoadResidentialCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetRoadCategoryVisible(ERoadCategory::Residential, bIsChecked);
	}
}

void UMapPanelWidget::OnRoadCyclewayCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetRoadCategoryVisible(ERoadCategory::Cycleway, bIsChecked);
	}
}

void UMapPanelWidget::OnRoadFootwayCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetRoadCategoryVisible(ERoadCategory::Footway, bIsChecked);
	}
}

void UMapPanelWidget::OnRoadOtherCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetRoadCategoryVisible(ERoadCategory::Other, bIsChecked);
	}
}

void UMapPanelWidget::OnRoadCampusOnlyCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetRoadsAndPOIsWithinCampusOnly(bIsChecked);
	}
}

// ---------------------------------------------------------------------------
// Buildings
// ---------------------------------------------------------------------------

void UMapPanelWidget::OnBuildingCampusCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetCampusBuildingsVisible(bIsChecked);
		RefreshBuildingFilterDisplay();
	}
}

void UMapPanelWidget::OnBuildingNonCampusCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetNonCampusBuildingsVisible(bIsChecked);
	}
}

void UMapPanelWidget::HandleBuildingSearchChanged(const FText& Text)
{
	if (!bUpdatingBuildingFilters)
	{
		ApplyBuildingFilters();
	}
}

void UMapPanelWidget::HandleBuildingFloorFilterChanged(FString Item, ESelectInfo::Type Type)
{
	if (!bUpdatingBuildingFilters && Type != ESelectInfo::Direct)
	{
		ApplyBuildingFilters();
	}
}

void UMapPanelWidget::ApplyBuildingFilters()
{
	ANavPlayerController* PC = GetController();
	if (!PC || !BuildingSearchBox || !BuildingFloorFilter)
	{
		return;
	}
	const FString Floor = BuildingFloorFilter->GetSelectedOption();
	const int32 Min = Floor == TEXT("1 floor") ? 1 : Floor == TEXT("2-5 floors") ? 2 : Floor == TEXT("6+ floors") ? 6 : 0;
	const int32 Max = Floor == TEXT("1 floor") ? 1 : Floor == TEXT("2-5 floors") ? 5 : 0;
	PC->SetBuildingFilter(BuildingSearchBox->GetText().ToString(), Min, Max);
	RefreshBuildingFilterDisplay();
}

void UMapPanelWidget::RefreshBuildingFilterDisplayIfChanged()
{
	ANavPlayerController* PC = GetController();
	if (PC && FilteredBuildingComboBox && DisplayedBuildingResultsRevision != PC->GetBuildingResultsRevision())
	{
		RefreshBuildingFilterDisplay();
	}
}

void UMapPanelWidget::RefreshBuildingFilterDisplay()
{
	ANavPlayerController* PC = GetController();
	if (!PC || !FilteredBuildingComboBox)
	{
		return;
	}
	if (DisplayedBuildingResultsRevision != PC->GetBuildingResultsRevision())
	{
		const TMap<FString, FString>& Options = PC->GetFilteredBuildingOptions();
		if (!Options.OrderIndependentCompareEqual(CachedBuildingOptions))
		{
			CachedBuildingOptions = Options;
			FilteredBuildingComboBox->ClearOptions();
			TArray<FString> Labels;
			Options.GetKeys(Labels);
			Labels.Sort();
			for (const FString& Label : Labels)
			{
				FilteredBuildingComboBox->AddOption(Label);
			}
		}
		FilteredBuildingComboBox->SetIsEnabled(!Options.IsEmpty());
		if (BuildingFilterResultsText)
		{
			BuildingFilterResultsText->SetText(FText::FromString(Options.IsEmpty() ? TEXT("No matching editable buildings.")
				: FString::Printf(TEXT("%d / %d editable buildings. Select one above."), Options.Num(), PC->GetEditableBuildingCount())));
		}
		DisplayedBuildingResultsRevision = PC->GetBuildingResultsRevision();
	}
	FString SelectedLabel;
	const FString SelectedId = PC->GetSelectedBuildingId();
	for (const auto& Pair : CachedBuildingOptions)
	{
		if (Pair.Value == SelectedId)
		{
			SelectedLabel = Pair.Key;
			break;
		}
	}
	if (SelectedLabel.IsEmpty())
	{
		FilteredBuildingComboBox->ClearSelection();
	}
	else if (FilteredBuildingComboBox->GetSelectedOption() != SelectedLabel)
	{
		FilteredBuildingComboBox->SetSelectedOption(SelectedLabel);
	}
}

void UMapPanelWidget::HandleFilteredBuildingChanged(FString Item, ESelectInfo::Type Type)
{
	if (Type == ESelectInfo::Direct || Item.IsEmpty())
	{
		return;
	}
	if (ANavPlayerController* PC = GetController())
	{
		if (const FString* Id = CachedBuildingOptions.Find(Item))
		{
			PC->SelectBuildingById(*Id);
		}
	}
}

void UMapPanelWidget::OnResetBuildingFiltersClicked()
{
	bUpdatingBuildingFilters = true;
	if (BuildingSearchBox)
	{
		BuildingSearchBox->SetText(FText::GetEmpty());
	}
	if (BuildingFloorFilter)
	{
		BuildingFloorFilter->SetSelectedOption(TEXT("Any floor count"));
	}
	if (BuildingCampusCheckbox)
	{
		BuildingCampusCheckbox->SetIsChecked(true);
	}
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetCampusBuildingsVisible(true);
	}
	bUpdatingBuildingFilters = false;
	ApplyBuildingFilters();
}

UWidget* UMapPanelWidget::GenerateBuildingFilterItem(FString Item)
{
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	ApplyStandardFont(Label);
	Label->SetText(FText::FromString(Item));
	Label->SetAutoWrapText(true);
	return Label;
}
