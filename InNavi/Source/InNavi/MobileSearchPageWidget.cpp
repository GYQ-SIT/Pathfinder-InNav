#include "MobileSearchPageWidget.h"

#include "MobileStyle.h"
#include "MobileTapTarget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"

using namespace MobileStyle;

TSharedRef<SWidget> UMobileSearchPageWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		UVerticalBox* Page = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SearchPage"));
		Tree->RootWidget = Page;

		// The back arrow on the left, the title on the right.
		UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SearchHeader"));
		UTextBlock* BackLabel = nullptr;
		UButton* Back = MakeButton(Tree, TEXT("Btn_SearchBack"), TEXT("‹"), White, Blue, 10.f, 36.f, &BackLabel, 14.f);
		ApplyFont(BackLabel, 28.f, true);
		Back->OnClicked.AddUniqueDynamic(this, &UMobileSearchPageWidget::HandleBackClicked);
		Header->AddChildToHorizontalBox(Back)->SetVerticalAlignment(VAlign_Center);
		Header->AddChildToHorizontalBox(Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("SearchHeaderSpacer")))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Header->AddChildToHorizontalBox(MakeText(Tree, TEXT("Txt_SearchTitle"), TEXT("Where to?"), 22.f, Ink, true))->SetVerticalAlignment(VAlign_Center);
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

		// The search box and the filters toggle.
		UHorizontalBox* SearchRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SearchRow"));
		SearchBox = Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_Search"));
		SearchBox->SetWidgetStyle(SearchField(15.f));
		SearchBox->SetHintText(FText::FromString(TEXT("Search a place or room")));
		SearchBox->OnTextChanged.AddUniqueDynamic(this, &UMobileSearchPageWidget::HandleQueryChanged);
		SearchRow->AddChildToHorizontalBox(SearchBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UButton* FiltersButton = MakeButton(Tree, TEXT("Btn_Filters"), TEXT("Filters"), White, Grey, 10.f, 40.f, &FiltersLabel, 14.f);
		FiltersButton->OnClicked.AddUniqueDynamic(this, &UMobileSearchPageWidget::HandleFiltersClicked);
		SearchRow->AddChildToHorizontalBox(FiltersButton)->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		Page->AddChildToVerticalBox(SearchRow)->SetPadding(FMargin(16.f, 12.f, 16.f, 0.f));

		// The filters, shown when the toggle is open.
		FilterPanel = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FilterPanel"));
		FilterPanel->SetVisibility(ESlateVisibility::Collapsed);
		ChipBox = Tree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("ChipBox"));
		ChipBox->SetInnerSlotPadding(FVector2D(8.f, 8.f));
		FilterPanel->AddChildToVerticalBox(ChipBox);
		Page->AddChildToVerticalBox(FilterPanel)->SetPadding(FMargin(16.f, 12.f, 16.f, 0.f));

		// The list.
		ListScroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ListScroll"));
		ListBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ListBox"));
		ListScroll->AddChild(ListBox);
		Page->AddChildToVerticalBox(ListScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Refresh();
	}
	return Super::RebuildWidget();
}

void UMobileSearchPageWidget::SetSource(IMobileNavSource* InSource, const FString& InSiteId, const FString& InContextBuildingId, int32 InContextLevel)
{
	Source = InSource;
	SiteId = InSiteId;
	ContextBuildingId = InContextBuildingId;
	ContextLevel = InContextLevel;
	Filter = FMobilePlaceFilter();
	Query.Reset();
	bBrowseAll = false;
	if (SearchBox)
	{
		SearchBox->SetText(FText::GetEmpty());
	}
	Refresh();
}

void UMobileSearchPageWidget::SetQuery(const FString& NewQuery)
{
	Query = NewQuery;
	if (SearchBox && SearchBox->GetText().ToString() != NewQuery)
	{
		SearchBox->SetText(FText::FromString(NewQuery));
	}
	Refresh();
}

void UMobileSearchPageWidget::ToggleCategory(const FString& Category)
{
	if (!Filter.Categories.Remove(Category))
	{
		Filter.Categories.Add(Category);
	}
	Refresh();
}

void UMobileSearchPageWidget::ToggleAvailableOnly()
{
	Filter.bAvailableOnly = !Filter.bAvailableOnly;
	Refresh();
}

void UMobileSearchPageWidget::ClearFilters()
{
	Filter.Categories.Reset();
	Filter.bAvailableOnly = false;
	Refresh();
}

void UMobileSearchPageWidget::SetFiltersOpen(bool bOpen)
{
	bFiltersOpen = bOpen;
	Refresh();
}

void UMobileSearchPageWidget::BrowseAll()
{
	bBrowseAll = true;
	Refresh();
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

void UMobileSearchPageWidget::HandleQueryChanged(const FText& Text)
{
	if (Text.ToString() != Query)
	{
		Query = Text.ToString();
		Refresh();
	}
}

void UMobileSearchPageWidget::HandleBackClicked()
{
	if (OnBack)
	{
		OnBack();
	}
}

void UMobileSearchPageWidget::HandleFiltersClicked()
{
	SetFiltersOpen(!bFiltersOpen);
}

void UMobileSearchPageWidget::HandleBrowseAllClicked()
{
	BrowseAll();
}

void UMobileSearchPageWidget::RefreshFilterChips()
{
	if (!ChipBox || !WidgetTree)
	{
		return;
	}
	ChipBox->ClearChildren();
	auto AddChip = [this](const FString& Payload, const FString& Label, bool bOn)
	{
		UMobileTapTarget* Chip = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
		UBorder* Pill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Pill->SetBrushColor(FLinearColor::Transparent);
		Pill->SetPadding(FMargin(14.f, 8.f));
		Pill->SetContent(MakeText(WidgetTree, FString(), Label, 13.f, bOn ? White : Blue, true));
		Chip->Setup(Pill, Payload, bOn ? Blue : BlueSoft, 16.f);
		TWeakObjectPtr<UMobileSearchPageWidget> WeakThis(this);
		Chip->SetOnTapped([WeakThis](const FString& Tapped)
		{
			UMobileSearchPageWidget* Page = WeakThis.Get();
			if (!Page)
			{
				return;
			}
			if (Tapped == TEXT("available"))
			{
				Page->ToggleAvailableOnly();
			}
			else if (Tapped == TEXT("clear"))
			{
				Page->ClearFilters();
			}
			else if (Tapped.StartsWith(TEXT("cat:")))
			{
				Page->ToggleCategory(Tapped.Mid(4));
			}
		});
		ChipBox->AddChildToWrapBox(Chip);
	};
	if (Source)
	{
		for (const FString& Category : Source->ListCategories(SiteId))
		{
			AddChip(TEXT("cat:") + Category, Category, Filter.Categories.Contains(Category));
		}
	}
	AddChip(TEXT("available"), TEXT("Available only"), Filter.bAvailableOnly);
	if (!Filter.Categories.IsEmpty() || Filter.bAvailableOnly)
	{
		AddChip(TEXT("clear"), TEXT("Clear"), false);
	}
}

void UMobileSearchPageWidget::AddGroupHeading(const FString& Label)
{
	++GroupCount;
	UBorder* Heading = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Heading->SetBrushColor(FLinearColor::Transparent);
	Heading->SetPadding(FMargin(16.f, 16.f, 16.f, 4.f));
	Heading->SetContent(MakeText(WidgetTree, FString(), Label, 12.f, Grey, true));
	ListBox->AddChildToVerticalBox(Heading);
}

void UMobileSearchPageWidget::AddPlaceRow(const FMobilePlace& Place)
{
	// The icon is a placeholder: the category's first letter on a soft square, until the design team's icons are in.
	UBorder* Icon = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Icon->SetBrush(Rounded(BlueSoft, FLinearColor::Transparent, 10.f, 0.f));
	Icon->SetBrushColor(FLinearColor::White);
	Icon->SetHorizontalAlignment(HAlign_Center);
	Icon->SetVerticalAlignment(VAlign_Center);
	Icon->SetContent(MakeText(WidgetTree, FString(), Place.Category.Left(1).ToUpper(), 16.f, Blue, true));
	USizeBox* IconSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	IconSize->SetWidthOverride(40.f);
	IconSize->SetHeightOverride(40.f);
	IconSize->SetContent(Icon);

	UVerticalBox* Text = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Text->AddChildToVerticalBox(MakeText(WidgetTree, FString(), Place.Name, 15.f, Ink, true));
	UHorizontalBox* Detail = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Detail->AddChildToHorizontalBox(MakeText(WidgetTree, FString(), Place.Code + TEXT(" · ") + Place.Category, 11.f, Grey))->SetVerticalAlignment(VAlign_Center);
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
	Row->AddChildToHorizontalBox(MakeText(WidgetTree, FString(), Place.LevelLabel, 12.f, Blue, true))->SetVerticalAlignment(VAlign_Center);

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

void UMobileSearchPageWidget::Refresh()
{
	ShownPlaces.Reset();
	GroupCount = 0;
	LastGroup.Reset();
	if (!ListBox || !WidgetTree)
	{
		return;
	}
	if (FilterPanel)
	{
		FilterPanel->SetVisibility(bFiltersOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (FiltersLabel)
	{
		FString Text = TEXT("Filters");
		const int32 Active = Filter.Categories.Num() + (Filter.bAvailableOnly ? 1 : 0);
		if (Active > 0)
		{
			Text += FString::Printf(TEXT(" (%d)"), Active);
		}
		FiltersLabel->SetText(FText::FromString(Text));
		// Blue while the panel is open or a filter is on, grey otherwise.
		FiltersLabel->SetColorAndOpacity(FSlateColor(bFiltersOpen || Active > 0 ? Blue : Grey));
	}
	RefreshFilterChips();

	ListBox->ClearChildren();
	if (!Source)
	{
		return;
	}
	// With nothing typed and no filter the list is the place the person is in; Browse all places opens the rest.
	const bool bFiltered = !Query.TrimStartAndEnd().IsEmpty() || !Filter.Categories.IsEmpty() || Filter.bAvailableOnly;
	const bool bAroundMe = !bFiltered && !bBrowseAll;
	FMobilePlaceFilter Effective = Filter;
	if (bAroundMe)
	{
		Effective.BuildingId = ContextBuildingId;
		Effective.Level = ContextLevel;
	}
	ShownPlaces = Source->SearchPlaces(SiteId, Query, Effective);
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
		UTextBlock* Message = MakeText(WidgetTree, FString(), TEXT("No places match. Try another word, or clear the filters."), 13.f, Grey);
		Message->SetAutoWrapText(true);
		Empty->SetContent(Message);
		ListBox->AddChildToVerticalBox(Empty);
	}
	if (bAroundMe)
	{
		UButton* Browse = MakeButton(WidgetTree, TEXT("Btn_BrowseAll"), TEXT("Browse all places"), White, Blue, 12.f, 44.f);
		Browse->OnClicked.AddUniqueDynamic(this, &UMobileSearchPageWidget::HandleBrowseAllClicked);
		ListBox->AddChildToVerticalBox(Browse)->SetPadding(FMargin(16.f, 14.f, 16.f, 16.f));
	}
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
