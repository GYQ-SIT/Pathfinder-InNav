#include "MobileSavedPageWidget.h"

#include "MobileStyle.h"
#include "MobileTapTarget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace MobileStyle;

TSharedRef<SWidget> UMobileSavedPageWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		UVerticalBox* Page = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SavedPage"));
		Tree->RootWidget = Page;
		Page->AddChildToVerticalBox(MakeText(Tree, TEXT("Txt_SavedTitle"), TEXT("Saved"), 26.f, Ink, true))->SetPadding(FMargin(16.f, 22.f, 16.f, 0.f));
		CountText = MakeText(Tree, TEXT("Txt_SavedCount"), TEXT(""), 13.f, Grey);
		Page->AddChildToVerticalBox(CountText)->SetPadding(FMargin(16.f, 6.f, 16.f, 0.f));
		UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SavedScroll"));
		ListBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SavedList"));
		Scroll->AddChild(ListBox);
		Page->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Refresh();
	}
	return Super::RebuildWidget();
}

void UMobileSavedPageWidget::SetSource(IMobileNavSource* InSource)
{
	Source = InSource;
	Refresh();
}

void UMobileSavedPageWidget::SetSaved(const TArray<FString>& InIds)
{
	Ids = InIds;
	Refresh();
}

UWidget* UMobileSavedPageWidget::FindNamedWidget(const FName& Name) const
{
	return WidgetTree ? WidgetTree->FindWidget(Name) : nullptr;
}

FString UMobileSavedPageWidget::GetCountString() const
{
	return CountText ? CountText->GetText().ToString() : FString();
}

void UMobileSavedPageWidget::ChoosePlace(const FString& PlaceId)
{
	FMobilePlace Place;
	if (Source && Source->FindPlace(PlaceId, Place) && OnPlaceChosen)
	{
		OnPlaceChosen(Place);
	}
}

void UMobileSavedPageWidget::RemovePlace(const FString& PlaceId)
{
	FMobilePlace Place;
	if (Source && Source->FindPlace(PlaceId, Place) && OnRemove)
	{
		OnRemove(Place);
	}
}

void UMobileSavedPageWidget::AddRow(const FMobilePlace& Place)
{
	// The icon is a placeholder (the category's letter on a soft square) until the design team's icons are in.
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

	UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Texts->AddChildToVerticalBox(MakeText(WidgetTree, FString(), Place.Name, 15.f, Ink, true));
	Texts->AddChildToVerticalBox(MakeText(WidgetTree, FString(), Place.Code + TEXT(" · ") + Place.GroupLabel(), 11.f, Grey))->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
	UHorizontalBox* Info = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Info->AddChildToHorizontalBox(IconSize)->SetVerticalAlignment(VAlign_Center);
	UHorizontalBoxSlot* TextSlot = Info->AddChildToHorizontalBox(Texts);
	TextSlot->SetPadding(FMargin(12.f, 0.f, 8.f, 0.f));
	TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TextSlot->SetVerticalAlignment(VAlign_Center);
	UBorder* InfoPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	InfoPad->SetBrushColor(FLinearColor::Transparent);
	InfoPad->SetPadding(FMargin(16.f, 10.f, 4.f, 10.f));
	InfoPad->SetContent(Info);

	TWeakObjectPtr<UMobileSavedPageWidget> WeakThis(this);
	UMobileTapTarget* Open = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
	Open->Setup(InfoPad, Place.Id, FLinearColor::Transparent, 0.f);
	Open->SetOnTapped([WeakThis](const FString& Id)
	{
		if (UMobileSavedPageWidget* Self = WeakThis.Get())
		{
			Self->ChoosePlace(Id);
		}
	});

	// Remove sits beside it, a target of its own.
	UBorder* RemovePill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	RemovePill->SetBrush(Rounded(BlueSoft, FLinearColor::Transparent, 8.f, 0.f));
	RemovePill->SetBrushColor(FLinearColor::White);
	RemovePill->SetPadding(FMargin(10.f, 6.f));
	RemovePill->SetContent(MakeText(WidgetTree, FString(), TEXT("Remove"), 12.f, Blue, true));
	UMobileTapTarget* Remove = CreateWidget<UMobileTapTarget>(this, UMobileTapTarget::StaticClass());
	Remove->Setup(RemovePill, Place.Id, FLinearColor::Transparent, 8.f);
	Remove->SetOnTapped([WeakThis](const FString& Id)
	{
		if (UMobileSavedPageWidget* Self = WeakThis.Get())
		{
			Self->RemovePlace(Id);
		}
	});

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Row->AddChildToHorizontalBox(Open)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UHorizontalBoxSlot* RemoveSlot = Row->AddChildToHorizontalBox(Remove);
	RemoveSlot->SetPadding(FMargin(0.f, 0.f, 16.f, 0.f));
	RemoveSlot->SetVerticalAlignment(VAlign_Center);
	ListBox->AddChildToVerticalBox(Row);

	USizeBox* Divider = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Divider->SetHeightOverride(1.f);
	UBorder* Rule = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Rule->SetBrushColor(Line);
	Divider->SetContent(Rule);
	ListBox->AddChildToVerticalBox(Divider)->SetPadding(FMargin(16.f, 0.f));
}

void UMobileSavedPageWidget::Refresh()
{
	ShownPlaces.Reset();
	GroupCount = 0;
	bEmptyShown = false;
	if (!ListBox || !WidgetTree)
	{
		return;
	}
	ListBox->ClearChildren();
	if (Source)
	{
		for (const FString& Id : Ids)
		{
			FMobilePlace Place;
			if (Source->FindPlace(Id, Place))
			{
				ShownPlaces.Add(Place);
			}
		}
	}
	if (CountText)
	{
		CountText->SetText(FText::FromString(ShownPlaces.IsEmpty() ? FString() : FString::Printf(TEXT("%d place%s"), ShownPlaces.Num(), ShownPlaces.Num() == 1 ? TEXT("") : TEXT("s"))));
	}
	if (ShownPlaces.IsEmpty())
	{
		bEmptyShown = true;
		UVerticalBox* Message = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Message->AddChildToVerticalBox(MakeText(WidgetTree, FString(), TEXT("No saved places yet"), 16.f, Ink, true));
		UTextBlock* Hint = MakeText(WidgetTree, FString(), TEXT("Tap Save place on a place, and it is kept here."), 13.f, Grey);
		Hint->SetAutoWrapText(true);
		Message->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		ListBox->AddChildToVerticalBox(Message)->SetPadding(FMargin(16.f, 28.f, 16.f, 0.f));
		return;
	}

	// Grouped by site, in the order the sites first come up (the newest saved place's site first).
	TArray<FString> SiteOrder;
	for (const FMobilePlace& Place : ShownPlaces)
	{
		SiteOrder.AddUnique(Place.SiteId);
	}
	for (const FString& SiteId : SiteOrder)
	{
		FString SiteName = SiteId;
		for (const FMobileSite& Site : Source->ListSites())
		{
			if (Site.Id == SiteId)
			{
				SiteName = Site.Name;
			}
		}
		++GroupCount;
		UBorder* Heading = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Heading->SetBrushColor(FLinearColor::Transparent);
		Heading->SetPadding(FMargin(16.f, 18.f, 16.f, 4.f));
		Heading->SetContent(MakeText(WidgetTree, FString(), SiteName, 12.f, Grey, true));
		ListBox->AddChildToVerticalBox(Heading);
		for (const FMobilePlace& Place : ShownPlaces)
		{
			if (Place.SiteId == SiteId)
			{
				AddRow(Place);
			}
		}
	}
}
