#include "MobileRoutePageWidget.h"

#include "MobileSelectWidget.h"
#include "MobileStyle.h"
#include "MobileTapTarget.h"

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
#include "Components/WrapBox.h"

using namespace MobileStyle;

TSharedRef<SWidget> UMobileRoutePageWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		UVerticalBox* Page = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoutePage"));
		Tree->RootWidget = Page;

		UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RouteHeader"));
		UTextBlock* BackLabel = nullptr;
		UButton* Back = MakeButton(Tree, TEXT("Btn_RouteBack"), TEXT("‹"), White, Blue, 10.f, 36.f, &BackLabel, 14.f);
		ApplyFont(BackLabel, 28.f, true);
		Back->OnClicked.AddUniqueDynamic(this, &UMobileRoutePageWidget::HandleBackClicked);
		Header->AddChildToHorizontalBox(Back)->SetVerticalAlignment(VAlign_Center);
		Header->AddChildToHorizontalBox(Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("RouteHeaderSpacer")))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Header->AddChildToHorizontalBox(MakeText(Tree, TEXT("Txt_RouteTitle"), TEXT("Your route"), 22.f, Ink, true))->SetVerticalAlignment(VAlign_Center);
		Page->AddChildToVerticalBox(Header)->SetPadding(FMargin(16.f, 0.f, 16.f, 0.f));

		UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("RouteScroll"));
		UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RouteContent"));
		Scroll->AddChild(Content);

		// The drop-downs: From, then (for My location) the building and level, then To.
		TWeakObjectPtr<UMobileRoutePageWidget> WeakThis(this);
		auto MakeSelect = [this, WeakThis](const TCHAR* Caption, TFunction<void(UMobileRoutePageWidget*, const FString&)> OnChosen) -> UMobileSelectWidget*
		{
			UMobileSelectWidget* Select = CreateWidget<UMobileSelectWidget>(this, UMobileSelectWidget::StaticClass());
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
		};
		FromSelect = MakeSelect(TEXT("From"), [](UMobileRoutePageWidget* Self, const FString& Value) { Self->ChooseFrom(Value); });
		Content->AddChildToVerticalBox(FromSelect)->SetPadding(FMargin(16.f, 10.f, 16.f, 0.f));

		MyLocationRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MyLocationRow"));
		BuildingSelect = MakeSelect(TEXT("Building"), [](UMobileRoutePageWidget* Self, const FString& Value) { Self->ChooseMyBuilding(Value); });
		LevelSelect = MakeSelect(TEXT("Level"), [](UMobileRoutePageWidget* Self, const FString& Value) { Self->ChooseMyLevel(FCString::Atoi(*Value)); });
		MyLocationRow->AddChildToHorizontalBox(BuildingSelect)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UHorizontalBoxSlot* LevelSlot = MyLocationRow->AddChildToHorizontalBox(LevelSelect);
		LevelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LevelSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		Content->AddChildToVerticalBox(MyLocationRow)->SetPadding(FMargin(16.f, 8.f, 16.f, 0.f));

		ToSelect = MakeSelect(TEXT("To"), [](UMobileRoutePageWidget* Self, const FString& Value) { Self->ChooseTo(Value); });
		Content->AddChildToVerticalBox(ToSelect)->SetPadding(FMargin(16.f, 8.f, 16.f, 0.f));

		// Minutes and metres, or why there is no route.
		UHorizontalBox* Stats = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RouteStats"));
		auto AddStat = [&](const TCHAR* ValueName, UTextBlock*& OutValue, const TCHAR* Caption)
		{
			UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			OutValue = MakeText(Tree, ValueName, TEXT(""), 24.f, Ink, true);
			Column->AddChildToVerticalBox(OutValue);
			Column->AddChildToVerticalBox(MakeText(Tree, FString(), Caption, 11.f, Grey));
			Stats->AddChildToHorizontalBox(Column)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		};
		AddStat(TEXT("Txt_RouteMinutes"), MinutesText, TEXT("Estimated walk"));
		AddStat(TEXT("Txt_RouteMetres"), MetresText, TEXT("Route distance"));
		StatsBox = Stats;
		Content->AddChildToVerticalBox(Stats)->SetPadding(FMargin(16.f, 16.f, 16.f, 0.f));
		ErrorText = MakeText(Tree, TEXT("Txt_RouteError"), TEXT(""), 13.f, Red, true);
		ErrorText->SetAutoWrapText(true);
		ErrorText->SetVisibility(ESlateVisibility::Collapsed);
		Content->AddChildToVerticalBox(ErrorText)->SetPadding(FMargin(16.f, 16.f, 16.f, 0.f));

		// The ways between levels.
		Content->AddChildToVerticalBox(MakeText(Tree, TEXT("Txt_RouteOptionsTitle"), TEXT("Ways between levels"), 12.f, Grey, true))->SetPadding(FMargin(16.f, 16.f, 16.f, 0.f));
		OptionBox = Tree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("RouteOptions"));
		OptionBox->SetInnerSlotPadding(FVector2D(8.f, 8.f));
		Content->AddChildToVerticalBox(OptionBox)->SetPadding(FMargin(16.f, 8.f, 16.f, 0.f));

		// The steps.
		Content->AddChildToVerticalBox(MakeText(Tree, TEXT("Txt_RouteStepsTitle"), TEXT("Steps"), 12.f, Grey, true))->SetPadding(FMargin(16.f, 16.f, 16.f, 0.f));
		StepBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RouteSteps"));
		Content->AddChildToVerticalBox(StepBox)->SetPadding(FMargin(16.f, 4.f, 16.f, 12.f));
		Page->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		StartButton = MakeButton(Tree, TEXT("Btn_StartNavigation"), TEXT("Start navigation"), Blue, White, 12.f, 48.f);
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

void UMobileRoutePageWidget::SetRequest(const FMobileRouteRequest& InRequest, const FMobileRoute& InRoute)
{
	Request = InRequest;
	Route = InRoute;
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

void UMobileRoutePageWidget::Refresh()
{
	if (!FromSelect || !WidgetTree)
	{
		return;
	}
	RefreshSelects();
	const bool bFound = Route.bFound;
	StatsBox->SetVisibility(bFound ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	ErrorText->SetVisibility(bFound ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	ErrorText->SetText(FText::FromString(Route.Error.IsEmpty() ? TEXT("No route.") : Route.Error));
	MinutesText->SetText(FText::FromString(FString::Printf(TEXT("%d min"), Route.Minutes)));
	MetresText->SetText(FText::FromString(FString::Printf(TEXT("%d m"), FMath::RoundToInt(Route.Metres))));
	StartButton->SetIsEnabled(bFound);
	RefreshOptions();
	RefreshSteps();
}

void UMobileRoutePageWidget::RefreshSelects()
{
	// From: My location first, then every place. To: every place.
	TArray<FMobileSelectOption> FromOptions, ToOptions;
	FMobileSelectOption Mine;
	Mine.Label = TEXT("My location");
	FromOptions.Add(Mine);
	for (const FMobilePlace& Place : Places)
	{
		FMobileSelectOption Option;
		Option.Value = Place.Id;
		Option.Label = Place.Name + TEXT(" · ") + Place.GroupLabel();
		FromOptions.Add(Option);
		ToOptions.Add(Option);
	}
	FromSelect->SetOptions(FromOptions, Request.FromPlaceId);
	ToSelect->SetOptions(ToOptions, Request.ToPlaceId);

	// For My location: which building and level the person is on.
	MyLocationRow->SetVisibility(Request.FromPlaceId.IsEmpty() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
}

void UMobileRoutePageWidget::RefreshOptions()
{
	OptionBox->ClearChildren();
	auto AddChip = [this](const FString& Payload, const FString& Label, bool bOn)
	{
		UMobileTapTarget* Chip = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
		UBorder* Pill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Pill->SetBrushColor(FLinearColor::Transparent);
		Pill->SetPadding(FMargin(14.f, 8.f));
		Pill->SetContent(MakeText(WidgetTree, FString(), Label, 13.f, bOn ? White : Blue, true));
		Chip->Setup(Pill, Payload, bOn ? Blue : BlueSoft, 16.f);
		TWeakObjectPtr<UMobileRoutePageWidget> WeakThis(this);
		Chip->SetOnTapped([WeakThis](const FString& Tapped)
		{
			if (UMobileRoutePageWidget* Self = WeakThis.Get())
			{
				Self->TapOption(Tapped);
			}
		});
		OptionBox->AddChildToWrapBox(Chip);
	};
	const FMobileRoutePreferences& Preferences = Request.Preferences;
	AddChip(TEXT("escalator"), TEXT("Escalator"), Preferences.bAllowEscalator);
	AddChip(TEXT("lift"), TEXT("Lift"), Preferences.bAllowLift);
	AddChip(TEXT("stairs"), TEXT("Stairs"), Preferences.bAllowStairs);
	const bool bLiftOnly = Preferences.bAllowLift && !Preferences.bAllowStairs && !Preferences.bAllowEscalator;
	AddChip(TEXT("liftonly"), TEXT("Use lift only"), bLiftOnly);
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
		Texts->AddChildToVerticalBox(MakeText(WidgetTree, FString(), Step.BuildingId + TEXT(" · ") + Step.LevelLabel, 11.f, Grey))->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Row->AddChildToHorizontalBox(BadgeSize)->SetVerticalAlignment(VAlign_Top);
		UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Texts);
		TextSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		StepBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 8.f, 0.f, 8.f));
	}
}

void UMobileRoutePageWidget::CloseOtherSelects(const UMobileSelectWidget* Keep)
{
	for (UMobileSelectWidget* Select : { FromSelect, ToSelect, BuildingSelect, LevelSelect })
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
	}
	else if (Option == TEXT("stairs"))
	{
		Preferences.bAllowStairs = !Preferences.bAllowStairs;
	}
	else if (Option == TEXT("escalator"))
	{
		Preferences.bAllowEscalator = !Preferences.bAllowEscalator;
	}
	else if (Option == TEXT("liftonly"))
	{
		Preferences = FMobileRoutePreferences::LiftOnly();
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
