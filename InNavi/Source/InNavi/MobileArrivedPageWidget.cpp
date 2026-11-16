#include "MobileArrivedPageWidget.h"

#include "MobileIcons.h"
#include "MobileStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace MobileStyle;

TSharedRef<SWidget> UMobileArrivedPageWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		UVerticalBox* Page = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ArrivedPage"));
		Tree->RootWidget = Page;

		// The title is centred with a close button (a circle with a cross) at the right; a spacer of the same width at the left keeps the title in the middle.
		UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ArrivedHeader"));
		USizeBox* LeftSpace = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ArrivedHeaderSpace"));
		LeftSpace->SetWidthOverride(44.f);
		Header->AddChildToHorizontalBox(LeftSpace);
		UTextBlock* Title = MakeText(Tree, TEXT("Txt_ArrivedTitle"), TEXT("You have arrived"), 24.f, Ink, true);
		Title->SetJustification(ETextJustify::Center);
		UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(Title);
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
		TitleSlot->SetVerticalAlignment(VAlign_Center);
		UButton* Close = MobileIcons::CloseButton(Tree, TEXT("Btn_ArrivedClose"));
		Close->OnClicked.AddUniqueDynamic(this, &UMobileArrivedPageWidget::HandleCloseClicked);
		Header->AddChildToHorizontalBox(Close)->SetVerticalAlignment(VAlign_Center);
		Page->AddChildToVerticalBox(Header)->SetPadding(FMargin(8.f, 0.f, 8.f, 0.f));

		PlaceText = MakeText(Tree, TEXT("Txt_ArrivedPlace"), TEXT(""), 14.f, Grey);
		PlaceText->SetAutoWrapText(true);
		PlaceText->SetJustification(ETextJustify::Center);
		Page->AddChildToVerticalBox(PlaceText)->SetPadding(FMargin(16.f, 2.f, 16.f, 0.f));

		UButton* Another = MakeButton(Tree, TEXT("Btn_FindAnother"), TEXT("Find another place"), Blue, OnBlue, 12.f, 48.f);
		Another->OnClicked.AddUniqueDynamic(this, &UMobileArrivedPageWidget::HandleFindAnotherClicked);
		Page->AddChildToVerticalBox(Another)->SetPadding(FMargin(16.f, 16.f, 16.f, 0.f));

		// Save this place: a heart and the words, dark on white with an outline (blue when it is saved).
		SaveButton = MobileIcons::IconLabelButton(Tree, TEXT("Btn_SaveThisPlace"), TEXT("Save this place"), TEXT("Heart"), White, Ink, 12.f, 48.f, &SaveLabel, &SaveHeart);
		SaveButton->OnClicked.AddUniqueDynamic(this, &UMobileArrivedPageWidget::HandleSaveClicked);
		Page->AddChildToVerticalBox(SaveButton)->SetPadding(FMargin(16.f, 8.f, 16.f, 0.f));

		UButton* View = MakeButton(Tree, TEXT("Btn_ViewDestination"), TEXT("View destination"), White, Blue, 12.f, 48.f);
		View->SetStyle(StartStyle::Button(White, White * 0.94f, White * 0.86f, FieldEdge, 12.f / StartStyle::DesignToUi));
		View->OnClicked.AddUniqueDynamic(this, &UMobileArrivedPageWidget::HandleViewClicked);
		Page->AddChildToVerticalBox(View)->SetPadding(FMargin(16.f, 8.f, 16.f, 12.f));

		Refresh();
	}
	return Super::RebuildWidget();
}

void UMobileArrivedPageWidget::SetPlace(const FMobilePlace& InPlace, bool bInSaved)
{
	Place = InPlace;
	bSaved = bInSaved;
	Refresh();
}

UWidget* UMobileArrivedPageWidget::FindNamedWidget(const FName& Name) const
{
	return WidgetTree ? WidgetTree->FindWidget(Name) : nullptr;
}

void UMobileArrivedPageWidget::Refresh()
{
	if (!PlaceText)
	{
		return;
	}
	PlaceText->SetText(FText::FromString(Place.Name.IsEmpty() ? FString() : Place.Name + TEXT(" · ") + Place.WithCode(FString::Printf(TEXT("Level %d"), Place.Level))));
	// A white button with a heart; saved, the heart is red and the button offers to remove the place.
	SaveLabel->SetText(FText::FromString(bSaved ? TEXT("Remove from saved") : TEXT("Save this place")));
	SaveButton->SetStyle(StartStyle::Button(White, White * 0.94f, White * 0.86f, FieldEdge, 12.f / StartStyle::DesignToUi));
	SaveLabel->SetColorAndOpacity(FSlateColor(Ink));
	if (SaveHeart)
	{
		SaveHeart->SetColorAndOpacity(bSaved ? RedHeart : Ink);
	}
}

void UMobileArrivedPageWidget::HandleFindAnotherClicked()
{
	if (OnFindAnother)
	{
		OnFindAnother();
	}
}

void UMobileArrivedPageWidget::HandleCloseClicked()
{
	if (OnClose)
	{
		OnClose();
	}
}

void UMobileArrivedPageWidget::HandleSaveClicked()
{
	if (OnSaveToggled)
	{
		OnSaveToggled(Place);
	}
}

void UMobileArrivedPageWidget::HandleViewClicked()
{
	if (OnViewDestination)
	{
		OnViewDestination();
	}
}

FString UMobileArrivedPageWidget::GetPlaceString() const { return PlaceText ? PlaceText->GetText().ToString() : FString(); }
FString UMobileArrivedPageWidget::GetSaveLabelString() const { return SaveLabel ? SaveLabel->GetText().ToString() : FString(); }
