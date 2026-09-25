#include "InteriorScreenWidget.h"

#include "ConfirmDialogWidget.h"
#include "EditScreenWidget.h"
#include "FloorListEntryWidget.h"
#include "InteriorFloorPlanWidget.h"
#include "InteriorTypes.h"
#include "NavPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"

UInteriorScreenWidget::UInteriorScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedRef<SWidget> UInteriorScreenWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("InteriorScreenCanvas"));
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Canvas;

		// Blocks the 3D scene behind, same purpose as the original Img_CanvasBackground - makes the
		// 2D floor plan easier to read.
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
		Background->SetBrushColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.f));
		UCanvasPanelSlot* BackgroundSlot = Canvas->AddChildToCanvas(Background);
		BackgroundSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackgroundSlot->SetOffsets(FMargin(0.f));
		BackgroundSlot->SetZOrder(0);

		// The 2D floor plan itself - already a self-contained C++ widget (finds its own
		// Building/InteriorGenerator/PlayerController references, handles its own clicks/drag/zoom),
		// nothing else to wire up here.
		FloorPlanWidget = WidgetTree->ConstructWidget<UInteriorFloorPlanWidget>(UInteriorFloorPlanWidget::StaticClass(), TEXT("FloorPlanWidget"));
		FloorPlanWidget->SetVisibility(ESlateVisibility::Visible);
		UCanvasPanelSlot* FloorPlanSlot = Canvas->AddChildToCanvas(FloorPlanWidget);
		FloorPlanSlot->SetZOrder(1);
		FloorPlanSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		FloorPlanSlot->SetOffsets(FMargin(0.f));

		// Left-side toolbar: one button per mode, static (no active-state highlighting, matching
		// the original).
		UVerticalBox* ModeStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ModeStack"));
		UCanvasPanelSlot* ModeStackSlot = Canvas->AddChildToCanvas(ModeStack);
		ModeStackSlot->SetAnchors(FAnchors(0.f, 0.f));
		ModeStackSlot->SetPosition(FVector2D(10.f, 10.f));
		ModeStackSlot->SetAutoSize(true);
		ModeStackSlot->SetZOrder(2);

		auto AddModeButton = [this, ModeStack](const TCHAR* Name, const TCHAR* Label) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));

			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString(Name) + TEXT("Label")));
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetJustification(ETextJustify::Center);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));

			USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			SizeBox->SetWidthOverride(150.f);
			SizeBox->SetHeightOverride(32.f);
			SizeBox->SetContent(Text);
			Button->SetContent(SizeBox);

			UVerticalBoxSlot* Slot = ModeStack->AddChildToVerticalBox(Button);
			Slot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
			return Button;
		};

		AddModeButton(TEXT("Draw_Mode_Button"), TEXT("DRAW ROOM MODE"))->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnDrawRoomModeClicked);
		AddModeButton(TEXT("Draw_Wall_Button"), TEXT("DRAW WALL MODE"))->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnDrawWallModeClicked);
		AddModeButton(TEXT("Select_Mode_Button"), TEXT("SELECT MODE"))->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnSelectModeClicked);
		AddModeButton(TEXT("Add_Rect_Button"), TEXT("ADD RECTANGLE"))->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddRectangleClicked);
		AddModeButton(TEXT("Add_Triangle_Button"), TEXT("ADD TRIANGLE"))->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddTriangleClicked);
		AddModeButton(TEXT("Add_Circle_Button"), TEXT("ADD CIRCLE"))->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddCircleClicked);
		AddModeButton(TEXT("Add_Door_Button"), TEXT("ADD DOOR MODE"))->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddDoorModeClicked);
		AddModeButton(TEXT("Add_Stairs_Button"), TEXT("ADD STAIRS MODE"))->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddStairsModeClicked);
		AddModeButton(TEXT("Add_Elevator_Button"), TEXT("ADD ELEVATOR MODE"))->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddElevatorModeClicked);
		AddModeButton(TEXT("Add_Escalator_Button"), TEXT("ADD ESCALATOR MODE"))->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddEscalatorModeClicked);

		// Back button, top-right (mirrors EditScreenWidget's layout).
		BackButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_Back"));
		BackButton->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		UTextBlock* BackText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Btn_BackLabel"));
		BackText->SetText(FText::FromString(TEXT("Back")));
		BackText->SetJustification(ETextJustify::Center);
		BackText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		USizeBox* BackSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Btn_BackSize"));
		BackSizeBox->SetWidthOverride(150.f);
		BackSizeBox->SetHeightOverride(36.f);
		BackSizeBox->SetContent(BackText);
		BackButton->SetContent(BackSizeBox);
		UCanvasPanelSlot* BackSlot = Canvas->AddChildToCanvas(BackButton);
		BackSlot->SetAnchors(FAnchors(1.f, 0.f));
		BackSlot->SetAlignment(FVector2D(1.f, 0.f));
		BackSlot->SetPosition(FVector2D(-20.f, 20.f));
		BackSlot->SetAutoSize(true);
		BackSlot->SetZOrder(2);

		// Floor panel - top-right, below Back (the one clear spot: mode toolbar is top-left,
		// door/room/connector panels bottom-right, building switcher bottom-left). Always visible
		// (unlike those panels), since switching floors should be available no matter what's
		// selected.
		UBorder* FloorPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_Floor"));
		FloorPanel->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.9f));
		FloorPanel->SetPadding(FMargin(14.f));

		UVerticalBox* FloorStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FloorStack"));
		FloorPanel->SetContent(FloorStack);

		UTextBlock* FloorTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_FloorTitle"));
		FloorTitle->SetText(FText::FromString(TEXT("Floor")));
		FloorTitle->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UVerticalBoxSlot* FloorTitleSlot = FloorStack->AddChildToVerticalBox(FloorTitle);
		FloorTitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		// Every button in this panel (floor rows, Add, Delete) uses this same size, so they all
		// visually match - see FloorListEntryWidget::RebuildWidget for the floor rows' own SizeBox.
		constexpr float FloorButtonWidth = 150.f;
		constexpr float FloorButtonHeight = 32.f;

		FloorListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("FloorListBox"));
		// 3 rows tall (row height + 4px padding each, see RefreshFloorList) caps the list without
		// hiding the scrollbar UScrollBox already shows once content overflows that.
		USizeBox* FloorListSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FloorListSizeBox"));
		FloorListSizeBox->SetHeightOverride((FloorButtonHeight + 4.f) * 3.f);
		FloorListSizeBox->SetWidthOverride(FloorButtonWidth);
		FloorListSizeBox->SetContent(FloorListBox);
		UVerticalBoxSlot* FloorListSlot = FloorStack->AddChildToVerticalBox(FloorListSizeBox);
		FloorListSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		AddFloorButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_AddFloor"));
		AddFloorButton->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		UTextBlock* AddFloorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Btn_AddFloorLabel"));
		AddFloorText->SetText(FText::FromString(TEXT("+ Add floor")));
		AddFloorText->SetJustification(ETextJustify::Center);
		AddFloorText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		USizeBox* AddFloorSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Btn_AddFloorSize"));
		AddFloorSizeBox->SetWidthOverride(FloorButtonWidth);
		AddFloorSizeBox->SetHeightOverride(FloorButtonHeight);
		AddFloorSizeBox->SetContent(AddFloorText);
		AddFloorButton->SetContent(AddFloorSizeBox);
		UVerticalBoxSlot* AddFloorSlot = FloorStack->AddChildToVerticalBox(AddFloorButton);
		AddFloorSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		RemoveFloorButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_RemoveFloor"));
		RemoveFloorButton->SetBackgroundColor(FLinearColor(0.6f, 0.2f, 0.2f, 1.f));
		UTextBlock* RemoveFloorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Btn_RemoveFloorLabel"));
		RemoveFloorText->SetText(FText::FromString(TEXT("Delete floor")));
		RemoveFloorText->SetJustification(ETextJustify::Center);
		RemoveFloorText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		USizeBox* RemoveFloorSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Btn_RemoveFloorSize"));
		RemoveFloorSizeBox->SetWidthOverride(FloorButtonWidth);
		RemoveFloorSizeBox->SetHeightOverride(FloorButtonHeight);
		RemoveFloorSizeBox->SetContent(RemoveFloorText);
		RemoveFloorButton->SetContent(RemoveFloorSizeBox);
		FloorStack->AddChildToVerticalBox(RemoveFloorButton);

		UCanvasPanelSlot* FloorPanelSlot = Canvas->AddChildToCanvas(FloorPanel);
		FloorPanelSlot->SetAnchors(FAnchors(1.f, 0.f));
		FloorPanelSlot->SetAlignment(FVector2D(1.f, 0.f));
		FloorPanelSlot->SetPosition(FVector2D(-20.f, 70.f));
		FloorPanelSlot->SetAutoSize(true);
		FloorPanelSlot->SetZOrder(2);

		// Door properties panel - bottom-right, hidden until a door is selected (see
		// HandleSelectedDoorChanged). Positioned away from the mode toolbar (top-left) and
		// Back/Navigation panel (top-right).
		DoorPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_DoorProperties"));
		DoorPanel->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.9f));
		DoorPanel->SetPadding(FMargin(12.f));
		DoorPanel->SetVisibility(ESlateVisibility::Collapsed);

		UVerticalBox* DoorStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DoorStack"));
		DoorPanel->SetContent(DoorStack);

		UTextBlock* DoorTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_DoorTitle"));
		DoorTitle->SetText(FText::FromString(TEXT("Door property")));
		DoorTitle->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UVerticalBoxSlot* DoorTitleSlot = DoorStack->AddChildToVerticalBox(DoorTitle);
		DoorTitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		DoorRoomText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_DoorRoom"));
		DoorRoomText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UVerticalBoxSlot* DoorRoomSlot = DoorStack->AddChildToVerticalBox(DoorRoomText);
		DoorRoomSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		UHorizontalBox* ConnectsToRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ConnectsToRow"));
		DoorStack->AddChildToVerticalBox(ConnectsToRow);

		UTextBlock* ConnectsToLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_ConnectsToLabel"));
		ConnectsToLabel->SetText(FText::FromString(TEXT("Connect to")));
		ConnectsToLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UHorizontalBoxSlot* ConnectsToLabelSlot = ConnectsToRow->AddChildToHorizontalBox(ConnectsToLabel);
		ConnectsToLabelSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		ConnectsToLabelSlot->SetVerticalAlignment(VAlign_Center);

		ConnectsToComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Cmb_ConnectsTo"));
		ConnectsToRow->AddChildToHorizontalBox(ConnectsToComboBox);

		ApplyDoorButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_ApplyDoor"));
		ApplyDoorButton->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		UTextBlock* ApplyDoorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Btn_ApplyDoorLabel"));
		ApplyDoorText->SetText(FText::FromString(TEXT("Apply")));
		ApplyDoorText->SetJustification(ETextJustify::Center);
		ApplyDoorText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyDoorButton->SetContent(ApplyDoorText);
		UVerticalBoxSlot* ApplyDoorSlot = DoorStack->AddChildToVerticalBox(ApplyDoorButton);
		ApplyDoorSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));

		UCanvasPanelSlot* DoorPanelSlot = Canvas->AddChildToCanvas(DoorPanel);
		DoorPanelSlot->SetAnchors(FAnchors(1.f, 1.f));
		DoorPanelSlot->SetAlignment(FVector2D(1.f, 1.f));
		DoorPanelSlot->SetPosition(FVector2D(-20.f, -20.f));
		DoorPanelSlot->SetAutoSize(true);
		DoorPanelSlot->SetZOrder(3);

		// Room properties panel - same position as the door panel (room/door/connector selection
		// is mutually exclusive, so only one of the three panels is ever visible at once).
		RoomPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_RoomProperties"));
		RoomPanel->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.9f));
		RoomPanel->SetPadding(FMargin(12.f));
		RoomPanel->SetVisibility(ESlateVisibility::Collapsed);

		UVerticalBox* RoomStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoomStack"));
		RoomPanel->SetContent(RoomStack);

		RoomTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_RoomTitle"));
		RoomTitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UVerticalBoxSlot* RoomTitleSlot = RoomStack->AddChildToVerticalBox(RoomTitleText);
		RoomTitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		UHorizontalBox* RoomNameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RoomNameRow"));
		RoomStack->AddChildToVerticalBox(RoomNameRow);

		UTextBlock* RoomNameLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_RoomNameLabel"));
		RoomNameLabel->SetText(FText::FromString(TEXT("Name :")));
		RoomNameLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UHorizontalBoxSlot* RoomNameLabelSlot = RoomNameRow->AddChildToHorizontalBox(RoomNameLabel);
		RoomNameLabelSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		RoomNameLabelSlot->SetVerticalAlignment(VAlign_Center);

		RoomNameTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_RoomName"));
		UHorizontalBoxSlot* RoomNameBoxSlot = RoomNameRow->AddChildToHorizontalBox(RoomNameTextBox);
		RoomNameBoxSlot->SetVerticalAlignment(VAlign_Center);

		ApplyRoomButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_ApplyRoom"));
		ApplyRoomButton->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		UTextBlock* ApplyRoomText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Btn_ApplyRoomLabel"));
		ApplyRoomText->SetText(FText::FromString(TEXT("Apply")));
		ApplyRoomText->SetJustification(ETextJustify::Center);
		ApplyRoomText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyRoomButton->SetContent(ApplyRoomText);
		UVerticalBoxSlot* ApplyRoomSlot = RoomStack->AddChildToVerticalBox(ApplyRoomButton);
		ApplyRoomSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));

		UCanvasPanelSlot* RoomPanelSlot = Canvas->AddChildToCanvas(RoomPanel);
		RoomPanelSlot->SetAnchors(FAnchors(1.f, 1.f));
		RoomPanelSlot->SetAlignment(FVector2D(1.f, 1.f));
		RoomPanelSlot->SetPosition(FVector2D(-20.f, -20.f));
		RoomPanelSlot->SetAutoSize(true);
		RoomPanelSlot->SetZOrder(3);

		// Connector (elevator/stairs/escalator) properties panel - same position as the other two
		// (mutually exclusive selection).
		ConnectorPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_ConnectorProperties"));
		ConnectorPanel->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.9f));
		ConnectorPanel->SetPadding(FMargin(12.f));
		ConnectorPanel->SetVisibility(ESlateVisibility::Collapsed);

		UVerticalBox* ConnectorStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ConnectorStack"));
		ConnectorPanel->SetContent(ConnectorStack);

		UTextBlock* ConnectorTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_ConnectorTitle"));
		ConnectorTitle->SetText(FText::FromString(TEXT("Connector property")));
		ConnectorTitle->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UVerticalBoxSlot* ConnectorTitleSlot = ConnectorStack->AddChildToVerticalBox(ConnectorTitle);
		ConnectorTitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		ConnectorTypeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_ConnectorType"));
		ConnectorTypeText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UVerticalBoxSlot* ConnectorTypeSlot = ConnectorStack->AddChildToVerticalBox(ConnectorTypeText);
		ConnectorTypeSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		UTextBlock* FloorsLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_FloorsLabel"));
		FloorsLabel->SetText(FText::FromString(TEXT("Floors :")));
		FloorsLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UVerticalBoxSlot* FloorsLabelSlot = ConnectorStack->AddChildToVerticalBox(FloorsLabel);
		FloorsLabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

		UWrapBox* FloorsWrap = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("FloorsWrap"));
		FloorsWrap->SetInnerSlotPadding(FVector2D(4.f, 2.f));
		UVerticalBoxSlot* FloorsWrapSlot = ConnectorStack->AddChildToVerticalBox(FloorsWrap);
		FloorsWrapSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		FloorCheckboxes.Reserve(20);
		for (int32 FloorNumber = 1; FloorNumber <= 20; ++FloorNumber)
		{
			UHorizontalBox* FloorEntry = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*FString::Printf(TEXT("FloorEntry%02d"), FloorNumber)));

			UCheckBox* CheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), FName(*FString::Printf(TEXT("Chk_Floor%d"), FloorNumber)));
			UHorizontalBoxSlot* CheckBoxSlot = FloorEntry->AddChildToHorizontalBox(CheckBox);
			CheckBoxSlot->SetVerticalAlignment(VAlign_Center);

			UTextBlock* FloorLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*FString::Printf(TEXT("Txt_Floor%dLabel"), FloorNumber)));
			FloorLabel->SetText(FText::AsNumber(FloorNumber));
			FloorLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			UHorizontalBoxSlot* FloorLabelSlot = FloorEntry->AddChildToHorizontalBox(FloorLabel);
			FloorLabelSlot->SetPadding(FMargin(2.f, 0.f, 0.f, 0.f));
			FloorLabelSlot->SetVerticalAlignment(VAlign_Center);

			FloorsWrap->AddChildToWrapBox(FloorEntry);
			FloorCheckboxes.Add(CheckBox);
		}

		ApplyConnectorButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_ApplyConnector"));
		ApplyConnectorButton->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		UTextBlock* ApplyConnectorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Btn_ApplyConnectorLabel"));
		ApplyConnectorText->SetText(FText::FromString(TEXT("Apply")));
		ApplyConnectorText->SetJustification(ETextJustify::Center);
		ApplyConnectorText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyConnectorButton->SetContent(ApplyConnectorText);
		UVerticalBoxSlot* ApplyConnectorSlot = ConnectorStack->AddChildToVerticalBox(ApplyConnectorButton);
		ApplyConnectorSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));

		UCanvasPanelSlot* ConnectorPanelSlot = Canvas->AddChildToCanvas(ConnectorPanel);
		ConnectorPanelSlot->SetAnchors(FAnchors(1.f, 1.f));
		ConnectorPanelSlot->SetAlignment(FVector2D(1.f, 1.f));
		ConnectorPanelSlot->SetPosition(FVector2D(-20.f, -20.f));
		ConnectorPanelSlot->SetAutoSize(true);
		ConnectorPanelSlot->SetZOrder(3);

		// Building switcher - bottom-left, the one clear corner (mode toolbar is top-left,
		// door/room/connector panels bottom-right, Back/Navigation panel top-right).
		UHorizontalBox* SwitcherRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BuildingSwitcherRow"));

		BuildingSwitcherComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Cbx_BuildingSwitcher"));
		UHorizontalBoxSlot* SwitcherComboSlot = SwitcherRow->AddChildToHorizontalBox(BuildingSwitcherComboBox);
		SwitcherComboSlot->SetVerticalAlignment(VAlign_Center);
		SwitcherComboSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));

		BuildingNameTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_BuildingName"));
		BuildingNameTextBox->SetVisibility(ESlateVisibility::Collapsed);
		UHorizontalBoxSlot* SwitcherTextSlot = SwitcherRow->AddChildToHorizontalBox(BuildingNameTextBox);
		SwitcherTextSlot->SetVerticalAlignment(VAlign_Center);
		SwitcherTextSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));

		EditBuildingNameButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_EditBuildingName"));
		EditBuildingNameButton->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		EditBuildingNameButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Btn_EditBuildingNameLabel"));
		EditBuildingNameButtonText->SetText(FText::FromString(TEXT("\x270E"))); // pencil
		EditBuildingNameButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		EditBuildingNameButton->SetContent(EditBuildingNameButtonText);
		UHorizontalBoxSlot* SwitcherButtonSlot = SwitcherRow->AddChildToHorizontalBox(EditBuildingNameButton);
		SwitcherButtonSlot->SetVerticalAlignment(VAlign_Center);

		UCanvasPanelSlot* SwitcherRowSlot = Canvas->AddChildToCanvas(SwitcherRow);
		SwitcherRowSlot->SetAnchors(FAnchors(0.f, 1.f));
		SwitcherRowSlot->SetAlignment(FVector2D(0.f, 1.f));
		SwitcherRowSlot->SetPosition(FVector2D(10.f, -10.f));
		SwitcherRowSlot->SetAutoSize(true);
		SwitcherRowSlot->SetZOrder(2);
	}

	return Super::RebuildWidget();
}

void UInteriorScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BackButton)
	{
		BackButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnBackClicked);
	}
	if (AddFloorButton)
	{
		AddFloorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddFloorClicked);
	}
	if (RemoveFloorButton)
	{
		RemoveFloorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRemoveFloorClicked);
	}
	if (ApplyDoorButton)
	{
		ApplyDoorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnApplyDoorClicked);
	}
	if (ApplyRoomButton)
	{
		ApplyRoomButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnApplyRoomClicked);
	}
	if (ApplyConnectorButton)
	{
		ApplyConnectorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnApplyConnectorClicked);
	}
	if (EditBuildingNameButton)
	{
		EditBuildingNameButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnEditBuildingNameClicked);
	}
	if (BuildingSwitcherComboBox)
	{
		BuildingSwitcherComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleBuildingSwitcherChanged);
	}

	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->OnSelectedDoorChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleSelectedDoorChanged);
		PC->OnSelectedRoomChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleSelectedRoomChanged);
		PC->OnSelectedConnectorChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleSelectedConnectorChanged);
	}

	RefreshBuildingSwitcher();
	RefreshFloorList();
}

void UInteriorScreenWidget::NativeDestruct()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->OnSelectedDoorChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleSelectedDoorChanged);
		PC->OnSelectedRoomChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleSelectedRoomChanged);
		PC->OnSelectedConnectorChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleSelectedConnectorChanged);
	}

	Super::NativeDestruct();
}

void UInteriorScreenWidget::HandleSelectedDoorChanged(FString RoomId, FString DoorId)
{
	if (!DoorPanel)
	{
		return;
	}

	if (RoomId.IsEmpty() && DoorId.IsEmpty())
	{
		DoorPanel->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	DoorPanel->SetVisibility(ESlateVisibility::Visible);

	if (DoorRoomText)
	{
		DoorRoomText->SetText(FText::FromString(FString::Printf(TEXT("Current room: %s"), *PC->ResolveConnectsToDisplayName(RoomId))));
	}

	if (ConnectsToComboBox)
	{
		ConnectsToComboBox->ClearOptions();
		for (const FString& Name : PC->GetConnectableTargetIds())
		{
			ConnectsToComboBox->AddOption(Name);
		}

		FInteriorOpening Opening;
		if (PC->GetSelectedDoorOpening(Opening))
		{
			ConnectsToComboBox->SetSelectedOption(PC->ResolveConnectsToDisplayName(Opening.ConnectsTo));
		}
	}
}

void UInteriorScreenWidget::OnApplyDoorClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (ConnectsToComboBox)
		{
			PC->SetSelectedDoorConnectsTo(ConnectsToComboBox->GetSelectedOption());
		}
	}
}

void UInteriorScreenWidget::HandleSelectedRoomChanged(FString RoomId)
{
	if (!RoomPanel)
	{
		return;
	}

	if (RoomId.IsEmpty())
	{
		RoomPanel->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	RoomPanel->SetVisibility(ESlateVisibility::Visible);

	if (RoomTitleText)
	{
		RoomTitleText->SetText(FText::FromString(PC->IsSelectedRoomClosedLoop() ? TEXT("Room property") : TEXT("Wall property")));
	}
	if (RoomNameTextBox)
	{
		RoomNameTextBox->SetText(FText::FromString(PC->GetSelectedRoomName()));
	}
}

void UInteriorScreenWidget::OnApplyRoomClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (RoomNameTextBox)
		{
			PC->SetSelectedRoomName(RoomNameTextBox->GetText().ToString());
		}
	}
}

void UInteriorScreenWidget::HandleSelectedConnectorChanged(FString GroupId)
{
	if (!ConnectorPanel)
	{
		return;
	}

	if (GroupId.IsEmpty())
	{
		ConnectorPanel->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	ConnectorPanel->SetVisibility(ESlateVisibility::Visible);

	if (ConnectorTypeText)
	{
		FString TypeName;
		switch (PC->GetSelectedConnectorType())
		{
		case EInteriorNavNodeType::Elevator:  TypeName = TEXT("Elevator");  break;
		case EInteriorNavNodeType::Stairs:    TypeName = TEXT("Stairs");    break;
		case EInteriorNavNodeType::Escalator: TypeName = TEXT("Escalator"); break;
		default:                              TypeName = TEXT("-");        break;
		}
		ConnectorTypeText->SetText(FText::FromString(FString::Printf(TEXT("Type: %s"), *TypeName)));
	}

	const TArray<int32> CurrentFloors = PC->GetSelectedConnectorFloors();
	const int32 FloorCount = PC->GetLoadedBuildingFloorCount();
	for (int32 Index = 0; Index < FloorCheckboxes.Num(); ++Index)
	{
		UCheckBox* CheckBox = FloorCheckboxes[Index];
		if (!CheckBox)
		{
			continue;
		}

		const int32 FloorNumber = Index + 1;
		if (FloorNumber <= FloorCount)
		{
			CheckBox->SetVisibility(ESlateVisibility::Visible);
			CheckBox->SetIsChecked(CurrentFloors.Contains(FloorNumber));
		}
		else
		{
			CheckBox->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UInteriorScreenWidget::OnApplyConnectorClicked()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	TArray<int32> NewFloors;
	for (int32 Index = 0; Index < FloorCheckboxes.Num(); ++Index)
	{
		const UCheckBox* CheckBox = FloorCheckboxes[Index];
		if (CheckBox && CheckBox->GetVisibility() != ESlateVisibility::Collapsed && CheckBox->IsChecked())
		{
			NewFloors.Add(Index + 1);
		}
	}

	PC->SetSelectedConnectorFloors(NewFloors);
}

void UInteriorScreenWidget::RefreshFloorList()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC || !FloorListBox)
	{
		return;
	}

	FloorListBox->ClearChildren();

	const int32 FloorCount = PC->GetLoadedBuildingFloorCount();
	const int32 ActiveFloor = PC->GetActiveFloor();
	// Lowest floor first (Level 1 at the top) - reversed from the original mockup per explicit
	// request.
	for (int32 FloorNumber = 1; FloorNumber <= FloorCount; ++FloorNumber)
	{
		UFloorListEntryWidget* Entry = CreateWidget<UFloorListEntryWidget>(PC, UFloorListEntryWidget::StaticClass());
		if (!Entry)
		{
			continue;
		}
		Entry->SetFloorNumber(FloorNumber, FloorNumber == ActiveFloor);
		Entry->OnEntryClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleFloorEntryClicked);

		if (UScrollBoxSlot* EntrySlot = Cast<UScrollBoxSlot>(FloorListBox->AddChild(Entry)))
		{
			EntrySlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}
	}
}

void UInteriorScreenWidget::HandleFloorEntryClicked(int32 FloorNumber)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetActiveFloor(FloorNumber);
	}
	RefreshFloorList();
}

void UInteriorScreenWidget::OnAddFloorClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->AddFloor();
	}
	RefreshFloorList();
}

void UInteriorScreenWidget::OnRemoveFloorClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->RemoveFloor(PC->GetActiveFloor());
	}
	RefreshFloorList();
}

void UInteriorScreenWidget::RefreshBuildingSwitcher()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC || !BuildingSwitcherComboBox)
	{
		return;
	}

	BuildingSwitcherComboBox->ClearOptions();
	for (const FString& Name : PC->GetAllBuildingNames())
	{
		BuildingSwitcherComboBox->AddOption(Name);
	}
	BuildingSwitcherComboBox->SetSelectedOption(PC->GetLoadedBuildingName());
}

void UInteriorScreenWidget::HandleBuildingSwitcherChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct)
	{
		// Our own code set this (RefreshBuildingSwitcher, or reverting on Cancel below) - not a
		// real user pick, ignore it.
		return;
	}

	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	const FString NewBuildingId = PC->FindBuildingIdByName(SelectedItem);
	if (NewBuildingId.IsEmpty() || NewBuildingId == PC->GetLoadedBuildingId())
	{
		return;
	}

	if (!PC->HasUnsavedChanges())
	{
		PC->LoadInteriorForBuilding(NewBuildingId);
		RefreshBuildingSwitcher();
		RefreshFloorList();
		return;
	}

	PendingBuildingId = NewBuildingId;
	PreviousBuildingName = PC->GetLoadedBuildingName();

	if (UConfirmDialogWidget* Dialog = CreateWidget<UConfirmDialogWidget>(PC, UConfirmDialogWidget::StaticClass()))
	{
		Dialog->AddToViewport();
		Dialog->OnSaveAndLeave.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleSwitchBuildingSave);
		Dialog->OnDiscardAndLeave.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleSwitchBuildingDiscard);
		Dialog->OnCancel.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleSwitchBuildingCancel);
	}
}

void UInteriorScreenWidget::HandleSwitchBuildingSave()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SaveCurrentProject();
		PC->LoadInteriorForBuilding(PendingBuildingId);
	}
	RefreshBuildingSwitcher();
	RefreshFloorList();
}

void UInteriorScreenWidget::HandleSwitchBuildingDiscard()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->DiscardUnsavedChanges();
		PC->LoadInteriorForBuilding(PendingBuildingId);
	}
	RefreshBuildingSwitcher();
	RefreshFloorList();
}

void UInteriorScreenWidget::HandleSwitchBuildingCancel()
{
	if (BuildingSwitcherComboBox)
	{
		BuildingSwitcherComboBox->SetSelectedOption(PreviousBuildingName);
	}
}

void UInteriorScreenWidget::OnEditBuildingNameClicked()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC || !BuildingNameTextBox || !BuildingSwitcherComboBox || !EditBuildingNameButtonText)
	{
		return;
	}

	if (!bIsEditingBuildingName)
	{
		bIsEditingBuildingName = true;
		BuildingNameTextBox->SetText(FText::FromString(PC->GetLoadedBuildingName()));
		BuildingNameTextBox->SetVisibility(ESlateVisibility::Visible);
		BuildingSwitcherComboBox->SetVisibility(ESlateVisibility::Collapsed);
		EditBuildingNameButtonText->SetText(FText::FromString(TEXT("\x2713"))); // checkmark
		return;
	}

	PC->SetLoadedBuildingName(BuildingNameTextBox->GetText().ToString());

	bIsEditingBuildingName = false;
	BuildingNameTextBox->SetVisibility(ESlateVisibility::Collapsed);
	BuildingSwitcherComboBox->SetVisibility(ESlateVisibility::Visible);
	EditBuildingNameButtonText->SetText(FText::FromString(TEXT("\x270E"))); // pencil
	RefreshBuildingSwitcher();
}

void UInteriorScreenWidget::OnDrawRoomModeClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ToggleDrawRoomMode();
	}
}

void UInteriorScreenWidget::OnDrawWallModeClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ToggleDrawWallMode();
	}
}

void UInteriorScreenWidget::OnSelectModeClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ToggleSelectMode();
	}
}

void UInteriorScreenWidget::OnAddRectangleClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->TogglePrefabRectangleMode();
	}
}

void UInteriorScreenWidget::OnAddTriangleClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->TogglePrefabTriangleMode();
	}
}

void UInteriorScreenWidget::OnAddCircleClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->TogglePrefabCircleMode();
	}
}

void UInteriorScreenWidget::OnAddDoorModeClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ToggleAddDoorMode();
	}
}

void UInteriorScreenWidget::OnAddStairsModeClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ToggleStairsMode();
	}
}

void UInteriorScreenWidget::OnAddElevatorModeClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ToggleElevatorMode();
	}
}

void UInteriorScreenWidget::OnAddEscalatorModeClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ToggleEscalatorMode();
	}
}

void UInteriorScreenWidget::OnBackClicked()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	if (!PC->HasUnsavedChanges())
	{
		GoToEditScreen();
		return;
	}

	if (UConfirmDialogWidget* Dialog = CreateWidget<UConfirmDialogWidget>(PC, UConfirmDialogWidget::StaticClass()))
	{
		Dialog->AddToViewport();
		Dialog->OnSaveAndLeave.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleSaveAndLeave);
		Dialog->OnDiscardAndLeave.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleDiscardAndLeave);
		Dialog->OnCancel.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleCancel);
	}
}

void UInteriorScreenWidget::HandleSaveAndLeave()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SaveCurrentProject();
	}
	GoToEditScreen();
}

void UInteriorScreenWidget::HandleDiscardAndLeave()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->DiscardUnsavedChanges();
	}
	GoToEditScreen();
}

void UInteriorScreenWidget::HandleCancel()
{
	// Nothing to do - the dialog already removed itself.
}

void UInteriorScreenWidget::GoToEditScreen()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	// Only reached once we've actually decided to leave (no unsaved changes to begin with, or the
	// user chose Save/Discard) - this destroys the currently-spawned interior actors
	// (InteriorGen->ClearLoadedInterior), which is only safe once there's nothing unsaved left to
	// lose. Calling this any earlier (e.g. before the confirm dialog is even shown) would destroy
	// in-progress edits even if the user then picks Cancel.
	PC->RestoreExteriorCameraView();

	if (UEditScreenWidget* EditScreen = CreateWidget<UEditScreenWidget>(PC, UEditScreenWidget::StaticClass()))
	{
		EditScreen->AddToViewport();
		RemoveFromParent();
	}
}
