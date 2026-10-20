#include "MapPanelWidget.h"

#include "MapBuildingRowWidget.h"
#include "NavPlayerController.h"
#include "RoadSegmentActor.h"
#include "StartScreenStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"

namespace
{
	constexpr float MapRowBottomPadding = 4.f;
}

USizeBox* UMapPanelWidget::WrapInPanelWidth(UWidget* Control)
{
	USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	SizeBox->SetWidthOverride(ContentWidth);
	SizeBox->SetContent(Control);
	return SizeBox;
}

UVerticalBox* UMapPanelWidget::AddCollapsibleSection(UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Title, UButton*& OutHeader, UTextBlock*& OutSign, USizeBox*& OutHeaderBox)
{
	using namespace StartStyle;
	const FString Base(Name);
	OutHeader = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Base + TEXT("Header")));
	OutHeader->SetStyle(Button(Rgb(23, 38, 60), Rgb(31, 51, 80), Rgb(18, 31, 50), Rgb(44, 62, 88), 8.f));
	OutHeader->SetBackgroundColor(FLinearColor::White);
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Pad->SetBrushColor(FLinearColor::Transparent);
	Pad->SetPadding(FMargin(12.f * DesignToUi, 9.f * DesignToUi));
	Pad->SetContent(Row);
	if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(OutHeader->SetContent(Pad)))
		{
			// A button centres and shrinks its content by default; these rows have to fill it so the right-hand part (the
			// shortcut, the chevron, the +/-) lines up in one column.
			ContentSlot->SetHorizontalAlignment(HAlign_Fill);
			ContentSlot->SetVerticalAlignment(VAlign_Fill);
		}
	UHorizontalBoxSlot* TitleSlot = Row->AddChildToHorizontalBox(Text(WidgetTree, Base + TEXT("Title"), Title, 12.f, Rgb(214, 226, 242), true, 20));
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	OutSign = Text(WidgetTree, Base + TEXT("Sign"), TEXT("+"), 14.f, Rgb(150, 172, 202), true);
	Row->AddChildToHorizontalBox(OutSign);
	OutHeaderBox = WrapInPanelWidth(OutHeader);
	Stack->AddChildToVerticalBox(OutHeaderBox)->SetPadding(FMargin(0.f, 10.f * DesignToUi, 0.f, 0.f));

	UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(Base + TEXT("Body")));
	Body->SetVisibility(ESlateVisibility::Collapsed);
	Stack->AddChildToVerticalBox(Body)->SetPadding(FMargin(4.f * DesignToUi, 10.f * DesignToUi, 0.f, 0.f));
	return Body;
}

void UMapPanelWidget::AddRoadRows(UVerticalBox* Stack)
{
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
	// The region limit only ever affects roads and POIs, so it is one more row of the road filters. It starts checked;
	// ApplyDefaultsToController makes the controller agree.
	RoadCampusOnlyCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadCampusOnly"), TEXT("Campus Only"), true, MapRowBottomPadding);
	RoadCampusOnlyCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadCampusOnlyCheckChanged);
}

TSharedRef<SWidget> UMapPanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		using namespace StartStyle;
		// The dock frame paints the panel background; this root only adds the usual panel padding.
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_Map"));
		Root->SetBrushColor(FLinearColor::Transparent);
		Root->SetPadding(FMargin(14.f));
		WidgetTree->RootWidget = Root;

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MapStack"));
		Root->SetContent(Stack);

		if (!bShowBuildingFilters)
		{
			// The interior editor: just the road check boxes.
			AddRoadRows(Stack);
			return Super::RebuildWidget();
		}

		// ---- Site overview ----
		Stack->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_MapKicker"), TEXT("SITE OVERVIEW"), 10.f, Rgb(116, 181, 255), true, 160));
		Stack->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_MapTitle"), TEXT("Choose a building"), 19.f, Rgb(241, 246, 255), true))
			->SetPadding(FMargin(0.f, 8.f * DesignToUi, 0.f, 0.f));
		UTextBlock* Help = Text(WidgetTree, TEXT("Txt_MapHelp"), TEXT("Pick the building to configure."), 12.f, Rgb(170, 190, 214));
		Help->SetAutoWrapText(true);
		Stack->AddChildToVerticalBox(WrapInPanelWidth(Help))->SetPadding(FMargin(0.f, 5.f * DesignToUi, 0.f, 0.f));

		BuildingSearchBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_BuildingSearch"));
		BuildingSearchBox->SetWidgetStyle(Field(12.f));
		BuildingSearchBox->SetHintText(FText::FromString(TEXT("Find a building...")));
		BuildingSearchBox->OnTextChanged.AddUniqueDynamic(this, &UMapPanelWidget::HandleBuildingSearchChanged);
		Stack->AddChildToVerticalBox(WrapInPanelWidth(BuildingSearchBox))->SetPadding(FMargin(0.f, 16.f * DesignToUi, 0.f, 0.f));

		BuildingFilterResultsText = Text(WidgetTree, TEXT("Txt_BuildingFilterResults"), TEXT(""), 11.f, Rgb(136, 156, 182));
		BuildingFilterResultsText->SetAutoWrapText(true);
		Stack->AddChildToVerticalBox(WrapInPanelWidth(BuildingFilterResultsText))->SetPadding(FMargin(0.f, 8.f * DesignToUi, 0.f, 8.f * DesignToUi));

		// The buildings, as rows; the list scrolls inside the panel once it is longer than this.
		BuildingList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("BuildingList"));
		USizeBox* ListSize = WrapInPanelWidth(BuildingList);
		ListSize->SetMaxDesiredHeight(330.f * DesignToUi);
		Stack->AddChildToVerticalBox(ListSize);

		// ---- Filters (collapsed) ----
		USizeBox* FiltersHeaderBox = nullptr;
		FiltersBody = AddCollapsibleSection(Stack, TEXT("Filters"), TEXT("FILTERS"), FiltersHeader, FiltersSign, FiltersHeaderBox);
		FiltersHeader->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::OnFiltersHeaderClicked);
		FiltersHeader->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::PlayButtonClickSound);

		FiltersBody->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_FloorsLabel"), TEXT("Floors"), 11.f, Rgb(165, 182, 204), true, 30));
		UWrapBox* Chips = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("FloorChips"));
		Chips->SetInnerSlotPadding(FVector2D(6.f * DesignToUi, 6.f * DesignToUi));
		struct FChipSpec { const TCHAR* Name; const TCHAR* Label; };
		const FChipSpec ChipSpecs[] = { { TEXT("Btn_FloorsAny"), TEXT("Any") }, { TEXT("Btn_FloorsOne"), TEXT("1 floor") }, { TEXT("Btn_FloorsMid"), TEXT("2-5") }, { TEXT("Btn_FloorsTall"), TEXT("6+") } };
		for (const FChipSpec& Spec : ChipSpecs)
		{
			UButton* Chip = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Spec.Name));
			Chip->SetBackgroundColor(FLinearColor::White);
			UBorder* ChipPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			ChipPad->SetBrushColor(FLinearColor::Transparent);
			ChipPad->SetPadding(FMargin(11.f * DesignToUi, 6.f * DesignToUi));
			ChipPad->SetContent(Text(WidgetTree, FString(Spec.Name) + TEXT("Label"), Spec.Label, 12.f, FLinearColor::White, true));
			Chip->SetContent(ChipPad);
			Chips->AddChildToWrapBox(Chip);
			FloorChips.Add(Chip);
		}
		FloorChips[0]->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::OnFloorAnyClicked);
		FloorChips[1]->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::OnFloorOneClicked);
		FloorChips[2]->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::OnFloorMidClicked);
		FloorChips[3]->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::OnFloorTallClicked);
		for (UButton* Chip : FloorChips)
		{
			Chip->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::PlayButtonClickSound);
		}
		FiltersBody->AddChildToVerticalBox(WrapInPanelWidth(Chips))->SetPadding(FMargin(0.f, 6.f * DesignToUi, 0.f, 12.f * DesignToUi));
		RefreshFloorChips();

		FiltersBody->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_ShowLabel"), TEXT("Show"), 11.f, Rgb(165, 182, 204), true, 30))
			->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f * DesignToUi));
		BuildingCampusCheckbox = AddCheckRow(FiltersBody, TEXT("Chk_BuildingCampus"), TEXT("Campus"), true, MapRowBottomPadding);
		BuildingCampusCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnBuildingCampusCheckChanged);
		// Starts unchecked: buildings outside the campus are hidden by default.
		BuildingNonCampusCheckbox = AddCheckRow(FiltersBody, TEXT("Chk_BuildingNonCampus"), TEXT("Non-Campus"), false, MapRowBottomPadding);
		BuildingNonCampusCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnBuildingNonCampusCheckChanged);

		// A wrapping label in a button whose width follows the panel, so the text wraps inside the button instead of
		// running out of it.
		ResetBuildingFiltersButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_ResetBuildingFilters"));
		ResetBuildingFiltersButton->SetStyle(SecondaryButton());
		ResetBuildingFiltersButton->SetBackgroundColor(FLinearColor::White);
		UTextBlock* ResetLabel = Text(WidgetTree, TEXT("Txt_ResetBuildingFilters"), TEXT("Reset Building Filters"), 12.f, FLinearColor::White, true);
		ResetLabel->SetJustification(ETextJustify::Center);
		ResetLabel->SetAutoWrapText(true);
		UBorder* ResetPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		ResetPad->SetBrushColor(FLinearColor::Transparent);
		ResetPad->SetPadding(FMargin(8.f * DesignToUi, 9.f * DesignToUi));
		ResetPad->SetContent(ResetLabel);
		ResetBuildingFiltersButton->SetContent(ResetPad);
		ResetBuildingFiltersButton->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::OnResetBuildingFiltersClicked);
		ResetBuildingFiltersButton->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::PlayButtonClickSound);
		FiltersBody->AddChildToVerticalBox(WrapInPanelWidth(ResetBuildingFiltersButton))->SetPadding(FMargin(0.f, 6.f * DesignToUi, 0.f, 0.f));

		// ---- Roads (collapsed) ----
		USizeBox* RoadsHeaderBox = nullptr;
		RoadsBody = AddCollapsibleSection(Stack, TEXT("Roads"), TEXT("ROADS"), RoadsHeader, RoadsSign, RoadsHeaderBox);
		RoadsHeader->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadsHeaderClicked);
		RoadsHeader->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::PlayButtonClickSound);
		AddRoadRows(RoadsBody);

		// ---- Note ----
		UBorder* Note = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MapNote"));
		Note->SetBrush(Rounded(Rgb(24, 38, 60), Rgb(40, 57, 82), 8.f));
		Note->SetBrushColor(FLinearColor::White);
		Note->SetPadding(FMargin(12.f * DesignToUi, 11.f * DesignToUi));
		UTextBlock* NoteText = Text(WidgetTree, TEXT("Txt_MapNote"),
			TEXT("Buildings outside the campus are context only and cannot be edited. Turn them on under Filters > Non-Campus."), 11.f, Rgb(136, 156, 182));
		NoteText->SetAutoWrapText(true);
		Note->SetContent(NoteText);
		Stack->AddChildToVerticalBox(WrapInPanelWidth(Note))->SetPadding(FMargin(0.f, 14.f * DesignToUi, 0.f, 0.f));
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
	// Every SizeBox the panel put its controls in follows the panel's width.
	TArray<UWidget*> Controls = { BuildingSearchBox, BuildingFilterResultsText, BuildingList, ResetBuildingFiltersButton, FiltersHeader, RoadsHeader };
	if (WidgetTree)
	{
		for (const FName Name : { FName(TEXT("Txt_MapHelp")), FName(TEXT("FloorChips")), FName(TEXT("MapNote")) })
		{
			if (UWidget* Found = FindContentWidget(Name))
			{
				Controls.Add(Found);
			}
		}
	}
	for (UWidget* Control : Controls)
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
		FloorMinimum = PC->GetBuildingFilterMinimumFloors();
		FloorMaximum = PC->GetBuildingFilterMaximumFloors();
		RefreshFloorChips();
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
// Sections
// ---------------------------------------------------------------------------

void UMapPanelWidget::SetSectionOpen(bool bFilters, bool bOpen)
{
	bool& bFlag = bFilters ? bFiltersOpen : bRoadsOpen;
	bFlag = bOpen;
	if (UVerticalBox* Body = bFilters ? FiltersBody : RoadsBody)
	{
		Body->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (UTextBlock* Sign = bFilters ? FiltersSign : RoadsSign)
	{
		Sign->SetText(FText::FromString(bOpen ? TEXT("-") : TEXT("+")));
	}
}

void UMapPanelWidget::SetSectionOpenForTests(bool bFilters, bool bOpen)
{
	SetSectionOpen(bFilters, bOpen);
}

void UMapPanelWidget::OnFiltersHeaderClicked()
{
	SetSectionOpen(true, !bFiltersOpen);
}

void UMapPanelWidget::OnRoadsHeaderClicked()
{
	SetSectionOpen(false, !bRoadsOpen);
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

void UMapPanelWidget::SetBuildingSearchForTests(const FString& Text)
{
	if (BuildingSearchBox)
	{
		BuildingSearchBox->SetText(FText::FromString(Text));
	}
	ApplyBuildingFilters();
}

void UMapPanelWidget::SelectFloorOption(int32 Minimum, int32 Maximum)
{
	FloorMinimum = Minimum;
	FloorMaximum = Maximum;
	RefreshFloorChips();
	if (!bUpdatingBuildingFilters)
	{
		ApplyBuildingFilters();
	}
}

void UMapPanelWidget::OnFloorAnyClicked() { SelectFloorOption(0, 0); }
void UMapPanelWidget::OnFloorOneClicked() { SelectFloorOption(1, 1); }
void UMapPanelWidget::OnFloorMidClicked() { SelectFloorOption(2, 5); }
void UMapPanelWidget::OnFloorTallClicked() { SelectFloorOption(6, 0); }

void UMapPanelWidget::RefreshFloorChips()
{
	using namespace StartStyle;
	if (FloorChips.Num() != 4)
	{
		return;
	}
	const int32 Selected = (FloorMinimum == 1 && FloorMaximum == 1) ? 1 : (FloorMinimum == 2 && FloorMaximum == 5) ? 2 : (FloorMinimum == 6 && FloorMaximum == 0) ? 3 : 0;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FloorChips[Index]->SetStyle(Index == Selected ? PrimaryButton() : SecondaryButton());
	}
}

void UMapPanelWidget::ApplyBuildingFilters()
{
	ANavPlayerController* PC = GetController();
	if (!PC || !BuildingSearchBox)
	{
		return;
	}
	PC->SetBuildingFilter(BuildingSearchBox->GetText().ToString(), FloorMinimum, FloorMaximum);
	RefreshBuildingFilterDisplay();
}

void UMapPanelWidget::RefreshBuildingFilterDisplayIfChanged()
{
	ANavPlayerController* PC = GetController();
	if (PC && BuildingList && (DisplayedBuildingResultsRevision != PC->GetBuildingResultsRevision() || DisplayedSelectedBuildingId != PC->GetSelectedBuildingId()))
	{
		RefreshBuildingFilterDisplay();
	}
}

FText UMapPanelWidget::GetBuildingCountTextForTests() const
{
	return BuildingFilterResultsText ? BuildingFilterResultsText->GetText() : FText::GetEmpty();
}

void UMapPanelWidget::RefreshBuildingFilterDisplay()
{
	ANavPlayerController* PC = GetController();
	if (!PC || !BuildingList)
	{
		return;
	}
	const FString Selected = PC->GetSelectedBuildingId();
	BuildingList->ClearChildren();
	BuildingRows.Reset();
	for (const ANavPlayerController::FBuildingListItem& Item : PC->GetFilteredBuildingList())
	{
		UMapBuildingRowWidget* Row = CreateWidget<UMapBuildingRowWidget>(this, UMapBuildingRowWidget::StaticClass());
		if (!Row)
		{
			continue;
		}
		Row->SetItem(Item.Id, Item.Name, Item.Floors, Item.Id == Selected);
		Row->OnClicked = [this](const FString& BuildingId) { HandleBuildingRowClicked(BuildingId); };
		BuildingRows.Add(Row);
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(BuildingList->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f * StartStyle::DesignToUi));
		}
	}
	if (BuildingFilterResultsText)
	{
		BuildingFilterResultsText->SetText(FText::FromString(BuildingRows.IsEmpty() ? FString(TEXT("No matching editable buildings."))
			: FString::Printf(TEXT("%d / %d editable buildings"), BuildingRows.Num(), PC->GetEditableBuildingCount())));
	}
	DisplayedBuildingResultsRevision = PC->GetBuildingResultsRevision();
	DisplayedSelectedBuildingId = Selected;
}

void UMapPanelWidget::HandleBuildingRowClicked(const FString& BuildingId)
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SelectBuildingById(BuildingId);
		RefreshBuildingFilterDisplay();
	}
}

void UMapPanelWidget::OnResetBuildingFiltersClicked()
{
	bUpdatingBuildingFilters = true;
	if (BuildingSearchBox)
	{
		BuildingSearchBox->SetText(FText::GetEmpty());
	}
	FloorMinimum = 0;
	FloorMaximum = 0;
	RefreshFloorChips();
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
