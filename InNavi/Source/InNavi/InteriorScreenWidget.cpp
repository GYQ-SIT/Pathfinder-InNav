#include "InteriorScreenWidget.h"

#include "ConfirmDialogWidget.h"
#include "EditScreenWidget.h"
#include "FloorListEntryWidget.h"
#include "InteriorFloorPlanWidget.h"
#include "InteriorTypes.h"
#include "LayerRowWidget.h"
#include "NavPlayerController.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
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
#include "Components/PanelWidget.h"
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
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

UInteriorScreenWidget::UInteriorScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitializeDockRuntimeArrays();
}

void UInteriorScreenWidget::InitializeDockRuntimeArrays()
{
	DockPanelBorders.Init(nullptr, NumDockPanels);
	DockPanelCanvasSlots.Init(nullptr, NumDockPanels);
	DockResizeHandles.Init(nullptr, NumDockPanels);
	DockResizeHandleSlots.Init(nullptr, NumDockPanels);
	DockLeftResizeHandles.Init(nullptr, NumDockPanels);
	DockLeftResizeHandleSlots.Init(nullptr, NumDockPanels);
	SplitterHandles.Init(nullptr, MaxDockSplitters);
	SplitterCanvasSlots.Init(nullptr, MaxDockSplitters);

	DockPanelWidths.Init(SidebarMinWidth, NumDockPanels);
	DockPanelColumnIndex.Init(INDEX_NONE, NumDockPanels);
	bDockPanelIsDraggingMove.Init(false, NumDockPanels);
	DockPanelMoveDragStartMouse.Init(FVector2D::ZeroVector, NumDockPanels);
	DockPanelMoveDragStartPosition.Init(FVector2D::ZeroVector, NumDockPanels);
	bDockPanelIsResizingWidth.Init(false, NumDockPanels);
	DockPanelResizeStartMouseX.Init(0.f, NumDockPanels);
	DockPanelResizeStartWidth.Init(0.f, NumDockPanels);

	bDockPanelIsResizingLeftWidth.Init(false, NumDockPanels);
	DockPanelLeftResizeStartMouseX.Init(0.f, NumDockPanels);
	DockPanelLeftResizeStartWidth.Init(0.f, NumDockPanels);
	DockPanelLeftResizeStartPanelX.Init(0.f, NumDockPanels);

	SplitterColumnIndex.Init(INDEX_NONE, MaxDockSplitters);
	SplitterUpperIndexInColumn.Init(INDEX_NONE, MaxDockSplitters);
	bSplitterIsDragging.Init(false, MaxDockSplitters);
	SplitterDragStartMouseY.Init(0.f, MaxDockSplitters);
	SplitterDragStartFractions.Init(TArray<float>(), MaxDockSplitters);
}

TSharedRef<SWidget> UInteriorScreenWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("InteriorScreenCanvas"));
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Canvas;
		RootCanvas = Canvas;

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

		// Each of the 3 dockable panels (Toolbar/Floor/Properties) is a UBorder wrapped in a
		// USizeBox, added DIRECTLY to Canvas (no shared bar/VerticalBox) - the SizeBox is what
		// actually gets the CanvasPanelSlot, so it alone controls the panel's rendered width/height
		// (width always via WidthOverride; height via HeightOverride while docked in a column, or
		// left auto/natural via ClearHeightOverride while floating - see RefreshDockLayout). A
		// drag-to-resize handle (width only) sits glued to each panel's own right edge, same
		// SetCursor/CaptureMouse pattern as before, just per-panel instead of per-bar. All layout
		// math (position/size/handle placement) happens later in RefreshDockLayout - this just
		// constructs the widgets and gives them a starting Canvas slot; InitializeDefaultDockLayout
		// (called once from NativeConstruct) positions everything for the first time.
		auto ConstructDockPanel = [this, Canvas](EDockPanelId Id, const TCHAR* BorderName, const FLinearColor& BrushColor, float BorderPadding) -> UBorder*
		{
			const int32 Index = (int32)Id;

			UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(BorderName));
			Border->SetBrushColor(BrushColor);
			Border->SetPadding(FMargin(BorderPadding));
			// BindDynamic is a macro that stringifies its function-pointer argument at compile time
			// (to look up the UFUNCTION by name for reflection-based invocation) - it can't take a
			// ternary/runtime-selected pointer, only a literal &Class::Method token. So this lambda
			// only constructs the widgets; each panel's actual event bindings are 3 tiny literal
			// BindDynamic calls right after its ConstructDockPanel call site below.
			DockPanelBorders[Index] = Border;

			USizeBox* Wrapper = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(BorderName) + TEXT("SizeBox")));
			Wrapper->SetWidthOverride(DockPanelWidths[Index]);
			Wrapper->SetContent(Border);

			UCanvasPanelSlot* PanelCanvasSlot = Canvas->AddChildToCanvas(Wrapper);
			PanelCanvasSlot->SetAnchors(FAnchors(0.f, 0.f));
			PanelCanvasSlot->SetAlignment(FVector2D(0.f, 0.f));
			PanelCanvasSlot->SetAutoSize(true);
			PanelCanvasSlot->SetZOrder(2);
			DockPanelCanvasSlots[Index] = PanelCanvasSlot;

			UBorder* Handle = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(FString(BorderName) + TEXT("ResizeHandle")));
			Handle->SetBrushColor(FLinearColor(0.4f, 0.4f, 0.4f, 0.6f));
			Handle->SetCursor(EMouseCursor::ResizeLeftRight);
			// Same reasoning as Border above - bound with literal calls at each call site instead.
			DockResizeHandles[Index] = Handle;

			UCanvasPanelSlot* HandleSlot = Canvas->AddChildToCanvas(Handle);
			HandleSlot->SetAnchors(FAnchors(0.f, 0.f));
			HandleSlot->SetAlignment(FVector2D(0.f, 0.f));
			HandleSlot->SetZOrder(3);
			DockResizeHandleSlots[Index] = HandleSlot;

			// A second handle on the panel's LEFT edge too - free-floating panels can end up
			// anywhere on screen, and a panel sitting near the right edge of the viewport can have
			// its own right edge crowded right up against it (or past it), making the right handle
			// awkward or impossible to grab. Dragging this one keeps the panel's RIGHT edge fixed
			// and grows/shrinks it from the left instead (see HandleLeftResizeMouseMove).
			UBorder* LeftHandle = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(FString(BorderName) + TEXT("LeftResizeHandle")));
			LeftHandle->SetBrushColor(FLinearColor(0.4f, 0.4f, 0.4f, 0.6f));
			LeftHandle->SetCursor(EMouseCursor::ResizeLeftRight);
			DockLeftResizeHandles[Index] = LeftHandle;

			UCanvasPanelSlot* LeftHandleSlot = Canvas->AddChildToCanvas(LeftHandle);
			LeftHandleSlot->SetAnchors(FAnchors(0.f, 0.f));
			LeftHandleSlot->SetAlignment(FVector2D(0.f, 0.f));
			LeftHandleSlot->SetZOrder(3);
			DockLeftResizeHandleSlots[Index] = LeftHandleSlot;

			return Border;
		};

		// Two splitter handles - the most that can ever be needed at once (3 panels docked together
		// in one column has 2 internal boundaries). RefreshSplitterLayout shows/hides and repositions
		// these to match whichever columns actually exist right now.
		for (int32 SplitterIndex = 0; SplitterIndex < MaxDockSplitters; ++SplitterIndex)
		{
			UBorder* Splitter = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*FString::Printf(TEXT("DockSplitter%d"), SplitterIndex)));
			Splitter->SetBrushColor(FLinearColor(0.4f, 0.4f, 0.4f, 0.6f));
			Splitter->SetCursor(EMouseCursor::ResizeUpDown);
			Splitter->SetVisibility(ESlateVisibility::Collapsed);
			if (SplitterIndex == 0)
			{
				Splitter->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter0MouseButtonDown);
				Splitter->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter0MouseMove);
				Splitter->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter0MouseButtonUp);
			}
			else if (SplitterIndex == 1)
			{
				Splitter->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter1MouseButtonDown);
				Splitter->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter1MouseMove);
				Splitter->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter1MouseButtonUp);
			}
			else
			{
				Splitter->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter2MouseButtonDown);
				Splitter->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter2MouseMove);
				Splitter->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter2MouseButtonUp);
			}
			SplitterHandles[SplitterIndex] = Splitter;

			UCanvasPanelSlot* SplitterSlot = Canvas->AddChildToCanvas(Splitter);
			SplitterSlot->SetAnchors(FAnchors(0.f, 0.f));
			SplitterSlot->SetAlignment(FVector2D(0.f, 0.f));
			SplitterSlot->SetZOrder(4);
			SplitterCanvasSlots[SplitterIndex] = SplitterSlot;
		}

		// Panels flush against each other in a column should read as one continuous surface, not
		// separate boxes with visible seams - so all 4 share this exact background color.
		const FLinearColor DockPanelColor(0.03f, 0.03f, 0.03f, 0.88f);

		// -- Toolbar panel: building switcher on top, then the mode toolbar grouped into Default/
		// Draw/Navigate sections (matching the hand-drawn layout) - one VerticalBox so everything
		// auto-stacks instead of each piece needing its own guessed Y position.
		ToolbarPanel = ConstructDockPanel(EDockPanelId::Toolbar, TEXT("Pnl_Toolbar"), DockPanelColor, 10.f);
		ToolbarPanel->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnToolbarPanelMouseButtonDown);
		ToolbarPanel->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnToolbarPanelMouseMove);
		ToolbarPanel->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnToolbarPanelMouseButtonUp);
		DockResizeHandles[(int32)EDockPanelId::Toolbar]->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnToolbarResizeMouseButtonDown);
		DockResizeHandles[(int32)EDockPanelId::Toolbar]->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnToolbarResizeMouseMove);
		DockResizeHandles[(int32)EDockPanelId::Toolbar]->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnToolbarResizeMouseButtonUp);
		DockLeftResizeHandles[(int32)EDockPanelId::Toolbar]->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnToolbarLeftResizeMouseButtonDown);
		DockLeftResizeHandles[(int32)EDockPanelId::Toolbar]->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnToolbarLeftResizeMouseMove);
		DockLeftResizeHandles[(int32)EDockPanelId::Toolbar]->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnToolbarLeftResizeMouseButtonUp);

		UVerticalBox* LeftSidebar = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LeftSidebar"));
		ToolbarPanel->SetContent(LeftSidebar);

		// Building switcher - lets the user jump straight to a different building's interior
		// without going back to the exterior first.
		UHorizontalBox* SwitcherRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BuildingSwitcherRow"));

		BuildingSwitcherComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Cbx_BuildingSwitcher"));
		BuildingSwitcherComboBox->OnGenerateWidgetEvent.BindDynamic(this, &UInteriorScreenWidget::HandleGenerateComboBoxItem);
		// Fixed width so a long building name can't push this row past the Toolbar panel's edge -
		// the combo button clips its own text at this width rather than growing to fit it. Kept in
		// sync with the panel's width as it's dragged by RefreshDockPanelContentWidths, which
		// recomputes this exact formula - keep the two in sync by hand if this one changes.
		USizeBox* BuildingSwitcherSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		BuildingSwitcherSizeBox->SetWidthOverride(DockPanelWidths[(int32)EDockPanelId::Toolbar] - 20.f - 100.f); // leaves ~100px for the edit-name button beside it, with slack
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
		// switcher's width formula budgeted for it, so it always overflowed past the panel's edge by
		// that fixed difference, independent of the panel's width (i.e. resizing never helped).
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
			UVerticalBoxSlot* GroupLabelSlot = LeftSidebar->AddChildToVerticalBox(GroupLabel);
			GroupLabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
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

			UVerticalBoxSlot* ButtonSlot = LeftSidebar->AddChildToVerticalBox(Button);
			ButtonSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
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

		// -- Floor panel --
		FloorPanel = ConstructDockPanel(EDockPanelId::Floor, TEXT("Pnl_Floor"), DockPanelColor, 14.f);
		FloorPanel->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnFloorPanelMouseButtonDown);
		FloorPanel->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnFloorPanelMouseMove);
		FloorPanel->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnFloorPanelMouseButtonUp);
		DockResizeHandles[(int32)EDockPanelId::Floor]->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnFloorResizeMouseButtonDown);
		DockResizeHandles[(int32)EDockPanelId::Floor]->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnFloorResizeMouseMove);
		DockResizeHandles[(int32)EDockPanelId::Floor]->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnFloorResizeMouseButtonUp);
		DockLeftResizeHandles[(int32)EDockPanelId::Floor]->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnFloorLeftResizeMouseButtonDown);
		DockLeftResizeHandles[(int32)EDockPanelId::Floor]->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnFloorLeftResizeMouseMove);
		DockLeftResizeHandles[(int32)EDockPanelId::Floor]->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnFloorLeftResizeMouseButtonUp);

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

		// PropertiesPanel/FloorPanel's own SetPadding(14.f) eats 14px on each side - this is what's
		// actually left for their content, and is what any text inside them (that isn't already
		// inside its own fixed-width button) needs to be hard-wrapped to. A SizeBox's WidthOverride
		// only constrains layout size, it doesn't clip - text wider than that still painted straight
		// through it and past the panel's edge until it was also given SetAutoWrapText(true) at this
		// same width. Computed from Floor's width (both panels always share a width once docked
		// together, and start out that way - see InitializeDefaultDockLayout).
		const float SidebarContentWidth = DockPanelWidths[(int32)EDockPanelId::Floor] - 28.f;

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

		// Back - fixed chrome pinned to the top-right corner, NOT part of the docking system (always
		// stays put, like a permanent close/back button in most editors) - point-anchored the same
		// way UNavigationPanelWidget's own floating panel is (FAnchors(1,0) + Alignment(1,0) +
		// SetAutoSize(true) = free position measured from the viewport's top-right corner).
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
		UCanvasPanelSlot* BackSlot = Canvas->AddChildToCanvas(BackButton);
		BackSlot->SetAnchors(FAnchors(1.f, 0.f));
		BackSlot->SetAlignment(FVector2D(1.f, 0.f));
		BackSlot->SetPosition(FVector2D(-20.f, SidebarTopMargin));
		BackSlot->SetAutoSize(true);
		BackSlot->SetZOrder(5);

		// -- Properties panel: merges what used to be three separately-positioned Door/Room/
		// Connector panels into one (selection between them is already mutually exclusive at the PC
		// level); only one of DoorFieldsBox/RoomFieldsBox/ConnectorFieldsBox is ever visible at a
		// time - see HandleSelectedDoorChanged/HandleSelectedRoomChanged/HandleSelectedConnectorChanged.
		PropertiesPanel = ConstructDockPanel(EDockPanelId::Properties, TEXT("Pnl_Properties"), DockPanelColor, 14.f);
		PropertiesPanel->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnPropertiesPanelMouseButtonDown);
		PropertiesPanel->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnPropertiesPanelMouseMove);
		PropertiesPanel->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnPropertiesPanelMouseButtonUp);
		DockResizeHandles[(int32)EDockPanelId::Properties]->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnPropertiesResizeMouseButtonDown);
		DockResizeHandles[(int32)EDockPanelId::Properties]->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnPropertiesResizeMouseMove);
		DockResizeHandles[(int32)EDockPanelId::Properties]->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnPropertiesResizeMouseButtonUp);
		DockLeftResizeHandles[(int32)EDockPanelId::Properties]->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnPropertiesLeftResizeMouseButtonDown);
		DockLeftResizeHandles[(int32)EDockPanelId::Properties]->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnPropertiesLeftResizeMouseMove);
		DockLeftResizeHandles[(int32)EDockPanelId::Properties]->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnPropertiesLeftResizeMouseButtonUp);

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
		// shrinking with it. Wrapped in a SizeBox (tracked in RefreshDockPanelContentWidths the same
		// as everything else) with the combo box itself set to Fill so it's what actually shrinks,
		// not the "Connect to" label.
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
		// Fill (matches ConnectsToComboBox) so this still tracks the panel width like everything
		// else, instead of sitting at a fixed size - readability at a narrow width is handled by
		// raising SidebarMinWidth instead (see its own comment) so there's just always enough room,
		// rather than exempting this one element from the "everything is linked" rule.
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
		// Hard-capped to PropertiesPanel's content width (panel width minus its own 14px-a-side
		// padding) so the floor checkboxes wrap onto more lines instead of pushing the panel wider
		// than Floor above it - a WrapBox with nothing constraining its width just grows to fit
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

		// Permanent panel, like Floor/Toolbar/Layers - always shown; RefreshPropertiesPanelVisibility
		// keeps its title as this neutral placeholder while nothing's selected instead of collapsing
		// the whole panel away.
		PropertiesPanel->SetVisibility(ESlateVisibility::Visible);
		PropertiesTitleText->SetText(FText::FromString(TEXT("Properties")));

		// -- Layers panel: a Hierarchy/Outliner-style tree over what the 2D floor plan actually
		// draws (Boundary/Walls/Path Nodes/I/O Nodes) - see the comment above
		// HandleLayerRowExpandClicked. Rows are ULayerRowWidget instances, built fresh each time by
		// RefreshLayersList rather than laid out here (same reasoning as FloorListBox's rows).
		LayersPanel = ConstructDockPanel(EDockPanelId::Layers, TEXT("Pnl_Layers"), DockPanelColor, 14.f);
		LayersPanel->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnLayersPanelMouseButtonDown);
		LayersPanel->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnLayersPanelMouseMove);
		LayersPanel->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnLayersPanelMouseButtonUp);
		DockResizeHandles[(int32)EDockPanelId::Layers]->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnLayersResizeMouseButtonDown);
		DockResizeHandles[(int32)EDockPanelId::Layers]->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnLayersResizeMouseMove);
		DockResizeHandles[(int32)EDockPanelId::Layers]->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnLayersResizeMouseButtonUp);
		DockLeftResizeHandles[(int32)EDockPanelId::Layers]->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnLayersLeftResizeMouseButtonDown);
		DockLeftResizeHandles[(int32)EDockPanelId::Layers]->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnLayersLeftResizeMouseMove);
		DockLeftResizeHandles[(int32)EDockPanelId::Layers]->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnLayersLeftResizeMouseButtonUp);

		UVerticalBox* LayersStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LayersStack"));
		LayersPanel->SetContent(LayersStack);

		UTextBlock* LayersTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_LayersTitle"));
		LayersTitle->SetText(FText::FromString(TEXT("Layers")));
		LayersTitle->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		UVerticalBoxSlot* LayersTitleSlot = LayersStack->AddChildToVerticalBox(LayersTitle);
		LayersTitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		LayersSearchBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_LayersSearch"));
		LayersSearchBox->SetHintText(FText::FromString(TEXT("Search")));
		LayersSearchBox->OnTextChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleLayersSearchTextChanged);
		USizeBox* LayersSearchSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Txt_LayersSearchSize"));
		LayersSearchSizeBox->SetWidthOverride(StandardButtonWidth);
		LayersSearchSizeBox->SetContent(LayersSearchBox);
		UVerticalBoxSlot* LayersSearchSlot = LayersStack->AddChildToVerticalBox(LayersSearchSizeBox);
		LayersSearchSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		LayersListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("LayersListBox"));
		// 8 rows tall by default - scrollable beyond that via UScrollBox's own scrollbar, same
		// reasoning as FloorListBox. Shrunk to fit whenever Layers is actually docked into a column
		// smaller than this - see LayersListDefaultHeight's own comment.
		USizeBox* LayersListSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LayersListSizeBox"));
		LayersListSizeBox->SetHeightOverride(LayersListDefaultHeight);
		LayersListSizeBox->SetWidthOverride(StandardButtonWidth);
		LayersListSizeBox->SetContent(LayersListBox);
		LayersStack->AddChildToVerticalBox(LayersListSizeBox);
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
		// Ctrl+S (or any other trigger of PC->SaveCurrentProject/SaveCurrentProjectAs) doesn't go
		// through this widget at all - this is what lets the dock layout still get saved when the
		// user saves that way instead of through Back's own confirm dialog.
		PC->OnProjectSaved.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleProjectSaved);
		// Same reasoning - the [/]/F/X floor keyboard shortcuts change the active floor without ever
		// going through this screen's own Floor panel handlers.
		PC->OnActiveFloorChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleActiveFloorChanged);
	}

	RefreshBuildingSwitcher();
	RefreshFloorList();
	RefreshLayersList();
	RefreshModeButtonHighlights();
	// Syncs Properties' placeholder title/visibility against PC's actual current selection state
	// (normally nothing yet, but defensive either way) before panels get positioned below.
	RefreshPropertiesPanelVisibility();
	// Places every dock panel for the first time (Toolbar floating top-left, Floor+Properties
	// pre-docked top-right) - guarded internally so a later NativeConstruct call (if any) is a no-op.
	InitializeDefaultDockLayout();
}

void UInteriorScreenWidget::NativeDestruct()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->OnSelectedDoorChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleSelectedDoorChanged);
		PC->OnSelectedRoomChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleSelectedRoomChanged);
		PC->OnSelectedConnectorChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleSelectedConnectorChanged);
		PC->OnEditModeChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleEditModeChanged);
		PC->OnProjectSaved.RemoveDynamic(this, &UInteriorScreenWidget::HandleProjectSaved);
		PC->OnActiveFloorChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleActiveFloorChanged);
	}

	Super::NativeDestruct();
}

void UInteriorScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (bNeedsPostConstructLayoutRefresh)
	{
		bNeedsPostConstructLayoutRefresh = false;
		RefreshDockLayout();
	}

	if (PropertiesFractionRecalcFramesRemaining > 0)
	{
		--PropertiesFractionRecalcFramesRemaining;
		RecomputeColumnFractionsFromNaturalSizes(DockPanelColumnIndex[(int32)EDockPanelId::Properties]);
		RefreshDockLayout();
	}
}

// ---------------------------------------------------------------------------
// Docking system
// ---------------------------------------------------------------------------

FVector2D UInteriorScreenWidget::GetCanvasSize() const
{
	if (RootCanvas)
	{
		const FVector2D LocalSize = RootCanvas->GetCachedGeometry().GetLocalSize();
		if (LocalSize.X > 0.f && LocalSize.Y > 0.f)
		{
			return LocalSize;
		}
	}
	return UWidgetLayoutLibrary::GetViewportSize(this);
}

FVector2D UInteriorScreenWidget::GetDefaultPanelPosition(EDockPanelId Id, const FVector2D& ViewportSize) const
{
	switch (Id)
	{
	case EDockPanelId::Toolbar:
		return FVector2D(10.f, SidebarTopMargin);
	case EDockPanelId::Floor:
	case EDockPanelId::Properties:
		// Same spot for both - see InitializeDefaultDockLayout's own comment.
		return FVector2D(FMath::Max(10.f, ViewportSize.X - DockPanelWidths[(int32)EDockPanelId::Floor] - 20.f), SidebarTopMargin);
	case EDockPanelId::Layers:
		// Left side, vertically centered - clear of Toolbar (top-left) and Floor/Properties
		// (top-right) in most window sizes without needing to know either's actual height.
		return FVector2D(10.f, ViewportSize.Y * 0.5f);
	default:
		return FVector2D(10.f, SidebarTopMargin);
	}
}

void UInteriorScreenWidget::InitializeDefaultDockLayout()
{
	if (bDockLayoutInitialized)
	{
		return;
	}
	bDockLayoutInitialized = true;

	// A previously-saved layout (see SaveDockLayoutToDisk) takes over entirely if one exists - it
	// already positions every panel and rebuilds DockColumns itself, so there's nothing left for the
	// default placement below to do.
	if (LoadDockLayoutFromDisk())
	{
		bNeedsPostConstructLayoutRefresh = true;
		return;
	}

	const FVector2D ViewportSize = GetCanvasSize();

	for (int32 Index = 0; Index < NumDockPanels; ++Index)
	{
		if (UCanvasPanelSlot* PanelSlot = DockPanelCanvasSlots[Index])
		{
			PanelSlot->SetPosition(GetDefaultPanelPosition((EDockPanelId)Index, ViewportSize));
		}
	}

	RefreshDockPanelContentWidths();
	RefreshDockLayout();

	// Properties is a permanent panel (always visible, just empty with nothing selected - see
	// RefreshPropertiesPanelVisibility) - dock it in under Floor by default right away instead of
	// leaving it floating at its own default spot.
	AutoDockPropertiesBelowFloor();

	// This all just ran before the widget's had even one real Slate layout/paint pass, which can
	// leave a docked panel's background border a frame stale (see NativeTick) - queue one more pass
	// for the first real tick, once everything has actually settled.
	bNeedsPostConstructLayoutRefresh = true;
}

FVector2D UInteriorScreenWidget::GetPanelPosition(EDockPanelId Id) const
{
	const UCanvasPanelSlot* PanelSlot = DockPanelCanvasSlots[(int32)Id];
	return PanelSlot ? PanelSlot->GetPosition() : FVector2D::ZeroVector;
}

FVector2D UInteriorScreenWidget::GetPanelSize(EDockPanelId Id) const
{
	const UBorder* Border = DockPanelBorders[(int32)Id];
	return Border ? Border->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
}

bool UInteriorScreenWidget::IsScreenPositionOverPanelButton(EDockPanelId Id, const FVector2D& ScreenPosition) const
{
	TArray<UWidget*> Widgets;
	switch (Id)
	{
	case EDockPanelId::Toolbar:
		Widgets = { BuildingSwitcherComboBox, BuildingNameTextBox, EditBuildingNameButton,
			SelectModeButton, DrawRoomModeButton, DrawWallModeButton, AddRectModeButton,
			AddTriangleModeButton, AddCircleModeButton, AddDoorModeButton, AddStairsModeButton,
			AddElevatorModeButton, AddEscalatorModeButton };
		break;
	case EDockPanelId::Floor:
		Widgets = { AddFloorButton, RemoveFloorButton, FloorListBox };
		break;
	case EDockPanelId::Properties:
		Widgets = { ConnectsToComboBox, ApplyDoorButton, RoomNameTextBox, ApplyRoomButton, ApplyConnectorButton };
		for (UCheckBox* CheckBox : FloorCheckboxes)
		{
			Widgets.Add(CheckBox);
		}
		break;
	case EDockPanelId::Layers:
		Widgets = { LayersSearchBox, LayersListBox };
		break;
	}

	for (const UWidget* Widget : Widgets)
	{
		if (Widget && Widget->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			return true;
		}
	}
	return false;
}

void UInteriorScreenWidget::SetPanelWidth(EDockPanelId Id, float NewWidth)
{
	const int32 Index = (int32)Id;
	DockPanelWidths[Index] = NewWidth;

	// A docked column always stays a uniform width, like a real docked group - dragging any one
	// member's handle resizes every sibling in the same column too.
	const int32 ColumnIdx = DockPanelColumnIndex[Index];
	if (DockColumns.IsValidIndex(ColumnIdx))
	{
		for (EDockPanelId Sibling : DockColumns[ColumnIdx].Panels)
		{
			DockPanelWidths[(int32)Sibling] = NewWidth;
		}
	}

	RefreshDockPanelContentWidths();
	RefreshDockLayout();
}

void UInteriorScreenWidget::RemovePanelFromColumn(EDockPanelId Id)
{
	const int32 Index = (int32)Id;
	const int32 ColumnIdx = DockPanelColumnIndex[Index];
	if (!DockColumns.IsValidIndex(ColumnIdx))
	{
		return;
	}

	FDockColumn& Column = DockColumns[ColumnIdx];
	const int32 PosInColumn = Column.Panels.IndexOfByKey(Id);
	if (PosInColumn == INDEX_NONE)
	{
		return;
	}

	const float FreedFraction = Column.HeightFractions[PosInColumn];
	Column.Panels.RemoveAt(PosInColumn);
	Column.HeightFractions.RemoveAt(PosInColumn);
	if (Column.HeightFractions.Num() > 0)
	{
		// Give the freed share to whichever panel was sharing a boundary with the one that left.
		const int32 GiveTo = FMath::Min(PosInColumn, Column.HeightFractions.Num() - 1);
		Column.HeightFractions[GiveTo] += FreedFraction;
	}

	DockPanelColumnIndex[Index] = INDEX_NONE;

	if (Column.Panels.Num() <= 1)
	{
		// A column of 1 isn't a column anymore - dissolve it, its last member goes back to floating.
		if (Column.Panels.Num() == 1)
		{
			DockPanelColumnIndex[(int32)Column.Panels[0]] = INDEX_NONE;
		}
		DockColumns.RemoveAt(ColumnIdx);
		for (int32& Stored : DockPanelColumnIndex)
		{
			if (Stored > ColumnIdx)
			{
				--Stored;
			}
		}
	}
}

void UInteriorScreenWidget::TrySnapPanel(EDockPanelId Id)
{
	const FVector2D DraggedPos = GetPanelPosition(Id);
	const FVector2D DraggedSize = GetPanelSize(Id);
	const float DraggedLeft = DraggedPos.X;
	const float DraggedRight = DraggedPos.X + DraggedSize.X;
	const float DraggedTop = DraggedPos.Y;
	const float DraggedBottom = DraggedPos.Y + DraggedSize.Y;

	for (int32 OtherIndex = 0; OtherIndex < NumDockPanels; ++OtherIndex)
	{
		const EDockPanelId OtherId = (EDockPanelId)OtherIndex;
		if (OtherId == Id)
		{
			continue;
		}

		const FVector2D OtherPos = GetPanelPosition(OtherId);
		const FVector2D OtherSize = GetPanelSize(OtherId);
		const float OtherLeft = OtherPos.X;
		const float OtherRight = OtherPos.X + OtherSize.X;
		const float OtherTop = OtherPos.Y;
		const float OtherBottom = OtherPos.Y + OtherSize.Y;

		const bool bXOverlaps = DraggedLeft < OtherRight && OtherLeft < DraggedRight;
		if (!bXOverlaps)
		{
			continue;
		}

		bool bSnapBelow = FMath::Abs(DraggedTop - OtherBottom) <= DockSnapThresholdPx;
		bool bSnapAbove = FMath::Abs(DraggedBottom - OtherTop) <= DockSnapThresholdPx;
		if (!bSnapBelow && !bSnapAbove)
		{
			// Not edge-adjacent, but the two panels' rects genuinely overlap (e.g. dropped right on
			// top of another panel rather than nudged against its edge) - a real editor never lets
			// two panels just sit on top of each other unglued reading as garbled overlapping text,
			// so this resolves the same way an edge-snap would: dock against whichever side needs to
			// move the least, based on their centers.
			const bool bYOverlaps = DraggedTop < OtherBottom && OtherTop < DraggedBottom;
			if (!bYOverlaps)
			{
				continue;
			}
			const float DraggedCenterY = (DraggedTop + DraggedBottom) * 0.5f;
			const float OtherCenterY = (OtherTop + OtherBottom) * 0.5f;
			if (DraggedCenterY >= OtherCenterY)
			{
				bSnapBelow = true;
			}
			else
			{
				bSnapAbove = true;
			}
		}

		// Join Other's column (creating one if it doesn't have one yet).
		int32 ColumnIdx = DockPanelColumnIndex[OtherIndex];
		if (!DockColumns.IsValidIndex(ColumnIdx))
		{
			FDockColumn NewColumn;
			NewColumn.Panels = { OtherId };
			NewColumn.HeightFractions = { 1.f };
			NewColumn.X = OtherPos.X;
			ColumnIdx = DockColumns.Add(NewColumn);
			DockPanelColumnIndex[OtherIndex] = ColumnIdx;
		}

		FDockColumn& Column = DockColumns[ColumnIdx];
		const int32 OtherPosInColumn = Column.Panels.IndexOfByKey(OtherId);
		const int32 InsertAt = bSnapBelow ? OtherPosInColumn + 1 : OtherPosInColumn;

		// The new panel's share comes out of Other's own share specifically (not spread evenly
		// across every column member), split proportionally to their current actual content heights
		// (DraggedSize/OtherSize, both reliable here - real geometry from painted, in-progress-or-
		// just-finished drag, not a stale first-frame guess) rather than always an equal 1/N split.
		// An even split left a lot of empty space below a short panel's content (e.g. Floor next to
		// a taller Properties), or squeezed a tall one (Toolbar's 10 mode buttons) into less height
		// than it actually needs, spilling its own content into the panel below it.
		const float NewPanelHeight = FMath::Max(DraggedSize.Y, DockPanelMinHeight);
		const float OtherPanelHeight = FMath::Max(OtherSize.Y, DockPanelMinHeight);
		const float OtherOldFraction = Column.HeightFractions[OtherPosInColumn];
		const float NewFraction = OtherOldFraction * NewPanelHeight / (NewPanelHeight + OtherPanelHeight);
		Column.HeightFractions[OtherPosInColumn] = OtherOldFraction - NewFraction;
		Column.Panels.Insert(Id, InsertAt);
		Column.HeightFractions.Insert(NewFraction, InsertAt);
		Column.X = OtherPos.X;

		DockPanelColumnIndex[(int32)Id] = ColumnIdx;
		SetPanelWidth(Id, DockPanelWidths[OtherIndex]); // also refreshes content widths + layout
		return;
	}

	// No snap - stays floating exactly where it was dropped.
	RefreshDockLayout();
}

void UInteriorScreenWidget::RefreshDockLayout()
{
	const FVector2D ViewportSize = GetCanvasSize();
	const float ColumnTotalHeight = FMath::Max(1.f, ViewportSize.Y - SidebarTopMargin - SidebarBottomMargin);

	for (FDockColumn& Column : DockColumns)
	{
		// A hidden panel (Properties, while nothing is selected) keeps its stored HeightFractions
		// entry so a later splitter drag still has something meaningful to resume from, but must not
		// actually consume column space while hidden.
		int32 VisibleCount = 0;
		float VisibleFractionSum = 0.f;
		for (int32 i = 0; i < Column.Panels.Num(); ++i)
		{
			const UBorder* Border = DockPanelBorders[(int32)Column.Panels[i]];
			if (Border && Border->GetVisibility() != ESlateVisibility::Collapsed)
			{
				++VisibleCount;
				VisibleFractionSum += Column.HeightFractions[i];
			}
		}
		if (VisibleFractionSum <= 0.f)
		{
			VisibleFractionSum = 1.f; // every member hidden (shouldn't normally happen) - fall back to raw fractions
		}

		// Splitting the column's height only makes sense once 2+ of its members are actually visible
		// at once and genuinely competing for the same space (that's what the splitter is for). With
		// just one currently shown - its docked sibling collapsed, e.g. Properties with nothing
		// selected - stretching that lone panel to the FULL column height left a huge empty gap below
		// its own content; it should just size to fit itself instead, exactly like a floating panel.
		const bool bSplitColumn = VisibleCount >= 2;

		float RunningY = SidebarTopMargin;
		for (int32 i = 0; i < Column.Panels.Num(); ++i)
		{
			const EDockPanelId PanelId = Column.Panels[i];
			const int32 Index = (int32)PanelId;
			UBorder* Border = DockPanelBorders[Index];
			const bool bPanelVisible = Border && Border->GetVisibility() != ESlateVisibility::Collapsed;

			float PanelHeight = 0.f;
			if (bPanelVisible)
			{
				PanelHeight = bSplitColumn
					? FMath::Max(DockPanelMinHeight, ColumnTotalHeight * Column.HeightFractions[i] / VisibleFractionSum)
					: FMath::Max(DockPanelMinHeight, GetPanelSize(PanelId).Y);
			}

			if (Border)
			{
				if (USizeBox* Wrapper = Cast<USizeBox>(Border->GetParent()))
				{
					Wrapper->SetWidthOverride(DockPanelWidths[Index]);
					if (bPanelVisible && bSplitColumn)
					{
						Wrapper->SetHeightOverride(PanelHeight);
					}
					else
					{
						// Not actually sharing the column with another visible panel right now - let
						// it size to its own natural content height instead of a forced value.
						Wrapper->ClearHeightOverride();
					}
				}
			}
			if (PanelId == EDockPanelId::Layers)
			{
				// See LayersListDefaultHeight's own comment - the list's own fixed-height wrapper
				// needs to shrink to fit whenever this panel's OUTER height was just forced smaller
				// than that fixed default, or its content paints straight through the panel's edge.
				ResizeLayersListToFit(bPanelVisible && bSplitColumn ? PanelHeight : -1.f);
			}
			if (UCanvasPanelSlot* PanelSlot = DockPanelCanvasSlots[Index])
			{
				PanelSlot->SetPosition(FVector2D(Column.X, RunningY));
			}

			// A hidden panel's own width handle has nothing visible to resize and would otherwise
			// show up as a stray draggable bar floating wherever its now-zero-height slice landed.
			if (UBorder* Handle = DockResizeHandles[Index])
			{
				Handle->SetVisibility(bPanelVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			}
			if (UCanvasPanelSlot* HandleSlot = DockResizeHandleSlots[Index])
			{
				HandleSlot->SetAutoSize(false);
				HandleSlot->SetSize(FVector2D(SidebarResizeHandleWidth, PanelHeight));
				HandleSlot->SetPosition(FVector2D(Column.X + DockPanelWidths[Index], RunningY));
			}
			if (UBorder* LeftHandle = DockLeftResizeHandles[Index])
			{
				LeftHandle->SetVisibility(bPanelVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			}
			if (UCanvasPanelSlot* LeftHandleSlot = DockLeftResizeHandleSlots[Index])
			{
				LeftHandleSlot->SetAutoSize(false);
				LeftHandleSlot->SetSize(FVector2D(SidebarResizeHandleWidth, PanelHeight));
				LeftHandleSlot->SetPosition(FVector2D(Column.X - SidebarResizeHandleWidth, RunningY));
			}

			RunningY += PanelHeight;
		}
	}

	// Floating panels just keep their own auto height - only width and the handle need (re)applying.
	for (int32 Index = 0; Index < NumDockPanels; ++Index)
	{
		if (DockPanelColumnIndex[Index] != INDEX_NONE)
		{
			continue;
		}

		UBorder* Border = DockPanelBorders[Index];
		if (Border)
		{
			if (USizeBox* Wrapper = Cast<USizeBox>(Border->GetParent()))
			{
				Wrapper->SetWidthOverride(DockPanelWidths[Index]);
				Wrapper->ClearHeightOverride();
			}
		}
		if ((EDockPanelId)Index == EDockPanelId::Layers)
		{
			ResizeLayersListToFit(-1.f); // floating - not forced, back to its own fixed default cap
		}

		// Same reasoning as the docked branch above - Properties starts out floating AND hidden
		// (nothing selected yet), and a hidden panel's Collapsed geometry reads as 0 height, which
		// clamped to DockPanelMinHeight and rendered as a small floor-height gray bar sitting right
		// on top of wherever Properties' initial position happened to coincide with another panel's.
		const bool bPanelVisible = Border && Border->GetVisibility() != ESlateVisibility::Collapsed;
		if (UBorder* Handle = DockResizeHandles[Index])
		{
			Handle->SetVisibility(bPanelVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
		const FVector2D PanelPos = GetPanelPosition((EDockPanelId)Index);
		const FVector2D PanelSize = GetPanelSize((EDockPanelId)Index);
		const float HandleHeight = FMath::Max(PanelSize.Y, DockPanelMinHeight);
		if (UCanvasPanelSlot* HandleSlot = DockResizeHandleSlots[Index])
		{
			HandleSlot->SetAutoSize(false);
			HandleSlot->SetSize(FVector2D(SidebarResizeHandleWidth, HandleHeight));
			HandleSlot->SetPosition(FVector2D(PanelPos.X + DockPanelWidths[Index], PanelPos.Y));
		}
		if (UBorder* LeftHandle = DockLeftResizeHandles[Index])
		{
			LeftHandle->SetVisibility(bPanelVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
		if (UCanvasPanelSlot* LeftHandleSlot = DockLeftResizeHandleSlots[Index])
		{
			LeftHandleSlot->SetAutoSize(false);
			LeftHandleSlot->SetSize(FVector2D(SidebarResizeHandleWidth, HandleHeight));
			LeftHandleSlot->SetPosition(FVector2D(PanelPos.X - SidebarResizeHandleWidth, PanelPos.Y));
		}
	}

	RefreshSplitterLayout();
}

void UInteriorScreenWidget::RefreshSplitterLayout()
{
	int32 SplitterSlotsUsed = 0;
	for (int32 ColumnIdx = 0; ColumnIdx < DockColumns.Num() && SplitterSlotsUsed < MaxDockSplitters; ++ColumnIdx)
	{
		const FDockColumn& Column = DockColumns[ColumnIdx];
		for (int32 i = 0; i + 1 < Column.Panels.Num() && SplitterSlotsUsed < MaxDockSplitters; ++i)
		{
			const EDockPanelId UpperId = Column.Panels[i];
			const EDockPanelId LowerId = Column.Panels[i + 1];

			// Properties is only shown while something is selected (see
			// RefreshPropertiesPanelVisibility) - RefreshDockLayout gives a hidden panel 0 height, but
			// a splitter drawn at the boundary with one has nothing real to divide either way, and
			// would just sit collapsed onto its visible neighbour's edge. Skip it until both sides are
			// actually showing.
			const UBorder* UpperBorder = DockPanelBorders[(int32)UpperId];
			const UBorder* LowerBorder = DockPanelBorders[(int32)LowerId];
			if (!UpperBorder || !LowerBorder
				|| UpperBorder->GetVisibility() == ESlateVisibility::Collapsed
				|| LowerBorder->GetVisibility() == ESlateVisibility::Collapsed)
			{
				continue;
			}

			const int32 SplitterIdx = SplitterSlotsUsed;
			++SplitterSlotsUsed;
			SplitterColumnIndex[SplitterIdx] = ColumnIdx;
			SplitterUpperIndexInColumn[SplitterIdx] = i;
			const FVector2D UpperPos = GetPanelPosition(UpperId);
			const FVector2D UpperSize = GetPanelSize(UpperId);

			if (UBorder* Handle = SplitterHandles[SplitterIdx])
			{
				Handle->SetVisibility(ESlateVisibility::Visible);
			}
			if (UCanvasPanelSlot* SplitterSlot = SplitterCanvasSlots[SplitterIdx])
			{
				SplitterSlot->SetAutoSize(false);
				SplitterSlot->SetPosition(FVector2D(Column.X, UpperPos.Y + UpperSize.Y - SplitterHeight * 0.5f));
				SplitterSlot->SetSize(FVector2D(DockPanelWidths[(int32)UpperId], SplitterHeight));
			}
		}
	}

	for (int32 SplitterIdx = SplitterSlotsUsed; SplitterIdx < MaxDockSplitters; ++SplitterIdx)
	{
		SplitterColumnIndex[SplitterIdx] = INDEX_NONE;
		if (UBorder* Handle = SplitterHandles[SplitterIdx])
		{
			Handle->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UInteriorScreenWidget::RecomputeColumnFractionsFromNaturalSizes(int32 ColumnIndex)
{
	if (!DockColumns.IsValidIndex(ColumnIndex))
	{
		return;
	}

	FDockColumn& Column = DockColumns[ColumnIndex];

	TArray<float> NaturalHeights;
	NaturalHeights.Reserve(Column.Panels.Num());
	float TotalHeight = 0.f;
	for (EDockPanelId PanelId : Column.Panels)
	{
		const UBorder* Border = DockPanelBorders[(int32)PanelId];
		const bool bPanelVisible = Border && Border->GetVisibility() != ESlateVisibility::Collapsed;
		// A currently-hidden member's own content size means nothing right now - just keep it at the
		// floor so it doesn't distort the visible members' split, and so it isn't literally zero
		// (dividing TotalHeight by zero members would be worse) if it's the only one in the column.
		const float Height = bPanelVisible ? FMath::Max(GetPanelSize(PanelId).Y, DockPanelMinHeight) : DockPanelMinHeight;
		NaturalHeights.Add(Height);
		TotalHeight += Height;
	}

	if (TotalHeight <= 0.f)
	{
		return;
	}
	for (int32 i = 0; i < Column.Panels.Num(); ++i)
	{
		Column.HeightFractions[i] = NaturalHeights[i] / TotalHeight;
	}
}

void UInteriorScreenWidget::AutoDockPropertiesBelowFloor()
{
	if (DockPanelColumnIndex[(int32)EDockPanelId::Properties] != INDEX_NONE)
	{
		return; // already docked somewhere (the user got there first) - leave it alone
	}

	const FVector2D FloorPos = GetPanelPosition(EDockPanelId::Floor);
	const FVector2D FloorSize = GetPanelSize(EDockPanelId::Floor);
	if (UCanvasPanelSlot* PropertiesSlot = DockPanelCanvasSlots[(int32)EDockPanelId::Properties])
	{
		// Directly under Floor's current bottom edge, well within TrySnapPanel's own snap threshold -
		// this reuses its exact join/proportional-split logic instead of duplicating it here.
		PropertiesSlot->SetPosition(FVector2D(FloorPos.X, FloorPos.Y + FloorSize.Y));
	}
	TrySnapPanel(EDockPanelId::Properties);
}

const TCHAR* UInteriorScreenWidget::GetDockPanelName(EDockPanelId Id)
{
	switch (Id)
	{
	case EDockPanelId::Toolbar:    return TEXT("Toolbar");
	case EDockPanelId::Floor:      return TEXT("Floor");
	case EDockPanelId::Properties: return TEXT("Properties");
	case EDockPanelId::Layers:     return TEXT("Layers");
	default:                       return TEXT("");
	}
}

bool UInteriorScreenWidget::FindDockPanelIdByName(const FString& Name, EDockPanelId& OutId)
{
	for (int32 Index = 0; Index < NumDockPanels; ++Index)
	{
		if (Name == GetDockPanelName((EDockPanelId)Index))
		{
			OutId = (EDockPanelId)Index;
			return true;
		}
	}
	return false;
}

TSharedPtr<FJsonObject> UInteriorScreenWidget::BuildLayoutSaveData() const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> PanelsObject = MakeShared<FJsonObject>();
	for (int32 Index = 0; Index < NumDockPanels; ++Index)
	{
		const FVector2D Position = GetPanelPosition((EDockPanelId)Index);
		TSharedPtr<FJsonObject> PanelObject = MakeShared<FJsonObject>();
		PanelObject->SetNumberField(TEXT("x"), Position.X);
		PanelObject->SetNumberField(TEXT("y"), Position.Y);
		PanelObject->SetNumberField(TEXT("width"), DockPanelWidths[Index]);
		PanelsObject->SetObjectField(GetDockPanelName((EDockPanelId)Index), PanelObject);
	}
	Root->SetObjectField(TEXT("panels"), PanelsObject);

	TArray<TSharedPtr<FJsonValue>> ColumnsArray;
	for (const FDockColumn& Column : DockColumns)
	{
		TArray<TSharedPtr<FJsonValue>> PanelNameArray;
		TArray<TSharedPtr<FJsonValue>> FractionArray;
		for (int32 i = 0; i < Column.Panels.Num(); ++i)
		{
			PanelNameArray.Add(MakeShared<FJsonValueString>(GetDockPanelName(Column.Panels[i])));
			FractionArray.Add(MakeShared<FJsonValueNumber>(Column.HeightFractions[i]));
		}

		TSharedPtr<FJsonObject> ColumnObject = MakeShared<FJsonObject>();
		ColumnObject->SetArrayField(TEXT("panels"), PanelNameArray);
		ColumnObject->SetArrayField(TEXT("fractions"), FractionArray);
		ColumnObject->SetNumberField(TEXT("x"), Column.X);
		ColumnsArray.Add(MakeShared<FJsonValueObject>(ColumnObject));
	}
	Root->SetArrayField(TEXT("columns"), ColumnsArray);

	return Root;
}

void UInteriorScreenWidget::ApplyLayoutSaveData(const TSharedPtr<FJsonObject>& Root)
{
	if (!Root.IsValid())
	{
		return;
	}

	const FVector2D ViewportSize = GetCanvasSize();
	const TSharedPtr<FJsonObject>* PanelsObject = nullptr;
	const bool bHasPanelsObject = Root->TryGetObjectField(TEXT("panels"), PanelsObject);
	for (int32 Index = 0; Index < NumDockPanels; ++Index)
	{
		const EDockPanelId Id = (EDockPanelId)Index;
		const TSharedPtr<FJsonObject>* PanelObject = nullptr;
		if (!bHasPanelsObject || !(*PanelsObject)->TryGetObjectField(GetDockPanelName(Id), PanelObject))
		{
			// A layout file saved before Id existed as a panel (e.g. an existing save from before
			// the Layers panel was added) has no entry for it - fall back to its normal default
			// spot instead of leaving it at its CanvasSlot's own uninitialized position.
			if (UCanvasPanelSlot* PanelSlot = DockPanelCanvasSlots[Index])
			{
				PanelSlot->SetPosition(GetDefaultPanelPosition(Id, ViewportSize));
			}
			continue;
		}

		double X = 0.0, Y = 0.0, Width = SidebarMinWidth;
		(*PanelObject)->TryGetNumberField(TEXT("x"), X);
		(*PanelObject)->TryGetNumberField(TEXT("y"), Y);
		(*PanelObject)->TryGetNumberField(TEXT("width"), Width);

		if (UCanvasPanelSlot* PanelSlot = DockPanelCanvasSlots[Index])
		{
			PanelSlot->SetPosition(FVector2D((float)X, (float)Y));
		}
		DockPanelWidths[Index] = FMath::Clamp((float)Width, SidebarMinWidth, SidebarMaxWidth);
	}

	DockColumns.Reset();
	for (int32 Index = 0; Index < NumDockPanels; ++Index)
	{
		DockPanelColumnIndex[Index] = INDEX_NONE;
	}

	const TArray<TSharedPtr<FJsonValue>>* ColumnsArray = nullptr;
	if (Root->TryGetArrayField(TEXT("columns"), ColumnsArray))
	{
		for (const TSharedPtr<FJsonValue>& ColumnValue : *ColumnsArray)
		{
			// FJsonValue::TryGetObject has both a const and non-const overload; called through
			// operator-> on a TSharedPtr (which doesn't propagate const to its pointee), this binds
			// to the non-const one, which needs a non-const TSharedPtr<FJsonObject>* to write into.
			TSharedPtr<FJsonObject>* ColumnObject = nullptr;
			if (!ColumnValue.IsValid() || !ColumnValue->TryGetObject(ColumnObject))
			{
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* PanelNameArray = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* FractionArray = nullptr;
			if (!(*ColumnObject)->TryGetArrayField(TEXT("panels"), PanelNameArray)
				|| !(*ColumnObject)->TryGetArrayField(TEXT("fractions"), FractionArray))
			{
				continue;
			}

			FDockColumn NewColumn;
			for (int32 i = 0; i < PanelNameArray->Num(); ++i)
			{
				EDockPanelId PanelId = EDockPanelId::Toolbar;
				if (!FindDockPanelIdByName((*PanelNameArray)[i]->AsString(), PanelId))
				{
					continue;
				}
				NewColumn.Panels.Add(PanelId);
				const double Fraction = FractionArray->IsValidIndex(i) ? (*FractionArray)[i]->AsNumber() : (1.0 / PanelNameArray->Num());
				NewColumn.HeightFractions.Add((float)Fraction);
			}
			if (NewColumn.Panels.Num() == 0)
			{
				continue;
			}

			double ColumnX = 0.0;
			(*ColumnObject)->TryGetNumberField(TEXT("x"), ColumnX);
			NewColumn.X = (float)ColumnX;

			const int32 NewColumnIndex = DockColumns.Add(NewColumn);
			for (EDockPanelId PanelId : NewColumn.Panels)
			{
				DockPanelColumnIndex[(int32)PanelId] = NewColumnIndex;
			}
		}
	}

	// This is a deliberately-restored layout, not the freshly-constructed default - InitializeDefaultDockLayout
	// only calls AutoDockPropertiesBelowFloor on the path that DOESN'T load a saved layout, so
	// Properties is left wherever this restore put it (docked or floating) either way.

	RefreshDockPanelContentWidths();
	RefreshDockLayout();
}

void UInteriorScreenWidget::SaveDockLayoutToDisk() const
{
	const TSharedPtr<FJsonObject> Root = BuildLayoutSaveData();

	FString OutputString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	FFileHelper::SaveStringToFile(OutputString, *(FPaths::ProjectSavedDir() / TEXT("InteriorEditorLayout.json")));
}

bool UInteriorScreenWidget::LoadDockLayoutFromDisk()
{
	FString FileContents;
	if (!FFileHelper::LoadFileToString(FileContents, *(FPaths::ProjectSavedDir() / TEXT("InteriorEditorLayout.json"))))
	{
		return false; // nothing saved yet
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContents);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	ApplyLayoutSaveData(Root);
	return true;
}

FEventReply UInteriorScreenWidget::HandlePanelMouseButtonDown(EDockPanelId Id, const FPointerEvent& MouseEvent)
{
	const int32 Index = (int32)Id;
	UBorder* Border = DockPanelBorders[Index];
	UCanvasPanelSlot* PanelSlot = DockPanelCanvasSlots[Index];
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !Border || !PanelSlot)
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	const FVector2D ScreenPosition = MouseEvent.GetScreenSpacePosition();
	if (!Border->GetCachedGeometry().IsUnderLocation(ScreenPosition) || IsScreenPositionOverPanelButton(Id, ScreenPosition))
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	USizeBox* Wrapper = Cast<USizeBox>(Border->GetParent());
	UPanelWidget* CanvasParent = Wrapper ? Wrapper->GetParent() : nullptr;
	if (!CanvasParent)
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	bDockPanelIsDraggingMove[Index] = true;
	DockPanelMoveDragStartMouse[Index] = CanvasParent->GetCachedGeometry().AbsoluteToLocal(ScreenPosition);
	DockPanelMoveDragStartPosition[Index] = PanelSlot->GetPosition();

	// The drag itself always behaves like a floating panel - TrySnapPanel (on drag end) re-evaluates
	// whether it should re-dock.
	RemovePanelFromColumn(Id);
	RefreshDockLayout();

	// Undocking just now changed this panel's own height (docked -> auto), but its cached geometry
	// won't reflect that until the next paint - sizing the handle off it right now would use the
	// stale (still-docked) height for one frame, showing as a gray sliver poking out past the
	// panel's real new edge. Simplest fix: just hide this panel's own handle for the drag's
	// duration - HandlePanelMouseButtonUp restores it once the drop has settled.
	if (UBorder* Handle = DockResizeHandles[Index])
	{
		Handle->SetVisibility(ESlateVisibility::Collapsed);
	}

	FEventReply Reply = UWidgetBlueprintLibrary::Handled();
	return UWidgetBlueprintLibrary::CaptureMouse(Reply, Border);
}

FEventReply UInteriorScreenWidget::HandlePanelMouseMove(EDockPanelId Id, const FPointerEvent& MouseEvent)
{
	const int32 Index = (int32)Id;
	if (!bDockPanelIsDraggingMove[Index])
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	UBorder* Border = DockPanelBorders[Index];
	UCanvasPanelSlot* PanelSlot = DockPanelCanvasSlots[Index];
	USizeBox* Wrapper = Border ? Cast<USizeBox>(Border->GetParent()) : nullptr;
	UPanelWidget* CanvasParent = Wrapper ? Wrapper->GetParent() : nullptr;
	if (!PanelSlot || !CanvasParent)
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	const FGeometry& ParentGeometry = CanvasParent->GetCachedGeometry();
	const FVector2D CurrentMousePosition = ParentGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2D NewPosition = DockPanelMoveDragStartPosition[Index] + (CurrentMousePosition - DockPanelMoveDragStartMouse[Index]);

	// ParentGeometry (the canvas' own geometry, already fetched above) rather than
	// UWidgetLayoutLibrary::GetViewportSize - that returns the platform's raw pixel resolution,
	// which only matches this Slate-local coordinate space at DPI scale 1; using it here clamped
	// dragging into a box visibly smaller than the real screen whenever the two didn't match.
	const FVector2D ViewportSize = ParentGeometry.GetLocalSize();
	const FVector2D CurrentPanelSize = GetPanelSize(Id);
	const float ClampedX = FMath::Clamp(NewPosition.X, 0.f, FMath::Max(0.f, ViewportSize.X - CurrentPanelSize.X));
	const float ClampedY = FMath::Clamp(NewPosition.Y, 0.f, FMath::Max(0.f, ViewportSize.Y - CurrentPanelSize.Y));
	PanelSlot->SetPosition(FVector2D(ClampedX, ClampedY));

	if (UCanvasPanelSlot* HandleSlot = DockResizeHandleSlots[Index])
	{
		HandleSlot->SetPosition(FVector2D(ClampedX + DockPanelWidths[Index], ClampedY));
	}

	return UWidgetBlueprintLibrary::Handled();
}

FEventReply UInteriorScreenWidget::HandlePanelMouseButtonUp(EDockPanelId Id, const FPointerEvent& MouseEvent)
{
	const int32 Index = (int32)Id;
	if (bDockPanelIsDraggingMove[Index] && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDockPanelIsDraggingMove[Index] = false;
		TrySnapPanel(Id);
		// Restore the handle HandlePanelMouseButtonDown hid for the drag - TrySnapPanel already
		// refreshed layout above, so by the next paint it'll be sized/positioned for wherever the
		// panel actually ended up (docked or still floating).
		if (UBorder* Handle = DockResizeHandles[Index])
		{
			Handle->SetVisibility(ESlateVisibility::Visible);
		}
		FEventReply Reply = UWidgetBlueprintLibrary::Handled();
		return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
	}

	return UWidgetBlueprintLibrary::Unhandled();
}

FEventReply UInteriorScreenWidget::HandleResizeMouseButtonDown(EDockPanelId Id, const FPointerEvent& MouseEvent)
{
	const int32 Index = (int32)Id;
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !DockResizeHandles[Index])
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	bDockPanelIsResizingWidth[Index] = true;
	DockPanelResizeStartMouseX[Index] = MouseEvent.GetScreenSpacePosition().X;
	DockPanelResizeStartWidth[Index] = DockPanelWidths[Index];
	FEventReply Reply = UWidgetBlueprintLibrary::Handled();
	return UWidgetBlueprintLibrary::CaptureMouse(Reply, DockResizeHandles[Index]);
}

FEventReply UInteriorScreenWidget::HandleResizeMouseMove(EDockPanelId Id, const FPointerEvent& MouseEvent)
{
	const int32 Index = (int32)Id;
	if (!bDockPanelIsResizingWidth[Index])
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		bDockPanelIsResizingWidth[Index] = false;
		FEventReply Reply = UWidgetBlueprintLibrary::Handled();
		return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
	}

	const float DeltaX = MouseEvent.GetScreenSpacePosition().X - DockPanelResizeStartMouseX[Index];
	const float NewWidth = FMath::Clamp(DockPanelResizeStartWidth[Index] + DeltaX, SidebarMinWidth, SidebarMaxWidth);
	SetPanelWidth(Id, NewWidth);

	return UWidgetBlueprintLibrary::Handled();
}

FEventReply UInteriorScreenWidget::HandleResizeMouseButtonUp(EDockPanelId Id, const FPointerEvent& MouseEvent)
{
	const int32 Index = (int32)Id;
	if (bDockPanelIsResizingWidth[Index] && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDockPanelIsResizingWidth[Index] = false;
		FEventReply Reply = UWidgetBlueprintLibrary::Handled();
		return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
	}

	return UWidgetBlueprintLibrary::Unhandled();
}

FEventReply UInteriorScreenWidget::HandleLeftResizeMouseButtonDown(EDockPanelId Id, const FPointerEvent& MouseEvent)
{
	const int32 Index = (int32)Id;
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !DockLeftResizeHandles[Index])
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	bDockPanelIsResizingLeftWidth[Index] = true;
	DockPanelLeftResizeStartMouseX[Index] = MouseEvent.GetScreenSpacePosition().X;
	DockPanelLeftResizeStartWidth[Index] = DockPanelWidths[Index];
	DockPanelLeftResizeStartPanelX[Index] = GetPanelPosition(Id).X;
	FEventReply Reply = UWidgetBlueprintLibrary::Handled();
	return UWidgetBlueprintLibrary::CaptureMouse(Reply, DockLeftResizeHandles[Index]);
}

FEventReply UInteriorScreenWidget::HandleLeftResizeMouseMove(EDockPanelId Id, const FPointerEvent& MouseEvent)
{
	const int32 Index = (int32)Id;
	if (!bDockPanelIsResizingLeftWidth[Index])
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		bDockPanelIsResizingLeftWidth[Index] = false;
		FEventReply Reply = UWidgetBlueprintLibrary::Handled();
		return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
	}

	// Dragging the LEFT edge to the left (negative DeltaX) GROWS the panel - opposite sign from the
	// right handle, since width is now measured from this edge instead.
	const float DeltaX = MouseEvent.GetScreenSpacePosition().X - DockPanelLeftResizeStartMouseX[Index];
	const float NewWidth = FMath::Clamp(DockPanelLeftResizeStartWidth[Index] - DeltaX, SidebarMinWidth, SidebarMaxWidth);
	const float ActualWidthDelta = NewWidth - DockPanelLeftResizeStartWidth[Index];
	const float NewX = DockPanelLeftResizeStartPanelX[Index] - ActualWidthDelta; // keeps the RIGHT edge fixed in place

	const int32 ColumnIdx = DockPanelColumnIndex[Index];
	if (DockColumns.IsValidIndex(ColumnIdx))
	{
		// Column members all share one X - shift the whole column, not just this panel, or it'd
		// drift out of alignment with its docked sibling(s).
		DockColumns[ColumnIdx].X = NewX;
	}
	else if (UCanvasPanelSlot* PanelSlot = DockPanelCanvasSlots[Index])
	{
		PanelSlot->SetPosition(FVector2D(NewX, PanelSlot->GetPosition().Y));
	}

	SetPanelWidth(Id, NewWidth); // syncs width across column siblings, refreshes content widths + layout

	return UWidgetBlueprintLibrary::Handled();
}

FEventReply UInteriorScreenWidget::HandleLeftResizeMouseButtonUp(EDockPanelId Id, const FPointerEvent& MouseEvent)
{
	const int32 Index = (int32)Id;
	if (bDockPanelIsResizingLeftWidth[Index] && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDockPanelIsResizingLeftWidth[Index] = false;
		FEventReply Reply = UWidgetBlueprintLibrary::Handled();
		return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
	}

	return UWidgetBlueprintLibrary::Unhandled();
}

FEventReply UInteriorScreenWidget::HandleSplitterMouseButtonDown(int32 SplitterIndex, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !SplitterHandles.IsValidIndex(SplitterIndex)
		|| !SplitterHandles[SplitterIndex] || SplitterColumnIndex[SplitterIndex] == INDEX_NONE)
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	bSplitterIsDragging[SplitterIndex] = true;
	SplitterDragStartMouseY[SplitterIndex] = MouseEvent.GetScreenSpacePosition().Y;
	SplitterDragStartFractions[SplitterIndex] = DockColumns[SplitterColumnIndex[SplitterIndex]].HeightFractions;
	FEventReply Reply = UWidgetBlueprintLibrary::Handled();
	return UWidgetBlueprintLibrary::CaptureMouse(Reply, SplitterHandles[SplitterIndex]);
}

FEventReply UInteriorScreenWidget::HandleSplitterMouseMove(int32 SplitterIndex, const FPointerEvent& MouseEvent)
{
	if (!bSplitterIsDragging.IsValidIndex(SplitterIndex) || !bSplitterIsDragging[SplitterIndex])
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		bSplitterIsDragging[SplitterIndex] = false;
		FEventReply Reply = UWidgetBlueprintLibrary::Handled();
		return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
	}

	const int32 ColumnIdx = SplitterColumnIndex[SplitterIndex];
	if (!DockColumns.IsValidIndex(ColumnIdx))
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	FDockColumn& Column = DockColumns[ColumnIdx];
	const int32 Upper = SplitterUpperIndexInColumn[SplitterIndex];
	const int32 Lower = Upper + 1;
	if (!Column.HeightFractions.IsValidIndex(Upper) || !Column.HeightFractions.IsValidIndex(Lower)
		|| !SplitterDragStartFractions[SplitterIndex].IsValidIndex(Upper) || !SplitterDragStartFractions[SplitterIndex].IsValidIndex(Lower))
	{
		return UWidgetBlueprintLibrary::Unhandled();
	}

	const FVector2D ViewportSize = GetCanvasSize();
	const float ColumnTotalHeight = FMath::Max(1.f, ViewportSize.Y - SidebarTopMargin - SidebarBottomMargin);
	const float DeltaY = MouseEvent.GetScreenSpacePosition().Y - SplitterDragStartMouseY[SplitterIndex];
	const float DeltaFraction = DeltaY / ColumnTotalHeight;

	const float MinFraction = DockPanelMinHeight / ColumnTotalHeight;
	const float StartUpper = SplitterDragStartFractions[SplitterIndex][Upper];
	const float StartLower = SplitterDragStartFractions[SplitterIndex][Lower];
	const float PairTotal = StartUpper + StartLower;

	const float NewUpper = FMath::Clamp(StartUpper + DeltaFraction, MinFraction, FMath::Max(MinFraction, PairTotal - MinFraction));
	Column.HeightFractions[Upper] = NewUpper;
	Column.HeightFractions[Lower] = PairTotal - NewUpper;

	RefreshDockLayout();
	return UWidgetBlueprintLibrary::Handled();
}

FEventReply UInteriorScreenWidget::HandleSplitterMouseButtonUp(int32 SplitterIndex, const FPointerEvent& MouseEvent)
{
	if (bSplitterIsDragging.IsValidIndex(SplitterIndex) && bSplitterIsDragging[SplitterIndex] && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bSplitterIsDragging[SplitterIndex] = false;
		FEventReply Reply = UWidgetBlueprintLibrary::Handled();
		return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
	}

	return UWidgetBlueprintLibrary::Unhandled();
}

FEventReply UInteriorScreenWidget::OnToolbarPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseButtonDown(EDockPanelId::Toolbar, MouseEvent); }
FEventReply UInteriorScreenWidget::OnToolbarPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseMove(EDockPanelId::Toolbar, MouseEvent); }
FEventReply UInteriorScreenWidget::OnToolbarPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseButtonUp(EDockPanelId::Toolbar, MouseEvent); }
FEventReply UInteriorScreenWidget::OnFloorPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseButtonDown(EDockPanelId::Floor, MouseEvent); }
FEventReply UInteriorScreenWidget::OnFloorPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseMove(EDockPanelId::Floor, MouseEvent); }
FEventReply UInteriorScreenWidget::OnFloorPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseButtonUp(EDockPanelId::Floor, MouseEvent); }
FEventReply UInteriorScreenWidget::OnPropertiesPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseButtonDown(EDockPanelId::Properties, MouseEvent); }
FEventReply UInteriorScreenWidget::OnPropertiesPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseMove(EDockPanelId::Properties, MouseEvent); }
FEventReply UInteriorScreenWidget::OnPropertiesPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseButtonUp(EDockPanelId::Properties, MouseEvent); }
FEventReply UInteriorScreenWidget::OnLayersPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseButtonDown(EDockPanelId::Layers, MouseEvent); }
FEventReply UInteriorScreenWidget::OnLayersPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseMove(EDockPanelId::Layers, MouseEvent); }
FEventReply UInteriorScreenWidget::OnLayersPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseButtonUp(EDockPanelId::Layers, MouseEvent); }

FEventReply UInteriorScreenWidget::OnToolbarResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseButtonDown(EDockPanelId::Toolbar, MouseEvent); }
FEventReply UInteriorScreenWidget::OnToolbarResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseMove(EDockPanelId::Toolbar, MouseEvent); }
FEventReply UInteriorScreenWidget::OnToolbarResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseButtonUp(EDockPanelId::Toolbar, MouseEvent); }
FEventReply UInteriorScreenWidget::OnFloorResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseButtonDown(EDockPanelId::Floor, MouseEvent); }
FEventReply UInteriorScreenWidget::OnFloorResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseMove(EDockPanelId::Floor, MouseEvent); }
FEventReply UInteriorScreenWidget::OnFloorResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseButtonUp(EDockPanelId::Floor, MouseEvent); }
FEventReply UInteriorScreenWidget::OnPropertiesResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseButtonDown(EDockPanelId::Properties, MouseEvent); }
FEventReply UInteriorScreenWidget::OnPropertiesResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseMove(EDockPanelId::Properties, MouseEvent); }
FEventReply UInteriorScreenWidget::OnPropertiesResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseButtonUp(EDockPanelId::Properties, MouseEvent); }
FEventReply UInteriorScreenWidget::OnLayersResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseButtonDown(EDockPanelId::Layers, MouseEvent); }
FEventReply UInteriorScreenWidget::OnLayersResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseMove(EDockPanelId::Layers, MouseEvent); }
FEventReply UInteriorScreenWidget::OnLayersResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseButtonUp(EDockPanelId::Layers, MouseEvent); }

FEventReply UInteriorScreenWidget::OnToolbarLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseButtonDown(EDockPanelId::Toolbar, MouseEvent); }
FEventReply UInteriorScreenWidget::OnToolbarLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseMove(EDockPanelId::Toolbar, MouseEvent); }
FEventReply UInteriorScreenWidget::OnToolbarLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseButtonUp(EDockPanelId::Toolbar, MouseEvent); }
FEventReply UInteriorScreenWidget::OnFloorLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseButtonDown(EDockPanelId::Floor, MouseEvent); }
FEventReply UInteriorScreenWidget::OnFloorLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseMove(EDockPanelId::Floor, MouseEvent); }
FEventReply UInteriorScreenWidget::OnFloorLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseButtonUp(EDockPanelId::Floor, MouseEvent); }
FEventReply UInteriorScreenWidget::OnPropertiesLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseButtonDown(EDockPanelId::Properties, MouseEvent); }
FEventReply UInteriorScreenWidget::OnPropertiesLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseMove(EDockPanelId::Properties, MouseEvent); }
FEventReply UInteriorScreenWidget::OnPropertiesLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseButtonUp(EDockPanelId::Properties, MouseEvent); }
FEventReply UInteriorScreenWidget::OnLayersLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseButtonDown(EDockPanelId::Layers, MouseEvent); }
FEventReply UInteriorScreenWidget::OnLayersLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseMove(EDockPanelId::Layers, MouseEvent); }
FEventReply UInteriorScreenWidget::OnLayersLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseButtonUp(EDockPanelId::Layers, MouseEvent); }

FEventReply UInteriorScreenWidget::OnSplitter0MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonDown(0, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter0MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseMove(0, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter0MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonUp(0, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter1MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonDown(1, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter1MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseMove(1, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter1MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonUp(1, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter2MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonDown(2, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter2MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseMove(2, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter2MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonUp(2, MouseEvent); }

void UInteriorScreenWidget::HandleEditModeChanged()
{
	RefreshModeButtonHighlights();
}

void UInteriorScreenWidget::HandleProjectSaved()
{
	SaveDockLayoutToDisk();
}

void UInteriorScreenWidget::HandleActiveFloorChanged()
{
	RefreshFloorList();
	RefreshLayersList();
}

void UInteriorScreenWidget::RefreshDockPanelContentWidths()
{
	// Toolbar: building switcher combo box tracks the panel (minus its own 10px-a-side padding and
	// the edit-name button beside it). Mode-toolbar icons deliberately stay fixed 48x48 - stretching
	// a square icon into a rectangle would just look broken.
	if (BuildingSwitcherComboBox)
	{
		if (USizeBox* ComboSizeBox = Cast<USizeBox>(BuildingSwitcherComboBox->GetParent()))
		{
			ComboSizeBox->SetWidthOverride(FMath::Max(80.f, DockPanelWidths[(int32)EDockPanelId::Toolbar] - 85.f));
		}
	}

	// Floor/Properties: everything inside them tracks their own panel width in turn (minus ITS 14px-
	// a-side padding) - see ComputePanelButtonWidth.
	const float FloorButtonWidth = ComputePanelButtonWidth(EDockPanelId::Floor);
	const float PropertiesButtonWidth = ComputePanelButtonWidth(EDockPanelId::Properties);

	auto ResizeButton = [](UButton* Button, float Width)
	{
		if (Button)
		{
			if (USizeBox* SizeBox = Cast<USizeBox>(Button->GetContent()))
			{
				SizeBox->SetWidthOverride(Width);
			}
		}
	};
	ResizeButton(AddFloorButton, FloorButtonWidth);
	ResizeButton(RemoveFloorButton, FloorButtonWidth);
	ResizeButton(ApplyDoorButton, PropertiesButtonWidth);
	ResizeButton(ApplyRoomButton, PropertiesButtonWidth);
	ResizeButton(ApplyConnectorButton, PropertiesButtonWidth);

	auto ResizeLabelWrap = [](UTextBlock* TextBlock, float Width)
	{
		if (TextBlock)
		{
			if (USizeBox* SizeBox = Cast<USizeBox>(TextBlock->GetParent()))
			{
				SizeBox->SetWidthOverride(Width);
			}
		}
	};
	ResizeLabelWrap(PropertiesTitleText, PropertiesButtonWidth);
	ResizeLabelWrap(DoorRoomText, PropertiesButtonWidth);
	ResizeLabelWrap(ConnectorTypeText, PropertiesButtonWidth);

	if (ConnectsToRowSizeBox)
	{
		ConnectsToRowSizeBox->SetWidthOverride(PropertiesButtonWidth);
	}
	if (RoomNameRowSizeBox)
	{
		RoomNameRowSizeBox->SetWidthOverride(PropertiesButtonWidth);
	}
	if (FloorsWrapSizeBox)
	{
		FloorsWrapSizeBox->SetWidthOverride(PropertiesButtonWidth);
	}

	if (FloorListBox)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(FloorListBox->GetParent()))
		{
			SizeBox->SetWidthOverride(FloorButtonWidth);
		}
		for (int32 Index = 0; Index < FloorListBox->GetChildrenCount(); ++Index)
		{
			if (UFloorListEntryWidget* Entry = Cast<UFloorListEntryWidget>(FloorListBox->GetChildAt(Index)))
			{
				Entry->SetWidth(FloorButtonWidth);
			}
		}
	}

	// Layers: search box and the list's own width track the panel the same way as everything above;
	// existing rows are updated directly (same reasoning as FloorListBox's entries) rather than
	// waiting for the next RefreshLayersList to happen to be called for some other reason.
	const float LayersButtonWidth = ComputePanelButtonWidth(EDockPanelId::Layers);
	if (LayersSearchBox)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(LayersSearchBox->GetParent()))
		{
			SizeBox->SetWidthOverride(LayersButtonWidth);
		}
	}
	if (LayersListBox)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(LayersListBox->GetParent()))
		{
			SizeBox->SetWidthOverride(LayersButtonWidth);
		}
		for (int32 Index = 0; Index < LayersListBox->GetChildrenCount(); ++Index)
		{
			if (ULayerRowWidget* Row = Cast<ULayerRowWidget>(LayersListBox->GetChildAt(Index)))
			{
				Row->SetWidth(LayersButtonWidth);
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

	// Properties is a permanent panel now, same as Floor/Toolbar/Layers - it never collapses away
	// entirely; with nothing selected it just shows an empty "Properties" placeholder instead of
	// disappearing, matching a real editor's Details/Properties panel (see AutoDockPropertiesBelowFloor,
	// called once at construction instead of waiting for a first selection to dock it under Floor).
	PropertiesPanel->SetVisibility(ESlateVisibility::Visible);

	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	const bool bAnySelected = PC && (!PC->GetSelectedRoomId().IsEmpty() || !PC->GetSelectedDoorId().IsEmpty() || !PC->GetSelectedConnectorGroupId().IsEmpty());
	if (!bAnySelected && PropertiesTitleText)
	{
		PropertiesTitleText->SetText(FText::FromString(TEXT("Properties")));
	}

	// Its natural content height changes meaningfully with every selection change (Door/Room/
	// Connector fields are different heights, and the empty placeholder above is shorter than any of
	// them) - queue a re-split of its column once geometry actually reflects the new content. See
	// PropertiesFractionRecalcFramesRemaining's own comment for why more than one tick is needed.
	PropertiesFractionRecalcFramesRemaining = PropertiesFractionRecalcFrameCount;

	RefreshSplitterLayout();
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
		Entry->SetWidth(ComputePanelButtonWidth(EDockPanelId::Floor));
		Entry->OnEntryClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleFloorEntryClicked);

		if (UScrollBoxSlot* EntrySlot = Cast<UScrollBoxSlot>(FloorListBox->AddChild(Entry)))
		{
			EntrySlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}
	}
}

void UInteriorScreenWidget::ResizeLayersListToFit(float AvailablePanelHeight)
{
	if (!LayersListBox)
	{
		return;
	}
	USizeBox* SizeBox = Cast<USizeBox>(LayersListBox->GetParent());
	if (!SizeBox)
	{
		return;
	}

	if (AvailablePanelHeight < 0.f)
	{
		SizeBox->SetHeightOverride(LayersListDefaultHeight);
		return;
	}

	const float FittedHeight = FMath::Clamp(AvailablePanelHeight - LayersListNonListOverhead, LayersListMinHeight, LayersListDefaultHeight);
	SizeBox->SetHeightOverride(FittedHeight);
}

void UInteriorScreenWidget::RefreshLayersList()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC || !LayersListBox)
	{
		return;
	}

	LayersListBox->ClearChildren();

	const FString SearchFilter = LayersSearchBox ? LayersSearchBox->GetText().ToString() : FString();
	const float RowWidth = ComputePanelButtonWidth(EDockPanelId::Layers);
	constexpr float ItemIndent = 20.f;

	// Category headers are never filtered out by search (only their children are) - if they were,
	// searching for an item would also hide the very row you'd expand to find more like it.
	auto AddRow = [this, PC, RowWidth](const FString& RowKey, const FString& Label, float IndentPx, bool bShowExpandArrow, bool bIsExpanded, bool bIsVisible)
	{
		ULayerRowWidget* Row = CreateWidget<ULayerRowWidget>(PC, ULayerRowWidget::StaticClass());
		if (!Row)
		{
			return;
		}
		Row->Setup(RowKey, Label, IndentPx, bShowExpandArrow, bIsExpanded, bIsVisible);
		Row->SetWidth(RowWidth);
		Row->OnExpandClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleLayerRowExpandClicked);
		Row->OnVisibilityClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleLayerRowVisibilityClicked);
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(LayersListBox->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
		}
	};

	const int32 ActiveFloor = PC->GetActiveFloor();

	// -- Boundary -- exactly one item, the loaded building itself.
	{
		const bool bExpanded = ExpandedLayerCategories.Contains(TEXT("category:Boundary"));
		AddRow(TEXT("category:Boundary"), TEXT("Boundary"), 0.f, true, bExpanded, PC->IsBoundaryVisible());
		if (bExpanded)
		{
			const FString BuildingName = PC->GetLoadedBuildingName();
			const FString ItemLabel = BuildingName.IsEmpty() ? TEXT("Building outline") : BuildingName;
			if (SearchFilter.IsEmpty() || ItemLabel.Contains(SearchFilter))
			{
				AddRow(TEXT("item:Boundary:building"), ItemLabel, ItemIndent, false, false, PC->IsBoundaryVisible());
			}
		}
	}

	// -- Rooms -- and -- Walls -- both come from GetRoomIdsOnFloor(), split by IsRoomClosedLoop (see
	// InteriorFloorPlanWidget::NativePaint, which draws them the same way - AInteriorRoom covers
	// both closed rooms and open standalone walls, sharing the same id/class).
	{
		const bool bExpanded = ExpandedLayerCategories.Contains(TEXT("category:Rooms"));
		AddRow(TEXT("category:Rooms"), TEXT("Rooms"), 0.f, true, bExpanded, PC->AreRoomsVisible());
		if (bExpanded)
		{
			for (const FString& RoomId : PC->GetRoomIdsOnFloor(ActiveFloor))
			{
				if (!PC->IsRoomClosedLoop(RoomId))
				{
					continue;
				}
				const FString RoomName = PC->GetRoomName(RoomId);
				const FString ItemLabel = RoomName.IsEmpty() ? RoomId : RoomName;
				if (!SearchFilter.IsEmpty() && !ItemLabel.Contains(SearchFilter))
				{
					continue;
				}
				AddRow(FString::Printf(TEXT("item:Rooms:%s"), *RoomId), ItemLabel, ItemIndent, false, false, !PC->IsRoomHidden(RoomId));
			}
		}
	}

	{
		const bool bExpanded = ExpandedLayerCategories.Contains(TEXT("category:Walls"));
		AddRow(TEXT("category:Walls"), TEXT("Walls"), 0.f, true, bExpanded, PC->AreWallsVisible());
		if (bExpanded)
		{
			for (const FString& RoomId : PC->GetRoomIdsOnFloor(ActiveFloor))
			{
				if (PC->IsRoomClosedLoop(RoomId))
				{
					continue;
				}
				const FString RoomName = PC->GetRoomName(RoomId);
				const FString ItemLabel = RoomName.IsEmpty() ? RoomId : RoomName;
				if (!SearchFilter.IsEmpty() && !ItemLabel.Contains(SearchFilter))
				{
					continue;
				}
				AddRow(FString::Printf(TEXT("item:Walls:%s"), *RoomId), ItemLabel, ItemIndent, false, false, !PC->IsRoomHidden(RoomId));
			}
		}
	}

	// -- Path Nodes -- and -- I/O Nodes (elevator/stairs/escalator) -- both come from GetNavNodes(),
	// split by type (see InteriorFloorPlanWidget::NativePaint, which draws them the same way).
	{
		const bool bExpanded = ExpandedLayerCategories.Contains(TEXT("category:PathNodes"));
		AddRow(TEXT("category:PathNodes"), TEXT("Path Nodes"), 0.f, true, bExpanded, PC->ArePathNodesVisible());
		if (bExpanded)
		{
			for (const FInteriorNavNode& Node : PC->GetNavNodes())
			{
				if (Node.Level != ActiveFloor || Node.Type != EInteriorNavNodeType::PathNode)
				{
					continue;
				}
				const FString ItemLabel = FString::Printf(TEXT("Path Node - %s"), *Node.Id);
				if (!SearchFilter.IsEmpty() && !ItemLabel.Contains(SearchFilter))
				{
					continue;
				}
				AddRow(FString::Printf(TEXT("item:PathNodes:%s"), *Node.Id), ItemLabel, ItemIndent, false, false, !PC->IsPathNodeHidden(Node.Id));
			}
		}
	}

	{
		const bool bExpanded = ExpandedLayerCategories.Contains(TEXT("category:IONodes"));
		AddRow(TEXT("category:IONodes"), TEXT("I/O Nodes"), 0.f, true, bExpanded, PC->AreIONodesVisible());
		if (bExpanded)
		{
			for (const FInteriorNavNode& Node : PC->GetNavNodes())
			{
				if (Node.Level != ActiveFloor || Node.Type == EInteriorNavNodeType::PathNode)
				{
					continue;
				}

				FString TypeName;
				switch (Node.Type)
				{
				case EInteriorNavNodeType::Elevator:  TypeName = TEXT("Elevator");  break;
				case EInteriorNavNodeType::Stairs:    TypeName = TEXT("Stairs");    break;
				case EInteriorNavNodeType::Escalator: TypeName = TEXT("Escalator"); break;
				default: break;
				}
				const FString ItemLabel = FString::Printf(TEXT("%s - %s"), *TypeName, *Node.Id);
				if (!SearchFilter.IsEmpty() && !ItemLabel.Contains(SearchFilter))
				{
					continue;
				}
				AddRow(FString::Printf(TEXT("item:IONodes:%s"), *Node.Id), ItemLabel, ItemIndent, false, false, !PC->IsIONodeHidden(Node.Id));
			}
		}
	}
}

void UInteriorScreenWidget::HandleLayerRowExpandClicked(FString RowKey)
{
	if (ExpandedLayerCategories.Contains(RowKey))
	{
		ExpandedLayerCategories.Remove(RowKey);
	}
	else
	{
		ExpandedLayerCategories.Add(RowKey);
	}
	RefreshLayersList();
}

void UInteriorScreenWidget::HandleLayerRowVisibilityClicked(FString RowKey)
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	// RowKey is either "category:<Name>" or "item:<Category>:<Id>" (see the comment on
	// FOnLayerRowKeyClicked) - split on ':' to figure out which PC setter to call.
	TArray<FString> Parts;
	RowKey.ParseIntoArray(Parts, TEXT(":"), false);
	if (Parts.Num() < 2)
	{
		return;
	}

	if (Parts[0] == TEXT("category"))
	{
		const FString& Category = Parts[1];
		if (Category == TEXT("Boundary"))       { PC->SetBoundaryVisible(!PC->IsBoundaryVisible()); }
		else if (Category == TEXT("Rooms"))     { PC->SetRoomsVisible(!PC->AreRoomsVisible()); }
		else if (Category == TEXT("Walls"))     { PC->SetWallsVisible(!PC->AreWallsVisible()); }
		else if (Category == TEXT("PathNodes")) { PC->SetPathNodesVisible(!PC->ArePathNodesVisible()); }
		else if (Category == TEXT("IONodes"))   { PC->SetIONodesVisible(!PC->AreIONodesVisible()); }
	}
	else if (Parts[0] == TEXT("item") && Parts.Num() >= 3)
	{
		const FString& Category = Parts[1];
		// The id itself may contain ':' (unlikely, but ids are otherwise free-form) - rejoin
		// everything after the category so it isn't truncated at the first one.
		FString Id = Parts[2];
		for (int32 i = 3; i < Parts.Num(); ++i)
		{
			Id += TEXT(":") + Parts[i];
		}

		if (Category == TEXT("Boundary"))       { /* single item, nothing more specific to toggle than the category itself */ PC->SetBoundaryVisible(!PC->IsBoundaryVisible()); }
		else if (Category == TEXT("Rooms"))     { PC->SetRoomHidden(Id, !PC->IsRoomHidden(Id)); }
		else if (Category == TEXT("Walls"))     { PC->SetRoomHidden(Id, !PC->IsRoomHidden(Id)); }
		else if (Category == TEXT("PathNodes")) { PC->SetPathNodeHidden(Id, !PC->IsPathNodeHidden(Id)); }
		else if (Category == TEXT("IONodes"))   { PC->SetIONodeHidden(Id, !PC->IsIONodeHidden(Id)); }
	}

	RefreshLayersList();
}

void UInteriorScreenWidget::HandleLayersSearchTextChanged(const FText& NewText)
{
	RefreshLayersList();
}

void UInteriorScreenWidget::HandleFloorEntryClicked(int32 FloorNumber)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetActiveFloor(FloorNumber);
	}
	RefreshFloorList();
	RefreshLayersList();
}

void UInteriorScreenWidget::OnAddFloorClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->AddFloor();
	}
	RefreshFloorList();
	RefreshLayersList();
}

void UInteriorScreenWidget::OnRemoveFloorClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->RemoveFloor(PC->GetActiveFloor());
	}
	RefreshFloorList();
	RefreshLayersList();
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
		// Same reasoning as GoToEditScreen - the dock layout is a local preference, not project
		// data, so it's saved regardless of whether there was any actual building content to save.
		SaveDockLayoutToDisk();
		PC->LoadInteriorForBuilding(NewBuildingId);
		RefreshBuildingSwitcher();
		RefreshFloorList();
		RefreshLayersList();
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
	SaveDockLayoutToDisk();
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SaveCurrentProject();
		PC->LoadInteriorForBuilding(PendingBuildingId);
	}
	RefreshBuildingSwitcher();
	RefreshFloorList();
	RefreshLayersList();
}

void UInteriorScreenWidget::HandleSwitchBuildingDiscard()
{
	// Discarding the building's own content edits shouldn't also lose the panel layout - see
	// GoToEditScreen's comment.
	SaveDockLayoutToDisk();
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->DiscardUnsavedChanges();
		PC->LoadInteriorForBuilding(PendingBuildingId);
	}
	RefreshBuildingSwitcher();
	RefreshFloorList();
	RefreshLayersList();
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
	// GoToEditScreen (below) already saves the dock layout unconditionally on every exit.
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

	// The dock layout is a harmless local preference, not project data, so it's saved unconditionally
	// on every real exit from this screen - unlike PC->SaveCurrentProject(), it doesn't need gating
	// behind PC->HasUnsavedChanges() (rearranging panels alone doesn't set that flag, so relying on
	// it here would silently drop a layout-only change whenever Back skips the save dialog entirely).
	SaveDockLayoutToDisk();

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
