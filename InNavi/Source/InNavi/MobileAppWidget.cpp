#include "MobileAppWidget.h"

#include "MobilePlacePageWidget.h"
#include "MobileSearchPageWidget.h"
#include "MobileStyle.h"
#include "StartScreenStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Framework/Application/SlateApplication.h"

using namespace MobileStyle;

namespace
{
	UCanvasPanelSlot* CanvasSlot(UWidget* Widget)
	{
		return Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
	}
}

TSharedRef<SWidget> UMobileAppWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		UCanvasPanel* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MobileRoot"));
		Root->SetVisibility(ESlateVisibility::Visible);
		Tree->RootWidget = Root;

		UBorder* Backdrop = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
		Backdrop->SetBrushColor(White);
		UCanvasPanelSlot* BackdropSlot = Root->AddChildToCanvas(Backdrop);
		BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackdropSlot->SetOffsets(FMargin(0.f));

		// Everything below is laid out in a space 375 wide; the scale box blows it up to the window's width.
		PhoneScaleBox = Tree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("PhoneScaleBox"));
		PhoneScaleBox->SetStretch(EStretch::UserSpecified);
		PhoneScaleBox->SetUserSpecifiedScale(1.f);
		UCanvasPanelSlot* ScaleSlot = Root->AddChildToCanvas(PhoneScaleBox);
		ScaleSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		ScaleSlot->SetOffsets(FMargin(0.f));
		PhoneSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PhoneSize"));
		PhoneSize->SetWidthOverride(static_cast<float>(DesignWidth));
		PhoneSize->SetHeightOverride(static_cast<float>(DesignHeight));
		PhoneScaleBox->SetContent(PhoneSize);
		Phone = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Phone"));
		PhoneSize->SetContent(Phone);

		// The map: a placeholder for now.
		UBorder* MapArea = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MapArea"));
		MapArea->SetBrushColor(MapBackground);
		MapArea->SetHorizontalAlignment(HAlign_Center);
		MapArea->SetVerticalAlignment(VAlign_Top);
		MapArea->SetPadding(FMargin(0.f, 220.f, 0.f, 0.f));
		MapArea->SetContent(MakeText(Tree, TEXT("MapPlaceholder"), TEXT("Map"), 18.f, Grey, true));
		UCanvasPanelSlot* MapSlot = Phone->AddChildToCanvas(MapArea);
		MapSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		MapSlot->SetOffsets(FMargin(0.f));

		// Top bar: the site's name, Overview, and 2D | 3D.
		TopBarBorder = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TopBar"));
		TopBarBorder->SetBrushColor(FLinearColor::Transparent);
		TopBarBorder->SetPadding(FMargin(12.f, 0.f));
		UHorizontalBox* TopRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TopRow"));
		TopBarBorder->SetContent(TopRow);
		UBorder* SiteChip = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SiteChip"));
		SiteChip->SetBrush(Rounded(White, Line, 10.f));
		SiteChip->SetBrushColor(FLinearColor::White);
		SiteChip->SetPadding(FMargin(12.f, 8.f));
		SiteChip->SetContent(MakeText(Tree, TEXT("SiteName"), TEXT("SIT Campus"), 13.f, Ink, true));
		UHorizontalBoxSlot* ChipSlot = TopRow->AddChildToHorizontalBox(SiteChip);
		ChipSlot->SetVerticalAlignment(VAlign_Center);
		USpacer* TopSpacer = Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("TopSpacer"));
		TopRow->AddChildToHorizontalBox(TopSpacer)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UButton* OverviewButton = MakeButton(Tree, TEXT("Btn_Overview"), TEXT("Overview"), White, Ink, 10.f, 34.f, nullptr, 12.f);
		TopRow->AddChildToHorizontalBox(OverviewButton)->SetVerticalAlignment(VAlign_Center);
		UBorder* ModeBox = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ModeBox"));
		ModeBox->SetBrush(Rounded(White, Line, 10.f));
		ModeBox->SetBrushColor(FLinearColor::White);
		ModeBox->SetPadding(FMargin(3.f));
		UHorizontalBox* ModeRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ModeRow"));
		ModeBox->SetContent(ModeRow);
		Mode2DButton = MakeButton(Tree, TEXT("Btn_Mode2D"), TEXT("2D"), Blue, White, 8.f, 28.f, &Mode2DLabel, 12.f);
		Mode3DButton = MakeButton(Tree, TEXT("Btn_Mode3D"), TEXT("3D"), White, Ink, 8.f, 28.f, &Mode3DLabel, 12.f);
		Mode2DButton->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnShow2DClicked);
		Mode3DButton->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnShow3DClicked);
		ModeRow->AddChildToHorizontalBox(Mode2DButton);
		ModeRow->AddChildToHorizontalBox(Mode3DButton);
		UHorizontalBoxSlot* ModeSlot = TopRow->AddChildToHorizontalBox(ModeBox);
		ModeSlot->SetVerticalAlignment(VAlign_Center);
		ModeSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		UCanvasPanelSlot* TopSlot = Phone->AddChildToCanvas(TopBarBorder);
		TopSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
		TopSlot->SetAlignment(FVector2D(0.f, 0.f));

		// The level picker and the locate button, floating above the sheet's top edge.
		UVerticalBox* Controls = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MapControls"));
		UButton* LevelButton = MakeButton(Tree, TEXT("Btn_Level"), TEXT("L1"), White, Ink, 10.f, 40.f, nullptr, 16.f);
		Controls->AddChildToVerticalBox(LevelButton);
		UButton* LocateButton = MakeButton(Tree, TEXT("Btn_Locate"), TEXT("Locate"), White, Blue, 10.f, 40.f, nullptr, 12.f);
		Controls->AddChildToVerticalBox(LocateButton)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
		ControlsBox = Controls;
		UCanvasPanelSlot* ControlsSlot = Phone->AddChildToCanvas(Controls);
		ControlsSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
		ControlsSlot->SetAlignment(FVector2D(1.f, 1.f));
		ControlsSlot->SetAutoSize(true);

		// The bottom sheet.
		SheetBorder = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Sheet"));
		SheetBorder->SetBrush(RoundedTop(White, 20.f));
		SheetBorder->SetBrushColor(FLinearColor::White);
		SheetBorder->SetPadding(FMargin(0.f));
		UVerticalBox* SheetColumn = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SheetColumn"));
		SheetBorder->SetContent(SheetColumn);
		UBorder* HandleZone = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SheetHandleZone"));
		HandleZone->SetBrushColor(FLinearColor::Transparent);
		HandleZone->SetHorizontalAlignment(HAlign_Center);
		HandleZone->SetVerticalAlignment(VAlign_Top);
		HandleZone->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
		USizeBox* HandleSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SheetHandleSize"));
		HandleSize->SetWidthOverride(40.f);
		HandleSize->SetHeightOverride(5.f);
		UBorder* HandleBar = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SheetHandle"));
		HandleBar->SetBrush(Rounded(StartStyle::Rgb(209, 213, 219), FLinearColor::Transparent, 3.f, 0.f));
		HandleBar->SetBrushColor(FLinearColor::White);
		HandleSize->SetContent(HandleBar);
		HandleZone->SetContent(HandleSize);
		USizeBox* HandleZoneSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SheetHandleZoneSize"));
		HandleZoneSize->SetHeightOverride(static_cast<float>(HandleZoneHeight));
		HandleZoneSize->SetContent(HandleZone);
		SheetColumn->AddChildToVerticalBox(HandleZoneSize);

		SheetPages = Tree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("SheetPages"));
		SheetColumn->AddChildToVerticalBox(SheetPages)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		// One placeholder page for each state of the flow: its name, what goes there, and the button that moves the flow on.
		struct FPageSpec
		{
			const TCHAR* Title;
			const TCHAR* Note;
			const TCHAR* PrimaryName;
			const TCHAR* PrimaryLabel;
			bool bHasBack;
		};
		const FPageSpec Specs[] = {
			{ TEXT("Where to?"), TEXT("Search box, filters and the places, grouped by building and level."), TEXT("Btn_PickPlace"), TEXT("Pick a place (demo)"), false },
			{ TEXT("Place"), TEXT("Name, code, category, Save place, Use as starting point."), TEXT("Btn_GetDirections"), TEXT("Get directions"), true },
			{ TEXT("Your route"), TEXT("From, To, minutes, metres and the steps."), TEXT("Btn_StartNavigation"), TEXT("Start navigation"), true },
			{ TEXT("Navigating"), TEXT("The next step and what is left."), TEXT("Btn_Arrive"), TEXT("Arrive (demo)"), false },
			{ TEXT("You have arrived"), TEXT("Find another place, save this place, view the destination."), TEXT("Btn_FindAnother"), TEXT("Find another place"), false },
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Specs); ++Index)
		{
			const FPageSpec& Spec = Specs[Index];
			const FString Suffix = FString::FromInt(Index);
			if (Index == 0)
			{
				// The first page is the real search page, on the prototype's data.
				Source = MakeShared<FMobileMockSource>();
				SearchPage = CreateWidget<UMobileSearchPageWidget>(this, UMobileSearchPageWidget::StaticClass());
				FString ContextBuilding;
				int32 ContextLevel = 1;
				const TArray<FMobileBuilding> Buildings = Source->ListBuildings(SiteId);
				if (!Buildings.IsEmpty() && !Buildings[0].Levels.IsEmpty())
				{
					ContextBuilding = Buildings[0].Id;
					ContextLevel = Buildings[0].Levels[0].Level;
				}
				SearchPage->SetSource(Source.Get(), SiteId, ContextBuilding, ContextLevel);
				TWeakObjectPtr<UMobileAppWidget> WeakThis(this);
				SearchPage->SetOnPlaceChosen([WeakThis](const FMobilePlace& Place)
				{
					if (UMobileAppWidget* Screen = WeakThis.Get())
					{
						Screen->ShowPlace(Place);
					}
				});
				SearchPage->SetOnBack([WeakThis]()
				{
					if (UMobileAppWidget* Screen = WeakThis.Get())
					{
						Screen->SetActiveTab(ETab::Explore);
					}
				});
				SheetPages->AddChild(SearchPage);
				continue;
			}
			if (Index == 1)
			{
				// The second page is the real place page: it shows the place and tells the screen what the person did.
				PlacePage = CreateWidget<UMobilePlacePageWidget>(this, UMobilePlacePageWidget::StaticClass());
				TWeakObjectPtr<UMobileAppWidget> WeakThis(this);
				PlacePage->SetOnBack([WeakThis]()
				{
					if (UMobileAppWidget* Screen = WeakThis.Get())
					{
						Screen->SendSheetEvent(EMobileSheetEvent::Back);
					}
				});
				PlacePage->SetOnDirections([WeakThis](const FMobilePlace&)
				{
					if (UMobileAppWidget* Screen = WeakThis.Get())
					{
						Screen->SendSheetEvent(EMobileSheetEvent::DirectionsRequested);
					}
				});
				PlacePage->SetOnSaveToggled([WeakThis](const FMobilePlace& Place)
				{
					if (UMobileAppWidget* Screen = WeakThis.Get())
					{
						if (Screen->SavedPlaceIds.Remove(Place.Id) == 0)
						{
							Screen->SavedPlaceIds.Add(Place.Id);
						}
						Screen->SelectPlace(Place);
					}
				});
				PlacePage->SetOnUseAsStart([WeakThis](const FMobilePlace& Place)
				{
					if (UMobileAppWidget* Screen = WeakThis.Get())
					{
						Screen->StartPlaceId = Screen->StartPlaceId == Place.Id ? FString() : Place.Id;
						Screen->SelectPlace(Place);
					}
				});
				SheetPages->AddChild(PlacePage);
				continue;
			}
			UBorder* Page = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(TEXT("SheetPage") + Suffix));
			Page->SetBrushColor(FLinearColor::Transparent);
			Page->SetPadding(FMargin(16.f, 4.f, 16.f, 12.f));
			UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(TEXT("SheetPageContent") + Suffix));
			Page->SetContent(Content);
			if (Spec.bHasBack)
			{
				UButton* Back = MakeButton(Tree, TEXT("Btn_Back") + Suffix, TEXT("Back"), BlueSoft, Blue, 8.f, 30.f, nullptr, 12.f);
				Back->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnBackClicked);
				Content->AddChildToVerticalBox(Back)->SetHorizontalAlignment(HAlign_Left);
			}
			Content->AddChildToVerticalBox(MakeText(Tree, TEXT("Txt_Title") + Suffix, Spec.Title, 22.f, Ink, true))->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
			UTextBlock* Note = MakeText(Tree, TEXT("Txt_Note") + Suffix, Spec.Note, 13.f, Grey);
			Note->SetAutoWrapText(true);
			Content->AddChildToVerticalBox(Note)->SetPadding(FMargin(0.f, 6.f, 0.f, 14.f));
			UButton* Primary = MakeButton(Tree, Spec.PrimaryName, Spec.PrimaryLabel, Blue, White, 12.f, 48.f);
			Content->AddChildToVerticalBox(Primary);
			switch (Index)
			{
			case 2: Primary->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnStartNavigationClicked); break;
			case 3: Primary->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnArriveClicked); break;
			default: Primary->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnFindAnotherClicked); break;
			}
			if (Index == 3)
			{
				UButton* Exit = MakeButton(Tree, TEXT("Btn_Exit"), TEXT("Exit"), BlueSoft, Blue, 12.f, 44.f);
				Exit->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnExitNavigationClicked);
				Content->AddChildToVerticalBox(Exit)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
			}
			if (Index == 4)
			{
				UButton* View = MakeButton(Tree, TEXT("Btn_ViewDestination"), TEXT("View destination"), BlueSoft, Blue, 12.f, 44.f);
				View->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnViewDestinationClicked);
				Content->AddChildToVerticalBox(View)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
			}
			SheetPages->AddChild(Page);
		}
		UCanvasPanelSlot* SheetSlot = Phone->AddChildToCanvas(SheetBorder);
		SheetSlot->SetAnchors(FAnchors(0.f, 1.f, 1.f, 1.f));
		SheetSlot->SetAlignment(FVector2D(0.f, 1.f));

		// The pages of the other tabs lie over the map and the sheet.
		auto MakeOverlay = [&](const TCHAR* Name, const TCHAR* Title, const TCHAR* Note, bool bWithOpenButton) -> UBorder*
		{
			UBorder* Overlay = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Name));
			Overlay->SetBrushColor(White);
			Overlay->SetPadding(FMargin(16.f, 64.f, 16.f, 0.f));
			UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(FString(Name) + TEXT("_Content")));
			Overlay->SetContent(Content);
			Content->AddChildToVerticalBox(MakeText(Tree, FString(Name) + TEXT("_Title"), Title, 26.f, Ink, true));
			UTextBlock* Text = MakeText(Tree, FString(Name) + TEXT("_Note"), Note, 13.f, Grey);
			Text->SetAutoWrapText(true);
			Content->AddChildToVerticalBox(Text)->SetPadding(FMargin(0.f, 8.f, 0.f, 16.f));
			if (bWithOpenButton)
			{
				UButton* Open = MakeButton(Tree, TEXT("Btn_OpenSite"), TEXT("Open SIT Campus (demo)"), Blue, White, 12.f, 48.f);
				Open->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnOpenSiteClicked);
				Content->AddChildToVerticalBox(Open);
			}
			UCanvasPanelSlot* OverlaySlot = Phone->AddChildToCanvas(Overlay);
			OverlaySlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			return Overlay;
		};
		ExploreOverlay = MakeOverlay(TEXT("ExploreOverlay"), TEXT("Find your way indoors"), TEXT("The sites to choose from, as cards."), true);
		SavedOverlay = MakeOverlay(TEXT("SavedOverlay"), TEXT("Saved"), TEXT("The places you saved."), false);
		ProfileOverlay = MakeOverlay(TEXT("ProfileOverlay"), TEXT("Profile"), TEXT("Settings and preferences."), false);

		// The tab bar.
		TabBarBorder = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TabBar"));
		TabBarBorder->SetBrush(Rounded(White, Line, 0.f));
		TabBarBorder->SetBrushColor(FLinearColor::White);
		TabBarBorder->SetPadding(FMargin(0.f));
		UHorizontalBox* TabRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TabRow"));
		TabBarBorder->SetContent(TabRow);
		const TCHAR* TabNames[4] = { TEXT("Explore"), TEXT("Directory"), TEXT("Saved"), TEXT("Profile") };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FString Name = FString::Printf(TEXT("Btn_Tab%s"), TabNames[Index]);
			UButton* Tab = Tree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Tab->SetStyle(StartStyle::Button(White, White * 0.97f, White * 0.92f, FLinearColor::Transparent, 0.f));
			Tab->SetBackgroundColor(FLinearColor::White);
			UVerticalBox* TabContent = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(Name + TEXT("_Content")));
			UBorder* Icon = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Name + TEXT("_Icon")));
			USizeBox* IconSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(Name + TEXT("_IconSize")));
			IconSize->SetWidthOverride(22.f);
			IconSize->SetHeightOverride(22.f);
			IconSize->SetContent(Icon);
			TabContent->AddChildToVerticalBox(IconSize)->SetHorizontalAlignment(HAlign_Center);
			UTextBlock* Label = MakeText(Tree, Name + TEXT("_Label"), TabNames[Index], 11.f, Grey, true);
			TabContent->AddChildToVerticalBox(Label)->SetHorizontalAlignment(HAlign_Center);
			UBorder* TabPad = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Name + TEXT("_Pad")));
			TabPad->SetBrushColor(FLinearColor::Transparent);
			TabPad->SetPadding(FMargin(0.f, 7.f));
			TabPad->SetContent(TabContent);
			Tab->SetContent(TabPad);
			TabButtons[Index] = Tab;
			TabLabels[Index] = Label;
			TabIcons[Index] = Icon;
			TabRow->AddChildToHorizontalBox(Tab)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		TabButtons[0]->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnTabExploreClicked);
		TabButtons[1]->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnTabDirectoryClicked);
		TabButtons[2]->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnTabSavedClicked);
		TabButtons[3]->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnTabProfileClicked);
		UCanvasPanelSlot* TabSlot = Phone->AddChildToCanvas(TabBarBorder);
		TabSlot->SetAnchors(FAnchors(0.f, 1.f, 1.f, 1.f));
		TabSlot->SetAlignment(FVector2D(0.f, 1.f));

		SetActiveTab(ActiveTab);
		RefreshModeButtons();
		RefreshSheetPage();
		PositionSheet();
		SnapSheetNow();
	}
	return Super::RebuildWidget();
}

void UMobileAppWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const FVector2D LocalSize = MyGeometry.GetLocalSize();
	if (LocalSize.X > 1.f && LocalSize.Y > 1.f)
	{
		ApplyViewportSize(LocalSize.X, LocalSize.Y);
	}
	if (!bDraggingSheet)
	{
		const double Target = RestHeight(Snap);
		if (!FMath::IsNearlyEqual(SheetHeight, Target, 0.5))
		{
			SheetHeight = FMath::FInterpTo(static_cast<float>(SheetHeight), static_cast<float>(Target), InDeltaTime, 14.f);
			PositionSheet();
		}
	}
}

void UMobileAppWidget::ApplyViewportSize(double LocalWidth, double LocalHeight)
{
	if (LocalWidth < 1.0 || LocalHeight < 1.0 || !PhoneSize || !PhoneScaleBox)
	{
		return;
	}
	const double NewScale = LocalWidth / DesignWidth;
	const double NewHeight = LocalHeight / NewScale;
	const bool bChanged = !FMath::IsNearlyEqual(NewScale, Scale, 1e-6) || !FMath::IsNearlyEqual(NewHeight, DesignHeight, 1e-3);
	Scale = NewScale;
	DesignHeight = NewHeight;
	PhoneScaleBox->SetUserSpecifiedScale(static_cast<float>(Scale));
	PhoneSize->SetHeightOverride(static_cast<float>(DesignHeight));
	if (bChanged)
	{
		// A different screen shape: the sheet stays at its rest height for the new height.
		SheetHeight = RestHeight(Snap);
		PositionSheet();
	}
}

void UMobileAppWidget::SetSafeInsets(double Top, double Bottom)
{
	SafeTop = FMath::Max(Top, 0.0);
	SafeBottom = FMath::Max(Bottom, 0.0);
	SheetHeight = RestHeight(Snap);
	PositionSheet();
}

double UMobileAppWidget::RestHeight(EMobileSheetSnap Target) const
{
	return MobileSheet::SnapHeight(Target, DesignHeight, TabBarHeight + SafeBottom, TopMargin + SafeTop);
}

void UMobileAppWidget::PositionSheet()
{
	if (!SheetBorder || !TabBarBorder || !TopBarBorder || !ControlsBox)
	{
		return;
	}
	const double TabTotal = TabBarHeight + SafeBottom;
	const bool bMapPage = ActiveTab == ETab::Directory;
	if (UCanvasPanelSlot* PanelSlot = CanvasSlot(SheetBorder))
	{
		PanelSlot->SetOffsets(FMargin(0.f, static_cast<float>(-TabTotal), 0.f, static_cast<float>(SheetHeight)));
	}
	if (UCanvasPanelSlot* PanelSlot = CanvasSlot(TabBarBorder))
	{
		PanelSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, static_cast<float>(TabTotal)));
	}
	if (TabBarBorder)
	{
		TabBarBorder->SetPadding(FMargin(0.f, 0.f, 0.f, static_cast<float>(SafeBottom)));
	}
	if (UCanvasPanelSlot* PanelSlot = CanvasSlot(TopBarBorder))
	{
		PanelSlot->SetOffsets(FMargin(0.f, static_cast<float>(SafeTop + 12.0), 0.f, 0.f));
		PanelSlot->SetAutoSize(true);
	}
	if (UCanvasPanelSlot* PanelSlot = CanvasSlot(ControlsBox))
	{
		// Anchored at the top edge, moved down to sit just above the sheet.
		const double SheetTop = DesignHeight - TabTotal - SheetHeight;
		PanelSlot->SetPosition(FVector2D(-12.0, SheetTop - 12.0));
	}
	for (UBorder* Overlay : { ExploreOverlay, SavedOverlay, ProfileOverlay })
	{
		if (UCanvasPanelSlot* PanelSlot = CanvasSlot(Overlay))
		{
			PanelSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, static_cast<float>(TabTotal)));
		}
	}
	SheetBorder->SetVisibility(bMapPage ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	ControlsBox->SetVisibility(bMapPage && ControlsFit() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	TopBarBorder->SetVisibility(bMapPage ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

bool UMobileAppWidget::ControlsFit() const
{
	const double SheetTop = DesignHeight - (TabBarHeight + SafeBottom) - SheetHeight;
	const double TopBarBottom = SafeTop + 12.0 + 40.0;
	return SheetTop - ControlsGap - ControlsHeight >= TopBarBottom + 8.0;
}

FMobileLayoutSnapshot UMobileAppWidget::GetLayoutSnapshot() const
{
	FMobileLayoutSnapshot Snapshot;
	Snapshot.DesignWidth = DesignWidth;
	Snapshot.DesignHeight = DesignHeight;
	Snapshot.TabBarHeight = TabBarHeight + SafeBottom;
	Snapshot.TabBarTop = DesignHeight - Snapshot.TabBarHeight;
	Snapshot.SheetHeight = SheetHeight;
	Snapshot.SheetTop = Snapshot.TabBarTop - SheetHeight;
	Snapshot.TopBarBottom = SafeTop + 12.0 + 40.0;
	Snapshot.ControlsBottom = Snapshot.SheetTop - ControlsGap;
	Snapshot.bControlsVisible = ControlsFit();
	return Snapshot;
}

void UMobileAppWidget::SnapSheetNow()
{
	SheetHeight = RestHeight(Snap);
	PositionSheet();
}

void UMobileAppWidget::SetSnap(EMobileSheetSnap NewSnap)
{
	Snap = NewSnap;
}

void UMobileAppWidget::SetActiveTab(ETab Tab)
{
	ActiveTab = Tab;
	if (ExploreOverlay) ExploreOverlay->SetVisibility(Tab == ETab::Explore ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (SavedOverlay) SavedOverlay->SetVisibility(Tab == ETab::Saved ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (ProfileOverlay) ProfileOverlay->SetVisibility(Tab == ETab::Profile ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	RefreshTabs();
	PositionSheet();
}

void UMobileAppWidget::SendSheetEvent(EMobileSheetEvent Event)
{
	if (Flow.Handle(Event))
	{
		Snap = MobileSheet::SnapForState(Flow.GetState());
		RefreshSheetPage();
	}
}

void UMobileAppWidget::RefreshSheetPage()
{
	if (SheetPages)
	{
		SheetPages->SetActiveWidgetIndex(static_cast<int32>(Flow.GetState()));
	}
}

void UMobileAppWidget::RefreshTabs()
{
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const bool bActive = static_cast<int32>(ActiveTab) == Index;
		if (TabLabels[Index])
		{
			TabLabels[Index]->SetColorAndOpacity(FSlateColor(bActive ? Blue : Grey));
		}
		if (TabIcons[Index])
		{
			// Placeholder icons: a rounded square, blue for the open tab.
			TabIcons[Index]->SetBrush(Rounded(bActive ? Blue : StartStyle::Rgb(209, 213, 219), FLinearColor::Transparent, 6.f, 0.f));
			TabIcons[Index]->SetBrushColor(FLinearColor::White);
		}
	}
}

void UMobileAppWidget::RefreshModeButtons()
{
	auto Style = [](UButton* Button, UTextBlock* Label, bool bOn)
	{
		if (Button)
		{
			const FLinearColor Fill = bOn ? Blue : White;
			Button->SetStyle(StartStyle::Button(Fill, Fill * 0.94f, Fill * 0.86f, Line, 8.f / StartStyle::DesignToUi));
		}
		if (Label)
		{
			Label->SetColorAndOpacity(FSlateColor(bOn ? White : Ink));
		}
	};
	Style(Mode2DButton, Mode2DLabel, !bShow3D);
	Style(Mode3DButton, Mode3DLabel, bShow3D);
}

UWidget* UMobileAppWidget::FindNamedWidget(const FName& Name) const
{
	return WidgetTree ? WidgetTree->FindWidget(Name) : nullptr;
}

// ---- Sheet dragging ----

bool UMobileAppWidget::BeginSheetDrag(const FVector2D& DesignPosition)
{
	if (ActiveTab != ETab::Directory)
	{
		return false;
	}
	const double SheetTop = DesignHeight - (TabBarHeight + SafeBottom) - SheetHeight;
	if (DesignPosition.Y < SheetTop || DesignPosition.Y > SheetTop + HandleZoneHeight)
	{
		return false;
	}
	bDraggingSheet = true;
	bSheetMoved = false;
	DragStartY = DesignPosition.Y;
	DragStartHeight = SheetHeight;
	return true;
}

void UMobileAppWidget::MoveSheetDrag(const FVector2D& DesignPosition)
{
	if (!bDraggingSheet)
	{
		return;
	}
	if (FMath::Abs(DesignPosition.Y - DragStartY) > 6.0)
	{
		bSheetMoved = true;
	}
	if (bSheetMoved)
	{
		const double Lowest = RestHeight(EMobileSheetSnap::Collapsed) * 0.7;
		const double Highest = RestHeight(EMobileSheetSnap::Full);
		SheetHeight = FMath::Clamp(DragStartHeight + (DragStartY - DesignPosition.Y), Lowest, Highest);
		PositionSheet();
	}
}

void UMobileAppWidget::EndSheetDrag(const FVector2D& DesignPosition)
{
	if (!bDraggingSheet)
	{
		return;
	}
	MoveSheetDrag(DesignPosition);
	bDraggingSheet = false;
	if (bSheetMoved)
	{
		Snap = MobileSheet::NearestSnap(SheetHeight, DesignHeight, TabBarHeight + SafeBottom, TopMargin + SafeTop);
	}
	else
	{
		// A tap on the handle: the next height.
		Snap = MobileSheet::NextSnap(MobileSheet::NearestSnap(SheetHeight, DesignHeight, TabBarHeight + SafeBottom, TopMargin + SafeTop));
	}
}

FVector2D UMobileAppWidget::ToDesign(const FGeometry& Geometry, const FVector2D& ScreenSpacePosition) const
{
	return Geometry.AbsoluteToLocal(ScreenSpacePosition) / FMath::Max(Scale, 1e-6);
}

FReply UMobileAppWidget::HandlePointerDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (BeginSheetDrag(ToDesign(Geometry, Event.GetScreenSpacePosition())))
	{
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return FReply::Unhandled();
}

FReply UMobileAppWidget::HandlePointerMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (bDraggingSheet)
	{
		MoveSheetDrag(ToDesign(Geometry, Event.GetScreenSpacePosition()));
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply UMobileAppWidget::HandlePointerUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (bDraggingSheet)
	{
		EndSheetDrag(ToDesign(Geometry, Event.GetScreenSpacePosition()));
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply UMobileAppWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton ? HandlePointerDown(InGeometry, InMouseEvent) : FReply::Unhandled();
}

FReply UMobileAppWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return HandlePointerMove(InGeometry, InMouseEvent);
}

FReply UMobileAppWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton ? HandlePointerUp(InGeometry, InMouseEvent) : FReply::Unhandled();
}

FReply UMobileAppWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	return HandlePointerDown(InGeometry, InGestureEvent);
}

FReply UMobileAppWidget::NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	return HandlePointerMove(InGeometry, InGestureEvent);
}

FReply UMobileAppWidget::NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	return HandlePointerUp(InGeometry, InGestureEvent);
}

// ---- Buttons ----

void UMobileAppWidget::OnTabExploreClicked() { SetActiveTab(ETab::Explore); }
void UMobileAppWidget::OnTabDirectoryClicked() { SetActiveTab(ETab::Directory); }
void UMobileAppWidget::OnTabSavedClicked() { SetActiveTab(ETab::Saved); }
void UMobileAppWidget::OnTabProfileClicked() { SetActiveTab(ETab::Profile); }
void UMobileAppWidget::OnOpenSiteClicked() { SetActiveTab(ETab::Directory); }
void UMobileAppWidget::OnStartNavigationClicked() { SendSheetEvent(EMobileSheetEvent::NavigationStarted); }
void UMobileAppWidget::OnArriveClicked() { SendSheetEvent(EMobileSheetEvent::ArrivedAtDestination); }
void UMobileAppWidget::OnBackClicked() { SendSheetEvent(EMobileSheetEvent::Back); }
void UMobileAppWidget::OnExitNavigationClicked() { SendSheetEvent(EMobileSheetEvent::ExitNavigation); }
void UMobileAppWidget::OnFindAnotherClicked() { SendSheetEvent(EMobileSheetEvent::FindAnotherPlace); }
void UMobileAppWidget::OnViewDestinationClicked() { SendSheetEvent(EMobileSheetEvent::ViewDestination); }

void UMobileAppWidget::OnShow2DClicked()
{
	bShow3D = false;
	RefreshModeButtons();
}

void UMobileAppWidget::OnShow3DClicked()
{
	bShow3D = true;
	RefreshModeButtons();
}

void UMobileAppWidget::PreviewState(EMobileSheetState Target)
{
	static const EMobileSheetEvent Way[] = { EMobileSheetEvent::PlaceChosen, EMobileSheetEvent::DirectionsRequested, EMobileSheetEvent::NavigationStarted, EMobileSheetEvent::ArrivedAtDestination };
	// A preview past the search needs a place to show: the first one of the site.
	if (Target != EMobileSheetState::Search && SelectedPlace.Id.IsEmpty() && Source.IsValid())
	{
		const TArray<FMobilePlace> All = Source->SearchPlaces(SiteId, FString(), FMobilePlaceFilter());
		if (!All.IsEmpty())
		{
			SelectPlace(All[0]);
		}
	}
	Flow.Reset();
	for (int32 Step = 0; Step < UE_ARRAY_COUNT(Way) && Flow.GetState() != Target; ++Step)
	{
		Flow.Handle(Way[Step]);
	}
	Snap = MobileSheet::SnapForState(Flow.GetState());
	RefreshSheetPage();
	SnapSheetNow();
}

void UMobileAppWidget::SelectPlace(const FMobilePlace& Place)
{
	SelectedPlace = Place;
	if (PlacePage)
	{
		PlacePage->SetPlace(Place, SavedPlaceIds.Contains(Place.Id), StartPlaceId == Place.Id);
	}
}

void UMobileAppWidget::ShowPlace(const FMobilePlace& Place)
{
	SelectPlace(Place);
	SendSheetEvent(EMobileSheetEvent::PlaceChosen);
}
