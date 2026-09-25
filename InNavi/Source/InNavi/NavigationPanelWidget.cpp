#include "NavigationPanelWidget.h"

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "PolyBuilding.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"

namespace
{
	const FLinearColor InactiveButtonColor(0.12f, 0.18f, 0.28f, 1.f);
	const FLinearColor ActiveStartColor(0.08f, 0.55f, 0.22f, 1.f);
	const FLinearColor ActiveDestinationColor(0.72f, 0.12f, 0.12f, 1.f);

	FString ConnectorTypeName(EInteriorNavNodeType Type)
	{
		switch (Type)
		{
		case EInteriorNavNodeType::Elevator: return TEXT("elevator");
		case EInteriorNavNodeType::Stairs: return TEXT("stairs");
		case EInteriorNavNodeType::Escalator: return TEXT("escalator");
		default: return TEXT("connector");
		}
	}
}

TSharedRef<SWidget> UNavigationPanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("NavigationCanvas"));
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Canvas;

		PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("NavigationPanel"));
		PanelBorder->SetBrushColor(FLinearColor(0.02f, 0.04f, 0.08f, 0.94f));
		PanelBorder->SetPadding(FMargin(14.f));
		PanelCanvasSlot = Canvas->AddChildToCanvas(PanelBorder);
		PanelCanvasSlot->SetAnchors(FAnchors(1.f, 0.f));
		PanelCanvasSlot->SetAlignment(FVector2D(1.f, 0.f));
		PanelCanvasSlot->SetPosition(FVector2D(-20.f, 20.f));
		PanelCanvasSlot->SetAutoSize(true);

		PanelSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("NavigationPanelSize"));
		PanelSizeBox->SetWidthOverride(340.f);
		PanelBorder->SetContent(PanelSizeBox);

		UVerticalBox* Controls = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NavigationControls"));
		PanelSizeBox->SetContent(Controls);

		auto AddText = [this, Controls](const TCHAR* Name, const FString& Value,
			const FLinearColor& Color, int32 FontSize) -> UTextBlock*
			{
				UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
				Text->SetText(FText::FromString(Value));
				Text->SetColorAndOpacity(FSlateColor(Color));
				Text->SetAutoWrapText(true);
				FSlateFontInfo Font = Text->GetFont();
				Font.Size = FontSize;
				Text->SetFont(Font);
				UVerticalBoxSlot* Slot = Controls->AddChildToVerticalBox(Text);
				Slot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
				return Text;
			};

		AddText(TEXT("Txt_NavigationTitle"), TEXT("Navigation (drag to move)"), FLinearColor::White, 20);
		ContextText = AddText(TEXT("Txt_NavigationContext"), TEXT("No building loaded"),
			FLinearColor(0.7f, 0.78f, 0.88f, 1.f), 12);

		auto AddButton = [this, Controls](const TCHAR* ButtonName, const TCHAR* Label,
			UTextBlock*& OutLabel) -> UButton*
			{
				UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(ButtonName));
				UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
					FName(FString(ButtonName) + TEXT("Label")));
				Text->SetText(FText::FromString(FString(Label)));
				Text->SetJustification(ETextJustify::Center);
				Text->SetAutoWrapText(true);
				Button->SetContent(Text);
				Button->SetBackgroundColor(InactiveButtonColor);
				UVerticalBoxSlot* Slot = Controls->AddChildToVerticalBox(Button);
				Slot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
				OutLabel = Text;
				return Button;
			};

		UTextBlock* UnusedFindLabel = nullptr;
		UTextBlock* UnusedClearLabel = nullptr;
		SetStartButton = AddButton(TEXT("Btn_SetStart"), TEXT("Set Start"), SetStartButtonLabel);
		SetDestinationButton = AddButton(TEXT("Btn_SetDestination"), TEXT("Set Destination"), SetDestinationButtonLabel);
		FindRouteButton = AddButton(TEXT("Btn_FindRoute"), TEXT("Find Route"), UnusedFindLabel);
		ClearRouteButton = AddButton(TEXT("Btn_ClearRoute"), TEXT("Clear Route"), UnusedClearLabel);

		StatusText = AddText(TEXT("Txt_Status"), TEXT("Choose Set Start or Set Destination."),
			FLinearColor(0.55f, 0.82f, 1.f, 1.f), 13);
		RouteDetailsText = AddText(TEXT("Txt_RouteDetails"), FString(),
			FLinearColor(0.45f, 0.92f, 1.f, 1.f), 12);
	}

	return Super::RebuildWidget();
}

void UNavigationPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTemp, Warning,
		TEXT("[Navigation] Panel constructed: Start=%s Destination=%s Find=%s Clear=%s"),
		SetStartButton ? TEXT("valid") : TEXT("NULL"),
		SetDestinationButton ? TEXT("valid") : TEXT("NULL"),
		FindRouteButton ? TEXT("valid") : TEXT("NULL"),
		ClearRouteButton ? TEXT("valid") : TEXT("NULL"));

	// Bind each button independently. One missing optional control must not prevent
	// Set Start and Set Destination from receiving their click handlers.
	if (SetStartButton)
	{
		SetStartButton->OnClicked.AddUniqueDynamic(this, &UNavigationPanelWidget::OnSetStartClicked);
	}
	if (SetDestinationButton)
	{
		SetDestinationButton->OnClicked.AddUniqueDynamic(this, &UNavigationPanelWidget::OnSetDestinationClicked);
	}
	if (FindRouteButton)
	{
		FindRouteButton->OnClicked.AddUniqueDynamic(this, &UNavigationPanelWidget::OnFindRouteClicked);
	}
	if (ClearRouteButton)
	{
		ClearRouteButton->OnClicked.AddUniqueDynamic(this, &UNavigationPanelWidget::OnClearRouteClicked);
	}
	if (PanelBorder)
	{
		PanelBorder->OnMouseButtonDownEvent.BindDynamic(this, &UNavigationPanelWidget::OnPanelMouseButtonDown);
		PanelBorder->OnMouseButtonUpEvent.BindDynamic(this, &UNavigationPanelWidget::OnPanelMouseButtonUp);
		PanelBorder->OnMouseMoveEvent.BindDynamic(this, &UNavigationPanelWidget::OnPanelMouseMove);
	}

	RefreshControls();
}

void UNavigationPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const int32 ActiveFloor = Interior ? Interior->GetActiveFloor() : INDEX_NONE;
	const FString BuildingId = Interior ? Interior->GetLoadedBuildingId() : FString();
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
	if (bIsDraggingPanel && FSlateApplication::IsInitialized()
		&& !FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton))
	{
		bIsDraggingPanel = false;
	}
	const float DesiredWidth = FMath::Clamp(ViewportSize.X * 0.28f, 260.f, 360.f);
	if (PanelSizeBox && !FMath::IsNearlyEqual(LastPanelWidth, DesiredWidth, 1.f))
	{
		PanelSizeBox->SetWidthOverride(DesiredWidth);
		LastPanelWidth = DesiredWidth;
	}
	if (PanelCanvasSlot && PanelBorder)
	{
		const FVector2D CurrentPosition = PanelCanvasSlot->GetPosition();
		const FVector2D ClampedPosition = ClampPanelPosition(CurrentPosition, MyGeometry.GetLocalSize());
		if (!CurrentPosition.Equals(ClampedPosition, 0.5f))
		{
			PanelCanvasSlot->SetPosition(ClampedPosition);
		}
	}
	if (ActiveFloor != LastDisplayedFloor || BuildingId != LastDisplayedBuildingId)
	{
		LastDisplayedFloor = ActiveFloor;
		LastDisplayedBuildingId = BuildingId;
		RefreshControls();
	}
}

FEventReply UNavigationPanelWidget::OnPanelMouseButtonDown(FGeometry MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && PanelBorder && PanelCanvasSlot)
	{
		const FVector2D ScreenPosition = MouseEvent.GetScreenSpacePosition();
		if (PanelBorder->GetCachedGeometry().IsUnderLocation(ScreenPosition)
			&& !IsScreenPositionOverButton(ScreenPosition))
		{
			const UPanelWidget* Parent = PanelBorder->GetParent();
			if (!Parent)
			{
				return UWidgetBlueprintLibrary::Unhandled();
			}
			bIsDraggingPanel = true;
			PanelDragStartMousePosition = Parent->GetCachedGeometry().AbsoluteToLocal(ScreenPosition);
			PanelDragStartPosition = PanelCanvasSlot->GetPosition();
			FEventReply Reply = UWidgetBlueprintLibrary::Handled();
			return UWidgetBlueprintLibrary::CaptureMouse(Reply, PanelBorder);
		}
	}

	return UWidgetBlueprintLibrary::Unhandled();
}

FEventReply UNavigationPanelWidget::OnPanelMouseMove(FGeometry MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (bIsDraggingPanel && PanelCanvasSlot)
	{
		if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
		{
			bIsDraggingPanel = false;
			FEventReply Reply = UWidgetBlueprintLibrary::Handled();
			return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
		}

		const UPanelWidget* Parent = PanelBorder ? PanelBorder->GetParent() : nullptr;
		if (!Parent)
		{
			bIsDraggingPanel = false;
			return UWidgetBlueprintLibrary::Unhandled();
		}
		const FGeometry& ParentGeometry = Parent->GetCachedGeometry();
		const FVector2D CurrentMousePosition = ParentGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const FVector2D NewPosition = PanelDragStartPosition
			+ (CurrentMousePosition - PanelDragStartMousePosition);
		PanelCanvasSlot->SetPosition(ClampPanelPosition(NewPosition, ParentGeometry.GetLocalSize()));
		return UWidgetBlueprintLibrary::Handled();
	}

	return UWidgetBlueprintLibrary::Unhandled();
}

FEventReply UNavigationPanelWidget::OnPanelMouseButtonUp(FGeometry MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (bIsDraggingPanel && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDraggingPanel = false;
		FEventReply Reply = UWidgetBlueprintLibrary::Handled();
		return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
	}

	return UWidgetBlueprintLibrary::Unhandled();
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
	UE_LOG(LogTemp, Warning, TEXT("[Navigation] Set Start button clicked; previous mode=%d"),
		static_cast<int32>(SelectionMode));

	if (SelectionMode == ENavigationSelectionMode::Start)
	{
		CancelSelection();
	}
	else
	{
		SetSelectionMode(ENavigationSelectionMode::Start);
	}
}

void UNavigationPanelWidget::OnSetDestinationClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("[Navigation] Set Destination button clicked; previous mode=%d"),
		static_cast<int32>(SelectionMode));

	if (SelectionMode == ENavigationSelectionMode::Destination)
	{
		CancelSelection();
	}
	else
	{
		SetSelectionMode(ENavigationSelectionMode::Destination);
	}
}

void UNavigationPanelWidget::OnClearRouteClicked()
{
	ClearAll();
	SetStatusMessage(TEXT("Route and selected points cleared."), ENavigationMessageKind::Success);
}

void UNavigationPanelWidget::SetNavigationContext(ABuildingGenerator* InGenerator, AInteriorGenerator* InInterior)
{
	Generator = InGenerator;
	Interior = InInterior;
	UE_LOG(LogTemp, Warning, TEXT("[Navigation] Context set: Generator=%s Interior=%s BuildingId=%s Floor=%d"),
		Generator ? TEXT("valid") : TEXT("NULL"),
		Interior ? TEXT("valid") : TEXT("NULL"),
		Interior ? *Interior->GetLoadedBuildingId() : TEXT("<none>"),
		Interior ? Interior->GetActiveFloor() : INDEX_NONE);
	RefreshControls();
}

void UNavigationPanelWidget::SelectPoint(FVector2D FootprintPosition, int32 Floor)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[Navigation] SelectPoint called: position=(%.2f, %.2f) floor=%d mode=%d"),
		FootprintPosition.X, FootprintPosition.Y, Floor,
		static_cast<int32>(SelectionMode));

	if (SelectionMode == ENavigationSelectionMode::None)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Navigation] SelectPoint ignored: no selection mode is active"));
		return;
	}
	if (!Generator || !Interior || Interior->GetLoadedBuildingId().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[Navigation] SelectPoint rejected: Generator=%s Interior=%s BuildingId=%s"),
			Generator ? TEXT("valid") : TEXT("NULL"),
			Interior ? TEXT("valid") : TEXT("NULL"),
			Interior ? *Interior->GetLoadedBuildingId() : TEXT("<none>"));
		SetStatusMessage(TEXT("Load a building interior first."), ENavigationMessageKind::Error);
		return;
	}
	APolyBuilding* Building = Generator->FindBuildingById(Interior->GetLoadedBuildingId());
	if (!Building)
	{
		UE_LOG(LogTemp, Error, TEXT("[Navigation] SelectPoint rejected: building '%s' was not found"),
			*Interior->GetLoadedBuildingId());
		SetStatusMessage(TEXT("The selected building is unavailable."), ENavigationMessageKind::Error);
		return;
	}
	FString Error;
	if (!FNavigationRoutePlanner::IsValidPoint(*Building, *Interior, Generator->GetScale(), Floor,
		FootprintPosition, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Navigation] SelectPoint rejected by validation: %s"), *Error);
		SetStatusMessage(Error, ENavigationMessageKind::Error);
		return;
	}

	FSelectedEndpoint& Endpoint = SelectionMode == ENavigationSelectionMode::Start ? Start : Destination;
	Endpoint.bValid = true;
	Endpoint.Point.Position = FootprintPosition;
	Endpoint.Point.Floor = Floor;
	Endpoint.BuildingId = Building->BuildingId;
	RoutePoints.Reset();
	const bool bWasSelectingStart = SelectionMode == ENavigationSelectionMode::Start;
	const FString EndpointName = bWasSelectingStart ? TEXT("Start") : TEXT("Destination");
	const FString AcceptedMessage = bWasSelectingStart
		? FString::Printf(TEXT("Start set on Floor %d."), Floor)
		: FString::Printf(TEXT("Destination set on Floor %d."), Floor);
	SelectionMode = ENavigationSelectionMode::None;
	UE_LOG(LogTemp, Warning, TEXT("[Navigation] Point accepted as %s: position=(%.2f, %.2f), floor=%d"),
		*EndpointName,
		FootprintPosition.X, FootprintPosition.Y, Floor);
	SetStatusMessage(AcceptedMessage, ENavigationMessageKind::Success);
}

void UNavigationPanelWidget::OnFindRouteClicked()
{
	if (!Generator || !Interior || !Start.bValid || !Destination.bValid
		|| Start.BuildingId != Destination.BuildingId
		|| Start.BuildingId != Interior->GetLoadedBuildingId())
	{
		SetStatusMessage(TEXT("Set a start and destination in the loaded building."),
			ENavigationMessageKind::Error);
		return;
	}
	APolyBuilding* Building = Generator->FindBuildingById(Start.BuildingId);
	if (!Building)
	{
		SetStatusMessage(TEXT("The selected building is unavailable."), ENavigationMessageKind::Error);
		return;
	}

	FNavigationRouteResult Result;
	if (!FNavigationRoutePlanner::FindRoute(*Building, *Interior, Generator->GetScale(),
		Start.Point.Floor, Start.Point.Position, Destination.Point.Floor,
		Destination.Point.Position, Result))
	{
		RoutePoints.Reset();
		SetStatusMessage(Result.Error, ENavigationMessageKind::Warning);
		return;
	}

	RoutePoints = MoveTemp(Result.Points);
	SetStatusMessage(FString::Printf(TEXT("Route found: %d points."), RoutePoints.Num()),
		ENavigationMessageKind::Success);
}

void UNavigationPanelWidget::ClearAll()
{
	SelectionMode = ENavigationSelectionMode::None;
	Start = FSelectedEndpoint();
	Destination = FSelectedEndpoint();
	RoutePoints.Reset();
	StatusMessage.Empty();
	StatusKind = ENavigationMessageKind::Instruction;
	RefreshControls();
}

void UNavigationPanelWidget::CancelSelection()
{
	if (SelectionMode != ENavigationSelectionMode::None)
	{
		SelectionMode = ENavigationSelectionMode::None;
		SetStatusMessage(TEXT("Selection cancelled."), ENavigationMessageKind::Warning);
	}
}

void UNavigationPanelWidget::SetSelectionMode(ENavigationSelectionMode NewMode)
{
	SelectionMode = NewMode;
	StatusMessage.Empty();
	StatusKind = ENavigationMessageKind::Instruction;
	RefreshControls();
}

void UNavigationPanelWidget::SetStatusMessage(const FString& Message, ENavigationMessageKind Kind)
{
	StatusMessage = Message;
	StatusKind = Kind;
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
		ClearRouteButton->SetIsEnabled(IsSelectingPoint() || Start.bValid || Destination.bValid
			|| !RoutePoints.IsEmpty());
	}
	if (SetStartButton)
	{
		SetStartButton->SetBackgroundColor(SelectionMode == ENavigationSelectionMode::Start
			? ActiveStartColor : InactiveButtonColor);
	}
	if (SetDestinationButton)
	{
		SetDestinationButton->SetBackgroundColor(SelectionMode == ENavigationSelectionMode::Destination
			? ActiveDestinationColor : InactiveButtonColor);
	}
	if (SetStartButtonLabel)
	{
		SetStartButtonLabel->SetText(FText::FromString(SelectionMode == ENavigationSelectionMode::Start
			? TEXT("Click a Start Location (Cancel)") : TEXT("Set Start")));
	}
	if (SetDestinationButtonLabel)
	{
		SetDestinationButtonLabel->SetText(FText::FromString(
			SelectionMode == ENavigationSelectionMode::Destination
			? TEXT("Click a Destination (Cancel)") : TEXT("Set Destination")));
	}
	if (ContextText)
	{
		const FString BuildingId = Interior ? Interior->GetLoadedBuildingId() : FString();
		const int32 ActiveFloor = Interior ? Interior->GetActiveFloor() : INDEX_NONE;
		ContextText->SetText(FText::FromString(BuildingId.IsEmpty()
			? TEXT("No building loaded")
			: FString::Printf(TEXT("Building: %s  |  Viewing Floor: %d"), *BuildingId, ActiveFloor)));
	}
	if (StatusText)
	{
		if (!StatusMessage.IsEmpty())
		{
			StatusText->SetText(FText::FromString(StatusMessage));
		}
		else
		{
			switch (SelectionMode)
			{
			case ENavigationSelectionMode::Start:
				StatusText->SetText(FText::FromString(TEXT("Select a start on the current floor.")));
				break;
			case ENavigationSelectionMode::Destination:
				StatusText->SetText(FText::FromString(TEXT("Select a destination on the current floor.")));
				break;
			default:
				StatusText->SetText(FText::FromString(TEXT("Choose Set Start or Set Destination.")));
				break;
			}
		}
		StatusText->SetColorAndOpacity(FSlateColor(GetStatusColor()));
	}
	if (RouteDetailsText)
	{
		RouteDetailsText->SetText(FText::FromString(BuildRouteDetails()));
	}
}

FString UNavigationPanelWidget::BuildRouteDetails() const
{
	FString Details;
	if (Start.bValid)
	{
		Details += FString::Printf(TEXT("Start: Floor %d\n"), Start.Point.Floor);
	}
	if (Destination.bValid)
	{
		Details += FString::Printf(TEXT("Destination: Floor %d\n"), Destination.Point.Floor);
	}

	const int32 ActiveFloor = Interior ? Interior->GetActiveFloor() : INDEX_NONE;
	int32 NextFloor = INDEX_NONE;
	int32 PreviousFloor = INDEX_NONE;
	for (int32 I = 1; I < RoutePoints.Num(); ++I)
	{
		const FNavigationRoutePoint& From = RoutePoints[I - 1];
		const FNavigationRoutePoint& To = RoutePoints[I];
		if (From.Floor == To.Floor)
		{
			continue;
		}
		const FString Connector = ConnectorTypeName(To.ConnectorType);
		const FString ConnectorLabel = To.ConnectorId.IsEmpty()
			? Connector : Connector + TEXT(" ") + To.ConnectorId;
		Details += FString::Printf(TEXT("Take %s from Floor %d to Floor %d.\n"),
			*ConnectorLabel, From.Floor, To.Floor);
		if (From.Floor == ActiveFloor && NextFloor == INDEX_NONE)
		{
			NextFloor = To.Floor;
		}
		if (To.Floor == ActiveFloor)
		{
			PreviousFloor = From.Floor;
		}
	}
	if (NextFloor != INDEX_NONE)
	{
		Details += FString::Printf(TEXT("Route continues to Floor %d."), NextFloor);
	}
	else if (PreviousFloor != INDEX_NONE)
	{
		Details += FString::Printf(TEXT("Route arrives from Floor %d."), PreviousFloor);
	}
	Details.RemoveFromEnd(TEXT("\n"));
	return Details;
}

FLinearColor UNavigationPanelWidget::GetStatusColor() const
{
	if (StatusMessage.IsEmpty())
	{
		return FLinearColor(0.55f, 0.82f, 1.f, 1.f);
	}
	switch (StatusKind)
	{
	case ENavigationMessageKind::Success: return FLinearColor(0.3f, 1.f, 0.45f, 1.f);
	case ENavigationMessageKind::Error: return FLinearColor(1.f, 0.3f, 0.3f, 1.f);
	case ENavigationMessageKind::Warning: return FLinearColor(1.f, 0.65f, 0.15f, 1.f);
	case ENavigationMessageKind::Route: return FLinearColor(0.45f, 0.92f, 1.f, 1.f);
	default: return FLinearColor(0.55f, 0.82f, 1.f, 1.f);
	}
}

bool UNavigationPanelWidget::IsPointerOverPanel() const
{
	// The full-screen user widget is SelfHitTestInvisible so WBP_InteriorScreen remains
	// interactive underneath it. Check general visibility rather than requiring the
	// exact Visible enum value; only PanelBorder and its children should own pointer input.
	return IsVisible() && PanelBorder && FSlateApplication::IsInitialized()
		&& PanelBorder->GetCachedGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos());
}

bool UNavigationPanelWidget::IsScreenPositionOverButton(const FVector2D& ScreenPosition) const
{
	const UButton* Buttons[] =
	{
		SetStartButton,
		SetDestinationButton,
		FindRouteButton,
		ClearRouteButton
	};

	for (const UButton* Button : Buttons)
	{
		if (Button && Button->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			return true;
		}
	}
	return false;
}

FVector2D UNavigationPanelWidget::ClampPanelPosition(const FVector2D& Position,
	const FVector2D& ViewportSize) const
{
	if (!PanelBorder)
	{
		return Position;
	}

	const FVector2D PanelSize = PanelBorder->GetCachedGeometry().GetLocalSize();
	if (PanelSize.X <= 0.f || PanelSize.Y <= 0.f
		|| ViewportSize.X <= 0.f || ViewportSize.Y <= 0.f)
	{
		return Position;
	}

	// The slot is anchored to the viewport's top-right corner with alignment (1, 0).
	// X=0 places its right edge at the viewport edge; PanelSize.X-ViewportSize.X
	// places its left edge at the opposite edge.
	const float MinX = FMath::Min(PanelSize.X - ViewportSize.X, 0.f);
	const float MaxY = FMath::Max(ViewportSize.Y - PanelSize.Y, 0.f);
	return FVector2D(
		FMath::Clamp(Position.X, MinX, 0.f),
		FMath::Clamp(Position.Y, 0.f, MaxY));
}
