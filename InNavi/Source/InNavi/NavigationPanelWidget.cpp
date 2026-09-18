#include "NavigationPanelWidget.h"

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "PolyBuilding.h"
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
		RouteDetailsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_RouteDetails"));
		RouteDetailsText->SetAutoWrapText(true);
		RouteDetailsText->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.9f, 1.f)));
		Controls->AddChildToVerticalBox(RouteDetailsText);
	}

	return Super::RebuildWidget();
}

void UNavigationPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (SetStartButton && SetDestinationButton && FindRouteButton && ClearRouteButton)
	{
		SetStartButton->OnClicked.AddUniqueDynamic(this, &UNavigationPanelWidget::OnSetStartClicked);
		SetDestinationButton->OnClicked.AddUniqueDynamic(this, &UNavigationPanelWidget::OnSetDestinationClicked);
		FindRouteButton->OnClicked.AddUniqueDynamic(this, &UNavigationPanelWidget::OnFindRouteClicked);
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
	ClearAll();
}

void UNavigationPanelWidget::SetNavigationContext(ABuildingGenerator* InGenerator, AInteriorGenerator* InInterior)
{
	Generator = InGenerator;
	Interior = InInterior;
	RefreshControls();
}

void UNavigationPanelWidget::SelectPoint(FVector2D FootprintPosition, int32 Floor)
{
	if (SelectionMode == ENavigationSelectionMode::None)
	{
		return;
	}
	if (!Generator || !Interior || Interior->GetLoadedBuildingId().IsEmpty())
	{
		StatusMessage = TEXT("Load a building interior first.");
		RefreshControls();
		return;
	}
	APolyBuilding* Building = Generator->FindBuildingById(Interior->GetLoadedBuildingId());
	if (!Building)
	{
		StatusMessage = TEXT("The selected building is unavailable.");
		RefreshControls();
		return;
	}
	FString Error;
	if (!FNavigationRoutePlanner::IsValidPoint(*Building, *Interior, Generator->GetScale(), Floor, FootprintPosition, Error))
	{
		StatusMessage = Error;
		RefreshControls();
		return;
	}

	FSelectedEndpoint& Endpoint = SelectionMode == ENavigationSelectionMode::Start ? Start : Destination;
	Endpoint.bValid = true;
	Endpoint.Point.Position = FootprintPosition;
	Endpoint.Point.Floor = Floor;
	Endpoint.BuildingId = Building->BuildingId;
	RoutePoints.Reset();
	StatusMessage = SelectionMode == ENavigationSelectionMode::Start
		? FString::Printf(TEXT("Start set on Floor %d."), Floor)
		: FString::Printf(TEXT("Destination set on Floor %d."), Floor);
	SelectionMode = ENavigationSelectionMode::None;
	RefreshControls();
}

void UNavigationPanelWidget::OnFindRouteClicked()
{
	if (!Generator || !Interior || !Start.bValid || !Destination.bValid
		|| Start.BuildingId != Destination.BuildingId
		|| Start.BuildingId != Interior->GetLoadedBuildingId())
	{
		StatusMessage = TEXT("Set a start and destination in the loaded building.");
		RefreshControls();
		return;
	}
	APolyBuilding* Building = Generator->FindBuildingById(Start.BuildingId);
	if (!Building)
	{
		StatusMessage = TEXT("The selected building is unavailable.");
		RefreshControls();
		return;
	}

	FNavigationRouteResult Result;
	if (!FNavigationRoutePlanner::FindRoute(*Building, *Interior, Generator->GetScale(),
		Start.Point.Floor, Start.Point.Position, Destination.Point.Floor, Destination.Point.Position, Result))
	{
		RoutePoints.Reset();
		StatusMessage = Result.Error;
		RefreshControls();
		return;
	}

	RoutePoints = MoveTemp(Result.Points);
	StatusMessage = FString::Printf(TEXT("Route found: %d points."), RoutePoints.Num());
	RefreshControls();
}

void UNavigationPanelWidget::ClearAll()
{
	SelectionMode = ENavigationSelectionMode::None;
	Start = FSelectedEndpoint();
	Destination = FSelectedEndpoint();
	RoutePoints.Reset();
	StatusMessage.Empty();
	RefreshControls();
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
	StatusMessage.Empty();
	RefreshControls();
}

void UNavigationPanelWidget::RefreshControls()
{
	if (FindRouteButton)
	{
		FindRouteButton->SetIsEnabled(Start.bValid && Destination.bValid && Interior
			&& Start.BuildingId == Destination.BuildingId
			&& Start.BuildingId == Interior->GetLoadedBuildingId());
	}
	if (ClearRouteButton)
	{
		ClearRouteButton->SetIsEnabled(IsSelectingPoint() || Start.bValid || Destination.bValid || !RoutePoints.IsEmpty());
	}
	if (StatusText)
	{
		if (!StatusMessage.IsEmpty())
		{
			StatusText->SetText(FText::FromString(StatusMessage));
		}
		else switch (SelectionMode)
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
	if (RouteDetailsText)
	{
		FString Details;
		for (int32 I = 1; I < RoutePoints.Num(); ++I)
		{
			if (RoutePoints[I].Floor == RoutePoints[I - 1].Floor)
			{
				continue;
			}
			FString Connector = TEXT("connector");
			switch (RoutePoints[I].ConnectorType)
			{
			case EInteriorNavNodeType::Elevator: Connector = TEXT("elevator"); break;
			case EInteriorNavNodeType::Stairs: Connector = TEXT("stairs"); break;
			case EInteriorNavNodeType::Escalator: Connector = TEXT("escalator"); break;
			default: break;
			}
			const FString ConnectorLabel = RoutePoints[I].ConnectorId.IsEmpty()
				? Connector : Connector + TEXT(" ") + RoutePoints[I].ConnectorId;
			Details += FString::Printf(TEXT("Take %s to Floor %d.\n"),
				*ConnectorLabel, RoutePoints[I].Floor);
		}
		RouteDetailsText->SetText(FText::FromString(Details));
	}
}

bool UNavigationPanelWidget::IsPointerOverPanel() const
{
	return GetVisibility() == ESlateVisibility::Visible && PanelBorder && FSlateApplication::IsInitialized()
		&& PanelBorder->GetCachedGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos());
}
