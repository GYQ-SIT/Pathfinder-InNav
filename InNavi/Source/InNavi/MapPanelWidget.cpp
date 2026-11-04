#include "MapPanelWidget.h"

#include "BuildingTypes.h"
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
#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"

namespace
{
	constexpr float MapRowBottomPadding = 4.f;
	// A section's header and the controls under it sit in one frame (see AddCollapsibleSection), with this much room between the outline and them.
	constexpr float SectionFramePadding = 6.f * StartStyle::DesignToUi;
	// What the controls inside a frame lose of the panel's width.
	constexpr float SectionInset = 2.f * SectionFramePadding;

	// The frame of an open section is outlined; that of a closed one is not drawn and takes no room.
	void ApplySectionFrame(UBorder* Frame, bool bOpen)
	{
		if (!Frame)
		{
			return;
		}
		using namespace StartStyle;
		Frame->SetBrush(bOpen ? Rounded(FLinearColor::Transparent, Rgb(96, 116, 146), 10.f, 1.5f) : Rounded(FLinearColor::Transparent, FLinearColor::Transparent, 10.f, 0.f));
		Frame->SetPadding(bOpen ? FMargin(SectionFramePadding) : FMargin(0.f));
	}
}

USizeBox* UMapPanelWidget::WrapInPanelWidth(UWidget* Control, float Inset)
{
	USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	SizeBox->SetWidthOverride(ContentWidth - Inset);
	SizeBox->SetContent(Control);
	return SizeBox;
}

UVerticalBox* UMapPanelWidget::AddCollapsibleSection(UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Title, UButton*& OutHeader, UTextBlock*& OutSign, USizeBox*& OutHeaderBox, UBorder*& OutFrame)
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
	// The header and the body in one frame, outlined while the section is open (ApplySectionFrame).
	OutFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Base + TEXT("Frame")));
	OutFrame->SetBrushColor(FLinearColor::White);
	ApplySectionFrame(OutFrame, false);
	UVerticalBox* Inner = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(Base + TEXT("Inner")));
	OutFrame->SetContent(Inner);
	Inner->AddChildToVerticalBox(OutHeader);
	OutHeaderBox = WrapInPanelWidth(OutFrame);
	Stack->AddChildToVerticalBox(OutHeaderBox)->SetPadding(FMargin(0.f, 10.f * DesignToUi, 0.f, 0.f));

	UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(Base + TEXT("Body")));
	Body->SetVisibility(ESlateVisibility::Collapsed);
	Inner->AddChildToVerticalBox(Body)->SetPadding(FMargin(0.f, 10.f * DesignToUi, 0.f, 2.f * DesignToUi));
	return Body;
}

void UMapPanelWidget::AddRoadRows(UVerticalBox* Stack)
{
	// With the roads hidden by default, Hide All starts checked and the categories unchecked (as Hide All leaves them).
	RoadHideAllCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadHideAll"), TEXT("Hide All"), !bRoadsVisibleByDefault, MapRowBottomPadding);
	RoadHideAllCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadHideAllCheckChanged);
	RoadPrimaryCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadPrimary"), TEXT("Primary"), bRoadsVisibleByDefault, MapRowBottomPadding);
	RoadPrimaryCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadPrimaryCheckChanged);
	RoadSecondaryCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadSecondary"), TEXT("Secondary"), bRoadsVisibleByDefault, MapRowBottomPadding);
	RoadSecondaryCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadSecondaryCheckChanged);
	RoadResidentialCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadResidential"), TEXT("Residential/Tertiary"), bRoadsVisibleByDefault, MapRowBottomPadding);
	RoadResidentialCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadResidentialCheckChanged);
	RoadCyclewayCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadCycleway"), TEXT("Cycleway"), bRoadsVisibleByDefault, MapRowBottomPadding);
	RoadCyclewayCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadCyclewayCheckChanged);
	RoadFootwayCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadFootway"), TEXT("Footway/Path"), bRoadsVisibleByDefault, MapRowBottomPadding);
	RoadFootwayCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadFootwayCheckChanged);
	RoadOtherCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadOther"), TEXT("Other"), bRoadsVisibleByDefault, MapRowBottomPadding);
	RoadOtherCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadOtherCheckChanged);
	// The region limit only ever affects roads and POIs, so it is one more row of the road filters. It starts checked;
	// ApplyDefaultsToController makes the controller agree.
	RoadCampusOnlyCheckbox = AddCheckRow(Stack, TEXT("Chk_RoadCampusOnly"), TEXT("Campus Only"), true, MapRowBottomPadding);
	RoadCampusOnlyCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadCampusOnlyCheckChanged);
}

void UMapPanelWidget::AddBuildingTypeRows(UVerticalBox* Stack)
{
	using namespace StartStyle;
	Stack->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_TypeLabel"), TEXT("Type"), 11.f, Rgb(165, 182, 204), true, 30))
		->SetPadding(FMargin(0.f, 8.f * DesignToUi, 0.f, 6.f * DesignToUi));
	BuildingTypeCheckboxes.Reset();
	for (const FString& Type : BuildingTypes::All())
	{
		const FString Name = FString::Printf(TEXT("Chk_BuildingType_%s"), *Type);
		UCheckBox* Checkbox = AddCheckRow(Stack, *Name, *Type, true, MapRowBottomPadding, BuildingTypes::Color(Type));
		Checkbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnBuildingTypeCheckChanged);
		BuildingTypeCheckboxes.Add(Checkbox);
	}
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
		FiltersBody = AddCollapsibleSection(Stack, TEXT("Filters"), TEXT("FILTERS"), FiltersHeader, FiltersSign, FiltersHeaderBox, FiltersFrame);
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
		FiltersBody->AddChildToVerticalBox(WrapInPanelWidth(Chips, SectionInset))->SetPadding(FMargin(0.f, 6.f * DesignToUi, 0.f, 12.f * DesignToUi));
		RefreshFloorChips();

		FiltersBody->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_ShowLabel"), TEXT("Show"), 11.f, Rgb(165, 182, 204), true, 30))
			->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f * DesignToUi));
		BuildingCampusCheckbox = AddCheckRow(FiltersBody, TEXT("Chk_BuildingCampus"), TEXT("Campus"), true, MapRowBottomPadding);
		BuildingCampusCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnBuildingCampusCheckChanged);
		// Starts unchecked: buildings outside the campus are hidden by default.
		BuildingNonCampusCheckbox = AddCheckRow(FiltersBody, TEXT("Chk_BuildingNonCampus"), TEXT("Non-Campus"), false, MapRowBottomPadding);
		BuildingNonCampusCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnBuildingNonCampusCheckChanged);
		AddBuildingTypeRows(FiltersBody);

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
		FiltersBody->AddChildToVerticalBox(WrapInPanelWidth(ResetBuildingFiltersButton, SectionInset))->SetPadding(FMargin(0.f, 6.f * DesignToUi, 0.f, 0.f));

		// ---- Roads (collapsed) ----
		USizeBox* RoadsHeaderBox = nullptr;
		RoadsBody = AddCollapsibleSection(Stack, TEXT("Roads"), TEXT("ROADS"), RoadsHeader, RoadsSign, RoadsHeaderBox, RoadsFrame);
		RoadsHeader->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::OnRoadsHeaderClicked);
		RoadsHeader->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::PlayButtonClickSound);
		AddRoadRows(RoadsBody);

		// ---- See-through (collapsed) ----
		USizeBox* ViewHeaderBox = nullptr;
		ViewBody = AddCollapsibleSection(Stack, TEXT("View"), TEXT("SEE-THROUGH"), ViewHeader, ViewSign, ViewHeaderBox, ViewFrame);
		ViewHeader->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::OnViewHeaderClicked);
		ViewHeader->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::PlayButtonClickSound);
		AllSeeThroughButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_SeeThroughAll"));
		AllSeeThroughButton->SetBackgroundColor(FLinearColor::White);
		AllSeeThroughButton->SetStyle(OrangeButton(8.f));
		AllSeeThroughLabel = Text(WidgetTree, TEXT("Txt_SeeThroughAll"), TEXT("See-through ON"), 12.f, FLinearColor::White, true);
		AllSeeThroughLabel->SetJustification(ETextJustify::Center);
		UBorder* AllSeeThroughPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Btn_SeeThroughAllPad"));
		AllSeeThroughPad->SetBrushColor(FLinearColor::Transparent);
		AllSeeThroughPad->SetPadding(FMargin(10.f * DesignToUi, 10.f * DesignToUi));
		AllSeeThroughPad->SetContent(AllSeeThroughLabel);
		AllSeeThroughButton->SetContent(AllSeeThroughPad);
		AllSeeThroughButton->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::OnAllSeeThroughClicked);
		AllSeeThroughButton->OnClicked.AddUniqueDynamic(this, &UMapPanelWidget::PlayButtonClickSound);
		ViewBody->AddChildToVerticalBox(WrapInPanelWidth(AllSeeThroughButton, SectionInset))->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f * DesignToUi));
		UTextBlock* ViewHelp = Text(WidgetTree, TEXT("Txt_RevealHelp"),
			TEXT("Far away a building is solid, with its roof. Coming closer, its walls and roof turn clear and its rooms show."), 11.f, Rgb(136, 156, 182));
		ViewHelp->SetAutoWrapText(true);
		ViewBody->AddChildToVerticalBox(WrapInPanelWidth(ViewHelp, SectionInset))->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f * DesignToUi));
		struct FRevealSpec { const TCHAR* TextName; const TCHAR* SliderName; float Minimum; float Maximum; float Initial; };
		const FRevealSpec RevealSpecs[] = {
			{ TEXT("Txt_RevealFar"), TEXT("Slider_RevealFar"), 10.f, 150.f, 80.f },
			{ TEXT("Txt_RevealNear"), TEXT("Slider_RevealNear"), 2.f, 80.f, 70.f } };
		for (const FRevealSpec& Spec : RevealSpecs)
		{
			const bool bFar = (&Spec == &RevealSpecs[0]);
			UTextBlock* Label = Text(WidgetTree, Spec.TextName, TEXT(""), 11.f, Rgb(165, 182, 204), true, 30);
			USlider* Slider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), FName(Spec.SliderName));
			Slider->SetMinValue(Spec.Minimum);
			Slider->SetMaxValue(Spec.Maximum);
			Slider->SetStepSize(1.f);
			Slider->SetValue(Spec.Initial);
			DarkSlider(Slider);
			if (bFar)
			{
				Slider->OnValueChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRevealFarChanged);
				RevealFarText = Label;
				RevealFarSlider = Slider;
			}
			else
			{
				Slider->OnValueChanged.AddUniqueDynamic(this, &UMapPanelWidget::OnRevealNearChanged);
				RevealNearText = Label;
				RevealNearSlider = Slider;
			}
			ViewBody->AddChildToVerticalBox(Label);
			ViewBody->AddChildToVerticalBox(WrapInPanelWidth(Slider, SectionInset))->SetPadding(FMargin(0.f, 4.f * DesignToUi, 0.f, 12.f * DesignToUi));
		}
		RefreshRevealControls();

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
	TArray<UWidget*> Controls = { BuildingSearchBox, BuildingFilterResultsText, BuildingList, FiltersFrame, RoadsFrame, ViewFrame };
	// The controls inside the frames are narrower than the panel by what the frames take.
	TArray<UWidget*> InnerControls = { ResetBuildingFiltersButton, AllSeeThroughButton, RevealFarSlider, RevealNearSlider };
	if (WidgetTree)
	{
		for (const FName Name : { FName(TEXT("Txt_MapHelp")), FName(TEXT("MapNote")) })
		{
			if (UWidget* Found = FindContentWidget(Name))
			{
				Controls.Add(Found);
			}
		}
		for (const FName Name : { FName(TEXT("FloorChips")), FName(TEXT("Txt_RevealHelp")) })
		{
			if (UWidget* Found = FindContentWidget(Name))
			{
				InnerControls.Add(Found);
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
	for (UWidget* Control : InnerControls)
	{
		if (Control)
		{
			if (USizeBox* SizeBox = Cast<USizeBox>(Control->GetParent()))
			{
				SizeBox->SetWidthOverride(Width - SectionInset);
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
		RefreshBuildingTypeChecks();
		FloorMinimum = PC->GetBuildingFilterMinimumFloors();
		FloorMaximum = PC->GetBuildingFilterMaximumFloors();
		RefreshFloorChips();
		bUpdatingBuildingFilters = false;
		RefreshRevealControls();
		ApplyBuildingFilters();
	}
}

void UMapPanelWidget::ApplyDefaultsToController()
{
	if (ANavPlayerController* PC = GetController())
	{
		// By the time the screen is constructed any roads and POIs to filter have already been loaded.
		PC->SetAllRoadsVisible(bRoadsVisibleByDefault);
		PC->SetRoadsAndPOIsWithinCampusOnly(true);
		if (bShowBuildingFilters && bApplyBuildingDefaults)
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
	ApplySectionFrame(bFilters ? FiltersFrame : RoadsFrame, bOpen);
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

void UMapPanelWidget::OnViewHeaderClicked()
{
	bViewOpen = !bViewOpen;
	ApplySectionFrame(ViewFrame, bViewOpen);
	if (ViewBody)
	{
		ViewBody->SetVisibility(bViewOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (ViewSign)
	{
		ViewSign->SetText(FText::FromString(bViewOpen ? TEXT("-") : TEXT("+")));
	}
}

void UMapPanelWidget::RefreshRevealControls()
{
	ANavPlayerController* PC = GetController();
	if (!PC)
	{
		return;
	}
	const bool bWasUpdating = bUpdatingBuildingFilters;
	bUpdatingBuildingFilters = true;
	if (AllSeeThroughButton)
	{
		const bool bAllOn = PC->IsCampusSeeThroughEnabled();
		AllSeeThroughButton->SetStyle(bAllOn ? StartStyle::OrangeButton(8.f) : StartStyle::SecondaryButton());
		if (AllSeeThroughLabel)
		{
			AllSeeThroughLabel->SetText(FText::FromString(bAllOn ? TEXT("See-through ON") : TEXT("See-through OFF")));
		}
	}
	const float FarMetres = PC->GetCampusRevealFarDistance() / 100.f;
	const float NearMetres = PC->GetCampusRevealNearDistance() / 100.f;
	if (RevealFarSlider)
	{
		RevealFarSlider->SetValue(FarMetres);
	}
	if (RevealNearSlider)
	{
		RevealNearSlider->SetValue(NearMetres);
	}
	if (RevealFarText)
	{
		RevealFarText->SetText(FText::FromString(FString::Printf(TEXT("Starts to clear at %.0f m"), FarMetres)));
	}
	if (RevealNearText)
	{
		RevealNearText->SetText(FText::FromString(FString::Printf(TEXT("Fully clear within %.0f m"), NearMetres)));
	}
	bUpdatingBuildingFilters = bWasUpdating;
}

FString UMapPanelWidget::GetAllSeeThroughTextForTests() const { return AllSeeThroughLabel ? AllSeeThroughLabel->GetText().ToString() : FString(); }
float UMapPanelWidget::GetRevealFarSliderForTests() const { return RevealFarSlider ? RevealFarSlider->GetValue() : -1.f; }
float UMapPanelWidget::GetRevealNearSliderForTests() const { return RevealNearSlider ? RevealNearSlider->GetValue() : -1.f; }

void UMapPanelWidget::OnAllSeeThroughClicked()
{
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetCampusSeeThroughEnabled(!PC->IsCampusSeeThroughEnabled());
	}
	RefreshRevealControls();
}

void UMapPanelWidget::OnRevealFarChanged(float NewValue)
{
	if (bUpdatingBuildingFilters)
	{
		return;
	}
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetCampusRevealFarDistance(NewValue * 100.f);
	}
	RefreshRevealControls();
}

void UMapPanelWidget::OnRevealNearChanged(float NewValue)
{
	if (bUpdatingBuildingFilters)
	{
		return;
	}
	if (ANavPlayerController* PC = GetController())
	{
		PC->SetCampusRevealNearDistance(NewValue * 100.f);
	}
	RefreshRevealControls();
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

void UMapPanelWidget::OnBuildingTypeCheckChanged(bool)
{
	ANavPlayerController* PC = GetController();
	if (!PC || bUpdatingBuildingFilters)
	{
		return;
	}
	const TArray<FString>& Types = BuildingTypes::All();
	for (int32 Index = 0; Index < BuildingTypeCheckboxes.Num() && Index < Types.Num(); ++Index)
	{
		PC->SetBuildingTypeVisible(Types[Index], BuildingTypeCheckboxes[Index]->IsChecked());
	}
	RefreshBuildingFilterDisplay();
}

void UMapPanelWidget::RefreshBuildingTypeChecks()
{
	const ANavPlayerController* PC = GetController();
	const TArray<FString>& Types = BuildingTypes::All();
	for (int32 Index = 0; Index < BuildingTypeCheckboxes.Num() && Index < Types.Num(); ++Index)
	{
		BuildingTypeCheckboxes[Index]->SetIsChecked(!PC || PC->IsBuildingTypeVisible(Types[Index]));
	}
}

bool UMapPanelWidget::IsBuildingTypeCheckedForTests(const FString& Type) const
{
	const int32 Index = BuildingTypes::All().IndexOfByKey(Type);
	return BuildingTypeCheckboxes.IsValidIndex(Index) && BuildingTypeCheckboxes[Index]->IsChecked();
}

void UMapPanelWidget::SetBuildingTypeCheckedForTests(const FString& Type, bool bChecked)
{
	const int32 Index = BuildingTypes::All().IndexOfByKey(Type);
	if (BuildingTypeCheckboxes.IsValidIndex(Index))
	{
		BuildingTypeCheckboxes[Index]->SetIsChecked(bChecked);
		OnBuildingTypeCheckChanged(bChecked);
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
	if (PC && BuildingList && (DisplayedBuildingResultsRevision != PC->GetBuildingResultsRevision() || DisplayedSelectedBuildingId != SelectedBuildingId()))
	{
		RefreshBuildingFilterDisplay();
	}
}

bool UMapPanelWidget::AreRoadsCheckedForTests() const
{
	return RoadPrimaryCheckbox && RoadPrimaryCheckbox->IsChecked();
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
	const FString Selected = SelectedBuildingId();
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

FString UMapPanelWidget::SelectedBuildingId() const
{
	if (SelectedBuildingProvider)
	{
		return SelectedBuildingProvider();
	}
	const ANavPlayerController* PC = GetController();
	return PC ? PC->GetSelectedBuildingId() : FString();
}

void UMapPanelWidget::HandleBuildingRowClicked(const FString& BuildingId)
{
	if (BuildingClickHandler)
	{
		BuildingClickHandler(BuildingId);
		RefreshBuildingFilterDisplay();
		return;
	}
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
		PC->ResetBuildingTypeFilter();
	}
	RefreshBuildingTypeChecks();
	bUpdatingBuildingFilters = false;
	ApplyBuildingFilters();
}
