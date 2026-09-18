#include "NavigationPanelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"

TSharedRef<SWidget> UNavigationPanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("NavigationCanvas"));
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Canvas;

		PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("NavigationPanel"));
		PanelBorder->SetBrushColor(FLinearColor(0.02f, 0.04f, 0.08f, 0.94f));
		PanelBorder->SetPadding(FMargin(12.f));
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(PanelBorder);
		PanelSlot->SetPosition(FVector2D(20.f, 20.f));
		PanelSlot->SetAutoSize(true);

		UVerticalBox* Controls = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NavigationControls"));
		PanelBorder->SetContent(Controls);

		auto AddButton = [this, Controls](const TCHAR* ButtonName, const TCHAR* Label) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(ButtonName));
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString(ButtonName) + TEXT("Label")));
			Text->SetText(FText::FromString(FString(Label)));
			Button->SetContent(Text);
			Controls->AddChildToVerticalBox(Button);
			return Button;
		};

		SetStartButton = AddButton(TEXT("Btn_SetStart"), TEXT("Set Start"));
		SetDestinationButton = AddButton(TEXT("Btn_SetDestination"), TEXT("Set Destination"));
		FindRouteButton = AddButton(TEXT("Btn_FindRoute"), TEXT("Find Route"));
		ClearRouteButton = AddButton(TEXT("Btn_ClearRoute"), TEXT("Clear Route"));

		StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_Status"));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Controls->AddChildToVerticalBox(StatusText);
	}

	return Super::RebuildWidget();
}

void UNavigationPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (SetStartButton && SetDestinationButton && ClearRouteButton)
	{
		SetStartButton->OnClicked.AddUniqueDynamic(this, &UNavigationPanelWidget::OnSetStartClicked);
		SetDestinationButton->OnClicked.AddUniqueDynamic(this, &UNavigationPanelWidget::OnSetDestinationClicked);
		ClearRouteButton->OnClicked.AddUniqueDynamic(this, &UNavigationPanelWidget::OnClearRouteClicked);
	}

	RefreshControls();
}

FReply UNavigationPanelWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && SelectionMode != ENavigationSelectionMode::None)
	{
		CancelSelection();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UNavigationPanelWidget::OnSetStartClicked()
{
	SetSelectionMode(SelectionMode == ENavigationSelectionMode::Start
		? ENavigationSelectionMode::None : ENavigationSelectionMode::Start);
}

void UNavigationPanelWidget::OnSetDestinationClicked()
{
	SetSelectionMode(SelectionMode == ENavigationSelectionMode::Destination
		? ENavigationSelectionMode::None : ENavigationSelectionMode::Destination);
}

void UNavigationPanelWidget::OnClearRouteClicked()
{
	CancelSelection();
}

void UNavigationPanelWidget::CancelSelection()
{
	if (SelectionMode != ENavigationSelectionMode::None)
	{
		SetSelectionMode(ENavigationSelectionMode::None);
	}
}

void UNavigationPanelWidget::SetSelectionMode(ENavigationSelectionMode NewMode)
{
	SelectionMode = NewMode;
	RefreshControls();
}

void UNavigationPanelWidget::RefreshControls()
{
	if (FindRouteButton)
	{
		FindRouteButton->SetIsEnabled(false);
	}
	if (ClearRouteButton)
	{
		ClearRouteButton->SetIsEnabled(SelectionMode != ENavigationSelectionMode::None);
	}
	if (StatusText)
	{
		switch (SelectionMode)
		{
		case ENavigationSelectionMode::Start:
			StatusText->SetText(FText::FromString(TEXT("Select a start.")));
			break;
		case ENavigationSelectionMode::Destination:
			StatusText->SetText(FText::FromString(TEXT("Select a destination.")));
			break;
		default:
			StatusText->SetText(FText::FromString(TEXT("Choose Set Start or Set Destination.")));
			break;
		}
	}
}

bool UNavigationPanelWidget::IsPointerOverPanel() const
{
	return PanelBorder && FSlateApplication::IsInitialized()
		&& PanelBorder->GetCachedGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos());
}
