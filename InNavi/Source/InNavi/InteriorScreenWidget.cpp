#include "InteriorScreenWidget.h"
#include "DockHostUMG.h"
#include "InteriorDockLayout.h"
#include "DockSharedPanels.h"
#include "NavigationSearchUtils.h"
#include "InteriorDoorColors.h"
#include "InteriorEntrance.h"
#include "RoomTypes.h"

#include "AudioFileRowWidget.h"
#include "AudioPanelWidget.h"
#include "StartScreenStyle.h"
#include "MapPanelWidget.h"
#include "DebugPanelWidget.h"
#include "LayoutValidationPanelWidget.h"
#include "PluginToolHost.h"
#include "ConfirmDialogWidget.h"
#include "EditScreenWidget.h"
#include "FloorListEntryWidget.h"
#include "InteriorFloorPlanWidget.h"
#include "InteriorTypes.h"
#include "LayerRowWidget.h"
#include "NavigationPanelWidget.h"
#include "NavigationDockPanelWidget.h"
#include "NavPlayerController.h"
#include "InteriorBuildingPreviewPivot.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
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
#include "Components/SpinBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/DefaultValueHelper.h"
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
	DockPanelTitleTexts.Init(nullptr, NumDockPanels);
	// Replaced with each group's real width by SyncDockPanelWidths once the dock has been laid out.
	DockPanelWidths.Init(SidebarMinWidth, NumDockPanels);
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

const FLinearColor UInteriorScreenWidget::StandardTextBoxBackgroundColor(0.149f, 0.149f, 0.169f, 1.f);
const FLinearColor UInteriorScreenWidget::StandardTextBoxTextColor(1.f, 1.f, 1.f, 1.f);

void UInteriorScreenWidget::ApplyStandardFont(UEditableTextBox* TextBox)
{
	if (!TextBox)
	{
		return;
	}
	FEditableTextBoxStyle Style = TextBox->GetWidgetStyle();
	Style.TextStyle.Font.Size = StandardFontSize;
	Style.BackgroundColor = FSlateColor(StandardTextBoxBackgroundColor);
	Style.ForegroundColor = FSlateColor(StandardTextBoxTextColor);
	Style.FocusedForegroundColor = FSlateColor(StandardTextBoxTextColor);
	TextBox->SetWidgetStyle(Style);
}

namespace
{
	// A 2D / 3D segment: amber when it is the active mode, otherwise just text on the frame.
	FButtonStyle Button_Style_Segment(bool bActive)
	{
		using namespace StartStyle;
		return bActive ? ActiveButton(6.f)
			: Button(FLinearColor::Transparent, Rgb(31, 51, 80), Rgb(22, 36, 58), FLinearColor::Transparent, 6.f);
	}

	// The 3D View / Edit button: amber for View, green for Edit.
	FButtonStyle Button_Style_Edit3D(bool bEdit)
	{
		using namespace StartStyle;
		return bEdit ? Button(Rgb(26, 138, 84), Rgb(36, 160, 100), Rgb(18, 110, 66), Rgb(26, 138, 84), 6.f)
			: Button(Rgb(196, 112, 8), Rgb(220, 130, 14), Rgb(160, 90, 4), Rgb(196, 112, 8), 6.f);
	}

	// A tool row's look: plain, or amber when its tool is the active one.
	FButtonStyle Button_Style_Tool(bool bActive)
	{
		using namespace StartStyle;
		return bActive ? ActiveButton()
			: Button(Rgb(24, 38, 60), Rgb(33, 54, 84), Rgb(18, 30, 50), Rgb(46, 66, 94), 8.f);
	}
}

UWidget* UInteriorScreenWidget::MakeToolTipWidget(const TCHAR* TooltipLabel)
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

		// Panel contents live in a UDockHostWidget (the dock system), not directly on this canvas. Each
		// panel is built as a content-root UBorder with the usual padding; nothing is added to the canvas -
		// the root is registered with DockHost and placed there. DockPanelBorders[] keeps hold of each
		// root so the rest of this class can still reach its content.
		DockHost = WidgetTree->ConstructWidget<UDockHostWidget>(UDockHostWidget::StaticClass(), TEXT("DockHost"));
		{
			FDockMetrics DockMetrics;
			DockMetrics.TopMargin = SidebarTopMargin;
			DockMetrics.BottomMargin = SidebarBottomMargin;
			DockHost->SetMetrics(DockMetrics);
		}
		UCanvasPanelSlot* DockHostSlot = Canvas->AddChildToCanvas(DockHost);
		DockHostSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		DockHostSlot->SetOffsets(FMargin(0.f));
		DockHostSlot->SetZOrder(2);

		auto ConstructDockPanel = [this](EDockPanelId Id, const TCHAR* BorderName, const FLinearColor& BrushColor, float BorderPadding) -> UBorder*
		{
			const int32 Index = (int32)Id;
			UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(BorderName));
			// The dock frame already paints the panel background.
			Border->SetBrushColor(FLinearColor::Transparent);
			Border->SetPadding(FMargin(BorderPadding));
			DockPanelBorders[Index] = Border;
			DockHost->RegisterPanel(FName(GetDockPanelName(Id)), FText::FromString(GetDockPanelName(Id)), Border);
			return Border;
		};

		// Panels flush against each other in a column should read as one continuous surface, not
		// separate boxes with visible seams - so all 4 share this exact background color.
		const FLinearColor DockPanelColor(0.03f, 0.03f, 0.03f, 0.88f);

		// -- Toolbar panel: one row per tool, in the style of the design team's "2D floor tools" - the tool's icon, its name and
		// its keyboard shortcut - in four groups. The row of the tool that is active is highlighted in blue (see
		// RefreshModeButtonHighlights). What each tool does, and every shortcut, is unchanged. One VerticalBox so everything
		// auto-stacks. (The building switcher is its own fixed bar below the menu bar, see BuildingSwitcherBarBackground.)
		ToolbarPanel = ConstructDockPanel(EDockPanelId::Toolbar, TEXT("Pnl_Toolbar"), DockPanelColor, 14.f);

		UVerticalBox* LeftSidebar = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LeftSidebar"));
		ToolbarPanel->SetContent(LeftSidebar);

		// Heading. (Not registered as the panel's title text: the dock tab already names the panel, and those titles are hidden.)
		LeftSidebar->AddChildToVerticalBox(StartStyle::Text(WidgetTree, TEXT("Txt_ToolbarKicker"), TEXT("FLOOR EDITOR"), 10.f, StartStyle::Rgb(116, 181, 255), true, 160));
		LeftSidebar->AddChildToVerticalBox(StartStyle::Text(WidgetTree, TEXT("Txt_ToolbarTitle"), TEXT("2D floor tools"), 18.f, StartStyle::Rgb(241, 246, 255), true))
			->SetPadding(FMargin(0.f, 6.f * StartStyle::DesignToUi, 0.f, 0.f));

		auto AddToolGroupLabel = [this, LeftSidebar](const TCHAR* Name, const TCHAR* Label)
		{
			UTextBlock* GroupLabel = StartStyle::Text(WidgetTree, Name, Label, 10.f, StartStyle::Rgb(150, 170, 196), true, 120);
			LeftSidebar->AddChildToVerticalBox(GroupLabel)->SetPadding(FMargin(0.f, 16.f * StartStyle::DesignToUi, 0.f, 8.f * StartStyle::DesignToUi));
		};

		// A tool row: icon, name, shortcut. The tooltip keeps describing the actual interaction (click count, confirm key,
		// defaults) - see docs/design/interior-editor-guide.md. IconAssetName is one of Padre's toolbar icons under
		// /Game/ImageAssets, forced to one size because the source PNGs were not authored at a consistent resolution.
		auto AddToolRow = [this, LeftSidebar](const TCHAR* Name, const TCHAR* IconAssetName, const TCHAR* Label, const TCHAR* Key, const TCHAR* TooltipLabel) -> UButton*
		{
			using namespace StartStyle;
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetStyle(Button_Style_Tool(false));
			Button->SetBackgroundColor(FLinearColor::White);
			Button->SetToolTip(MakeToolTipWidget(TooltipLabel));
			Button->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);

			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(FString(Name) + TEXT("Row")));
			UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(FString(Name) + TEXT("Pad")));
			Pad->SetBrushColor(FLinearColor::Transparent);
			Pad->SetPadding(FMargin(12.f * DesignToUi, 9.f * DesignToUi, 10.f * DesignToUi, 9.f * DesignToUi));
			Pad->SetContent(Row);
			if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Button->SetContent(Pad)))
			{
				// A button centres and shrinks its content by default; these rows have to fill it so the right-hand part (the
				// shortcut, the chevron, the +/-) lines up in one column.
				ContentSlot->SetHorizontalAlignment(HAlign_Fill);
				ContentSlot->SetVerticalAlignment(VAlign_Fill);
			}

			UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(FString(Name) + TEXT("Icon")));
			const FString IconPath = FString::Printf(TEXT("/Game/ImageAssets/%s.%s"), IconAssetName, IconAssetName);
			if (UTexture2D* IconTexture = LoadObject<UTexture2D>(nullptr, *IconPath))
			{
				Icon->SetBrushFromTexture(IconTexture, false);
				Icon->SetDesiredSizeOverride(FVector2D(22.f, 22.f));
			}
			USizeBox* IconSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("IconSize")));
			IconSize->SetWidthOverride(24.f);
			IconSize->SetHeightOverride(24.f);
			IconSize->SetContent(Icon);
			Row->AddChildToHorizontalBox(IconSize)->SetVerticalAlignment(VAlign_Center);

			UTextBlock* Caption = Text(WidgetTree, FString(Name) + TEXT("Label"), Label, 13.f, Rgb(241, 246, 255), true);
			Caption->SetAutoWrapText(true);
			UHorizontalBoxSlot* CaptionSlot = Row->AddChildToHorizontalBox(Caption);
			CaptionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			CaptionSlot->SetVerticalAlignment(VAlign_Center);
			CaptionSlot->SetPadding(FMargin(12.f * DesignToUi, 0.f, 8.f * DesignToUi, 0.f));

			// The shortcut, in a small rounded key.
			UBorder* Key_Badge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(FString(Name) + TEXT("Key")));
			Key_Badge->SetBrush(Rounded(Rgb(14, 24, 38), Rgb(60, 78, 104), 5.f));
			Key_Badge->SetBrushColor(FLinearColor::White);
			Key_Badge->SetPadding(FMargin(7.f * DesignToUi, 3.f * DesignToUi));
			Key_Badge->SetContent(Text(WidgetTree, FString(Name) + TEXT("KeyText"), Key, 11.f, Rgb(170, 190, 214), true));
			UHorizontalBoxSlot* KeySlot = Row->AddChildToHorizontalBox(Key_Badge);
			KeySlot->SetVerticalAlignment(VAlign_Center);

			LeftSidebar->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f * DesignToUi));
			return Button;
		};

		AddToolGroupLabel(TEXT("Txt_GroupSelect"), TEXT("SELECT"));
		SelectModeButton = AddToolRow(TEXT("Select_Mode_Button"), TEXT("Icon_Select"), TEXT("Select"), TEXT("Q"), TEXT("Select Mode [Q] (click a room, door, or connector to select it; drag to move it; Delete to delete)"));
		SelectModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnSelectModeClicked);

		AddToolGroupLabel(TEXT("Txt_GroupDraw"), TEXT("DRAW & PLACE"));
		DrawRoomModeButton = AddToolRow(TEXT("Draw_Mode_Button"), TEXT("Icon_DrawRoom"), TEXT("Draw room"), TEXT("P"), TEXT("Draw Room Mode [P] (click at least 3 points to outline a room, then press Enter to finish; Esc cancels)"));
		DrawRoomModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnDrawRoomModeClicked);
		DrawWallModeButton = AddToolRow(TEXT("Draw_Wall_Button"), TEXT("Icon_DrawWall"), TEXT("Draw wall"), TEXT("L"), TEXT("Draw Wall Mode [L] (click points to draw a standalone wall, then press Enter to finish; Esc cancels)"));
		DrawWallModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnDrawWallModeClicked);
		AddRectModeButton = AddToolRow(TEXT("Add_Rect_Button"), TEXT("Icon_AddRectangle"), TEXT("Rectangle room"), TEXT("1"), TEXT("Add Rectangle [1] (click a point to place a rectangular room centered there)"));
		AddRectModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddRectangleClicked);
		AddTriangleModeButton = AddToolRow(TEXT("Add_Triangle_Button"), TEXT("Icon_AddTriangle"), TEXT("Triangle room"), TEXT("2"), TEXT("Add Triangle [2] (click a point to place a triangular room centered there)"));
		AddTriangleModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddTriangleClicked);
		AddCircleModeButton = AddToolRow(TEXT("Add_Circle_Button"), TEXT("Icon_AddCircle"), TEXT("Circle room"), TEXT("3"), TEXT("Add Circle [3] (click a point to place a circular room centered there)"));
		AddCircleModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddCircleClicked);

		AddToolGroupLabel(TEXT("Txt_GroupNavigate"), TEXT("DOORS & LINKS"));
		AddDoorModeButton = AddToolRow(TEXT("Add_Door_Button"), TEXT("Icon_AddDoor"), TEXT("Place door"), TEXT("K"), TEXT("Add Door Mode [K] (click a wall edge to cut a fixed-size door into it)"));
		AddDoorModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddDoorModeClicked);
		AddStairsModeButton = AddToolRow(TEXT("Add_Stairs_Button"), TEXT("Icon_AddStairs"), TEXT("Stairs"), TEXT("5"), TEXT("Add Stairs Mode [5] (click a point to place stairs, connecting the floor above and below by default)"));
		AddStairsModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddStairsModeClicked);
		AddElevatorModeButton = AddToolRow(TEXT("Add_Elevator_Button"), TEXT("Icon_AddElevator"), TEXT("Elevator"), TEXT("4"), TEXT("Add Elevator Mode [4] (click a point to place an elevator, connecting every floor by default)"));
		AddElevatorModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddElevatorModeClicked);
		AddEscalatorModeButton = AddToolRow(TEXT("Add_Escalator_Button"), TEXT("Icon_AddEscalator"), TEXT("Escalator"), TEXT("6"), TEXT("Add Escalator Mode [6] (click a point to place an escalator, connecting the floor above and below by default)"));
		AddEscalatorModeButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnAddEscalatorModeClicked);

		// Grow/Shrink and Rotate mirror the room keys = - 9 0 exactly (see OnGrowRoomClicked etc.). Rotate keeps turning while the
		// button is held.
		AddToolGroupLabel(TEXT("Txt_GroupGizmos"), TEXT("TRANSFORM"));
		GrowRoomButton = AddToolRow(TEXT("Btn_GrowRoom"), TEXT("Icon_IncreaseSize"), TEXT("Grow"), TEXT("="), TEXT("Grow (increase the selected room's size)"));
		GrowRoomButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnGrowRoomClicked);
		ShrinkRoomButton = AddToolRow(TEXT("Btn_ShrinkRoom"), TEXT("Icon_DecreaseSize"), TEXT("Shrink"), TEXT("-"), TEXT("Shrink (decrease the selected room's size)"));
		ShrinkRoomButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnShrinkRoomClicked);
		ResizeStepLabel = StartStyle::Text(WidgetTree, TEXT("Txt_ResizeStep"), TEXT("Grow / shrink step (m)"), 11.f, FLinearColor::White);
		LeftSidebar->AddChildToVerticalBox(ResizeStepLabel);
		ResizeStepBox = WidgetTree->ConstructWidget<USpinBox>(USpinBox::StaticClass(), TEXT("RoomResizeStep"));
		ResizeStepBox->SetMinValue(0.01f);
		ResizeStepBox->SetMaxValue(100.f);
		ResizeStepBox->SetDelta(0.1f);
		ResizeStepBox->SetValue(0.5f);
		ResizeStepBox->OnValueChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnResizeStepChanged);
		LeftSidebar->AddChildToVerticalBox(ResizeStepBox);
		RotateCCWButton = AddToolRow(TEXT("Btn_RotateCCW"), TEXT("Icon_RotateCCW"), TEXT("Rotate left"), TEXT("9"), TEXT("Rotate Left (hold to keep rotating the selected room counter-clockwise)"));
		RotateCCWButton->OnPressed.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRotateCCWPressed);
		RotateCCWButton->OnReleased.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRotateCCWReleased);
		RotateCWButton = AddToolRow(TEXT("Btn_RotateCW"), TEXT("Icon_RotateCW"), TEXT("Rotate right"), TEXT("0"), TEXT("Rotate Right (hold to keep rotating the selected room clockwise)"));
		RotateCWButton->OnPressed.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRotateCWPressed);
		RotateCWButton->OnReleased.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRotateCWReleased);

		// -- Floor panel --
		FloorPanel = ConstructDockPanel(EDockPanelId::Floor, TEXT("Pnl_Floor"), DockPanelColor, 14.f);

		UVerticalBox* FloorStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FloorStack"));
		FloorPanel->SetContent(FloorStack);

		// Heading: a floor mark, "Floors" with how many there are, and + Add on the right.
		UHorizontalBox* FloorHeader = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FloorHeader"));
		FloorHeader->AddChildToHorizontalBox(StartStyle::Text(WidgetTree, TEXT("Txt_FloorMark"), TEXT("\u25A4"), 16.f, StartStyle::Rgb(176, 198, 228)))->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* FloorTitleSlot = FloorHeader->AddChildToHorizontalBox(StartStyle::Text(WidgetTree, TEXT("Txt_FloorTitle"), TEXT("Floors"), 16.f, StartStyle::Rgb(241, 246, 255), true));
		FloorTitleSlot->SetVerticalAlignment(VAlign_Center);
		FloorTitleSlot->SetPadding(FMargin(8.f * StartStyle::DesignToUi, 0.f, 6.f * StartStyle::DesignToUi, 0.f));
		FloorCountText = StartStyle::Text(WidgetTree, TEXT("Txt_FloorCount"), TEXT("0"), 13.f, StartStyle::Rgb(150, 172, 202), true);
		UHorizontalBoxSlot* FloorCountSlot = FloorHeader->AddChildToHorizontalBox(FloorCountText);
		FloorCountSlot->SetVerticalAlignment(VAlign_Center);
		FloorCountSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		AddFloorButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_AddFloor"));
		AddFloorButton->SetStyle(StartStyle::SecondaryButton());
		AddFloorButton->SetBackgroundColor(FLinearColor::White);
		UTextBlock* AddFloorText = StartStyle::Text(WidgetTree, TEXT("Btn_AddFloorLabel"), TEXT("+  Add"), 12.f, FLinearColor::White, true);
		UBorder* AddFloorPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Btn_AddFloorPad"));
		AddFloorPad->SetBrushColor(FLinearColor::Transparent);
		AddFloorPad->SetPadding(FMargin(12.f * StartStyle::DesignToUi, 7.f * StartStyle::DesignToUi));
		AddFloorPad->SetContent(AddFloorText);
		AddFloorButton->SetContent(AddFloorPad);
		FloorHeader->AddChildToHorizontalBox(AddFloorButton)->SetVerticalAlignment(VAlign_Center);
		FloorStack->AddChildToVerticalBox(FloorHeader)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f * StartStyle::DesignToUi));

		FloorListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("FloorListBox"));
		// 3 rows tall (row height + 4px padding each, see RefreshFloorList) caps the list without
		// hiding the scrollbar UScrollBox already shows once content overflows that.
		USizeBox* FloorListSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FloorListSizeBox"));
		FloorListSizeBox->SetHeightOverride((StandardButtonHeight + 10.f) * 3.f);
		FloorListSizeBox->SetWidthOverride(StandardButtonWidth);
		FloorListSizeBox->SetContent(FloorListBox);
		UVerticalBoxSlot* FloorListSlot = FloorStack->AddChildToVerticalBox(FloorListSizeBox);
		FloorListSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f * StartStyle::DesignToUi));

		// Delete floor: a quiet button that turns red when the mouse is over it.
		RemoveFloorButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_RemoveFloor"));
		RemoveFloorButton->SetStyle(StartStyle::Button(StartStyle::Rgb(27, 41, 63), StartStyle::Rgb(96, 34, 42), StartStyle::Rgb(72, 24, 32), StartStyle::Rgb(61, 78, 104)));
		RemoveFloorButton->SetBackgroundColor(FLinearColor::White);
		UTextBlock* RemoveFloorText = StartStyle::Text(WidgetTree, TEXT("Btn_RemoveFloorLabel"), TEXT("Delete floor"), 12.f, FLinearColor::White, true);
		RemoveFloorText->SetJustification(ETextJustify::Center);
		UBorder* RemoveFloorPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Btn_RemoveFloorPad"));
		RemoveFloorPad->SetBrushColor(FLinearColor::Transparent);
		RemoveFloorPad->SetPadding(FMargin(10.f * StartStyle::DesignToUi, 9.f * StartStyle::DesignToUi));
		RemoveFloorPad->SetContent(RemoveFloorText);
		RemoveFloorButton->SetContent(RemoveFloorPad);
		FloorStack->AddChildToVerticalBox(RemoveFloorButton);

		// PropertiesPanel/FloorPanel's own SetPadding(14.f) eats 14px on each side - this is what's
		// actually left for their content, and is what any text inside them (that isn't already
		// inside its own fixed-width button) needs to be hard-wrapped to. A SizeBox's WidthOverride
		// only constrains layout size, it doesn't clip - text wider than that still painted straight
		// through it and past the panel's edge until it was also given SetAutoWrapText(true) at this
		// same width. Computed from Floor's width (both panels always share a width once docked
		// together, and start out that way).
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
		constexpr float MenuButtonWidth = 72.f;
		// The Save button's slot in the menu bar, padding included.
		constexpr float SaveButtonWidth = 96.f;
		constexpr float CameraMenuButtonWidth = 84.f;
		constexpr float MenuBarLeftMargin = 10.f;

		UBorder* MenuBarBackground = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_MenuBar"));
		MenuBarBackground->SetBrushColor(StartStyle::Rgb(16, 32, 50));
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
			Button->SetStyle(StartStyle::MenuButton());
			Button->SetBackgroundColor(FLinearColor::White);
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

		// Save icon button, left of File - a plain icon rather than a labeled AddMenuButton, since it
		// takes no dropdown of its own (unlike File/Window) and just fires PC->SaveCurrentProject()
		// on click. Square, matching the bar's own height.
		SaveButton = StartStyle::SaveBarButton(WidgetTree, SaveIndicatorOuter, SaveIndicatorCore, SaveButtonWidth - 12.f);
		SaveButton->SetToolTip(MakeToolTipWidget(TEXT("Save [Ctrl+S]")));
		SaveButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnSaveClicked);
		SaveButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
		if (UHorizontalBoxSlot* SaveBarSlot = MenuBarRow->AddChildToHorizontalBox(SaveButton))
		{
			SaveBarSlot->SetPadding(FMargin(6.f, 5.f));
			SaveBarSlot->SetVerticalAlignment(VAlign_Center);
		}
		RefreshSaveIndicator();

		FileMenuButton = AddMenuButton(TEXT("Btn_FileMenu"), TEXT("File"));
		FileMenuButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnFileMenuClicked);
		WindowMenuButton = AddMenuButton(TEXT("Btn_WindowMenu"), TEXT("Window"));
		WindowMenuButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnWindowMenuClicked);
		// Camera menu - only visible while in 3D preview (see HandlePreviewModeChanged); wider than File/Window
		// since its label is longer.
		CameraMenuButton = AddMenuButton(TEXT("Btn_CameraMenu"), TEXT("Camera"));
		CameraMenuButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnCameraMenuClicked);
		CameraMenuButton->SetVisibility(ESlateVisibility::Collapsed);
		if (USizeBox* CameraMenuSizeBox = Cast<USizeBox>(CameraMenuButton->GetContent()))
		{
			CameraMenuSizeBox->SetWidthOverride(CameraMenuButtonWidth);
		}

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
		BackButton->SetStyle(StartStyle::SecondaryButton());
		BackButton->SetBackgroundColor(FLinearColor::White);
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
			Dropdown->SetBrush(StartStyle::MenuPanel());
			Dropdown->SetBrushColor(FLinearColor::White);
			Dropdown->SetPadding(FMargin(6.f));
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
			Button->SetStyle(StartStyle::MenuRowButton());
			Button->SetBackgroundColor(FLinearColor::White);
			Button->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetJustification(ETextJustify::Center);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(Text);
			USizeBox* RowSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			RowSizeBox->SetWidthOverride(130.f);
			RowSizeBox->SetMinDesiredHeight(24.f);
			if (USizeBoxSlot* RowTextSlot = Cast<USizeBoxSlot>(RowSizeBox->SetContent(Text)))
			{
				RowTextSlot->SetPadding(FMargin(10.f, 4.f));
			}
			Button->SetContent(RowSizeBox);
			UVerticalBoxSlot* RowSlot = Stack->AddChildToVerticalBox(Button);
			RowSlot->SetPadding(FMargin(2.f, 3.f));
			return Button;
		};

		auto AddDropdownCheckRow = [this](UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label) -> UCheckBox*
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			UCheckBox* CheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), FName(Name));
			CheckBox->SetIsChecked(true);
			StartStyle::DarkCheckBox(CheckBox);
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

		FileMenuDropdown = ConstructDropdown(TEXT("Pnl_FileDropdown"), MenuBarLeftMargin + SaveButtonWidth);
		UVerticalBox* FileDropdownStack = Cast<UVerticalBox>(FileMenuDropdown->GetContent());
		UButton* FileNewButton = AddDropdownButtonRow(FileDropdownStack, TEXT("Btn_FileNew"), TEXT("New"));
		FileNewButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnFileNewClicked);
		UButton* FileLoadButton = AddDropdownButtonRow(FileDropdownStack, TEXT("Btn_FileLoad"), TEXT("Load"));
		FileLoadButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnFileLoadClicked);
		UButton* FileSaveAsButton = AddDropdownButtonRow(FileDropdownStack, TEXT("Btn_FileSaveAs"), TEXT("Save As"));
		FileSaveAsButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnFileSaveAsClicked);
		UButton* FileGenerateFloorButton = AddDropdownButtonRow(FileDropdownStack, TEXT("Btn_FileGenerateFloor"), TEXT("Generate floor"));
		FileGenerateFloorButton->SetToolTip(MakeToolTipWidget(TEXT("Fill this floor with rooms along its outer wall and a core beside each door")));
		FileGenerateFloorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnFileGenerateFloorClicked);

		WindowMenuDropdown = ConstructDropdown(TEXT("Pnl_WindowDropdown"), MenuBarLeftMargin + SaveButtonWidth + MenuButtonWidth);
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
		// Unlike every row besides Audio, starts unchecked - see EDockPanelId::Navigation's own comment.
		WindowNavigationCheckbox->SetIsChecked(false);
		WindowNavigationCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnWindowNavigationCheckChanged);
		WindowAudioCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowAudio"), TEXT("Audio"));
		// Unlike every other row, starts unchecked - see EDockPanelId::Audio's own comment.
		WindowAudioCheckbox->SetIsChecked(false);
		WindowAudioCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnWindowAudioCheckChanged);
		WindowMapCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowMap"), TEXT("Map"));
		// Starts checked, matching UEditScreenWidget's own "Map" panel default.
		WindowMapCheckbox->SetIsChecked(false); // the Map panel starts hidden (RefreshWindowMenuChecks follows the real state)
		WindowMapCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnWindowMapCheckChanged);
		WindowDebugCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowDebug"), TEXT("Debug"));
		WindowDebugCheckbox->SetIsChecked(false);
		WindowDebugCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnWindowDebugCheckChanged);
		PluginToolHost = NewObject<UPluginToolHost>(this);
		PluginToolHost->AddMenu(LayoutTools::EScreen::Interior, [&AddDropdownCheckRow, WindowDropdownStack](const FString& Name, const FString& Label)
		{
			return AddDropdownCheckRow(WindowDropdownStack, *Name, *Label);
		});
		UButton* WindowResetLayoutButton = AddDropdownButtonRow(WindowDropdownStack, TEXT("Btn_WindowResetLayout"), TEXT("Reset Layout"));
		WindowResetLayoutButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnWindowResetLayoutClicked);

		// Camera dropdown: Reset, the typed move speed, +, -. Stays open after +/- so the speed can be nudged
		// several times; Reset closes it (see OnResetView3DClicked).
		CameraMenuDropdown = ConstructDropdown(TEXT("Pnl_CameraDropdown"), MenuBarLeftMargin + SaveButtonWidth + 2.f * MenuButtonWidth);
		UVerticalBox* CameraDropdownStack = Cast<UVerticalBox>(CameraMenuDropdown->GetContent());
		ResetView3DButton = AddDropdownButtonRow(CameraDropdownStack, TEXT("Btn_ResetView3D"), TEXT("Reset"));
		ResetView3DButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnResetView3DClicked);
		ResetView3DButton->SetToolTipText(FText::FromString(TEXT("Reset View")));

		// Typed speed entry - see OnCameraSpeedTextCommitted.
		CameraSpeedTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_CameraSpeed"));
		CameraSpeedTextBox->SetJustification(ETextJustify::Center);
		CameraSpeedTextBox->SetToolTipText(FText::FromString(TEXT("Camera Speed (type a value, then press Enter or click away)")));
		CameraSpeedTextBox->OnTextCommitted.AddUniqueDynamic(this, &UInteriorScreenWidget::OnCameraSpeedTextCommitted);
		ApplyStandardFont(CameraSpeedTextBox);
		CameraSpeedTextBox->SetWidgetStyle(StartStyle::Field(13.f));
		USizeBox* CameraSpeedSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		CameraSpeedSizeBox->SetWidthOverride(90.f);
		CameraSpeedSizeBox->SetContent(CameraSpeedTextBox);
		UVerticalBoxSlot* CameraSpeedSlot = CameraDropdownStack->AddChildToVerticalBox(CameraSpeedSizeBox);
		CameraSpeedSlot->SetPadding(FMargin(2.f));

		IncreaseSpeedButton = AddDropdownButtonRow(CameraDropdownStack, TEXT("Btn_IncreaseSpeed"), TEXT("+"));
		IncreaseSpeedButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnIncreaseSpeedClicked);
		IncreaseSpeedButton->SetToolTipText(FText::FromString(TEXT("Increase Camera Speed")));
		DecreaseSpeedButton = AddDropdownButtonRow(CameraDropdownStack, TEXT("Btn_DecreaseSpeed"), TEXT("-"));
		DecreaseSpeedButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnDecreaseSpeedClicked);
		DecreaseSpeedButton->SetToolTipText(FText::FromString(TEXT("Decrease Camera Speed")));
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
		// hidden. Content stays left-aligned; the rest of the bar's width is just empty background. (The 3D
		// camera controls are not in this bar: they are the Camera menu, after Window in the menu bar.)
		UBorder* BuildingSwitcherBarBackground = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_BuildingSwitcherBar"));
		BuildingSwitcherBarBackground->SetBrushColor(StartStyle::Rgb(17, 31, 50));
		BuildingSwitcherBarBackground->SetPadding(FMargin(14.f, 8.f));
		UCanvasPanelSlot* BuildingSwitcherBarSlot = Canvas->AddChildToCanvas(BuildingSwitcherBarBackground);
		BuildingSwitcherBarSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
		BuildingSwitcherBarSlot->SetAlignment(FVector2D(0.f, 0.f));
		BuildingSwitcherBarSlot->SetOffsets(FMargin(0.f, MenuBarHeight, 0.f, BuildingSwitcherBarHeight));
		BuildingSwitcherBarSlot->SetZOrder(6);

		// In the style of the design team's page, one row, everything side by side: the building (a drop-down to switch, a pencil to
		// rename), then 2D | 3D, undo, redo, snapping, and (in 3D only) View / Edit. Choosing the tools is the
		// Toolbar panel's job, so they are not repeated here.
		{
		using namespace StartStyle;
		// Adds a child to a row, vertically centred, with the given padding.
		auto AddCentered = [](UHorizontalBox* Row, UWidget* Child, const FMargin& SlotPadding)
		{
			UHorizontalBoxSlot* ChildSlot = Row->AddChildToHorizontalBox(Child);
			ChildSlot->SetVerticalAlignment(VAlign_Center);
			ChildSlot->SetPadding(SlotPadding);
		};

		UHorizontalBox* SwitcherRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BuildingSwitcherRow"));
		BuildingSwitcherBarBackground->SetContent(SwitcherRow);

		AddCentered(SwitcherRow, Text(WidgetTree, TEXT("Txt_BuildingMark"), TEXT("\u25A5"), 15.f, Rgb(80, 150, 255)), FMargin(0.f));

		BuildingSwitcherComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Cbx_BuildingSwitcher"));
		BuildingSwitcherComboBox->OnGenerateWidgetEvent.BindDynamic(this, &UInteriorScreenWidget::HandleGenerateComboBoxItem);
		DarkCombo(BuildingSwitcherComboBox);
		USizeBox* BuildingSwitcherSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		// A floor, not a fixed width, so a long building name (e.g. a database-loaded one) isn't
		// clipped/cramped - it grows with the name instead of staying stuck at one width for everyone.
		BuildingSwitcherSizeBox->SetMinDesiredWidth(150.f);
		BuildingSwitcherSizeBox->SetContent(BuildingSwitcherComboBox);
		AddCentered(SwitcherRow, BuildingSwitcherSizeBox, FMargin(8.f * DesignToUi, 0.f, 4.f, 0.f));

		BuildingNameTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_BuildingName"));
		BuildingNameTextBox->SetVisibility(ESlateVisibility::Collapsed);
		BuildingNameTextBox->SetWidgetStyle(Field(13.f));
		AddCentered(SwitcherRow, BuildingNameTextBox, FMargin(8.f * DesignToUi, 0.f, 4.f, 0.f));

		// The pencil renames the building (and becomes a check mark to confirm).
		EditBuildingNameButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_EditBuildingName"));
		EditBuildingNameButton->SetStyle(SecondaryButton());
		EditBuildingNameButton->SetBackgroundColor(FLinearColor::White);
		EditBuildingNameButton->SetToolTip(MakeToolTipWidget(TEXT("Rename the building")));
		EditBuildingNameButtonText = Text(WidgetTree, TEXT("Btn_EditBuildingNameLabel"), TEXT("\x270E"), 13.f, FLinearColor::White, true); // pencil
		EditBuildingNameButtonText->SetJustification(ETextJustify::Center);
		USizeBox* EditButtonSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		EditButtonSizeBox->SetWidthOverride(32.f);
		EditButtonSizeBox->SetHeightOverride(32.f);
		if (USizeBoxSlot* EditButtonTextSlot = Cast<USizeBoxSlot>(EditButtonSizeBox->SetContent(EditBuildingNameButtonText)))
		{
			EditButtonTextSlot->SetHorizontalAlignment(HAlign_Center);
			EditButtonTextSlot->SetVerticalAlignment(VAlign_Center);
		}
		EditBuildingNameButton->SetContent(EditButtonSizeBox);
		AddCentered(SwitcherRow, EditBuildingNameButton, FMargin(0.f));

		// 2D | 3D as one segmented control: the active one is blue. (No Stack view in this editor.)
		UBorder* SegmentFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ModeSegmentFrame"));
		SegmentFrame->SetBrush(Rounded(Rgb(14, 24, 38), Rgb(46, 66, 94), 8.f));
		SegmentFrame->SetBrushColor(FLinearColor::White);
		SegmentFrame->SetPadding(FMargin(3.f));
		UHorizontalBox* SegmentRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ModeSegmentRow"));
		SegmentFrame->SetContent(SegmentRow);
		auto AddSegment = [this, SegmentRow](const TCHAR* Name, const TCHAR* Label) -> UButton*
		{
			UButton* Segment = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Segment->SetStyle(Button_Style_Segment(false));
			Segment->SetBackgroundColor(FLinearColor::White);
			Segment->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
			UTextBlock* Caption = StartStyle::Text(WidgetTree, FString(Name) + TEXT("Label"), Label, 13.f, FLinearColor::White, true);
			Caption->SetJustification(ETextJustify::Center);
			UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(FString(Name) + TEXT("Pad")));
			Pad->SetBrushColor(FLinearColor::Transparent);
			Pad->SetPadding(FMargin(20.f * StartStyle::DesignToUi, 6.f * StartStyle::DesignToUi));
			Pad->SetContent(Caption);
			Segment->SetContent(Pad);
			SegmentRow->AddChildToHorizontalBox(Segment)->SetPadding(FMargin(2.f, 0.f));
			return Segment;
		};
		Switch2DButton = AddSegment(TEXT("Btn_Switch2D"), TEXT("2D"));
		Switch2DButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnSwitch2DClicked);
		Switch3DButton = AddSegment(TEXT("Btn_Switch3D"), TEXT("3D"));
		Switch3DButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnSwitch3DClicked);
		AddCentered(SwitcherRow, SegmentFrame, FMargin(16.f * DesignToUi, 0.f, 16.f * DesignToUi, 0.f));

		// ---- Undo, redo, snapping ----
		// An action button: its icon and its name, like the design's edit strip.
		auto AddEditButton = [this, SwitcherRow, &AddCentered](const TCHAR* Name, const TCHAR* IconAssetName, const TCHAR* Label, const TCHAR* TooltipLabel, UImage** OutIcon) -> UButton*
		{
			using namespace StartStyle;
			UButton* Edit = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Edit->SetStyle(SecondaryButton());
			Edit->SetBackgroundColor(FLinearColor::White);
			Edit->SetToolTip(MakeToolTipWidget(TooltipLabel));
			Edit->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
			UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(FString(Name) + TEXT("Content")));
			UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(FString(Name) + TEXT("Icon")));
			Icon->SetDesiredSizeOverride(FVector2D(18.f, 18.f));
			if (IconAssetName && *IconAssetName)
			{
				const FString IconPath = FString::Printf(TEXT("/Game/ImageAssets/%s.%s"), IconAssetName, IconAssetName);
				if (UTexture2D* IconTexture = LoadObject<UTexture2D>(nullptr, *IconPath))
				{
					Icon->SetBrushFromTexture(IconTexture, false);
				}
			}
			USizeBox* IconSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("IconSize")));
			IconSize->SetWidthOverride(20.f);
			IconSize->SetHeightOverride(20.f);
			IconSize->SetContent(Icon);
			AddCentered(Content, IconSize, FMargin(0.f));
			AddCentered(Content, Text(WidgetTree, FString(Name) + TEXT("Label"), Label, 12.f, FLinearColor::White, true), FMargin(8.f * DesignToUi, 0.f, 0.f, 0.f));
			UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(FString(Name) + TEXT("Pad")));
			Pad->SetBrushColor(FLinearColor::Transparent);
			Pad->SetPadding(FMargin(12.f * DesignToUi, 7.f * DesignToUi));
			Pad->SetContent(Content);
			Edit->SetContent(Pad);
			AddCentered(SwitcherRow, Edit, FMargin(0.f, 0.f, 8.f * DesignToUi, 0.f));
			if (OutIcon)
			{
				*OutIcon = Icon;
			}
			return Edit;
		};
		UndoButton = AddEditButton(TEXT("Btn_Undo"), TEXT("Icon_Undo"), TEXT("Undo"), TEXT("Undo [Ctrl+Z]"), nullptr);
		UndoButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnUndoClicked);
		RedoButton = AddEditButton(TEXT("Btn_Redo"), TEXT("Icon_Redo"), TEXT("Redo"), TEXT("Redo [Ctrl+Y]"), nullptr);
		RedoButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRedoClicked);
		// Snapping: always visible (it matters in 2D too). Its icon swaps between on and off in HandleSnapEnabledChanged, once the
		// controller is there to say which.
		SnapToggleButton = AddEditButton(TEXT("Btn_SnapToggle"), TEXT(""), TEXT("Snapping"), TEXT("Toggle Snapping [N]"), &SnapToggleIcon);
		SnapToggleButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnSnapToggleClicked);
		// Room colours: fills the rooms in the 2D plan with their type's colour. The swatch is coloured when on and grey when off (HandleRoomColoursChanged).
		RoomColoursButton = AddEditButton(TEXT("Btn_RoomColours"), TEXT(""), TEXT("Colours"), TEXT("Room colours ON"), &RoomColoursIcon);
		RoomColoursButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRoomColoursClicked);


		// Isolate | Orbit | Grab - in 3D only, as one framed group like 2D | 3D. Isolate hides the bottom platform and the surroundings and lets the
		// model be turned; Orbit and Grab are how dragging turns it, and one of them is always on (picking one turns the other off).
		InspectionFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InspectionFrame"));
		InspectionFrame->SetBrush(Rounded(Rgb(14, 24, 38), Rgb(46, 66, 94), 8.f));
		InspectionFrame->SetBrushColor(FLinearColor::White);
		InspectionFrame->SetPadding(FMargin(3.f));
		InspectionFrame->SetVisibility(ESlateVisibility::Collapsed);
		UHorizontalBox* InspectionRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("InspectionRow"));
		InspectionFrame->SetContent(InspectionRow);
		auto AddInspectionSegment = [this, InspectionRow](const TCHAR* Name, const TCHAR* Label, UTextBlock** OutCaption) -> UButton*
		{
			UButton* Segment = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Segment->SetStyle(Button_Style_Segment(false));
			Segment->SetBackgroundColor(FLinearColor::White);
			Segment->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
			UTextBlock* Caption = StartStyle::Text(WidgetTree, FString(Name) + TEXT("Label"), Label, 13.f, FLinearColor::White, true);
			Caption->SetJustification(ETextJustify::Center);
			if (OutCaption) *OutCaption = Caption;
			UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(FString(Name) + TEXT("Pad")));
			Pad->SetBrushColor(FLinearColor::Transparent);
			Pad->SetPadding(FMargin(12.f * StartStyle::DesignToUi, 6.f * StartStyle::DesignToUi));
			Pad->SetContent(Caption);
			Segment->SetContent(Pad);
			Segment->SetVisibility(ESlateVisibility::Collapsed);
			InspectionRow->AddChildToHorizontalBox(Segment)->SetPadding(FMargin(2.f, 0.f));
			return Segment;
		};
		InspectionToggleButton = AddInspectionSegment(TEXT("Btn_InspectionToggle"), TEXT("Isolate"), &InspectionToggleText);
		InspectionToggleButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnInspectionToggleClicked);
		InspectionOrbitButton = AddInspectionSegment(TEXT("Btn_InspectionOrbit"), TEXT("Orbit"), nullptr);
		InspectionOrbitButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnInspectionOrbitClicked);
		InspectionGrabButton = AddInspectionSegment(TEXT("Btn_InspectionGrab"), TEXT("Grab"), nullptr);
		InspectionGrabButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnInspectionGrabClicked);
		AddCentered(SwitcherRow, InspectionFrame, FMargin(8.f * DesignToUi, 0.f, 0.f, 0.f));

		// "View" / "Edit" - in 3D only, beside 2D | 3D since it is that mode's sub-mode. Starts as "View" (amber), matching PC's
		// own default (see ANavPlayerController::bIsIn3DEditMode); HandleEdit3DModeChanged makes it green for Edit.
		Edit3DModeToggleButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_Edit3DModeToggle"));
		Edit3DModeToggleButton->SetStyle(Button_Style_Edit3D(false));
		Edit3DModeToggleButton->SetBackgroundColor(FLinearColor::White);
		Edit3DModeToggleButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
		Edit3DModeToggleButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnEdit3DModeToggleClicked);
		Edit3DModeToggleButtonText = Text(WidgetTree, TEXT("Btn_Edit3DModeToggleLabel"), TEXT("View"), 13.f, FLinearColor::White, true);
		Edit3DModeToggleButtonText->SetJustification(ETextJustify::Center);
		UBorder* Edit3DPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Btn_Edit3DModeTogglePad"));
		Edit3DPad->SetBrushColor(FLinearColor::Transparent);
		Edit3DPad->SetPadding(FMargin(16.f * DesignToUi, 9.f * DesignToUi));
		Edit3DPad->SetContent(Edit3DModeToggleButtonText);
		Edit3DModeToggleButton->SetContent(Edit3DPad);
		Edit3DModeToggleButton->SetVisibility(ESlateVisibility::Collapsed);
		AddCentered(SwitcherRow, Edit3DModeToggleButton, FMargin(8.f * DesignToUi, 0.f, 0.f, 0.f));
		InspectionHint = Text(WidgetTree, TEXT("Txt_InspectionHint"), TEXT(""), 12.f, StartStyle::Rgb(255, 200, 100), true);
		InspectionHint->SetVisibility(ESlateVisibility::Collapsed);
		InspectionHint->SetAutoWrapText(true);
		InspectionHint->SetWrapTextAt(460.f);
		InspectionHint->SetJustification(ETextJustify::Center);
		UCanvasPanelSlot* HintSlot = Canvas->AddChildToCanvas(InspectionHint);
		HintSlot->SetAnchors(FAnchors(0.5f, 0.f));
		HintSlot->SetAlignment(FVector2D(0.5f, 0.f));
		HintSlot->SetAutoSize(true);
		HintSlot->SetPosition(FVector2D(0.f, MenuBarHeight + BuildingSwitcherBarHeight + 12.f));
		HintSlot->SetZOrder(4);
		// Screen-space ring: readable through the active upper floor, fixed to the real bottom anchor.
		InspectionPivotOverlay = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Img_InspectionPivot"));
		InspectionPivotOverlay->SetBrush(Rounded(Rgb(16,32,50,130), Rgb(255,190,70), 11.f, 2.f));
		InspectionPivotOverlay->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* PivotSlot = Canvas->AddChildToCanvas(InspectionPivotOverlay);
		PivotSlot->SetAlignment(FVector2D(0.5f,0.5f));
		PivotSlot->SetSize(FVector2D(22.f * DesignToUi));
		PivotSlot->SetZOrder(4);
		}

		// Thin separator lines at the seam between the menu bar and the switcher bar, and again below
		// the switcher bar - both bars (and the panels below) share the same dark background color, so
		// without these the eye can't tell where one ends and the next begins.
		constexpr float SeparatorLineHeight = 2.f;
		auto AddHorizontalSeparator = [this, Canvas, SeparatorLineHeight](float PositionY)
		{
			UBorder* Separator = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*FString::Printf(TEXT("Chrome_Separator_%d"), FMath::RoundToInt(PositionY))));
			Separator->SetBrushColor(StartStyle::Rgb(41, 64, 87));
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

		UVerticalBox* PropertiesStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PropertiesStack"));
		PropertiesPanel->SetContent(PropertiesStack);
		{
		// What follows is in the style of the design team's Properties panel: a heading with a tag saying what is selected, a
		// section heading, small-capital field labels over dark fields, and Delete / Save changes at the bottom. Every piece sits
		// in a SizeBox that follows the panel's width (PropertiesWidthBoxes, see RefreshDockPanelContentWidths).
		using namespace StartStyle;
		auto InWidthBox = [this](UWidget* Content) -> USizeBox*
		{
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			Box->SetWidthOverride(StandardButtonWidth);
			Box->SetContent(Content);
			PropertiesWidthBoxes.Add(Box);
			return Box;
		};
		auto AddFieldLabel = [this](UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label)
		{
			Stack->AddChildToVerticalBox(StartStyle::Text(WidgetTree, Name, Label, 10.f, StartStyle::Rgb(165, 182, 204), true, 30))
				->SetPadding(FMargin(0.f, 12.f * StartStyle::DesignToUi, 0.f, 6.f * StartStyle::DesignToUi));
		};
		auto StyleField = [](UEditableTextBox* Box, bool bReadOnly)
		{
			Box->SetWidgetStyle(StartStyle::Field(12.f));
			Box->SetIsReadOnly(bReadOnly);
		};

		// Heading: a mark and "Properties" on the left, the tag on the right.
		// A wrap box: a long tag (a room type such as "MEETING ROOM") drops under the title when the panel is narrow, instead of drawing over it.
		UWrapBox* PropertiesHeader = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("PropertiesHeader"));
		PropertiesHeader->SetInnerSlotPadding(FVector2D(8.f * DesignToUi, 6.f * DesignToUi));
		UHorizontalBox* PropertiesHeaderTitle = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PropertiesHeaderTitle"));
		PropertiesHeaderTitle->AddChildToHorizontalBox(Text(WidgetTree, TEXT("Txt_PropertiesMark"), TEXT("\u2261"), 17.f, Rgb(176, 198, 228)))->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* PropertiesHeaderTitleSlot = PropertiesHeaderTitle->AddChildToHorizontalBox(Text(WidgetTree, TEXT("Txt_PropertiesHeader"), TEXT("Properties"), 16.f, Rgb(241, 246, 255), true));
		PropertiesHeaderTitleSlot->SetVerticalAlignment(VAlign_Center);
		PropertiesHeaderTitleSlot->SetPadding(FMargin(8.f * DesignToUi, 0.f, 0.f, 0.f));
		PropertiesHeader->AddChildToWrapBox(PropertiesHeaderTitle)->SetVerticalAlignment(VAlign_Center);
		PropertiesKindTag = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PropertiesKindTag"));
		PropertiesKindTag->SetBrush(Rounded(Rgb(14, 24, 38), Rgb(60, 78, 104), 5.f));
		PropertiesKindTag->SetBrushColor(FLinearColor::White);
		PropertiesKindTag->SetPadding(FMargin(7.f * DesignToUi, 3.f * DesignToUi));
		PropertiesKindTag->SetVisibility(ESlateVisibility::Collapsed);
		PropertiesKindText = Text(WidgetTree, TEXT("Txt_PropertiesKind"), TEXT(""), 10.f, Rgb(170, 190, 214), true, 100);
		PropertiesKindTag->SetContent(PropertiesKindText);
		PropertiesHeader->AddChildToWrapBox(PropertiesKindTag)->SetVerticalAlignment(VAlign_Center);
		PropertiesStack->AddChildToVerticalBox(InWidthBox(PropertiesHeader))->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f * DesignToUi));

		// Section heading ("Room properties", ...), which is also what the dock treats as this panel's title.
		PropertiesTitleText = Text(WidgetTree, TEXT("Txt_PropertiesTitle"), TEXT(""), 15.f, Rgb(241, 246, 255), true);
		PropertiesTitleText->SetAutoWrapText(true);
		PropertiesStack->AddChildToVerticalBox(WrapLabelToPanelWidth(PropertiesTitleText, TEXT("Txt_PropertiesTitleSize")));
		DockPanelTitleTexts[(int32)EDockPanelId::Properties] = PropertiesTitleText;

		// A Delete button (red under the mouse) and a Save changes button, side by side.
		auto AddButtonsRow = [this, &InWidthBox](UVerticalBox* Box, const TCHAR* DeleteName, const TCHAR* DeleteLabelText, UTextBlock** OutDeleteLabel,
			const TCHAR* SaveName, const TCHAR* SaveLabelText, UButton*& OutSaveButton)
		{
			using namespace StartStyle;
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(FString(SaveName) + TEXT("Row")));

			UButton* Delete = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(DeleteName));
			Delete->SetStyle(Button(Rgb(86, 32, 40), Rgb(124, 42, 52), Rgb(66, 24, 32), Rgb(150, 56, 66)));
			Delete->SetBackgroundColor(FLinearColor::White);
			UTextBlock* DeleteText = Text(WidgetTree, FString(DeleteName) + TEXT("Label"), DeleteLabelText, 12.f, FLinearColor::White, true);
			DeleteText->SetJustification(ETextJustify::Center);
			DeleteText->SetAutoWrapText(true);
			UBorder* DeletePad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(FString(DeleteName) + TEXT("Pad")));
			DeletePad->SetBrushColor(FLinearColor::Transparent);
			DeletePad->SetPadding(FMargin(8.f * DesignToUi, 10.f * DesignToUi));
			DeletePad->SetContent(DeleteText);
			Delete->SetContent(DeletePad);
			Delete->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnDeleteSelectionClicked);
			Delete->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::PlayButtonClickSound);
			if (OutDeleteLabel)
			{
				*OutDeleteLabel = DeleteText;
			}
			UHorizontalBoxSlot* DeleteSlot = Row->AddChildToHorizontalBox(Delete);
			DeleteSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			DeleteSlot->SetPadding(FMargin(0.f, 0.f, 4.f * DesignToUi, 0.f));

			OutSaveButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(SaveName));
			OutSaveButton->SetStyle(SecondaryButton());
			OutSaveButton->SetBackgroundColor(FLinearColor::White);
			UTextBlock* SaveText = Text(WidgetTree, FString(SaveName) + TEXT("Label"), SaveLabelText, 12.f, FLinearColor::White, true);
			SaveText->SetJustification(ETextJustify::Center);
			SaveText->SetAutoWrapText(true);
			UBorder* SavePad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(FString(SaveName) + TEXT("Pad")));
			SavePad->SetBrushColor(FLinearColor::Transparent);
			SavePad->SetPadding(FMargin(8.f * DesignToUi, 10.f * DesignToUi));
			SavePad->SetContent(SaveText);
			OutSaveButton->SetContent(SavePad);
			UHorizontalBoxSlot* SaveSlot = Row->AddChildToHorizontalBox(OutSaveButton);
			SaveSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			SaveSlot->SetPadding(FMargin(4.f * DesignToUi, 0.f, 0.f, 0.f));
			Box->AddChildToVerticalBox(InWidthBox(Row))->SetPadding(FMargin(0.f, 18.f * DesignToUi, 0.f, 0.f));
		};

		// -- Door fields --
		DoorFieldsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DoorFieldsBox"));
		DoorFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
		PropertiesStack->AddChildToVerticalBox(DoorFieldsBox);

		DoorRoomText = Text(WidgetTree, TEXT("Txt_DoorRoom"), TEXT(""), 12.f, Rgb(170, 190, 214));
		DoorFieldsBox->AddChildToVerticalBox(WrapLabelToPanelWidth(DoorRoomText, TEXT("Txt_DoorRoomSize")))->SetPadding(FMargin(0.f, 4.f * DesignToUi, 0.f, 0.f));

		// "Connects to": the label above a drop-down that fills the width.
		UVerticalBox* ConnectsToColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ConnectsToColumn"));
		ConnectsToRowSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ConnectsToRowSizeBox->SetContent(ConnectsToColumn);
		DoorFieldsBox->AddChildToVerticalBox(ConnectsToRowSizeBox);
		AddFieldLabel(ConnectsToColumn, TEXT("Txt_ConnectsToLabel"), TEXT("CONNECTS TO"));
		ConnectsToComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Cmb_ConnectsTo"));
		ConnectsToComboBox->OnGenerateWidgetEvent.BindDynamic(this, &UInteriorScreenWidget::HandleGenerateComboBoxItem);
		DarkCombo(ConnectsToComboBox); // the same dark look as the text fields
		ConnectsToColumn->AddChildToVerticalBox(ConnectsToComboBox);
		AddButtonsRow(DoorFieldsBox, TEXT("Btn_DeleteDoor"), TEXT("Delete door"), nullptr, TEXT("Btn_ApplyDoor"), TEXT("Save changes"), ApplyDoorButton);
		// Wired as the button is built, so it works however the widget came to exist.
		ApplyDoorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnApplyDoorClicked);

		// -- Room/Wall fields --
		RoomFieldsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoomFieldsBox"));
		RoomFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
		PropertiesStack->AddChildToVerticalBox(RoomFieldsBox);

		RoomSubtitleText = Text(WidgetTree, TEXT("Txt_RoomSubtitle"), TEXT(""), 12.f, Rgb(170, 190, 214));
		RoomSubtitleText->SetAutoWrapText(true);
		RoomFieldsBox->AddChildToVerticalBox(WrapLabelToPanelWidth(RoomSubtitleText, TEXT("Txt_RoomSubtitleSize")))->SetPadding(FMargin(0.f, 4.f * DesignToUi, 0.f, 0.f));

		UVerticalBox* RoomNameColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoomNameRow"));
		RoomNameRowSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		RoomNameRowSizeBox->SetContent(RoomNameColumn);
		RoomFieldsBox->AddChildToVerticalBox(RoomNameRowSizeBox);
		AddFieldLabel(RoomNameColumn, TEXT("Txt_RoomNameLabel"), TEXT("ROOM NAME"));
		RoomNameTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_RoomName"));
		StyleField(RoomNameTextBox, false);
		RoomNameColumn->AddChildToVerticalBox(RoomNameTextBox);

		// Position stays read-only; size fields edit the bounding box in metres.
		auto AddBoundsCell = [this, &AddFieldLabel, &StyleField](UHorizontalBox* Row, const TCHAR* Name, const TCHAR* Label, UEditableTextBox*& OutBox, bool bFirstInRow)
		{
			UVerticalBox* Cell = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(FString(Name) + TEXT("Cell")));
			AddFieldLabel(Cell, *(FString(Name) + TEXT("Label")), Label);
			OutBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), FName(Name));
			StyleField(OutBox, true);
			Cell->AddChildToVerticalBox(OutBox);
			UHorizontalBoxSlot* CellSlot = Row->AddChildToHorizontalBox(Cell);
			CellSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			CellSlot->SetPadding(FMargin(bFirstInRow ? 0.f : 6.f * StartStyle::DesignToUi, 0.f, bFirstInRow ? 6.f * StartStyle::DesignToUi : 0.f, 0.f));
		};
		UHorizontalBox* PositionRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RoomPositionRow"));
		AddBoundsCell(PositionRow, TEXT("Txt_RoomX"), TEXT("X POSITION"), RoomXBox, true);
		AddBoundsCell(PositionRow, TEXT("Txt_RoomY"), TEXT("Y POSITION"), RoomYBox, false);
		RoomNameColumn->AddChildToVerticalBox(PositionRow);
		UHorizontalBox* SizeRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RoomSizeRow"));
		AddBoundsCell(SizeRow, TEXT("Txt_RoomWidth"), TEXT("WIDTH (m)"), RoomWidthBox, true);
		AddBoundsCell(SizeRow, TEXT("Txt_RoomLength"), TEXT("LENGTH (m)"), RoomLengthBox, false);
		RoomWidthBox->SetIsReadOnly(false);
		RoomLengthBox->SetIsReadOnly(false);
		// Enter commits without clearing focus (which would trigger OnCleared and discard invalid text).
		RoomWidthBox->SetClearKeyboardFocusOnCommit(false);
		RoomLengthBox->SetClearKeyboardFocusOnCommit(false);
		RoomWidthBox->OnTextChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRoomSizeTextChanged);
		RoomLengthBox->OnTextChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRoomSizeTextChanged);
		RoomWidthBox->OnTextCommitted.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRoomSizeTextCommitted);
		RoomLengthBox->OnTextCommitted.AddUniqueDynamic(this, &UInteriorScreenWidget::OnRoomSizeTextCommitted);
		RoomNameColumn->AddChildToVerticalBox(SizeRow);

		RoomTagsFields = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoomTagsFields"));
		RoomTagsFieldsSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		RoomTagsFieldsSizeBox->SetWidthOverride(StandardButtonWidth);
		RoomTagsFieldsSizeBox->SetContent(RoomTagsFields);
		RoomFieldsBox->AddChildToVerticalBox(RoomTagsFieldsSizeBox);
		AddFieldLabel(RoomTagsFields, TEXT("Txt_RoomTypeLabel"), TEXT("ROOM TYPE"));
		RoomTypeComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Cmb_RoomType"));
		RoomTypeComboBox->OnGenerateWidgetEvent.BindDynamic(this, &UInteriorScreenWidget::HandleGenerateRoomTypeItem);
		DarkCombo(RoomTypeComboBox);
		for (const FString& TypeName : RoomTypes::All())
		{
			RoomTypeComboBox->AddOption(TypeName);
		}
		RoomTypeComboBox->SetSelectedOption(RoomTypes::Default());
		RoomTagsFields->AddChildToVerticalBox(RoomTypeComboBox);
		AddFieldLabel(RoomTagsFields, TEXT("Txt_RoomTagsLabel"), TEXT("SEARCH TAGS (COMMA SEPARATED)"));
		RoomTagsTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_RoomTags"));
		StyleField(RoomTagsTextBox, false);
		RoomTagsTextBox->SetHintText(FText::FromString(TEXT("e.g. laboratory, teaching")));
		RoomTagsFields->AddChildToVerticalBox(RoomTagsTextBox);

		// One button saves the name and the tags together.
		AddButtonsRow(RoomFieldsBox, TEXT("Btn_DeleteRoom"), TEXT("Delete room"), &RoomDeleteLabel, TEXT("Btn_ApplyRoom"), TEXT("Save changes"), ApplyRoomButton);
		ApplyRoomButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnApplyRoomClicked);

		// -- Connector (elevator/stairs/escalator) fields --
		ConnectorFieldsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ConnectorFieldsBox"));
		ConnectorFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
		PropertiesStack->AddChildToVerticalBox(ConnectorFieldsBox);

		ConnectorTypeText = Text(WidgetTree, TEXT("Txt_ConnectorType"), TEXT(""), 12.f, Rgb(170, 190, 214));
		ConnectorFieldsBox->AddChildToVerticalBox(WrapLabelToPanelWidth(ConnectorTypeText, TEXT("Txt_ConnectorTypeSize")))->SetPadding(FMargin(0.f, 4.f * DesignToUi, 0.f, 0.f));

		AddFieldLabel(ConnectorFieldsBox, TEXT("Txt_FloorsLabel"), TEXT("FLOORS IT REACHES"));

		UWrapBox* FloorsWrap = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("FloorsWrap"));
		FloorsWrap->SetInnerSlotPadding(FVector2D(8.f, 4.f));
		// Hard-capped to PropertiesPanel's content width (panel width minus its own 14px-a-side
		// padding) so the floor checkboxes wrap onto more lines instead of pushing the panel wider
		// than Floor above it - a WrapBox with nothing constraining its width just grows to fit
		// everything on one line.
		FloorsWrapSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FloorsWrapSizeBox"));
		FloorsWrapSizeBox->SetWidthOverride(SidebarContentWidth);
		FloorsWrapSizeBox->SetContent(FloorsWrap);
		ConnectorFieldsBox->AddChildToVerticalBox(FloorsWrapSizeBox)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		FloorCheckboxes.Reserve(20);
		for (int32 FloorNumber = 1; FloorNumber <= 20; ++FloorNumber)
		{
			UHorizontalBox* FloorEntry = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*FString::Printf(TEXT("FloorEntry%02d"), FloorNumber)));

			UCheckBox* CheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), FName(*FString::Printf(TEXT("Chk_Floor%d"), FloorNumber)));
			StartStyle::DarkCheckBox(CheckBox);
			UHorizontalBoxSlot* CheckBoxSlot = FloorEntry->AddChildToHorizontalBox(CheckBox);
			CheckBoxSlot->SetVerticalAlignment(VAlign_Center);

			UTextBlock* FloorLabel = Text(WidgetTree, *FString::Printf(TEXT("Txt_Floor%dLabel"), FloorNumber), *FString::FromInt(FloorNumber), 12.f, Rgb(214, 226, 242));
			UHorizontalBoxSlot* FloorLabelSlot = FloorEntry->AddChildToHorizontalBox(FloorLabel);
			FloorLabelSlot->SetPadding(FMargin(2.f, 0.f, 0.f, 0.f));
			FloorLabelSlot->SetVerticalAlignment(VAlign_Center);

			FloorsWrap->AddChildToWrapBox(FloorEntry);
			FloorCheckboxes.Add(CheckBox);
		}

		AddButtonsRow(ConnectorFieldsBox, TEXT("Btn_DeleteConnector"), TEXT("Delete connector"), nullptr, TEXT("Btn_ApplyConnector"), TEXT("Save changes"), ApplyConnectorButton);
		ApplyConnectorButton->OnClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::OnApplyConnectorClicked);
		}

		// Permanent panel, like Floor/Toolbar/Layers - always shown; RefreshPropertiesPanelVisibility
		// keeps its title as this neutral placeholder while nothing's selected instead of collapsing
		// the whole panel away.
		PropertiesPanel->SetVisibility(ESlateVisibility::Visible);
		PropertiesTitleText->SetText(FText::FromString(TEXT("Nothing selected")));

		// -- Layers panel: a Hierarchy/Outliner-style tree over what the 2D floor plan actually
		// draws (Boundary/Walls/Path Nodes/I/O Nodes) - see the comment above
		// HandleLayerRowExpandClicked. Rows are ULayerRowWidget instances, built fresh each time by
		// RefreshLayersList rather than laid out here (same reasoning as FloorListBox's rows).
		LayersPanel = ConstructDockPanel(EDockPanelId::Layers, TEXT("Pnl_Layers"), DockPanelColor, 14.f);

		UVerticalBox* LayersStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LayersStack"));
		LayersPanel->SetContent(LayersStack);

		// Heading: "Layers / Scene" on the left, "LEVEL n" (the floor being listed) on the right.
		UHorizontalBox* LayersHeader = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("LayersHeader"));
		LayersHeader->AddChildToHorizontalBox(StartStyle::Text(WidgetTree, TEXT("Txt_LayersMark"), TEXT("\u25C7"), 14.f, StartStyle::Rgb(176, 198, 228)))->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* LayersTitleSlot = LayersHeader->AddChildToHorizontalBox(StartStyle::Text(WidgetTree, TEXT("Txt_LayersTitle"), TEXT("Layers / Scene"), 16.f, StartStyle::Rgb(241, 246, 255), true));
		LayersTitleSlot->SetVerticalAlignment(VAlign_Center);
		LayersTitleSlot->SetPadding(FMargin(8.f * StartStyle::DesignToUi, 0.f, 8.f * StartStyle::DesignToUi, 0.f));
		LayersTitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LayersLevelText = StartStyle::Text(WidgetTree, TEXT("Txt_LayersLevel"), TEXT(""), 10.f, StartStyle::Rgb(150, 172, 202), true, 100);
		LayersHeader->AddChildToHorizontalBox(LayersLevelText)->SetVerticalAlignment(VAlign_Center);
		LayersStack->AddChildToVerticalBox(LayersHeader)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f * StartStyle::DesignToUi));

		LayersSearchBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_LayersSearch"));
		LayersSearchBox->SetHintText(FText::FromString(TEXT("Search")));
		LayersSearchBox->SetWidgetStyle(StartStyle::Field(12.f));
		LayersSearchBox->OnTextChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleLayersSearchTextChanged);
		USizeBox* LayersSearchSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Txt_LayersSearchSize"));
		LayersSearchSizeBox->SetWidthOverride(StandardButtonWidth);
		LayersSearchSizeBox->SetContent(LayersSearchBox);
		UVerticalBoxSlot* LayersSearchSlot = LayersStack->AddChildToVerticalBox(LayersSearchSizeBox);
		LayersSearchSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f * StartStyle::DesignToUi));

		LayersListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("LayersListBox"));
		// 8 rows tall by default - scrollable beyond that via UScrollBox's own scrollbar, same
		// reasoning as FloorListBox. Shrunk to fit whenever Layers is actually docked into a column
		// smaller than this - see LayersListDefaultHeight's own comment.
		USizeBox* LayersListSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LayersListSizeBox"));
		LayersListSizeBox->SetHeightOverride(LayersListDefaultHeight);
		LayersListSizeBox->SetWidthOverride(StandardButtonWidth);
		LayersListSizeBox->SetContent(LayersListBox);
		LayersStack->AddChildToVerticalBox(LayersListSizeBox);

		// -- Navigation panel: the shared one (NavigationDockPanelWidget.h) - Start and Destination, Find Route, the status and the
		// directions. The state it shows and the route planning live in UNavigationPanelWidget, reached through
		// ANavPlayerController::GetNavigationPanel(). Registered with the dock host and not parented here.
		NavigationPanelWidget = CreateWidget<UNavigationDockPanelWidget>(this, UNavigationDockPanelWidget::StaticClass());
		NavigationPanelWidget->SetOwnerScreen(this);
		DockHost->RegisterPanel(FName(GetDockPanelName(EDockPanelId::Navigation)), FText::FromString(GetDockPanelName(EDockPanelId::Navigation)), NavigationPanelWidget);

		// -- Audio panel: the shared one (AudioPanelWidget.h) - Stop All, the sound settings, volume and the file
		// list. Starts hidden (see InteriorDock::BuildDefaultLayout); shown via the Window menu like any other
		// hidden panel. It is registered with the dock host and not parented here.
		AudioPanelWidget = CreateWidget<UAudioPanelWidget>(this, UAudioPanelWidget::StaticClass());
		AudioPanelWidget->SetOwnerScreen(this);
		DockHost->RegisterPanel(FName(GetDockPanelName(EDockPanelId::Audio)), FText::FromString(GetDockPanelName(EDockPanelId::Audio)), AudioPanelWidget);

		// -- Map panel: the shared one (MapPanelWidget.h), the same as on the exterior screen: the list of buildings (a click opens
		// one), the filters and the roads. Starts shown.
		MapPanelWidget = CreateWidget<UMapPanelWidget>(this, UMapPanelWidget::StaticClass());
		MapPanelWidget->SetOwnerScreen(this);
		MapPanelWidget->SetShowBuildingFilters(true);
		// The filters are the controller's, and what they hold carries over from the exterior screen: they are not reset here.
		MapPanelWidget->SetBuildingVisibilityDefaults(false);
		MapPanelWidget->SetBuildingClickHandler([this](const FString& BuildingId) { SwitchToBuilding(BuildingId); });
		MapPanelWidget->SetSelectedBuildingProvider([this]()
		{
			const ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
			return PC ? PC->GetLoadedBuildingId() : FString();
		});
		// The interior editor does not want roads in the way; the exterior screen turns them back on.
		MapPanelWidget->SetRoadsVisibleByDefault(false);
		DockHost->RegisterPanel(FName(GetDockPanelName(EDockPanelId::Map)), FText::FromString(GetDockPanelName(EDockPanelId::Map)), MapPanelWidget);

		// -- Debug panel: the shared one (DebugPanelWidget.h). The screen supplies the lines that are its own.
		DebugPanelWidget = CreateWidget<UDebugPanelWidget>(this, UDebugPanelWidget::StaticClass());
		DebugPanelWidget->SetOwnerScreen(this);
		DebugPanelWidget->SetInfoProvider([this]() { return BuildDebugInfo(); });
		DockHost->RegisterPanel(FName(GetDockPanelName(EDockPanelId::Debug)), FText::FromString(GetDockPanelName(EDockPanelId::Debug)), DebugPanelWidget);
		PluginToolHost->CreatePanels(this, DockHost, LayoutTools::EScreen::Interior, [this]() { ShowValidationPanel(); });
		ValidationPanelWidget = Cast<ULayoutValidationPanelWidget>(PluginToolHost->FindPanel(TEXT("Validation")));

		// The dock frames' tabs already show each panel's name, so the static title text at the top of a
		// panel's content would just repeat it. Properties keeps its own - that one changes with the
		// selection (see RefreshPropertiesPanelVisibility).
		for (int32 Index = 0; Index < NumDockPanels; ++Index)
		{
			if (UTextBlock* Title = DockPanelTitleTexts[Index])
			{
				if ((EDockPanelId)Index != EDockPanelId::Properties)
				{
					Title->SetVisibility(ESlateVisibility::Collapsed);
				}
			}
		}
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
		PC->OnSelectedEntranceChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleSelectedEntranceChanged);
		PC->OnBeforeProjectSave.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleBeforeProjectSave);
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
		PC->OnInspectionModeChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleInspectionModeChanged);
		HandleInspectionModeChanged(PC->IsInteriorInspectionEnabled());
		HandleEdit3DModeChanged(PC->IsIn3DEditMode());
		// Snapping can also be toggled by the N key, not just SnapToggleButton (defensive symmetry,
		// matching every other PC-state delegate above).
		PC->OnSnapEnabledChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleSnapEnabledChanged);
		PC->OnRoomColoursChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleRoomColoursChanged);
		HandleRoomColoursChanged(PC->AreRoomColoursVisible());
		HandleSnapEnabledChanged(PC->IsSnapEnabled());

		// Navigation panel: hand ws/jh's UNavigationPanelWidget this screen's own Navigation Border to
		// hit-test against (see SetPanelBorderForHitTest's own comment) and subscribe to its
		// OnStateChanged so RefreshNavigationPanelDisplay repaints whenever selection/route state
		// changes, from any trigger (this panel's own buttons, or a floor-plan click).
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			// A building is open here: the dropdowns list its rooms and a click picks a point on the floor plan.
			Nav->SetOutdoorMode(false);
			Nav->SetPointerOverPanelCallback([this](const FVector2D& ScreenPosition)
			{
				return DockHost && DockHost->IsScreenPositionOverPanel(FName(GetDockPanelName(EDockPanelId::Navigation)), ScreenPosition);
			});
			Nav->OnStateChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleNavigationStateChanged);
			Nav->OnGuidedStepChanged.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleGuidedStepChanged);
		}

	}

	RefreshBuildingSwitcher();
	RefreshFloorList();
	RefreshLayersList();
	RefreshNavigationPanelDisplay();
	RefreshNavRoomDropdowns();
	if (AudioPanelWidget)
	{
		AudioPanelWidget->Refresh();
	}
	if (DebugPanelWidget)
	{
		DebugPanelWidget->BindToController();
	}
	if (MapPanelWidget)
	{
		// Roads off while editing a building; the check boxes in the Map panel agree, and so do its building filters.
		MapPanelWidget->ApplyDefaultsToController();
		MapPanelWidget->Refresh();
	}
	RefreshModeButtonHighlights();
	// Syncs Properties' placeholder title/visibility against PC's actual current selection state
	// (normally nothing yet, but defensive either way) before panels get positioned below.
	RefreshPropertiesPanelVisibility();
	// Arranges every dock panel for the first time: the user's saved layout, else the team default.
	InitializeDockLayout();
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		// A step outside is the exterior screen's: from now on this screen hands over to it when the route comes to one. (It does not
		// follow the step when it comes up: it may have been opened by the person for another building, and the step stays as it is
		// until the next one.)
		PC->SetNavigationStepScreenCallback(this, [this](const FNavigationRoutePoint& Point) { return HandOverToCampusForStep(Point); });
	}
}

void UInteriorScreenWidget::InitializeDockLayout()
{
	if (!DockHost || bDockLayoutInitialized)
	{
		return;
	}
	bDockLayoutInitialized = true;

	DockHost->OnPanelClosedNative.BindUObject(this, &UInteriorScreenWidget::HandleDockPanelClosed);
	DockHost->OnLayoutChangedNative.BindUObject(this, &UInteriorScreenWidget::HandleDockLayoutChanged);

	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetPointerOverUICallback(this, [this](const FVector2D& ScreenPosition)
		{
			return DockHost && DockHost->IsScreenPositionOverDock(ScreenPosition);
		});
	}

	InteriorDock::LoadLayout(*DockHost->GetLayout(), GetCanvasSize());
	// The panels this screen has in common with the exterior one (Audio, Map, ...) are where that screen left them.
	DockSharedPanels::ApplyPublished(*DockHost->GetLayout());
	bSharedPanelsTaken = true;
	DockHost->NotifyLayoutChanged();
	RefreshWindowMenuChecks();
}

bool UInteriorScreenWidget::IsDockPanelShown(EDockPanelId Id) const
{
	return DockHost && DockHost->IsPanelShown(FName(GetDockPanelName(Id)));
}

void UInteriorScreenWidget::HandleDockPanelClosed(FName Panel)
{
	CancelNavigationPickIfNavigationHidden();
	RefreshWindowMenuChecks();
}

void UInteriorScreenWidget::CancelNavigationPickIfNavigationHidden()
{
	// A Set Start / Set Destination pick that is waiting for a click would otherwise stay armed with no panel
	// on screen to show it (or to cancel it).
	if (IsDockPanelShown(EDockPanelId::Navigation))
	{
		return;
	}
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->CancelSelection();
		}
	}
}

void UInteriorScreenWidget::HandleDockLayoutChanged()
{
	if (bSharedPanelsTaken && DockHost)
	{
		DockSharedPanels::Publish(*DockHost->GetLayout());
	}
	RefreshWindowMenuChecks();
}

void UInteriorScreenWidget::RefreshWindowMenuChecks()
{
	// Every change of the layout by the screen itself ends here (the Window menu, a closed panel, the first arrangement), and the dock host does
	// not report those: the shared panels are published from here as well as from the changes a person makes with the mouse.
	if (bSharedPanelsTaken && DockHost)
	{
		DockSharedPanels::Publish(*DockHost->GetLayout());
	}
	auto Sync = [this](UCheckBox* Checkbox, EDockPanelId Id)
	{
		if (Checkbox)
		{
			Checkbox->SetIsChecked(IsDockPanelShown(Id));
		}
	};
	Sync(WindowToolbarCheckbox, EDockPanelId::Toolbar);
	Sync(WindowFloorCheckbox, EDockPanelId::Floor);
	Sync(WindowPropertiesCheckbox, EDockPanelId::Properties);
	Sync(WindowLayersCheckbox, EDockPanelId::Layers);
	Sync(WindowNavigationCheckbox, EDockPanelId::Navigation);
	Sync(WindowAudioCheckbox, EDockPanelId::Audio);
	Sync(WindowMapCheckbox, EDockPanelId::Map);
	Sync(WindowDebugCheckbox, EDockPanelId::Debug);
	if (PluginToolHost) PluginToolHost->Sync();
}

void UInteriorScreenWidget::ResetDockLayout()
{
	if (!DockHost)
	{
		return;
	}
	InteriorDock::BuildDefaultLayout(*DockHost->GetLayout());
	DockHost->NotifyLayoutChanged();
	RefreshWindowMenuChecks();
	SaveDockLayoutToDisk();
}

void UInteriorScreenWidget::SyncDockPanelWidths()
{
	if (!DockHost)
	{
		return;
	}
	bool bAnyChanged = false;
	for (int32 Index = 0; Index < NumDockPanels; ++Index)
	{
		const float Width = DockHost->GetPanelWidth(FName(GetDockPanelName((EDockPanelId)Index)));
		if (Width > 0.f && !FMath::IsNearlyEqual(Width, DockPanelWidths[Index], 0.5f))
		{
			DockPanelWidths[Index] = Width;
			bAnyChanged = true;
		}
	}
	if (bAnyChanged)
	{
		RefreshDockPanelContentWidths();
	}
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
		PC->OnSelectedEntranceChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleSelectedEntranceChanged);
		PC->OnBeforeProjectSave.RemoveDynamic(this, &UInteriorScreenWidget::HandleBeforeProjectSave);
		PC->OnActiveFloorChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleActiveFloorChanged);
		PC->OnInteriorContentChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleInteriorContentChanged);
		PC->OnPreviewModeChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandlePreviewModeChanged);
		PC->OnEdit3DModeChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleEdit3DModeChanged);
		PC->SetInteriorInspectionEnabled(false);
		PC->OnInspectionModeChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleInspectionModeChanged);
		PC->OnSnapEnabledChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleSnapEnabledChanged);
		PC->OnRoomColoursChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleRoomColoursChanged);
		PC->ClearPointerOverUICallback(this);

		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->OnStateChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleNavigationStateChanged);
			Nav->OnGuidedStepChanged.RemoveDynamic(this, &UInteriorScreenWidget::HandleGuidedStepChanged);
			PC->ClearNavigationStepScreenCallback(this);
			Nav->SetPointerOverPanelCallback(nullptr);
		}
	}

	if (DockHost)
	{
		DockHost->OnPanelClosedNative.Unbind();
		DockHost->OnLayoutChangedNative.Unbind();
	}

	Super::NativeDestruct();
}

void UInteriorScreenWidget::RefreshSaveIndicator()
{
	const ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	const int8 State = PC && PC->HasUnsavedChanges() ? 1 : 0;
	if (State != SaveIndicatorState)
	{
		SaveIndicatorState = State;
		StartStyle::PaintSaveIndicator(SaveIndicatorOuter, SaveIndicatorCore, State == 1);
	}
}

void UInteriorScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	if (InspectionPivotOverlay)
	{
		ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
		AInteriorBuildingPreviewPivot* Pivot = PC ? PC->GetInspectionPivot() : nullptr;
		FVector2D Position = FVector2D::ZeroVector;
		const bool bShowPivot = PC && PC->IsInteriorInspectionEnabled() && IsValid(Pivot) &&
			UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Pivot->GetActorLocation(), Position, false);
		InspectionPivotOverlay->SetVisibility(bShowPivot ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bShowPivot)
		{
			if (UCanvasPanelSlot* PivotCanvasSlot = Cast<UCanvasPanelSlot>(InspectionPivotOverlay->Slot)) PivotCanvasSlot->SetPosition(Position);
		}
	}
	RefreshSaveIndicator();
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>(); PC && ResizeStepBox)
	{
		const bool bConnector = !PC->GetSelectedConnectorGroupId().IsEmpty();
		const FString Label = bConnector ? TEXT("Scale step (%)") : TEXT("Grow / shrink step (m)");
		if (ResizeStepLabel && ResizeStepLabel->GetText().ToString() != Label) ResizeStepLabel->SetText(FText::FromString(Label));
		const float Step = bConnector ? PC->GetConnectorResizeStepPercent() : PC->GetRoomResizeStepMetres();
		if (!FMath::IsNearlyEqual(ResizeStepBox->GetValue(), Step)) ResizeStepBox->SetValue(Step);
	}
	if (RoomFieldsBox && RoomFieldsBox->GetVisibility() == ESlateVisibility::Visible)
	{
		RefreshRoomBoundsFields();
	}
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (MapPanelWidget)
	{
		MapPanelWidget->RefreshBuildingFilterDisplayIfChanged();
	}
	// Panel content sizes itself from its panel's width, which the user can change at any time.
	SyncDockPanelWidths();
	if (PluginToolHost) PluginToolHost->Sync();
}

FString UInteriorScreenWidget::BuildDebugInfo() const
{
	FString ActiveBuildingId = TEXT("None");
	int32 ActiveFloor = 0;
	int32 TotalFloors = 0;

	FString ToolMode = TEXT("None");
	FString ViewType = TEXT("2D");

	FString SelectedRoom = TEXT("None");
	FString SelectedDoor = TEXT("None");
	FString SelectedConnector = TEXT("None");

	FString UnsavedChanges = TEXT("No");

	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		// Building
		const FString BuildingId = PC->GetSelectedBuildingId();
		if (!BuildingId.IsEmpty())
		{
			ActiveBuildingId = BuildingId;
		}

		// Floor
		ActiveFloor = PC->GetActiveFloor();
		TotalFloors = PC->GetLoadedBuildingFloorCount();

		// Tool mode
		if (PC->IsInDrawRoomMode())
		{
			ToolMode = TEXT("Draw Room");
		}
		else if (PC->IsInDrawWallMode())
		{
			ToolMode = TEXT("Draw Wall");
		}
		else if (PC->IsInAddDoorMode())
		{
			ToolMode = TEXT("Add Door");
		}
		else if (PC->GetActivePrefabShape() != 0)
		{
			ToolMode = FString::Printf(
				TEXT("Prefab (%d)"),
				PC->GetActivePrefabShape()
			);
		}
		else if (PC->GetActiveConnectorType() != 0)
		{
			ToolMode = FString::Printf(
				TEXT("Connector (%d)"),
				PC->GetActiveConnectorType()
			);
		}
		else if (PC->IsInSelectMode())
		{
			ToolMode = TEXT("Select");
		}

		// View
		if (PC->IsIn3DPreviewMode())
		{
			ViewType = PC->IsIn3DEditMode()
				? TEXT("3D Edit")
				: TEXT("3D View");
		}

		// Selection
		const FString RoomId = PC->GetSelectedRoomId();
		const FString DoorId = PC->GetSelectedDoorId();
		const FString ConnectorId = PC->GetSelectedConnectorGroupId();

		if (!RoomId.IsEmpty())
		{
			SelectedRoom = RoomId;
		}

		if (!DoorId.IsEmpty())
		{
			SelectedDoor = DoorId;
		}

		if (!ConnectorId.IsEmpty())
		{
			SelectedConnector = ConnectorId;
		}

		// Unsaved state
		UnsavedChanges = PC->HasUnsavedChanges()
			? TEXT("Yes")
			: TEXT("No");
	}

	return FString::Printf(
		TEXT(
			"Building: %s\n"
			"Floor: %d / %d\n"
			"Tool Mode: %s\n"
			"View: %s\n"
			"Selected Room: %s\n"
			"Selected Door: %s\n"
			"Selected Connector: %s\n"
			"Unsaved Changes: %s"
		),
		*ActiveBuildingId,
		ActiveFloor,
		TotalFloors,
		*ToolMode,
		*ViewType,
		*SelectedRoom,
		*SelectedDoor,
		*SelectedConnector,
		*UnsavedChanges
	);
}

FReply UInteriorScreenWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const bool bOverAnyPanel = DockHost && DockHost->IsScreenPositionOverDock(InMouseEvent.GetScreenSpacePosition());

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

UWidget* UInteriorScreenWidget::FindScreenWidget(FName Name) const
{
	if (UWidget* Found = GetWidgetFromName(Name))
	{
		return Found;
	}
	if (WidgetTree)
	{
		if (UWidget* Found = FindObject<UWidget>(WidgetTree, *Name.ToString()))
		{
			return Found;
		}
	}
	// Shared panels keep their controls in their own widget trees.
	if (NavigationPanelWidget)
	{
		if (UWidget* Found = NavigationPanelWidget->FindContentWidget(Name))
		{
			return Found;
		}
	}
	if (AudioPanelWidget)
	{
		if (UWidget* Found = AudioPanelWidget->FindContentWidget(Name))
		{
			return Found;
		}
	}
	if (MapPanelWidget)
	{
		if (UWidget* Found = MapPanelWidget->FindContentWidget(Name))
		{
			return Found;
		}
	}
	if (PluginToolHost)
	{
		if (UWidget* Found = PluginToolHost->FindControl(Name)) return Found;
	}
	if (DebugPanelWidget)
	{
		if (UWidget* Found = DebugPanelWidget->FindContentWidget(Name))
		{
			return Found;
		}
	}
	return nullptr;
}

FVector2D UInteriorScreenWidget::GetCanvasSize() const
{
	FVector2D ReliableSize;
	if (GetReliableCanvasSize(ReliableSize))
	{
		return ReliableSize;
	}
	// The screen has not been painted yet (the saved layout is loaded before that). The raw viewport is in screen pixels, but the
	// layout works in UI units - pixels divided by the DPI scale (0.77 here) - so a layout loaded against the raw size would
	// come out that much too small.
	const FVector2D Viewport = UWidgetLayoutLibrary::GetViewportSize(this);
	const float DpiScale = UWidgetLayoutLibrary::GetViewportScale(this);
	return DpiScale > KINDA_SMALL_NUMBER ? Viewport / DpiScale : Viewport;
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
	LastKnownCanvasSize = LocalSize;
	return true;
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
	case EDockPanelId::Map:        return TEXT("Map");
	case EDockPanelId::Debug:      return TEXT("Debug");
	case EDockPanelId::Validation: return TEXT("Validation");
	default:                       return TEXT("");
	}
}

void UInteriorScreenWidget::SaveDockLayoutToDisk() const
{
	// The screen is about to be left (or the layout has just been changed): what the next screen takes over is this.
	if (bSharedPanelsTaken && DockHost)
	{
		DockSharedPanels::Publish(*DockHost->GetLayout());
	}
	// Sizes are stored as fractions of the canvas, so a layout saved against a guessed size (a screen that never got on screen,
	// as in a headless test) would replace the user's own with one that restores far too big or small.
	FVector2D Size;
	if (DockHost && (GetReliableCanvasSize(Size) || (LastKnownCanvasSize.X > 0.f && LastKnownCanvasSize.Y > 0.f)))
	{
		if (!GetReliableCanvasSize(Size))
		{
			Size = LastKnownCanvasSize;
		}
		InteriorDock::SaveToFile(*DockHost->GetLayout(), InteriorDock::UserLayoutPath(), Size);
	}
}






void UInteriorScreenWidget::HandleEditModeChanged()
{
	RefreshModeButtonHighlights();
}

void UInteriorScreenWidget::HandleBeforeProjectSave()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	// Only what differs from the model is applied, so a save with nothing pending adds no undo steps.
	if (RoomFieldsBox && RoomFieldsBox->GetVisibility() == ESlateVisibility::Visible && !PC->GetSelectedRoomId().IsEmpty())
	{
		const bool bNameChanged = RoomNameTextBox && RoomNameTextBox->GetText().ToString() != PC->GetSelectedRoomName();
		const bool bTagsChanged = RoomTagsTextBox && NavigationSearch::ParseTags(RoomTagsTextBox->GetText().ToString()) != PC->GetSelectedRoomTags();
		const bool bTypeChanged = PC->IsSelectedRoomClosedLoop() && RoomTypeComboBox && !RoomTypeComboBox->GetSelectedOption().IsEmpty()
			&& RoomTypeComboBox->GetSelectedOption() != PC->GetSelectedRoomType();
		if (bNameChanged || bTagsChanged || bTypeChanged || bRoomSizeDirty)
		{
			OnApplyRoomClicked();
		}
	}
	if (ConnectorFieldsBox && ConnectorFieldsBox->GetVisibility() == ESlateVisibility::Visible)
	{
		TArray<int32> Checked;
		for (int32 Index = 0; Index < FloorCheckboxes.Num(); ++Index)
		{
			const UCheckBox* CheckBox = FloorCheckboxes[Index];
			if (CheckBox && CheckBox->GetVisibility() != ESlateVisibility::Collapsed && CheckBox->IsChecked())
			{
				Checked.Add(Index + 1);
			}
		}
		TArray<int32> Current = PC->GetSelectedConnectorFloors();
		Current.Sort();
		if (!Checked.IsEmpty() && Checked != Current)
		{
			OnApplyConnectorClicked();
		}
	}
}

void UInteriorScreenWidget::HandleProjectSaved()
{
	SaveDockLayoutToDisk();
}

void UInteriorScreenWidget::HandleActiveFloorChanged()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>()) HandleInspectionModeChanged(PC->IsInteriorInspectionEnabled());
	RefreshFloorList();
	RefreshLayersList();
	RefreshNavigationPanelDisplay();
}

void UInteriorScreenWidget::HandleInteriorContentChanged()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>()) HandleInspectionModeChanged(PC->IsInteriorInspectionEnabled());
	RefreshLayersList();
	RefreshNavRoomDropdowns();
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (RoomTagsTextBox) RoomTagsTextBox->SetText(FText::FromString(FString::Join(PC->GetSelectedRoomTags(), TEXT(", "))));
		RefreshRoomTypeDisplay();
	}
}

void UInteriorScreenWidget::HandlePreviewModeChanged(bool bIsActive)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>()) HandleInspectionModeChanged(PC->IsInteriorInspectionEnabled());
	if (Background)
	{
		Background->SetVisibility(bIsActive ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (FloorPlanWidget)
	{
		FloorPlanWidget->SetVisibility(bIsActive ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	if (Switch2DButton)
	{
		Switch2DButton->SetStyle(Button_Style_Segment(!bIsActive));
	}
	if (Switch3DButton)
	{
		Switch3DButton->SetStyle(Button_Style_Segment(bIsActive));
	}
	if (CameraMenuButton)
	{
		CameraMenuButton->SetVisibility(bIsActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (!bIsActive)
	{
		// Leaving 3D preview takes the Camera menu away, so an open Camera dropdown must go with it.
		if (CameraMenuDropdown && CameraMenuDropdown->GetVisibility() != ESlateVisibility::Collapsed)
		{
			CloseMenuDropdowns();
		}
	}
	else
	{
		RefreshCameraSpeedDisplay();
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

void UInteriorScreenWidget::RefreshInspectionStyleButtons()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC) return;
	const ESlateVisibility Shown = PC->IsIn3DPreviewMode() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (InspectionFrame) InspectionFrame->SetVisibility(Shown);
	for (UButton* Button : { InspectionToggleButton, InspectionOrbitButton, InspectionGrabButton })
	{
		if (Button) Button->SetVisibility(Shown);
	}
	const bool bGrab = PC->GetInspectionRotateStyle() == EInspectionRotateStyle::Grab;
	if (InspectionOrbitButton)
	{
		InspectionOrbitButton->SetIsEnabled(PC->IsInteriorInspectionEnabled());
		InspectionOrbitButton->SetStyle(Button_Style_Segment(!bGrab));
		InspectionOrbitButton->SetToolTipText(FText::FromString(TEXT("Orbit: turning the model is like moving the camera around it, so it turns the other way from the mouse.")));
	}
	if (InspectionGrabButton)
	{
		InspectionGrabButton->SetIsEnabled(PC->IsInteriorInspectionEnabled());
		InspectionGrabButton->SetStyle(Button_Style_Segment(bGrab));
		InspectionGrabButton->SetToolTipText(FText::FromString(TEXT("Grab: the model follows the mouse when you turn it, as if you were holding it in your hand.")));
	}
}

void UInteriorScreenWidget::OnInspectionOrbitClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>()) PC->SetInspectionRotateStyle(EInspectionRotateStyle::Orbit);
	RefreshInspectionStyleButtons();
}

void UInteriorScreenWidget::OnInspectionGrabClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>()) PC->SetInspectionRotateStyle(EInspectionRotateStyle::Grab);
	RefreshInspectionStyleButtons();
}

void UInteriorScreenWidget::OnInspectionToggleClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>()) PC->SetInteriorInspectionEnabled(!PC->IsInteriorInspectionEnabled());
}

void UInteriorScreenWidget::HandleInspectionModeChanged(bool bEnabled)
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC) return;
	if (InspectionToggleButton)
	{
		InspectionToggleButton->SetIsEnabled(bEnabled || PC->CanInspectBuilding());
		InspectionToggleButton->SetStyle(Button_Style_Segment(bEnabled));
		InspectionToggleButton->SetToolTipText(FText::FromString(PC->CanInspectBuilding()
			? TEXT("Isolate: hide the bottom platform and the surroundings and look at the model on its own. Drag with the left button to move it, with the right button to turn it; scroll to zoom. Click again to restore the view.")
			: TEXT("This building has no interior floor to look at.")));
	}
	RefreshInspectionStyleButtons();
	if (InspectionHint)
	{
		InspectionHint->SetVisibility(bEnabled ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		InspectionHint->SetText(FText::FromString(FString::Printf(TEXT("Showing Level %d without the bottom platform. Left-drag to move the model, right-drag to turn it, scroll to zoom. Esc to restore."), PC->GetActiveFloor())));
	}
	if (!bEnabled && InspectionPivotOverlay) InspectionPivotOverlay->SetVisibility(ESlateVisibility::Collapsed);
	if (bEnabled && InspectionUiStates.IsEmpty())
	{
		auto Disable = [this](UWidget* Widget)
		{
			if (Widget) { InspectionUiStates.Add({ Widget, Widget->GetIsEnabled() }); Widget->SetIsEnabled(false); }
		};
		for (EDockPanelId Id : { EDockPanelId::Toolbar, EDockPanelId::Properties, EDockPanelId::Layers, EDockPanelId::Navigation, EDockPanelId::Map, EDockPanelId::Validation })
		{
			if (DockPanelBorders.IsValidIndex((int32)Id)) Disable(DockPanelBorders[(int32)Id]);
		}
		for (UWidget* Widget : TArray<UWidget*>{ AddFloorButton, RemoveFloorButton, UndoButton, RedoButton, Edit3DModeToggleButton,
			EditBuildingNameButton, CameraSpeedTextBox, IncreaseSpeedButton, DecreaseSpeedButton }) Disable(Widget);
	}
	else if (!bEnabled)
	{
		for (const auto& State : InspectionUiStates) if (UWidget* Widget = State.Key.Get()) Widget->SetIsEnabled(State.Value);
		InspectionUiStates.Reset();
	}
	RefreshFloorList();
}

void UInteriorScreenWidget::HandleEdit3DModeChanged(bool bIsEditMode)
{
	if (Edit3DModeToggleButtonText)
	{
		Edit3DModeToggleButtonText->SetText(FText::FromString(bIsEditMode ? TEXT("Edit") : TEXT("View")));
	}
	if (Edit3DModeToggleButton)
	{
		// View: amber. Edit: green.
		Edit3DModeToggleButton->SetStyle(Button_Style_Edit3D(bIsEditMode));
	}
}

void UInteriorScreenWidget::HandleSnapEnabledChanged(bool bNewSnapEnabled)
{
	if (SnapToggleIcon)
	{
		const TCHAR* AssetName = bNewSnapEnabled ? TEXT("Icon_SnappingOn") : TEXT("Icon_SnappingOff");
		const FString IconPath = FString::Printf(TEXT("/Game/ImageAssets/%s.%s"), AssetName, AssetName);
		if (UTexture2D* IconTexture = LoadObject<UTexture2D>(nullptr, *IconPath))
		{
			SnapToggleIcon->SetBrushFromTexture(IconTexture, false);
		}
	}
	if (SnapToggleButton)
	{
		SnapToggleButton->SetToolTip(MakeToolTipWidget(bNewSnapEnabled ? TEXT("Snapping ON [N]") : TEXT("Snapping OFF [N]")));
	}
}

void UInteriorScreenWidget::HandleRoomColoursChanged(bool bVisible)
{
	if (RoomColoursIcon)
	{
		// A rounded swatch: a room colour when the colours show, a flat grey when they do not.
		RoomColoursIcon->SetBrush(StartStyle::Rounded(bVisible ? RoomTypes::Color(TEXT("Classroom")) : StartStyle::Rgb(90, 100, 114), FLinearColor::Transparent, 4.f, 0.f));
		RoomColoursIcon->SetDesiredSizeOverride(FVector2D(16.f, 16.f));
	}
	if (RoomColoursButton)
	{
		RoomColoursButton->SetToolTip(MakeToolTipWidget(bVisible ? TEXT("Room colours ON") : TEXT("Room colours OFF")));
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
	if (RoomTagsFieldsSizeBox) RoomTagsFieldsSizeBox->SetWidthOverride(PropertiesButtonWidth);

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
	for (USizeBox* WidthBox : PropertiesWidthBoxes)
	{
		if (WidthBox)
		{
			WidthBox->SetWidthOverride(PropertiesButtonWidth);
		}
	}
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

	// Navigation: the shared panel sizes its own controls from the width it is given.
	if (NavigationPanelWidget)
	{
		NavigationPanelWidget->SetContentWidth(ComputePanelButtonWidth(EDockPanelId::Navigation));
	}

	if (PluginToolHost) PluginToolHost->Sync();

	// Audio: the shared panel sizes its own controls from the width it is given.
	if (AudioPanelWidget)
	{
		AudioPanelWidget->SetContentWidth(ComputePanelButtonWidth(EDockPanelId::Audio));
	}
	if (MapPanelWidget)
	{
		MapPanelWidget->SetContentWidth(ComputePanelButtonWidth(EDockPanelId::Map));
	}
}

void UInteriorScreenWidget::RefreshModeButtonHighlights()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	// The active tool's row is blue; the others are the plain row.
	auto SetHighlight = [](UButton* Button, bool bActive)
	{
		if (Button)
		{
			Button->SetStyle(Button_Style_Tool(bActive));
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

void UInteriorScreenWidget::HandleSelectedEntranceChanged(FString EntranceId)
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!DoorFieldsBox || !PC)
	{
		return;
	}
	if (EntranceId.IsEmpty())
	{
		// Another selection (or none) takes over; a door's own handler shows its fields again.
		if (PC->GetSelectedDoorId().IsEmpty())
		{
			DoorFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
		}
		RefreshPropertiesPanelVisibility();
		return;
	}

	const FInteriorEntrance* Entrance = PC->GetSelectedEntrance();
	if (!Entrance)
	{
		return;
	}
	// The door's fields are reused: what kind of door it is and where it leads, and Delete (no Connects to, nothing to save).
	DoorFieldsBox->SetVisibility(ESlateVisibility::Visible);
	if (RoomFieldsBox) RoomFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
	if (ConnectorFieldsBox) ConnectorFieldsBox->SetVisibility(ESlateVisibility::Collapsed);
	if (ConnectsToRowSizeBox) ConnectsToRowSizeBox->SetVisibility(ESlateVisibility::Collapsed);
	if (ApplyDoorButton) ApplyDoorButton->SetVisibility(ESlateVisibility::Collapsed);
	const bool bMain = Entrance->Kind == EInteriorEntranceKind::Main;
	if (PropertiesTitleText)
	{
		PropertiesTitleText->SetText(FText::FromString(bMain ? TEXT("Main door") : TEXT("Link door")));
	}
	SetKindTag(bMain ? TEXT("MAIN DOOR") : TEXT("LINK DOOR"), bMain ? InteriorDoorColors::MainDoor() : InteriorDoorColors::LinkDoor());
	if (PropertiesKindTag)
	{
		PropertiesKindTag->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (DoorRoomText)
	{
		DoorRoomText->SetText(FText::FromString(bMain
			? FString::Printf(TEXT("Floor %d. The way out: leads outside the building."), Entrance->Level)
			: FString::Printf(TEXT("Floor %d. Connects to building %s%s."), Entrance->Level, *PC->GetBuildingDisplayName(Entrance->ConnectsTo),
				Entrance->bMirrored ? TEXT(" (opened from that building)") : TEXT(""))));
	}
	RefreshPropertiesPanelVisibility();
}

void UInteriorScreenWidget::HandleSelectedDoorChanged(FString RoomId, FString DoorId)
{
	// A room's door has the Connects to field and Save changes, which an outer-wall door's selection hides.
	if (ConnectsToRowSizeBox) ConnectsToRowSizeBox->SetVisibility(ESlateVisibility::Visible);
	if (ApplyDoorButton) ApplyDoorButton->SetVisibility(ESlateVisibility::Visible);
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
		PropertiesTitleText->SetText(FText::FromString(TEXT("Door properties")));
	}
	if (PropertiesKindText)
	{
		SetKindTag(TEXT("DOOR"), InteriorDoorColors::RoomDoor());
	}
	if (PropertiesKindTag)
	{
		PropertiesKindTag->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	if (DoorRoomText)
	{
		DoorRoomText->SetText(FText::FromString(FString::Printf(TEXT("Door belongs to: %s"), *PC->ResolveConnectsToDisplayName(RoomId))));
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
		PropertiesTitleText->SetText(FText::FromString(PC->IsSelectedRoomClosedLoop() ? TEXT("Room properties") : TEXT("Wall properties")));
	}
	if (PropertiesKindText)
	{
		if (PC->IsSelectedRoomClosedLoop())
		{
			SetKindTag(PC->GetSelectedRoomType().ToUpper(), RoomTypes::Color(PC->GetSelectedRoomType()));
		}
		else
		{
			SetKindTag(TEXT("WALL"), StartStyle::Rgb(60, 78, 104));
		}
	}
	if (PropertiesKindTag)
	{
		PropertiesKindTag->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (RoomDeleteLabel)
	{
		RoomDeleteLabel->SetText(FText::FromString(PC->IsSelectedRoomClosedLoop() ? TEXT("Delete room") : TEXT("Delete wall")));
	}
	if (RoomSubtitleText)
	{
		const FString Name = PC->GetSelectedRoomName();
		RoomSubtitleText->SetText(FText::FromString(Name.IsEmpty() ? PC->GetSelectedRoomId() : Name));
	}
	if (RoomNameTextBox)
	{
		RoomNameTextBox->SetText(FText::FromString(PC->GetSelectedRoomName()));
	}
	if (RoomTagsFields) RoomTagsFields->SetVisibility(PC->IsSelectedRoomClosedLoop() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (RoomTagsTextBox) RoomTagsTextBox->SetText(FText::FromString(FString::Join(PC->GetSelectedRoomTags(), TEXT(", "))));
	RefreshRoomTypeDisplay();

	RefreshRoomBoundsFields();
	RefreshPropertiesPanelVisibility();
}

void UInteriorScreenWidget::OnApplyRoomClicked()
{
	// Capture every property before resizing or renaming can refresh the panel.
	const FString NewName = RoomNameTextBox ? RoomNameTextBox->GetText().ToString() : FString();
	const FString NewTags = RoomTagsTextBox ? RoomTagsTextBox->GetText().ToString() : FString();
	const FString NewType = RoomTypeComboBox ? RoomTypeComboBox->GetSelectedOption() : FString();
	if (!ApplyRoomDimensions())
	{
		bRoomSizeDirty = false;
		RefreshRoomBoundsFields();
	}
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (RoomTypeComboBox)
		{
			PC->SetSelectedRoomType(NewType);
		}
		if (RoomNameTextBox)
		{
			PC->SetSelectedRoomName(NewName);
		}
	}
	ApplyRoomTags(NewTags);
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (RoomSubtitleText)
		{
			const FString Name = PC->GetSelectedRoomName();
			RoomSubtitleText->SetText(FText::FromString(Name.IsEmpty() ? PC->GetSelectedRoomId() : Name));
		}
		if (PropertiesKindText && PC->IsSelectedRoomClosedLoop())
		{
			SetKindTag(PC->GetSelectedRoomType().ToUpper(), RoomTypes::Color(PC->GetSelectedRoomType()));
		}
	}
}

void UInteriorScreenWidget::RefreshRoomTypeDisplay()
{
	const ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (PC && RoomTypeComboBox && PC->IsSelectedRoomClosedLoop())
	{
		const FString Type = PC->GetSelectedRoomType();
		if (RoomTypeComboBox->GetSelectedOption() != Type)
		{
			RoomTypeComboBox->SetSelectedOption(Type);
		}
	}
}

void UInteriorScreenWidget::ApplyRoomTags(const FString& TagsText)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (PC->SetSelectedRoomTags(NavigationSearch::ParseTags(TagsText)) && RoomTagsTextBox)
		{
			RoomTagsTextBox->SetText(FText::FromString(FString::Join(PC->GetSelectedRoomTags(), TEXT(", "))));
		}
	}
}

void UInteriorScreenWidget::OnDeleteSelectionClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->DeleteSelection();
	}
}

void UInteriorScreenWidget::RefreshRoomBoundsFields()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	const FString RoomId = PC ? PC->GetSelectedRoomId() : FString();
	if (RoomId != BoundsRoomId) { bRoomSizeDirty = false; BoundsRoomId = RoomId; }
	TGuardValue<bool> Updating(bUpdatingRoomBounds, true);
	FVector2D Position = FVector2D::ZeroVector, Size = FVector2D::ZeroVector;
	const bool bHave = PC && PC->GetSelectedRoomBounds(Position, Size);
	const bool bReadOnly = !bHave || PC->IsInteriorInspectionEnabled() || (PC->IsIn3DPreviewMode() && !PC->IsIn3DEditMode());
	if (RoomWidthBox) RoomWidthBox->SetIsReadOnly(bReadOnly || (!PC->IsSelectedRoomClosedLoop() && FMath::IsNearlyZero(Size.X)));
	if (RoomLengthBox) RoomLengthBox->SetIsReadOnly(bReadOnly || (!PC->IsSelectedRoomClosedLoop() && FMath::IsNearlyZero(Size.Y)));
	auto Show = [bHave](UEditableTextBox* Box, double Value)
	{
		if (Box)
		{
			const FString Text = bHave ? FString::SanitizeFloat(Value) : FString();
			if (Box->GetText().ToString() != Text)
			{
				Box->SetText(FText::FromString(Text));
			}
		}
	};
	Show(RoomXBox, Position.X);
	Show(RoomYBox, Position.Y);
	if (!bRoomSizeDirty)
	{
		const double MetresPerUnit = PC ? PC->GetBuildingScale() / 100.0 : 1.0;
		Show(RoomWidthBox, Size.X * MetresPerUnit);
		Show(RoomLengthBox, Size.Y * MetresPerUnit);
		DisplayedRoomWidth = RoomWidthBox ? RoomWidthBox->GetText().ToString() : FString();
		DisplayedRoomLength = RoomLengthBox ? RoomLengthBox->GetText().ToString() : FString();
	}
}

// Unlike LexTryParseString(double), require the whole field to be a decimal number.
static bool InteriorScreen_ParseDimension(const FString& Input, double& Value)
{
	const FString Text = Input.TrimStartAndEnd();
	if (Text.IsEmpty()) return false;
	for (const TCHAR C : Text) if (!FChar::IsDigit(C) && C != '.' && C != '+' && C != '-' && C != 'e' && C != 'E') return false;
	return FDefaultValueHelper::ParseDouble(Text, Value) && FMath::IsFinite(Value);
}

bool UInteriorScreenWidget::ApplyRoomDimensions()
{
	if (!bRoomSizeDirty) return true;
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC) return false;
	if (BoundsRoomId != PC->GetSelectedRoomId()) { bRoomSizeDirty = false; RefreshRoomBoundsFields(); return false; }
	FVector2D Position, Current;
	if (!PC->GetSelectedRoomBounds(Position, Current)) return false;
	const double MetresPerUnit = PC->GetBuildingScale() / 100.0;
	double Width = Current.X * MetresPerUnit, Length = Current.Y * MetresPerUnit;
	const bool bRoom = PC->IsSelectedRoomClosedLoop();
	if (!RoomWidthBox || !RoomLengthBox ||
		(RoomWidthBox->GetText().ToString() != DisplayedRoomWidth && !InteriorScreen_ParseDimension(RoomWidthBox->GetText().ToString(), Width)) ||
		(RoomLengthBox->GetText().ToString() != DisplayedRoomLength && !InteriorScreen_ParseDimension(RoomLengthBox->GetText().ToString(), Length)) || Width < 0 || Length < 0 ||
		(bRoom ? (Width < 0.5 || Length < 0.5) : FMath::Max(Width, Length) < 0.5))
	{
		PC->OnActionFailed.Broadcast(TEXT("Enter numeric dimensions in metres. Rooms need at least 0.5 m on each side; straight walls keep their zero dimension."), EActionFailureSeverity::Warning);
		return false;
	}
	FVector2D Size = Current;
	if (RoomWidthBox->GetText().ToString() != DisplayedRoomWidth) Size.X = Width / MetresPerUnit;
	if (RoomLengthBox->GetText().ToString() != DisplayedRoomLength) Size.Y = Length / MetresPerUnit;
	if (!Current.Equals(Size, 1e-6) && !PC->ResizeRoomToBounds(PC->GetSelectedRoomId(), Position + (Current - Size) * 0.5, Size)) return false;
	bRoomSizeDirty = false;
	RefreshRoomBoundsFields();
	return true;
}

void UInteriorScreenWidget::OnRoomSizeTextCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	if (bUpdatingRoomBounds) return;
	if (CommitType == ETextCommit::OnEnter) ApplyRoomDimensions();
	else if (CommitType == ETextCommit::OnUserMovedFocus)
	{
		if (!ApplyRoomDimensions()) { bRoomSizeDirty = false; RefreshRoomBoundsFields(); }
	}
	else if (CommitType == ETextCommit::OnCleared) { bRoomSizeDirty = false; RefreshRoomBoundsFields(); }
}

void UInteriorScreenWidget::OnRoomSizeTextChanged(const FText& Text)
{
	if (!bUpdatingRoomBounds) bRoomSizeDirty =
		(RoomWidthBox && RoomWidthBox->GetText().ToString() != DisplayedRoomWidth) ||
		(RoomLengthBox && RoomLengthBox->GetText().ToString() != DisplayedRoomLength);
}

void UInteriorScreenWidget::OnResizeStepChanged(float Value)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (PC->GetSelectedConnectorGroupId().IsEmpty()) PC->SetRoomResizeStepMetres(Value);
		else PC->SetConnectorResizeStepPercent(Value);
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
		PropertiesTitleText->SetText(FText::FromString(TEXT("Connector properties")));
	}
	if (PropertiesKindText)
	{
		SetKindTag(TEXT("CONNECTOR"), StartStyle::Rgb(60, 78, 104));
	}
	if (PropertiesKindTag)
	{
		PropertiesKindTag->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
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
		ConnectorTypeText->SetText(FText::FromString(TypeName));
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
	// Properties is a permanent panel: with nothing selected it just shows an empty "Properties"
	// placeholder instead of disappearing, like a real editor's Details panel. Showing or hiding it is the
	// dock system's business (Window menu / tab close button), not selection state's.
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	const bool bAnySelected = PC && (!PC->GetSelectedRoomId().IsEmpty() || !PC->GetSelectedDoorId().IsEmpty() || !PC->GetSelectedConnectorGroupId().IsEmpty() || !PC->GetSelectedEntranceId().IsEmpty());
	if (!bAnySelected && PropertiesTitleText)
	{
		PropertiesTitleText->SetText(FText::FromString(TEXT("Nothing selected")));
	}
	if (!bAnySelected && PropertiesKindTag)
	{
		PropertiesKindTag->SetVisibility(ESlateVisibility::Collapsed);
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
	const float FloorHeightCm = PC->GetLoadedBuildingFloorHeight();
	if (FloorCountText)
	{
		FloorCountText->SetText(FText::AsNumber(FloorCount));
	}
	// Lowest floor first (Level 1 at the top) - reversed from the original mockup per explicit
	// request.
	for (int32 FloorNumber = 1; FloorNumber <= FloorCount; ++FloorNumber)
	{
		UFloorListEntryWidget* Entry = CreateWidget<UFloorListEntryWidget>(PC, UFloorListEntryWidget::StaticClass());
		if (!Entry)
		{
			continue;
		}
		Entry->SetFloorNumber(FloorNumber, FloorNumber == ActiveFloor, (FloorNumber - 1) * FloorHeightCm / 100.f);
		Entry->SetWidth(ComputePanelButtonWidth(EDockPanelId::Floor));
		Entry->OnEntryClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleFloorEntryClicked);

		if (UScrollBoxSlot* EntrySlot = Cast<UScrollBoxSlot>(FloorListBox->AddChild(Entry)))
		{
			EntrySlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		}
	}
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
	auto AddRow = [this, PC, RowWidth](const FString& RowKey, const FString& Label, float IndentPx, bool bShowExpandArrow, bool bIsExpanded, bool bIsVisible, bool bSearchResult = false, int32 Count = -1)
	{
		ULayerRowWidget* Row = CreateWidget<ULayerRowWidget>(PC, ULayerRowWidget::StaticClass());
		if (!Row)
		{
			return;
		}
		Row->Setup(RowKey, Label, IndentPx, bShowExpandArrow, bIsExpanded, bIsVisible, Count);
		Row->SetWidth(RowWidth);
		Row->OnExpandClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleLayerRowExpandClicked);
		Row->OnVisibilityClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleLayerRowVisibilityClicked);
		Row->OnLabelClicked.AddUniqueDynamic(this, &UInteriorScreenWidget::HandleLayerRowLabelClicked);
		Row->SetSearchResult(bSearchResult);
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(LayersListBox->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
		}
	};

	const int32 ActiveFloor = PC->GetActiveFloor();
	if (LayersLevelText)
	{
		LayersLevelText->SetText(FText::FromString(FString::Printf(TEXT("LEVEL %d"), ActiveFloor)));
	}

	// How many of each kind the active floor has, shown on the category rows.
	int32 RoomCount = 0, WallCount = 0, PathNodeCount = 0, IONodeCount = 0;
	for (const FString& RoomId : PC->GetRoomIdsOnFloor(ActiveFloor))
	{
		(PC->IsRoomClosedLoop(RoomId) ? RoomCount : WallCount)++;
	}
	for (const FInteriorNavNode& Node : PC->GetNavNodes())
	{
		if (Node.Level == ActiveFloor)
		{
			(Node.Type == EInteriorNavNodeType::PathNode ? PathNodeCount : IONodeCount)++;
		}
	}

	// -- Search results -- while something is typed in the search box, every room of the building (all
	// floors, not just the active one) whose name, id or tags match is listed first. Clicking one switches to
	// its floor and selects the room.
	if (!SearchFilter.IsEmpty())
	{
		TArray<FString> Words;
		SearchFilter.ParseIntoArrayWS(Words);
		struct FResult
		{
			int32 Floor;
			FString RoomId;
			FString Label;
		};
		TArray<FResult> Results;
		for (int32 Floor = 1; Floor <= PC->GetLoadedBuildingFloorCount(); ++Floor)
		{
			for (const FString& RoomId : PC->GetRoomIdsOnFloor(Floor))
			{
				if (!PC->IsRoomClosedLoop(RoomId))
				{
					continue;
				}
				const FString RoomName = PC->GetRoomName(RoomId);
				const TArray<FString> Tags = PC->GetRoomTags(RoomId);
				const FString RoomType = PC->GetRoomType(RoomId);
				const bool bTyped = RoomType != RoomTypes::Default();
				const FString Searchable = RoomName + TEXT(" ") + RoomId + TEXT(" ") + FString::Join(Tags, TEXT(" ")) + (bTyped ? TEXT(" ") + RoomType : FString());
				if (!NavigationSearch::MatchesWords(Words, Searchable))
				{
					continue;
				}
				FString Label = FString::Printf(TEXT("%s [%s] - Floor %d"), RoomName.IsEmpty() ? *RoomId : *RoomName, *RoomId, Floor);
				if (bTyped)
				{
					Label += TEXT(" - ") + RoomType;
				}
				if (!Tags.IsEmpty())
				{
					Label += TEXT(" - ") + FString::Join(Tags, TEXT(", "));
				}
				Results.Add({ Floor, RoomId, Label });
			}
		}
		AddRow(TEXT("category:SearchResults"), FString::Printf(TEXT("Search results (%d)"), Results.Num()), 0.f, false, false, true, true);
		for (const FResult& Result : Results)
		{
			AddRow(FString::Printf(TEXT("result:%d:%s"), Result.Floor, *Result.RoomId), Result.Label, ItemIndent, false, false, true, true);
		}
	}

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
		AddRow(TEXT("category:Rooms"), TEXT("Rooms"), 0.f, true, bExpanded, PC->AreRoomsVisible(), false, RoomCount);
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
		AddRow(TEXT("category:Walls"), TEXT("Walls"), 0.f, true, bExpanded, PC->AreWallsVisible(), false, WallCount);
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
		AddRow(TEXT("category:PathNodes"), TEXT("Path Nodes"), 0.f, true, bExpanded, PC->ArePathNodesVisible(), false, PathNodeCount);
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
		AddRow(TEXT("category:IONodes"), TEXT("I/O Nodes"), 0.f, true, bExpanded, PC->AreIONodesVisible(), false, IONodeCount);
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

void UInteriorScreenWidget::HandleLayerRowLabelClicked(FString RowKey)
{
	// "result:<floor>:<room id>"
	FString Rest;
	if (!RowKey.Split(TEXT("result:"), nullptr, &Rest) || Rest.IsEmpty())
	{
		return;
	}
	FString FloorText, RoomId;
	if (!Rest.Split(TEXT(":"), &FloorText, &RoomId) || RoomId.IsEmpty())
	{
		return;
	}
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		const int32 Floor = FCString::Atoi(*FloorText);
		if (Floor > 0 && Floor != PC->GetActiveFloor())
		{
			PC->SetActiveFloor(Floor);
		}
		PC->SelectRoom(RoomId);
	}
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

bool UInteriorScreenWidget::HandOverToCampusForStep(const FNavigationRoutePoint& Point)
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC || Point.Floor != UNavigationPanelWidget::OutdoorFloor)
	{
		return false;
	}
	// Going outside: the campus is the exterior screen. What was edited here stays (in the project data, still unsaved), as when
	// another building is opened; the route and the step are the panel's, and stay.
	PC->SyncLoadedInterior();
	GoToEditScreen();
	// The exterior screen is up: the campus is what the panel shows, and the camera goes to the step.
	if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
	{
		Nav->SetOutdoorMode(true);
	}
	PC->FollowNavigationStep();
	return true;
}

void UInteriorScreenWidget::HandleGuidedStepChanged()
{
	// A step can be in another building or on another floor, and the plan may have been zoomed or moved for the last one.
	if (FloorPlanWidget)
	{
		FloorPlanWidget->ResetView();
	}
}

void UInteriorScreenWidget::HandleNavigationStateChanged()
{
	RefreshNavigationPanelDisplay();
}

void UInteriorScreenWidget::RefreshNavigationPanelDisplay()
{
	if (NavigationPanelWidget)
	{
		NavigationPanelWidget->RefreshDisplay();
	}
}

void UInteriorScreenWidget::RefreshNavRoomDropdowns()
{
	if (NavigationPanelWidget)
	{
		NavigationPanelWidget->RefreshRoomDropdowns();
	}
}

void UInteriorScreenWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
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

	SwitchToBuilding(PC->FindBuildingIdByName(SelectedItem));
}

void UInteriorScreenWidget::RefreshAfterBuildingSwitch()
{
	RefreshBuildingSwitcher();
	RefreshFloorList();
	RefreshLayersList();
	RefreshNavigationPanelDisplay();
	RefreshNavRoomDropdowns();
	if (MapPanelWidget)
	{
		MapPanelWidget->RefreshBuildingFilterDisplay();
	}
}

void UInteriorScreenWidget::SwitchToBuilding(const FString& NewBuildingId)
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC || NewBuildingId.IsEmpty() || NewBuildingId == PC->GetLoadedBuildingId())
	{
		return;
	}

	if (!PC->HasUnsavedChanges())
	{
		// Same reasoning as GoToEditScreen - the dock layout is a local preference, not project
		// data, so it's saved regardless of whether there was any actual building content to save.
		SaveDockLayoutToDisk();
		PC->LoadInteriorForBuilding(NewBuildingId);
		RefreshAfterBuildingSwitch();
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
		if (!PC->SaveCurrentProject())
		{
			// Not saved (cancelled or failed): stay in this building, as Cancel does.
			HandleSwitchBuildingCancel();
			return;
		}
		PC->LoadInteriorForBuilding(PendingBuildingId);
	}
	RefreshAfterBuildingSwitch();
}

void UInteriorScreenWidget::HandleSwitchBuildingDiscard()
{
	// Discarding the building's own content edits shouldn't also lose the panel layout - see
	// GoToEditScreen's comment.
	SaveDockLayoutToDisk();
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		const FString PreviousBuildingId = PC->GetLoadedBuildingId();
		PC->DiscardUnsavedChanges();
		if (PC->LoadInteriorForBuildingAfterDiscard(PendingBuildingId) < 0 &&
			PC->LoadInteriorForBuildingAfterDiscard(PreviousBuildingId) < 0)
		{
			GoToEditScreen();
			return;
		}
	}
	RefreshAfterBuildingSwitch();
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

UWidget* UInteriorScreenWidget::HandleGenerateRoomTypeItem(FString Item)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	USizeBox* SquareSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	SquareSize->SetWidthOverride(10.f * StartStyle::DesignToUi);
	SquareSize->SetHeightOverride(10.f * StartStyle::DesignToUi);
	UBorder* Square = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Square->SetBrush(StartStyle::Rounded(RoomTypes::Color(Item), FLinearColor::Transparent, 3.f, 0.f));
	Square->SetBrushColor(FLinearColor::White);
	SquareSize->SetContent(Square);
	UHorizontalBoxSlot* SquareSlot = Row->AddChildToHorizontalBox(SquareSize);
	SquareSlot->SetVerticalAlignment(VAlign_Center);
	SquareSlot->SetPadding(FMargin(0.f, 0.f, 8.f * StartStyle::DesignToUi, 0.f));
	UTextBlock* ItemText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	ItemText->SetText(FText::FromString(Item));
	ItemText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ApplyStandardFont(ItemText);
	Row->AddChildToHorizontalBox(ItemText)->SetVerticalAlignment(VAlign_Center);
	return RetainComboItem(Row);
}

void UInteriorScreenWidget::SetKindTag(const FString& Label, const FLinearColor& OutlineColor)
{
	if (PropertiesKindText)
	{
		PropertiesKindText->SetText(FText::FromString(Label));
	}
	if (PropertiesKindTag)
	{
		PropertiesKindTag->SetBrush(StartStyle::Rounded(StartStyle::Rgb(14, 24, 38), OutlineColor, 5.f));
	}
}

UWidget* UInteriorScreenWidget::RetainComboItem(UWidget* Item)
{
	constexpr int32 MaxRetained = 400;
	RetainedComboItems.Add(Item);
	if (RetainedComboItems.Num() > MaxRetained)
	{
		RetainedComboItems.RemoveAt(0, RetainedComboItems.Num() - MaxRetained);
	}
	return Item;
}

UWidget* UInteriorScreenWidget::HandleGenerateComboBoxItem(FString Item)
{
	UTextBlock* ItemText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	ItemText->SetText(FText::FromString(Item));
	ItemText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ApplyStandardFont(ItemText);
	return RetainComboItem(ItemText);
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

void UInteriorScreenWidget::OnSaveClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SaveCurrentProject();
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

void UInteriorScreenWidget::OnCameraMenuClicked()
{
	if (!CameraMenuDropdown)
	{
		return;
	}
	if (CameraMenuDropdown->GetVisibility() != ESlateVisibility::Collapsed)
	{
		CloseMenuDropdowns();
	}
	else
	{
		RefreshCameraSpeedDisplay();
		OpenMenuDropdown(CameraMenuDropdown);
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

void UInteriorScreenWidget::OnRoomColoursClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ToggleRoomColours();
	}
}

void UInteriorScreenWidget::OnSnapToggleClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ToggleSnapEnabled();
	}
}

void UInteriorScreenWidget::OnUndoClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->UndoInteriorEdit();
	}
}

void UInteriorScreenWidget::OnRedoClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->RedoInteriorEdit();
	}
}

void UInteriorScreenWidget::OnResetView3DClicked()
{
	CloseMenuDropdowns();
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
	RefreshCameraSpeedDisplay();
}

void UInteriorScreenWidget::OnIncreaseSpeedClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->IncreaseMoveSpeed3DPreview();
	}
	RefreshCameraSpeedDisplay();
}

void UInteriorScreenWidget::OnCameraSpeedTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType != ETextCommit::OnEnter && CommitType != ETextCommit::OnUserMovedFocus)
	{
		return;
	}
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetMoveSpeed3DPreview(FCString::Atof(*NewText.ToString()));
	}
	RefreshCameraSpeedDisplay();
}

void UInteriorScreenWidget::RefreshCameraSpeedDisplay()
{
	if (!CameraSpeedTextBox)
	{
		return;
	}
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		CameraSpeedTextBox->SetText(FText::FromString(FString::FromInt(FMath::RoundToInt(PC->GetCameraMoveSpeed()))));
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
	if (CameraMenuDropdown)
	{
		CameraMenuDropdown->SetVisibility(Dropdown == CameraMenuDropdown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
	if (CameraMenuDropdown)
	{
		CameraMenuDropdown->SetVisibility(ESlateVisibility::Collapsed);
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
		if (!PC->SaveCurrentProject())
		{
			return;
		}
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

void UInteriorScreenWidget::OnFileGenerateFloorClicked()
{
	CloseMenuDropdowns();
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		FString Message;
		PC->GenerateFloorForActiveFloor(Message);
	}
}

void UInteriorScreenWidget::OnFileSaveAsClicked()
{
	CloseMenuDropdowns();
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>()) PC->SaveCurrentProjectToNewLocation();
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
		if (PC->TryLoadBuildingDataFromPathOrDatabase(FilePath))
		{
			GoToEditScreen();
		}
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
		if (PC->SaveCurrentProject() && PC->TryLoadBuildingDataFromPathOrDatabase(PendingLoadFilePath))
		{
			GoToEditScreen();
		}
	}
}

void UInteriorScreenWidget::HandleFileLoadDiscard()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (PC->TryLoadBuildingDataFromPathOrDatabase(PendingLoadFilePath))
		{
			GoToEditScreen();
		}
	}
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
		if (AudioPanelWidget)
		{
			AudioPanelWidget->RefreshAudioList();
		}
	}
}

void UInteriorScreenWidget::OnWindowMapCheckChanged(bool bIsChecked)
{
	SetDockPanelUserHidden(EDockPanelId::Map, !bIsChecked);
}
void UInteriorScreenWidget::OnWindowDebugCheckChanged(bool bIsChecked)
{
	SetDockPanelUserHidden(EDockPanelId::Debug, !bIsChecked);
}

void UInteriorScreenWidget::ShowValidationPanel()
{
	SetDockPanelUserHidden(EDockPanelId::Validation, false);
	SetDockPanelUserHidden(EDockPanelId::Properties, false);
	if (FloorPlanWidget) FloorPlanWidget->ResetView();
}

void UInteriorScreenWidget::SetDockPanelUserHidden(EDockPanelId Id, bool bHidden)
{
	if (!DockHost)
	{
		return;
	}
	const FName Panel(GetDockPanelName(Id));
	if (bHidden)
	{
		DockHost->GetLayout()->HidePanel(Panel);
	}
	else
	{
		// Comes back where it was (or at its default edge if it was never placed).
		DockHost->GetLayout()->ShowPanel(Panel, InteriorDock::DefaultSideFor(Panel));
	}
	DockHost->NotifyLayoutChanged();
	CancelNavigationPickIfNavigationHidden();
	RefreshWindowMenuChecks();
}

void UInteriorScreenWidget::OnWindowResetLayoutClicked()
{
	CloseMenuDropdowns();
	ResetDockLayout();
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
		// A cancelled "Save as" or a failed write (which says why) keeps the project open.
		if (!PC->SaveCurrentProject())
		{
			return;
		}
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
