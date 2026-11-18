#include "RoomContextMenuWidget.h"

#include "NavPlayerController.h"
#include "StartScreenStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

TSharedRef<SWidget> URoomContextMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RoomContextMenuCanvas"));
		Canvas->SetVisibility(ESlateVisibility::Visible);
		WidgetTree->RootWidget = Canvas;

		// A see-through button over the whole screen: a click anywhere outside the menu closes it.
		DismissButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_MenuCatcher"));
		DismissButton->SetStyle(StartStyle::MenuButton());
		DismissButton->SetBackgroundColor(FLinearColor::Transparent);
		UCanvasPanelSlot* CatcherSlot = Canvas->AddChildToCanvas(DismissButton);
		CatcherSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		CatcherSlot->SetOffsets(FMargin(0.f));

		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_RoomContextMenu"));
		Panel->SetBrush(StartStyle::MenuPanel());
		Panel->SetBrushColor(FLinearColor::White);
		Panel->SetPadding(FMargin(6.f));
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
		PanelSlot->SetPosition(MenuPosition);
		PanelSlot->SetAutoSize(true);

		UVerticalBox* Rows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuRows"));
		Panel->SetContent(Rows);

		DuplicateButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_DuplicateToFloors"));
		DuplicateButton->SetStyle(StartStyle::MenuRowButton());
		DuplicateButton->SetBackgroundColor(FLinearColor::White);
		USizeBox* RowSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Btn_DuplicateToFloorsSize"));
		RowSize->SetMinDesiredWidth(170.f);
		RowSize->SetContent(StartStyle::Text(WidgetTree, TEXT("Btn_DuplicateToFloorsLabel"), TEXT("Duplicate to Floors..."), 13.f, FLinearColor::White));
		DuplicateButton->SetContent(RowSize);
		Rows->AddChildToVerticalBox(DuplicateButton);
	}

	return Super::RebuildWidget();
}

void URoomContextMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (DuplicateButton)
	{
		DuplicateButton->OnClicked.AddUniqueDynamic(this, &URoomContextMenuWidget::OnDuplicateClicked);
		DuplicateButton->OnClicked.AddUniqueDynamic(this, &URoomContextMenuWidget::PlayButtonClickSound);
	}
	if (DismissButton)
	{
		DismissButton->OnClicked.AddUniqueDynamic(this, &URoomContextMenuWidget::OnDismissClicked);
	}
}

void URoomContextMenuWidget::OnDuplicateClicked()
{
	// Closed first: the choice opens the dialog, which must not find this menu still on the screen.
	RemoveFromParent();
	OnDuplicateToFloors.Broadcast();
}

void URoomContextMenuWidget::OnDismissClicked()
{
	RemoveFromParent();
}

void URoomContextMenuWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
	}
}
