#include "MobileAppWidget.h"

#include "MobileArrivedPageWidget.h"
#include "MobileExplorePageWidget.h"
#include "MobileMapWidget.h"
#include "MobileNavigatePageWidget.h"
#include "MobileTapTarget.h"
#include "MobilePlacePageWidget.h"
#include "MobileRoutePageWidget.h"
#include "MobileProfilePageWidget.h"
#include "MobileSavedPageWidget.h"
#include "MobileSelectWidget.h"
#include "MobileSearchPageWidget.h"
#include "MobileStyle.h"
#include "MobileRealSource.h"
#include "MobileMockSource.h"
#include "StartScreenStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
		Backdrop->SetBrushColor(PageBg);
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

		// The map: a floor plan of the level, or the site's overview, on the map's own colour. It fills the screen; the sheet covers its lower part.
		UBorder* MapArea = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MapArea"));
		MapArea->SetBrushColor(MapBackground);
		UCanvasPanelSlot* MapSlot = Phone->AddChildToCanvas(MapArea);
		MapSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		MapSlot->SetOffsets(FMargin(0.f));
		MapView = CreateWidget<UMobileMapWidget>(this, UMobileMapWidget::StaticClass());
		TWeakObjectPtr<UMobileAppWidget> WeakMapOwner(this);
		MapView->SetOnPlaceTapped([WeakMapOwner](const FString& PlaceId)
		{
			if (UMobileAppWidget* Screen = WeakMapOwner.Get())
			{
				// A tap on a room opens its place while looking for one.
				FMobilePlace Place;
				const EMobileSheetState State = Screen->Flow.GetState();
				if ((State == EMobileSheetState::Search || State == EMobileSheetState::Place) && Screen->Source.IsValid() && Screen->Source->FindPlace(PlaceId, Place))
				{
					if (State == EMobileSheetState::Place)
					{
						Screen->SelectPlace(Place);
						Screen->RefreshMap();
					}
					else
					{
						Screen->ShowPlace(Place);
					}
				}
			}
		});
		MapView->SetOnBuildingTapped([WeakMapOwner](const FString& BuildingId)
		{
			if (UMobileAppWidget* Screen = WeakMapOwner.Get())
			{
				Screen->OpenBuilding(BuildingId);
			}
		});
		UCanvasPanelSlot* MapViewSlot = Phone->AddChildToCanvas(MapView);
		MapViewSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		MapViewSlot->SetOffsets(FMargin(0.f));

		// The top row of the map, as in the prototype: the title (the site, or the building and level) as plain text, Overview (for a campus), and 2D | 3D.
		TopBarBorder = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TopBar"));
		TopBarBorder->SetBrushColor(FLinearColor::Transparent);
		TopBarBorder->SetPadding(FMargin(12.f, 0.f));
		UHorizontalBox* TopRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TopRow"));
		TopBarBorder->SetContent(TopRow);
		SiteNameText = MakeText(Tree, TEXT("SiteName"), TEXT("SIT Campus"), 14.f, Ink, true);
		UHorizontalBoxSlot* NameSlot = TopRow->AddChildToHorizontalBox(SiteNameText);
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NameSlot->SetVerticalAlignment(VAlign_Center);
		OverviewButton = MakeButton(Tree, TEXT("Btn_Overview"), TEXT("Overview"), White, Ink, 8.f, 44.f, &OverviewLabel, 14.f);
		OverviewButton->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnOverviewClicked);
		TopRow->AddChildToHorizontalBox(OverviewButton)->SetVerticalAlignment(VAlign_Center);
		UBorder* ModeBox = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ModeBox"));
		ModeBox->SetBrush(Rounded(White, Line, 8.f));
		ModeBox->SetBrushColor(FLinearColor::White);
		ModeBox->SetPadding(FMargin(5.f));
		UHorizontalBox* ModeRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ModeRow"));
		ModeBox->SetContent(ModeRow);
		Mode2DButton = MakeButton(Tree, TEXT("Btn_Mode2D"), TEXT("2D"), Blue, OnBlue, 8.f, 44.f, &Mode2DLabel, 10.f);
		Mode3DButton = MakeButton(Tree, TEXT("Btn_Mode3D"), TEXT("3D"), White, Ink, 8.f, 44.f, &Mode3DLabel, 10.f);
		ApplyFont(Mode2DLabel, 12.f, true);
		ApplyFont(Mode3DLabel, 12.f, true);
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

		// The map's toolbar, as in the prototype: one bar at the bottom right, above the sheet, with the level, locate and more. The levels (a list)
		// and the tools (Move, zoom out and in) open upward from it.
		UVerticalBox* Controls = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MapControls"));
		LevelPopupBorder = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LevelPopup"));
		LevelPopupBorder->SetBrush(Rounded(White, Line, 10.f));
		LevelPopupBorder->SetBrushColor(FLinearColor::White);
		LevelPopupBorder->SetPadding(FMargin(4.f));
		LevelPopupBorder->SetVisibility(ESlateVisibility::Collapsed);
		LevelPopupList = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LevelPopupList"));
		UScrollBox* LevelScroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("LevelPopupScroll"));
		LevelScroll->AddChild(LevelPopupList);
		USizeBox* LevelLimit = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LevelPopupLimit"));
		LevelLimit->SetMaxDesiredHeight(200.f);
		LevelLimit->SetMinDesiredWidth(64.f);
		LevelLimit->SetContent(LevelScroll);
		LevelPopupBorder->SetContent(LevelLimit);
		UVerticalBoxSlot* LevelPopupSlot = Controls->AddChildToVerticalBox(LevelPopupBorder);
		LevelPopupSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 7.f));
		LevelPopupSlot->SetHorizontalAlignment(HAlign_Left);

		// The tools: Move (drag and pinch), and zoom out and in.
		MapToolsBorder = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MapTools"));
		MapToolsBorder->SetBrush(Rounded(White, Line, 10.f));
		MapToolsBorder->SetBrushColor(FLinearColor::White);
		MapToolsBorder->SetPadding(FMargin(8.f));
		MapToolsBorder->SetVisibility(ESlateVisibility::Collapsed);
		UHorizontalBox* ToolsRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MapToolsRow"));
		MapToolsBorder->SetContent(ToolsRow);
		auto Sized = [Tree](UWidget* Content, float Width, float Height)
		{
			USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			Box->SetMinDesiredWidth(Width);
			Box->SetMinDesiredHeight(Height);
			Box->SetContent(Content);
			return Box;
		};
		MapMoveButton = MakeButton(Tree, TEXT("Btn_MapMove"), TEXT("Move: On"), White, Ink, 8.f, 44.f, &MapMoveLabel, 10.f);
		ApplyFont(MapMoveLabel, 13.f, true);
		MapMoveButton->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnMapMoveClicked);
		ToolsRow->AddChildToHorizontalBox(Sized(MapMoveButton, 73.f, 44.f));
		ZoomOutButton = MakeButton(Tree, TEXT("Btn_ZoomOut"), TEXT("-"), White, Ink, 8.f, 44.f, &ZoomOutLabel, 10.f);
		ZoomOutButton->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnZoomOutClicked);
		ToolsRow->AddChildToHorizontalBox(Sized(ZoomOutButton, 44.f, 44.f))->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		ZoomInButton = MakeButton(Tree, TEXT("Btn_ZoomIn"), TEXT("+"), White, Ink, 8.f, 44.f, &ZoomInLabel, 10.f);
		ZoomInButton->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnZoomInClicked);
		ToolsRow->AddChildToHorizontalBox(Sized(ZoomInButton, 44.f, 44.f))->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		ApplyFont(ZoomOutLabel, 18.f, true);
		ApplyFont(ZoomInLabel, 18.f, true);
		UVerticalBoxSlot* ToolsSlot = Controls->AddChildToVerticalBox(MapToolsBorder);
		ToolsSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 7.f));
		ToolsSlot->SetHorizontalAlignment(HAlign_Right);

		// The bar: level, locate, more.
		UBorder* ToolbarBorder = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MapToolbar"));
		ToolbarBorder->SetBrush(Rounded(White, Line, 10.f));
		ToolbarBorder->SetBrushColor(FLinearColor::White);
		ToolbarBorder->SetPadding(FMargin(5.f));
		UHorizontalBox* ToolbarRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MapToolbarRow"));
		ToolbarBorder->SetContent(ToolbarRow);
		UButton* LevelButton = MakeButton(Tree, TEXT("Btn_Level"), TEXT("L1"), White, Ink, 8.f, 44.f, &LevelLabel, 10.f);
		LevelButton->SetStyle(StartStyle::Button(White, White * 0.94f, White * 0.86f, FLinearColor::Transparent, 8.f / StartStyle::DesignToUi));
		ApplyFont(LevelLabel, 16.f, false);
		LevelButton->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnLevelClicked);
		ToolbarRow->AddChildToHorizontalBox(Sized(LevelButton, 68.f, 44.f));
		// The two icon buttons are drawn from shapes until the design team's icons are in: a target for locate, three dots for more.
		auto MakeIconButton = [&](const TCHAR* Name, UWidget* Icon) -> UButton*
		{
			UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetStyle(StartStyle::Button(White, White * 0.94f, White * 0.86f, FLinearColor::Transparent, 8.f / StartStyle::DesignToUi));
			Button->SetBackgroundColor(FLinearColor::White);
			UBorder* Centre = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Centre->SetBrushColor(FLinearColor::Transparent);
			Centre->SetHorizontalAlignment(HAlign_Center);
			Centre->SetVerticalAlignment(VAlign_Center);
			Centre->SetContent(Icon);
			Button->SetContent(Sized(Centre, 44.f, 44.f));
			return Button;
		};
		auto Shape = [Tree](float Size, const FLinearColor& Fill, const FLinearColor& Edge, float EdgeWidth)
		{
			UBorder* Piece = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Piece->SetBrush(Rounded(Fill, Edge, Size * 0.5f, EdgeWidth));
			Piece->SetBrushColor(FLinearColor::White);
			USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			Box->SetWidthOverride(Size);
			Box->SetHeightOverride(Size);
			Box->SetContent(Piece);
			return Box;
		};
		UOverlay* Target = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		UOverlaySlot* RingSlot = Target->AddChildToOverlay(Shape(18.f, FLinearColor::Transparent, Ink, 2.f));
		RingSlot->SetHorizontalAlignment(HAlign_Center);
		RingSlot->SetVerticalAlignment(VAlign_Center);
		UOverlaySlot* DotSlot = Target->AddChildToOverlay(Shape(6.f, Ink, FLinearColor::Transparent, 0.f));
		DotSlot->SetHorizontalAlignment(HAlign_Center);
		DotSlot->SetVerticalAlignment(VAlign_Center);
		UButton* LocateButton = MakeIconButton(TEXT("Btn_Locate"), Target);
		LocateButton->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnLocateClicked);
		ToolbarRow->AddChildToHorizontalBox(LocateButton)->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
		UVerticalBox* Dots = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		for (int32 Dot = 0; Dot < 3; ++Dot)
		{
			Dots->AddChildToVerticalBox(Shape(4.f, Ink, FLinearColor::Transparent, 0.f))->SetPadding(FMargin(0.f, Dot == 0 ? 0.f : 3.f, 0.f, 0.f));
		}
		UButton* MoreButton = MakeIconButton(TEXT("Btn_MapMore"), Dots);
		MoreButton->OnClicked.AddUniqueDynamic(this, &UMobileAppWidget::OnMapMoreClicked);
		ToolbarRow->AddChildToHorizontalBox(MoreButton)->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
		Controls->AddChildToVerticalBox(ToolbarBorder)->SetHorizontalAlignment(HAlign_Right);
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
		HandleZone->SetPadding(FMargin(0.f, 11.f, 0.f, 0.f));
		USizeBox* HandleSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SheetHandleSize"));
		HandleSize->SetWidthOverride(40.f);
		HandleSize->SetHeightOverride(5.f);
		UBorder* HandleBar = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SheetHandle"));
		HandleBar->SetBrush(Rounded(IconGrey, FLinearColor::Transparent, 3.f, 0.f));
		HandleBar->SetBrushColor(FLinearColor::White);
		HandleSize->SetContent(HandleBar);
		HandleZone->SetContent(HandleSize);
		USizeBox* HandleZoneSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SheetHandleZoneSize"));
		HandleZoneSize->SetHeightOverride(static_cast<float>(HandleZoneHeight));
		HandleZoneSize->SetContent(HandleZone);
		SheetColumn->AddChildToVerticalBox(HandleZoneSize);

		SheetPages = Tree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("SheetPages"));
		SheetColumn->AddChildToVerticalBox(SheetPages)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		// One page for each state of the flow, in the order of EMobileSheetState.
		for (int32 Index = 0; Index < 5; ++Index)
		{
			if (Index == 0)
			{
				// Screens consume the same interface in production and in prototype tests.
				if (!Source)
				{
					if (FParse::Param(FCommandLine::Get(), TEXT("MobileMock"))) { Source = MakeShared<FMobileMockSource>(); SiteId = TEXT("sit-punggol"); }
					else Source = FMobileRealSource::FromWorld(GetWorld());
				}
				const auto Sites = Source->ListSites();
				if (!Sites.ContainsByPredicate([&](const auto& Site) { return Site.Id == SiteId; }))
					SiteId = Sites.IsEmpty() ? FString() : Sites[0].Id;
				SearchPage = CreateWidget<UMobileSearchPageWidget>(this, UMobileSearchPageWidget::StaticClass());
				FString ContextBuilding;
				int32 ContextLevel = 1;
				const TArray<FMobileBuilding> Buildings = Source->ListBuildings(SiteId);
				if (!Buildings.IsEmpty() && !Buildings[0].Levels.IsEmpty())
				{
					ContextBuilding = Buildings[0].Id;
					ContextLevel = Buildings[0].Levels[0].Level;
				}
				const auto Places = Source->SearchPlaces(SiteId, FString(), FMobilePlaceFilter());
				if (!Places.IsEmpty()) { ContextBuilding = Places[0].BuildingId; ContextLevel = Places[0].Level; }
				SearchPage->SetSource(Source.Get(), SiteId, ContextBuilding, ContextLevel);
				TWeakObjectPtr<UMobileAppWidget> WeakSearchOwner(this);
				SearchPage->SetOnClearStart([WeakSearchOwner]()
				{
					if (UMobileAppWidget* Screen = WeakSearchOwner.Get())
					{
						Screen->StartPlaceId.Reset();
						Screen->RefreshSearchStart();
						Screen->SelectPlace(Screen->SelectedPlace);
						Screen->RefreshMap();
					}
				});
				MapView->SetSource(Source.Get(), SiteId);
				MapBuildingId = ContextBuilding;
				MapLevel = ContextLevel;
				// My location starts in the first building, on its first level, as in the prototype.
				MyLocationBuildingId = ContextBuilding;
				MyLocationLevel = ContextLevel;
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
						Screen->SavedPlaces.Toggle(Place.Id);
						Screen->RefreshSaved();
						Screen->SelectPlace(Place);
					}
				});
				PlacePage->SetOnUseAsStart([WeakThis](const FMobilePlace& Place)
				{
					if (UMobileAppWidget* Screen = WeakThis.Get())
					{
						// Setting a start moves on to "Where to?" to choose the destination; tapping it again takes it back.
						const bool bWasStart = Screen->StartPlaceId == Place.Id;
						Screen->StartPlaceId = bWasStart ? FString() : Place.Id;
						Screen->SelectPlace(Place);
						Screen->RefreshSearchStart();
						if (!bWasStart)
						{
							Screen->SendSheetEvent(EMobileSheetEvent::Back);
						}
						else
						{
							Screen->RefreshMap();
						}
					}
				});
				SheetPages->AddChild(PlacePage);
				continue;
			}
			if (Index == 2)
			{
				// The third page is the real route page.
				RoutePage = CreateWidget<UMobileRoutePageWidget>(this, UMobileRoutePageWidget::StaticClass());
				RoutePage->SetChoices(Source->SearchPlaces(SiteId, FString(), FMobilePlaceFilter()), Source->ListBuildings(SiteId));
				TWeakObjectPtr<UMobileAppWidget> WeakThis(this);
				RoutePage->SetOnBack([WeakThis]()
				{
					if (UMobileAppWidget* Screen = WeakThis.Get())
					{
						Screen->SendSheetEvent(EMobileSheetEvent::Back);
					}
				});
				RoutePage->SetOnRequestChanged([WeakThis](const FMobileRouteRequest& Changed)
				{
					if (UMobileAppWidget* Screen = WeakThis.Get())
					{
						Screen->ApplyRouteRequest(Changed);
					}
				});
				RoutePage->SetOnStart([WeakThis]()
				{
					if (UMobileAppWidget* Screen = WeakThis.Get())
					{
						Screen->SendSheetEvent(EMobileSheetEvent::NavigationStarted);
					}
				});
				SheetPages->AddChild(RoutePage);
				continue;
			}
			if (Index == 3)
			{
				// The fourth page is the real page for walking.
				NavigatePage = CreateWidget<UMobileNavigatePageWidget>(this, UMobileNavigatePageWidget::StaticClass());
				TWeakObjectPtr<UMobileAppWidget> WeakThis(this);
				NavigatePage->SetOnExit([WeakThis]()
				{
					if (UMobileAppWidget* Screen = WeakThis.Get())
					{
						Screen->SendSheetEvent(EMobileSheetEvent::ExitNavigation);
					}
				});
				NavigatePage->SetOnNext([WeakThis]()
				{
					if (UMobileAppWidget* Screen = WeakThis.Get())
					{
						Screen->AdvanceNavigation();
					}
				});
				SheetPages->AddChild(NavigatePage);
				continue;
			}
			// The fifth page: arrived.
			ArrivedPage = CreateWidget<UMobileArrivedPageWidget>(this, UMobileArrivedPageWidget::StaticClass());
			TWeakObjectPtr<UMobileAppWidget> WeakThis(this);
			ArrivedPage->SetOnFindAnother([WeakThis]()
			{
				if (UMobileAppWidget* Screen = WeakThis.Get())
				{
					Screen->SendSheetEvent(EMobileSheetEvent::FindAnotherPlace);
				}
			});
			ArrivedPage->SetOnSaveToggled([WeakThis](const FMobilePlace& Place)
			{
				if (UMobileAppWidget* Screen = WeakThis.Get())
				{
					Screen->SavedPlaces.Toggle(Place.Id);
					Screen->RefreshSaved();
					Screen->SelectPlace(Place);
					Screen->RefreshArrived();
				}
			});
			ArrivedPage->SetOnViewDestination([WeakThis]()
			{
				if (UMobileAppWidget* Screen = WeakThis.Get())
				{
					Screen->SendSheetEvent(EMobileSheetEvent::ViewDestination);
				}
			});
			SheetPages->AddChild(ArrivedPage);
		}
		UCanvasPanelSlot* SheetSlot = Phone->AddChildToCanvas(SheetBorder);
		SheetSlot->SetAnchors(FAnchors(0.f, 1.f, 1.f, 1.f));
		SheetSlot->SetAlignment(FVector2D(0.f, 1.f));

		// The pages of the other tabs lie over the map and the sheet.
		auto MakeOverlay = [&](const TCHAR* Name, const TCHAR* Title, const TCHAR* Note) -> UBorder*
		{
			UBorder* Overlay = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Name));
			Overlay->SetBrushColor(PageBg);
			Overlay->SetPadding(FMargin(16.f, 64.f, 16.f, 0.f));
			UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(FString(Name) + TEXT("_Content")));
			Overlay->SetContent(Content);
			Content->AddChildToVerticalBox(MakeText(Tree, FString(Name) + TEXT("_Title"), Title, 26.f, Ink, true));
			UTextBlock* Text = MakeText(Tree, FString(Name) + TEXT("_Note"), Note, 13.f, Grey);
			Text->SetAutoWrapText(true);
			Content->AddChildToVerticalBox(Text)->SetPadding(FMargin(0.f, 8.f, 0.f, 16.f));
			UCanvasPanelSlot* OverlaySlot = Phone->AddChildToCanvas(Overlay);
			OverlaySlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			return Overlay;
		};
		ExploreOverlay = MakeOverlay(TEXT("ExploreOverlay"), TEXT("Find your way indoors"), TEXT("The sites to choose from, as cards."));
		// The Explore tab is the page of sites, not a placeholder.
		ExplorePage = CreateWidget<UMobileExplorePageWidget>(this, UMobileExplorePageWidget::StaticClass());
		ExplorePage->SetSource(Source.Get());
		{
			TWeakObjectPtr<UMobileAppWidget> WeakExplore(this);
			ExplorePage->SetOnSiteChosen([WeakExplore](const FString& Chosen)
			{
				if (UMobileAppWidget* Screen = WeakExplore.Get())
				{
					Screen->OpenSite(Chosen);
				}
			});
		}
		ExploreOverlay->SetPadding(FMargin(0.f));
		ExploreOverlay->SetContent(ExplorePage);
		SavedOverlay = MakeOverlay(TEXT("SavedOverlay"), TEXT("Saved"), TEXT("The places you saved."));
		// The Saved tab is the list of saved places, not a placeholder.
		SavedPage = CreateWidget<UMobileSavedPageWidget>(this, UMobileSavedPageWidget::StaticClass());
		SavedPage->SetSource(Source.Get());
		{
			TWeakObjectPtr<UMobileAppWidget> WeakSaved(this);
			SavedPage->SetOnPlaceChosen([WeakSaved](const FMobilePlace& Place)
			{
				if (UMobileAppWidget* Screen = WeakSaved.Get())
				{
					Screen->OpenSavedPlace(Place);
				}
			});
			SavedPage->SetOnRemove([WeakSaved](const FMobilePlace& Place)
			{
				if (UMobileAppWidget* Screen = WeakSaved.Get())
				{
					Screen->SavedPlaces.Remove(Place.Id);
					Screen->RefreshSaved();
					Screen->SelectPlace(Screen->SelectedPlace);
					Screen->RefreshArrived();
				}
			});
		}
		SavedOverlay->SetPadding(FMargin(0.f));
		SavedOverlay->SetContent(SavedPage);
		RefreshSaved();
		ProfileOverlay = MakeOverlay(TEXT("ProfileOverlay"), TEXT("Profile"), TEXT("Settings and preferences."));
		// The Profile tab is the page of preferences, not a placeholder.
		ProfilePage = CreateWidget<UMobileProfilePageWidget>(this, UMobileProfilePageWidget::StaticClass());
		ProfilePage->SetPreferences(Preferences);
		ProfilePage->SetNote(Source->GetDataNote());
		{
			TWeakObjectPtr<UMobileAppWidget> WeakProfile(this);
			ProfilePage->SetOnThemeChosen([WeakProfile](EMobileTheme Theme)
			{
				if (UMobileAppWidget* Screen = WeakProfile.Get())
				{
					Screen->Preferences.SetTheme(Theme);
					Screen->SyncAppearance();
				}
			});
			ProfilePage->SetOnLargerTextToggled([WeakProfile](bool bLarger)
			{
				if (UMobileAppWidget* Screen = WeakProfile.Get())
				{
					Screen->Preferences.SetLargerText(bLarger);
					Screen->SyncAppearance();
				}
			});
			ProfilePage->SetOnReset([WeakProfile]()
			{
				if (UMobileAppWidget* Screen = WeakProfile.Get())
				{
					Screen->ResetSavedAndPreferences();
				}
			});
		}
		ProfileOverlay->SetPadding(FMargin(0.f));
		ProfileOverlay->SetContent(ProfilePage);

		// The header, on every tab as in the prototype: the app's name with a small logo (a placeholder until the real one is in).
		HeaderBorder = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Header"));
		HeaderBorder->SetBrush(Rounded(White, Line, 0.f));
		HeaderBorder->SetBrushColor(FLinearColor::White);
		HeaderBorder->SetPadding(FMargin(16.f, 0.f));
		UHorizontalBox* HeaderRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HeaderRow"));
		HeaderBorder->SetContent(HeaderRow);
		UBorder* HeaderMark = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HeaderLogo"));
		HeaderMark->SetBrush(Rounded(Blue, FLinearColor::Transparent, 7.f, 0.f));
		HeaderMark->SetBrushColor(FLinearColor::White);
		HeaderMark->SetHorizontalAlignment(HAlign_Center);
		HeaderMark->SetVerticalAlignment(VAlign_Center);
		HeaderMark->SetContent(MakeText(Tree, TEXT("Txt_HeaderLogo"), TEXT("N"), 14.f, OnBlue, true));
		USizeBox* HeaderMarkSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("HeaderLogoSize"));
		HeaderMarkSize->SetWidthOverride(24.f);
		HeaderMarkSize->SetHeightOverride(24.f);
		HeaderMarkSize->SetContent(HeaderMark);
		HeaderRow->AddChildToHorizontalBox(HeaderMarkSize)->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* BrandSlot = HeaderRow->AddChildToHorizontalBox(MakeText(Tree, TEXT("Txt_HeaderBrand"), TEXT("InNav"), 16.f, Ink, true));
		BrandSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		BrandSlot->SetVerticalAlignment(VAlign_Center);
		UCanvasPanelSlot* HeaderSlot = Phone->AddChildToCanvas(HeaderBorder);
		HeaderSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
		HeaderSlot->SetAlignment(FVector2D(0.f, 0.f));

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
			UTextBlock* Label = MakeText(Tree, Name + TEXT("_Label"), TabNames[Index], 12.f, Grey, true);
			TabContent->AddChildToVerticalBox(Label)->SetHorizontalAlignment(HAlign_Center);
			UBorder* TabPad = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Name + TEXT("_Pad")));
			TabPad->SetBrushColor(FLinearColor::Transparent);
			TabPad->SetPadding(FMargin(0.f, 8.f));
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

		RefreshMap();
		SetActiveTab(ActiveTab);
		RefreshModeButtons();
		RefreshSheetPage();
		PositionSheet();
		SnapSheetNow();
		BuiltTheme = CurrentTheme;
		bBuiltLarger = bCurrentLarger;
	}
	return Super::RebuildWidget();
}

void UMobileAppWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (PendingRoute && PendingRoute->Step(0.003, CurrentRoute))
	{
		PendingRoute.Reset();
		if (RoutePage) RoutePage->SetRequest(ActiveRouteRequest, CurrentRoute);
		RefreshNavigation();
		RefreshMap();
	}
#if PLATFORM_ANDROID && !UE_BUILD_SHIPPING
	++PerfFrames;
	PerfSeconds += InDeltaTime;
	PerfWorst = FMath::Max(PerfWorst, static_cast<double>(InDeltaTime));
	PerfSlowFrames += InDeltaTime > 0.0168f ? 1 : 0;
	PerfVerySlowFrames += InDeltaTime > 0.0334f ? 1 : 0;
	if (PerfSeconds >= 2.0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MobilePerf] frames=%d avg=%.1f ms (%.0f fps) worst=%.1f ms over16.7=%d over33=%d tab=%d sheet=%.0f zoom=%.1f"),
			PerfFrames, PerfSeconds / PerfFrames * 1000.0, PerfFrames / PerfSeconds, PerfWorst * 1000.0, PerfSlowFrames, PerfVerySlowFrames,
			static_cast<int32>(ActiveTab), SheetHeight, MapView ? MapView->GetZoom() : 1.0);
		PerfFrames = 0;
		PerfSlowFrames = 0;
		PerfVerySlowFrames = 0;
		PerfSeconds = 0.0;
		PerfWorst = 0.0;
	}
#endif
	const FVector2D LocalSize = MyGeometry.GetLocalSize();
	if (LocalSize.X > 1.f && LocalSize.Y > 1.f)
	{
		ApplyViewportSize(LocalSize.X, LocalSize.Y);
	}
	if (bMapToolsOpen)
	{
		// A pinch changes the zoom without the screen hearing of it; the buttons follow.
		RefreshMapTools();
	}
#if PLATFORM_ANDROID || PLATFORM_IOS
	SafeAreaPollSeconds -= InDeltaTime;
	if (SafeAreaPollSeconds <= 0.f)
	{
		SafeAreaPollSeconds = 0.5f;
		ApplyDeviceSafeArea();
	}
#endif
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

void UMobileAppWidget::ApplyDeviceSafeArea()
{
	FDisplayMetrics Metrics;
	FDisplayMetrics::RebuildDisplayMetrics(Metrics);
	if (Metrics.PrimaryDisplayHeight <= 0)
	{
		return;
	}
	// The insets are in screen pixels; the screen is laid out in design pixels, DesignHeight of them over the display's height.
	const double PerPixel = DesignHeight / Metrics.PrimaryDisplayHeight;
	const double Top = Metrics.TitleSafePaddingSize.Y * PerPixel;
	const double Bottom = Metrics.TitleSafePaddingSize.W * PerPixel;
	if (!FMath::IsNearlyEqual(Top, SafeTop, 0.5) || !FMath::IsNearlyEqual(Bottom, SafeBottom, 0.5))
	{
		SetSafeInsets(Top, Bottom);
	}
}

double UMobileAppWidget::RestHeight(EMobileSheetSnap Target) const
{
	return MobileSheet::SnapHeight(Target, DesignHeight, TabBarHeight + SafeBottom, HeaderHeight + SafeTop);
}

void UMobileAppWidget::PositionSheet()
{
	if (!SheetBorder || !TabBarBorder || !TopBarBorder || !ControlsBox || !HeaderBorder)
	{
		return;
	}
	const double TabTotal = TabBarHeight + SafeBottom;
	const double HeaderTotal = HeaderHeight + SafeTop;
	const bool bMapPage = ActiveTab == ETab::Directory;
	const bool bOpen = SheetIsOpen();
	if (UCanvasPanelSlot* PanelSlot = CanvasSlot(SheetBorder))
	{
		PanelSlot->SetOffsets(FMargin(0.f, static_cast<float>(-TabTotal), 0.f, static_cast<float>(SheetHeight)));
	}
	if (UCanvasPanelSlot* PanelSlot = CanvasSlot(TabBarBorder))
	{
		PanelSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, static_cast<float>(TabTotal)));
	}
	TabBarBorder->SetPadding(FMargin(0.f, 0.f, 0.f, static_cast<float>(SafeBottom)));
	// The header takes the status bar's place too: its colour reaches the top of the screen and its content sits below the status bar.
	if (UCanvasPanelSlot* PanelSlot = CanvasSlot(HeaderBorder))
	{
		PanelSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, static_cast<float>(HeaderTotal)));
	}
	HeaderBorder->SetPadding(FMargin(16.f, static_cast<float>(SafeTop), 16.f, 0.f));
	if (UCanvasPanelSlot* PanelSlot = CanvasSlot(TopBarBorder))
	{
		PanelSlot->SetOffsets(FMargin(0.f, static_cast<float>(HeaderTotal + TopRowTop), 0.f, 0.f));
		PanelSlot->SetAutoSize(true);
	}
	if (UCanvasPanelSlot* PanelSlot = CanvasSlot(ControlsBox))
	{
		// Anchored at the top edge, moved down to sit just above the sheet.
		const double SheetTop = DesignHeight - TabTotal - SheetHeight;
		PanelSlot->SetPosition(FVector2D(-12.0, SheetTop - ControlsGap));
	}
	for (UBorder* Overlay : { ExploreOverlay, SavedOverlay, ProfileOverlay })
	{
		if (UCanvasPanelSlot* PanelSlot = CanvasSlot(Overlay))
		{
			PanelSlot->SetOffsets(FMargin(0.f, static_cast<float>(HeaderTotal), 0.f, static_cast<float>(TabTotal)));
		}
	}
	if (MapView)
	{
		// The plan fits between the top row and the toolbar, above the sheet at the height it has now: it grows and shrinks as the sheet is dragged,
		// as in the prototype, and is not shown while the sheet is open to its top.
		MapView->SetFitInsets(HeaderTotal + TopRowTop + TopRowHeight + 4.0, TabTotal + SheetHeight + ControlsGap + ControlsHeight + 8.0);
		MapView->SetVisibility(bOpen ? ESlateVisibility::Hidden : ESlateVisibility::Visible);
	}
	SheetBorder->SetVisibility(bMapPage ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	ControlsBox->SetVisibility(bMapPage && ControlsFit() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	TopBarBorder->SetVisibility(bMapPage ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

bool UMobileAppWidget::SheetIsOpen() const
{
	// The prototype hides the plan and the toolbar from 80 percent of the space between the header and the tab bar.
	const double Workspace = DesignHeight - (HeaderHeight + SafeTop) - (TabBarHeight + SafeBottom);
	return SheetHeight >= 0.8 * Workspace;
}

bool UMobileAppWidget::ControlsFit() const
{
	return !SheetIsOpen();
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
	Snapshot.HeaderBottom = HeaderHeight + SafeTop;
	Snapshot.TopBarBottom = Snapshot.HeaderBottom + TopRowTop + TopRowHeight;
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
	if (Event == EMobileSheetEvent::NavigationStarted && !CurrentRoute.bFound) return;
	if (Flow.Handle(Event))
	{
		Snap = MobileSheet::SnapForState(Flow.GetState());
		RefreshSheetPage();
		// Arriving at the route page (from the place, from walking, from choosing a place) finds the route again for what is chosen now.
		if (Flow.GetState() == EMobileSheetState::Route)
		{
			RefreshRoute();
		}
		else
		{
			PendingRoute.Reset();
		}
		// Setting off starts at the first instruction; arriving shows the place.
		if (Flow.GetState() == EMobileSheetState::Navigating && Event == EMobileSheetEvent::NavigationStarted)
		{
			NavStepIndex = 0;
		}
		RefreshNavigation();
		RefreshArrived();
		RefreshMap();
	}
}

void UMobileAppWidget::AdvanceNavigation()
{
	if (Flow.GetState() != EMobileSheetState::Navigating)
	{
		return;
	}
	if (NavStepIndex < CurrentRoute.Steps.Num() - 1)
	{
		++NavStepIndex;
		RefreshNavigation();
		RefreshMap();
	}
	else
	{
		// The last instruction was the arrival.
		SendSheetEvent(EMobileSheetEvent::ArrivedAtDestination);
	}
}

void UMobileAppWidget::RefreshNavigation()
{
	if (NavigatePage)
	{
		NavigatePage->SetNavigation(SelectedPlace, CurrentRoute, NavStepIndex);
	}
}

void UMobileAppWidget::RefreshArrived()
{
	if (ArrivedPage)
	{
		ArrivedPage->SetPlace(SelectedPlace, SavedPlaces.Contains(SelectedPlace.Id));
	}
}

void UMobileAppWidget::RefreshRoute()
{
	if (!RoutePage || !Source.IsValid() || SelectedPlace.Id.IsEmpty())
	{
		return;
	}
	FMobileRouteRequest Request;
	Request.FromPlaceId = StartPlaceId;
	Request.ToPlaceId = SelectedPlace.Id;
	Request.Preferences = RoutePreferences;
	Request.MyLocationBuildingId = MyLocationBuildingId;
	Request.MyLocationLevel = MyLocationLevel;
	ActiveRouteRequest = Request;
	PendingRoute.Reset();
	PendingRoute = Source->BeginRoute(Request);
	CurrentRoute = PendingRoute ? FMobileRoute() : Source->FindRoute(Request);
	RoutePage->SetRequest(Request, CurrentRoute, PendingRoute.IsValid());
	RefreshMap();
}

void UMobileAppWidget::NativeDestruct()
{
	PendingRoute.Reset();
	Super::NativeDestruct();
}

void UMobileAppWidget::ApplyRouteRequest(const FMobileRouteRequest& Changed)
{
	StartPlaceId = Changed.FromPlaceId;
	RefreshSearchStart();
	RoutePreferences = Changed.Preferences;
	MyLocationBuildingId = Changed.MyLocationBuildingId;
	MyLocationLevel = Changed.MyLocationLevel;
	// A different destination chosen on the route page is the place the place page shows too.
	FMobilePlace Destination;
	if (Changed.ToPlaceId != SelectedPlace.Id && Source.IsValid() && Source->FindPlace(Changed.ToPlaceId, Destination))
	{
		SelectPlace(Destination);
	}
	RefreshRoute();
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
			TabIcons[Index]->SetBrush(Rounded(bActive ? Blue : IconGrey, FLinearColor::Transparent, 6.f, 0.f));
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
			Label->SetColorAndOpacity(FSlateColor(bOn ? OnBlue : Ink));
		}
	};
	Style(Mode2DButton, Mode2DLabel, !bShow3D);
	Style(Mode3DButton, Mode3DLabel, bShow3D);
}

UWidget* UMobileAppWidget::FindNamedWidget(const FName& Name) const
{
	return WidgetTree ? WidgetTree->FindWidget(Name) : nullptr;
}

void UMobileAppWidget::SetSource(TSharedPtr<IMobileNavSource> InSource, const FString& InitialSite)
{
	check(!WidgetTree || !WidgetTree->RootWidget);
	Source = MoveTemp(InSource); SiteId = InitialSite;
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
		Snap = MobileSheet::NearestSnap(SheetHeight, DesignHeight, TabBarHeight + SafeBottom, HeaderHeight + SafeTop);
	}
	else
	{
		// A tap on the handle: the next height.
		Snap = MobileSheet::NextSnap(MobileSheet::NearestSnap(SheetHeight, DesignHeight, TabBarHeight + SafeBottom, HeaderHeight + SafeTop));
	}
}

FVector2D UMobileAppWidget::ToDesign(const FGeometry& Geometry, const FVector2D& ScreenSpacePosition) const
{
	return Geometry.AbsoluteToLocal(ScreenSpacePosition) / FMath::Max(Scale, 1e-6);
}

FReply UMobileAppWidget::HandlePointerDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	// A second press while a drag is on (the mouse of the same click) is not a new drag.
	if (!bDraggingSheet && BeginSheetDrag(ToDesign(Geometry, Event.GetScreenSpacePosition())))
	{
		DragPointerIndex = Event.GetPointerIndex();
		bDragIsTouch = Event.IsTouchEvent();
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return FReply::Unhandled();
}

FReply UMobileAppWidget::HandlePointerMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (bDraggingSheet && Event.GetPointerIndex() == DragPointerIndex && Event.IsTouchEvent() == bDragIsTouch)
	{
		MoveSheetDrag(ToDesign(Geometry, Event.GetScreenSpacePosition()));
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply UMobileAppWidget::HandlePointerUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (bDraggingSheet && Event.GetPointerIndex() == DragPointerIndex && Event.IsTouchEvent() == bDragIsTouch)
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

void UMobileAppWidget::OnShow2DClicked() { SetMap3D(false); }
void UMobileAppWidget::OnShow3DClicked() { SetMap3D(true); }


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
	RefreshRoute();
	RefreshNavigation();
	RefreshArrived();
	RefreshMap();
	Snap = MobileSheet::SnapForState(Flow.GetState());
	RefreshSheetPage();
	SnapSheetNow();
}

void UMobileAppWidget::SelectPlace(const FMobilePlace& Place)
{
	if (SelectedPlace.Id != Place.Id)
	{
		PendingRoute.Reset();
		CurrentRoute = FMobileRoute();
	}
	SelectedPlace = Place;
	if (PlacePage)
	{
		PlacePage->SetPlace(Place, SavedPlaces.Contains(Place.Id), StartPlaceId == Place.Id);
	}
}

void UMobileAppWidget::ShowPlace(const FMobilePlace& Place)
{
	SelectPlace(Place);
	SendSheetEvent(EMobileSheetEvent::PlaceChosen);
}

void UMobileAppWidget::PreviewOpenSelect(const FString& Name)
{
	if (!RoutePage)
	{
		return;
	}
	UMobileSelectWidget* Select = Name == TEXT("To") ? RoutePage->GetToSelect() : Name == TEXT("Building") ? RoutePage->GetBuildingSelect() : Name == TEXT("Level") ? RoutePage->GetLevelSelect() : RoutePage->GetFromSelect();
	if (Select)
	{
		Select->ToggleOpen();
	}
}

// ---- The map ----

void UMobileAppWidget::RefreshMap()
{
	if (!MapView || !Source.IsValid())
	{
		return;
	}
	// What the sheet is about decides what the map shows; the person's own choice of level or overview lasts until the sheet moves on.
	const EMobileSheetState State = Flow.GetState();
	if (State != EMobileSheetState::Search)
	{
		bMapOverview = false;
	}
	auto Follow = [this](const FString& BuildingId, int32 Level)
	{
		if (!BuildingId.IsEmpty())
		{
			MapBuildingId = BuildingId;
			MapLevel = Level;
			bMapOverview = Level == 0;
		}
	};
	if (State == EMobileSheetState::Place || State == EMobileSheetState::Arrived)
	{
		Follow(SelectedPlace.BuildingId, SelectedPlace.Level);
	}
	else if ((State == EMobileSheetState::Route || State == EMobileSheetState::Navigating) && CurrentRoute.bFound && !CurrentRoute.Points.IsEmpty())
	{
		if (State == EMobileSheetState::Navigating && CurrentRoute.Steps.IsValidIndex(NavStepIndex))
		{
			// Walking: the level the person is on now.
			Follow(CurrentRoute.Steps[NavStepIndex].BuildingId, CurrentRoute.Steps[NavStepIndex].Level);
		}
		else
		{
			Follow(CurrentRoute.Points[0].BuildingId, CurrentRoute.Points[0].Level);
		}
	}
	else if (MapBuildingId.IsEmpty())
	{
		// At the start: the building the person is in.
		const TArray<FMobileBuilding> Buildings = Source->ListBuildings(SiteId);
		if (!Buildings.IsEmpty() && !Buildings[0].Levels.IsEmpty())
		{
			Follow(Buildings[0].Id, Buildings[0].Levels[0].Level);
		}
	}
	bLevelPopupOpen = false;
	ApplyMap();
}

void UMobileAppWidget::ApplyMap()
{
	if (!MapView || !Source.IsValid())
	{
		return;
	}
	const EMobileSheetState State = Flow.GetState();
	if (bMapOverview)
	{
		MapView->ShowOverview();
	}
	else
	{
		MapView->ShowLevel(MapBuildingId, MapLevel);
	}
	const bool bWalking = State == EMobileSheetState::Route || State == EMobileSheetState::Navigating;
	if (bWalking && CurrentRoute.bFound)
	{
		MapView->SetRoute(CurrentRoute);
	}
	else
	{
		MapView->ClearRoute();
	}
	const bool bShowsPlace = State == EMobileSheetState::Place || State == EMobileSheetState::Route || State == EMobileSheetState::Navigating || State == EMobileSheetState::Arrived;
	MapView->SetHighlightPlace(bShowsPlace ? SelectedPlace.Id : FString());

	// The dot: the step the person is at while walking, otherwise My location (at the door of the building they said they are in).
	if (State == EMobileSheetState::Navigating && CurrentRoute.bFound && CurrentRoute.Steps.IsValidIndex(NavStepIndex))
	{
		const FMobileStep& Step = CurrentRoute.Steps[NavStepIndex];
		FMobileRoutePoint Here;
		Here.BuildingId = Step.BuildingId;
		Here.Level = Step.Level;
		Here.Position = Step.Position;
		MapView->SetPosition(Here);
	}
	else if (!StartPlaceId.IsEmpty())
	{
		// Starting from a place: the dot is there.
		FMobilePlace Start;
		if (Source->FindPlace(StartPlaceId, Start))
		{
			FMobileRoutePoint Here;
			Here.BuildingId = Start.BuildingId;
			Here.Level = Start.Level;
			Here.Position = Start.Position;
			MapView->SetPosition(Here);
		}
		else
		{
			MapView->ClearPosition();
		}
	}
	else if (StartPlaceId.IsEmpty() && !MyLocationBuildingId.IsEmpty())
	{
		FMobileLevelMap Mine;
		FMobileRoutePoint Here;
		Here.BuildingId = MyLocationBuildingId;
		Here.Level = MyLocationLevel;
		if (Source->GetLevelMap(MyLocationBuildingId, MyLocationLevel, Mine) && Mine.bHasEntrance)
		{
			Here.Position = Mine.Entrance;
			MapView->SetPosition(Here);
		}
		else MapView->ClearPosition();
	}
	else
	{
		MapView->ClearPosition();
	}

	// The level button says which level, and its list (when open) the building's levels, the top one first.
	if (LevelLabel)
	{
		FString Label = TEXT("Map");
		if (!bMapOverview)
		{
			for (const FMobileBuilding& Building : Source->ListBuildings(SiteId))
			{
				for (const FMobileLevel& Level : Building.Levels)
				{
					if (Building.Id == MapBuildingId && Level.Level == MapLevel)
					{
						Label = Level.Label;
					}
				}
			}
		}
		LevelLabel->SetText(FText::FromString(Label));
	}
	RefreshMapTools();
	RefreshSiteName();
	if (LevelPopupBorder && LevelPopupList)
	{
		LevelPopupBorder->SetVisibility(bLevelPopupOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		LevelPopupList->ClearChildren();
		if (bLevelPopupOpen)
		{
			for (const FMobileBuilding& Building : Source->ListBuildings(SiteId))
			{
				if (Building.Id != MapBuildingId)
				{
					continue;
				}
				for (int32 i = Building.Levels.Num() - 1; i >= 0; --i)
				{
					const FMobileLevel& Level = Building.Levels[i];
					const bool bChosen = Level.Level == MapLevel;
					UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
					Pad->SetBrushColor(FLinearColor::Transparent);
					Pad->SetPadding(FMargin(14.f, 9.f));
					Pad->SetHorizontalAlignment(HAlign_Center);
					Pad->SetContent(MakeText(WidgetTree, FString(), Level.Label, 14.f, bChosen ? OnBlue : Ink, true));
					UMobileTapTarget* Row = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
					Row->Setup(Pad, FString::FromInt(Level.Level), bChosen ? Blue : FLinearColor::Transparent, 8.f);
					TWeakObjectPtr<UMobileAppWidget> WeakThis(this);
					Row->SetOnTapped([WeakThis](const FString& Value)
					{
						if (UMobileAppWidget* Screen = WeakThis.Get())
						{
							Screen->ChooseMapLevel(FCString::Atoi(*Value));
						}
					});
					LevelPopupList->AddChildToVerticalBox(Row);
				}
			}
		}
	}
}

void UMobileAppWidget::SetLevelPopupOpen(bool bOpen)
{
	bLevelPopupOpen = bOpen && !bMapOverview;
	if (bLevelPopupOpen)
	{
		bMapToolsOpen = false;
	}
	ApplyMap();
}

void UMobileAppWidget::ChooseMapLevel(int32 Level)
{
	MapLevel = Level;
	bMapOverview = false;
	bLevelPopupOpen = false;
	ApplyMap();
}

void UMobileAppWidget::ToggleOverview()
{
	bMapOverview = !bMapOverview;
	bLevelPopupOpen = false;
	ApplyMap();
}

void UMobileAppWidget::OpenBuilding(const FString& BuildingId)
{
	if (!Source.IsValid())
	{
		return;
	}
	for (const FMobileBuilding& Building : Source->ListBuildings(SiteId))
	{
		if (Building.Id == BuildingId && !Building.Levels.IsEmpty())
		{
			MapBuildingId = BuildingId;
			MapLevel = BuildingId == MyLocationBuildingId ? MyLocationLevel : Building.Levels[0].Level;
			bMapOverview = false;
			bLevelPopupOpen = false;
			ApplyMap();
			return;
		}
	}
}

void UMobileAppWidget::LocateMe()
{
	if (!MyLocationBuildingId.IsEmpty())
	{
		MapBuildingId = MyLocationBuildingId;
		MapLevel = MyLocationLevel;
	}
	bMapOverview = false;
	if (MapView)
	{
		MapView->ResetView();
	}
	bLevelPopupOpen = false;
	ApplyMap();
}

void UMobileAppWidget::SetMap3D(bool bShow3DMap)
{
	bShow3D = bShow3DMap;
	if (MapView)
	{
		MapView->Set3D(bShow3D);
	}
	RefreshModeButtons();
}

void UMobileAppWidget::SetMapToolsOpen(bool bOpen)
{
	bMapToolsOpen = bOpen;
	if (bMapToolsOpen)
	{
		bLevelPopupOpen = false;
	}
	ApplyMap();
}

void UMobileAppWidget::ToggleMapGestures()
{
	bMapGestures = !bMapGestures;
	RefreshMapTools();
}

void UMobileAppWidget::ZoomMap(double Delta)
{
	if (MapView)
	{
		MapView->ZoomBy(Delta);
	}
	RefreshMapTools();
}

void UMobileAppWidget::RefreshMapTools()
{
	if (MapView)
	{
		MapView->SetGesturesEnabled(bMapGestures);
	}
	if (MapToolsBorder)
	{
		MapToolsBorder->SetVisibility(bMapToolsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (MapMoveButton)
	{
		const FLinearColor Fill = bMapGestures ? Blue : White;
		MapMoveButton->SetStyle(StartStyle::Button(Fill, Fill * 0.94f, Fill * 0.86f, Line, 8.f / StartStyle::DesignToUi));
	}
	if (MapMoveLabel)
	{
		MapMoveLabel->SetText(FText::FromString(bMapGestures ? TEXT("Move: On") : TEXT("Move: Off")));
		MapMoveLabel->SetColorAndOpacity(FSlateColor(bMapGestures ? OnBlue : Ink));
	}
	// The zoom buttons stop at the ends of the range.
	const double Zoom = MapView ? MapView->GetZoom() : 1.0;
	const bool bCanIn = Zoom < UMobileMapWidget::MaxZoom - 1e-6;
	const bool bCanOut = Zoom > UMobileMapWidget::MinZoom + 1e-6;
	if (ZoomInButton) ZoomInButton->SetIsEnabled(bCanIn);
	if (ZoomOutButton) ZoomOutButton->SetIsEnabled(bCanOut);
	if (ZoomInLabel) ZoomInLabel->SetColorAndOpacity(FSlateColor(bCanIn ? Ink : Grey));
	if (ZoomOutLabel) ZoomOutLabel->SetColorAndOpacity(FSlateColor(bCanOut ? Ink : Grey));
}

void UMobileAppWidget::OnMapMoreClicked() { SetMapToolsOpen(!bMapToolsOpen); }
void UMobileAppWidget::OnMapMoveClicked() { ToggleMapGestures(); }
void UMobileAppWidget::OnZoomInClicked() { ZoomMap(UMobileMapWidget::ZoomStep); }
void UMobileAppWidget::OnZoomOutClicked() { ZoomMap(-UMobileMapWidget::ZoomStep); }

void UMobileAppWidget::OnOverviewClicked() { ToggleOverview(); }
void UMobileAppWidget::OnLevelClicked() { SetLevelPopupOpen(!bLevelPopupOpen); }
void UMobileAppWidget::OnLocateClicked() { LocateMe(); }

void UMobileAppWidget::RefreshSearchStart()
{
	if (!SearchPage)
	{
		return;
	}
	FMobilePlace Start;
	if (!StartPlaceId.IsEmpty() && Source.IsValid() && Source->FindPlace(StartPlaceId, Start))
	{
		SearchPage->SetStartLabel(Start.Name + TEXT(" \u00B7 ") + Start.GroupLabel());
	}
	else
	{
		SearchPage->SetStartLabel(FString());
	}
}

void UMobileAppWidget::OpenSite(const FString& NewSiteId)
{
	if (!Source.IsValid())
	{
		return;
	}
	const TArray<FMobileBuilding> Buildings = Source->ListBuildings(NewSiteId);
	if (Buildings.IsEmpty())
	{
		return;
	}
	// Everything starts again for the site: no place, no start, no route, the sheet at "Where to?", My location in its first building.
	SiteId = NewSiteId;
	Flow.Reset();
	PendingRoute.Reset();
	ActiveRouteRequest = FMobileRouteRequest();
	SelectedPlace = FMobilePlace();
	StartPlaceId.Reset();
	CurrentRoute = FMobileRoute();
	RoutePreferences = FMobileRoutePreferences();
	NavStepIndex = 0;
	bMapOverview = false;
	bLevelPopupOpen = false;
	FString FirstBuilding = Buildings[0].Id;
	int32 FirstLevel = Buildings[0].Levels.IsEmpty() ? 1 : Buildings[0].Levels[0].Level;
	const auto Places = Source->SearchPlaces(SiteId, FString(), FMobilePlaceFilter());
	if (!Places.IsEmpty()) { FirstBuilding = Places[0].BuildingId; FirstLevel = Places[0].Level; }
	MyLocationBuildingId = FirstBuilding;
	MyLocationLevel = FirstLevel;
	MapBuildingId = FirstBuilding;
	MapLevel = FirstLevel;
	if (SearchPage)
	{
		SearchPage->SetSource(Source.Get(), SiteId, FirstBuilding, FirstLevel);
	}
	RefreshSearchStart();
	if (RoutePage)
	{
		RoutePage->SetChoices(Source->SearchPlaces(SiteId, FString(), FMobilePlaceFilter()), Buildings);
		RoutePage->SetRequest(FMobileRouteRequest(), FMobileRoute());
	}
	if (PlacePage)
	{
		PlacePage->SetPlace(FMobilePlace(), false, false);
	}
	RefreshNavigation();
	RefreshArrived();
	if (MapView)
	{
		MapView->SetSource(Source.Get(), SiteId);
		MapView->ResetView();
	}
	RefreshSiteName();
	Snap = MobileSheet::SnapForState(Flow.GetState());
	RefreshSheetPage();
	SnapSheetNow();
	RefreshMap();
	SetActiveTab(ETab::Directory);
}

// ---- Saved places ----

void UMobileAppWidget::RefreshSaved()
{
	if (SavedPage)
	{
		SavedPage->SetSaved(SavedPlaces.Ids());
	}
}

void UMobileAppWidget::UseSavedFile(const FString& Path)
{
	SavedPlaces.UseFile(Path);
	RefreshSaved();
	SelectPlace(SelectedPlace);
	RefreshArrived();
}

void UMobileAppWidget::UsePreferencesFile(const FString& Path)
{
	Preferences.UseFile(Path);
	if (ProfilePage)
	{
		ProfilePage->SetPreferences(Preferences);
	}
	SyncAppearance();
}

void UMobileAppWidget::ResetSavedAndPreferences()
{
	SavedPlaces.Clear();
	Preferences.Reset();
	RefreshSaved();
	SelectPlace(SelectedPlace);
	RefreshArrived();
	if (ProfilePage)
	{
		ProfilePage->SetPreferences(Preferences);
	}
	SyncAppearance();
}

// ---- The look: theme and text size ----

void UMobileAppWidget::SyncAppearance()
{
	ApplyAppearance(Preferences.GetTheme(), Preferences.IsLargerText());
	// A screen that is not built yet is built in the look that is open now; one that is built in another look is built again.
	if (SearchPage && (BuiltTheme != CurrentTheme || bBuiltLarger != bCurrentLarger))
	{
		QueueAppearanceRebuild();
	}
}

void UMobileAppWidget::QueueAppearanceRebuild()
{
	if (bAppearanceRebuildQueued)
	{
		return;
	}
	bAppearanceRebuildQueued = true;
	TWeakObjectPtr<UMobileAppWidget> WeakThis(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
	{
		if (UMobileAppWidget* Screen = WeakThis.Get())
		{
			if (Screen->bAppearanceRebuildQueued)
			{
				Screen->RebuildForAppearance();
			}
		}
		return false;
	}));
}

void UMobileAppWidget::RebuildForAppearance()
{
	bAppearanceRebuildQueued = false;
	ApplyAppearance(Preferences.GetTheme(), Preferences.IsLargerText());
	if (!SearchPage)
	{
		return;
	}
	// What is not kept in the members the build starts from: the map's level and where My location is (the build puts both back at the first
	// building), and the line the Profile tab shows.
	const FString KeptMapBuilding = MapBuildingId;
	const int32 KeptMapLevel = MapLevel;
	const FString KeptMyBuilding = MyLocationBuildingId;
	const int32 KeptMyLevel = MyLocationLevel;
	const FString KeptStatus = ProfilePage ? ProfilePage->GetStatusString() : FString();

	const bool bInViewport = IsInViewport();
	if (bInViewport)
	{
		RemoveFromParent();
	}
	ReleaseSlateResources(true);
	WidgetTree = NewObject<UWidgetTree>(this, UWidgetTree::StaticClass(), NAME_None, RF_Transient);
	if (bInViewport)
	{
		AddToViewport();
	}
	else
	{
		TakeWidget();
	}

	// The members that say where the person is in the app stayed; the new pages are told.
	MapBuildingId = KeptMapBuilding;
	MapLevel = KeptMapLevel;
	MyLocationBuildingId = KeptMyBuilding;
	MyLocationLevel = KeptMyLevel;
	RefreshSiteName();
	RefreshSearchStart();
	if (!SelectedPlace.Id.IsEmpty())
	{
		SelectPlace(SelectedPlace);
		if (Flow.GetState() == EMobileSheetState::Route) RefreshRoute();
		else if (RoutePage) RoutePage->SetRequest(ActiveRouteRequest, CurrentRoute);
	}
	RefreshNavigation();
	RefreshArrived();
	RefreshSaved();
	if (MapView)
	{
		MapView->Set3D(bShow3D);
	}
	RefreshModeButtons();
	RefreshSheetPage();
	RefreshMap();
	SnapSheetNow();
	SetActiveTab(ActiveTab);
	if (ProfilePage)
	{
		ProfilePage->SetPreferences(Preferences);
		ProfilePage->ShowStatus(KeptStatus);
	}
}

void UMobileAppWidget::RefreshSiteName()
{
	if (!Source.IsValid())
	{
		return;
	}
	// The title at the top left, as in the prototype: the overview shows the site's name; a level shows the building and the level ("E2 . L1",
	// "Paya Lebar Mall . L1").
	FString SiteName = SiteId;
	for (const FMobileSite& Site : Source->ListSites())
	{
		if (Site.Id == SiteId)
		{
			SiteName = Site.ShortName.IsEmpty() ? Site.Name : Site.ShortName;
		}
	}
	const TArray<FMobileBuilding> Buildings = Source->ListBuildings(SiteId);
	FString Title = SiteName;
	if (!bMapOverview)
	{
		for (const FMobileBuilding& Building : Buildings)
		{
			if (Building.Id != MapBuildingId)
			{
				continue;
			}
			FString LevelName;
			for (const FMobileLevel& Level : Building.Levels)
			{
				if (Level.Level == MapLevel)
				{
					LevelName = Level.Label;
				}
			}
			Title = (Building.Name.IsEmpty() ? Building.Id : Building.Name) + (LevelName.IsEmpty() ? FString() : TEXT(" · ") + LevelName);
		}
	}
	if (SiteNameText)
	{
		SiteNameText->SetText(FText::FromString(Title));
	}
	// Overview is only for a campus (more than one building); it says what a tap does: "Overview" from a level, "Current" from the overview.
	if (OverviewButton)
	{
		OverviewButton->SetVisibility(Buildings.Num() > 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (OverviewLabel)
	{
		OverviewLabel->SetText(FText::FromString(bMapOverview ? TEXT("Current") : TEXT("Overview")));
	}
}

void UMobileAppWidget::OpenSavedPlace(const FMobilePlace& Place)
{
	// Another site opens first (which starts the app again for it); then the place, from Where to?, on the map tab.
	if (Place.SiteId != SiteId)
	{
		OpenSite(Place.SiteId);
	}
	const FString KeptStart = StartPlaceId;
	Flow.Reset();
	StartPlaceId = KeptStart;
	SetActiveTab(ETab::Directory);
	RefreshSheetPage();
	ShowPlace(Place);
}

void UMobileAppWidget::PreviewSave(const FString& CommaIds)
{
	TArray<FString> Ids;
	CommaIds.ParseIntoArray(Ids, TEXT(","));
	for (int32 i = Ids.Num() - 1; i >= 0; --i)
	{
		SavedPlaces.Add(Ids[i]);
	}
	RefreshSaved();
}
