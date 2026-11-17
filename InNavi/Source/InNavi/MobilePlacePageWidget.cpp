#include "MobilePlacePageWidget.h"

#include "MobileIcons.h"
#include "MobileStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace MobileStyle;

namespace
{

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

		// The name and below it the code and where it is, with a close button (a circle with a cross) at the right: it goes back, as the back arrow did.
		UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PlaceHeader"));
		UVerticalBox* Titles = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PlaceTitles"));
		TitleText = MakeText(Tree, TEXT("Txt_PlaceTitle"), TEXT(""), 17.f, Ink, true);
		SubtitleText = MakeText(Tree, TEXT("Txt_PlaceSubtitle"), TEXT(""), 12.f, Grey);
		Titles->AddChildToVerticalBox(TitleText);
		Titles->AddChildToVerticalBox(SubtitleText)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		UHorizontalBoxSlot* TitlesSlot = Header->AddChildToHorizontalBox(Titles);
		TitlesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitlesSlot->SetVerticalAlignment(VAlign_Center);
		UButton* Back = MobileIcons::CloseButton(Tree, TEXT("Btn_PlaceBack"));
		Back->OnClicked.AddUniqueDynamic(this, &UMobilePlacePageWidget::HandleBackClicked);
		Header->AddChildToHorizontalBox(Back)->SetVerticalAlignment(VAlign_Top);
		Page->AddChildToVerticalBox(Header)->SetPadding(FMargin(16.f, 0.f, 4.f, 0.f));

		// The part that scrolls: the photo, the pills, the description and the two secondary buttons.
		UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("PlaceScroll"));
		UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PlaceContent"));
		NoScrollShadow(Scroll);
		Scroll->AddChild(Content);

		UBorder* Photo = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PlacePhoto"));
		Photo->SetBrush(Rounded(SoftPanel, Line, 14.f));
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

		SaveButton = MobileIcons::IconLabelButton(Tree, TEXT("Btn_SavePlace"), TEXT("Save place"), TEXT("Heart"), White, Ink, 12.f, 38.f, &SaveLabel, &SaveHeart);
		SaveButton->OnClicked.AddUniqueDynamic(this, &UMobilePlacePageWidget::HandleSaveClicked);
		Content->AddChildToVerticalBox(SaveButton)->SetPadding(FMargin(16.f, 12.f, 16.f, 0.f));
		StartButton = MakeButton(Tree, TEXT("Btn_UseAsStart"), TEXT("Use as starting point"), White, Blue, 12.f, 38.f, &StartLabel);
		StartButton->OnClicked.AddUniqueDynamic(this, &UMobilePlacePageWidget::HandleStartClicked);
		Content->AddChildToVerticalBox(StartButton)->SetPadding(FMargin(16.f, 8.f, 16.f, 12.f));
		Page->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		// Get directions stays at the bottom whatever the height of the sheet.
		UButton* Directions = MobileIcons::IconLabelButton(Tree, TEXT("Btn_GetDirections"), bAddStop ? TEXT("Add stop") : TEXT("Get directions"), TEXT("Location"), Blue, OnBlue, 12.f, 48.f, &DirectionsLabel, nullptr);
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
	SubtitleText->SetText(FText::FromString(Place.WithCode(Place.GroupLabel())));
	DescriptionText->SetText(FText::FromString(Place.Description));
	DescriptionText->SetVisibility(Place.Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	PhotoText->SetText(FText::FromString(Place.PhotoPath.IsEmpty() ? TEXT("No photo") : TEXT("Photo")));

	// What kind of place, and whether it can be used.
	PillRow->ClearChildren();
	if (!Place.Category.IsEmpty())
	{
		UHorizontalBoxSlot* CategorySlot = PillRow->AddChildToHorizontalBox(MakeText(WidgetTree, FString(), Place.Category, 14.f, Ink));
		CategorySlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		CategorySlot->SetVerticalAlignment(VAlign_Center);
	}
	PillRow->AddChildToHorizontalBox(Place.bAvailable ? MakePill(WidgetTree, TEXT("Available"), GreenSoft, Green) : MakePill(WidgetTree, TEXT("Unavailable"), RedSoft, Red));

	// The two secondary buttons say what they have done. Saved is a white button with a red heart that offers to remove the place (as in the prototype); the starting
	// point is a blue one that says so.
	{
		SaveLabel->SetText(FText::FromString(bSaved ? TEXT("Remove from saved") : TEXT("Save place")));
		SaveButton->SetStyle(StartStyle::Button(White, White * 0.94f, White * 0.86f, FieldEdge, 12.f / StartStyle::DesignToUi));
		SaveLabel->SetColorAndOpacity(FSlateColor(Ink));
		if (SaveHeart)
		{
			SaveHeart->SetColorAndOpacity(bSaved ? RedHeart : Ink);
		}
		StartLabel->SetText(FText::FromString(bIsStart ? TEXT("Starting point") : TEXT("Use as starting point")));
		const FLinearColor Fill = bIsStart ? Blue : White;
		StartButton->SetStyle(StartStyle::Button(Fill, Fill * 0.94f, Fill * 0.86f, bIsStart ? Line : FieldEdge, 12.f / StartStyle::DesignToUi));
		StartLabel->SetColorAndOpacity(FSlateColor(bIsStart ? OnBlue : Blue));
	}
}

void UMobilePlacePageWidget::HandleBackClicked()
{
	if (OnBack)
	{
		OnBack();
	}
}

void UMobilePlacePageWidget::SetAddStopMode(bool bInAddStop)
{
	bAddStop = bInAddStop;
	if (DirectionsLabel)
	{
		DirectionsLabel->SetText(FText::FromString(bAddStop ? TEXT("Add stop") : TEXT("Get directions")));
	}
}

FString UMobilePlacePageWidget::GetDirectionsLabelString() const
{
	return DirectionsLabel ? DirectionsLabel->GetText().ToString() : FString();
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
