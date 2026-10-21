#include "MobileExplorePageWidget.h"

#include "MobileStyle.h"
#include "MobileTapTarget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace MobileStyle;

TSharedRef<SWidget> UMobileExplorePageWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		UVerticalBox* Page = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ExplorePage"));
		Tree->RootWidget = Page;

		// The app's name, with a small logo (a placeholder until the real one is in).
		UHorizontalBox* Brand = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ExploreBrand"));
		UBorder* Logo = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ExploreLogo"));
		Logo->SetBrush(Rounded(Blue, FLinearColor::Transparent, 7.f, 0.f));
		Logo->SetBrushColor(FLinearColor::White);
		Logo->SetHorizontalAlignment(HAlign_Center);
		Logo->SetVerticalAlignment(VAlign_Center);
		Logo->SetContent(MakeText(Tree, TEXT("Txt_ExploreLogo"), TEXT("N"), 14.f, White, true));
		USizeBox* LogoSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ExploreLogoSize"));
		LogoSize->SetWidthOverride(26.f);
		LogoSize->SetHeightOverride(26.f);
		LogoSize->SetContent(Logo);
		Brand->AddChildToHorizontalBox(LogoSize)->SetVerticalAlignment(VAlign_Center);
		Brand->AddChildToHorizontalBox(MakeText(Tree, TEXT("Txt_ExploreBrand"), TEXT("InNav"), 16.f, Ink, true))->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
		Page->AddChildToVerticalBox(Brand)->SetPadding(FMargin(16.f, 14.f, 16.f, 0.f));

		UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ExploreScroll"));
		UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ExploreContent"));
		Scroll->AddChild(Content);
		Content->AddChildToVerticalBox(MakeText(Tree, TEXT("Txt_ExploreTitle"), TEXT("Find your way indoors"), 26.f, Ink, true))->SetPadding(FMargin(16.f, 22.f, 16.f, 0.f));
		Content->AddChildToVerticalBox(MakeText(Tree, TEXT("Txt_ExploreSubtitle"), TEXT("Choose a site. Find your destination."), 13.f, Grey))->SetPadding(FMargin(16.f, 6.f, 16.f, 0.f));

		// "Sites", and a pill when the data is simulated.
		UHorizontalBox* Heading = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ExploreHeading"));
		Heading->AddChildToHorizontalBox(MakeText(Tree, TEXT("Txt_ExploreSites"), TEXT("Sites"), 15.f, Ink, true))->SetVerticalAlignment(VAlign_Center);
		Heading->AddChildToHorizontalBox(Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("ExploreHeadingSpacer")))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NotePill = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ExploreNotePill"));
		NotePill->SetBrush(Rounded(StartStyle::Rgb(241, 245, 249), Line, 12.f));
		NotePill->SetBrushColor(FLinearColor::White);
		NotePill->SetPadding(FMargin(10.f, 4.f));
		NotePill->SetVisibility(ESlateVisibility::Collapsed);
		NoteText = MakeText(Tree, TEXT("Txt_ExploreMock"), TEXT("Mock locations"), 11.f, Grey, true);
		NotePill->SetContent(NoteText);
		Heading->AddChildToHorizontalBox(NotePill)->SetVerticalAlignment(VAlign_Center);
		Content->AddChildToVerticalBox(Heading)->SetPadding(FMargin(16.f, 22.f, 16.f, 0.f));

		CardList = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ExploreCards"));
		Content->AddChildToVerticalBox(CardList)->SetPadding(FMargin(16.f, 10.f, 16.f, 0.f));
		Page->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		Refresh();
	}
	return Super::RebuildWidget();
}

void UMobileExplorePageWidget::SetSource(IMobileNavSource* InSource)
{
	Source = InSource;
	Refresh();
}

UWidget* UMobileExplorePageWidget::FindNamedWidget(const FName& Name) const
{
	return WidgetTree ? WidgetTree->FindWidget(Name) : nullptr;
}

FString UMobileExplorePageWidget::GetCountLabel(const FMobileSite& Site) const
{
	// A campus counts its buildings; a single building its levels.
	return Site.BuildingCount > 1 ? FString::Printf(TEXT("%d buildings"), Site.BuildingCount) : FString::Printf(TEXT("%d levels"), Site.LevelCount);
}

void UMobileExplorePageWidget::ChooseSite(const FString& SiteId)
{
	if (OnSiteChosen)
	{
		OnSiteChosen(SiteId);
	}
}

void UMobileExplorePageWidget::AddCard(const FMobileSite& Site)
{
	UVerticalBox* Card = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	// A picture of the site (a placeholder drawing of buildings until there are pictures).
	UHorizontalBox* Skyline = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	const float Heights[] = { 38.f, 62.f, 48.f };
	for (int32 i = 0; i < 3; ++i)
	{
		UBorder* Block = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Block->SetBrush(Rounded(i == 1 ? StartStyle::Rgb(147, 179, 240) : StartStyle::Rgb(186, 206, 245), FLinearColor::Transparent, 4.f, 0.f));
		Block->SetBrushColor(FLinearColor::White);
		USizeBox* BlockSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		BlockSize->SetWidthOverride(i == 1 ? 46.f : 38.f);
		BlockSize->SetHeightOverride(Heights[i]);
		BlockSize->SetContent(Block);
		Skyline->AddChildToHorizontalBox(BlockSize)->SetPadding(FMargin(i == 0 ? 0.f : 6.f, 0.f, 0.f, 0.f));
	}
	UBorder* Picture = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Picture->SetBrush(Rounded(BlueSoft, FLinearColor::Transparent, 12.f, 0.f));
	Picture->SetBrushColor(FLinearColor::White);
	Picture->SetHorizontalAlignment(HAlign_Center);
	Picture->SetVerticalAlignment(VAlign_Bottom);
	Picture->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
	Picture->SetContent(Skyline);
	USizeBox* PictureSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	PictureSize->SetHeightOverride(104.f);
	PictureSize->SetContent(Picture);
	Card->AddChildToVerticalBox(PictureSize);

	// The name and an arrow, what it is, and the distance and how many buildings or levels.
	UHorizontalBox* NameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	NameRow->AddChildToHorizontalBox(MakeText(WidgetTree, FString(), Site.Name, 18.f, Ink, true))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	NameRow->AddChildToHorizontalBox(MakeText(WidgetTree, FString(), TEXT("›"), 22.f, Grey, true))->SetVerticalAlignment(VAlign_Center);
	Card->AddChildToVerticalBox(NameRow)->SetPadding(FMargin(2.f, 12.f, 2.f, 0.f));
	Card->AddChildToVerticalBox(MakeText(WidgetTree, FString(), Site.Subtitle, 12.f, Grey, true))->SetPadding(FMargin(2.f, 4.f, 2.f, 0.f));

	UHorizontalBox* Facts = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UBorder* Dot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Dot->SetBrush(Rounded(Blue, FLinearColor::Transparent, 5.f, 0.f));
	Dot->SetBrushColor(FLinearColor::White);
	USizeBox* DotSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	DotSize->SetWidthOverride(10.f);
	DotSize->SetHeightOverride(10.f);
	DotSize->SetContent(Dot);
	Facts->AddChildToHorizontalBox(DotSize)->SetVerticalAlignment(VAlign_Center);
	Facts->AddChildToHorizontalBox(MakeText(WidgetTree, FString(), Site.DistanceLabel, 13.f, Blue, true))->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
	Facts->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UBorder* Count = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Count->SetBrush(Rounded(White, Line, 12.f));
	Count->SetBrushColor(FLinearColor::White);
	Count->SetPadding(FMargin(10.f, 3.f));
	Count->SetContent(MakeText(WidgetTree, FString(), GetCountLabel(Site), 11.f, Ink, true));
	Facts->AddChildToHorizontalBox(Count)->SetVerticalAlignment(VAlign_Center);
	Card->AddChildToVerticalBox(Facts)->SetPadding(FMargin(2.f, 10.f, 2.f, 0.f));

	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Frame->SetBrush(Rounded(White, Line, 16.f));
	Frame->SetBrushColor(FLinearColor::White);
	Frame->SetPadding(FMargin(10.f, 10.f, 10.f, 14.f));
	Frame->SetContent(Card);
	UMobileTapTarget* Tap = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
	Tap->Setup(Frame, Site.Id, FLinearColor::Transparent, 16.f);
	TWeakObjectPtr<UMobileExplorePageWidget> WeakThis(this);
	Tap->SetOnTapped([WeakThis](const FString& Id)
	{
		if (UMobileExplorePageWidget* Self = WeakThis.Get())
		{
			Self->ChooseSite(Id);
		}
	});
	CardList->AddChildToVerticalBox(Tap)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
}

void UMobileExplorePageWidget::Refresh()
{
	ShownSites.Reset();
	bDataNoteShown = false;
	if (!CardList || !WidgetTree)
	{
		return;
	}
	CardList->ClearChildren();
	if (!Source)
	{
		return;
	}
	ShownSites = Source->ListSites();
	for (const FMobileSite& Site : ShownSites)
	{
		AddCard(Site);
	}
	// When the data is simulated: a pill by "Sites", and a line at the bottom saying what is.
	const FString Note = Source->GetDataNote();
	bDataNoteShown = !Note.IsEmpty();
	NotePill->SetVisibility(bDataNoteShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bDataNoteShown)
	{
		UTextBlock* Footer = MakeText(WidgetTree, FString(), Note, 11.f, Grey);
		Footer->SetAutoWrapText(true);
		CardList->AddChildToVerticalBox(Footer)->SetPadding(FMargin(2.f, 0.f, 2.f, 24.f));
	}
}
