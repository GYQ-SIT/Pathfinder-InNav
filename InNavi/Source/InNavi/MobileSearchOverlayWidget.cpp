#include "MobileSearchOverlayWidget.h"

#include "MobileIcons.h"
#include "MobileSearchHistory.h"
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
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace MobileStyle;

namespace
{
	// Every word of the query is somewhere in the text (any case); the same rule the sources use for places.
	bool OverlayMatchesWords(const FString& Text, const FString& Query)
	{
		TArray<FString> Words;
		Query.ParseIntoArrayWS(Words);
		for (const FString& Word : Words)
		{
			if (!Text.Contains(Word, ESearchCase::IgnoreCase))
			{
				return false;
			}
		}
		return true;
	}
}

TSharedRef<SWidget> UMobileSearchOverlayWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		// It covers the whole screen, on the page's colour, so nothing of the map or the tabs shows through (or can be tapped).
		UBorder* Root = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SearchOverlayRoot"));
		Root->SetBrushColor(PageBg);
		Root->SetPadding(FMargin(16.f, 12.f, 16.f, 0.f));
		Root->SetVisibility(ESlateVisibility::Visible);
		Tree->RootWidget = Root;
		UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SearchOverlayColumn"));
		Root->SetContent(Column);

		// The field: a soft rounded box with a blue edge, the magnifier, the text, and a close button.
		UBorder* Field = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SearchOverlayField"));
		Field->SetBrush(Rounded(SoftPanel, Blue, 24.f, 1.f));
		Field->SetBrushColor(FLinearColor::White);
		Field->SetPadding(FMargin(16.f, 0.f, 0.f, 0.f));
		USizeBox* FieldSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SearchOverlayFieldSize"));
		FieldSize->SetHeightOverride(46.f);
		FieldSize->SetContent(Field);
		UHorizontalBox* FieldRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SearchOverlayFieldRow"));
		Field->SetContent(FieldRow);
		FieldRow->AddChildToHorizontalBox(MobileIcons::Magnifier(Tree, 20.f, Ink))->SetVerticalAlignment(VAlign_Center);

		SearchBox = Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_OverlaySearch"));
		FEditableTextBoxStyle Style;
		const FSlateBrush NoBox = Rounded(FLinearColor::Transparent, FLinearColor::Transparent, 0.f, 0.f);
		Style.BackgroundImageNormal = NoBox;
		Style.BackgroundImageHovered = NoBox;
		Style.BackgroundImageFocused = NoBox;
		Style.BackgroundImageReadOnly = NoBox;
		Style.Padding = FMargin(0.f, 10.f);
		Style.ForegroundColor = FSlateColor(Ink);
		Style.FocusedForegroundColor = FSlateColor(Ink);
		Style.TextStyle.Font.Size = FMath::RoundToInt(16.f * TextScale * StartStyle::PixelsToFont);
		Style.TextStyle.ColorAndOpacity = FSlateColor(Ink);
		SearchBox->SetWidgetStyle(Style);
		SearchBox->SetHintText(FText::FromString(TEXT("Search buildings or places")));
		SearchBox->OnTextChanged.AddUniqueDynamic(this, &UMobileSearchOverlayWidget::HandleQueryChanged);
		SearchBox->OnTextCommitted.AddUniqueDynamic(this, &UMobileSearchOverlayWidget::HandleQueryCommitted);
		UHorizontalBoxSlot* BoxSlot = FieldRow->AddChildToHorizontalBox(SearchBox);
		BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BoxSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		BoxSlot->SetVerticalAlignment(VAlign_Center);

		// The close button: a cross in a 44 x 44 tap area.
		UButton* Close = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_OverlayClose"));
		Close->SetStyle(StartStyle::Button(FLinearColor::Transparent, TapHover, TapHover * 2.f, FLinearColor::Transparent, 22.f / StartStyle::DesignToUi));
		Close->SetBackgroundColor(FLinearColor::White);
		UBorder* CloseCentre = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SearchOverlayCloseCentre"));
		CloseCentre->SetBrushColor(FLinearColor::Transparent);
		CloseCentre->SetHorizontalAlignment(HAlign_Center);
		CloseCentre->SetVerticalAlignment(VAlign_Center);
		CloseCentre->SetContent(MobileIcons::Cross(Tree, 20.f, Ink));
		Close->SetContent(CloseCentre);
		USizeBox* CloseSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SearchOverlayCloseSize"));
		CloseSize->SetWidthOverride(44.f);
		CloseSize->SetHeightOverride(44.f);
		CloseSize->SetContent(Close);
		Close->OnClicked.AddUniqueDynamic(this, &UMobileSearchOverlayWidget::HandleCloseClicked);
		FieldRow->AddChildToHorizontalBox(CloseSize)->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		Column->AddChildToVerticalBox(FieldSize);

		// The list under it, which scrolls.
		ListScroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SearchOverlayScroll"));
		ListBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SearchOverlayList"));
		ListScroll->AddChild(ListBox);
		Column->AddChildToVerticalBox(ListScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		Refresh();
	}
	return Super::RebuildWidget();
}

void UMobileSearchOverlayWidget::SetSource(IMobileNavSource* InSource, const FString& InSiteId)
{
	Source = InSource;
	SiteId = InSiteId;
	Refresh();
}

void UMobileSearchOverlayWidget::SetHistory(FMobileSearchHistory* InHistory)
{
	History = InHistory;
	Refresh();
}

UWidget* UMobileSearchOverlayWidget::FindNamedWidget(const FName& Name) const
{
	return WidgetTree ? WidgetTree->FindWidget(Name) : nullptr;
}

void UMobileSearchOverlayWidget::Open(EMode InMode)
{
	Mode = InMode;
	bOpen = true;
	Query.Reset();
	if (SearchBox)
	{
		SearchBox->SetText(FText::GetEmpty());
		SearchBox->SetHintText(FText::FromString(Mode == EMode::Places ? TEXT("Search buildings or places") : TEXT("Search sites")));
	}
	SetVisibility(ESlateVisibility::Visible);
	Refresh();
	if (SearchBox)
	{
		SearchBox->SetKeyboardFocus();
	}
}

void UMobileSearchOverlayWidget::Close()
{
	const bool bWasOpen = bOpen;
	bOpen = false;
	Query.Reset();
	if (SearchBox)
	{
		SearchBox->SetText(FText::GetEmpty());
	}
	SetVisibility(ESlateVisibility::Collapsed);
	Refresh();
	if (bWasOpen && OnClosed)
	{
		OnClosed();
	}
}

void UMobileSearchOverlayWidget::SetQuery(const FString& NewQuery)
{
	if (SearchBox && SearchBox->GetText().ToString() != NewQuery)
	{
		SearchBox->SetText(FText::FromString(NewQuery));
	}
	if (Query != NewQuery)
	{
		Query = NewQuery;
		Refresh();
	}
}

void UMobileSearchOverlayWidget::HandleQueryChanged(const FText& Text)
{
	if (Text.ToString() != Query)
	{
		Query = Text.ToString();
		Refresh();
	}
}

void UMobileSearchOverlayWidget::HandleQueryCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		Submit();
	}
}

void UMobileSearchOverlayWidget::HandleCloseClicked()
{
	Close();
}

void UMobileSearchOverlayWidget::HandleClearClicked()
{
	ClearHistory();
}

void UMobileSearchOverlayWidget::Submit()
{
	if (Mode == EMode::Sites && History)
	{
		History->AddQuery(Query);
	}
}

void UMobileSearchOverlayWidget::ClearHistory()
{
	if (History)
	{
		if (Mode == EMode::Places) History->ClearChosen();
		else History->ClearQueries();
	}
	Refresh();
}

FString UMobileSearchOverlayWidget::PlaceSubtitle(const FMobilePlace& Place)
{
	return Place.WithCode(Place.GroupLabel());
}

void UMobileSearchOverlayWidget::ChooseRow(const FString& Key)
{
	if (Key.StartsWith(TEXT("query:")))
	{
		// A recent search of words: typed again, and kept at the top.
		const FString Words = Key.Mid(6);
		if (History)
		{
			History->AddQuery(Words);
		}
		SetQuery(Words);
		return;
	}
	if (Key.StartsWith(TEXT("site:")))
	{
		const FString Id = Key.Mid(5);
		if (History && !Query.TrimStartAndEnd().IsEmpty())
		{
			History->AddQuery(Query);
		}
		const TFunction<void(const FString&)> Callback = OnSiteChosen;
		Close();
		if (Callback)
		{
			Callback(Id);
		}
		return;
	}
	FString Kind, Id;
	if (!FMobileSearchHistory::ParseKey(Key, Kind, Id))
	{
		return;
	}
	if (Kind == TEXT("building"))
	{
		if (History)
		{
			History->AddChosen(Key);
		}
		const TFunction<void(const FString&)> Callback = OnBuildingChosen;
		Close();
		if (Callback)
		{
			Callback(Id);
		}
		return;
	}
	FMobilePlace Place;
	if (!Source || !Source->FindPlace(Id, Place))
	{
		return;
	}
	if (History)
	{
		History->AddChosen(Key);
	}
	const TFunction<void(const FMobilePlace&)> Callback = OnPlaceChosen;
	Close();
	if (Callback)
	{
		Callback(Place);
	}
}

void UMobileSearchOverlayWidget::AddHeading(const FString& Text, bool bWithClear)
{
	Heading = Text;
	bClearShown = bWithClear;
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SearchOverlayHeading"));
	UTextBlock* Label = MakeText(WidgetTree, TEXT("Txt_OverlayHeading"), Text, 14.f, Grey, true);
	UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label);
	LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LabelSlot->SetVerticalAlignment(VAlign_Center);
	if (bWithClear)
	{
		// Plain blue words with no box, as in the prototype; a tap area of 36 high.
		UButton* Clear = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_OverlayClearHistory"));
		Clear->SetStyle(StartStyle::Button(FLinearColor::Transparent, TapHover, TapHover * 2.f, FLinearColor::Transparent, 8.f / StartStyle::DesignToUi));
		Clear->SetBackgroundColor(FLinearColor::White);
		UBorder* ClearPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		ClearPad->SetBrushColor(FLinearColor::Transparent);
		ClearPad->SetPadding(FMargin(8.f, 8.f));
		ClearPad->SetContent(MakeText(WidgetTree, TEXT("Btn_OverlayClearHistory_Label"), TEXT("Clear history"), 14.f, Blue, true));
		Clear->SetContent(ClearPad);
		Clear->OnClicked.AddUniqueDynamic(this, &UMobileSearchOverlayWidget::HandleClearClicked);
		Row->AddChildToHorizontalBox(Clear)->SetVerticalAlignment(VAlign_Center);
	}
	UBorder* Padded = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Padded->SetBrushColor(FLinearColor::Transparent);
	Padded->SetPadding(FMargin(4.f, 8.f, 0.f, 4.f));
	Padded->SetContent(Row);
	ListBox->AddChildToVerticalBox(Padded);
}

void UMobileSearchOverlayWidget::AddNote(const FString& Text)
{
	UTextBlock* Note = MakeText(WidgetTree, TEXT("Txt_OverlayNote"), Text, 14.f, Grey);
	Note->SetAutoWrapText(true);
	UBorder* Padded = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Padded->SetBrushColor(FLinearColor::Transparent);
	Padded->SetPadding(FMargin(12.f, 8.f));
	Padded->SetContent(Note);
	ListBox->AddChildToVerticalBox(Padded);
}

void UMobileSearchOverlayWidget::AddRow(const FString& Key, const FString& Title, const FString& Subtitle, bool bClockIcon)
{
	ShownKeys.Add(Key);

	// A soft circle with the icon, the name over its detail, and an arrow.
	UBorder* Circle = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Circle->SetBrush(Rounded(SoftPanel, FLinearColor::Transparent, 20.f, 0.f));
	Circle->SetBrushColor(FLinearColor::White);
	Circle->SetHorizontalAlignment(HAlign_Center);
	Circle->SetVerticalAlignment(VAlign_Center);
	Circle->SetContent(bClockIcon ? MobileIcons::Clock(WidgetTree, 20.f, Grey) : MobileIcons::Magnifier(WidgetTree, 20.f, Grey));
	USizeBox* CircleSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	CircleSize->SetWidthOverride(40.f);
	CircleSize->SetHeightOverride(40.f);
	CircleSize->SetContent(Circle);

	UVerticalBox* Text = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Text->AddChildToVerticalBox(MakeText(WidgetTree, FString(), Title, 14.f, Ink, true));
	if (!Subtitle.IsEmpty())
	{
		Text->AddChildToVerticalBox(MakeText(WidgetTree, FString(), Subtitle, 12.f, Grey))->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	}

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Row->AddChildToHorizontalBox(CircleSize)->SetVerticalAlignment(VAlign_Center);
	UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
	TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TextSlot->SetPadding(FMargin(16.f, 0.f, 8.f, 0.f));
	TextSlot->SetVerticalAlignment(VAlign_Center);
	Row->AddChildToHorizontalBox(MobileIcons::Arrow(WidgetTree, 20.f, Ink))->SetVerticalAlignment(VAlign_Center);

	UBorder* Padded = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Padded->SetBrushColor(FLinearColor::Transparent);
	Padded->SetPadding(FMargin(4.f, 16.f));
	Padded->SetContent(Row);

	UMobileTapTarget* Tap = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
	Tap->Setup(Padded, Key, FLinearColor::Transparent, 0.f);
	TWeakObjectPtr<UMobileSearchOverlayWidget> WeakThis(this);
	Tap->SetOnTapped([WeakThis](const FString& Tapped)
	{
		if (UMobileSearchOverlayWidget* Overlay = WeakThis.Get())
		{
			Overlay->ChooseRow(Tapped);
		}
	});
	ListBox->AddChildToVerticalBox(Tap);

	USizeBox* Divider = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Divider->SetHeightOverride(1.f);
	UBorder* Rule = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Rule->SetBrushColor(FieldEdge);
	Divider->SetContent(Rule);
	ListBox->AddChildToVerticalBox(Divider);
}

void UMobileSearchOverlayWidget::Refresh()
{
	ShownKeys.Reset();
	Heading.Reset();
	EmptyText.Reset();
	bClearShown = false;
	if (!ListBox || !WidgetTree)
	{
		return;
	}
	ListBox->ClearChildren();
	if (!bOpen || !Source)
	{
		return;
	}
	const FString Typed = Query.TrimStartAndEnd();

	if (Typed.IsEmpty())
	{
		// Nothing typed: what was searched before.
		if (Mode == EMode::Sites)
		{
			const TArray<FString> Recent = History ? History->Queries() : TArray<FString>();
			AddHeading(TEXT("Recent searches"), !Recent.IsEmpty());
			for (const FString& Words : Recent)
			{
				AddRow(TEXT("query:") + Words, Words, FString(), true);
			}
			if (Recent.IsEmpty())
			{
				EmptyText = TEXT("No recent searches yet.");
				AddNote(EmptyText);
			}
			return;
		}
		// Keys of buildings and places that are not (or no longer) of this site are left out.
		const TArray<FMobileBuilding> Buildings = Source->ListBuildings(SiteId);
		TArray<FString> Keys;
		TArray<TTuple<FString, FString>> Lines;
		for (const FString& Key : (History ? History->Chosen() : TArray<FString>()))
		{
			FString Kind, Id;
			if (!FMobileSearchHistory::ParseKey(Key, Kind, Id))
			{
				continue;
			}
			if (Kind == TEXT("building"))
			{
				if (const FMobileBuilding* Building = Buildings.FindByPredicate([&Id](const FMobileBuilding& Candidate) { return Candidate.Id == Id; }))
				{
					Keys.Add(Key);
					Lines.Add(MakeTuple(Building->Name, FString(TEXT("Building"))));
				}
			}
			else
			{
				FMobilePlace Place;
				if (Source->FindPlace(Id, Place) && Place.SiteId == SiteId)
				{
					Keys.Add(Key);
					Lines.Add(MakeTuple(Place.Name, PlaceSubtitle(Place)));
				}
			}
		}
		AddHeading(TEXT("Recent searches"), !Keys.IsEmpty());
		for (int32 Index = 0; Index < Keys.Num(); ++Index)
		{
			AddRow(Keys[Index], Lines[Index].Get<0>(), Lines[Index].Get<1>(), true);
		}
		if (Keys.IsEmpty())
		{
			EmptyText = TEXT("No recent searches yet. Search for a building or place.");
			AddNote(EmptyText);
		}
		return;
	}

	// Something typed: what matches it.
	AddHeading(TEXT("Search results"), false);
	bool bMore = false;
	if (Mode == EMode::Sites)
	{
		for (const FMobileSite& Site : Source->ListSites())
		{
			if (!OverlayMatchesWords(Site.Name + TEXT(" ") + Site.Subtitle, Typed))
			{
				continue;
			}
			if (ShownKeys.Num() >= MaxRows) { bMore = true; break; }
			AddRow(TEXT("site:") + Site.Id, Site.Name, Site.Subtitle, false);
		}
		if (ShownKeys.IsEmpty())
		{
			EmptyText = TEXT("No sites found. Try another name.");
		}
	}
	else
	{
		// The buildings first, then the places.
		for (const FMobileBuilding& Building : Source->ListBuildings(SiteId))
		{
			if (!OverlayMatchesWords(Building.Name + TEXT(" ") + Building.Id, Typed))
			{
				continue;
			}
			if (ShownKeys.Num() >= MaxRows) { bMore = true; break; }
			AddRow(FMobileSearchHistory::BuildingKey(Building.Id), Building.Name, TEXT("Building"), false);
		}
		if (!bMore)
		{
			for (const FMobilePlace& Place : Source->SearchPlaces(SiteId, Typed, FMobilePlaceFilter()))
			{
				if (ShownKeys.Num() >= MaxRows) { bMore = true; break; }
				AddRow(FMobileSearchHistory::PlaceKey(Place.Id), Place.Name, PlaceSubtitle(Place), false);
			}
		}
		if (ShownKeys.IsEmpty())
		{
			EmptyText = TEXT("No matching buildings or places.");
		}
	}
	if (!EmptyText.IsEmpty())
	{
		AddNote(EmptyText);
	}
	else if (bMore)
	{
		AddNote(FString::Printf(TEXT("Showing the first %d. Type more to narrow it down."), MaxRows));
	}
}
