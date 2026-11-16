#include "MobileSearchPageWidget.h"

#include "MobileIcons.h"
#include "MobileSelectWidget.h"
#include "MobileStyle.h"
#include "MobileTapTarget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace MobileStyle;

TSharedRef<SWidget> UMobileSearchPageWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		UVerticalBox* Page = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SearchPage"));
		Tree->RootWidget = Page;

		// The title (the site, or the building when one is chosen) and, at the right, the filters button with the number of filters that are on.
		UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SearchHeader"));
		TitleText = MakeText(Tree, TEXT("Txt_SearchTitle"), TEXT(""), 16.f, Ink, true);
		UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(TitleText);
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Center);
		FiltersButton = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_Filters"));
		FiltersButton->SetBackgroundColor(FLinearColor::White);
		UOverlay* FilterOverlay = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("FiltersOverlay"));
		FiltersIcon = MobileIcons::Image(Tree, TEXT("Filter"), 18.f);
		USizeBox* FilterIconSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FiltersIconSize"));
		FilterIconSize->SetWidthOverride(18.f);
		FilterIconSize->SetHeightOverride(18.f);
		if (FiltersIcon)
		{
			FilterIconSize->SetContent(FiltersIcon);
		}
		UWidget* FilterFace = FiltersIcon ? static_cast<UWidget*>(FilterIconSize) : static_cast<UWidget*>(FiltersLabel = MakeText(Tree, TEXT("Btn_Filters_Label"), TEXT("Filters"), 11.f, Ink, true));
		UOverlaySlot* FaceSlot = FilterOverlay->AddChildToOverlay(FilterFace);
		FaceSlot->SetHorizontalAlignment(HAlign_Center);
		FaceSlot->SetVerticalAlignment(VAlign_Center);
		FiltersBadge = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FiltersBadge"));
		FiltersBadge->SetBrush(Rounded(Blue, FLinearColor::Transparent, 8.f, 0.f));
		FiltersBadge->SetBrushColor(FLinearColor::White);
		FiltersBadge->SetPadding(FMargin(5.f, 1.f));
		FiltersBadge->SetVisibility(ESlateVisibility::Collapsed);
		FiltersBadgeText = MakeText(Tree, TEXT("Txt_FiltersBadge"), TEXT("1"), 10.f, OnBlue, true);
		FiltersBadge->SetContent(FiltersBadgeText);
		UOverlaySlot* BadgeSlot = FilterOverlay->AddChildToOverlay(FiltersBadge);
		BadgeSlot->SetHorizontalAlignment(HAlign_Right);
		BadgeSlot->SetVerticalAlignment(VAlign_Top);
		BadgeSlot->SetPadding(FMargin(0.f, 2.f, 2.f, 0.f));
		USizeBox* FilterSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FiltersSize"));
		FilterSize->SetWidthOverride(36.f);
		FilterSize->SetHeightOverride(36.f);
		FilterSize->SetContent(FilterOverlay);
		FiltersButton->SetContent(FilterSize);
		FiltersButton->OnClicked.AddUniqueDynamic(this, &UMobileSearchPageWidget::HandleFiltersClicked);
		Header->AddChildToHorizontalBox(FiltersButton)->SetVerticalAlignment(VAlign_Center);
		Page->AddChildToVerticalBox(Header)->SetPadding(FMargin(16.f, 0.f, 16.f, 0.f));

		// Where the walk starts, when the person chose a place for it: a line under the title, and Clear to go back to My location.
		StartRow = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StartRow"));
		StartRow->SetBrush(Rounded(BlueSoft, FLinearColor::Transparent, 10.f, 0.f));
		StartRow->SetBrushColor(FLinearColor::White);
		StartRow->SetPadding(FMargin(12.f, 8.f));
		StartRow->SetVisibility(ESlateVisibility::Collapsed);
		UHorizontalBox* StartLine = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("StartLine"));
		StartText = MakeText(Tree, TEXT("Txt_StartingFrom"), TEXT(""), 12.f, Blue, true);
		StartText->SetAutoWrapText(true);
		UHorizontalBoxSlot* StartTextSlot = StartLine->AddChildToHorizontalBox(StartText);
		StartTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		StartTextSlot->SetVerticalAlignment(VAlign_Center);
		UButton* ClearStartButton = MakeButton(Tree, TEXT("Btn_ClearStart"), TEXT("Clear"), White, Blue, 8.f, 28.f, nullptr, 12.f);
		ClearStartButton->OnClicked.AddUniqueDynamic(this, &UMobileSearchPageWidget::HandleClearStartClicked);
		StartLine->AddChildToHorizontalBox(ClearStartButton)->SetVerticalAlignment(VAlign_Center);
		StartRow->SetContent(StartLine);
		Page->AddChildToVerticalBox(StartRow)->SetPadding(FMargin(16.f, 8.f, 16.f, 0.f));

		// The filters, three drop-downs in a row, shown when the button is open.
		FilterPanel = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FilterPanel"));
		FilterPanel->SetVisibility(ESlateVisibility::Collapsed);
		TWeakObjectPtr<UMobileSearchPageWidget> WeakThis(this);
		auto MakeSelect = [this, WeakThis](const TCHAR* Caption, TFunction<void(UMobileSearchPageWidget*, const FString&)> OnChosen) -> UMobileSelectWidget*
		{
			UMobileSelectWidget* Select = CreateWidget<UMobileSelectWidget>(this, UMobileSelectWidget::StaticClass());
			Select->SetCompact(true);
			Select->SetCaption(Caption);
			Select->SetOnChanged([WeakThis, OnChosen](const FString& Value)
			{
				if (UMobileSearchPageWidget* Self = WeakThis.Get())
				{
					OnChosen(Self, Value);
				}
			});
			TWeakObjectPtr<UMobileSelectWidget> WeakSelect(Select);
			Select->SetOnOpened([WeakThis, WeakSelect]()
			{
				if (UMobileSearchPageWidget* Self = WeakThis.Get())
				{
					Self->CloseOtherSelects(WeakSelect.Get());
				}
			});
			return Select;
		};
		CategorySelect = MakeSelect(TEXT("Category"), [](UMobileSearchPageWidget* Self, const FString& Value) { Self->SetCategory(Value); });
		BuildingSelect = MakeSelect(TEXT("Building"), [](UMobileSearchPageWidget* Self, const FString& Value) { Self->SetBuilding(Value); });
		LevelSelect = MakeSelect(TEXT("Level"), [](UMobileSearchPageWidget* Self, const FString& Value) { Self->SetLevel(Value.IsEmpty() ? INDEX_NONE : FCString::Atoi(*Value)); });
		UHorizontalBox* SelectRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FilterSelects"));
		SelectRow->AddChildToHorizontalBox(CategorySelect)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UHorizontalBoxSlot* BuildingSlot = SelectRow->AddChildToHorizontalBox(BuildingSelect);
		BuildingSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BuildingSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		UHorizontalBoxSlot* LevelSlot = SelectRow->AddChildToHorizontalBox(LevelSelect);
		LevelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LevelSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		FilterPanel->AddChildToVerticalBox(SelectRow);
		Page->AddChildToVerticalBox(FilterPanel)->SetPadding(FMargin(16.f, 8.f, 16.f, 0.f));

		// The list.
		ListScroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ListScroll"));
		ListBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ListBox"));
		MobileStyle::NoScrollShadow(ListScroll);
		ListScroll->AddChild(ListBox);
		Page->AddChildToVerticalBox(ListScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Refresh();
	}
	return Super::RebuildWidget();
}

void UMobileSearchPageWidget::SetSource(IMobileNavSource* InSource, const FString& InSiteId)
{
	Source = InSource;
	SiteId = InSiteId;
	Filter = FMobilePlaceFilter();
	ContextBuildingId.Reset();
	bFiltersOpen = false;
	Refresh();
}

void UMobileSearchPageWidget::SetContextBuilding(const FString& BuildingId)
{
	if (ContextBuildingId != BuildingId)
	{
		ContextBuildingId = BuildingId;
		Refresh();
	}
}

void UMobileSearchPageWidget::SetCategory(const FString& Category)
{
	Filter.Categories.Reset();
	if (!Category.IsEmpty())
	{
		Filter.Categories.Add(Category);
	}
	Refresh();
}

void UMobileSearchPageWidget::SetBuilding(const FString& BuildingId)
{
	Filter.BuildingId = BuildingId;
	Refresh();
}

void UMobileSearchPageWidget::SetLevel(int32 Level)
{
	Filter.Level = Level;
	Refresh();
}

void UMobileSearchPageWidget::ClearFilters()
{
	Filter = FMobilePlaceFilter();
	Refresh();
}

void UMobileSearchPageWidget::SetFiltersOpen(bool bOpen)
{
	bFiltersOpen = bOpen;
	Refresh();
}

int32 UMobileSearchPageWidget::GetActiveFilterCount() const
{
	return (Filter.Categories.IsEmpty() ? 0 : 1) + (Filter.BuildingId.IsEmpty() ? 0 : 1) + (Filter.Level == INDEX_NONE ? 0 : 1);
}

FString UMobileSearchPageWidget::GetTitle() const
{
	if (!Source)
	{
		return FString();
	}
	const FString& TitleBuilding = Filter.BuildingId.IsEmpty() ? ContextBuildingId : Filter.BuildingId;
	if (!TitleBuilding.IsEmpty())
	{
		for (const FMobileBuilding& Building : Source->ListBuildings(SiteId))
		{
			if (Building.Id == TitleBuilding)
			{
				return Building.Name.IsEmpty() ? Building.Id : Building.Name;
			}
		}
		return TitleBuilding;
	}
	for (const FMobileSite& Site : Source->ListSites())
	{
		if (Site.Id == SiteId)
		{
			return Site.Name;
		}
	}
	return SiteId;
}

void UMobileSearchPageWidget::HandleFiltersClicked()
{
	SetFiltersOpen(!bFiltersOpen);
}

void UMobileSearchPageWidget::CloseOtherSelects(UMobileSelectWidget* Keep)
{
	for (UMobileSelectWidget* Select : { CategorySelect, BuildingSelect, LevelSelect })
	{
		if (Select && Select != Keep && Select->IsOpen())
		{
			Select->SetOpen(false);
		}
	}
}

void UMobileSearchPageWidget::RefreshHeader()
{
	if (TitleText)
	{
		TitleText->SetText(FText::FromString(GetTitle()));
	}
	const int32 Active = GetActiveFilterCount();
	if (FiltersButton)
	{
		// Blue while the panel is open, soft grey otherwise.
		const FLinearColor Fill = bFiltersOpen ? Blue : SoftPanel;
		FiltersButton->SetStyle(StartStyle::Button(Fill, Fill * 0.94f, Fill * 0.86f, FLinearColor::Transparent, 10.f / StartStyle::DesignToUi));
	}
	if (FiltersIcon)
	{
		FiltersIcon->SetColorAndOpacity(bFiltersOpen ? OnBlue : Grey);
	}
	if (FiltersLabel)
	{
		FiltersLabel->SetColorAndOpacity(FSlateColor(bFiltersOpen ? OnBlue : Grey));
	}
	if (FiltersBadge)
	{
		FiltersBadge->SetVisibility(Active > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (FiltersBadgeText)
	{
		FiltersBadgeText->SetText(FText::AsNumber(Active));
	}
	if (FilterPanel)
	{
		FilterPanel->SetVisibility(bFiltersOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (!Source || !CategorySelect || !BuildingSelect || !LevelSelect)
	{
		return;
	}
	// The drop-downs: All first, then what the site has. The building one is only for a campus.
	TArray<FMobileSelectOption> Categories = { { FString(), TEXT("All") } };
	for (const FString& Category : Source->ListCategories(SiteId))
	{
		Categories.Add({ Category, Category });
	}
	CategorySelect->SetOptions(Categories, Filter.Categories.IsEmpty() ? FString() : Filter.Categories[0]);

	const TArray<FMobileBuilding> Buildings = Source->ListBuildings(SiteId);
	TArray<FMobileSelectOption> BuildingOptions = { { FString(), TEXT("All buildings") } };
	TMap<int32, FString> LevelNames;
	for (const FMobileBuilding& Building : Buildings)
	{
		BuildingOptions.Add({ Building.Id, Building.Name.IsEmpty() ? Building.Id : Building.Name });
		for (const FMobileLevel& Level : Building.Levels)
		{
			if (!LevelNames.Contains(Level.Level))
			{
				LevelNames.Add(Level.Level, Level.Label);
			}
		}
	}
	BuildingSelect->SetOptions(BuildingOptions, Filter.BuildingId);
	BuildingSelect->SetVisibility(Buildings.Num() > 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	TArray<int32> LevelKeys;
	LevelNames.GetKeys(LevelKeys);
	LevelKeys.Sort();
	TArray<FMobileSelectOption> LevelOptions = { { FString(), TEXT("All levels") } };
	for (int32 Level : LevelKeys)
	{
		LevelOptions.Add({ FString::FromInt(Level), LevelNames[Level] });
	}
	LevelSelect->SetOptions(LevelOptions, Filter.Level == INDEX_NONE ? FString() : FString::FromInt(Filter.Level));
}

void UMobileSearchPageWidget::Refresh()
{
	ShownPlaces.Reset();
	GroupCount = 0;
	LastGroup.Reset();
	if (!ListBox || !WidgetTree)
	{
		return;
	}
	RefreshHeader();
	ListBox->ClearChildren();
	if (!Source)
	{
		return;
	}
	FMobilePlaceFilter Effective = Filter;
	if (Effective.BuildingId.IsEmpty())
	{
		Effective.BuildingId = ContextBuildingId;
	}
	ShownPlaces = Source->SearchPlaces(SiteId, FString(), Effective);
	for (const FMobilePlace& Place : ShownPlaces)
	{
		const FString Group = Place.GroupLabel();
		if (Group != LastGroup)
		{
			AddGroupHeading(Group);
			LastGroup = Group;
		}
		AddPlaceRow(Place);
	}
	if (ShownPlaces.IsEmpty())
	{
		UBorder* Empty = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Empty"));
		Empty->SetBrushColor(FLinearColor::Transparent);
		Empty->SetPadding(FMargin(16.f, 24.f));
		UTextBlock* Message = MakeText(WidgetTree, FString(), TEXT("No places match. Try other filters."), 13.f, Grey);
		Message->SetAutoWrapText(true);
		Empty->SetContent(Message);
		ListBox->AddChildToVerticalBox(Empty);
	}
}

void UMobileSearchPageWidget::ChoosePlace(const FString& PlaceId)
{
	FMobilePlace Place;
	if (Source && Source->FindPlace(PlaceId, Place) && OnPlaceChosen)
	{
		OnPlaceChosen(Place);
	}
}

UWidget* UMobileSearchPageWidget::FindNamedWidget(const FName& Name) const
{
	return WidgetTree ? WidgetTree->FindWidget(Name) : nullptr;
}

void UMobileSearchPageWidget::AddGroupHeading(const FString& Label)
{
	++GroupCount;
	UBorder* Heading = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Heading->SetBrushColor(FLinearColor::Transparent);
	Heading->SetPadding(FMargin(16.f, 16.f, 16.f, 4.f));
	Heading->SetContent(MakeText(WidgetTree, FString(), Label, 14.f, Grey, true));
	ListBox->AddChildToVerticalBox(Heading);
}

void UMobileSearchPageWidget::AddPlaceRow(const FMobilePlace& Place)
{
	// The icon: a map pin on a soft square.
	UBorder* Icon = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Icon->SetBrush(Rounded(SoftPanel, FLinearColor::Transparent, 8.f, 0.f));
	Icon->SetBrushColor(FLinearColor::White);
	Icon->SetHorizontalAlignment(HAlign_Center);
	Icon->SetVerticalAlignment(VAlign_Center);
	// The design team's map pin; until its texture is imported, the category's first letter.
	UWidget* Pin = MobileIcons::Picture(WidgetTree, TEXT("Location"), 20.f, Blue);
	Icon->SetContent(Pin ? Pin : static_cast<UWidget*>(MakeText(WidgetTree, FString(), Place.Category.Left(1).ToUpper(), 16.f, Blue, true)));
	USizeBox* IconSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	IconSize->SetWidthOverride(32.f);
	IconSize->SetHeightOverride(32.f);
	IconSize->SetContent(Icon);

	UVerticalBox* Text = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Text->AddChildToVerticalBox(MakeText(WidgetTree, FString(), Place.Name, 15.f, Ink, true));
	UHorizontalBox* Detail = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Detail->AddChildToHorizontalBox(MakeText(WidgetTree, FString(), Place.WithCode(Place.Category), 11.f, Grey))->SetVerticalAlignment(VAlign_Center);
	if (!Place.bAvailable)
	{
		UBorder* Pill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Pill->SetBrush(Rounded(RedSoft, FLinearColor::Transparent, 10.f, 0.f));
		Pill->SetBrushColor(FLinearColor::White);
		Pill->SetPadding(FMargin(8.f, 2.f));
		Pill->SetContent(MakeText(WidgetTree, FString(), TEXT("Unavailable"), 10.f, Red, true));
		Detail->AddChildToHorizontalBox(Pill)->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
	}
	Text->AddChildToVerticalBox(Detail)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Row->AddChildToHorizontalBox(IconSize)->SetVerticalAlignment(VAlign_Center);
	UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
	TextSlot->SetPadding(FMargin(12.f, 0.f, 8.f, 0.f));
	TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TextSlot->SetVerticalAlignment(VAlign_Center);
	Row->AddChildToHorizontalBox(MakeText(WidgetTree, FString(), Place.GroupLabel(), 12.f, Blue, true))->SetVerticalAlignment(VAlign_Center);

	UBorder* Padded = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Padded->SetBrushColor(FLinearColor::Transparent);
	Padded->SetPadding(FMargin(16.f, 10.f));
	Padded->SetContent(Row);

	UMobileTapTarget* Tap = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
	Tap->Setup(Padded, Place.Id, FLinearColor::Transparent, 0.f);
	TWeakObjectPtr<UMobileSearchPageWidget> WeakThis(this);
	Tap->SetOnTapped([WeakThis](const FString& Id)
	{
		if (UMobileSearchPageWidget* Page = WeakThis.Get())
		{
			Page->ChoosePlace(Id);
		}
	});
	ListBox->AddChildToVerticalBox(Tap);

	USizeBox* Divider = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Divider->SetHeightOverride(1.f);
	UBorder* Rule = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Rule->SetBrushColor(Line);
	Divider->SetContent(Rule);
	ListBox->AddChildToVerticalBox(Divider)->SetPadding(FMargin(16.f, 0.f));
}

void UMobileSearchPageWidget::SetStartLabel(const FString& Label)
{
	StartLabel = Label;
	if (StartRow && StartText)
	{
		StartRow->SetVisibility(Label.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		StartText->SetText(FText::FromString(Label.IsEmpty() ? FString() : TEXT("Starting from ") + Label));
	}
}

void UMobileSearchPageWidget::HandleClearStartClicked()
{
	ClearStart();
}
