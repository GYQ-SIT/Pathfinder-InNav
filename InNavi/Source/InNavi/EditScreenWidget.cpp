#include "EditScreenWidget.h"

#include "AudioFileRowWidget.h"
#include "ConfirmDialogWidget.h"
#include "DockHostUMG.h"
#include "ExteriorDockLayout.h"
#include "InteriorScreenWidget.h"
#include "NavPlayerController.h"
#include "StartScreenWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
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

namespace
{
	// Same look as UInteriorScreenWidget's own menu bar/dropdown/dock-panel chrome (DockPanelColor,
	// MenuBarHeight) - kept as separate, distinctly-named local constants here rather than shared
	// (Edit-prefixed to avoid colliding with UInteriorScreenWidget::RebuildWidget's own identically-
	// named local variables under Unreal's Unity Build, which merges multiple .cpp files into one
	// translation unit - two unrelated same-named locals across files then trip C4459 "hides global
	// declaration"), since this screen has no dock system of its own to also key off them.
	const FLinearColor EditMenuBarColor(0.03f, 0.03f, 0.03f, 0.88f);
	// Matches UInteriorScreenWidget::StandardTextBoxBackgroundColor/StandardTextBoxTextColor exactly -
	// see that pair's own comment for why (engine default editable-text-box colors read as nearly the
	// same gray against this app's dark chrome). Duplicated rather than shared for the same Unity
	// Build reason as EditMenuBarColor above.
	const FLinearColor EditTextBoxBackgroundColor(0.149f, 0.149f, 0.169f, 1.f);
	const FLinearColor EditTextBoxTextColor(1.f, 1.f, 1.f, 1.f);
	constexpr float EditMenuBarHeight = 40.f;
	constexpr float EditMenuButtonWidth = 50.f;
	constexpr float EditMenuBarLeftMargin = 10.f;
	constexpr float EditAudioPanelWidth = 220.f;
	constexpr float EditAudioFileListHeight = 150.f;

	// Second bar under the menu bar, matching UInteriorScreenWidget's own BuildingSwitcherBarHeight -
	// see the toolbar row's own construction comment in RebuildWidget for why it exists here.
	constexpr float ExteriorToolbarBarHeight = 52.f;
}

UEditScreenWidget::UEditScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UEditScreenWidget::ApplyStandardFont(UTextBlock* TextBlock)
{
	if (!TextBlock)
	{
		return;
	}
	FSlateFontInfo Font = TextBlock->GetFont();
	Font.Size = StandardFontSize;
	TextBlock->SetFont(Font);
}

void UEditScreenWidget::ApplyStandardFont(UEditableTextBox* TextBox)
{
	if (!TextBox)
	{
		return;
	}
	FEditableTextBoxStyle Style = TextBox->GetWidgetStyle();
	Style.TextStyle.Font.Size = StandardFontSize;
	Style.BackgroundColor = FSlateColor(EditTextBoxBackgroundColor);
	Style.ForegroundColor = FSlateColor(EditTextBoxTextColor);
	Style.FocusedForegroundColor = FSlateColor(EditTextBoxTextColor);
	TextBox->SetWidgetStyle(Style);
}

TSharedRef<SWidget> UEditScreenWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("EditScreenCanvas"));
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Canvas;

		// --- Top menu bar: File (New/Load) + Window (Audio) + Enter Interior/Back, in that order,
		// right-aligned - mirrors UInteriorScreenWidget's own menu bar construction exactly (see its
		// RebuildWidget), just without the BuildingSwitcherBar/dock-panel machinery this screen has no
		// use for. Enter Interior and Back used to live in a standalone bottom-right button stack;
		// they now live in this bar instead, Enter Interior immediately left of Back.
		UBorder* MenuBarBackground = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_MenuBar"));
		MenuBarBackground->SetBrushColor(EditMenuBarColor);
		MenuBarBackground->SetPadding(FMargin(EditMenuBarLeftMargin, 0.f, 0.f, 0.f));
		UCanvasPanelSlot* MenuBarSlot = Canvas->AddChildToCanvas(MenuBarBackground);
		MenuBarSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
		MenuBarSlot->SetAlignment(FVector2D(0.f, 0.f));
		MenuBarSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, EditMenuBarHeight));
		MenuBarSlot->SetZOrder(6);

		UHorizontalBox* MenuBarRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MenuBarRow"));
		MenuBarBackground->SetContent(MenuBarRow);

		auto AddMenuButton = [this, MenuBarRow](const TCHAR* Name, const TCHAR* Label) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f)); // transparent
			Button->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::PlayButtonClickSound);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Text->SetJustification(ETextJustify::Center);
			ApplyStandardFont(Text);
			USizeBox* MenuButtonSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			MenuButtonSizeBox->SetWidthOverride(EditMenuButtonWidth);
			MenuButtonSizeBox->SetHeightOverride(EditMenuBarHeight);
			if (USizeBoxSlot* TextSlot = Cast<USizeBoxSlot>(MenuButtonSizeBox->SetContent(Text)))
			{
				TextSlot->SetHorizontalAlignment(HAlign_Center);
				TextSlot->SetVerticalAlignment(VAlign_Center);
			}
			Button->SetContent(MenuButtonSizeBox);
			MenuBarRow->AddChildToHorizontalBox(Button);
			return Button;
		};

		// Save icon button, left of File - see UInteriorScreenWidget::OnSaveClicked's own comment
		// (mirrors it exactly, just without that class's MakeToolTipWidget helper).
		SaveButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_Save"));
		SaveButton->SetBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		SaveButton->SetToolTipText(FText::FromString(TEXT("Save [Ctrl+S]")));
		SaveButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnSaveClicked);
		SaveButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::PlayButtonClickSound);
		UImage* SaveIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Img_Save"));
		if (UTexture2D* SaveTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/ImageAssets/Icon_Save.Icon_Save")))
		{
			SaveIcon->SetBrushFromTexture(SaveTexture, false);
			SaveIcon->SetDesiredSizeOverride(FVector2D(22.f, 22.f));
		}
		USizeBox* SaveSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SaveSizeBox->SetWidthOverride(EditMenuBarHeight);
		SaveSizeBox->SetHeightOverride(EditMenuBarHeight);
		if (USizeBoxSlot* SaveIconSlot = Cast<USizeBoxSlot>(SaveSizeBox->SetContent(SaveIcon)))
		{
			SaveIconSlot->SetHorizontalAlignment(HAlign_Center);
			SaveIconSlot->SetVerticalAlignment(VAlign_Center);
		}
		SaveButton->SetContent(SaveSizeBox);
		MenuBarRow->AddChildToHorizontalBox(SaveButton);

		FileMenuButton = AddMenuButton(TEXT("Btn_FileMenu"), TEXT("File"));
		FileMenuButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnFileMenuClicked);
		WindowMenuButton = AddMenuButton(TEXT("Btn_WindowMenu"), TEXT("Window"));
		WindowMenuButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowMenuClicked);

		auto AddBarButton = [this, MenuBarRow](const TCHAR* Name, const TCHAR* Label, float Width) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString(Name) + TEXT("Label")));
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetJustification(ETextJustify::Center);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(Text);
			USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			SizeBox->SetWidthOverride(Width);
			SizeBox->SetHeightOverride(34.f); // shorter than EditMenuBarHeight so it visibly sits inside the bar
			if (USizeBoxSlot* TextSlot = Cast<USizeBoxSlot>(SizeBox->SetContent(Text)))
			{
				TextSlot->SetHorizontalAlignment(HAlign_Center);
				TextSlot->SetVerticalAlignment(VAlign_Center);
			}
			Button->SetContent(SizeBox);
			if (UHorizontalBoxSlot* RowSlot = Cast<UHorizontalBoxSlot>(MenuBarRow->AddChildToHorizontalBox(Button)))
			{
				RowSlot->SetVerticalAlignment(VAlign_Center);
				RowSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
			}
			return Button;
		};

		// Eats all remaining row width so Enter Interior/Back land flush against the bar's right edge.
		USpacer* MenuBarSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
		if (UHorizontalBoxSlot* SpacerSlot = Cast<UHorizontalBoxSlot>(MenuBarRow->AddChildToHorizontalBox(MenuBarSpacer)))
		{
			SpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		EnterInteriorButton = AddBarButton(TEXT("Btn_EnterInterior"), TEXT("Enter Interior"), 120.f);
		EnterInteriorButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnEnterInteriorClicked);
		EnterInteriorButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::PlayButtonClickSound);
		// Hidden until a building is actually selected (see HandleBuildingSelected) - matches
		// WBP_EditScreen's original behavior.
		EnterInteriorButton->SetVisibility(ESlateVisibility::Collapsed);

		BackButton = AddBarButton(TEXT("Btn_Back"), TEXT("Back"), 60.f);
		BackButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnBackClicked);
		BackButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::PlayButtonClickSound);

		// --- Toolbar bar: fixed chrome directly under the menu bar, same vertical position as
		// UInteriorScreenWidget's own BuildingSwitcherBarBackground+SwitcherRow - this screen has
		// nothing else to put there (no building switcher, 2D/3D toggle, Reset, or View/Edit of its
		// own), so it only ever holds the camera speed controls, centered in the bar.
		UBorder* ToolbarBarBackground = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_ToolbarBar"));
		ToolbarBarBackground->SetBrushColor(EditMenuBarColor);
		ToolbarBarBackground->SetPadding(FMargin(10.f, 8.f));
		ToolbarBarBackground->SetHorizontalAlignment(HAlign_Center);
		ToolbarBarBackground->SetVerticalAlignment(VAlign_Center);
		UCanvasPanelSlot* ToolbarBarSlot = Canvas->AddChildToCanvas(ToolbarBarBackground);
		ToolbarBarSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
		ToolbarBarSlot->SetAlignment(FVector2D(0.f, 0.f));
		ToolbarBarSlot->SetOffsets(FMargin(0.f, EditMenuBarHeight, 0.f, ExteriorToolbarBarHeight));
		ToolbarBarSlot->SetZOrder(6);

		UHorizontalBox* ToolbarRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ToolbarRow"));
		ToolbarBarBackground->SetContent(ToolbarRow);

		auto AddToolbarButton = [this, ToolbarRow](const TCHAR* Name, const TCHAR* Label, float Width, float LeftPadding) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
			Button->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::PlayButtonClickSound);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Text->SetJustification(ETextJustify::Center);
			ApplyStandardFont(Text);
			USizeBox* ButtonSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			ButtonSizeBox->SetWidthOverride(Width);
			ButtonSizeBox->SetHeightOverride(36.f);
			if (USizeBoxSlot* TextSlot = Cast<USizeBoxSlot>(ButtonSizeBox->SetContent(Text)))
			{
				TextSlot->SetHorizontalAlignment(HAlign_Center);
				TextSlot->SetVerticalAlignment(VAlign_Center);
			}
			Button->SetContent(ButtonSizeBox);
			if (UHorizontalBoxSlot* RowSlot = Cast<UHorizontalBoxSlot>(ToolbarRow->AddChildToHorizontalBox(Button)))
			{
				RowSlot->SetVerticalAlignment(VAlign_Center);
				RowSlot->SetPadding(FMargin(LeftPadding, 0.f, 0.f, 0.f));
			}
			return Button;
		};

		ResetCameraViewButton = AddToolbarButton(TEXT("Btn_ResetCameraView"), TEXT("Reset"), 64.f, 0.f);
		ResetCameraViewButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnResetCameraViewClicked);
		ResetCameraViewButton->SetToolTipText(FText::FromString(TEXT("Reset camera to the default view")));

		DecreaseCameraSpeedButton = AddToolbarButton(TEXT("Btn_DecreaseCameraSpeed"), TEXT("-"), 48.f, 6.f);
		DecreaseCameraSpeedButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnDecreaseCameraSpeedClicked);
		DecreaseCameraSpeedButton->SetToolTipText(FText::FromString(TEXT("Decrease Camera Speed")));

		// Typed speed entry, between the -/+ buttons - see OnCameraSpeedTextCommitted. Shows/accepts
		// the raw speed value directly (matching UInteriorScreenWidget's own 3D-preview speed box,
		// which does the same) - up to 6 digits at ExteriorCameraMaxSpeed=300000, hence the wider box.
		CameraSpeedTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_CameraSpeed"));
		CameraSpeedTextBox->SetJustification(ETextJustify::Center);
		CameraSpeedTextBox->SetToolTipText(FText::FromString(TEXT("Camera Speed (type a value, then press Enter or click away)")));
		CameraSpeedTextBox->OnTextCommitted.AddUniqueDynamic(this, &UEditScreenWidget::OnCameraSpeedTextCommitted);
		ApplyStandardFont(CameraSpeedTextBox);
		USizeBox* CameraSpeedSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		CameraSpeedSizeBox->SetWidthOverride(68.f);
		CameraSpeedSizeBox->SetHeightOverride(36.f);
		CameraSpeedSizeBox->SetContent(CameraSpeedTextBox);
		if (UHorizontalBoxSlot* CameraSpeedRowSlot = Cast<UHorizontalBoxSlot>(ToolbarRow->AddChildToHorizontalBox(CameraSpeedSizeBox)))
		{
			CameraSpeedRowSlot->SetVerticalAlignment(VAlign_Center);
			CameraSpeedRowSlot->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
		}

		IncreaseCameraSpeedButton = AddToolbarButton(TEXT("Btn_IncreaseCameraSpeed"), TEXT("+"), 48.f, 6.f);
		IncreaseCameraSpeedButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnIncreaseCameraSpeedClicked);
		IncreaseCameraSpeedButton->SetToolTipText(FText::FromString(TEXT("Increase Camera Speed")));

		SetDefaultViewButton = AddToolbarButton(TEXT("Btn_SetDefaultView"), TEXT("Set Default"), 84.f, 6.f);
		SetDefaultViewButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnSetDefaultViewClicked);
		SetDefaultViewButton->SetToolTipText(FText::FromString(TEXT("Save the current camera view as the default")));

		// --- Dropdowns (File, Window) + the click-catcher that closes them on an outside click ---
		auto ConstructDropdown = [this, Canvas](const TCHAR* Name, float PositionX) -> UBorder*
		{
			UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(FString(Name) + TEXT("Stack")));
			UBorder* Dropdown = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Name));
			Dropdown->SetBrushColor(EditMenuBarColor);
			Dropdown->SetPadding(FMargin(4.f));
			Dropdown->SetContent(Stack);
			Dropdown->SetVisibility(ESlateVisibility::Collapsed);
			UCanvasPanelSlot* DropdownSlot = Canvas->AddChildToCanvas(Dropdown);
			DropdownSlot->SetAnchors(FAnchors(0.f, 0.f));
			DropdownSlot->SetAlignment(FVector2D(0.f, 0.f));
			DropdownSlot->SetPosition(FVector2D(PositionX, EditMenuBarHeight + 4.f));
			DropdownSlot->SetAutoSize(true);
			DropdownSlot->SetZOrder(7);
			return Dropdown;
		};

		auto AddDropdownButtonRow = [this](UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
			Button->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::PlayButtonClickSound);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetJustification(ETextJustify::Center);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(Text);
			USizeBox* RowSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			RowSizeBox->SetWidthOverride(120.f);
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

		// A small non-interactive sub-header row (e.g. "Roads"/"Buildings"/"Region" within the Map
		// panel) - just a styled UTextBlock, extra top padding to separate it from the section above.
		auto AddSectionHeaderRow = [this](UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label)
		{
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(Text);
			UVerticalBoxSlot* RowSlot = Stack->AddChildToVerticalBox(Text);
			RowSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 2.f));
		};

		FileMenuDropdown = ConstructDropdown(TEXT("Pnl_FileDropdown"), EditMenuBarLeftMargin);
		UVerticalBox* FileDropdownStack = Cast<UVerticalBox>(FileMenuDropdown->GetContent());
		UButton* FileNewButton = AddDropdownButtonRow(FileDropdownStack, TEXT("Btn_FileNew"), TEXT("New"));
		FileNewButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnFileNewClicked);
		UButton* FileLoadButton = AddDropdownButtonRow(FileDropdownStack, TEXT("Btn_FileLoad"), TEXT("Load"));
		FileLoadButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnFileLoadClicked);

		WindowMenuDropdown = ConstructDropdown(TEXT("Pnl_WindowDropdown"), EditMenuBarLeftMargin + EditMenuButtonWidth + 10.f);
		UVerticalBox* WindowDropdownStack = Cast<UVerticalBox>(WindowMenuDropdown->GetContent());
		WindowAudioCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowAudio"), TEXT("Audio"));
		// Unlike UInteriorScreenWidget's own dock-panel checkboxes (all default checked, since those
		// panels are visible from the start), this is the only Window entry on this screen and starts
		// unchecked - the Audio panel starts hidden, matching the interior editor's own Audio panel.
		WindowAudioCheckbox->SetIsChecked(false);
		WindowAudioCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowAudioCheckChanged);

		WindowRoadsCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowRoads"), TEXT("Map"));
		// Unlike WindowAudioCheckbox, starts checked - RoadFilterPanel starts Visible.
		WindowRoadsCheckbox->SetIsChecked(true);
		WindowRoadsCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowRoadsCheckChanged);

		// Starts unchecked - the Debug panel starts hidden, like the interior editor's own. All three boxes
		// are re-synced to the real dock state by RefreshWindowMenuChecks once the layout is loaded.
		WindowDebugCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowDebug"), TEXT("Debug"));
		WindowDebugCheckbox->SetIsChecked(false);
		WindowDebugCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowDebugCheckChanged);

		UButton* WindowResetLayoutButton = AddDropdownButtonRow(WindowDropdownStack, TEXT("Btn_WindowResetLayout"), TEXT("Reset Layout"));
		WindowResetLayoutButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowResetLayoutClicked);

		MenuClickCatcher = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuClickCatcher"));
		MenuClickCatcher->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		MenuClickCatcher->SetVisibility(ESlateVisibility::Collapsed);
		MenuClickCatcher->OnMouseButtonDownEvent.BindDynamic(this, &UEditScreenWidget::OnMenuClickCatcherMouseButtonDown);
		UCanvasPanelSlot* MenuClickCatcherSlot = Canvas->AddChildToCanvas(MenuClickCatcher);
		MenuClickCatcherSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		MenuClickCatcherSlot->SetOffsets(FMargin(0.f));
		MenuClickCatcherSlot->SetZOrder(5); // above nothing else on this screen, below menu bar/dropdowns (6-7)

		// --- Dock host: lays out the Audio / Map / Debug panels (tab groups, splits, edge areas, floating
		// windows) - see DockLayoutModel / DockHostWidget. Panel contents are built here as content-root
		// UBorders and registered with it; nothing is added to the canvas directly. Below the two fixed bars
		// (menu bar + toolbar bar) so docked panels start flush under them.
		DockHost = WidgetTree->ConstructWidget<UDockHostWidget>(UDockHostWidget::StaticClass(), TEXT("DockHost"));
		{
			FDockMetrics DockMetrics;
			DockMetrics.TopMargin = EditMenuBarHeight + ExteriorToolbarBarHeight;
			DockHost->SetMetrics(DockMetrics);
		}
		UCanvasPanelSlot* DockHostSlot = Canvas->AddChildToCanvas(DockHost);
		DockHostSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		DockHostSlot->SetOffsets(FMargin(0.f));
		DockHostSlot->SetZOrder(2);

		auto ConstructDockPanel = [this](FName Id, const TCHAR* BorderName) -> UBorder*
		{
			UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(BorderName));
			// The dock frame already paints the panel background.
			Border->SetBrushColor(FLinearColor::Transparent);
			Border->SetPadding(FMargin(14.f));
			DockHost->RegisterPanel(Id, FText::FromName(Id), Border);
			return Border;
		};

		// --- Audio panel (Window > Audio) - same Play/Stop All/mute-click-sound/file-list layout as
		// UInteriorScreenWidget's own Audio panel, reusing the same backend and UAudioFileRowWidget class.
		AudioPanel = ConstructDockPanel(ExteriorDock::Audio, TEXT("Pnl_Audio"));
		UVerticalBox* AudioStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AudioStack"));
		AudioPanel->SetContent(AudioStack);

		AudioStopAllButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_AudioStopAll"));
		AudioStopAllButton->SetBackgroundColor(FLinearColor(0.75f, 0.2f, 0.2f, 1.f));
		AudioStopAllButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnAudioStopAllClicked);
		AudioStopAllButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::PlayButtonClickSound);
		UTextBlock* AudioStopAllText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_AudioStopAll"));
		AudioStopAllText->SetText(FText::FromString(TEXT("Stop All")));
		AudioStopAllText->SetJustification(ETextJustify::Center);
		AudioStopAllText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(AudioStopAllText);
		USizeBox* AudioStopAllSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size_AudioStopAll"));
		AudioStopAllSizeBox->SetWidthOverride(EditAudioPanelWidth);
		AudioStopAllSizeBox->SetMinDesiredHeight(32.f);
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
		AudioClickSoundCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnAudioClickSoundCheckChanged);

		AudioSelectSoundCheckbox = AddAudioCheckRow(TEXT("Chk_AudioSelectSound"), TEXT("Building Select Sound"), true);
		AudioSelectSoundCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnAudioSelectSoundCheckChanged);

		AudioQuitConfirmSoundCheckbox = AddAudioCheckRow(TEXT("Chk_AudioQuitConfirmSound"), TEXT("Confirm Dialog Sound"), true);
		AudioQuitConfirmSoundCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnAudioQuitConfirmSoundCheckChanged);

		// Checked = everything muted (opposite sense from the three rows above) - unchecked by default,
		// matching nothing being muted at first.
		AudioMuteAllCheckbox = AddAudioCheckRow(TEXT("Chk_AudioMuteAll"), TEXT("Mute All"), false);
		AudioMuteAllCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnAudioMuteAllCheckChanged);

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
		AudioVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnAudioVolumeChanged);
		USizeBox* AudioVolumeSliderSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		AudioVolumeSliderSizeBox->SetWidthOverride(EditAudioPanelWidth - 60.f);
		AudioVolumeSliderSizeBox->SetContent(AudioVolumeSlider);
		UHorizontalBoxSlot* AudioVolumeSliderSlot = AudioVolumeRow->AddChildToHorizontalBox(AudioVolumeSliderSizeBox);
		AudioVolumeSliderSlot->SetVerticalAlignment(VAlign_Center);
		UVerticalBoxSlot* AudioVolumeRowSlot = AudioStack->AddChildToVerticalBox(AudioVolumeRow);
		AudioVolumeRowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		// Rows themselves are built by RefreshAudioList (one UAudioFileRowWidget per file under
		// Data/Audio/), same reasoning as UInteriorScreenWidget's own AudioFileListBox.
		AudioFileListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("AudioFileListBox"));
		USizeBox* AudioFileListSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("AudioFileListSizeBox"));
		AudioFileListSizeBox->SetHeightOverride(EditAudioFileListHeight);
		AudioFileListSizeBox->SetWidthOverride(EditAudioPanelWidth);
		AudioFileListSizeBox->SetContent(AudioFileListBox);
		AudioStack->AddChildToVerticalBox(AudioFileListSizeBox);

		// --- Map panel (Window > Map): road category/region filters and the building filters.
		RoadFilterPanel = ConstructDockPanel(ExteriorDock::Map, TEXT("Pnl_RoadFilter"));

		UVerticalBox* RoadFilterStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoadFilterStack"));
		RoadFilterPanel->SetContent(RoadFilterStack);

		AddSectionHeaderRow(RoadFilterStack, TEXT("Txt_RoadFilterRoads"), TEXT("Roads"));
		RoadHideAllCheckbox = AddDropdownCheckRow(RoadFilterStack, TEXT("Chk_RoadHideAll"), TEXT("Hide All"));
		RoadHideAllCheckbox->SetIsChecked(false);
		RoadHideAllCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnRoadHideAllCheckChanged);
		RoadPrimaryCheckbox = AddDropdownCheckRow(RoadFilterStack, TEXT("Chk_RoadPrimary"), TEXT("Primary"));
		RoadPrimaryCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnRoadPrimaryCheckChanged);
		RoadSecondaryCheckbox = AddDropdownCheckRow(RoadFilterStack, TEXT("Chk_RoadSecondary"), TEXT("Secondary"));
		RoadSecondaryCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnRoadSecondaryCheckChanged);
		RoadResidentialCheckbox = AddDropdownCheckRow(RoadFilterStack, TEXT("Chk_RoadResidential"), TEXT("Residential/Tertiary"));
		RoadResidentialCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnRoadResidentialCheckChanged);
		RoadCyclewayCheckbox = AddDropdownCheckRow(RoadFilterStack, TEXT("Chk_RoadCycleway"), TEXT("Cycleway"));
		RoadCyclewayCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnRoadCyclewayCheckChanged);
		RoadFootwayCheckbox = AddDropdownCheckRow(RoadFilterStack, TEXT("Chk_RoadFootway"), TEXT("Footway/Path"));
		RoadFootwayCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnRoadFootwayCheckChanged);
		RoadOtherCheckbox = AddDropdownCheckRow(RoadFilterStack, TEXT("Chk_RoadOther"), TEXT("Other"));
		RoadOtherCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnRoadOtherCheckChanged);
		// Region/distance filter, folded into the Roads section (not its own "Region" header) - it only
		// ever affects roads/POIs, so it reads as a 7th road-filter row, not a separate concept.
		RoadCampusOnlyCheckbox = AddDropdownCheckRow(RoadFilterStack, TEXT("Chk_RoadCampusOnly"), TEXT("Campus Only"));
		// Starts checked - OnRoadCampusOnlyCheckChanged isn't bound yet at this point in RebuildWidget,
		// so this alone doesn't apply the filter - see the PC->SetRoadsAndPOIsWithinCampusOnly(true)
		// call in NativeConstruct that actually turns it on once a PC/Generator exists.
		RoadCampusOnlyCheckbox->SetIsChecked(true);
		RoadCampusOnlyCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnRoadCampusOnlyCheckChanged);

		AddSectionHeaderRow(RoadFilterStack, TEXT("Txt_RoadFilterBuildings"), TEXT("Buildings"));
		BuildingCampusCheckbox = AddDropdownCheckRow(RoadFilterStack, TEXT("Chk_BuildingCampus"), TEXT("Campus"));
		BuildingCampusCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnBuildingCampusCheckChanged);
		BuildingNonCampusCheckbox = AddDropdownCheckRow(RoadFilterStack, TEXT("Chk_BuildingNonCampus"), TEXT("Non-Campus"));
		// Starts unchecked (unlike BuildingCampusCheckbox) - non-campus buildings are hidden by
		// default. OnBuildingNonCampusCheckChanged isn't bound yet at this point in RebuildWidget, so
		// this alone doesn't hide anything - see the PC->SetNonCampusBuildingsVisible(false) call in
		// NativeConstruct that actually applies it once a PC/Generator exists.
		BuildingNonCampusCheckbox->SetIsChecked(false);
		BuildingNonCampusCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnBuildingNonCampusCheckChanged);

		// --- Debug panel (Window > Debug): live info plus the last failed action. Starts hidden.
		DebugPanel = ConstructDockPanel(ExteriorDock::Debug, TEXT("Pnl_Debug"));
		UVerticalBox* DebugStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DebugStack"));
		DebugPanel->SetContent(DebugStack);

		DebugInfoText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_DebugInfo"));
		DebugInfoText->SetText(FText::FromString(TEXT("Debug information")));
		DebugInfoText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(DebugInfoText);
		DebugInfoText->SetAutoWrapText(true);
		DebugStack->AddChildToVerticalBox(DebugInfoText);

		// Separate from DebugInfoText so it can carry its own colour (yellow/red once an action has failed)
		// independently of the rest of the panel.
		DebugLastErrorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_DebugLastError"));
		DebugLastErrorText->SetText(FText::FromString(TEXT("Last Error: None")));
		DebugLastErrorText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(DebugLastErrorText);
		DebugLastErrorText->SetAutoWrapText(true);
		UVerticalBoxSlot* DebugErrorSlot = DebugStack->AddChildToVerticalBox(DebugLastErrorText);
		DebugErrorSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	}

	return Super::RebuildWidget();
}

void UEditScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->OnBuildingSelected.AddUniqueDynamic(this, &UEditScreenWidget::HandleBuildingSelected);
		PC->OnActionFailed.AddUniqueDynamic(this, &UEditScreenWidget::HandleActionFailed);

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

		// RoadCampusOnlyCheckbox defaults to checked (set in RebuildWidget), but that alone doesn't hide
		// anything - actually apply it now that a PC/Generator exists (by the time this screen is
		// constructed, LoadBuildingDataFromPath has already loaded any roads/POIs to filter).
		PC->SetRoadsAndPOIsWithinCampusOnly(true);
		// Same reasoning for BuildingNonCampusCheckbox, which defaults to UNCHECKED - actually hide the
		// context buildings now that a PC/Generator exists.
		PC->SetNonCampusBuildingsVisible(false);
	}

	RefreshAudioList();
	RefreshCameraSpeedDisplay();
	// Arranges every dock panel for the first time: the user's saved layout, else the team default.
	InitializeDockLayout();
}

void UEditScreenWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
	}
}

void UEditScreenWidget::NativeDestruct()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->OnBuildingSelected.RemoveDynamic(this, &UEditScreenWidget::HandleBuildingSelected);
		PC->OnActionFailed.RemoveDynamic(this, &UEditScreenWidget::HandleActionFailed);
		PC->ClearPointerOverUICallback(this);
	}

	// Leaving this screen (Back, Enter Interior, quitting) keeps the arrangement for next time.
	SaveDockLayoutToDisk();
	if (DockHost)
	{
		DockHost->OnPanelClosedNative.Unbind();
		DockHost->OnLayoutChangedNative.Unbind();
	}

	Super::NativeDestruct();
}

void UEditScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	FVector2D ReliableSize;
	if (GetReliableCanvasSize(ReliableSize))
	{
		LastKnownCanvasSize = ReliableSize;
	}

	if (DebugInfoText)
	{
		// Smooth frame time / FPS
		DebugSmoothedDeltaTime = DebugSmoothedDeltaTime <= 0.f ? InDeltaTime : FMath::Lerp(DebugSmoothedDeltaTime, InDeltaTime, 0.1f);
		const float FPS = DebugSmoothedDeltaTime > 0.f ? 1.f / DebugSmoothedDeltaTime : 0.f;

		FString SelectedBuilding = TEXT("None");
		FString CameraSpeed = TEXT("-");
		FString UnsavedChanges = TEXT("No");
		if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
		{
			const FString BuildingId = PC->GetSelectedBuildingId();
			if (!BuildingId.IsEmpty())
			{
				SelectedBuilding = BuildingId;
			}
			CameraSpeed = FString::FromInt(FMath::RoundToInt(PC->GetCameraMoveSpeed()));
			UnsavedChanges = PC->HasUnsavedChanges() ? TEXT("Yes") : TEXT("No");
		}

		DebugInfoText->SetText(FText::FromString(FString::Printf(
			TEXT("Selected Building: %s\nCamera Speed: %s\nUnsaved Changes: %s\nFPS: %.1f\nFrame Time: %.2f ms"),
			*SelectedBuilding, *CameraSpeed, *UnsavedChanges, FPS, DebugSmoothedDeltaTime * 1000.f)));

		if (DebugLastErrorText)
		{
			if (LastActionFailedMessage.IsEmpty())
			{
				DebugLastErrorText->SetText(FText::FromString(TEXT("Last Error: None")));
				DebugLastErrorText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			}
			else
			{
				DebugLastErrorText->SetText(FText::FromString(TEXT("Last Error: ") + LastActionFailedMessage));
				// Yellow: the action was rejected but nothing was lost. Red: existing data may have been
				// cleared, or a save may have silently failed - see EActionFailureSeverity.
				DebugLastErrorText->SetColorAndOpacity(FSlateColor(
					LastActionFailedSeverity == EActionFailureSeverity::Critical ? FLinearColor::Red : FLinearColor::Yellow));
			}
		}
	}

	// The Audio panel's controls size themselves from its panel's width, which the user can change at any time.
	SyncAudioPanelWidth();
}

FReply UEditScreenWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Scrollable panel content already got first crack at this via normal event bubbling; this just stops
	// whatever it did not consume from falling through to the scene and changing the camera speed/zoom.
	if (DockHost && DockHost->IsScreenPositionOverDock(InMouseEvent.GetScreenSpacePosition()))
	{
		return FReply::Handled();
	}
	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

void UEditScreenWidget::HandleBuildingSelected(FString BuildingId)
{
	// An empty BuildingId means the selection was cleared (see ANavPlayerController::
	// SelectBuildingUnderCursor - clicking empty space, or re-clicking the already-selected building,
	// now deselects instead of only ever adding/switching a selection).
	if (EnterInteriorButton)
	{
		EnterInteriorButton->SetVisibility(BuildingId.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
}

void UEditScreenWidget::OnEnterInteriorClicked()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	PC->LoadInteriorForBuilding(PC->GetSelectedBuildingId());

	if (UInteriorScreenWidget* InteriorScreen = CreateWidget<UInteriorScreenWidget>(PC, UInteriorScreenWidget::StaticClass()))
	{
		InteriorScreen->AddToViewport();
		RemoveFromParent();
	}
}

void UEditScreenWidget::OnIncreaseCameraSpeedClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->IncreaseCameraSpeedExterior();
	}
	RefreshCameraSpeedDisplay();
}

void UEditScreenWidget::OnDecreaseCameraSpeedClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->DecreaseCameraSpeedExterior();
	}
	RefreshCameraSpeedDisplay();
}

void UEditScreenWidget::OnResetCameraViewClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ResetExteriorCameraToDefaultView();
	}
	RefreshCameraSpeedDisplay();
}

void UEditScreenWidget::OnSetDefaultViewClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetCurrentViewAsDefaultExterior();
	}
}

void UEditScreenWidget::OnCameraSpeedTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType != ETextCommit::OnEnter && CommitType != ETextCommit::OnUserMovedFocus)
	{
		return;
	}
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetCameraSpeedExterior(FCString::Atof(*NewText.ToString()));
	}
	// Re-reads the actual (possibly clamped) value back in, so a garbage or out-of-range entry
	// doesn't stay displayed as if it had taken effect.
	RefreshCameraSpeedDisplay();
}

void UEditScreenWidget::RefreshCameraSpeedDisplay()
{
	if (!CameraSpeedTextBox)
	{
		return;
	}
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		// Plain FString::FromInt, not FText::AsNumber - the latter can insert locale thousands
		// separators (e.g. "12,500"), which FCString::Atof would then misparse as just "12" if the
		// user committed the box without editing it.
		CameraSpeedTextBox->SetText(FText::FromString(FString::FromInt(FMath::RoundToInt(PC->GetCameraMoveSpeed()))));
	}
}

void UEditScreenWidget::OnBackClicked()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	if (!PC->HasUnsavedChanges())
	{
		GoToStartScreen();
		return;
	}

	if (UConfirmDialogWidget* Dialog = CreateWidget<UConfirmDialogWidget>(PC, UConfirmDialogWidget::StaticClass()))
	{
		Dialog->AddToViewport();
		Dialog->OnSaveAndLeave.AddUniqueDynamic(this, &UEditScreenWidget::HandleSaveAndLeave);
		Dialog->OnDiscardAndLeave.AddUniqueDynamic(this, &UEditScreenWidget::HandleDiscardAndLeave);
		Dialog->OnCancel.AddUniqueDynamic(this, &UEditScreenWidget::HandleCancel);
	}
}

void UEditScreenWidget::HandleSaveAndLeave()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SaveCurrentProject();
	}
	GoToStartScreen();
}

void UEditScreenWidget::HandleDiscardAndLeave()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->DiscardUnsavedChanges();
	}
	GoToStartScreen();
}

void UEditScreenWidget::HandleCancel()
{
	// Nothing to do - the dialog already removed itself.
}

void UEditScreenWidget::GoToStartScreen()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	// This is the only place that ever creates the start screen - stop here so audio started from
	// either Audio panel (this screen's own, or the interior editor's) never keeps playing once the
	// user is back at the initial page with no editor UI left to stop it from.
	PC->StopAllAudio();

	if (UStartScreenWidget* Screen = CreateWidget<UStartScreenWidget>(PC, UStartScreenWidget::StaticClass()))
	{
		Screen->AddToViewport();
		RemoveFromParent();
	}
}

void UEditScreenWidget::OpenMenuDropdown(UBorder* Dropdown)
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

void UEditScreenWidget::CloseMenuDropdowns()
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

FEventReply UEditScreenWidget::OnMenuClickCatcherMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent)
{
	CloseMenuDropdowns();
	return UWidgetBlueprintLibrary::Handled();
}

void UEditScreenWidget::OnSaveClicked()
{
	SaveDockLayoutToDisk();
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SaveCurrentProject();
	}
}

void UEditScreenWidget::OnFileMenuClicked()
{
	OpenMenuDropdown(FileMenuDropdown);
}

void UEditScreenWidget::OnWindowMenuClicked()
{
	OpenMenuDropdown(WindowMenuDropdown);
}

void UEditScreenWidget::OnFileNewClicked()
{
	CloseMenuDropdowns();
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}
	if (!PC->HasUnsavedChanges())
	{
		PC->StartNewProject();
		HandleBuildingSelected(FString());
		return;
	}
	if (UConfirmDialogWidget* Dialog = CreateWidget<UConfirmDialogWidget>(PC, UConfirmDialogWidget::StaticClass()))
	{
		Dialog->AddToViewport();
		Dialog->OnSaveAndLeave.AddUniqueDynamic(this, &UEditScreenWidget::HandleFileNewSave);
		Dialog->OnDiscardAndLeave.AddUniqueDynamic(this, &UEditScreenWidget::HandleFileNewDiscard);
		Dialog->OnCancel.AddUniqueDynamic(this, &UEditScreenWidget::HandleFileNewCancel);
	}
}

void UEditScreenWidget::HandleFileNewSave()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SaveCurrentProject();
		PC->StartNewProject();
	}
	HandleBuildingSelected(FString());
}

void UEditScreenWidget::HandleFileNewDiscard()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->DiscardUnsavedChanges();
		PC->StartNewProject();
	}
	HandleBuildingSelected(FString());
}

void UEditScreenWidget::HandleFileNewCancel()
{
	// Nothing to do - the dialog already removed itself.
}

void UEditScreenWidget::OnFileLoadClicked()
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
		PC->LoadBuildingDataFromPathOrDatabase(FilePath);
		HandleBuildingSelected(FString());
		return;
	}
	PendingLoadFilePath = FilePath;
	if (UConfirmDialogWidget* Dialog = CreateWidget<UConfirmDialogWidget>(PC, UConfirmDialogWidget::StaticClass()))
	{
		Dialog->AddToViewport();
		Dialog->OnSaveAndLeave.AddUniqueDynamic(this, &UEditScreenWidget::HandleFileLoadSave);
		Dialog->OnDiscardAndLeave.AddUniqueDynamic(this, &UEditScreenWidget::HandleFileLoadDiscard);
		Dialog->OnCancel.AddUniqueDynamic(this, &UEditScreenWidget::HandleFileLoadCancel);
	}
}

void UEditScreenWidget::HandleFileLoadSave()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SaveCurrentProject();
		PC->LoadBuildingDataFromPathOrDatabase(PendingLoadFilePath);
	}
	HandleBuildingSelected(FString());
}

void UEditScreenWidget::HandleFileLoadDiscard()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->DiscardUnsavedChanges();
		PC->LoadBuildingDataFromPathOrDatabase(PendingLoadFilePath);
	}
	HandleBuildingSelected(FString());
}

void UEditScreenWidget::HandleFileLoadCancel()
{
	// Nothing to do - the dialog already removed itself.
}

void UEditScreenWidget::OnRoadHideAllCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetAllRoadsVisible(!bIsChecked);
	}
	// Visually sync the 6 category checkboxes to match (checked = shown) - SetIsChecked doesn't re-fire
	// OnCheckStateChanged, same pattern as OnAudioMuteAllCheckChanged.
	const bool bCategoriesChecked = !bIsChecked;
	if (RoadPrimaryCheckbox) RoadPrimaryCheckbox->SetIsChecked(bCategoriesChecked);
	if (RoadSecondaryCheckbox) RoadSecondaryCheckbox->SetIsChecked(bCategoriesChecked);
	if (RoadResidentialCheckbox) RoadResidentialCheckbox->SetIsChecked(bCategoriesChecked);
	if (RoadCyclewayCheckbox) RoadCyclewayCheckbox->SetIsChecked(bCategoriesChecked);
	if (RoadFootwayCheckbox) RoadFootwayCheckbox->SetIsChecked(bCategoriesChecked);
	if (RoadOtherCheckbox) RoadOtherCheckbox->SetIsChecked(bCategoriesChecked);
}

void UEditScreenWidget::OnRoadPrimaryCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetRoadCategoryVisible(ERoadCategory::Primary, bIsChecked);
	}
}

void UEditScreenWidget::OnRoadSecondaryCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetRoadCategoryVisible(ERoadCategory::Secondary, bIsChecked);
	}
}

void UEditScreenWidget::OnRoadResidentialCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetRoadCategoryVisible(ERoadCategory::Residential, bIsChecked);
	}
}

void UEditScreenWidget::OnRoadCyclewayCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetRoadCategoryVisible(ERoadCategory::Cycleway, bIsChecked);
	}
}

void UEditScreenWidget::OnRoadFootwayCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetRoadCategoryVisible(ERoadCategory::Footway, bIsChecked);
	}
}

void UEditScreenWidget::OnRoadOtherCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetRoadCategoryVisible(ERoadCategory::Other, bIsChecked);
	}
}

void UEditScreenWidget::OnRoadCampusOnlyCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetRoadsAndPOIsWithinCampusOnly(bIsChecked);
	}
}

void UEditScreenWidget::OnBuildingCampusCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetCampusBuildingsVisible(bIsChecked);
	}
}

void UEditScreenWidget::OnBuildingNonCampusCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetNonCampusBuildingsVisible(bIsChecked);
	}
}

void UEditScreenWidget::RefreshAudioList()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC || !AudioFileListBox)
	{
		return;
	}

	AudioFileListBox->ClearChildren();

	for (const FString& FileName : PC->GetAvailableAudioFiles())
	{
		UAudioFileRowWidget* Row = CreateWidget<UAudioFileRowWidget>(PC, UAudioFileRowWidget::StaticClass());
		if (!Row)
		{
			continue;
		}
		Row->SetFileName(FileName, PC->IsAudioFilePlaying(FileName));
		Row->SetWidth(AudioContentWidth);
		Row->OnPlayClicked.AddUniqueDynamic(this, &UEditScreenWidget::HandleAudioRowPlayClicked);

		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(AudioFileListBox->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}
	}
}

void UEditScreenWidget::OnAudioStopAllClicked()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->StopAllAudio();
		RefreshAudioList();
	}
}

void UEditScreenWidget::OnAudioClickSoundCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetButtonClickSoundEnabled(bIsChecked);
	}
}

void UEditScreenWidget::OnAudioSelectSoundCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetBuildingSelectSoundEnabled(bIsChecked);
	}
}

void UEditScreenWidget::OnAudioQuitConfirmSoundCheckChanged(bool bIsChecked)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetQuitConfirmSoundEnabled(bIsChecked);
	}
}

void UEditScreenWidget::OnAudioMuteAllCheckChanged(bool bIsChecked)
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

void UEditScreenWidget::OnAudioVolumeChanged(float NewValue)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetMasterVolume(NewValue);
	}
}

void UEditScreenWidget::HandleAudioRowPlayClicked(FString FileName)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayAudioFile(FileName);
		RefreshAudioList();
	}
}

void UEditScreenWidget::HandleActionFailed(FString Reason, EActionFailureSeverity Severity)
{
	LastActionFailedMessage = Reason;
	LastActionFailedSeverity = Severity;
}

// ---------------------------------------------------------------------------
// Docking
// ---------------------------------------------------------------------------

bool UEditScreenWidget::GetReliableCanvasSize(FVector2D& OutSize) const
{
	UCanvasPanel* RootCanvas = WidgetTree ? Cast<UCanvasPanel>(WidgetTree->RootWidget) : nullptr;
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

FVector2D UEditScreenWidget::GetCanvasSize() const
{
	FVector2D ReliableSize;
	if (GetReliableCanvasSize(ReliableSize))
	{
		return ReliableSize;
	}
	if (LastKnownCanvasSize.X > 0.f && LastKnownCanvasSize.Y > 0.f)
	{
		return LastKnownCanvasSize;
	}
	return UWidgetLayoutLibrary::GetViewportSize(this);
}

void UEditScreenWidget::InitializeDockLayout()
{
	if (!DockHost || bDockLayoutInitialized)
	{
		return;
	}
	bDockLayoutInitialized = true;

	DockHost->OnPanelClosedNative.BindUObject(this, &UEditScreenWidget::HandleDockPanelClosed);
	DockHost->OnLayoutChangedNative.BindUObject(this, &UEditScreenWidget::HandleDockLayoutChanged);

	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->SetPointerOverUICallback(this, [this](const FVector2D& ScreenPosition)
		{
			return DockHost && DockHost->IsScreenPositionOverDock(ScreenPosition);
		});
	}

	ExteriorDock::LoadLayout(*DockHost->GetLayout(), GetCanvasSize());
	DockHost->NotifyLayoutChanged();
	RefreshWindowMenuChecks();
	RefreshAudioList();
}

bool UEditScreenWidget::IsDockPanelShown(FName Panel) const
{
	return DockHost && DockHost->IsPanelShown(Panel);
}

void UEditScreenWidget::SetDockPanelHidden(FName Panel, bool bHidden)
{
	if (!DockHost)
	{
		return;
	}
	if (bHidden)
	{
		DockHost->GetLayout()->HidePanel(Panel);
	}
	else
	{
		// Comes back where it was (or at its default edge if it was never placed).
		DockHost->GetLayout()->ShowPanel(Panel, ExteriorDock::DefaultSideFor(Panel));
	}
	DockHost->NotifyLayoutChanged();
	RefreshWindowMenuChecks();
}

void UEditScreenWidget::HandleDockPanelClosed(FName Panel)
{
	RefreshWindowMenuChecks();
}

void UEditScreenWidget::HandleDockLayoutChanged()
{
	RefreshWindowMenuChecks();
}

void UEditScreenWidget::RefreshWindowMenuChecks()
{
	auto Sync = [this](UCheckBox* Checkbox, FName Panel)
	{
		if (Checkbox)
		{
			Checkbox->SetIsChecked(IsDockPanelShown(Panel));
		}
	};
	Sync(WindowAudioCheckbox, ExteriorDock::Audio);
	Sync(WindowRoadsCheckbox, ExteriorDock::Map);
	Sync(WindowDebugCheckbox, ExteriorDock::Debug);
}

void UEditScreenWidget::ResetDockLayout()
{
	if (!DockHost)
	{
		return;
	}
	ExteriorDock::BuildDefaultLayout(*DockHost->GetLayout());
	DockHost->NotifyLayoutChanged();
	RefreshWindowMenuChecks();
	SaveDockLayoutToDisk();
}

void UEditScreenWidget::SaveDockLayoutToDisk() const
{
	// Extents are stored as fractions of the canvas size, so saving against a guessed size (a widget that
	// never got on screen, e.g. in a test) would write a layout that restores far too big or small.
	FVector2D Size;
	if (DockHost && (GetReliableCanvasSize(Size) || (LastKnownCanvasSize.X > 0.f && LastKnownCanvasSize.Y > 0.f)))
	{
		if (!GetReliableCanvasSize(Size))
		{
			Size = LastKnownCanvasSize;
		}
		ExteriorDock::SaveToFile(*DockHost->GetLayout(), ExteriorDock::UserLayoutPath(), Size);
	}
}

void UEditScreenWidget::SyncAudioPanelWidth()
{
	if (!DockHost)
	{
		return;
	}
	const float PanelWidth = DockHost->GetPanelWidth(ExteriorDock::Audio);
	if (PanelWidth <= 0.f)
	{
		return; // hidden, or not laid out yet
	}
	// Minus the panel's 14px padding on each side and room for the scroll bar.
	const float Width = FMath::Max(80.f, PanelWidth - 48.f);
	if (FMath::IsNearlyEqual(Width, AudioContentWidth, 0.5f))
	{
		return;
	}
	AudioContentWidth = Width;

	if (AudioStopAllButton)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(AudioStopAllButton->GetContent()))
		{
			SizeBox->SetWidthOverride(Width);
		}
	}
	if (AudioFileListBox)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(AudioFileListBox->GetParent()))
		{
			SizeBox->SetWidthOverride(Width);
		}
	}
	if (AudioVolumeSlider)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(AudioVolumeSlider->GetParent()))
		{
			SizeBox->SetWidthOverride(FMath::Max(60.f, Width - 60.f));
		}
	}
	// Rows are rebuilt from scratch with the new width baked in - cheap for a list this small.
	RefreshAudioList();
}

void UEditScreenWidget::OnWindowAudioCheckChanged(bool bIsChecked)
{
	SetDockPanelHidden(ExteriorDock::Audio, !bIsChecked);
	if (bIsChecked)
	{
		RefreshAudioList();
	}
}

void UEditScreenWidget::OnWindowRoadsCheckChanged(bool bIsChecked)
{
	SetDockPanelHidden(ExteriorDock::Map, !bIsChecked);
}

void UEditScreenWidget::OnWindowDebugCheckChanged(bool bIsChecked)
{
	SetDockPanelHidden(ExteriorDock::Debug, !bIsChecked);
}

void UEditScreenWidget::OnWindowResetLayoutClicked()
{
	CloseMenuDropdowns();
	ResetDockLayout();
}
