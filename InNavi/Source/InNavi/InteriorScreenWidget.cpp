#include "InteriorScreenWidget.h"

#include "ConfirmDialogWidget.h"
#include "EditScreenWidget.h"
#include "FloorListEntryWidget.h"
#include "InteriorFloorPlanWidget.h"
#include "InteriorTypes.h"
#include "NavPlayerController.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
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
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/Texture2D.h"

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

		// Both side bars stretch to the full height of the screen (anchored top AND bottom, like an
		// editor dock panel - e.g. the engine's own Details panel, which stays full-height and shows
		// empty content rather than shrinking away when nothing is selected) instead of shrink-
		// wrapping to their own content. Width is live/resizable (LeftSidebarWidth/RightSidebarWidth,
		// see OnLeftSidebarResizeMouseButtonDown) rather than fixed, starting at SidebarMinWidth.

		// Every button on this screen (mode toolbar, Back, floor list rows, Add/Delete floor, Apply)
		// shares this exact size, so nothing looks bigger/smaller than anything else. None of them
		// override font size - they render at the same default size as everything else on this
		// screen (e.g. the building switcher's "Block E6"), just sized generously enough to fit that
		// default size without the text spilling past the button. Floor list rows are the one
		// exception built elsewhere (see FloorListEntryWidget::RebuildWidget) - keep that in sync
		// with these values by hand.
		constexpr float StandardButtonWidth = 200.f;
		constexpr float StandardButtonHeight = 44.f;

		// Safety net, not the primary fix - wraps to a second line instead of spilling past the
		// button on the rare label that's still too wide for StandardButtonWidth at default font size.
		auto ApplyStandardTextWrap = [](UTextBlock* TextBlock)
		{
			TextBlock->SetAutoWrapText(true);
		};

		// Left sidebar: building switcher on top, then the mode toolbar grouped into Default/Draw/
		// Navigate sections (matching the hand-drawn layout) - one VerticalBox so everything
		// auto-stacks instead of each piece needing its own guessed Y position. Wrapped in a visible
		// background bar (LeftSidebarBar) so the whole column reads as one distinct panel instead of
		// buttons floating loose over the 3D scene.
		UBorder* LeftSidebarBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_LeftSidebarBar"));
		LeftSidebarBar->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.85f));
		LeftSidebarBar->SetPadding(FMargin(10.f));

		UVerticalBox* LeftSidebar = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LeftSidebar"));
		LeftSidebarBar->SetContent(LeftSidebar);

		// Anchors(0,0,0,1): fixed to the left edge (X point-anchored), stretched top-to-bottom (Y
		// anchored 0..1). For a stretched axis, SetOffsets' corresponding pair means margin-from-edge
		// (Top/Bottom here) rather than position/size - for the point axis (X here), Left/Right still
		// mean position/width, same as a normal point-anchored slot.
		LeftSidebarCanvasSlot = Canvas->AddChildToCanvas(LeftSidebarBar);
		LeftSidebarCanvasSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 1.f));
		LeftSidebarCanvasSlot->SetAlignment(FVector2D(0.f, 0.f));
		LeftSidebarCanvasSlot->SetOffsets(FMargin(10.f, SidebarTopMargin, LeftSidebarWidth, SidebarBottomMargin));

		// Drag handle glued to the bar's right edge (see OnLeftSidebarResizeMouseButtonDown) - same
		// mixed anchor as the bar itself, positioned just past its current width.
		LeftSidebarResizeHandle = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_LeftSidebarResizeHandle"));
		LeftSidebarResizeHandle->SetBrushColor(FLinearColor(0.4f, 0.4f, 0.4f, 0.6f));
		LeftSidebarResizeHandle->SetCursor(EMouseCursor::ResizeLeftRight);
		LeftSidebarResizeHandle->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnLeftSidebarResizeMouseButtonDown);
		LeftSidebarResizeHandle->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnLeftSidebarResizeMouseMove);
		LeftSidebarResizeHandle->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnLeftSidebarResizeMouseButtonUp);
		LeftSidebarResizeHandleSlot = Canvas->AddChildToCanvas(LeftSidebarResizeHandle);
		LeftSidebarResizeHandleSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 1.f));
		LeftSidebarResizeHandleSlot->SetAlignment(FVector2D(0.f, 0.f));
		LeftSidebarResizeHandleSlot->SetOffsets(FMargin(10.f + LeftSidebarWidth, SidebarTopMargin, SidebarResizeHandleWidth, SidebarBottomMargin));
		LeftSidebarResizeHandleSlot->SetZOrder(3);
		LeftSidebarCanvasSlot->SetZOrder(2);

		// Building switcher - lets the user jump straight to a different building's interior
		// without going back to the exterior first.
		UHorizontalBox* SwitcherRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BuildingSwitcherRow"));

		BuildingSwitcherComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Cbx_BuildingSwitcher"));
		BuildingSwitcherComboBox->OnGenerateWidgetEvent.BindDynamic(this, &UInteriorScreenWidget::HandleGenerateComboBoxItem);
		// Fixed width so a long building name can't push this row past LeftSidebarBar's edge (its
		// content area is LeftSidebarWidth minus the bar's own 10px-a-side padding) - the combo
		// button clips its own text at this width rather than growing to fit it. Kept in sync with
		// LeftSidebarWidth as the bar is dragged by RefreshSidebarContentWidths, which recomputes
		// this exact formula - keep the two in sync by hand if this one changes.
		USizeBox* BuildingSwitcherSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		BuildingSwitcherSizeBox->SetWidthOverride(LeftSidebarWidth - 20.f - 100.f); // leaves ~100px for the edit-name button beside it, with slack
		BuildingSwitcherSizeBox->SetContent(BuildingSwitcherComboBox);
		UHorizontalBoxSlot* SwitcherComboSlot = SwitcherRow->AddChildToHorizontalBox(BuildingSwitcherSizeBox);
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
		EditBuildingNameButtonText->SetJustification(ETextJustify::Center);
		EditBuildingNameButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		// The 32x32 box below stops the button from pushing the row wider, but doesn't clip - at the
		// default (larger) font, the glyph itself still rendered a few px wider than the box, poking
		// out evenly on both sides. Shrinking the glyph itself is what actually keeps it inside.
		{
			FSlateFontInfo EditIconFont = EditBuildingNameButtonText->GetFont();
			EditIconFont.Size = 14;
			EditBuildingNameButtonText->SetFont(EditIconFont);
		}
		// Never had a size constraint before - the pencil glyph's own natural width (with the
		// button's default padding on top) turned out to be wider than the ~46px the building
		// switcher's width formula budgeted for it, so it always overflowed past the bar's edge by
		// that fixed difference, independent of LeftSidebarWidth (i.e. resizing never helped).
		USizeBox* EditButtonSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		EditButtonSizeBox->SetWidthOverride(32.f);
		EditButtonSizeBox->SetHeightOverride(32.f);
		if (USizeBoxSlot* EditButtonTextSlot = Cast<USizeBoxSlot>(EditButtonSizeBox->SetContent(EditBuildingNameButtonText)))
		{
			EditButtonTextSlot->SetHorizontalAlignment(HAlign_Center);
			EditButtonTextSlot->SetVerticalAlignment(VAlign_Center);
		}
		EditBuildingNameButton->SetContent(EditButtonSizeBox);
		UHorizontalBoxSlot* SwitcherButtonSlot = SwitcherRow->AddChildToHorizontalBox(EditBuildingNameButton);
		SwitcherButtonSlot->SetVerticalAlignment(VAlign_Center);

		UVerticalBoxSlot* SwitcherRowSlot = LeftSidebar->AddChildToVerticalBox(SwitcherRow);
		SwitcherRowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

		auto AddToolGroupLabel = [this, LeftSidebar](const TCHAR* Name, const TCHAR* Label)
		{
			UTextBlock* GroupLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
			GroupLabel->SetText(FText::FromString(FString(Label)));
			GroupLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f, 1.f)));
			UVerticalBoxSlot* Slot = LeftSidebar->AddChildToVerticalBox(GroupLabel);
			Slot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
		};

		// Icon-only now instead of a text label - sidesteps the font-size/overflow fight entirely
		// (an icon has no line-wrap or width-overflow failure mode), at the cost of needing a
		// tooltip so the mode is still discoverable. IconAssetName is one of Padre's existing
		// toolbar icons under /Game/ImageAssets (Icon_AddDoor, Icon_DrawRoom, ...), previously
		// unreferenced in C++ (same asset pack already reused for the floor-plan connector markers
		// in InteriorFloorPlanWidget).
		constexpr float ModeButtonSize = 48.f;

		// SetToolTipText's default tooltip renders at the engine's small default font size with no
		// way to resize just that text. SetToolTip(Widget) takes over the whole tooltip instead, but
		// its SToolTip wrapper is built with no border/background of its own (BorderImage(nullptr)) -
		// so this builds a small light box to replicate the default tooltip's look, just with the
		// font doubled.
		auto MakeToolTipWidget = [this](const TCHAR* TooltipLabel) -> UWidget*
		{
			UTextBlock* TooltipText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			TooltipText->SetText(FText::FromString(FString(TooltipLabel)));
			TooltipText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
			TooltipText->SetAutoWrapText(true);
			// Left-justified text in a fixed-width box left a ragged, lopsided-looking gap on the
			// right of every line shorter than the longest one - centering each wrapped line reads as
			// a balanced block instead.
			TooltipText->SetJustification(ETextJustify::Center);
			FSlateFontInfo TooltipFont = TooltipText->GetFont();
			TooltipFont.TypefaceFontName = TEXT("Regular"); // the default font style otherwise renders bold
			TooltipText->SetFont(TooltipFont);

			// Descriptions are full sentences now (how to actually use the mode, not just its name) -
			// wide enough for about 7 words a line before wrapping, instead of stretching edge to
			// edge. MaxDesiredWidth alone let the wrap collapse to one word per line (nothing gave
			// AutoWrapText an actual width to wrap against) - WidthOverride is a hard width, so
			// wrapping has something real to measure against.
			USizeBox* TooltipSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			TooltipSizeBox->SetWidthOverride(1000.f);
			TooltipSizeBox->SetContent(TooltipText);

			UBorder* TooltipBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			TooltipBorder->SetBrushColor(FLinearColor::White);
			TooltipBorder->SetPadding(FMargin(10.f, 6.f));
			TooltipBorder->SetContent(TooltipSizeBox);

			// UWidget::SetToolTip (below) builds its SToolTip with no border/background of its own
			// (BorderImage(nullptr)), unlike the engine's native default tooltip (SetToolTipText),
			// which has a soft drop shadow baked into its brush. Faking that here with a second,
			// darker copy of the same box behind the real one, offset a few px down-right - an Overlay
			// stretches every child to match its largest sibling by default, so the shadow layer ends
			// up the same size as the real box even though it has no text of its own.
			UBorder* ShadowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			ShadowBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.35f));

			// Overlay slots default to Left/Top, not Fill - without Fill on both, the empty shadow
			// layer would just collapse to zero size instead of stretching to match its sibling.
			UOverlay* TooltipOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
			UOverlaySlot* ShadowSlot = TooltipOverlay->AddChildToOverlay(ShadowBorder);
			ShadowSlot->SetHorizontalAlignment(HAlign_Fill);
			ShadowSlot->SetVerticalAlignment(VAlign_Fill);
			ShadowSlot->SetPadding(FMargin(3.f, 3.f, 0.f, 0.f));
			UOverlaySlot* ForegroundSlot = TooltipOverlay->AddChildToOverlay(TooltipBorder);
			ForegroundSlot->SetHorizontalAlignment(HAlign_Fill);
			ForegroundSlot->SetVerticalAlignment(VAlign_Fill);
			ForegroundSlot->SetPadding(FMargin(0.f, 0.f, 3.f, 3.f));

			return TooltipOverlay;
		};

		auto AddModeButton = [this, LeftSidebar, MakeToolTipWidget](const TCHAR* Name, const TCHAR* IconAssetName, const TCHAR* TooltipLabel) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
			Button->SetToolTip(MakeToolTipWidget(TooltipLabel));

			UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(FString(Name) + TEXT("Icon")));
			const FString IconPath = FString::Printf(TEXT("/Game/ImageAssets/%s.%s"), IconAssetName, IconAssetName);
			if (UTexture2D* IconTexture = LoadObject<UTexture2D>(nullptr, *IconPath))
			{
				// Not bMatchSize - these source PNGs weren't authored at a consistent resolution, so
				// matching native size would make icons different sizes (and some bigger than the
				// button itself). DesiredSizeOverride forces every icon to render at the same size
				// regardless of its source image's actual resolution.
				Icon->SetBrushFromTexture(IconTexture, false);
				Icon->SetDesiredSizeOverride(FVector2D(28.f, 28.f));
			}

			USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			SizeBox->SetWidthOverride(ModeButtonSize);
			SizeBox->SetHeightOverride(ModeButtonSize);
			if (USizeBoxSlot* IconSlot = Cast<USizeBoxSlot>(SizeBox->SetContent(Icon)))
			{
				IconSlot->SetHorizontalAlignment(HAlign_Center);
				IconSlot->SetVerticalAlignment(VAlign_Center);
			}
			Button->SetContent(SizeBox);

			UVerticalBoxSlot* Slot = LeftSidebar->AddChildToVerticalBox(Button);
			Slot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
			return Button;
		};

		// Same 10 tools as before, just regrouped to match the hand-drawn layout's
		// Default/Draw/Navigate sections - no new tools added. Tooltips describe the actual
		// interaction (click count, confirm key, defaults), not just the mode's name - matches
		// docs/design/interior-editor-guide.md.
		AddToolGroupLabel(TEXT("Txt_GroupDefault"), TEXT("Default"));
		SelectModeButton = AddModeButton(TEXT("Select_Mode_Button"), TEXT("Icon_Select"), TEXT("Select Mode (click a room, door, or connector to select it; drag to move it; Backspace to delete)"));
		SelectModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnSelectModeClicked);

		AddToolGroupLabel(TEXT("Txt_GroupDraw"), TEXT("Draw"));
		DrawRoomModeButton = AddModeButton(TEXT("Draw_Mode_Button"), TEXT("Icon_DrawRoom"), TEXT("Draw Room Mode (click at least 3 points to outline a room, then press Enter to finish; Esc cancels)"));
		DrawRoomModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnDrawRoomModeClicked);
		DrawWallModeButton = AddModeButton(TEXT("Draw_Wall_Button"), TEXT("Icon_DrawWall"), TEXT("Draw Wall Mode (click points to draw a standalone wall, then press Enter to finish; Esc cancels)"));
		DrawWallModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnDrawWallModeClicked);
		AddRectModeButton = AddModeButton(TEXT("Add_Rect_Button"), TEXT("Icon_AddRectangle"), TEXT("Add Rectangle (click a point to place a rectangular room centered there)"));
		AddRectModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddRectangleClicked);
		AddTriangleModeButton = AddModeButton(TEXT("Add_Triangle_Button"), TEXT("Icon_AddTriangle"), TEXT("Add Triangle (click a point to place a triangular room centered there)"));
		AddTriangleModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddTriangleClicked);
		AddCircleModeButton = AddModeButton(TEXT("Add_Circle_Button"), TEXT("Icon_AddCircle"), TEXT("Add Circle (click a point to place a circular room centered there)"));
		AddCircleModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddCircleClicked);

		AddToolGroupLabel(TEXT("Txt_GroupNavigate"), TEXT("Navigate"));
		AddDoorModeButton = AddModeButton(TEXT("Add_Door_Button"), TEXT("Icon_AddDoor"), TEXT("Add Door Mode (click a wall edge to cut a fixed-size door into it)"));
		AddDoorModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddDoorModeClicked);
		AddStairsModeButton = AddModeButton(TEXT("Add_Stairs_Button"), TEXT("Icon_AddStairs"), TEXT("Add Stairs Mode (click a point to place stairs, connecting the floor above and below by default)"));
		AddStairsModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddStairsModeClicked);
		AddElevatorModeButton = AddModeButton(TEXT("Add_Elevator_Button"), TEXT("Icon_AddElevator"), TEXT("Add Elevator Mode (click a point to place an elevator, connecting every floor by default)"));
		AddElevatorModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddElevatorModeClicked);
		AddEscalatorModeButton = AddModeButton(TEXT("Add_Escalator_Button"), TEXT("Icon_AddEscalator"), TEXT("Add Escalator Mode (click a point to place an escalator, connecting the floor above and below by default)"));
		AddEscalatorModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddEscalatorModeClicked);

		// Floor panel - added below, under RightSidebar, below Back. Always visible
		// (unlike the Properties panel next to it), since switching floors should be available no
		// matter what's selected.
		FloorPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_Floor"));
		FloorPanel->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.9f));
		FloorPanel->SetPadding(FMargin(14.f));

		UVerticalBox* FloorStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FloorStack"));
		FloorPanel->SetContent(FloorStack);

		UTextBlock* FloorTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_FloorTitle"));
		FloorTitle->SetText(FText::FromString(TEXT("Floor")));
		FloorTitle->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UVerticalBoxSlot* FloorTitleSlot = FloorStack->AddChildToVerticalBox(FloorTitle);
		FloorTitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		FloorListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("FloorListBox"));
		// 3 rows tall (row height + 4px padding each, see RefreshFloorList) caps the list without
		// hiding the scrollbar UScrollBox already shows once content overflows that.
		USizeBox* FloorListSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FloorListSizeBox"));
		FloorListSizeBox->SetHeightOverride((StandardButtonHeight + 4.f) * 3.f);
		FloorListSizeBox->SetWidthOverride(StandardButtonWidth);
		FloorListSizeBox->SetContent(FloorListBox);
		UVerticalBoxSlot* FloorListSlot = FloorStack->AddChildToVerticalBox(FloorListSizeBox);
		FloorListSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		AddFloorButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_AddFloor"));
		AddFloorButton->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		UTextBlock* AddFloorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Btn_AddFloorLabel"));
		AddFloorText->SetText(FText::FromString(TEXT("+ Add floor")));
		AddFloorText->SetJustification(ETextJustify::Center);
		AddFloorText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardTextWrap(AddFloorText);
		USizeBox* AddFloorSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Btn_AddFloorSize"));
		AddFloorSizeBox->SetWidthOverride(StandardButtonWidth);
		AddFloorSizeBox->SetMinDesiredHeight(StandardButtonHeight);
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
		ApplyStandardTextWrap(RemoveFloorText);
		USizeBox* RemoveFloorSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Btn_RemoveFloorSize"));
		RemoveFloorSizeBox->SetWidthOverride(StandardButtonWidth);
		RemoveFloorSizeBox->SetMinDesiredHeight(StandardButtonHeight);
		RemoveFloorSizeBox->SetContent(RemoveFloorText);
		RemoveFloorButton->SetContent(RemoveFloorSizeBox);
		FloorStack->AddChildToVerticalBox(RemoveFloorButton);

		// Right sidebar: Floor panel + merged Properties panel, stacked in one auto-sizing column
		// below Back. Wrapped in a visible background bar (RightSidebarBar), same reasoning and
		// full-height stretch as LeftSidebarBar. Both FloorPanel and PropertiesPanel are forced to
		// the same explicit width (SidebarPanelWidth) via their own SizeBox, so they always line up
		// exactly instead of each auto-sizing to its own content (the connector floor checklist in
		// particular could otherwise grow wider than Floor panel).
		constexpr float SidebarPanelWidth = 240.f;
		// PropertiesPanel/FloorPanel's own SetPadding(14.f) eats 14px on each side - this is what's
		// actually left for their content, and is what any text inside them (that isn't already
		// inside its own fixed-width button) needs to be hard-wrapped to. A SizeBox's WidthOverride
		// only constrains layout size, it doesn't clip - text wider than that still painted straight
		// through it and past the panel's edge until it was also given SetAutoWrapText(true) at this
		// same width.
		constexpr float SidebarContentWidth = SidebarPanelWidth - 28.f;

		// For a standalone label (its own full-width row, not sharing a HorizontalBox with an input),
		// hard-wraps it to SidebarContentWidth the same way FloorsWrapSizeBox does - covers
		// PropertiesTitleText ("Room property"/"Wall property"/...) and DoorRoomText/ConnectorTypeText,
		// whose content is dynamic (room/connector names) and so can't just be eyeballed to fit.
		auto WrapLabelToPanelWidth = [this, SidebarContentWidth](UTextBlock* TextBlock, const TCHAR* SizeBoxName) -> USizeBox*
		{
			TextBlock->SetAutoWrapText(true);
			USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(SizeBoxName));
			SizeBox->SetWidthOverride(SidebarContentWidth);
			SizeBox->SetContent(TextBlock);
			return SizeBox;
		};

		UBorder* RightSidebarBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_RightSidebarBar"));
		RightSidebarBar->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.85f));
		RightSidebarBar->SetPadding(FMargin(10.f));

		UVerticalBox* RightSidebar = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RightSidebar"));
		RightSidebarBar->SetContent(RightSidebar);

		RightSidebarCanvasSlot = Canvas->AddChildToCanvas(RightSidebarBar);
		RightSidebarCanvasSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 1.f));
		RightSidebarCanvasSlot->SetAlignment(FVector2D(1.f, 0.f));
		RightSidebarCanvasSlot->SetOffsets(FMargin(-20.f, SidebarTopMargin, RightSidebarWidth, SidebarBottomMargin));
		RightSidebarCanvasSlot->SetZOrder(2);

		// Drag handle glued to the bar's left edge - mirrors LeftSidebarResizeHandle, but this bar is
		// anchored/aligned to the viewport's right edge, so the handle's own Offsets.Left has to sit
		// (20 + RightSidebarWidth) to the left of that same anchor point to end up touching the bar's
		// left edge.
		RightSidebarResizeHandle = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_RightSidebarResizeHandle"));
		RightSidebarResizeHandle->SetBrushColor(FLinearColor(0.4f, 0.4f, 0.4f, 0.6f));
		RightSidebarResizeHandle->SetCursor(EMouseCursor::ResizeLeftRight);
		RightSidebarResizeHandle->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnRightSidebarResizeMouseButtonDown);
		RightSidebarResizeHandle->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnRightSidebarResizeMouseMove);
		RightSidebarResizeHandle->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnRightSidebarResizeMouseButtonUp);
		RightSidebarResizeHandleSlot = Canvas->AddChildToCanvas(RightSidebarResizeHandle);
		RightSidebarResizeHandleSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 1.f));
		RightSidebarResizeHandleSlot->SetAlignment(FVector2D(1.f, 0.f));
		RightSidebarResizeHandleSlot->SetOffsets(FMargin(-20.f - RightSidebarWidth, SidebarTopMargin, SidebarResizeHandleWidth, SidebarBottomMargin));
		RightSidebarResizeHandleSlot->SetZOrder(3);

		// Back - first row of the right sidebar (was a separate floating button above the bar).
		BackButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_Back"));
		BackButton->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		UTextBlock* BackText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Btn_BackLabel"));
		BackText->SetText(FText::FromString(TEXT("Back")));
		BackText->SetJustification(ETextJustify::Center);
		BackText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardTextWrap(BackText);
		USizeBox* BackSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Btn_BackSize"));
		BackSizeBox->SetWidthOverride(StandardButtonWidth);
		BackSizeBox->SetMinDesiredHeight(StandardButtonHeight);
		BackSizeBox->SetContent(BackText);
		BackButton->SetContent(BackSizeBox);
		UVerticalBoxSlot* BackSlot = RightSidebar->AddChildToVerticalBox(BackButton);
		BackSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

		USizeBox* FloorPanelSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FloorPanelSizeBox"));
		FloorPanelSizeBox->SetWidthOverride(SidebarPanelWidth);
		FloorPanelSizeBox->SetContent(FloorPanel);
		UVerticalBoxSlot* FloorPanelSlot = RightSidebar->AddChildToVerticalBox(FloorPanelSizeBox);
		FloorPanelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

		// Properties panel - merges what used to be three separately-positioned Door/Room/Connector
		// panels into one (selection between them is already mutually exclusive at the PC level);
		// only one of DoorFieldsBox/RoomFieldsBox/ConnectorFieldsBox is ever visible at a time - see
		// HandleSelectedDoorChanged/HandleSelectedRoomChanged/HandleSelectedConnectorChanged.
		PropertiesPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_Properties"));
		PropertiesPanel->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.9f));
		PropertiesPanel->SetPadding(FMargin(14.f));
		PropertiesPanel->SetVisibility(ESlateVisibility::Collapsed);

		UVerticalBox* PropertiesStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PropertiesStack"));
		PropertiesPanel->SetContent(PropertiesStack);

		PropertiesTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_PropertiesTitle"));
		PropertiesTitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UVerticalBoxSlot* PropertiesTitleSlot = PropertiesStack->AddChildToVerticalBox(WrapLabelToPanelWidth(PropertiesTitleText, TEXT("Txt_PropertiesTitleSize")));
		PropertiesTitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		// -- Door fields --
		DoorFieldsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DoorFieldsBox"));
		DoorFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
		PropertiesStack->AddChildToVerticalBox(DoorFieldsBox);

		DoorRoomText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_DoorRoom"));
		DoorRoomText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UVerticalBoxSlot* DoorRoomSlot = DoorFieldsBox->AddChildToVerticalBox(WrapLabelToPanelWidth(DoorRoomText, TEXT("Txt_DoorRoomSize")));
		DoorRoomSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		UHorizontalBox* ConnectsToRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ConnectsToRow"));
		// Neither this row nor RoomNameRow below had any width constraint before - at a narrow panel
		// width, the dropdown/text box's own natural size could push past the panel's edge instead of
		// shrinking with it. Wrapped in a SizeBox (tracked in RefreshSidebarContentWidths the same as
		// everything else) with the combo box itself set to Fill so it's what actually shrinks, not
		// the "Connect to" label.
		ConnectsToRowSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ConnectsToRowSizeBox->SetContent(ConnectsToRow);
		DoorFieldsBox->AddChildToVerticalBox(ConnectsToRowSizeBox);

		UTextBlock* ConnectsToLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_ConnectsToLabel"));
		ConnectsToLabel->SetText(FText::FromString(TEXT("Connect to")));
		ConnectsToLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UHorizontalBoxSlot* ConnectsToLabelSlot = ConnectsToRow->AddChildToHorizontalBox(ConnectsToLabel);
		ConnectsToLabelSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		ConnectsToLabelSlot->SetVerticalAlignment(VAlign_Center);

		ConnectsToComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Cmb_ConnectsTo"));
		ConnectsToComboBox->OnGenerateWidgetEvent.BindDynamic(this, &UInteriorScreenWidget::HandleGenerateComboBoxItem);
		UHorizontalBoxSlot* ConnectsToComboSlot = ConnectsToRow->AddChildToHorizontalBox(ConnectsToComboBox);
		ConnectsToComboSlot->SetHorizontalAlignment(HAlign_Fill);
		ConnectsToComboSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		ApplyDoorButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_ApplyDoor"));
		ApplyDoorButton->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		UTextBlock* ApplyDoorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Btn_ApplyDoorLabel"));
		ApplyDoorText->SetText(FText::FromString(TEXT("Apply")));
		ApplyDoorText->SetJustification(ETextJustify::Center);
		ApplyDoorText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardTextWrap(ApplyDoorText);
		USizeBox* ApplyDoorSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Btn_ApplyDoorSize"));
		ApplyDoorSizeBox->SetWidthOverride(StandardButtonWidth);
		ApplyDoorSizeBox->SetMinDesiredHeight(StandardButtonHeight);
		ApplyDoorSizeBox->SetContent(ApplyDoorText);
		ApplyDoorButton->SetContent(ApplyDoorSizeBox);
		UVerticalBoxSlot* ApplyDoorSlot = DoorFieldsBox->AddChildToVerticalBox(ApplyDoorButton);
		ApplyDoorSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));

		// -- Room/Wall fields --
		RoomFieldsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoomFieldsBox"));
		RoomFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
		PropertiesStack->AddChildToVerticalBox(RoomFieldsBox);

		UHorizontalBox* RoomNameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RoomNameRow"));
		// See ConnectsToRow's comment above - same missing-width-constraint issue.
		RoomNameRowSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		RoomNameRowSizeBox->SetContent(RoomNameRow);
		RoomFieldsBox->AddChildToVerticalBox(RoomNameRowSizeBox);

		UTextBlock* RoomNameLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_RoomNameLabel"));
		RoomNameLabel->SetText(FText::FromString(TEXT("Name :")));
		RoomNameLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UHorizontalBoxSlot* RoomNameLabelSlot = RoomNameRow->AddChildToHorizontalBox(RoomNameLabel);
		RoomNameLabelSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		RoomNameLabelSlot->SetVerticalAlignment(VAlign_Center);

		RoomNameTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_RoomName"));
		// Back to Fill (matches ConnectsToComboBox) so this still tracks the panel width like
		// everything else, instead of sitting at a fixed size - readability at a narrow width is
		// handled by raising SidebarMinWidth instead (see its own comment) so there's just always
		// enough room, rather than exempting this one element from the "everything is linked" rule.
		UHorizontalBoxSlot* RoomNameBoxSlot = RoomNameRow->AddChildToHorizontalBox(RoomNameTextBox);
		RoomNameBoxSlot->SetVerticalAlignment(VAlign_Center);
		RoomNameBoxSlot->SetHorizontalAlignment(HAlign_Fill);
		RoomNameBoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		ApplyRoomButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_ApplyRoom"));
		ApplyRoomButton->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		UTextBlock* ApplyRoomText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Btn_ApplyRoomLabel"));
		ApplyRoomText->SetText(FText::FromString(TEXT("Apply")));
		ApplyRoomText->SetJustification(ETextJustify::Center);
		ApplyRoomText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardTextWrap(ApplyRoomText);
		USizeBox* ApplyRoomSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Btn_ApplyRoomSize"));
		ApplyRoomSizeBox->SetWidthOverride(StandardButtonWidth);
		ApplyRoomSizeBox->SetMinDesiredHeight(StandardButtonHeight);
		ApplyRoomSizeBox->SetContent(ApplyRoomText);
		ApplyRoomButton->SetContent(ApplyRoomSizeBox);
		UVerticalBoxSlot* ApplyRoomSlot = RoomFieldsBox->AddChildToVerticalBox(ApplyRoomButton);
		ApplyRoomSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));

		// -- Connector (elevator/stairs/escalator) fields --
		ConnectorFieldsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ConnectorFieldsBox"));
		ConnectorFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
		PropertiesStack->AddChildToVerticalBox(ConnectorFieldsBox);

		ConnectorTypeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_ConnectorType"));
		ConnectorTypeText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UVerticalBoxSlot* ConnectorTypeSlot = ConnectorFieldsBox->AddChildToVerticalBox(WrapLabelToPanelWidth(ConnectorTypeText, TEXT("Txt_ConnectorTypeSize")));
		ConnectorTypeSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		UTextBlock* FloorsLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_FloorsLabel"));
		FloorsLabel->SetText(FText::FromString(TEXT("Floors :")));
		FloorsLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UVerticalBoxSlot* FloorsLabelSlot = ConnectorFieldsBox->AddChildToVerticalBox(FloorsLabel);
		FloorsLabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

		UWrapBox* FloorsWrap = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("FloorsWrap"));
		FloorsWrap->SetInnerSlotPadding(FVector2D(4.f, 2.f));
		// Hard-capped to PropertiesPanel's content width (SidebarPanelWidth minus its own 14px-a-side
		// padding) so the floor checkboxes wrap onto more lines instead of pushing the panel wider
		// than FloorPanel above it - a WrapBox with nothing constraining its width just grows to fit
		// everything on one line.
		FloorsWrapSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FloorsWrapSizeBox"));
		FloorsWrapSizeBox->SetWidthOverride(SidebarContentWidth);
		FloorsWrapSizeBox->SetContent(FloorsWrap);
		UVerticalBoxSlot* FloorsWrapSlot = ConnectorFieldsBox->AddChildToVerticalBox(FloorsWrapSizeBox);
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
		ApplyStandardTextWrap(ApplyConnectorText);
		USizeBox* ApplyConnectorSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Btn_ApplyConnectorSize"));
		ApplyConnectorSizeBox->SetWidthOverride(StandardButtonWidth);
		ApplyConnectorSizeBox->SetMinDesiredHeight(StandardButtonHeight);
		ApplyConnectorSizeBox->SetContent(ApplyConnectorText);
		ApplyConnectorButton->SetContent(ApplyConnectorSizeBox);
		UVerticalBoxSlot* ApplyConnectorSlot = ConnectorFieldsBox->AddChildToVerticalBox(ApplyConnectorButton);
		ApplyConnectorSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));

		USizeBox* PropertiesPanelSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PropertiesPanelSizeBox"));
		PropertiesPanelSizeBox->SetWidthOverride(SidebarPanelWidth);
		PropertiesPanelSizeBox->SetContent(PropertiesPanel);
		RightSidebar->AddChildToVerticalBox(PropertiesPanelSizeBox);
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
		PC->OnEditModeChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleEditModeChanged);
	}

	RefreshBuildingSwitcher();
	RefreshFloorList();
	RefreshModeButtonHighlights();
	// Construction hard-codes each SizeBox's initial width using the same layout numbers this
	// formula would compute at the default LeftSidebarWidth/RightSidebarWidth anyway - this just
	// makes that explicit/guaranteed instead of two copies of the same math staying in sync by luck.
	RefreshSidebarContentWidths();
}

void UInteriorScreenWidget::NativeDestruct()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->OnSelectedDoorChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleSelectedDoorChanged);
		PC->OnSelectedRoomChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleSelectedRoomChanged);
		PC->OnSelectedConnectorChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleSelectedConnectorChanged);
		PC->OnEditModeChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleEditModeChanged);
	}

	Super::NativeDestruct();
}

FEventReply UInteriorScreenWidget::OnLeftSidebarResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !LeftSidebarResizeHandle)
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	bIsResizingLeftSidebar = true;
	LeftSidebarResizeStartMouseX = MouseEvent.GetScreenSpacePosition().X;
	LeftSidebarResizeStartWidth = LeftSidebarWidth;
	FEventReply Reply = UWidgetBlueprintLibrary::Handled();
	return UWidgetBlueprintLibrary::CaptureMouse(Reply, LeftSidebarResizeHandle);
}

FEventReply UInteriorScreenWidget::OnLeftSidebarResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bIsResizingLeftSidebar)
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		bIsResizingLeftSidebar = false;
		FEventReply Reply = UWidgetBlueprintLibrary::Handled();
		return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
	}

	const float DeltaX = MouseEvent.GetScreenSpacePosition().X - LeftSidebarResizeStartMouseX;
	LeftSidebarWidth = FMath::Clamp(LeftSidebarResizeStartWidth + DeltaX, SidebarMinWidth, SidebarMaxWidth);

	if (LeftSidebarCanvasSlot)
	{
		LeftSidebarCanvasSlot->SetOffsets(FMargin(10.f, SidebarTopMargin, LeftSidebarWidth, SidebarBottomMargin));
	}
	if (LeftSidebarResizeHandleSlot)
	{
		LeftSidebarResizeHandleSlot->SetOffsets(FMargin(10.f + LeftSidebarWidth, SidebarTopMargin, SidebarResizeHandleWidth, SidebarBottomMargin));
	}
	RefreshSidebarContentWidths();

	return UWidgetBlueprintLibrary::Handled();
}

FEventReply UInteriorScreenWidget::OnLeftSidebarResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsResizingLeftSidebar && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsResizingLeftSidebar = false;
		FEventReply Reply = UWidgetBlueprintLibrary::Handled();
		return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
	}

	return UWidgetBlueprintLibrary::Unhandled();
}

FEventReply UInteriorScreenWidget::OnRightSidebarResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !RightSidebarResizeHandle)
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	bIsResizingRightSidebar = true;
	RightSidebarResizeStartMouseX = MouseEvent.GetScreenSpacePosition().X;
	RightSidebarResizeStartWidth = RightSidebarWidth;
	FEventReply Reply = UWidgetBlueprintLibrary::Handled();
	return UWidgetBlueprintLibrary::CaptureMouse(Reply, RightSidebarResizeHandle);
}

FEventReply UInteriorScreenWidget::OnRightSidebarResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bIsResizingRightSidebar)
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		bIsResizingRightSidebar = false;
		FEventReply Reply = UWidgetBlueprintLibrary::Handled();
		return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
	}

	// This bar is anchored/aligned to the right edge, so moving the mouse LEFT (negative delta)
	// grows it, the opposite of the left sidebar.
	const float DeltaX = MouseEvent.GetScreenSpacePosition().X - RightSidebarResizeStartMouseX;
	RightSidebarWidth = FMath::Clamp(RightSidebarResizeStartWidth - DeltaX, SidebarMinWidth, SidebarMaxWidth);

	if (RightSidebarCanvasSlot)
	{
		RightSidebarCanvasSlot->SetOffsets(FMargin(-20.f, SidebarTopMargin, RightSidebarWidth, SidebarBottomMargin));
	}
	if (RightSidebarResizeHandleSlot)
	{
		RightSidebarResizeHandleSlot->SetOffsets(FMargin(-20.f - RightSidebarWidth, SidebarTopMargin, SidebarResizeHandleWidth, SidebarBottomMargin));
	}
	RefreshSidebarContentWidths();

	return UWidgetBlueprintLibrary::Handled();
}

FEventReply UInteriorScreenWidget::OnRightSidebarResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsResizingRightSidebar && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsResizingRightSidebar = false;
		FEventReply Reply = UWidgetBlueprintLibrary::Handled();
		return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
	}

	return UWidgetBlueprintLibrary::Unhandled();
}

void UInteriorScreenWidget::HandleEditModeChanged()
{
	RefreshModeButtonHighlights();
}

void UInteriorScreenWidget::RefreshSidebarContentWidths()
{
	// Left: building switcher combo box tracks the bar (minus its own 10px-a-side padding and the
	// edit-name button beside it). Mode-toolbar icons deliberately stay fixed 48x48 - stretching a
	// square icon into a rectangle would just look broken.
	if (BuildingSwitcherComboBox)
	{
		if (USizeBox* ComboSizeBox = Cast<USizeBox>(BuildingSwitcherComboBox->GetParent()))
		{
			ComboSizeBox->SetWidthOverride(FMath::Max(80.f, LeftSidebarWidth - 85.f));
		}
	}

	// Right: Floor/Properties panel borders track the bar (minus its own 10px-a-side padding);
	// everything inside them tracks the panel in turn (minus ITS 14px-a-side padding) - see
	// ComputeRightSidebarButtonWidth.
	const float RightPanelWidth = FMath::Max(80.f, RightSidebarWidth - 20.f);
	const float RightButtonWidth = ComputeRightSidebarButtonWidth();

	auto ResizePanel = [RightPanelWidth](UWidget* PanelContent)
	{
		if (PanelContent)
		{
			if (USizeBox* SizeBox = Cast<USizeBox>(PanelContent->GetParent()))
			{
				SizeBox->SetWidthOverride(RightPanelWidth);
			}
		}
	};
	ResizePanel(FloorPanel);
	ResizePanel(PropertiesPanel);

	auto ResizeButton = [RightButtonWidth](UButton* Button)
	{
		if (Button)
		{
			if (USizeBox* SizeBox = Cast<USizeBox>(Button->GetContent()))
			{
				SizeBox->SetWidthOverride(RightButtonWidth);
			}
		}
	};
	ResizeButton(BackButton);
	ResizeButton(AddFloorButton);
	ResizeButton(RemoveFloorButton);
	ResizeButton(ApplyDoorButton);
	ResizeButton(ApplyRoomButton);
	ResizeButton(ApplyConnectorButton);

	auto ResizeLabelWrap = [RightButtonWidth](UTextBlock* TextBlock)
	{
		if (TextBlock)
		{
			if (USizeBox* SizeBox = Cast<USizeBox>(TextBlock->GetParent()))
			{
				SizeBox->SetWidthOverride(RightButtonWidth);
			}
		}
	};
	ResizeLabelWrap(PropertiesTitleText);
	ResizeLabelWrap(DoorRoomText);
	ResizeLabelWrap(ConnectorTypeText);

	if (ConnectsToRowSizeBox)
	{
		ConnectsToRowSizeBox->SetWidthOverride(RightButtonWidth);
	}
	if (RoomNameRowSizeBox)
	{
		RoomNameRowSizeBox->SetWidthOverride(RightButtonWidth);
	}

	if (FloorsWrapSizeBox)
	{
		FloorsWrapSizeBox->SetWidthOverride(RightButtonWidth);
	}

	if (FloorListBox)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(FloorListBox->GetParent()))
		{
			SizeBox->SetWidthOverride(RightButtonWidth);
		}
		for (int32 Index = 0; Index < FloorListBox->GetChildrenCount(); ++Index)
		{
			if (UFloorListEntryWidget* Entry = Cast<UFloorListEntryWidget>(FloorListBox->GetChildAt(Index)))
			{
				Entry->SetWidth(RightButtonWidth);
			}
		}
	}
}

void UInteriorScreenWidget::RefreshModeButtonHighlights()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	const FLinearColor ActiveColor(0.2f, 0.75f, 0.3f, 1.f);
	const FLinearColor InactiveColor(0.6f, 0.6f, 0.6f, 1.f);
	auto SetHighlight = [&ActiveColor, &InactiveColor](UButton* Button, bool bActive)
	{
		if (Button)
		{
			Button->SetBackgroundColor(bActive ? ActiveColor : InactiveColor);
		}
	};

	SetHighlight(SelectModeButton, PC->IsInSelectMode());
	SetHighlight(DrawRoomModeButton, PC->IsInDrawRoomMode());
	SetHighlight(DrawWallModeButton, PC->IsInDrawWallMode());
	SetHighlight(AddRectModeButton, PC->GetActivePrefabShape() == 1);
	SetHighlight(AddTriangleModeButton, PC->GetActivePrefabShape() == 2);
	SetHighlight(AddCircleModeButton, PC->GetActivePrefabShape() == 3);
	SetHighlight(AddDoorModeButton, PC->IsInAddDoorMode());
	SetHighlight(AddElevatorModeButton, PC->GetActiveConnectorType() == static_cast<int32>(EInteriorNavNodeType::Elevator));
	SetHighlight(AddStairsModeButton, PC->GetActiveConnectorType() == static_cast<int32>(EInteriorNavNodeType::Stairs));
	SetHighlight(AddEscalatorModeButton, PC->GetActiveConnectorType() == static_cast<int32>(EInteriorNavNodeType::Escalator));
}

void UInteriorScreenWidget::HandleSelectedDoorChanged(FString RoomId, FString DoorId)
{
	if (!DoorFieldsBox)
	{
		return;
	}

	if (RoomId.IsEmpty() && DoorId.IsEmpty())
	{
		DoorFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
		RefreshPropertiesPanelVisibility();
		return;
	}

	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	DoorFieldsBox->SetVisibility(ESlateVisibility::Visible);
	if (RoomFieldsBox)
	{
		RoomFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (ConnectorFieldsBox)
	{
		ConnectorFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (PropertiesTitleText)
	{
		PropertiesTitleText->SetText(FText::FromString(TEXT("Door property")));
	}

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

	RefreshPropertiesPanelVisibility();
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
	if (!RoomFieldsBox)
	{
		return;
	}

	if (RoomId.IsEmpty())
	{
		RoomFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
		RefreshPropertiesPanelVisibility();
		return;
	}

	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	RoomFieldsBox->SetVisibility(ESlateVisibility::Visible);
	if (DoorFieldsBox)
	{
		DoorFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (ConnectorFieldsBox)
	{
		ConnectorFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (PropertiesTitleText)
	{
		PropertiesTitleText->SetText(FText::FromString(PC->IsSelectedRoomClosedLoop() ? TEXT("Room property") : TEXT("Wall property")));
	}
	if (RoomNameTextBox)
	{
		RoomNameTextBox->SetText(FText::FromString(PC->GetSelectedRoomName()));
	}

	RefreshPropertiesPanelVisibility();
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
	if (!ConnectorFieldsBox)
	{
		return;
	}

	if (GroupId.IsEmpty())
	{
		ConnectorFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
		RefreshPropertiesPanelVisibility();
		return;
	}

	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	ConnectorFieldsBox->SetVisibility(ESlateVisibility::Visible);
	if (DoorFieldsBox)
	{
		DoorFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (RoomFieldsBox)
	{
		RoomFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (PropertiesTitleText)
	{
		PropertiesTitleText->SetText(FText::FromString(TEXT("Connector property")));
	}

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

	RefreshPropertiesPanelVisibility();
}

void UInteriorScreenWidget::RefreshPropertiesPanelVisibility()
{
	if (!PropertiesPanel)
	{
		return;
	}

	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	const bool bAnySelected = PC && (!PC->GetSelectedRoomId().IsEmpty() || !PC->GetSelectedDoorId().IsEmpty() || !PC->GetSelectedConnectorGroupId().IsEmpty());
	PropertiesPanel->SetVisibility(bAnySelected ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
		Entry->SetWidth(ComputeRightSidebarButtonWidth());
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

UWidget* UInteriorScreenWidget::HandleGenerateComboBoxItem(FString Item)
{
	UTextBlock* ItemText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	ItemText->SetText(FText::FromString(Item));
	ItemText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	return ItemText;
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
