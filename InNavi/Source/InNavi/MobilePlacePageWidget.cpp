#include "MobilePlacePageWidget.h"

#include "MobileStyle.h"

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

using namespace MobileStyle;

namespace
{
	const FLinearColor GreenSoft = StartStyle::Rgb(220, 252, 231);
	const FLinearColor Green = StartStyle::Rgb(22, 101, 52);

	UBorder* MakePill(UWidgetTree* Tree, const FString& Text, const FLinearColor& Fill, const FLinearColor& TextColor)
	{
		UBorder* Pill = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Pill->SetBrush(Rounded(Fill, FLinearColor::Transparent, 12.f, 0.f));
		Pill->SetBrushColor(FLinearColor::White);
		Pill->SetPadding(FMargin(10.f, 4.f));
		Pill->SetContent(MakeText(Tree, FString(), Text, 12.f, TextColor, true));
		return Pill;
	}
}

TSharedRef<SWidget> UMobilePlacePageWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UWidgetTree* Tree = WidgetTree;
		UVerticalBox* Page = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PlacePage"));
		Tree->RootWidget = Page;

		// The back arrow, then the name and below it the code and where it is.
		UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PlaceHeader"));
		UTextBlock* BackLabel = nullptr;
		UButton* Back = MakeButton(Tree, TEXT("Btn_PlaceBack"), TEXT("‹"), White, Blue, 10.f, 36.f, &BackLabel, 14.f);
		ApplyFont(BackLabel, 28.f, true);
		Back->OnClicked.AddUniqueDynamic(this, &UMobilePlacePageWidget::HandleBackClicked);
		Header->AddChildToHorizontalBox(Back)->SetVerticalAlignment(VAlign_Center);
		UVerticalBox* Titles = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PlaceTitles"));
		TitleText = MakeText(Tree, TEXT("Txt_PlaceTitle"), TEXT(""), 22.f, Ink, true);
		SubtitleText = MakeText(Tree, TEXT("Txt_PlaceSubtitle"), TEXT(""), 12.f, Grey);
		Titles->AddChildToVerticalBox(TitleText);
		Titles->AddChildToVerticalBox(SubtitleText)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		UHorizontalBoxSlot* TitlesSlot = Header->AddChildToHorizontalBox(Titles);
		TitlesSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		TitlesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitlesSlot->SetVerticalAlignment(VAlign_Center);
		Page->AddChildToVerticalBox(Header)->SetPadding(FMargin(16.f, 0.f, 16.f, 0.f));

		// The part that scrolls: the photo, the pills, the description and the two secondary buttons.
		UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("PlaceScroll"));
		UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PlaceContent"));
		Scroll->AddChild(Content);

		UBorder* Photo = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PlacePhoto"));
		Photo->SetBrush(Rounded(StartStyle::Rgb(241, 245, 249), Line, 14.f));
		Photo->SetBrushColor(FLinearColor::White);
		Photo->SetHorizontalAlignment(HAlign_Center);
		Photo->SetVerticalAlignment(VAlign_Center);
		PhotoText = MakeText(Tree, TEXT("Txt_PlacePhoto"), TEXT("No photo"), 13.f, Grey, true);
		Photo->SetContent(PhotoText);
		USizeBox* PhotoSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PlacePhotoSize"));
		PhotoSize->SetHeightOverride(132.f);
		PhotoSize->SetContent(Photo);
		Content->AddChildToVerticalBox(PhotoSize)->SetPadding(FMargin(16.f, 14.f, 16.f, 0.f));

		PillRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PlacePills"));
		Content->AddChildToVerticalBox(PillRow)->SetPadding(FMargin(16.f, 12.f, 16.f, 0.f));

		DescriptionText = MakeText(Tree, TEXT("Txt_PlaceDescription"), TEXT(""), 13.f, Grey);
		DescriptionText->SetAutoWrapText(true);
		Content->AddChildToVerticalBox(DescriptionText)->SetPadding(FMargin(16.f, 10.f, 16.f, 0.f));

		SaveButton = MakeButton(Tree, TEXT("Btn_SavePlace"), TEXT("Save place"), White, Blue, 12.f, 44.f, &SaveLabel);
		SaveButton->OnClicked.AddUniqueDynamic(this, &UMobilePlacePageWidget::HandleSaveClicked);
		Content->AddChildToVerticalBox(SaveButton)->SetPadding(FMargin(16.f, 16.f, 16.f, 0.f));
		StartButton = MakeButton(Tree, TEXT("Btn_UseAsStart"), TEXT("Use as starting point"), White, Blue, 12.f, 44.f, &StartLabel);
		StartButton->OnClicked.AddUniqueDynamic(this, &UMobilePlacePageWidget::HandleStartClicked);
		Content->AddChildToVerticalBox(StartButton)->SetPadding(FMargin(16.f, 8.f, 16.f, 12.f));
		Page->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		// Get directions stays at the bottom whatever the height of the sheet.
		UButton* Directions = MakeButton(Tree, TEXT("Btn_GetDirections"), TEXT("Get directions"), Blue, White, 12.f, 48.f);
		Directions->OnClicked.AddUniqueDynamic(this, &UMobilePlacePageWidget::HandleDirectionsClicked);
		Page->AddChildToVerticalBox(Directions)->SetPadding(FMargin(16.f, 8.f, 16.f, 12.f));

		Refresh();
	}
	return Super::RebuildWidget();
}

void UMobilePlacePageWidget::SetPlace(const FMobilePlace& InPlace, bool bInSaved, bool bInIsStart)
{
	Place = InPlace;
	bSaved = bInSaved;
	bIsStart = bInIsStart;
	Refresh();
}

UWidget* UMobilePlacePageWidget::FindNamedWidget(const FName& Name) const
{
	return WidgetTree ? WidgetTree->FindWidget(Name) : nullptr;
}

void UMobilePlacePageWidget::Refresh()
{
	if (!TitleText || !WidgetTree)
	{
		return;
	}
	TitleText->SetText(FText::FromString(Place.Name));
	SubtitleText->SetText(FText::FromString(Place.Code.IsEmpty() ? FString() : Place.Code + TEXT(" · ") + Place.GroupLabel()));
	DescriptionText->SetText(FText::FromString(Place.Description));
	DescriptionText->SetVisibility(Place.Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	PhotoText->SetText(FText::FromString(Place.PhotoPath.IsEmpty() ? TEXT("No photo") : TEXT("Photo")));

	// What kind of place, and whether it can be used.
	PillRow->ClearChildren();
	if (!Place.Category.IsEmpty())
	{
		PillRow->AddChildToHorizontalBox(MakePill(WidgetTree, Place.Category, BlueSoft, Blue))->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	}
	PillRow->AddChildToHorizontalBox(Place.bAvailable ? MakePill(WidgetTree, TEXT("Available"), GreenSoft, Green) : MakePill(WidgetTree, TEXT("Unavailable"), RedSoft, Red));

	// The two secondary buttons say what they have done: Saved, Starting point.
	auto Style = [](UButton* Button, UTextBlock* Label, const TCHAR* Off, const TCHAR* On, bool bOn)
	{
		Label->SetText(FText::FromString(bOn ? On : Off));
		const FLinearColor Fill = bOn ? Blue : White;
		Button->SetStyle(StartStyle::Button(Fill, Fill * 0.94f, Fill * 0.86f, Line, 12.f / StartStyle::DesignToUi));
		Label->SetColorAndOpacity(FSlateColor(bOn ? White : Blue));
	};
	Style(SaveButton, SaveLabel, TEXT("Save place"), TEXT("Saved"), bSaved);
	Style(StartButton, StartLabel, TEXT("Use as starting point"), TEXT("Starting point"), bIsStart);
}

void UMobilePlacePageWidget::HandleBackClicked()
{
	if (OnBack)
	{
		OnBack();
	}
}

void UMobilePlacePageWidget::HandleDirectionsClicked()
{
	if (OnDirections)
	{
		OnDirections(Place);
	}
}

void UMobilePlacePageWidget::HandleSaveClicked()
{
	if (OnSaveToggled)
	{
		OnSaveToggled(Place);
	}
}

void UMobilePlacePageWidget::HandleStartClicked()
{
	if (OnUseAsStart)
	{
		OnUseAsStart(Place);
	}
}
