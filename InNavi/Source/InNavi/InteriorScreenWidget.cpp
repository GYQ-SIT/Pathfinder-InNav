#include "InteriorScreenWidget.h"

#include "AudioFileRowWidget.h"
#include "ConfirmDialogWidget.h"
#include "EditScreenWidget.h"
#include "FloorListEntryWidget.h"
#include "InteriorFloorPlanWidget.h"
#include "InteriorTypes.h"
#include "LayerRowWidget.h"
#include "NavigationPanelWidget.h"
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
#include "Components/Slider.h"
#include "Components/Spacer.h"
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
	bDockPanelUserHidden.Init(false, NumDockPanels);
	// Audio starts hidden - see the enum's own comment. ConstructDockPanel's Audio call site (below)
	// collapses its actual Border to match.
	bDockPanelUserHidden[(int32)EDockPanelId::Audio] = true;
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

void UInteriorScreenWidget::ApplyStandardFont(UTextBlock* TextBlock)
{
	if (!TextBlock)
	{
		return;
	}
	FSlateFontInfo Font = TextBlock->GetFont();
	Font.Size = StandardFontSize;
	TextBlock->SetFont(Font);
}

void UInteriorScreenWidget::ApplyStandardFont(UEditableTextBox* TextBox)
{
	if (!TextBox)
	{
		return;
	}
	FEditableTextBoxStyle Style = TextBox->GetWidgetStyle();
	Style.TextStyle.Font.Size = StandardFontSize;
	TextBox->SetWidgetStyle(Style);
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
		Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
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
		// shares this exact size, so nothing looks bigger/smaller than anything else - sized to
		// comfortably fit StandardFontSize text without the label spilling past the button. Floor
		// list rows are the one exception built elsewhere (see FloorListEntryWidget::RebuildWidget) -
		// keep that in sync with these values by hand.
		constexpr float StandardButtonWidth = 150.f;
		constexpr float StandardButtonHeight = 32.f;

		// Safety net, not the primary fix - wraps to a second line instead of spilling past the
		// button on the rare label that's still too wide for StandardButtonWidth at default font size.
		auto ApplyStandardTextWrap = [](UTextBlock* TextBlock)
		{
			TextBlock->SetAutoWrapText(true);
			ApplyStandardFont(TextBlock);
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
			// A SizeBox's height/width override only constrains LAYOUT size, not paint size - content
			// taller than a squeezed-down panel (e.g. Properties' fields when a splitter drag or the
			// screen's own edge leaves it little room) would otherwise paint straight past the panel's
			// own border, potentially off the visible screen entirely. Clip it to its own bounds instead.
			Border->SetClipping(EWidgetClipping::ClipToBoundsAlways);
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

		// 4 splitter handles - the most that can ever be needed at once (all 5 panels docked together
		// in one column has 4 internal boundaries). RefreshSplitterLayout shows/hides and repositions
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
			else if (SplitterIndex == 2)
			{
				Splitter->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter2MouseButtonDown);
				Splitter->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter2MouseMove);
				Splitter->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter2MouseButtonUp);
			}
			else if (SplitterIndex == 3)
			{
				Splitter->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter3MouseButtonDown);
				Splitter->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter3MouseMove);
				Splitter->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter3MouseButtonUp);
			}
			else
			{
				Splitter->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter4MouseButtonDown);
				Splitter->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter4MouseMove);
				Splitter->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnSplitter4MouseButtonUp);
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

		// -- Toolbar panel: the mode toolbar grouped into Default/Draw/Navigate sections (matching the
		// hand-drawn layout) - one VerticalBox so everything auto-stacks instead of each piece needing
		// its own guessed Y position. The building switcher used to live at the top of this panel; it's
		// now its own fixed bar below the menu bar instead (see BuildingSwitcherBarBackground further
		// down) so it stays put regardless of how this panel gets dragged/resized/hidden.
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

		auto AddToolGroupLabel = [this, LeftSidebar](const TCHAR* Name, const TCHAR* Label)
		{
			UTextBlock* GroupLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
			GroupLabel->SetText(FText::FromString(FString(Label)));
			GroupLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f, 1.f)));
			ApplyStandardFont(GroupLabel);
			UVerticalBoxSlot* GroupLabelSlot = LeftSidebar->AddChildToVerticalBox(GroupLabel);
			GroupLabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
		};

		// Icon-only now instead of a text label - sidesteps the font-size/overflow fight entirely
		// (an icon has no line-wrap or width-overflow failure mode), at the cost of needing a
		// tooltip so the mode is still discoverable. IconAssetName is one of Padre's existing
		// toolbar icons under /Game/ImageAssets (Icon_AddDoor, Icon_DrawRoom, ...), previously
		// unreferenced in C++ (same asset pack already reused for the floor-plan connector markers
		// in InteriorFloorPlanWidget).
		constexpr float ModeButtonSize = 36.f;

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
			Button->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);

			UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(FString(Name) + TEXT("Icon")));
			const FString IconPath = FString::Printf(TEXT("/Game/ImageAssets/%s.%s"), IconAssetName, IconAssetName);
			if (UTexture2D* IconTexture = LoadObject<UTexture2D>(nullptr, *IconPath))
			{
				// Not bMatchSize - these source PNGs weren't authored at a consistent resolution, so
				// matching native size would make icons different sizes (and some bigger than the
				// button itself). DesiredSizeOverride forces every icon to render at the same size
				// regardless of its source image's actual resolution.
				Icon->SetBrushFromTexture(IconTexture, false);
				Icon->SetDesiredSizeOverride(FVector2D(22.f, 22.f));
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

		AddToolGroupLabel(TEXT("Txt_GroupGizmos"), TEXT("Gizmos"));
		// One icon asset each, reused for both buttons in its row rather than needing 4 separate
		// images: Icon_GizmoMove is drawn at two different sizes (Grow full-size, Shrink shrunk down)
		// so the icon's own size mirrors what the button does; Icon_GizmoRotate is drawn at two
		// different rotations (CW as-authored, CCW rotated 180) so the two read as opposite
		// directions. Grow/Shrink and Rotate CW/CCW mirror PC's existing =/- and 9/0 keys exactly (see
		// OnGrowRoomClicked etc.) rather than introducing any new interaction - small square buttons,
		// two per row, per the hand-drawn layout.
		constexpr float GizmoButtonSize = 26.f;
		constexpr float GizmoIconSizeLarge = 18.f;
		constexpr float GizmoIconSizeSmall = 11.f; // Shrink reuses Icon_GizmoMove at this smaller size instead of its own asset
		auto AddGizmoIconButton = [this, MakeToolTipWidget, GizmoButtonSize](UHorizontalBox* Row, const TCHAR* Name, const TCHAR* IconAssetName, float IconSize, float IconRotationDegrees, const TCHAR* TooltipLabel) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
			Button->SetToolTip(MakeToolTipWidget(TooltipLabel));
			Button->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);

			UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(FString(Name) + TEXT("Icon")));
			const FString IconPath = FString::Printf(TEXT("/Game/ImageAssets/%s.%s"), IconAssetName, IconAssetName);
			if (UTexture2D* IconTexture = LoadObject<UTexture2D>(nullptr, *IconPath))
			{
				Icon->SetBrushFromTexture(IconTexture, false);
				Icon->SetDesiredSizeOverride(FVector2D(IconSize, IconSize));
			}
			if (!FMath::IsNearlyZero(IconRotationDegrees))
			{
				Icon->SetRenderTransformAngle(IconRotationDegrees);
			}

			USizeBox* GizmoSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			GizmoSizeBox->SetWidthOverride(GizmoButtonSize);
			GizmoSizeBox->SetHeightOverride(GizmoButtonSize);
			if (USizeBoxSlot* GizmoIconSlot = Cast<USizeBoxSlot>(GizmoSizeBox->SetContent(Icon)))
			{
				GizmoIconSlot->SetHorizontalAlignment(HAlign_Center);
				GizmoIconSlot->SetVerticalAlignment(VAlign_Center);
			}
			Button->SetContent(GizmoSizeBox);

			UHorizontalBoxSlot* GizmoRowSlot = Row->AddChildToHorizontalBox(Button);
			GizmoRowSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
			return Button;
		};

		UHorizontalBox* GizmoScaleRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GizmoScaleRow"));
		UVerticalBoxSlot* GizmoScaleRowSlot = LeftSidebar->AddChildToVerticalBox(GizmoScaleRow);
		GizmoScaleRowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		// The row only takes up as much width as its 2 small buttons, not the panel's full width - by
		// default a VerticalBoxSlot stretches/left-aligns it, leaving it hugging the left edge. Center
		// it instead, matching the hand-drawn layout.
		GizmoScaleRowSlot->SetHorizontalAlignment(HAlign_Center);
		GrowRoomButton = AddGizmoIconButton(GizmoScaleRow, TEXT("Btn_GrowRoom"), TEXT("Icon_GizmoMove"), GizmoIconSizeLarge, 0.f, TEXT("Grow (increase the selected room's size)"));
		GrowRoomButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnGrowRoomClicked);
		ShrinkRoomButton = AddGizmoIconButton(GizmoScaleRow, TEXT("Btn_ShrinkRoom"), TEXT("Icon_GizmoMove"), GizmoIconSizeSmall, 0.f, TEXT("Shrink (decrease the selected room's size)"));
		ShrinkRoomButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnShrinkRoomClicked);

		UHorizontalBox* GizmoRotateRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GizmoRotateRow"));
		if (UVerticalBoxSlot* GizmoRotateRowSlot = LeftSidebar->AddChildToVerticalBox(GizmoRotateRow))
		{
			GizmoRotateRowSlot->SetHorizontalAlignment(HAlign_Center);
		}
		RotateCCWButton = AddGizmoIconButton(GizmoRotateRow, TEXT("Btn_RotateCCW"), TEXT("Icon_GizmoRotate"), GizmoIconSizeLarge, 180.f, TEXT("Rotate Left (hold to keep rotating the selected room counter-clockwise)"));
		RotateCCWButton->OnPressed.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRotateCCWPressed);
		RotateCCWButton->OnReleased.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRotateCCWReleased);
		RotateCWButton = AddGizmoIconButton(GizmoRotateRow, TEXT("Btn_RotateCW"), TEXT("Icon_GizmoRotate"), GizmoIconSizeLarge, 0.f, TEXT("Rotate Right (hold to keep rotating the selected room clockwise)"));
		RotateCWButton->OnPressed.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRotateCWPressed);
		RotateCWButton->OnReleased.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRotateCWReleased);

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
		ApplyStandardFont(FloorTitle);
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

		// -- Top menu bar (File/Window/Back) - fixed chrome, NOT part of the docking system (always
		// stays put, like a permanent menu bar in most editors). A thin full-width background strip
		// holding the File/Window menu buttons on the left and Back pinned to the right (a Fill-sized
		// Spacer between them pushes Back to the far edge, same trick a real toolbar uses); each menu
		// button's own dropdown is a separate Canvas child positioned at a fixed offset directly below
		// it (both button widths are fixed, so their X positions are known up front - simpler than
		// querying a HorizontalBox child's live position) rather than nested inside the same layout
		// flow, so opening one never resizes the bar itself.
		constexpr float MenuButtonWidth = 50.f;
		constexpr float MenuBarLeftMargin = 10.f;

		UBorder* MenuBarBackground = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_MenuBar"));
		MenuBarBackground->SetBrushColor(DockPanelColor);
		// Left margin so "File" doesn't sit flush against the screen's own left edge - File/Window's
		// own MenuButtonSizeBox already matches MenuBarHeight exactly, so the taller bar (see
		// MenuBarHeight's own comment) gives their text vertical breathing room on its own.
		MenuBarBackground->SetPadding(FMargin(MenuBarLeftMargin, 0.f, 0.f, 0.f));
		UCanvasPanelSlot* MenuBarSlot = Canvas->AddChildToCanvas(MenuBarBackground);
		MenuBarSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
		MenuBarSlot->SetAlignment(FVector2D(0.f, 0.f));
		MenuBarSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, MenuBarHeight));
		MenuBarSlot->SetZOrder(6);

		UHorizontalBox* MenuBarRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MenuBarRow"));
		MenuBarBackground->SetContent(MenuBarRow);

		auto AddMenuButton = [this, MenuBarRow, MenuButtonWidth](const TCHAR* Name, const TCHAR* Label) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f)); // transparent - just the bar's own background shows through
			Button->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Text->SetJustification(ETextJustify::Center);
			ApplyStandardFont(Text);
			USizeBox* MenuButtonSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			MenuButtonSizeBox->SetWidthOverride(MenuButtonWidth);
			MenuButtonSizeBox->SetHeightOverride(MenuBarHeight);
			if (USizeBoxSlot* TextSlot = Cast<USizeBoxSlot>(MenuButtonSizeBox->SetContent(Text)))
			{
				TextSlot->SetHorizontalAlignment(HAlign_Center);
				TextSlot->SetVerticalAlignment(VAlign_Center);
			}
			Button->SetContent(MenuButtonSizeBox);
			MenuBarRow->AddChildToHorizontalBox(Button);
			return Button;
		};

		FileMenuButton = AddMenuButton(TEXT("Btn_FileMenu"), TEXT("File"));
		FileMenuButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnFileMenuClicked);
		WindowMenuButton = AddMenuButton(TEXT("Btn_WindowMenu"), TEXT("Window"));
		WindowMenuButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnWindowMenuClicked);

		// Eats all remaining row width so Back (added right after it) lands flush against the bar's
		// right edge instead of sitting right beside Window.
		USpacer* MenuBarSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
		if (UHorizontalBoxSlot* SpacerSlot = Cast<UHorizontalBoxSlot>(MenuBarRow->AddChildToHorizontalBox(MenuBarSpacer)))
		{
			SpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		// A plain text label, not the arrow-glyph icon this used to be - tried the icon-only look, but
		// after actually using it a text label reads faster/clearer than the arrow, so back to this.
		BackButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_Back"));
		BackButton->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		UTextBlock* BackText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Btn_BackLabel"));
		BackText->SetText(FText::FromString(TEXT("Back")));
		BackText->SetJustification(ETextJustify::Center);
		BackText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(BackText);
		USizeBox* BackSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		BackSizeBox->SetWidthOverride(60.f);
		BackSizeBox->SetHeightOverride(28.f); // shorter than MenuBarHeight so it visibly sits inside the bar once centered, not flush top/bottom
		if (USizeBoxSlot* BackTextSlot = Cast<USizeBoxSlot>(BackSizeBox->SetContent(BackText)))
		{
			BackTextSlot->SetHorizontalAlignment(HAlign_Center);
			BackTextSlot->SetVerticalAlignment(VAlign_Center);
		}
		BackButton->SetContent(BackSizeBox);
		if (UHorizontalBoxSlot* BackRowSlot = Cast<UHorizontalBoxSlot>(MenuBarRow->AddChildToHorizontalBox(BackButton)))
		{
			BackRowSlot->SetVerticalAlignment(VAlign_Center);
			BackRowSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		}

		// Returns the dropdown's own Border - callers reach its UVerticalBox content back out via
		// Cast<UVerticalBox>(Dropdown->GetContent()), same "reach a child through the widget tree
		// instead of returning/storing every intermediate" convention already used throughout this
		// function (e.g. ConstructDockPanel's own resize handles).
		auto ConstructDropdown = [this, Canvas, DockPanelColor](const TCHAR* Name, float PositionX) -> UBorder*
		{
			UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(FString(Name) + TEXT("Stack")));
			UBorder* Dropdown = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Name));
			Dropdown->SetBrushColor(DockPanelColor);
			Dropdown->SetPadding(FMargin(4.f));
			Dropdown->SetContent(Stack);
			Dropdown->SetVisibility(ESlateVisibility::Collapsed);
			UCanvasPanelSlot* DropdownSlot = Canvas->AddChildToCanvas(Dropdown);
			DropdownSlot->SetAnchors(FAnchors(0.f, 0.f));
			DropdownSlot->SetAlignment(FVector2D(0.f, 0.f));
			// Below BOTH fixed bars (menu bar + building switcher bar), not just the menu bar - otherwise
			// an open dropdown would visually sit on top of the switcher bar instead of below it.
			DropdownSlot->SetPosition(FVector2D(PositionX, SidebarTopMargin + 4.f));
			DropdownSlot->SetAutoSize(true);
			DropdownSlot->SetZOrder(7);
			return Dropdown;
		};

		auto AddDropdownButtonRow = [this](UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
			Button->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetJustification(ETextJustify::Center);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(Text);
			USizeBox* RowSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			RowSizeBox->SetWidthOverride(90.f);
			RowSizeBox->SetMinDesiredHeight(24.f);
			RowSizeBox->SetContent(Text);
			Button->SetContent(RowSizeBox);
			UVerticalBoxSlot* RowSlot = Stack->AddChildToVerticalBox(Button);
			RowSlot->SetPadding(FMargin(2.f));
			return Button;
		};

		auto AddDropdownCheckRow = [this](UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label) -> UCheckBox*
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			UCheckBox* CheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), FName(Name));
			CheckBox->SetIsChecked(true);
			UHorizontalBoxSlot* CheckSlot = Row->AddChildToHorizontalBox(CheckBox);
			CheckSlot->SetVerticalAlignment(VAlign_Center);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(Text);
			UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
			TextSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
			TextSlot->SetVerticalAlignment(VAlign_Center);
			UVerticalBoxSlot* RowSlot = Stack->AddChildToVerticalBox(Row);
			RowSlot->SetPadding(FMargin(2.f));
			return CheckBox;
		};

		FileMenuDropdown = ConstructDropdown(TEXT("Pnl_FileDropdown"), MenuBarLeftMargin);
		UVerticalBox* FileDropdownStack = Cast<UVerticalBox>(FileMenuDropdown->GetContent());
		UButton* FileNewButton = AddDropdownButtonRow(FileDropdownStack, TEXT("Btn_FileNew"), TEXT("New"));
		FileNewButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnFileNewClicked);
		UButton* FileLoadButton = AddDropdownButtonRow(FileDropdownStack, TEXT("Btn_FileLoad"), TEXT("Load"));
		FileLoadButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnFileLoadClicked);

		WindowMenuDropdown = ConstructDropdown(TEXT("Pnl_WindowDropdown"), MenuBarLeftMargin + MenuButtonWidth + 10.f);
		UVerticalBox* WindowDropdownStack = Cast<UVerticalBox>(WindowMenuDropdown->GetContent());
		WindowToolbarCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowToolbar"), TEXT("Toolbar"));
		WindowToolbarCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnWindowToolbarCheckChanged);
		WindowFloorCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowFloor"), TEXT("Floor"));
		WindowFloorCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnWindowFloorCheckChanged);
		WindowPropertiesCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowProperties"), TEXT("Properties"));
		WindowPropertiesCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnWindowPropertiesCheckChanged);
		WindowLayersCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowLayers"), TEXT("Layers"));
		WindowLayersCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnWindowLayersCheckChanged);
		WindowNavigationCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowNavigation"), TEXT("Navigation"));
		WindowNavigationCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnWindowNavigationCheckChanged);
		WindowAudioCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowAudio"), TEXT("Audio"));
		// Unlike every other row, starts unchecked - see EDockPanelId::Audio's own comment.
		WindowAudioCheckbox->SetIsChecked(false);
		WindowAudioCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnWindowAudioCheckChanged);

		// Full-screen, fully transparent, Collapsed by default - see the comment on
		// OnMenuClickCatcherMouseButtonDown for why this is how "click elsewhere closes the dropdown"
		// works. Z=5 sits above every dock panel/handle/splitter (2-4) but below the menu bar and
		// dropdowns themselves (6-7), so File/Window and the open dropdown's own content stay
		// clickable while anything else on screen closes it first instead of acting through.
		MenuClickCatcher = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuClickCatcher"));
		MenuClickCatcher->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		MenuClickCatcher->SetVisibility(ESlateVisibility::Collapsed);
		MenuClickCatcher->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnMenuClickCatcherMouseButtonDown);
		UCanvasPanelSlot* MenuClickCatcherSlot = Canvas->AddChildToCanvas(MenuClickCatcher);
		MenuClickCatcherSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		MenuClickCatcherSlot->SetOffsets(FMargin(0.f));
		MenuClickCatcherSlot->SetZOrder(5);

		// -- Building switcher bar: fixed chrome directly under the menu bar, spanning the full width
		// like it does - holds the building switcher combo box (+ its edit-name button), pulled out of
		// the Toolbar panel so it stays visible regardless of how that panel gets dragged, resized, or
		// hidden. Content stays left-aligned; the rest of the bar's width is just empty background.
		UBorder* BuildingSwitcherBarBackground = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_BuildingSwitcherBar"));
		BuildingSwitcherBarBackground->SetBrushColor(DockPanelColor);
		BuildingSwitcherBarBackground->SetPadding(FMargin(10.f, 8.f));
		BuildingSwitcherBarBackground->SetHorizontalAlignment(HAlign_Left);
		BuildingSwitcherBarBackground->SetVerticalAlignment(VAlign_Center);
		UCanvasPanelSlot* BuildingSwitcherBarSlot = Canvas->AddChildToCanvas(BuildingSwitcherBarBackground);
		BuildingSwitcherBarSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
		BuildingSwitcherBarSlot->SetAlignment(FVector2D(0.f, 0.f));
		BuildingSwitcherBarSlot->SetOffsets(FMargin(0.f, MenuBarHeight, 0.f, BuildingSwitcherBarHeight));
		BuildingSwitcherBarSlot->SetZOrder(6);

		UHorizontalBox* SwitcherRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BuildingSwitcherRow"));
		BuildingSwitcherBarBackground->SetContent(SwitcherRow);

		BuildingSwitcherComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Cbx_BuildingSwitcher"));
		BuildingSwitcherComboBox->OnGenerateWidgetEvent.BindDynamic(this, &UInteriorScreenWidget::HandleGenerateComboBoxItem);
		USizeBox* BuildingSwitcherSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		BuildingSwitcherSizeBox->SetWidthOverride(260.f); // fixed now - this bar no longer resizes with any dock panel
		BuildingSwitcherSizeBox->SetContent(BuildingSwitcherComboBox);
		UHorizontalBoxSlot* SwitcherComboSlot = SwitcherRow->AddChildToHorizontalBox(BuildingSwitcherSizeBox);
		SwitcherComboSlot->SetVerticalAlignment(VAlign_Center);
		SwitcherComboSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));

		BuildingNameTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_BuildingName"));
		BuildingNameTextBox->SetVisibility(ESlateVisibility::Collapsed);
		ApplyStandardFont(BuildingNameTextBox);
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

		// 2D/3D toggle + "Reset View"/move-speed +/-, same row as EditBuildingNameButton - see the
		// comment on Switch2DButton in the header for why these live here instead of the menu bar.
		constexpr float SwitcherButtonWidth = 44.f;
		constexpr float SwitcherButtonHeight = 32.f;
		auto AddSwitcherButton = [this, SwitcherRow, SwitcherButtonWidth, SwitcherButtonHeight](const TCHAR* Name, const TCHAR* Label) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
			Button->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Text->SetJustification(ETextJustify::Center);
			ApplyStandardFont(Text);
			USizeBox* ButtonSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			ButtonSizeBox->SetWidthOverride(SwitcherButtonWidth);
			ButtonSizeBox->SetHeightOverride(SwitcherButtonHeight);
			if (USizeBoxSlot* TextSlot = Cast<USizeBoxSlot>(ButtonSizeBox->SetContent(Text)))
			{
				TextSlot->SetHorizontalAlignment(HAlign_Center);
				TextSlot->SetVerticalAlignment(VAlign_Center);
			}
			Button->SetContent(ButtonSizeBox);
			UHorizontalBoxSlot* RowSlot = SwitcherRow->AddChildToHorizontalBox(Button);
			RowSlot->SetVerticalAlignment(VAlign_Center);
			RowSlot->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
			return Button;
		};

		Switch2DButton = AddSwitcherButton(TEXT("Btn_Switch2D"), TEXT("2D"));
		Switch2DButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnSwitch2DClicked);
		Switch3DButton = AddSwitcherButton(TEXT("Btn_Switch3D"), TEXT("3D"));
		Switch3DButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnSwitch3DClicked);

		// Only shown while in 3D preview (see HandlePreviewModeChanged).
		ResetView3DButton = AddSwitcherButton(TEXT("Btn_ResetView3D"), TEXT("Reset"));
		ResetView3DButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnResetView3DClicked);
		ResetView3DButton->SetToolTipText(FText::FromString(TEXT("Reset View")));
		ResetView3DButton->SetVisibility(ESlateVisibility::Collapsed);

		DecreaseSpeedButton = AddSwitcherButton(TEXT("Btn_DecreaseSpeed"), TEXT("-"));
		DecreaseSpeedButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnDecreaseSpeedClicked);
		DecreaseSpeedButton->SetToolTipText(FText::FromString(TEXT("Decrease Move Speed")));
		DecreaseSpeedButton->SetVisibility(ESlateVisibility::Collapsed);

		IncreaseSpeedButton = AddSwitcherButton(TEXT("Btn_IncreaseSpeed"), TEXT("+"));
		IncreaseSpeedButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnIncreaseSpeedClicked);
		IncreaseSpeedButton->SetToolTipText(FText::FromString(TEXT("Increase Move Speed")));
		IncreaseSpeedButton->SetVisibility(ESlateVisibility::Collapsed);

		// "View"/"Edit" toggle - starts as "View" (yellow), matching PC's own default (see
		// ANavPlayerController::bIsIn3DEditMode). AddSwitcherButton doesn't hand back its inner
		// UTextBlock (every other button built with it has a fixed label), so this one reaches it back
		// out through the widget tree instead, same "don't return every intermediate" convention used
		// elsewhere in this function (e.g. the File/Window dropdowns).
		Edit3DModeToggleButton = AddSwitcherButton(TEXT("Btn_Edit3DModeToggle"), TEXT("View"));
		Edit3DModeToggleButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnEdit3DModeToggleClicked);
		Edit3DModeToggleButton->SetBackgroundColor(FLinearColor(0.8f, 0.7f, 0.1f, 1.f));
		Edit3DModeToggleButton->SetVisibility(ESlateVisibility::Collapsed);
		if (USizeBox* Edit3DModeSizeBox = Cast<USizeBox>(Edit3DModeToggleButton->GetContent()))
		{
			Edit3DModeToggleButtonText = Cast<UTextBlock>(Edit3DModeSizeBox->GetContent());
		}

		// Thin separator lines at the seam between the menu bar and the switcher bar, and again below
		// the switcher bar - both bars (and the panels below) share the same dark background color, so
		// without these the eye can't tell where one ends and the next begins.
		constexpr float SeparatorLineHeight = 2.f;
		auto AddHorizontalSeparator = [this, Canvas, SeparatorLineHeight](float PositionY)
		{
			UBorder* Separator = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Separator->SetBrushColor(FLinearColor(1.f, 1.f, 1.f, 0.5f));
			UCanvasPanelSlot* SeparatorSlot = Canvas->AddChildToCanvas(Separator);
			SeparatorSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
			SeparatorSlot->SetAlignment(FVector2D(0.f, 0.f));
			SeparatorSlot->SetOffsets(FMargin(0.f, PositionY, 0.f, SeparatorLineHeight));
			SeparatorSlot->SetZOrder(6);
		};
		AddHorizontalSeparator(MenuBarHeight - SeparatorLineHeight);
		AddHorizontalSeparator(SidebarTopMargin - SeparatorLineHeight);

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
		ApplyStandardFont(PropertiesTitleText);
		UVerticalBoxSlot* PropertiesTitleSlot = PropertiesStack->AddChildToVerticalBox(WrapLabelToPanelWidth(PropertiesTitleText, TEXT("Txt_PropertiesTitleSize")));
		PropertiesTitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		// -- Door fields --
		DoorFieldsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DoorFieldsBox"));
		DoorFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
		PropertiesStack->AddChildToVerticalBox(DoorFieldsBox);

		DoorRoomText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_DoorRoom"));
		DoorRoomText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(DoorRoomText);
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
		ApplyStandardFont(ConnectsToLabel);
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
		ApplyStandardFont(RoomNameLabel);
		UHorizontalBoxSlot* RoomNameLabelSlot = RoomNameRow->AddChildToHorizontalBox(RoomNameLabel);
		RoomNameLabelSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		RoomNameLabelSlot->SetVerticalAlignment(VAlign_Center);

		RoomNameTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_RoomName"));
		ApplyStandardFont(RoomNameTextBox);
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
		ApplyStandardFont(ConnectorTypeText);
		UVerticalBoxSlot* ConnectorTypeSlot = ConnectorFieldsBox->AddChildToVerticalBox(WrapLabelToPanelWidth(ConnectorTypeText, TEXT("Txt_ConnectorTypeSize")));
		ConnectorTypeSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		UTextBlock* FloorsLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_FloorsLabel"));
		FloorsLabel->SetText(FText::FromString(TEXT("Floors :")));
		FloorsLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(FloorsLabel);
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
			ApplyStandardFont(FloorLabel);
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
		ApplyStandardFont(LayersTitle);
		UVerticalBoxSlot* LayersTitleSlot = LayersStack->AddChildToVerticalBox(LayersTitle);
		LayersTitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		LayersSearchBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_LayersSearch"));
		LayersSearchBox->SetHintText(FText::FromString(TEXT("Search")));
		ApplyStandardFont(LayersSearchBox);
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

		// -- Navigation panel: presentation layer for ws/jh's UNavigationPanelWidget (route selection/
		// planning logic lives there, reached via ANavPlayerController::GetNavigationPanel() - see the
		// comment above OnNavSetStartClicked). Styled to match this screen's own panels instead of its
		// original bespoke draggable-overlay look; RefreshNavigationPanelDisplay fills in the dynamic
		// bits (button colors/labels/enablement, status/route text) once NativeConstruct hooks it up.
		NavigationPanel = ConstructDockPanel(EDockPanelId::Navigation, TEXT("Pnl_Navigation"), DockPanelColor, 14.f);
		NavigationPanel->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnNavigationPanelMouseButtonDown);
		NavigationPanel->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnNavigationPanelMouseMove);
		NavigationPanel->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnNavigationPanelMouseButtonUp);
		DockResizeHandles[(int32)EDockPanelId::Navigation]->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnNavigationResizeMouseButtonDown);
		DockResizeHandles[(int32)EDockPanelId::Navigation]->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnNavigationResizeMouseMove);
		DockResizeHandles[(int32)EDockPanelId::Navigation]->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnNavigationResizeMouseButtonUp);
		DockLeftResizeHandles[(int32)EDockPanelId::Navigation]->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnNavigationLeftResizeMouseButtonDown);
		DockLeftResizeHandles[(int32)EDockPanelId::Navigation]->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnNavigationLeftResizeMouseMove);
		DockLeftResizeHandles[(int32)EDockPanelId::Navigation]->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnNavigationLeftResizeMouseButtonUp);

		UVerticalBox* NavStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NavStack"));
		NavigationPanel->SetContent(NavStack);

		UTextBlock* NavTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_NavTitle"));
		NavTitle->SetText(FText::FromString(TEXT("Navigation")));
		NavTitle->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(NavTitle);
		UVerticalBoxSlot* NavTitleSlot = NavStack->AddChildToVerticalBox(NavTitle);
		NavTitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		NavContextText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_NavContext"));
		NavContextText->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.78f, 0.88f, 1.f)));
		ApplyStandardTextWrap(NavContextText);
		UVerticalBoxSlot* NavContextSlot = NavStack->AddChildToVerticalBox(NavContextText);
		NavContextSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

		// Room dropdowns - an alternative to clicking a point on the floor plan (see the comment
		// above HandleNavStartRoomChanged). Populated with every real room across every floor, so
		// picking one can jump straight to a room on a different floor than the one currently shown.
		auto AddNavRoomDropdown = [this, NavStack](const TCHAR* Name, const TCHAR* LabelText) -> UComboBoxString*
		{
			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString(Name) + TEXT("Label")));
			Label->SetText(FText::FromString(FString(LabelText)));
			Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(Label);
			UVerticalBoxSlot* LabelSlot = NavStack->AddChildToVerticalBox(Label);
			LabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

			UComboBoxString* ComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), FName(Name));
			ComboBox->OnGenerateWidgetEvent.BindDynamic(this, &UInteriorScreenWidget::HandleGenerateComboBoxItem);
			USizeBox* ComboSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			ComboSizeBox->SetWidthOverride(StandardButtonWidth);
			ComboSizeBox->SetContent(ComboBox);
			UVerticalBoxSlot* ComboSlot = NavStack->AddChildToVerticalBox(ComboSizeBox);
			ComboSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
			return ComboBox;
		};

		NavStartRoomComboBox = AddNavRoomDropdown(TEXT("Cbx_NavStartRoom"), TEXT("Start Room"));
		NavStartRoomComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleNavStartRoomChanged);
		NavDestinationRoomComboBox = AddNavRoomDropdown(TEXT("Cbx_NavDestinationRoom"), TEXT("Destination Room"));
		NavDestinationRoomComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleNavDestinationRoomChanged);

		auto AddNavButton = [this, NavStack, ApplyStandardTextWrap](const TCHAR* Name, const TCHAR* Label, UTextBlock*& OutLabel) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
			Button->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString(Name) + TEXT("Label")));
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetJustification(ETextJustify::Center);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardTextWrap(Text);
			USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			SizeBox->SetWidthOverride(StandardButtonWidth);
			SizeBox->SetMinDesiredHeight(StandardButtonHeight);
			SizeBox->SetContent(Text);
			Button->SetContent(SizeBox);
			UVerticalBoxSlot* ButtonSlot = NavStack->AddChildToVerticalBox(Button);
			ButtonSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
			OutLabel = Text;
			return Button;
		};

		UTextBlock* UnusedFindRouteLabel = nullptr;
		UTextBlock* UnusedClearRouteLabel = nullptr;
		NavSetStartButton = AddNavButton(TEXT("Btn_NavSetStart"), TEXT("Set Start"), NavSetStartButtonLabel);
		NavSetStartButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnNavSetStartClicked);
		NavSetDestinationButton = AddNavButton(TEXT("Btn_NavSetDestination"), TEXT("Set Destination"), NavSetDestinationButtonLabel);
		NavSetDestinationButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnNavSetDestinationClicked);
		NavFindRouteButton = AddNavButton(TEXT("Btn_NavFindRoute"), TEXT("Find Route"), UnusedFindRouteLabel);
		NavFindRouteButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnNavFindRouteClicked);
		NavClearRouteButton = AddNavButton(TEXT("Btn_NavClearRoute"), TEXT("Clear Route"), UnusedClearRouteLabel);
		NavClearRouteButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnNavClearRouteClicked);

		NavStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_NavStatus"));
		ApplyStandardTextWrap(NavStatusText);
		UVerticalBoxSlot* NavStatusSlot = NavStack->AddChildToVerticalBox(NavStatusText);
		NavStatusSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		NavRouteDetailsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_NavRouteDetails"));
		NavRouteDetailsText->SetColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.92f, 1.f, 1.f)));
		ApplyStandardTextWrap(NavRouteDetailsText);
		NavStack->AddChildToVerticalBox(NavRouteDetailsText);

		// -- Audio panel: reads InNavi/Data/Audio/ and lists every file as its own row with a Play
		// button (see RefreshAudioList); Stop All halts everything at once
		// (ANavPlayerController::StopAllAudio). Starts hidden (bDockPanelUserHidden, set in
		// InitializeDockRuntimeArrays) - shown via the Window menu, same mechanism as hiding any other
		// panel, just inverted for its starting state.
		AudioPanel = ConstructDockPanel(EDockPanelId::Audio, TEXT("Pnl_Audio"), DockPanelColor, 14.f);
		AudioPanel->SetVisibility(ESlateVisibility::Collapsed);
		AudioPanel->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnAudioPanelMouseButtonDown);
		AudioPanel->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnAudioPanelMouseMove);
		AudioPanel->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnAudioPanelMouseButtonUp);
		DockResizeHandles[(int32)EDockPanelId::Audio]->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnAudioResizeMouseButtonDown);
		DockResizeHandles[(int32)EDockPanelId::Audio]->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnAudioResizeMouseMove);
		DockResizeHandles[(int32)EDockPanelId::Audio]->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnAudioResizeMouseButtonUp);
		DockLeftResizeHandles[(int32)EDockPanelId::Audio]->OnMouseButtonDownEvent.BindDynamic(this, &UInteriorScreenWidget::OnAudioLeftResizeMouseButtonDown);
		DockLeftResizeHandles[(int32)EDockPanelId::Audio]->OnMouseMoveEvent.BindDynamic(this, &UInteriorScreenWidget::OnAudioLeftResizeMouseMove);
		DockLeftResizeHandles[(int32)EDockPanelId::Audio]->OnMouseButtonUpEvent.BindDynamic(this, &UInteriorScreenWidget::OnAudioLeftResizeMouseButtonUp);

		UVerticalBox* AudioStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AudioStack"));
		AudioPanel->SetContent(AudioStack);

		UTextBlock* AudioTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_AudioTitle"));
		AudioTitle->SetText(FText::FromString(TEXT("Audio")));
		AudioTitle->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(AudioTitle);
		UVerticalBoxSlot* AudioTitleSlot = AudioStack->AddChildToVerticalBox(AudioTitle);
		AudioTitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		AudioStopAllButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_AudioStopAll"));
		AudioStopAllButton->SetBackgroundColor(FLinearColor(0.75f, 0.2f, 0.2f, 1.f));
		AudioStopAllButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAudioStopAllClicked);
		AudioStopAllButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
		UTextBlock* AudioStopAllText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_AudioStopAll"));
		AudioStopAllText->SetText(FText::FromString(TEXT("Stop All")));
		AudioStopAllText->SetJustification(ETextJustify::Center);
		AudioStopAllText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardTextWrap(AudioStopAllText);
		USizeBox* AudioStopAllSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size_AudioStopAll"));
		AudioStopAllSizeBox->SetWidthOverride(StandardButtonWidth);
		AudioStopAllSizeBox->SetMinDesiredHeight(StandardButtonHeight);
		AudioStopAllSizeBox->SetContent(AudioStopAllText);
		AudioStopAllButton->SetContent(AudioStopAllSizeBox);
		UVerticalBoxSlot* AudioStopAllSlot = AudioStack->AddChildToVerticalBox(AudioStopAllButton);
		AudioStopAllSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		// Lets the user mute each UI sound at once (see ANavPlayerController::SetButtonClickSoundEnabled/
		// SetBuildingSelectSoundEnabled/SetQuitConfirmSoundEnabled) without touching the Audio files
		// below - own rows, not UAudioFileRowWidgets, since these are settings rather than playable
		// files.
		auto AddAudioCheckRow = [this, AudioStack](const TCHAR* Name, const TCHAR* Label, bool bDefaultChecked) -> UCheckBox*
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			UCheckBox* CheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), FName(Name));
			CheckBox->SetIsChecked(bDefaultChecked);
			UHorizontalBoxSlot* CheckSlot = Row->AddChildToHorizontalBox(CheckBox);
			CheckSlot->SetVerticalAlignment(VAlign_Center);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(Text);
			UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
			TextSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
			TextSlot->SetVerticalAlignment(VAlign_Center);
			UVerticalBoxSlot* RowSlot = AudioStack->AddChildToVerticalBox(Row);
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
			return CheckBox;
		};

		AudioClickSoundCheckbox = AddAudioCheckRow(TEXT("Chk_AudioClickSound"), TEXT("Button Click Sound"), true);
		AudioClickSoundCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAudioClickSoundCheckChanged);

		AudioSelectSoundCheckbox = AddAudioCheckRow(TEXT("Chk_AudioSelectSound"), TEXT("Building Select Sound"), true);
		AudioSelectSoundCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAudioSelectSoundCheckChanged);

		AudioQuitConfirmSoundCheckbox = AddAudioCheckRow(TEXT("Chk_AudioQuitConfirmSound"), TEXT("Confirm Dialog Sound"), true);
		AudioQuitConfirmSoundCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAudioQuitConfirmSoundCheckChanged);

		// Checked = everything muted (opposite sense from the three rows above) - unchecked by default,
		// matching nothing being muted at first.
		AudioMuteAllCheckbox = AddAudioCheckRow(TEXT("Chk_AudioMuteAll"), TEXT("Mute All"), false);
		AudioMuteAllCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAudioMuteAllCheckChanged);

		// Volume slider - ANavPlayerController::MasterVolume, applied to every sound this class plays.
		UHorizontalBox* AudioVolumeRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("AudioVolumeRow"));
		UTextBlock* AudioVolumeLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_AudioVolume"));
		AudioVolumeLabel->SetText(FText::FromString(TEXT("Volume")));
		AudioVolumeLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(AudioVolumeLabel);
		UHorizontalBoxSlot* AudioVolumeLabelSlot = AudioVolumeRow->AddChildToHorizontalBox(AudioVolumeLabel);
		AudioVolumeLabelSlot->SetVerticalAlignment(VAlign_Center);
		AudioVolumeLabelSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

		AudioVolumeSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("Slider_AudioVolume"));
		AudioVolumeSlider->SetMinValue(0.f);
		AudioVolumeSlider->SetMaxValue(1.f);
		AudioVolumeSlider->SetValue(1.f);
		AudioVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAudioVolumeChanged);
		USizeBox* AudioVolumeSliderSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		AudioVolumeSliderSizeBox->SetWidthOverride(StandardButtonWidth - 60.f);
		AudioVolumeSliderSizeBox->SetContent(AudioVolumeSlider);
		UHorizontalBoxSlot* AudioVolumeSliderSlot = AudioVolumeRow->AddChildToHorizontalBox(AudioVolumeSliderSizeBox);
		AudioVolumeSliderSlot->SetVerticalAlignment(VAlign_Center);
		UVerticalBoxSlot* AudioVolumeRowSlot = AudioStack->AddChildToVerticalBox(AudioVolumeRow);
		AudioVolumeRowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		// Rows themselves are built by RefreshAudioList (one UAudioFileRowWidget per file under
		// Data/Audio/), same reasoning as FloorListBox/LayersListBox not being laid out here.
		AudioFileListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("AudioFileListBox"));
		USizeBox* AudioFileListSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("AudioFileListSizeBox"));
		AudioFileListSizeBox->SetHeightOverride(LayersListDefaultHeight);
		AudioFileListSizeBox->SetWidthOverride(StandardButtonWidth);
		AudioFileListSizeBox->SetContent(AudioFileListBox);
		AudioStack->AddChildToVerticalBox(AudioFileListSizeBox);
	}

	return Super::RebuildWidget();
}

void UInteriorScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BackButton)
	{
		BackButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnBackClicked);
		BackButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
	}
	if (AddFloorButton)
	{
		AddFloorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddFloorClicked);
		AddFloorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
	}
	if (RemoveFloorButton)
	{
		RemoveFloorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRemoveFloorClicked);
		RemoveFloorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
	}
	if (ApplyDoorButton)
	{
		ApplyDoorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnApplyDoorClicked);
		ApplyDoorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
	}
	if (ApplyRoomButton)
	{
		ApplyRoomButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnApplyRoomClicked);
		ApplyRoomButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
	}
	if (ApplyConnectorButton)
	{
		ApplyConnectorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnApplyConnectorClicked);
		ApplyConnectorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
	}
	if (EditBuildingNameButton)
	{
		EditBuildingNameButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnEditBuildingNameClicked);
		EditBuildingNameButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
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
		// Rooms are created/deleted/renamed via floor-plan clicks and the Properties panel, neither of
		// which goes through this screen's own handlers either.
		PC->OnInteriorContentChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleInteriorContentChanged);
		// 3D preview mode can in principle be left/entered from something other than this screen's own
		// Switch2D/3D buttons (defensive symmetry, matching every other PC-state delegate above) - keep
		// Background/FloorPlanWidget/the toggle buttons in sync regardless of the trigger.
		PC->OnPreviewModeChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandlePreviewModeChanged);
		HandlePreviewModeChanged(PC->IsIn3DPreviewMode());
		PC->OnEdit3DModeChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleEdit3DModeChanged);
		HandleEdit3DModeChanged(PC->IsIn3DEditMode());

		// Navigation panel: hand ws/jh's UNavigationPanelWidget this screen's own Navigation Border to
		// hit-test against (see SetPanelBorderForHitTest's own comment) and subscribe to its
		// OnStateChanged so RefreshNavigationPanelDisplay repaints whenever selection/route state
		// changes, from any trigger (this panel's own buttons, or a floor-plan click).
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->SetPanelBorderForHitTest(NavigationPanel);
			Nav->OnStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleNavigationStateChanged);
		}

		// bButtonClickSoundEnabled (and the two flags below) live on the PC, not this widget, so they
		// survive this screen being torn down and rebuilt (e.g. Back to the start screen and in again) -
		// these checkboxes were hardcoded to SetIsChecked(true/false) at construction (RebuildWidget has
		// no reliable PC access yet), which could show stale state even after the user had changed the
		// real setting. Sync them to the actual state now that PC is available. Mute All has no backing
		// flag of its own (see SetAllUiSoundsEnabled's own comment) so there's nothing to sync it to
		// here - it always starts unchecked.
		if (AudioClickSoundCheckbox)
		{
			AudioClickSoundCheckbox->SetIsChecked(PC->IsButtonClickSoundEnabled());
		}
		if (AudioSelectSoundCheckbox)
		{
			AudioSelectSoundCheckbox->SetIsChecked(PC->IsBuildingSelectSoundEnabled());
		}
		if (AudioQuitConfirmSoundCheckbox)
		{
			AudioQuitConfirmSoundCheckbox->SetIsChecked(PC->IsQuitConfirmSoundEnabled());
		}
		if (AudioVolumeSlider)
		{
			AudioVolumeSlider->SetValue(PC->GetMasterVolume());
		}
	}

	RefreshBuildingSwitcher();
	RefreshFloorList();
	RefreshLayersList();
	RefreshNavigationPanelDisplay();
	RefreshNavRoomDropdowns();
	RefreshAudioList();
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
		PC->OnInteriorContentChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleInteriorContentChanged);
		PC->OnPreviewModeChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandlePreviewModeChanged);
		PC->OnEdit3DModeChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleEdit3DModeChanged);

		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->OnStateChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleNavigationStateChanged);
			Nav->SetPanelBorderForHitTest(nullptr);
		}
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

	// Keeps retrying every tick, not just once - RootCanvas's cached geometry (what
	// ApplyLayoutSaveData actually needs, see GetReliableCanvasSize's own comment) turned out to
	// still not be valid a tick or two after construction in practice, not just on the literal first
	// call, so a single one-shot retry (the previous approach) wasn't reliably enough. Cheap to poll -
	// ApplyLayoutSaveData returns immediately without doing any work once RootCanvas isn't ready.
	if (PendingSavedLayoutToReapply.IsValid() && ApplyLayoutSaveData(PendingSavedLayoutToReapply))
	{
		PendingSavedLayoutToReapply.Reset();
	}

	if (PropertiesFractionRecalcFramesRemaining > 0)
	{
		--PropertiesFractionRecalcFramesRemaining;
		RecomputeColumnFractionsFromNaturalSizes(DockPanelColumnIndex[(int32)EDockPanelId::Properties]);
		RefreshDockLayout();
	}

	if (SplitterSettleFramesRemaining > 0)
	{
		--SplitterSettleFramesRemaining;
		RefreshDockLayout();
	}
}

FReply UInteriorScreenWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const bool bOverAnyPanel =
		(ToolbarPanel && ToolbarPanel->IsHovered()) ||
		(FloorPanel && FloorPanel->IsHovered()) ||
		(PropertiesPanel && PropertiesPanel->IsHovered()) ||
		(LayersPanel && LayersPanel->IsHovered()) ||
		(NavigationPanel && NavigationPanel->IsHovered());

	if (bOverAnyPanel)
	{
		// Scrollable children (FloorListBox, the Layers list) already got first crack at this via
		// Slate's normal event bubbling, before it reached here - this just stops whatever they didn't
		// consume from falling through to the game viewport and triggering PC's 3D preview zoom.
		return FReply::Handled();
	}

	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

// ---------------------------------------------------------------------------
// Docking system
// ---------------------------------------------------------------------------

FVector2D UInteriorScreenWidget::GetCanvasSize() const
{
	FVector2D ReliableSize;
	if (GetReliableCanvasSize(ReliableSize))
	{
		return ReliableSize;
	}
	return UWidgetLayoutLibrary::GetViewportSize(this);
}

bool UInteriorScreenWidget::GetReliableCanvasSize(FVector2D& OutSize) const
{
	if (!RootCanvas)
	{
		return false;
	}
	const FVector2D LocalSize = RootCanvas->GetCachedGeometry().GetLocalSize();
	if (LocalSize.X <= 0.f || LocalSize.Y <= 0.f)
	{
		return false;
	}
	OutSize = LocalSize;
	return true;
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
	case EDockPanelId::Navigation:
		// Right side, lower than Floor/Properties' own top-right default spot, so a fresh install
		// doesn't start all 3 stacked on top of each other.
		return FVector2D(FMath::Max(10.f, ViewportSize.X - DockPanelWidths[(int32)EDockPanelId::Navigation] - 20.f), ViewportSize.Y * 0.55f);
	case EDockPanelId::Audio:
		// Below Layers on the left side - starts hidden anyway (see bDockPanelUserHidden), this only
		// matters for the very first time the user checks it in the Window menu.
		return FVector2D(10.f, ViewportSize.Y * 0.75f);
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
		// The building switcher used to be part of this panel's own hit-test list - it's now its own
		// fixed bar outside the dock system entirely, so it's no longer relevant here.
		Widgets = { SelectModeButton, DrawRoomModeButton, DrawWallModeButton, AddRectModeButton,
			AddTriangleModeButton, AddCircleModeButton, AddDoorModeButton, AddStairsModeButton,
			AddElevatorModeButton, AddEscalatorModeButton, GrowRoomButton, ShrinkRoomButton,
			RotateCCWButton, RotateCWButton };
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
	case EDockPanelId::Navigation:
		Widgets = { NavSetStartButton, NavSetDestinationButton, NavFindRouteButton, NavClearRouteButton,
			NavStartRoomComboBox, NavDestinationRoomComboBox };
		break;
	case EDockPanelId::Audio:
		Widgets = { AudioStopAllButton, AudioClickSoundCheckbox, AudioSelectSoundCheckbox,
			AudioQuitConfirmSoundCheckbox, AudioMuteAllCheckbox, AudioVolumeSlider, AudioFileListBox };
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

		// See SplitterSettleFramesRemaining's own comment - Other's Border (e.g. Layers, when
		// Properties is the one joining) just had its height overridden smaller by the RefreshDockLayout
		// call inside SetPanelWidth above, and RefreshSplitterLayout (called in that same pass) already
		// tried to position the new splitter off Other's GetPanelSize - but that still reads whatever
		// Slate last actually painted, not the just-set override, so the splitter lands off Other's OLD
		// size. Keep re-running RefreshDockLayout for the next several ticks until Other's geometry has
		// actually settled to its new size.
		SplitterSettleFramesRemaining = SplitterSettleFrameCount;
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

		// Each panel's own DockPanelMinHeight floor is applied per-panel below, independent of the
		// others - if a splitter drag (or the viewport itself shrinking after fractions were set)
		// leaves several panels below their proportional share at once, every one of them gets pushed
		// up to its own floor and their SUM can exceed what the column actually has room for, pushing
		// the column's later panels past the bottom of the visible screen. Compute the raw heights
		// first and shrink them all proportionally if that happened, so a merged column always stays
		// fully on-screen even when there isn't room for every member's own minimum at once.
		TArray<float> PanelHeights;
		PanelHeights.SetNumZeroed(Column.Panels.Num());
		float TotalPanelHeight = 0.f;
		for (int32 i = 0; i < Column.Panels.Num(); ++i)
		{
			const EDockPanelId PanelId = Column.Panels[i];
			const UBorder* VisBorder = DockPanelBorders[(int32)PanelId];
			if (!VisBorder || VisBorder->GetVisibility() == ESlateVisibility::Collapsed)
			{
				continue;
			}
			PanelHeights[i] = bSplitColumn
				? FMath::Max(DockPanelMinHeight, ColumnTotalHeight * Column.HeightFractions[i] / VisibleFractionSum)
				: FMath::Max(DockPanelMinHeight, GetPanelSize(PanelId).Y);
			TotalPanelHeight += PanelHeights[i];
		}
		if (bSplitColumn && TotalPanelHeight > ColumnTotalHeight)
		{
			const float ShrinkFactor = ColumnTotalHeight / TotalPanelHeight;
			for (float& Height : PanelHeights)
			{
				Height *= ShrinkFactor;
			}
		}

		float RunningY = SidebarTopMargin;
		for (int32 i = 0; i < Column.Panels.Num(); ++i)
		{
			const EDockPanelId PanelId = Column.Panels[i];
			const int32 Index = (int32)PanelId;
			UBorder* Border = DockPanelBorders[Index];
			const bool bPanelVisible = Border && Border->GetVisibility() != ESlateVisibility::Collapsed;
			const float PanelHeight = PanelHeights[i];

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
	case EDockPanelId::Navigation: return TEXT("Navigation");
	case EDockPanelId::Audio:      return TEXT("Audio");
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

	// LayoutVersion 2: every x/y/width/Column.X below is saved as a FRACTION of the viewport size at
	// save time (x/width relative to ViewportSize.X, y relative to ViewportSize.Y), not raw pixels -
	// version 1 saved raw pixels, so a layout arranged on one screen and reopened on a different
	// resolution/aspect ratio (e.g. desktop -> laptop) restored panels at the literal same pixel
	// coordinates, which could land off-screen or badly overlapping on a smaller/differently-shaped
	// viewport. ApplyLayoutSaveData/LoadDockLayoutFromDisk key off this field to tell an old-format
	// (unversioned) save apart from this one - an old save's raw pixels would otherwise be
	// misinterpreted as fractions (e.g. a saved x of 1850 read as "1850x the viewport width").
	Root->SetNumberField(TEXT("layoutVersion"), 2);

	// GetReliableCanvasSize, not GetCanvasSize - this specifically must not silently fall back to
	// GetViewportSize's different coordinate system (see GetReliableCanvasSize's own comment); saving
	// is always user-triggered well after this screen has painted, so RootCanvas's geometry should
	// always be valid by then, but if it somehow isn't, skipping the save (matching a version-1 file's
	// own shape) is far safer than silently writing fractions in the wrong basis.
	FVector2D ViewportSize;
	if (!GetReliableCanvasSize(ViewportSize))
	{
		return Root;
	}

	TSharedPtr<FJsonObject> PanelsObject = MakeShared<FJsonObject>();
	for (int32 Index = 0; Index < NumDockPanels; ++Index)
	{
		const FVector2D Position = GetPanelPosition((EDockPanelId)Index);
		TSharedPtr<FJsonObject> PanelObject = MakeShared<FJsonObject>();
		PanelObject->SetNumberField(TEXT("x"), Position.X / ViewportSize.X);
		PanelObject->SetNumberField(TEXT("y"), Position.Y / ViewportSize.Y);
		PanelObject->SetNumberField(TEXT("width"), DockPanelWidths[Index] / ViewportSize.X);
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
		ColumnObject->SetNumberField(TEXT("x"), Column.X / ViewportSize.X);
		ColumnsArray.Add(MakeShared<FJsonValueObject>(ColumnObject));
	}
	Root->SetArrayField(TEXT("columns"), ColumnsArray);

	return Root;
}

bool UInteriorScreenWidget::ApplyLayoutSaveData(const TSharedPtr<FJsonObject>& Root)
{
	if (!Root.IsValid())
	{
		return true; // nothing to apply, and nothing worth retrying later either
	}

	// GetReliableCanvasSize, not GetCanvasSize - see this function's own header comment for why the
	// fallback specifically breaks this round trip. Returning false here (instead of falling back)
	// is what lets the caller retry on a later tick instead of applying positions computed in the
	// wrong coordinate system.
	FVector2D ViewportSize;
	if (!GetReliableCanvasSize(ViewportSize))
	{
		return false;
	}

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

		double XFraction = 0.0, YFraction = 0.0, WidthFraction = SidebarMinWidth / ViewportSize.X;
		(*PanelObject)->TryGetNumberField(TEXT("x"), XFraction);
		(*PanelObject)->TryGetNumberField(TEXT("y"), YFraction);
		(*PanelObject)->TryGetNumberField(TEXT("width"), WidthFraction);

		// Fractions of the CURRENT viewport size (see BuildLayoutSaveData's own comment) - this is what
		// actually makes a layout saved on one screen reopen sensibly on a different resolution/aspect
		// ratio, instead of landing at the same raw pixel coordinates.
		const float Y = (float)(YFraction * ViewportSize.Y);
		const float ClampedWidth = FMath::Clamp((float)(WidthFraction * ViewportSize.X), SidebarMinWidth, SidebarMaxWidth);

		// XFraction and WidthFraction alone reproduce the exact same right-edge position on any
		// screen (X/Vp + Width/Vp is scale-invariant) - UNLESS ClampedWidth above actually changed the
		// width from what WidthFraction*ViewportSize.X would've been (e.g. the same proportional width
		// that fit on a laptop exceeds SidebarMaxWidth on a much wider desktop monitor and gets clamped
		// down), which breaks that invariant and opens a gap between the panel and the screen edge it
		// was touching. IsRightEdgeAnchored recognizes "was touching (or within a hair of) the right
		// edge when saved" from the fractions alone, and recomputes X from the CURRENT ClampedWidth so
		// it stays flush - the usual case (no clamping needed) leaves X unchanged either way, since
		// then ViewportSize.X - ClampedWidth already equals XFraction*ViewportSize.X exactly.
		constexpr double RightEdgeFractionEpsilon = 0.03;
		const bool bIsRightEdgeAnchored = (XFraction + WidthFraction) >= (1.0 - RightEdgeFractionEpsilon);
		const float X = bIsRightEdgeAnchored ? (ViewportSize.X - ClampedWidth) : (float)(XFraction * ViewportSize.X);

		if (UCanvasPanelSlot* PanelSlot = DockPanelCanvasSlots[Index])
		{
			// Clamped to stay reachable within the CURRENT viewport regardless of how different its
			// aspect ratio is from the one this was saved under (fractional scaling alone narrows the
			// gap but doesn't guarantee it for an extreme ratio change, e.g. ultrawide -> 4:3) -
			// MinVisiblePanelMargin is how much of the panel must stay on-screen so it's always
			// draggable back into full view. Also keeps a floating panel below the menu bar, same as
			// before (a layout saved before the menu bar existed can have a Y above it) - docked panels
			// get repositioned by RefreshDockLayout right after this anyway, so this only actually
			// matters for floating ones.
			constexpr float MinVisiblePanelMargin = 40.f;
			const float ClampedX = FMath::Clamp(X, 0.f, FMath::Max(0.f, ViewportSize.X - MinVisiblePanelMargin));
			const float ClampedY = FMath::Clamp(Y, SidebarTopMargin, FMath::Max(SidebarTopMargin, ViewportSize.Y - MinVisiblePanelMargin));
			PanelSlot->SetPosition(FVector2D(ClampedX, ClampedY));
		}
		DockPanelWidths[Index] = ClampedWidth;
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

			double ColumnXFraction = 0.0;
			(*ColumnObject)->TryGetNumberField(TEXT("x"), ColumnXFraction);

			// Same right-edge-anchor reasoning as the floating panel X above (see its own comment) -
			// the column's rendered width is its first member's (all members share one width via
			// SetPanelWidth's sibling-sync), already resolved to its final clamped pixel value by the
			// per-panel loop above. Re-reads that same member's saved width fraction here (not carried
			// over from that loop, which is scoped per-panel) purely to test whether the column was
			// flush against the right edge when saved.
			float ColumnWidth = SidebarMinWidth;
			bool bColumnIsRightEdgeAnchored = false;
			if (NewColumn.Panels.Num() > 0)
			{
				const EDockPanelId FirstMemberId = NewColumn.Panels[0];
				ColumnWidth = DockPanelWidths[(int32)FirstMemberId];

				double MemberWidthFraction = 0.0;
				const TSharedPtr<FJsonObject>* MemberPanelObject = nullptr;
				if (bHasPanelsObject && (*PanelsObject)->TryGetObjectField(GetDockPanelName(FirstMemberId), MemberPanelObject))
				{
					(*MemberPanelObject)->TryGetNumberField(TEXT("width"), MemberWidthFraction);
				}
				constexpr double RightEdgeFractionEpsilon = 0.03;
				bColumnIsRightEdgeAnchored = (ColumnXFraction + MemberWidthFraction) >= (1.0 - RightEdgeFractionEpsilon);
			}
			const float ColumnX = bColumnIsRightEdgeAnchored ? (ViewportSize.X - ColumnWidth) : (float)(ColumnXFraction * ViewportSize.X);

			// Same on-screen clamp as the floating panel X above - RefreshDockLayout applies this
			// directly to every panel docked in the column with no clamping of its own.
			constexpr float MinVisibleColumnMargin = 40.f;
			NewColumn.X = FMath::Clamp(ColumnX, 0.f, FMath::Max(0.f, ViewportSize.X - MinVisibleColumnMargin));

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
	return true;
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

	// A file saved before layoutVersion 2 (see BuildLayoutSaveData's own comment) stored raw pixel
	// positions instead of viewport-relative fractions - reinterpreting those old raw values as
	// fractions here would be actively wrong (e.g. a saved x of 1850 read as "1850x the viewport
	// width"), not just imprecise. Treating it as if nothing were saved and falling back to fresh,
	// resolution-aware defaults (see InitializeDefaultDockLayout's own caller) is the safe choice, and
	// the layout gets re-saved in the new format the next time it changes anyway.
	double LayoutVersion = 0.0;
	if (!Root->TryGetNumberField(TEXT("layoutVersion"), LayoutVersion) || LayoutVersion < 2.0)
	{
		return false;
	}

	// Called this early (from NativeConstruct), RootCanvas's cached geometry is essentially never
	// valid yet in practice, so this first attempt is expected to return false - held onto so
	// NativeTick can keep retrying every tick until GetReliableCanvasSize actually succeeds (see
	// PendingSavedLayoutToReapply's own comment). Skipped entirely on the rare chance this first
	// attempt already succeeded - nothing left to retry.
	if (!ApplyLayoutSaveData(Root))
	{
		PendingSavedLayoutToReapply = Root;
	}

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
	// panel's real new edge. Simplest fix: just hide this panel's own handles for the drag's
	// duration - HandlePanelMouseButtonUp restores them once the drop has settled.
	if (UBorder* Handle = DockResizeHandles[Index])
	{
		Handle->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (UBorder* LeftHandle = DockLeftResizeHandles[Index])
	{
		LeftHandle->SetVisibility(ESlateVisibility::Collapsed);
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
	const float ClampedY = FMath::Clamp(NewPosition.Y, SidebarTopMargin, FMath::Max(SidebarTopMargin, ViewportSize.Y - CurrentPanelSize.Y));
	PanelSlot->SetPosition(FVector2D(ClampedX, ClampedY));

	if (UCanvasPanelSlot* HandleSlot = DockResizeHandleSlots[Index])
	{
		HandleSlot->SetPosition(FVector2D(ClampedX + DockPanelWidths[Index], ClampedY));
	}
	if (UCanvasPanelSlot* LeftHandleSlot = DockLeftResizeHandleSlots[Index])
	{
		LeftHandleSlot->SetPosition(FVector2D(ClampedX - SidebarResizeHandleWidth, ClampedY));
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
		// Restore the handles HandlePanelMouseButtonDown hid for the drag - TrySnapPanel already
		// refreshed layout above, so by the next paint they'll be sized/positioned for wherever the
		// panel actually ended up (docked or still floating).
		if (UBorder* Handle = DockResizeHandles[Index])
		{
			Handle->SetVisibility(ESlateVisibility::Visible);
		}
		if (UBorder* LeftHandle = DockLeftResizeHandles[Index])
		{
			LeftHandle->SetVisibility(ESlateVisibility::Visible);
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
FEventReply UInteriorScreenWidget::OnNavigationPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseButtonDown(EDockPanelId::Navigation, MouseEvent); }
FEventReply UInteriorScreenWidget::OnNavigationPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseMove(EDockPanelId::Navigation, MouseEvent); }
FEventReply UInteriorScreenWidget::OnNavigationPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseButtonUp(EDockPanelId::Navigation, MouseEvent); }
FEventReply UInteriorScreenWidget::OnAudioPanelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseButtonDown(EDockPanelId::Audio, MouseEvent); }
FEventReply UInteriorScreenWidget::OnAudioPanelMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseMove(EDockPanelId::Audio, MouseEvent); }
FEventReply UInteriorScreenWidget::OnAudioPanelMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandlePanelMouseButtonUp(EDockPanelId::Audio, MouseEvent); }

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
FEventReply UInteriorScreenWidget::OnNavigationResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseButtonDown(EDockPanelId::Navigation, MouseEvent); }
FEventReply UInteriorScreenWidget::OnNavigationResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseMove(EDockPanelId::Navigation, MouseEvent); }
FEventReply UInteriorScreenWidget::OnNavigationResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseButtonUp(EDockPanelId::Navigation, MouseEvent); }
FEventReply UInteriorScreenWidget::OnAudioResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseButtonDown(EDockPanelId::Audio, MouseEvent); }
FEventReply UInteriorScreenWidget::OnAudioResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseMove(EDockPanelId::Audio, MouseEvent); }
FEventReply UInteriorScreenWidget::OnAudioResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleResizeMouseButtonUp(EDockPanelId::Audio, MouseEvent); }

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
FEventReply UInteriorScreenWidget::OnNavigationLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseButtonDown(EDockPanelId::Navigation, MouseEvent); }
FEventReply UInteriorScreenWidget::OnNavigationLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseMove(EDockPanelId::Navigation, MouseEvent); }
FEventReply UInteriorScreenWidget::OnNavigationLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseButtonUp(EDockPanelId::Navigation, MouseEvent); }
FEventReply UInteriorScreenWidget::OnAudioLeftResizeMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseButtonDown(EDockPanelId::Audio, MouseEvent); }
FEventReply UInteriorScreenWidget::OnAudioLeftResizeMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseMove(EDockPanelId::Audio, MouseEvent); }
FEventReply UInteriorScreenWidget::OnAudioLeftResizeMouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleLeftResizeMouseButtonUp(EDockPanelId::Audio, MouseEvent); }

FEventReply UInteriorScreenWidget::OnSplitter0MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonDown(0, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter0MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseMove(0, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter0MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonUp(0, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter1MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonDown(1, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter1MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseMove(1, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter1MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonUp(1, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter2MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonDown(2, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter2MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseMove(2, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter2MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonUp(2, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter3MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonDown(3, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter3MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseMove(3, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter3MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonUp(3, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter4MouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonDown(4, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter4MouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseMove(4, MouseEvent); }
FEventReply UInteriorScreenWidget::OnSplitter4MouseButtonUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent) { return HandleSplitterMouseButtonUp(4, MouseEvent); }

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
	RefreshNavigationPanelDisplay();
}

void UInteriorScreenWidget::HandleInteriorContentChanged()
{
	RefreshLayersList();
	RefreshNavRoomDropdowns();
}

void UInteriorScreenWidget::HandlePreviewModeChanged(bool bIsActive)
{
	if (Background)
	{
		Background->SetVisibility(bIsActive ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (FloorPlanWidget)
	{
		FloorPlanWidget->SetVisibility(bIsActive ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	const FLinearColor ActiveColor(0.2f, 0.75f, 0.3f, 1.f);
	const FLinearColor InactiveColor(0.6f, 0.6f, 0.6f, 1.f);
	if (Switch2DButton)
	{
		Switch2DButton->SetBackgroundColor(bIsActive ? InactiveColor : ActiveColor);
	}
	if (Switch3DButton)
	{
		Switch3DButton->SetBackgroundColor(bIsActive ? ActiveColor : InactiveColor);
	}
	if (ResetView3DButton)
	{
		ResetView3DButton->SetVisibility(bIsActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (DecreaseSpeedButton)
	{
		DecreaseSpeedButton->SetVisibility(bIsActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IncreaseSpeedButton)
	{
		IncreaseSpeedButton->SetVisibility(bIsActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (Edit3DModeToggleButton)
	{
		Edit3DModeToggleButton->SetVisibility(bIsActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// Entering 3D preview always starts fresh in View mode (see ANavPlayerController::
	// SetIn3DPreviewMode - the disable path always resets bIsIn3DEditMode back to false, so it's
	// already false by the time this fires with bIsActive true). SetIn3DPreviewMode's enable path
	// doesn't broadcast OnEdit3DModeChanged itself, so without this the button could still be showing
	// a stale "Edit"/green look left over from a PREVIOUS 3D session (e.g. switched to a different
	// building while in Edit mode, which silently resets the flag with no broadcast) even though the
	// state underneath is genuinely View again.
	if (bIsActive)
	{
		HandleEdit3DModeChanged(false);
	}
}

void UInteriorScreenWidget::HandleEdit3DModeChanged(bool bIsEditMode)
{
	if (Edit3DModeToggleButtonText)
	{
		Edit3DModeToggleButtonText->SetText(FText::FromString(bIsEditMode ? TEXT("Edit") : TEXT("View")));
	}
	if (Edit3DModeToggleButton)
	{
		// View: yellow. Edit: green (same green used elsewhere for an "active" state, e.g.
		// RefreshModeButtonHighlights/HandlePreviewModeChanged's own ActiveColor).
		Edit3DModeToggleButton->SetBackgroundColor(bIsEditMode ? FLinearColor(0.2f, 0.75f, 0.3f, 1.f) : FLinearColor(0.8f, 0.7f, 0.1f, 1.f));
	}
}

void UInteriorScreenWidget::RefreshDockPanelContentWidths()
{
	// Toolbar: the building switcher used to live in this panel and tracked its width here - it's now
	// its own fixed-width bar outside the dock system (see RebuildWidget), so there's nothing left to
	// sync for it. Mode-toolbar icons deliberately stay fixed 48x48 - stretching a square icon into a
	// rectangle would just look broken.

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

	// Navigation: buttons track the panel width the same way as everything above. Context/status/
	// route-details text aren't wrapped in their own SizeBox (same as Layers' own title text isn't
	// either) - they stretch to the panel's width naturally as VerticalBox children instead.
	const float NavigationButtonWidth = ComputePanelButtonWidth(EDockPanelId::Navigation);
	ResizeButton(NavSetStartButton, NavigationButtonWidth);
	ResizeButton(NavSetDestinationButton, NavigationButtonWidth);
	ResizeButton(NavFindRouteButton, NavigationButtonWidth);
	ResizeButton(NavClearRouteButton, NavigationButtonWidth);
	if (NavStartRoomComboBox)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(NavStartRoomComboBox->GetParent()))
		{
			SizeBox->SetWidthOverride(NavigationButtonWidth);
		}
	}
	if (NavDestinationRoomComboBox)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(NavDestinationRoomComboBox->GetParent()))
		{
			SizeBox->SetWidthOverride(NavigationButtonWidth);
		}
	}

	// Audio: Stop All tracks the panel width the same way as everything above. The file list is
	// simpler than Floor/Layers' own (no per-row width tracking) - RefreshAudioList just rebuilds
	// every row from scratch with the current width baked in, which is cheap enough for a list this
	// small (a handful of audio files, not hundreds of rooms).
	const float AudioButtonWidth = ComputePanelButtonWidth(EDockPanelId::Audio);
	ResizeButton(AudioStopAllButton, AudioButtonWidth);
	if (AudioFileListBox)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(AudioFileListBox->GetParent()))
		{
			SizeBox->SetWidthOverride(AudioButtonWidth);
		}
	}
	RefreshAudioList();
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
	// entirely just because nothing's selected; with nothing selected it just shows an empty
	// "Properties" placeholder instead of disappearing, matching a real editor's Details/Properties
	// panel (see AutoDockPropertiesBelowFloor, called once at construction instead of waiting for a
	// first selection to dock it under Floor). The Window menu's own checkbox can still hide it -
	// that user choice takes priority over this panel's usual always-visible behavior.
	const bool bUserHidden = bDockPanelUserHidden.IsValidIndex((int32)EDockPanelId::Properties)
		&& bDockPanelUserHidden[(int32)EDockPanelId::Properties];
	PropertiesPanel->SetVisibility(bUserHidden ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);

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
	RefreshNavigationPanelDisplay();
}

void UInteriorScreenWidget::OnAddFloorClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->AddFloor();
	}
	RefreshFloorList();
	RefreshLayersList();
	RefreshNavigationPanelDisplay();
	RefreshNavRoomDropdowns();
}

void UInteriorScreenWidget::OnRemoveFloorClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->RemoveFloor(PC->GetActiveFloor());
	}
	RefreshFloorList();
	RefreshLayersList();
	RefreshNavigationPanelDisplay();
	RefreshNavRoomDropdowns();
}

void UInteriorScreenWidget::OnNavSetStartClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->ToggleSelectStart();
		}
	}
}

void UInteriorScreenWidget::OnNavSetDestinationClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->ToggleSelectDestination();
		}
	}
}

void UInteriorScreenWidget::OnNavFindRouteClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->RequestFindRoute();
		}
	}
}

void UInteriorScreenWidget::OnNavClearRouteClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->RequestClearRoute();
		}
	}
}

void UInteriorScreenWidget::HandleNavigationStateChanged()
{
	RefreshNavigationPanelDisplay();
}

void UInteriorScreenWidget::RefreshNavigationPanelDisplay()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
	if (!Nav)
	{
		return;
	}

	const ENavigationSelectionMode Mode = Nav->GetSelectionMode();
	static const FLinearColor InactiveButtonColor(0.6f, 0.6f, 0.6f, 1.f);
	static const FLinearColor ActiveStartColor(0.08f, 0.55f, 0.22f, 1.f);
	static const FLinearColor ActiveDestinationColor(0.72f, 0.12f, 0.12f, 1.f);

	if (NavSetStartButton)
	{
		// Green while actively picking a start point, and stays green afterward too (once one's
		// actually set) rather than reverting to gray the instant the point is accepted.
		const bool bShowSet = Mode == ENavigationSelectionMode::Start || Nav->HasStart();
		NavSetStartButton->SetBackgroundColor(bShowSet ? ActiveStartColor : InactiveButtonColor);
	}
	if (NavSetStartButtonLabel)
	{
		NavSetStartButtonLabel->SetText(FText::FromString(Mode == ENavigationSelectionMode::Start
			? TEXT("Click a Start Location (Cancel)") : TEXT("Set Start")));
	}
	if (NavSetDestinationButton)
	{
		// Red while actively picking (unchanged), but green (not red) once a destination is actually
		// set - both buttons read as "green = set" once their own point is in place.
		const FLinearColor SetColor = Mode == ENavigationSelectionMode::Destination ? ActiveDestinationColor : ActiveStartColor;
		const bool bShowSet = Mode == ENavigationSelectionMode::Destination || Nav->HasDestination();
		NavSetDestinationButton->SetBackgroundColor(bShowSet ? SetColor : InactiveButtonColor);
	}
	if (NavSetDestinationButtonLabel)
	{
		NavSetDestinationButtonLabel->SetText(FText::FromString(Mode == ENavigationSelectionMode::Destination
			? TEXT("Click a Destination (Cancel)") : TEXT("Set Destination")));
	}
	if (NavFindRouteButton)
	{
		NavFindRouteButton->SetIsEnabled(Nav->CanFindRoute());
		// Green once a route's actually been found, red if the last attempt came back empty-handed,
		// gray otherwise (never tried, or cleared since).
		FLinearColor FindRouteColor = InactiveButtonColor;
		if (!Nav->GetRoutePoints().IsEmpty())
		{
			FindRouteColor = ActiveStartColor;
		}
		else if (Nav->DidLastFindRouteFail())
		{
			FindRouteColor = ActiveDestinationColor;
		}
		NavFindRouteButton->SetBackgroundColor(FindRouteColor);
	}
	if (NavClearRouteButton)
	{
		NavClearRouteButton->SetIsEnabled(Nav->CanClearRoute());
	}
	if (NavContextText)
	{
		const FString BuildingId = PC->GetLoadedBuildingId();
		NavContextText->SetText(FText::FromString(BuildingId.IsEmpty()
			? TEXT("No building loaded")
			: FString::Printf(TEXT("Building: %s  |  Viewing Floor: %d"), *BuildingId, PC->GetActiveFloor())));
	}
	if (NavStatusText)
	{
		NavStatusText->SetText(FText::FromString(Nav->GetDisplayStatusMessage()));
		NavStatusText->SetColorAndOpacity(FSlateColor(Nav->GetStatusColor()));
	}
	if (NavRouteDetailsText)
	{
		NavRouteDetailsText->SetText(FText::FromString(Nav->BuildRouteDetails()));
	}
}

void UInteriorScreenWidget::RefreshAudioList()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC || !AudioFileListBox)
	{
		return;
	}

	AudioFileListBox->ClearChildren();

	const float RowWidth = ComputePanelButtonWidth(EDockPanelId::Audio);
	for (const FString& FileName : PC->GetAvailableAudioFiles())
	{
		UAudioFileRowWidget* Row = CreateWidget<UAudioFileRowWidget>(PC, UAudioFileRowWidget::StaticClass());
		if (!Row)
		{
			continue;
		}
		Row->SetFileName(FileName, PC->IsAudioFilePlaying(FileName));
		Row->SetWidth(RowWidth);
		Row->OnPlayClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleAudioRowPlayClicked);

		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(AudioFileListBox->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}
	}
}

void UInteriorScreenWidget::OnAudioStopAllClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->StopAllAudio();
		RefreshAudioList();
	}
}

void UInteriorScreenWidget::OnAudioClickSoundCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetButtonClickSoundEnabled(bIsChecked);
	}
}

void UInteriorScreenWidget::OnAudioSelectSoundCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetBuildingSelectSoundEnabled(bIsChecked);
	}
}

void UInteriorScreenWidget::OnAudioQuitConfirmSoundCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetQuitConfirmSoundEnabled(bIsChecked);
	}
}

void UInteriorScreenWidget::OnAudioMuteAllCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		// bIsChecked means "muted" here, opposite of the three individual "X Sound" checkboxes (which
		// are checked when ENABLED) - so the three get the inverse, and get visually synced to match.
		PC->SetAllUiSoundsEnabled(!bIsChecked);
	}
	if (AudioClickSoundCheckbox)
	{
		AudioClickSoundCheckbox->SetIsChecked(!bIsChecked);
	}
	if (AudioSelectSoundCheckbox)
	{
		AudioSelectSoundCheckbox->SetIsChecked(!bIsChecked);
	}
	if (AudioQuitConfirmSoundCheckbox)
	{
		AudioQuitConfirmSoundCheckbox->SetIsChecked(!bIsChecked);
	}
}

void UInteriorScreenWidget::OnAudioVolumeChanged(float NewValue)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetMasterVolume(NewValue);
	}
}

void UInteriorScreenWidget::HandleAudioRowPlayClicked(FString FileName)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayAudioFile(FileName);
		RefreshAudioList();
	}
}

void UInteriorScreenWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
	}
}

void UInteriorScreenWidget::HandleNavStartRoomChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// Same reasoning as HandleBuildingSwitcherChanged - only react to the user's own pick, not this
	// function's own SetSelectedOption calls (RefreshNavRoomDropdowns doesn't currently make any,
	// but matching the established guard here costs nothing and avoids a future footgun).
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->SetStartToRoom(SelectedItem);
		}
	}
}

void UInteriorScreenWidget::HandleNavDestinationRoomChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->SetDestinationToRoom(SelectedItem);
		}
	}
}

void UInteriorScreenWidget::RefreshNavRoomDropdowns()
{
	if (!NavStartRoomComboBox || !NavDestinationRoomComboBox)
	{
		return;
	}
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	UNavigationPanelWidget* Nav = PC ? PC->GetNavigationPanel() : nullptr;
	if (!Nav)
	{
		return;
	}

	NavStartRoomComboBox->ClearOptions();
	NavDestinationRoomComboBox->ClearOptions();
	for (const UNavigationPanelWidget::FNavigationRoomOption& Option : Nav->GetSelectableRooms())
	{
		const FString Label = Option.GetDisplayLabel();
		NavStartRoomComboBox->AddOption(Label);
		NavDestinationRoomComboBox->AddOption(Label);
	}
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
		RefreshNavigationPanelDisplay();
		RefreshNavRoomDropdowns();
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
	RefreshNavigationPanelDisplay();
	RefreshNavRoomDropdowns();
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
	RefreshNavigationPanelDisplay();
	RefreshNavRoomDropdowns();
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
	ApplyStandardFont(ItemText);
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

void UInteriorScreenWidget::OnGrowRoomClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->GrowSelectedRoom();
	}
}

void UInteriorScreenWidget::OnShrinkRoomClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ShrinkSelectedRoom();
	}
}

void UInteriorScreenWidget::OnRotateCCWPressed()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->StartRotateCCW();
	}
}

void UInteriorScreenWidget::OnRotateCCWReleased()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->StopRotateCCW();
	}
}

void UInteriorScreenWidget::OnRotateCWPressed()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->StartRotateCW();
	}
}

void UInteriorScreenWidget::OnRotateCWReleased()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->StopRotateCW();
	}
}

void UInteriorScreenWidget::OnFileMenuClicked()
{
	if (!FileMenuDropdown)
	{
		return;
	}
	if (FileMenuDropdown->GetVisibility() != ESlateVisibility::Collapsed)
	{
		CloseMenuDropdowns();
	}
	else
	{
		OpenMenuDropdown(FileMenuDropdown);
	}
}

void UInteriorScreenWidget::OnWindowMenuClicked()
{
	if (!WindowMenuDropdown)
	{
		return;
	}
	if (WindowMenuDropdown->GetVisibility() != ESlateVisibility::Collapsed)
	{
		CloseMenuDropdowns();
	}
	else
	{
		OpenMenuDropdown(WindowMenuDropdown);
	}
}

void UInteriorScreenWidget::OnSwitch2DClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetIn3DPreviewMode(false);
	}
}

void UInteriorScreenWidget::OnSwitch3DClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetIn3DPreviewMode(true);
	}
}

void UInteriorScreenWidget::OnResetView3DClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ResetCamera3DPreview();
	}
}

void UInteriorScreenWidget::OnDecreaseSpeedClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->DecreaseMoveSpeed3DPreview();
	}
}

void UInteriorScreenWidget::OnIncreaseSpeedClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->IncreaseMoveSpeed3DPreview();
	}
}

void UInteriorScreenWidget::OnEdit3DModeToggleClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetIn3DEditMode(!PC->IsIn3DEditMode());
	}
}

void UInteriorScreenWidget::OpenMenuDropdown(UBorder* Dropdown)
{
	if (FileMenuDropdown)
	{
		FileMenuDropdown->SetVisibility(Dropdown == FileMenuDropdown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (WindowMenuDropdown)
	{
		WindowMenuDropdown->SetVisibility(Dropdown == WindowMenuDropdown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (MenuClickCatcher)
	{
		MenuClickCatcher->SetVisibility(ESlateVisibility::Visible);
	}
}

void UInteriorScreenWidget::CloseMenuDropdowns()
{
	if (FileMenuDropdown)
	{
		FileMenuDropdown->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (WindowMenuDropdown)
	{
		WindowMenuDropdown->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (MenuClickCatcher)
	{
		MenuClickCatcher->SetVisibility(ESlateVisibility::Collapsed);
	}
}

FEventReply UInteriorScreenWidget::OnMenuClickCatcherMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent)
{
	CloseMenuDropdowns();
	return UWidgetBlueprintLibrary::Handled();
}

void UInteriorScreenWidget::OnFileNewClicked()
{
	CloseMenuDropdowns();

	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	if (!PC->HasUnsavedChanges())
	{
		// RestoreExteriorCameraView clears interior-editing state (selection, in-progress draw
		// modes, spawned interior actors) before StartNewProject wipes everything else - and, if
		// there's a saved exterior camera position, restores it too, but that's immediately
		// superseded by StartNewProject's own FocusCameraOnAllBuildings, called after it.
		PC->RestoreExteriorCameraView();
		PC->StartNewProject();
		GoToEditScreen();
		return;
	}

	if (UConfirmDialogWidget* Dialog = CreateWidget<UConfirmDialogWidget>(PC, UConfirmDialogWidget::StaticClass()))
	{
		Dialog->AddToViewport();
		Dialog->OnSaveAndLeave.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleFileNewSave);
		Dialog->OnDiscardAndLeave.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleFileNewDiscard);
		Dialog->OnCancel.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleFileNewCancel);
	}
}

void UInteriorScreenWidget::HandleFileNewSave()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SaveCurrentProject();
		PC->RestoreExteriorCameraView();
		PC->StartNewProject();
	}
	GoToEditScreen();
}

void UInteriorScreenWidget::HandleFileNewDiscard()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->DiscardUnsavedChanges();
		PC->RestoreExteriorCameraView();
		PC->StartNewProject();
	}
	GoToEditScreen();
}

void UInteriorScreenWidget::HandleFileNewCancel()
{
	// Nothing to do - the dialog already removed itself.
}

void UInteriorScreenWidget::OnFileLoadClicked()
{
	CloseMenuDropdowns();

	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	// Browse first, before checking for unsaved changes - so cancelling the picker never triggers an
	// unnecessary save prompt.
	const FString FilePath = PC->BrowseForBuildingDataFile();
	if (FilePath.IsEmpty())
	{
		return;
	}

	if (!PC->HasUnsavedChanges())
	{
		PC->RestoreExteriorCameraView();
		PC->LoadBuildingDataFromPath(FilePath);
		GoToEditScreen();
		return;
	}

	PendingLoadFilePath = FilePath;
	if (UConfirmDialogWidget* Dialog = CreateWidget<UConfirmDialogWidget>(PC, UConfirmDialogWidget::StaticClass()))
	{
		Dialog->AddToViewport();
		Dialog->OnSaveAndLeave.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleFileLoadSave);
		Dialog->OnDiscardAndLeave.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleFileLoadDiscard);
		Dialog->OnCancel.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleFileLoadCancel);
	}
}

void UInteriorScreenWidget::HandleFileLoadSave()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SaveCurrentProject();
		PC->RestoreExteriorCameraView();
		PC->LoadBuildingDataFromPath(PendingLoadFilePath);
	}
	GoToEditScreen();
}

void UInteriorScreenWidget::HandleFileLoadDiscard()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->DiscardUnsavedChanges();
		PC->RestoreExteriorCameraView();
		PC->LoadBuildingDataFromPath(PendingLoadFilePath);
	}
	GoToEditScreen();
}

void UInteriorScreenWidget::HandleFileLoadCancel()
{
	// Nothing to do - the dialog already removed itself.
}

void UInteriorScreenWidget::OnWindowToolbarCheckChanged(bool bIsChecked)
{
	SetDockPanelUserHidden(EDockPanelId::Toolbar, !bIsChecked);
}

void UInteriorScreenWidget::OnWindowFloorCheckChanged(bool bIsChecked)
{
	SetDockPanelUserHidden(EDockPanelId::Floor, !bIsChecked);
}

void UInteriorScreenWidget::OnWindowPropertiesCheckChanged(bool bIsChecked)
{
	SetDockPanelUserHidden(EDockPanelId::Properties, !bIsChecked);
}

void UInteriorScreenWidget::OnWindowLayersCheckChanged(bool bIsChecked)
{
	SetDockPanelUserHidden(EDockPanelId::Layers, !bIsChecked);
}

void UInteriorScreenWidget::OnWindowNavigationCheckChanged(bool bIsChecked)
{
	SetDockPanelUserHidden(EDockPanelId::Navigation, !bIsChecked);
}

void UInteriorScreenWidget::OnWindowAudioCheckChanged(bool bIsChecked)
{
	SetDockPanelUserHidden(EDockPanelId::Audio, !bIsChecked);
	if (bIsChecked)
	{
		// The file list can go stale while the panel is hidden (a file dropped into Data/Audio/, or
		// something finishing/starting playback) - refresh right as it becomes visible instead of
		// waiting for some other trigger.
		RefreshAudioList();
	}
}

void UInteriorScreenWidget::SetDockPanelUserHidden(EDockPanelId Id, bool bHidden)
{
	const int32 Index = (int32)Id;
	if (!bDockPanelUserHidden.IsValidIndex(Index))
	{
		return;
	}
	bDockPanelUserHidden[Index] = bHidden;

	if (Id == EDockPanelId::Properties)
	{
		// Properties' own visibility already has other logic fighting over it (shown/hidden by
		// selection state) - RefreshPropertiesPanelVisibility is the single source of truth for it,
		// and now reads this same flag to override that.
		RefreshPropertiesPanelVisibility();
	}
	else if (UBorder* Border = DockPanelBorders[Index])
	{
		Border->SetVisibility(bHidden ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	RefreshDockLayout();
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
