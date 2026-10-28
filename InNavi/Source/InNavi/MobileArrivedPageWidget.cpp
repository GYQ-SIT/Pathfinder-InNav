#include "MobileArrivedPageWidget.h"

#include "MobileStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
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

		Page->AddChildToVerticalBox(MakeText(Tree, TEXT("Txt_ArrivedTitle"), TEXT("You have arrived"), 22.f, Ink, true))->SetPadding(FMargin(16.f, 4.f, 16.f, 0.f));
		PlaceText = MakeText(Tree, TEXT("Txt_ArrivedPlace"), TEXT(""), 13.f, Grey);
		PlaceText->SetAutoWrapText(true);
		Page->AddChildToVerticalBox(PlaceText)->SetPadding(FMargin(16.f, 4.f, 16.f, 0.f));

		UButton* Another = MakeButton(Tree, TEXT("Btn_FindAnother"), TEXT("Find another place"), Blue, OnBlue, 12.f, 48.f);
		Another->OnClicked.AddUniqueDynamic(this, &UMobileArrivedPageWidget::HandleFindAnotherClicked);
		Page->AddChildToVerticalBox(Another)->SetPadding(FMargin(16.f, 16.f, 16.f, 0.f));
		SaveButton = MakeButton(Tree, TEXT("Btn_SaveThisPlace"), TEXT("Save this place"), White, Blue, 12.f, 44.f, &SaveLabel);
		SaveButton->OnClicked.AddUniqueDynamic(this, &UMobileArrivedPageWidget::HandleSaveClicked);
		Page->AddChildToVerticalBox(SaveButton)->SetPadding(FMargin(16.f, 8.f, 16.f, 0.f));
		UButton* View = MakeButton(Tree, TEXT("Btn_ViewDestination"), TEXT("View destination"), BlueSoft, Blue, 12.f, 44.f);
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
	SaveLabel->SetText(FText::FromString(bSaved ? TEXT("Saved") : TEXT("Save this place")));
	const FLinearColor Fill = bSaved ? Blue : White;
	SaveButton->SetStyle(StartStyle::Button(Fill, Fill * 0.94f, Fill * 0.86f, Line, 12.f / StartStyle::DesignToUi));
	SaveLabel->SetColorAndOpacity(FSlateColor(bSaved ? OnBlue : Blue));
}

void UMobileArrivedPageWidget::HandleFindAnotherClicked()
{
	if (OnFindAnother)
	{
		OnFindAnother();
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
