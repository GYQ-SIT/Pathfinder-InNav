#include "MobileRoutePageWidget.h"

#include "MobileIcons.h"
#include "MobileReorderHandle.h"
#include "MobileSelectWidget.h"
#include "MobileStyle.h"
#include "MobileTapTarget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"

using namespace MobileStyle;

namespace
{
	// The red of a destination's badge (the prototype's #dc2626).
	FLinearColor RouteDestinationRed() { return StartStyle::Rgb(220, 38, 38); }

	// Something at the left of a row's text, a small square for a tap.
	USizeBox* RouteSized(UWidgetTree* Tree, UWidget* Content, float Width, float Height)
	{
		USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetWidthOverride(Width);
		Box->SetHeightOverride(Height);
		Box->SetContent(Content);
		return Box;
	}
}

UMobileSelectWidget* UMobileRoutePageWidget::MakeSelect(EMobileSelectLook Look, const TCHAR* Caption, TFunction<void(UMobileRoutePageWidget*, const FString&)> OnChosen)
{
	TWeakObjectPtr<UMobileRoutePageWidget> WeakThis(this);
	UMobileSelectWidget* Select = CreateWidget<UMobileSelectWidget>(this, UMobileSelectWidget::StaticClass());
	Select->SetLook(Look);
	Select->SetPopupHost(PopupRoot);
	Select->SetCaption(Caption);
	Select->SetOnChanged([WeakThis, OnChosen](const FString& Value)
	{
		if (UMobileRoutePageWidget* Self = WeakThis.Get())
		{
			OnChosen(Self, Value);
		}
	});
	TWeakObjectPtr<UMobileSelectWidget> WeakSelect(Select);
	Select->SetOnOpened([WeakThis, WeakSelect]()
	{
		if (UMobileRoutePageWidget* Self = WeakThis.Get())
		{
			Self->CloseOtherSelects(WeakSelect.Get());
		}
	});
	return Select;
}

UWidget* UMobileRoutePageWidget::MakeBadge(const FLinearColor& Fill, UWidget* Face)
{
	UBorder* Circle = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Circle->SetBrush(Rounded(Fill, FLinearColor::Transparent, 12.f, 0.f));
	Circle->SetBrushColor(FLinearColor::White);
	Circle->SetHorizontalAlignment(HAlign_Center);
	Circle->SetVerticalAlignment(VAlign_Center);
	if (Face)
	{
		Circle->SetContent(Face);
	}
	return RouteSized(WidgetTree, Circle, 24.f, 24.f);
}

USizeBox* UMobileRoutePageWidget::AddStopRow(UWidget* Badge, UWidget* Content, UWidget* Actions, bool bLineAbove, bool bLineBelow)
{
	// Left: the badge, with a thin line up to the row above and down to the row below.
	auto LineOf = [this](bool bOn) -> UWidget*
	{
		UBorder* Rule = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Rule->SetBrushColor(bOn ? FieldEdge : FLinearColor::Transparent);
		USizeBox* Thin = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Thin->SetWidthOverride(2.f);
		Thin->SetContent(Rule);
		return Thin;
	};
	UVerticalBox* Left = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	UVerticalBoxSlot* AboveSlot = Left->AddChildToVerticalBox(LineOf(bLineAbove));
	AboveSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AboveSlot->SetHorizontalAlignment(HAlign_Center);
	Left->AddChildToVerticalBox(Badge)->SetHorizontalAlignment(HAlign_Center);
	UVerticalBoxSlot* BelowSlot = Left->AddChildToVerticalBox(LineOf(bLineBelow));
	BelowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	BelowSlot->SetHorizontalAlignment(HAlign_Center);

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Row->AddChildToHorizontalBox(RouteSized(WidgetTree, Left, 24.f, StopRowHeight));
	UHorizontalBoxSlot* ContentSlot = Row->AddChildToHorizontalBox(Content);
	ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ContentSlot->SetPadding(FMargin(12.f, 0.f, 8.f, 0.f));
	ContentSlot->SetVerticalAlignment(VAlign_Center);
	if (Actions)
	{
		Row->AddChildToHorizontalBox(Actions)->SetVerticalAlignment(VAlign_Center);
	}
	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Box->SetMinDesiredHeight(StopRowHeight);
	// A hairline under the row, from the text to the right edge, between one place and the next (the prototype has it; the line between the badges
	// already joins them, this one tells the places apart).
	UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	// (An overlay slot is aligned left and top unless it is told to fill: the row was only as wide as its words, and the cross and the handle
	// followed the words instead of standing at the right edge.)
	UOverlaySlot* RowSlot = Layers->AddChildToOverlay(Row);
	RowSlot->SetHorizontalAlignment(HAlign_Fill);
	RowSlot->SetVerticalAlignment(VAlign_Fill);
	if (bLineBelow)
	{
		USizeBox* Hairline = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Hairline->SetHeightOverride(1.f);
		UBorder* Rule = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StopDivider"));
		Rule->SetBrushColor(Line);
		Hairline->SetContent(Rule);
		UOverlaySlot* RuleSlot = Layers->AddChildToOverlay(Hairline);
		RuleSlot->SetVerticalAlignment(VAlign_Bottom);
		RuleSlot->SetHorizontalAlignment(HAlign_Fill);
		RuleSlot->SetPadding(FMargin(36.f, 0.f, 0.f, 0.f));
	}
	Box->SetContent(Layers);
	StopsBox->AddChildToVerticalBox(Box);
	return Box;
}

UWidget* UMobileRoutePageWidget::MakeSectionHeader(const FString& Title, const FString& Count, bool bOpen, const FString& Payload)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Row->AddChildToHorizontalBox(MakeText(WidgetTree, FString(), Title, 12.f, Ink))->SetVerticalAlignment(VAlign_Center);
	if (!Count.IsEmpty())
	{
		Row->AddChildToHorizontalBox(MakeText(WidgetTree, FString(), Count, 12.f, Grey))->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
	}
	Row->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UWidget* Sign = MobileIcons::Picture(WidgetTree, bOpen ? TEXT("Collapse") : TEXT("Expand"), 16.f, Blue);
	Row->AddChildToHorizontalBox(Sign ? Sign : static_cast<UWidget*>(MakeText(WidgetTree, FString(), bOpen ? TEXT("−") : TEXT("+"), 16.f, Blue, true)))->SetVerticalAlignment(VAlign_Center);
	UBorder* Padded = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Padded->SetBrushColor(FLinearColor::Transparent);
	Padded->SetPadding(FMargin(0.f, 14.f));
	Padded->SetContent(Row);

	UMobileTapTarget* Tap = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
	Tap->Setup(Padded, Payload, FLinearColor::Transparent, 0.f);
	TWeakObjectPtr<UMobileRoutePageWidget> WeakThis(this);
	Tap->SetOnTapped([WeakThis](const FString& Tapped)
	{
		if (UMobileRoutePageWidget* Self = WeakThis.Get())
		{
			if (Tapped == TEXT("options")) Self->SetOptionsOpen(!Self->bOptionsOpen);
			else if (Tapped == TEXT("steps")) Self->SetStepsOpen(!Self->bStepsOpen);
		}
	});
	// A line over each section, as in the prototype.
	UBorder* Rule = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Rule->SetBrushColor(FieldEdge);
	USizeBox* RuleSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	RuleSize->SetHeightOverride(1.f);
	RuleSize->SetContent(Rule);
	UVerticalBox* Header = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Header->AddChildToVerticalBox(RuleSize);
	Header->AddChildToVerticalBox(Tap);
	return Header;
}

TSharedRef<SWidget> UMobileRoutePageWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		// An overlay over the page, which the lists of the drop-downs open in.
		PopupRoot = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RouteRoot"));
		Tree->RootWidget = PopupRoot;
		UVerticalBox* Page = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoutePage"));
		// (An overlay slot is aligned left and top unless it is told to fill.)
		UOverlaySlot* PageSlot = PopupRoot->AddChildToOverlay(Page);
		PageSlot->SetHorizontalAlignment(HAlign_Fill);
		PageSlot->SetVerticalAlignment(VAlign_Fill);

		// "Your route" at the left and, at the right, a close button (a circle with a cross).
		UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RouteHeader"));
		// The title, and under it the travel mode, which is shown while the sheet is minimised (it is then the sheet's title).
		UVerticalBox* Titles = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RouteTitles"));
		Titles->AddChildToVerticalBox(MakeText(Tree, TEXT("Txt_RouteTitle"), TEXT("Your route"), 16.f, Ink, true));
		SubtitleText = MakeText(Tree, TEXT("Txt_RouteSubtitle"), TEXT(""), 12.f, Grey);
		SubtitleText->SetVisibility(ESlateVisibility::Collapsed);
		Titles->AddChildToVerticalBox(SubtitleText)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(Titles);
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Center);
		UButton* Close = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_RouteBack"));
		Close->SetStyle(StartStyle::Button(FLinearColor::Transparent, TapHover, TapHover * 2.f, FLinearColor::Transparent, 22.f / StartStyle::DesignToUi));
		Close->SetBackgroundColor(FLinearColor::White);
		UBorder* CloseCentre = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RouteCloseCentre"));
		CloseCentre->SetBrushColor(FLinearColor::Transparent);
		CloseCentre->SetHorizontalAlignment(HAlign_Center);
		CloseCentre->SetVerticalAlignment(VAlign_Center);
		UWidget* CloseIcon = MobileIcons::Picture(Tree, TEXT("Close"), 24.f, Grey);
		CloseCentre->SetContent(CloseIcon ? CloseIcon : static_cast<UWidget*>(MakeText(Tree, FString(), TEXT("×"), 24.f, Grey, true)));
		Close->SetContent(RouteSized(Tree, CloseCentre, 44.f, 44.f));
		Close->OnClicked.AddUniqueDynamic(this, &UMobileRoutePageWidget::HandleBackClicked);
		Header->AddChildToHorizontalBox(Close)->SetVerticalAlignment(VAlign_Center);
		Page->AddChildToVerticalBox(Header)->SetPadding(FMargin(16.f, 0.f, 8.f, 0.f));

		UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("RouteScroll"));
		UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RouteContent"));
		MobileStyle::NoScrollShadow(Scroll);
		Scroll->AddChild(Content);

		// The stops card: where the walk starts, the destinations, and Add stop. The drop-downs of where it starts (and, for My location, the building
		// and the level) are made once; the rows are made again whenever the request changes.
		FromSelect = MakeSelect(EMobileSelectLook::Bare, TEXT("From"), [](UMobileRoutePageWidget* Self, const FString& Value) { Self->ChooseFrom(Value); });
		BuildingSelect = MakeSelect(EMobileSelectLook::Chip, TEXT("Building"), [](UMobileRoutePageWidget* Self, const FString& Value) { Self->ChooseMyBuilding(Value); });
		LevelSelect = MakeSelect(EMobileSelectLook::Chip, TEXT("Level"), [](UMobileRoutePageWidget* Self, const FString& Value) { Self->ChooseMyLevel(FCString::Atoi(*Value)); });
		MyLocationRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MyLocationRow"));
		MyLocationRow->AddChildToHorizontalBox(BuildingSelect);
		MyLocationRow->AddChildToHorizontalBox(LevelSelect)->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
		StopsBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RouteStops"));
		UBorder* Card = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RouteStopsCard"));
		Card->SetBrush(Rounded(SoftPanel, FLinearColor::Transparent, 20.f, 0.f));
		Card->SetBrushColor(FLinearColor::White);
		Card->SetPadding(FMargin(12.f, 2.f));
		Card->SetContent(StopsBox);
		Content->AddChildToVerticalBox(Card)->SetPadding(FMargin(16.f, 4.f, 16.f, 0.f));

		// The minutes and metres (or why there is no route), and how the person goes.
		JourneyText = MakeText(Tree, TEXT("Txt_RouteJourney"), TEXT(""), 12.f, Grey);
		JourneyText->SetVisibility(ESlateVisibility::Collapsed);
		Content->AddChildToVerticalBox(JourneyText)->SetPadding(FMargin(16.f, 12.f, 16.f, 0.f));
		UHorizontalBox* Stats = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RouteStats"));
		MinutesText = MakeText(Tree, TEXT("Txt_RouteMinutes"), TEXT(""), 20.f, Ink, true);
		MetresText = MakeText(Tree, TEXT("Txt_RouteMetres"), TEXT(""), 20.f, Ink, true);
		Stats->AddChildToHorizontalBox(MinutesText);
		Stats->AddChildToHorizontalBox(MakeText(Tree, FString(), TEXT(" · "), 20.f, Ink, true));
		Stats->AddChildToHorizontalBox(MetresText);
		StatsBox = Stats;
		Content->AddChildToVerticalBox(Stats)->SetPadding(FMargin(16.f, 12.f, 16.f, 0.f));
		ViaText = MakeText(Tree, TEXT("Txt_RouteVia"), TEXT(""), 12.f, Grey);
		Content->AddChildToVerticalBox(ViaText)->SetPadding(FMargin(16.f, 2.f, 16.f, 0.f));
		ErrorText = MakeText(Tree, TEXT("Txt_RouteError"), TEXT(""), 13.f, Red, true);
		ErrorText->SetAutoWrapText(true);
		ErrorText->SetVisibility(ESlateVisibility::Collapsed);
		Content->AddChildToVerticalBox(ErrorText)->SetPadding(FMargin(16.f, 12.f, 16.f, 0.f));

		// Escalator, Lift, Stairs: three pills in a row.
		TransportBox = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RouteTransport"));
		Content->AddChildToVerticalBox(TransportBox)->SetPadding(FMargin(16.f, 12.f, 16.f, 0.f));

		// Route options and Route steps: a line, a title and a sign, and the content when open.
		OptionsSection = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RouteOptionsHeader"));
		Content->AddChildToVerticalBox(OptionsSection)->SetPadding(FMargin(16.f, 12.f, 16.f, 0.f));
		OptionBox = Tree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("RouteOptions"));
		OptionBox->SetInnerSlotPadding(FVector2D(8.f, 8.f));
		OptionBox->SetVisibility(ESlateVisibility::Collapsed);
		Content->AddChildToVerticalBox(OptionBox)->SetPadding(FMargin(16.f, 0.f, 16.f, 12.f));
		StepsSection = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RouteStepsHeader"));
		Content->AddChildToVerticalBox(StepsSection)->SetPadding(FMargin(16.f, 0.f, 16.f, 0.f));
		StepBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RouteSteps"));
		StepBox->SetVisibility(ESlateVisibility::Collapsed);
		Content->AddChildToVerticalBox(StepBox)->SetPadding(FMargin(16.f, 0.f, 16.f, 12.f));
		BodyScroll = Scroll;
		Page->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		// Start navigation, pinned at the bottom under a line.
		UBorder* BarRule = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		BarRule->SetBrushColor(FieldEdge);
		USizeBox* BarLine = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		BarLine->SetHeightOverride(1.f);
		BarLine->SetContent(BarRule);
		Page->AddChildToVerticalBox(BarLine);
		StartButton = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_StartNavigation"));
		StartButton->SetStyle(StartStyle::Button(Blue, Blue * 0.94f, Blue * 0.86f, IconGrey, 8.f / StartStyle::DesignToUi));
		StartButton->SetBackgroundColor(FLinearColor::White);
		UHorizontalBox* StartRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("StartRow"));
		UWidget* StartIcon = MobileIcons::Picture(Tree, TEXT("Navigate"), 18.f, OnBlue);
		if (StartIcon)
		{
			StartRow->AddChildToHorizontalBox(StartIcon)->SetVerticalAlignment(VAlign_Center);
		}
		UHorizontalBoxSlot* StartTextSlot = StartRow->AddChildToHorizontalBox(MakeText(Tree, TEXT("Btn_StartNavigation_Label"), TEXT("Start navigation"), 14.f, OnBlue, true));
		StartTextSlot->SetPadding(FMargin(StartIcon ? 8.f : 0.f, 0.f, 0.f, 0.f));
		StartTextSlot->SetVerticalAlignment(VAlign_Center);
		UBorder* StartCentre = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		StartCentre->SetBrushColor(FLinearColor::Transparent);
		StartCentre->SetHorizontalAlignment(HAlign_Center);
		StartCentre->SetVerticalAlignment(VAlign_Center);
		StartCentre->SetContent(StartRow);
		USizeBox* StartSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		StartSize->SetMinDesiredHeight(44.f);
		StartSize->SetContent(StartCentre);
		if (UButtonSlot* StartContent = Cast<UButtonSlot>(StartButton->SetContent(StartSize)))
		{
			StartContent->SetHorizontalAlignment(HAlign_Fill);
			StartContent->SetVerticalAlignment(VAlign_Fill);
			StartContent->SetPadding(FMargin(0.f));
		}
		StartButton->OnClicked.AddUniqueDynamic(this, &UMobileRoutePageWidget::HandleStartClicked);
		Page->AddChildToVerticalBox(StartButton)->SetPadding(FMargin(16.f, 8.f, 16.f, 12.f));

		Refresh();
	}
	return Super::RebuildWidget();
}

void UMobileRoutePageWidget::SetChoices(const TArray<FMobilePlace>& InPlaces, const TArray<FMobileBuilding>& InBuildings)
{
	Places = InPlaces;
	Buildings = InBuildings;
	Refresh();
}

void UMobileRoutePageWidget::SetRequest(const FMobileRouteRequest& InRequest, const FMobileRoute& InRoute, bool bInPending, const TArray<FMobileRoute>& InLegs)
{
	Request = InRequest;
	Route = InRoute;
	Legs = InLegs;
	bPending = bInPending;
	Refresh();
}

UWidget* UMobileRoutePageWidget::FindNamedWidget(const FName& Name) const
{
	return WidgetTree ? WidgetTree->FindWidget(Name) : nullptr;
}

bool UMobileRoutePageWidget::IsMyLocationRowShown() const
{
	return MyLocationRow && MyLocationRow->GetVisibility() != ESlateVisibility::Collapsed;
}

int32 UMobileRoutePageWidget::GetTotalMinutes() const
{
	if (Legs.Num() < 2)
	{
		return Route.Minutes;
	}
	int32 Total = 0;
	for (const FMobileRoute& Leg : Legs) Total += Leg.Minutes;
	return Total;
}

double UMobileRoutePageWidget::GetTotalMetres() const
{
	if (Legs.Num() < 2)
	{
		return Route.Metres;
	}
	double Total = 0.0;
	for (const FMobileRoute& Leg : Legs) Total += Leg.Metres;
	return Total;
}

FString UMobileRoutePageWidget::GetViaString() const
{
	const FMobileRoutePreferences& Preferences = Request.Preferences;
	const bool bWheelchair = Preferences.bWheelchair && Preferences.bAllowLift && !Preferences.bAllowStairs && !Preferences.bAllowEscalator;
	FString Modes;
	if (bWheelchair)
	{
		Modes = TEXT("Wheelchair accessible");
	}
	else
	{
		TArray<FString> Allowed;
		if (Preferences.bAllowEscalator) Allowed.Add(TEXT("Escalator"));
		if (Preferences.bAllowLift) Allowed.Add(TEXT("Lift"));
		if (Preferences.bAllowStairs) Allowed.Add(TEXT("Stairs"));
		Modes = Allowed.IsEmpty() ? FString(TEXT("no way between levels")) : FString::Join(Allowed, TEXT(", "));
	}
	return (GetDestinationCount() > 1 ? FString(TEXT("First destination · via ")) : FString(TEXT("via "))) + Modes;
}

FString UMobileRoutePageWidget::GetJourneyString() const
{
	if (GetDestinationCount() < 2)
	{
		return FString();
	}
	return FString::Printf(TEXT("%d destinations · %d min · %d m total"), GetDestinationCount(), GetTotalMinutes(), FMath::RoundToInt(GetTotalMetres()));
}

FString UMobileRoutePageWidget::PlaceLine(const FString& PlaceId) const
{
	const FMobilePlace* Place = Places.FindByPredicate([&PlaceId](const FMobilePlace& Candidate) { return Candidate.Id == PlaceId; });
	return Place ? Place->WithCode(Place->GroupLabel()) : FString();
}

TArray<FMobileSelectOption> UMobileRoutePageWidget::PlaceOptions(bool bWithMyLocation, const TArray<FString>& Except) const
{
	TArray<FMobileSelectOption> Options;
	if (bWithMyLocation)
	{
		FMobileSelectOption Mine;
		Mine.Label = Request.bMyLocationUnavailable ? TEXT("Choose a starting point") : TEXT("My location");
		Options.Add(Mine);
	}
	for (const FMobilePlace& Place : Places)
	{
		if (Except.Contains(Place.Id))
		{
			continue;
		}
		FMobileSelectOption Option;
		Option.Value = Place.Id;
		Option.Label = Place.Name + TEXT(" · ") + Place.GroupLabel();
		Option.ShortLabel = Place.Name;
		Options.Add(Option);
	}
	return Options;
}

void UMobileRoutePageWidget::SetMinimized(bool bInMinimized)
{
	bMinimized = bInMinimized;
	if (BodyScroll)
	{
		BodyScroll->SetVisibility(bMinimized ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (bMinimized)
	{
		// A list that is open would float over the map.
		CloseOtherSelects(nullptr);
	}
	if (SubtitleText)
	{
		SubtitleText->SetVisibility(bMinimized ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UMobileRoutePageWidget::RefreshSubtitle()
{
	if (!SubtitleText)
	{
		return;
	}
	const FMobileRoutePreferences& Preferences = Request.Preferences;
	FString Mode;
	if (Preferences.bWheelchair && Preferences.bAllowLift && !Preferences.bAllowStairs && !Preferences.bAllowEscalator)
	{
		Mode = TEXT("Wheelchair accessible");
	}
	else if (Preferences.bAllowLift && Preferences.bAllowStairs && Preferences.bAllowEscalator)
	{
		Mode = TEXT("Any way between levels");
	}
	else
	{
		TArray<FString> Allowed;
		if (Preferences.bAllowEscalator) Allowed.Add(TEXT("escalator"));
		if (Preferences.bAllowLift) Allowed.Add(TEXT("lift"));
		if (Preferences.bAllowStairs) Allowed.Add(TEXT("stairs"));
		Mode = Allowed.IsEmpty() ? FString(TEXT("Same level only")) : FString::Join(Allowed, TEXT(", "));
	}
	SubtitleText->SetText(FText::FromString(TEXT("Travel mode · ") + Mode));
}

void UMobileRoutePageWidget::Refresh()
{
	if (!FromSelect || !StopsBox || !WidgetTree)
	{
		return;
	}
	RefreshStops();
	RefreshSummary();
	RefreshSubtitle();
	RefreshTransport();
	RefreshOptions();
	RefreshSteps();
	RefreshSections();
	StartButton->SetIsEnabled(Route.bFound);
}

void UMobileRoutePageWidget::RefreshStops()
{
	// The lists of the old rows' drop-downs are in the overlay: they go with their rows.
	for (UMobileSelectWidget* Old : DestinationSelects)
	{
		if (Old) Old->ClosePopup();
	}
	if (AddSelect) AddSelect->ClosePopup();
	EndDragGhost();
	StopGhosts.Reset();
	StopsBox->ClearChildren();
	StopRowBoxes.Reset();
	ReorderHandles.Reset();
	DestinationSelects.Reset();
	AddSelect = nullptr;
	// These live on from one build to the next: they leave the old rows before they join the new.
	FromSelect->RemoveFromParent();
	MyLocationRow->RemoveFromParent();

	// Where the walk starts: My location first, then every place; for My location, which building and level the person is on.
	FromSelect->SetOptions(PlaceOptions(true, TArray<FString>()), Request.FromPlaceId);
	MyLocationRow->SetVisibility(Request.FromPlaceId.IsEmpty() && !Request.bMyLocationUnavailable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	TArray<FMobileSelectOption> BuildingOptions, LevelOptions;
	for (const FMobileBuilding& Building : Buildings)
	{
		FMobileSelectOption Option;
		Option.Value = Building.Id;
		Option.Label = Building.Name;
		BuildingOptions.Add(Option);
		if (Building.Id == Request.MyLocationBuildingId)
		{
			for (const FMobileLevel& Level : Building.Levels)
			{
				FMobileSelectOption LevelOption;
				LevelOption.Value = FString::FromInt(Level.Level);
				LevelOption.Label = Level.Label;
				LevelOptions.Add(LevelOption);
			}
		}
	}
	BuildingSelect->SetOptions(BuildingOptions, Request.MyLocationBuildingId);
	LevelSelect->SetOptions(LevelOptions, FString::FromInt(Request.MyLocationLevel));

	TWeakObjectPtr<UMobileRoutePageWidget> WeakThis(this);
	// With no destination yet there is still the first row, with nothing chosen in it (the drop-down of the first destination is always there).
	TArray<FString> Ids = { Request.ToPlaceId };
	Ids.Append(Request.StopPlaceIds);
	const int32 Count = Ids.Num();
	const bool bHasDestination = GetDestinationCount() > 0;

	// The start: a blue badge with the locate icon, and the drop-down.
	{
		UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Content->AddChildToHorizontalBox(FromSelect)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Content->AddChildToHorizontalBox(MyLocationRow)->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		UWidget* Face = MobileIcons::Picture(WidgetTree, TEXT("StartLocation"), 14.f, OnBlue);
		if (!Face)
		{
			UBorder* Dot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Dot->SetBrush(Rounded(OnBlue, FLinearColor::Transparent, 4.f, 0.f));
			Dot->SetBrushColor(FLinearColor::White);
			Face = RouteSized(WidgetTree, Dot, 8.f, 8.f);
		}
		AddStopRow(MakeBadge(Blue, Face), Content, nullptr, false, true);
	}

	// The destinations: a number for each stop on the way and a red pin for the last, the name over its place, and (with more than one) a cross and a handle.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const bool bLast = Index == Count - 1;
		UMobileSelectWidget* Select = MakeSelect(EMobileSelectLook::Bare, TEXT(""), [Index](UMobileRoutePageWidget* Self, const FString& Value) { Self->ChooseDestination(Index, Value); });
		Select->SetOptions(PlaceOptions(false, TArray<FString>()), Ids[Index]);
		DestinationSelects.Add(Select);
		UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Texts->AddChildToVerticalBox(Select);
		const FString WhereLine = PlaceLine(Ids[Index]);
		if (!WhereLine.IsEmpty())
		{
			Texts->AddChildToVerticalBox(MakeText(WidgetTree, FString(), WhereLine, 11.f, Grey));
		}

		UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		if (Count > 1)
		{
			UWidget* Cross = MobileIcons::Picture(WidgetTree, TEXT("Remove"), 20.f, Grey);
			UBorder* CrossCentre = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			CrossCentre->SetBrushColor(FLinearColor::Transparent);
			CrossCentre->SetHorizontalAlignment(HAlign_Center);
			CrossCentre->SetVerticalAlignment(VAlign_Center);
			CrossCentre->SetContent(Cross ? Cross : static_cast<UWidget*>(MakeText(WidgetTree, FString(), TEXT("×"), 20.f, Grey, true)));
			UMobileTapTarget* Remove = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
			Remove->Setup(RouteSized(WidgetTree, CrossCentre, 28.f, 40.f), FString::FromInt(Index), FLinearColor::Transparent, 8.f);
			Remove->SetOnTapped([WeakThis](const FString& Tapped)
			{
				if (UMobileRoutePageWidget* Self = WeakThis.Get())
				{
					Self->RemoveDestination(FCString::Atoi(*Tapped));
				}
			});
			Actions->AddChildToHorizontalBox(Remove);
		}

		UWidget* Badge = nullptr;
		if (bLast)
		{
			UWidget* Pin = MobileIcons::Picture(WidgetTree, TEXT("Destination"), 14.f, OnBlue);
			Badge = MakeBadge(RouteDestinationRed(), Pin ? Pin : static_cast<UWidget*>(MakeText(WidgetTree, FString(), FString::FromInt(Index + 1), 12.f, OnBlue, true)));
		}
		else
		{
			Badge = MakeBadge(Blue, MakeText(WidgetTree, FString(), FString::FromInt(Index + 1), 12.f, OnBlue, true));
		}
		USizeBox* RowBox = AddStopRow(Badge, Texts, Actions, true, true);
		StopRowBoxes.Add(RowBox);
		FStopGhost Ghost;
		Ghost.Name = Select->GetSelectedShortLabel();
		Ghost.Where = WhereLine;
		Ghost.BadgeFill = bLast ? RouteDestinationRed() : Blue;
		Ghost.BadgeNumber = FString::FromInt(Index + 1);
		Ghost.bPin = bLast;
		Ghost.bRemovable = Count > 1;
		Ghost.bHandle = Count > 1;
		StopGhosts.Add(Ghost);

		UMobileReorderHandle* Handle = nullptr;
		if (Count > 1)
		{
			Handle = CreateWidget<UMobileReorderHandle>(this, UMobileReorderHandle::StaticClass());
			TWeakObjectPtr<USizeBox> WeakRow(RowBox);
			Handle->SetOnDragged([WeakThis, Index](float DeltaY)
			{
				// The row is a card that follows the pointer while it is dragged.
				if (UMobileRoutePageWidget* Self = WeakThis.Get())
				{
					if (!Self->IsDragGhostShown())
					{
						Self->BeginDragGhost(Index);
					}
					Self->MoveDragGhost(DeltaY);
				}
			});
			Handle->SetOnDropped([WeakThis, Index, Count](float DeltaY)
			{
				if (UMobileRoutePageWidget* Self = WeakThis.Get())
				{
					Self->EndDragGhost();
					Self->MoveDestination(Index, FMath::Clamp(Index + FMath::RoundToInt(DeltaY / StopRowHeight), 0, Count - 1));
				}
			});
			Actions->AddChildToHorizontalBox(Handle);
		}
		ReorderHandles.Add(Handle);
	}

	// Add stop: a plus badge and the words; tapped, a drop-down of the places that are not in the trip.
	if (bHasDestination)
	{
		UWidget* Plus = MobileIcons::Picture(WidgetTree, TEXT("AddStop"), 14.f, OnBlue);
		UWidget* PlusFace = Plus ? Plus : static_cast<UWidget*>(MakeText(WidgetTree, FString(), TEXT("+"), 16.f, OnBlue, true));
		if (bAddingStop)
		{
			AddSelect = MakeSelect(EMobileSelectLook::Bare, TEXT(""), [](UMobileRoutePageWidget* Self, const FString& Value) { Self->AddStop(Value); });
			TArray<FMobileSelectOption> Options = { { FString(), TEXT("Choose a destination"), FString() } };
			Options.Append(PlaceOptions(false, Ids));
			AddSelect->SetOptions(Options, FString());
			AddSelect->SetOpen(true);
			UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			Content->AddChildToHorizontalBox(AddSelect)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			UWidget* Cross = MobileIcons::Picture(WidgetTree, TEXT("Remove"), 20.f, Grey);
			UBorder* CrossCentre = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			CrossCentre->SetBrushColor(FLinearColor::Transparent);
			CrossCentre->SetHorizontalAlignment(HAlign_Center);
			CrossCentre->SetVerticalAlignment(VAlign_Center);
			CrossCentre->SetContent(Cross ? Cross : static_cast<UWidget*>(MakeText(WidgetTree, FString(), TEXT("×"), 20.f, Grey, true)));
			UMobileTapTarget* Cancel = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
			Cancel->Setup(RouteSized(WidgetTree, CrossCentre, 28.f, 40.f), TEXT("cancel"), FLinearColor::Transparent, 8.f);
			Cancel->SetOnTapped([WeakThis](const FString&)
			{
				if (UMobileRoutePageWidget* Self = WeakThis.Get())
				{
					Self->CancelAddStop();
				}
			});
			AddStopRow(MakeBadge(Blue, PlusFace), Content, Cancel, true, false);
		}
		else
		{
			UMobileTapTarget* Add = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
			UBorder* Words = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Words->SetBrushColor(FLinearColor::Transparent);
			Words->SetPadding(FMargin(0.f, 14.f));
			Words->SetContent(MakeText(WidgetTree, TEXT("Btn_AddStop_Label"), TEXT("Add stop"), 16.f, Blue));
			Add->Setup(Words, TEXT("add"), FLinearColor::Transparent, 8.f);
			Add->SetOnTapped([WeakThis](const FString&)
			{
				if (UMobileRoutePageWidget* Self = WeakThis.Get())
				{
					Self->BeginAddStop();
				}
			});
			AddStopRow(MakeBadge(Blue, PlusFace), Add, nullptr, true, false);
		}
	}
}

void UMobileRoutePageWidget::RefreshSummary()
{
	const bool bFound = Route.bFound;
	StatsBox->SetVisibility(bFound ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	ViaText->SetVisibility(bFound ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	ViaText->SetText(FText::FromString(GetViaString()));
	ErrorText->SetVisibility(bFound ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	ErrorText->SetColorAndOpacity(FSlateColor(bPending ? Grey : Red));
	ErrorText->SetText(FText::FromString(bPending ? TEXT("Finding route…") : Route.Error.IsEmpty() ? TEXT("No route.") : Route.Error));
	MinutesText->SetText(FText::FromString(FString::Printf(TEXT("%d min"), GetTotalMinutes())));
	MetresText->SetText(FText::FromString(FString::Printf(TEXT("%d m"), FMath::RoundToInt(GetTotalMetres()))));
	const FString Journey = GetJourneyString();
	JourneyText->SetText(FText::FromString(Journey));
	JourneyText->SetVisibility(bFound && !Journey.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UMobileRoutePageWidget::RefreshTransport()
{
	TransportBox->ClearChildren();
	const FMobileRoutePreferences& Preferences = Request.Preferences;
	auto AddPill = [this](const FString& Payload, const FString& Label, bool bOn, bool bFirst)
	{
		UBorder* Pill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Pill->SetBrush(Rounded(bOn ? BlueSoft : White, bOn ? Blue : FieldEdge, 22.f, 1.f));
		Pill->SetBrushColor(FLinearColor::White);
		Pill->SetHorizontalAlignment(HAlign_Center);
		Pill->SetVerticalAlignment(VAlign_Center);
		Pill->SetContent(MakeText(WidgetTree, FString(), Label, 12.f, bOn ? Blue : Ink, true));
		USizeBox* Tall = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Tall->SetHeightOverride(44.f);
		Tall->SetContent(Pill);
		UMobileTapTarget* Chip = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
		Chip->Setup(Tall, Payload, FLinearColor::Transparent, 22.f);
		TWeakObjectPtr<UMobileRoutePageWidget> WeakThis(this);
		Chip->SetOnTapped([WeakThis](const FString& Tapped)
		{
			if (UMobileRoutePageWidget* Self = WeakThis.Get())
			{
				Self->TapOption(Tapped);
			}
		});
		UHorizontalBoxSlot* Slot = TransportBox->AddChildToHorizontalBox(Chip);
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Slot->SetPadding(FMargin(bFirst ? 0.f : 8.f, 0.f, 0.f, 0.f));
	};
	AddPill(TEXT("escalator"), TEXT("Escalator"), Preferences.bAllowEscalator, true);
	AddPill(TEXT("lift"), TEXT("Lift"), Preferences.bAllowLift, false);
	AddPill(TEXT("stairs"), TEXT("Stairs"), Preferences.bAllowStairs, false);
}

void UMobileRoutePageWidget::RefreshOptions()
{
	OptionBox->ClearChildren();
	const FMobileRoutePreferences& Preferences = Request.Preferences;
	// On only when the person chose it (the app starts with the lift, which is the same ways but not this choice): plain, like the chips that are not
	// chosen, until then, and like a chosen chip after.
	const bool bOn = Preferences.bWheelchair && Preferences.bAllowLift && !Preferences.bAllowStairs && !Preferences.bAllowEscalator;
	UMobileTapTarget* Chip = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UWidget* Chair = MobileIcons::Picture(WidgetTree, TEXT("Wheelchair"), 18.f, bOn ? Blue : Ink);
	if (Chair)
	{
		Row->AddChildToHorizontalBox(Chair)->SetVerticalAlignment(VAlign_Center);
	}
	UHorizontalBoxSlot* Words = Row->AddChildToHorizontalBox(MakeText(WidgetTree, FString(), TEXT("Wheelchair accessible"), 13.f, bOn ? Blue : Ink, true));
	Words->SetPadding(FMargin(Chair ? 8.f : 0.f, 0.f, 0.f, 0.f));
	Words->SetVerticalAlignment(VAlign_Center);
	UBorder* Pill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Pill->SetBrush(Rounded(bOn ? BlueSoft : White, bOn ? Blue : FieldEdge, 18.f, 1.f));
	Pill->SetBrushColor(FLinearColor::White);
	Pill->SetPadding(FMargin(14.f, 8.f));
	Pill->SetContent(Row);
	Chip->Setup(Pill, TEXT("liftonly"), FLinearColor::Transparent, 18.f);
	TWeakObjectPtr<UMobileRoutePageWidget> WeakThis(this);
	Chip->SetOnTapped([WeakThis](const FString& Tapped)
	{
		if (UMobileRoutePageWidget* Self = WeakThis.Get())
		{
			Self->TapOption(Tapped);
		}
	});
	OptionBox->AddChildToWrapBox(Chip);
}

void UMobileRoutePageWidget::RefreshSteps()
{
	StepBox->ClearChildren();
	ShownSteps = 0;
	if (!Route.bFound)
	{
		return;
	}
	for (const FMobileStep& Step : Route.Steps)
	{
		++ShownSteps;
		UBorder* Badge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Badge->SetBrush(Rounded(BlueSoft, FLinearColor::Transparent, 12.f, 0.f));
		Badge->SetBrushColor(FLinearColor::White);
		Badge->SetHorizontalAlignment(HAlign_Center);
		Badge->SetVerticalAlignment(VAlign_Center);
		Badge->SetContent(MakeText(WidgetTree, FString(), FString::FromInt(ShownSteps), 12.f, Blue, true));
		USizeBox* BadgeSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		BadgeSize->SetWidthOverride(24.f);
		BadgeSize->SetHeightOverride(24.f);
		BadgeSize->SetContent(Badge);

		UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		UTextBlock* Text = MakeText(WidgetTree, FString(), Step.Text, 14.f, Ink, true);
		Text->SetAutoWrapText(true);
		Texts->AddChildToVerticalBox(Text);
		Texts->AddChildToVerticalBox(MakeText(WidgetTree, FString(), Step.WhereText(), 11.f, Grey))->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Row->AddChildToHorizontalBox(BadgeSize)->SetVerticalAlignment(VAlign_Top);
		UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Texts);
		TextSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		StepBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 8.f, 0.f, 8.f));
	}
}

void UMobileRoutePageWidget::RefreshSections()
{
	OptionsSection->ClearChildren();
	OptionsSection->AddChildToVerticalBox(MakeSectionHeader(TEXT("Route options"), FString(), bOptionsOpen, TEXT("options")));
	OptionBox->SetVisibility(bOptionsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	StepsSection->ClearChildren();
	StepsSection->AddChildToVerticalBox(MakeSectionHeader(TEXT("Route steps"), ShownSteps > 0 ? FString::FromInt(ShownSteps) : FString(), bStepsOpen, TEXT("steps")));
	StepBox->SetVisibility(bStepsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UMobileRoutePageWidget::SetOptionsOpen(bool bOpen)
{
	bOptionsOpen = bOpen;
	RefreshSections();
}

void UMobileRoutePageWidget::SetStepsOpen(bool bOpen)
{
	bStepsOpen = bOpen;
	RefreshSections();
}

bool UMobileRoutePageWidget::HasOpenSelect() const
{
	TArray<UMobileSelectWidget*> All = { FromSelect, BuildingSelect, LevelSelect, AddSelect };
	All.Append(DestinationSelects);
	return All.ContainsByPredicate([](const UMobileSelectWidget* Select) { return Select && Select->IsOpen(); });
}

void UMobileRoutePageWidget::BeginDragGhost(int32 Index)
{
	if (!PopupRoot || !StopGhosts.IsValidIndex(Index) || !StopRowBoxes.IsValidIndex(Index) || DragGhost)
	{
		return;
	}
	const FStopGhost& Spec = StopGhosts[Index];
	// The card: the badge, the name and its place, the cross and the handle, as the row has them, on white.
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UWidget* Face = Spec.bPin ? MobileIcons::Picture(WidgetTree, TEXT("Destination"), 14.f, OnBlue) : nullptr;
	Row->AddChildToHorizontalBox(MakeBadge(Spec.BadgeFill, Face ? Face : static_cast<UWidget*>(MakeText(WidgetTree, FString(), Spec.BadgeNumber, 12.f, OnBlue, true))))->SetVerticalAlignment(VAlign_Center);
	UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	UTextBlock* Name = MakeText(WidgetTree, FString(), Spec.Name, 15.f, Ink);
	Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	Name->SetClipping(EWidgetClipping::ClipToBounds);
	Texts->AddChildToVerticalBox(Name);
	if (!Spec.Where.IsEmpty())
	{
		Texts->AddChildToVerticalBox(MakeText(WidgetTree, FString(), Spec.Where, 11.f, Grey));
	}
	UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Texts);
	TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TextSlot->SetPadding(FMargin(12.f, 0.f, 8.f, 0.f));
	TextSlot->SetVerticalAlignment(VAlign_Center);
	// The cross and the handle as the row has them: the icon in the middle of a box 28 wide and 40 tall.
	auto Action = [this](const TCHAR* IconName, const TCHAR* Glyph) -> UWidget*
	{
		UWidget* Icon = MobileIcons::Picture(WidgetTree, IconName, 20.f, Grey);
		UBorder* Centre = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Centre->SetBrushColor(FLinearColor::Transparent);
		Centre->SetHorizontalAlignment(HAlign_Center);
		Centre->SetVerticalAlignment(VAlign_Center);
		Centre->SetContent(Icon ? Icon : static_cast<UWidget*>(MakeText(WidgetTree, FString(), Glyph, 20.f, Grey, true)));
		return RouteSized(WidgetTree, Centre, 28.f, 40.f);
	};
	if (Spec.bRemovable)
	{
		Row->AddChildToHorizontalBox(Action(TEXT("Remove"), TEXT("×")))->SetVerticalAlignment(VAlign_Center);
	}
	if (Spec.bHandle)
	{
		Row->AddChildToHorizontalBox(Action(TEXT("Reorder"), TEXT("≡")))->SetVerticalAlignment(VAlign_Center);
	}
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DragCard"));
	Card->SetBrush(Rounded(White, FLinearColor::Transparent, 14.f, 0.f));
	Card->SetBrushColor(FLinearColor::White);
	Card->SetPadding(FMargin(12.f, 0.f, 4.f, 0.f));
	Card->SetVerticalAlignment(VAlign_Center);
	Card->SetContent(Row);
	// A shadow under it: two darker rounded layers, a little lower, that show as a soft edge at the bottom.
	UBorder* ShadowNear = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	ShadowNear->SetBrush(Rounded(FLinearColor(0.f, 0.f, 0.f, 0.14f), FLinearColor::Transparent, 14.f, 0.f));
	ShadowNear->SetBrushColor(FLinearColor::White);
	ShadowNear->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
	ShadowNear->SetContent(Card);
	UBorder* ShadowFar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	ShadowFar->SetBrush(Rounded(FLinearColor(0.f, 0.f, 0.f, 0.07f), FLinearColor::Transparent, 16.f, 0.f));
	ShadowFar->SetBrushColor(FLinearColor::White);
	ShadowFar->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	ShadowFar->SetContent(ShadowNear);
	USizeBox* Sized = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	const FGeometry RowGeometry = StopRowBoxes[Index]->GetCachedGeometry();
	const FGeometry HostGeometry = PopupRoot->GetCachedGeometry();
	const FVector2D TopLeft = HostGeometry.AbsoluteToLocal(RowGeometry.GetAbsolutePositionAtCoordinates(FVector2D::ZeroVector));
	Sized->SetWidthOverride(static_cast<float>(RowGeometry.GetLocalSize().X) > 1.f ? static_cast<float>(RowGeometry.GetLocalSize().X) : 320.f);
	Sized->SetHeightOverride(StopRowHeight + 7.f);
	Sized->SetContent(ShadowFar);
	DragGhostSlot = PopupRoot->AddChildToOverlay(Sized);
	DragGhostSlot->SetHorizontalAlignment(HAlign_Left);
	DragGhostSlot->SetVerticalAlignment(VAlign_Top);
	DragGhostTop = static_cast<float>(TopLeft.Y);
	DragGhostSlot->SetPadding(FMargin(static_cast<float>(TopLeft.X), DragGhostTop, 0.f, 0.f));
	DragGhost = Sized;
	DragGhostIndex = Index;
	// The row it leaves is faint, so that it is seen where it was.
	StopRowBoxes[Index]->SetRenderOpacity(0.25f);
	StopRowBoxes[Index]->SetRenderTranslation(FVector2D::ZeroVector);
}

void UMobileRoutePageWidget::MoveDragGhost(float DeltaY)
{
	if (DragGhost && DragGhostSlot)
	{
		const FMargin Now = DragGhostSlot->GetPadding();
		DragGhostSlot->SetPadding(FMargin(Now.Left, DragGhostTop + DeltaY, 0.f, 0.f));
	}
}

void UMobileRoutePageWidget::EndDragGhost()
{
	if (DragGhost && PopupRoot)
	{
		PopupRoot->RemoveChild(DragGhost);
	}
	if (StopRowBoxes.IsValidIndex(DragGhostIndex) && StopRowBoxes[DragGhostIndex])
	{
		StopRowBoxes[DragGhostIndex]->SetRenderOpacity(1.f);
	}
	DragGhost = nullptr;
	DragGhostSlot = nullptr;
	DragGhostIndex = INDEX_NONE;
}

void UMobileRoutePageWidget::CloseOtherSelects(const UMobileSelectWidget* Keep)
{
	TArray<UMobileSelectWidget*> All = { FromSelect, BuildingSelect, LevelSelect, AddSelect };
	All.Append(DestinationSelects);
	for (UMobileSelectWidget* Select : All)
	{
		if (Select && Select != Keep && Select->IsOpen())
		{
			Select->SetOpen(false);
		}
	}
}

void UMobileRoutePageWidget::Changed()
{
	Refresh();
	if (OnRequestChanged)
	{
		OnRequestChanged(Request);
	}
}

void UMobileRoutePageWidget::TapOption(const FString& Option)
{
	FMobileRoutePreferences& Preferences = Request.Preferences;
	if (Option == TEXT("lift"))
	{
		Preferences.bAllowLift = !Preferences.bAllowLift;
		Preferences.bWheelchair = false;
	}
	else if (Option == TEXT("stairs"))
	{
		Preferences.bAllowStairs = !Preferences.bAllowStairs;
		Preferences.bWheelchair = false;
	}
	else if (Option == TEXT("escalator"))
	{
		Preferences.bAllowEscalator = !Preferences.bAllowEscalator;
		Preferences.bWheelchair = false;
	}
	else if (Option == TEXT("liftonly"))
	{
		Preferences = FMobileRoutePreferences::LiftOnly();
		Preferences.bWheelchair = true;
	}
	else
	{
		return;
	}
	Changed();
}

void UMobileRoutePageWidget::ChooseFrom(const FString& PlaceId)
{
	Request.FromPlaceId = PlaceId;
	Changed();
}

void UMobileRoutePageWidget::ChooseTo(const FString& PlaceId)
{
	Request.ToPlaceId = PlaceId;
	Changed();
}

void UMobileRoutePageWidget::BeginAddStop()
{
	bAddingStop = true;
	Refresh();
}

void UMobileRoutePageWidget::CancelAddStop()
{
	bAddingStop = false;
	Refresh();
}

void UMobileRoutePageWidget::AddStop(const FString& PlaceId)
{
	bAddingStop = false;
	if (PlaceId.IsEmpty() || Request.ToPlaceId.IsEmpty() || Request.ToPlaceId == PlaceId || Request.StopPlaceIds.Contains(PlaceId))
	{
		Refresh();
		return;
	}
	Request.StopPlaceIds.Add(PlaceId);
	Changed();
}

void UMobileRoutePageWidget::RemoveDestination(int32 Index)
{
	// Never the last one: a route has a destination.
	if (GetDestinationCount() < 2 || Index < 0 || Index >= GetDestinationCount())
	{
		return;
	}
	TArray<FString> Ids = { Request.ToPlaceId };
	Ids.Append(Request.StopPlaceIds);
	Ids.RemoveAt(Index);
	Request.ToPlaceId = Ids[0];
	Request.StopPlaceIds = TArray<FString>(Ids.GetData() + 1, Ids.Num() - 1);
	Changed();
}

void UMobileRoutePageWidget::MoveDestination(int32 From, int32 To)
{
	const int32 Count = GetDestinationCount();
	if (From < 0 || To < 0 || From >= Count || To >= Count || From == To)
	{
		return;
	}
	TArray<FString> Ids = { Request.ToPlaceId };
	Ids.Append(Request.StopPlaceIds);
	const FString Moved = Ids[From];
	Ids.RemoveAt(From);
	Ids.Insert(Moved, To);
	Request.ToPlaceId = Ids[0];
	Request.StopPlaceIds = TArray<FString>(Ids.GetData() + 1, Ids.Num() - 1);
	Changed();
}

void UMobileRoutePageWidget::ChooseDestination(int32 Index, const FString& PlaceId)
{
	if (PlaceId.IsEmpty() || Index < 0 || Index >= GetDestinationCount())
	{
		return;
	}
	if (Index == 0)
	{
		Request.ToPlaceId = PlaceId;
	}
	else
	{
		Request.StopPlaceIds[Index - 1] = PlaceId;
	}
	Changed();
}

void UMobileRoutePageWidget::ChooseMyBuilding(const FString& BuildingId)
{
	Request.MyLocationBuildingId = BuildingId;
	// The level stays if the new building has it, else it is the new building's first.
	const FMobileBuilding* Building = Buildings.FindByPredicate([&BuildingId](const FMobileBuilding& B) { return B.Id == BuildingId; });
	if (Building && !Building->Levels.ContainsByPredicate([this](const FMobileLevel& L) { return L.Level == Request.MyLocationLevel; }))
	{
		Request.MyLocationLevel = Building->Levels.IsEmpty() ? 1 : Building->Levels[0].Level;
	}
	Changed();
}

void UMobileRoutePageWidget::ChooseMyLevel(int32 Level)
{
	Request.MyLocationLevel = Level;
	Changed();
}

void UMobileRoutePageWidget::HandleBackClicked()
{
	if (OnBack)
	{
		OnBack();
	}
}

void UMobileRoutePageWidget::HandleStartClicked()
{
	if (OnStart)
	{
		OnStart();
	}
}

FString UMobileRoutePageWidget::GetMinutesString() const { return MinutesText ? MinutesText->GetText().ToString() : FString(); }
FString UMobileRoutePageWidget::GetMetresString() const { return MetresText ? MetresText->GetText().ToString() : FString(); }
FString UMobileRoutePageWidget::GetErrorString() const { return ErrorText ? ErrorText->GetText().ToString() : FString(); }
