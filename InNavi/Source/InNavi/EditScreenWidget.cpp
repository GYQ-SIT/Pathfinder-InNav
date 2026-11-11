#include "EditScreenWidget.h"

#include "AudioFileRowWidget.h"
#include "AudioPanelWidget.h"
#include "DebugPanelWidget.h"
#include "LayoutValidationPanelWidget.h"
#include "PluginToolHost.h"
#include "BuildingInfoPanelWidget.h"
#include "MapPanelWidget.h"
#include "NavigationDockPanelWidget.h"
#include "NavigationPanelWidget.h"
#include "ConfirmDialogWidget.h"
#include "DockHostUMG.h"
#include "ExteriorDockLayout.h"
#include "DockSharedPanels.h"
#include "InteriorScreenWidget.h"
#include "NavPlayerController.h"
#include "StartScreenWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "StartScreenStyle.h"
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
	// The Save button's slot in the menu bar, padding included.
	constexpr float EditSaveButtonWidth = 96.f;
	constexpr float EditMenuButtonWidth = 72.f;
	constexpr float EditCameraMenuButtonWidth = 84.f;
	constexpr float EditMenuBarLeftMargin = 10.f;

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
		MenuBarBackground->SetBrushColor(StartStyle::Rgb(16, 32, 50));
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
			Button->SetStyle(StartStyle::MenuButton());
			Button->SetBackgroundColor(FLinearColor::White);
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
		SaveButton = StartStyle::SaveBarButton(WidgetTree, SaveIndicatorOuter, SaveIndicatorCore, EditSaveButtonWidth - 12.f);
		SaveButton->SetToolTipText(FText::FromString(TEXT("Save [Ctrl+S]")));
		SaveButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnSaveClicked);
		SaveButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::PlayButtonClickSound);
		if (UHorizontalBoxSlot* SaveBarSlot = MenuBarRow->AddChildToHorizontalBox(SaveButton))
		{
			SaveBarSlot->SetPadding(FMargin(6.f, 5.f));
			SaveBarSlot->SetVerticalAlignment(VAlign_Center);
		}
		RefreshSaveIndicator();

		FileMenuButton = AddMenuButton(TEXT("Btn_FileMenu"), TEXT("File"));
		FileMenuButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnFileMenuClicked);
		WindowMenuButton = AddMenuButton(TEXT("Btn_WindowMenu"), TEXT("Window"));
		WindowMenuButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowMenuClicked);
		// Camera menu: the free-fly camera's Reset/speed/Set Default controls, which used to be a bar of their
		// own under this one. Wider than File/Window since its label is longer.
		CameraMenuButton = AddMenuButton(TEXT("Btn_CameraMenu"), TEXT("Camera"));
		CameraMenuButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnCameraMenuClicked);
		if (USizeBox* CameraMenuSizeBox = Cast<USizeBox>(CameraMenuButton->GetContent()))
		{
			CameraMenuSizeBox->SetWidthOverride(EditCameraMenuButtonWidth);
		}

		auto AddBarButton = [this, MenuBarRow](const TCHAR* Name, const TCHAR* Label, float Width) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetStyle(StartStyle::SecondaryButton());
			Button->SetBackgroundColor(FLinearColor::White);
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

		// Eats all remaining row width so Back lands flush against the bar's right edge.
		USpacer* MenuBarSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
		if (UHorizontalBoxSlot* SpacerSlot = Cast<UHorizontalBoxSlot>(MenuBarRow->AddChildToHorizontalBox(MenuBarSpacer)))
		{
			SpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		BackButton = AddBarButton(TEXT("Btn_Back"), TEXT("Back"), 60.f);
		BackButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnBackClicked);
		BackButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::PlayButtonClickSound);

		// --- Dropdowns (File, Window, Camera) + the click-catcher that closes them on an outside click ---
		auto ConstructDropdown = [this, Canvas](const TCHAR* Name, float PositionX) -> UBorder*
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
			DropdownSlot->SetPosition(FVector2D(PositionX, EditMenuBarHeight + 4.f));
			DropdownSlot->SetAutoSize(true);
			DropdownSlot->SetZOrder(7);
			return Dropdown;
		};

		auto AddDropdownButtonRow = [this](UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetStyle(StartStyle::MenuRowButton());
			Button->SetBackgroundColor(FLinearColor::White);
			Button->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::PlayButtonClickSound);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetJustification(ETextJustify::Center);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ApplyStandardFont(Text);
			USizeBox* RowSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			RowSizeBox->SetWidthOverride(120.f);
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

		FileMenuDropdown = ConstructDropdown(TEXT("Pnl_FileDropdown"), EditMenuBarLeftMargin + EditSaveButtonWidth);
		UVerticalBox* FileDropdownStack = Cast<UVerticalBox>(FileMenuDropdown->GetContent());
		UButton* FileNewButton = AddDropdownButtonRow(FileDropdownStack, TEXT("Btn_FileNew"), TEXT("New"));
		FileNewButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnFileNewClicked);
		UButton* FileLoadButton = AddDropdownButtonRow(FileDropdownStack, TEXT("Btn_FileLoad"), TEXT("Load"));
		FileLoadButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnFileLoadClicked);
		UButton* FileSaveAsButton = AddDropdownButtonRow(FileDropdownStack, TEXT("Btn_FileSaveAs"), TEXT("Save As"));
		FileSaveAsButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnFileSaveAsClicked);

		WindowMenuDropdown = ConstructDropdown(TEXT("Pnl_WindowDropdown"), EditMenuBarLeftMargin + EditSaveButtonWidth + EditMenuButtonWidth);
		UVerticalBox* WindowDropdownStack = Cast<UVerticalBox>(WindowMenuDropdown->GetContent());
		WindowAudioCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowAudio"), TEXT("Audio"));
		// Unlike UInteriorScreenWidget's own dock-panel checkboxes (all default checked, since those
		// panels are visible from the start), this is the only Window entry on this screen and starts
		// unchecked - the Audio panel starts hidden, matching the interior editor's own Audio panel.
		WindowAudioCheckbox->SetIsChecked(false);
		WindowAudioCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowAudioCheckChanged);

		WindowRoadsCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowRoads"), TEXT("Map"));
		// Unlike WindowAudioCheckbox, starts checked - the Map panel starts shown.
		WindowRoadsCheckbox->SetIsChecked(true);
		WindowRoadsCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowRoadsCheckChanged);

		WindowBuildingCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowBuilding"), TEXT("Building"));
		WindowBuildingCheckbox->SetIsChecked(true);
		WindowBuildingCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowBuildingCheckChanged);

		// Starts unchecked - the Debug panel starts hidden, like the interior editor's own. All three boxes
		// are re-synced to the real dock state by RefreshWindowMenuChecks once the layout is loaded.
		WindowDebugCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowDebug"), TEXT("Debug"));
		WindowDebugCheckbox->SetIsChecked(false);
		WindowDebugCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowDebugCheckChanged);
		PluginToolHost = NewObject<UPluginToolHost>(this);
		PluginToolHost->AddMenu(LayoutTools::EScreen::Exterior, [&AddDropdownCheckRow, WindowDropdownStack](const FString& Name, const FString& Label)
		{
			return AddDropdownCheckRow(WindowDropdownStack, *Name, *Label);
		});

		// Starts unchecked - the Navigation panel starts hidden, like the interior editor's own.
		WindowNavigationCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowNavigation"), TEXT("Navigation"));
		WindowNavigationCheckbox->SetIsChecked(false);
		WindowNavigationCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowNavigationCheckChanged);

		UButton* WindowResetLayoutButton = AddDropdownButtonRow(WindowDropdownStack, TEXT("Btn_WindowResetLayout"), TEXT("Reset Layout"));
		WindowResetLayoutButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowResetLayoutClicked);

		// Camera dropdown: Reset, the typed move speed, +, -, Set Default View. Stays open after +/- so the speed
		// can be nudged several times; Reset and Set Default close it.
		CameraMenuDropdown = ConstructDropdown(TEXT("Pnl_CameraDropdown"), EditMenuBarLeftMargin + EditSaveButtonWidth + 2.f * EditMenuButtonWidth);
		UVerticalBox* CameraDropdownStack = Cast<UVerticalBox>(CameraMenuDropdown->GetContent());
		ResetCameraViewButton = AddDropdownButtonRow(CameraDropdownStack, TEXT("Btn_ResetCameraView"), TEXT("Reset"));
		ResetCameraViewButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnResetCameraViewClicked);
		ResetCameraViewButton->SetToolTipText(FText::FromString(TEXT("Reset camera to the default view")));

		// Typed speed entry - see OnCameraSpeedTextCommitted. Shows/accepts the raw speed value directly (up to
		// 6 digits at ExteriorCameraMaxSpeed=300000).
		CameraSpeedTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_CameraSpeed"));
		CameraSpeedTextBox->SetJustification(ETextJustify::Center);
		CameraSpeedTextBox->SetToolTipText(FText::FromString(TEXT("Camera Speed (type a value, then press Enter or click away)")));
		CameraSpeedTextBox->OnTextCommitted.AddUniqueDynamic(this, &UEditScreenWidget::OnCameraSpeedTextCommitted);
		ApplyStandardFont(CameraSpeedTextBox);
		CameraSpeedTextBox->SetWidgetStyle(StartStyle::Field(13.f));
		USizeBox* CameraSpeedSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		CameraSpeedSizeBox->SetWidthOverride(120.f);
		CameraSpeedSizeBox->SetContent(CameraSpeedTextBox);
		UVerticalBoxSlot* CameraSpeedSlot = CameraDropdownStack->AddChildToVerticalBox(CameraSpeedSizeBox);
		CameraSpeedSlot->SetPadding(FMargin(2.f));

		IncreaseCameraSpeedButton = AddDropdownButtonRow(CameraDropdownStack, TEXT("Btn_IncreaseCameraSpeed"), TEXT("+"));
		IncreaseCameraSpeedButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnIncreaseCameraSpeedClicked);
		IncreaseCameraSpeedButton->SetToolTipText(FText::FromString(TEXT("Increase Camera Speed")));
		DecreaseCameraSpeedButton = AddDropdownButtonRow(CameraDropdownStack, TEXT("Btn_DecreaseCameraSpeed"), TEXT("-"));
		DecreaseCameraSpeedButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnDecreaseCameraSpeedClicked);
		DecreaseCameraSpeedButton->SetToolTipText(FText::FromString(TEXT("Decrease Camera Speed")));
		SetDefaultViewButton = AddDropdownButtonRow(CameraDropdownStack, TEXT("Btn_SetDefaultView"), TEXT("Set Default"));
		SetDefaultViewButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnSetDefaultViewClicked);
		SetDefaultViewButton->SetToolTipText(FText::FromString(TEXT("Save the current camera view as the default")));

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
			DockMetrics.TopMargin = EditMenuBarHeight;
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

		// --- Audio panel (Window > Audio): the shared one (AudioPanelWidget.h), also used by the interior
		// editor. Registered with the dock host, not parented here. Starts hidden.
		AudioPanelWidget = CreateWidget<UAudioPanelWidget>(this, UAudioPanelWidget::StaticClass());
		AudioPanelWidget->SetOwnerScreen(this);
		DockHost->RegisterPanel(ExteriorDock::Audio, FText::FromName(ExteriorDock::Audio), AudioPanelWidget);

		// --- Map panel (Window > Map): the shared one (MapPanelWidget.h) - road filters plus, on this screen, the
		// building filters. Starts shown.
		MapPanelWidget = CreateWidget<UMapPanelWidget>(this, UMapPanelWidget::StaticClass());
		MapPanelWidget->SetOwnerScreen(this);
		MapPanelWidget->SetShowBuildingFilters(true);
		DockHost->RegisterPanel(ExteriorDock::Map, FText::FromName(ExteriorDock::Map), MapPanelWidget);

		// --- Building panel (Window > Building): the selected building and the two ways into it. Starts shown.
		BuildingInfoPanelWidget = CreateWidget<UBuildingInfoPanelWidget>(this, UBuildingInfoPanelWidget::StaticClass());
		BuildingInfoPanelWidget->SetOwnerScreen(this);
		BuildingInfoPanelWidget->OnOpen2D = [this]() { OpenInteriorEditor(false); };
		BuildingInfoPanelWidget->OnOpen3D = [this]() { OpenInteriorEditor(true); };
		DockHost->RegisterPanel(ExteriorDock::Building, FText::FromName(ExteriorDock::Building), BuildingInfoPanelWidget);

		// --- Debug panel (Window > Debug): the shared one (DebugPanelWidget.h). The screen supplies the lines that
		// are its own. Starts hidden.
		DebugPanelWidget = CreateWidget<UDebugPanelWidget>(this, UDebugPanelWidget::StaticClass());
		DebugPanelWidget->SetOwnerScreen(this);
		DebugPanelWidget->SetInfoProvider([this]() -> FString
		{
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
			return FString::Printf(TEXT("Selected Building: %s\nCamera Speed: %s\nUnsaved Changes: %s"), *SelectedBuilding, *CameraSpeed, *UnsavedChanges);
		});
		DockHost->RegisterPanel(ExteriorDock::Debug, FText::FromName(ExteriorDock::Debug), DebugPanelWidget);
		PluginToolHost->CreatePanels(this, DockHost, LayoutTools::EScreen::Exterior, [this]()
		{
			if (auto* PC = GetOwningPlayer<ANavPlayerController>())
			{
				if (auto* Screen = CreateWidget<UInteriorScreenWidget>(PC, UInteriorScreenWidget::StaticClass()))
				{
					Screen->AddToViewport(); Screen->ShowValidationPanel(); RemoveFromParent();
				}
			}
		});
		ValidationPanelWidget = Cast<ULayoutValidationPanelWidget>(PluginToolHost->FindPanel(TEXT("Validation")));

		// --- Navigation panel (Window > Navigation): the shared one (NavigationDockPanelWidget.h), also used by the interior
		// editor. Here its dropdowns list each building's main door and a click on the ground sets a point. Starts hidden.
		NavigationPanelWidget = CreateWidget<UNavigationDockPanelWidget>(this, UNavigationDockPanelWidget::StaticClass());
		NavigationPanelWidget->SetOwnerScreen(this);
		DockHost->RegisterPanel(ExteriorDock::Navigation, FText::FromName(ExteriorDock::Navigation), NavigationPanelWidget);
	}

	return Super::RebuildWidget();
}

void UEditScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->OnBuildingSelected.AddUniqueDynamic(this, &UEditScreenWidget::HandleBuildingSelected);
		if (DebugPanelWidget)
		{
			DebugPanelWidget->BindToController();
		}
		// Navigation: the shared panel shows UNavigationPanelWidget's state; repaint it on every change, and let the state ask
		// whether the pointer is over the panel (a click on it must not also pick a point on the ground).
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			// No building is open here: the dropdowns list main doors and a click picks a spot on the ground.
			Nav->SetOutdoorMode(true);
			Nav->SetPointerOverPanelCallback([this](const FVector2D& ScreenPosition)
			{
				return DockHost && DockHost->IsScreenPositionOverPanel(ExteriorDock::Navigation, ScreenPosition);
			});
			Nav->OnStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::HandleNavigationStateChanged);
			PC->SetNavigationStepScreenCallback(this, [this](const FNavigationRoutePoint& Point) { return HandOverToBuildingForStep(Point); });
		}
		if (MapPanelWidget)
		{
			MapPanelWidget->Refresh();
			// The check boxes start in the state the filters should be in; make the controller agree.
			MapPanelWidget->ApplyDefaultsToController();
		}
	}

	if (AudioPanelWidget)
	{
		AudioPanelWidget->Refresh();
	}
	if (NavigationPanelWidget)
	{
		NavigationPanelWidget->RefreshRoomDropdowns();
		NavigationPanelWidget->RefreshDisplay();
	}
	if (BuildingInfoPanelWidget)
	{
		BuildingInfoPanelWidget->Refresh();
	}
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
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->OnStateChanged.RemoveDynamic(this, &UEditScreenWidget::HandleNavigationStateChanged);
			PC->ClearNavigationStepScreenCallback(this);
			Nav->SetPointerOverPanelCallback(nullptr);
		}
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

void UEditScreenWidget::RefreshSaveIndicator()
{
	const ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	const int8 State = PC && PC->HasUnsavedChanges() ? 1 : 0;
	if (State != SaveIndicatorState)
	{
		SaveIndicatorState = State;
		StartStyle::PaintSaveIndicator(SaveIndicatorOuter, SaveIndicatorCore, State == 1);
	}
}

void UEditScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshSaveIndicator();
	// Changes only bump a counter. Batch loads therefore cause one UI rebuild on the next frame,
	// while an idle frame does no building scan, label formatting, sorting, or option comparison.
	if (MapPanelWidget)
	{
		MapPanelWidget->RefreshBuildingFilterDisplayIfChanged();
	}

	FVector2D ReliableSize;
	if (GetReliableCanvasSize(ReliableSize))
	{
		LastKnownCanvasSize = ReliableSize;
	}

	// The Audio and Map panels' controls size themselves from their panel's width, which the user can change at any time.
	SyncAudioPanelWidth();
	SyncMapPanelWidth();
	SyncNavigationPanelWidth();
	SyncBuildingPanelWidth();
	if (PluginToolHost) PluginToolHost->Sync();
	if (BuildingInfoPanelWidget)
	{
		BuildingInfoPanelWidget->RefreshIfChanged();
	}
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
	if (MapPanelWidget)
	{
		MapPanelWidget->RefreshBuildingFilterDisplay();
	}
	// An empty BuildingId means the selection was cleared (see ANavPlayerController::
	// SelectBuildingUnderCursor - clicking empty space, or re-clicking the already-selected building,
	// now deselects instead of only ever adding/switching a selection).
	if (BuildingInfoPanelWidget)
	{
		BuildingInfoPanelWidget->Refresh();
	}
}

void UEditScreenWidget::OpenInteriorEditor(bool b3DView)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		OpenInteriorForBuilding(PC->GetSelectedBuildingId(), b3DView);
	}
}

bool UEditScreenWidget::HandOverToBuildingForStep(const FNavigationRoutePoint& Point)
{
	if (Point.Floor == UNavigationPanelWidget::OutdoorFloor || Point.BuildingId.IsEmpty())
	{
		return false;
	}
	OpenInteriorForBuilding(Point.BuildingId, false);
	// The interior screen is up: a building is open, and its floor and view are the step's.
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (UNavigationPanelWidget* Nav = PC->GetNavigationPanel())
		{
			Nav->SetOutdoorMode(false);
		}
		PC->FollowNavigationStep();
	}
	return true;
}

void UEditScreenWidget::OpenInteriorForBuilding(const FString& BuildingId, bool b3DView)
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	PC->LoadInteriorForBuilding(BuildingId);

	if (UInteriorScreenWidget* InteriorScreen = CreateWidget<UInteriorScreenWidget>(PC, UInteriorScreenWidget::StaticClass()))
	{
		InteriorScreen->AddToViewport();
		RemoveFromParent();
		if (b3DView)
		{
			// The interior screen follows this through its own preview-mode notification.
			PC->SetIn3DPreviewMode(true);
		}
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
	CloseMenuDropdowns();
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->ResetExteriorCameraToDefaultView();
	}
	RefreshCameraSpeedDisplay();
}

void UEditScreenWidget::OnSetDefaultViewClicked()
{
	CloseMenuDropdowns();
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
		// A cancelled "Save as" or a failed write (which says why) keeps the project open.
		if (!PC->SaveCurrentProject())
		{
			return;
		}
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

	// Going back to the start screen closes the project (the person has just saved it or discarded it, or it had no changes): nothing of it stays
	// in the scene behind the start screen, or in the controller for the next New / Open to meet.
	PC->CloseProject();

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
	if (CameraMenuDropdown)
	{
		CameraMenuDropdown->SetVisibility(Dropdown == CameraMenuDropdown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
	if (CameraMenuDropdown)
	{
		CameraMenuDropdown->SetVisibility(ESlateVisibility::Collapsed);
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

void UEditScreenWidget::OnCameraMenuClicked()
{
	RefreshCameraSpeedDisplay();
	OpenMenuDropdown(CameraMenuDropdown);
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
		if (!PC->SaveCurrentProject())
		{
			return;
		}
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

void UEditScreenWidget::OnFileSaveAsClicked()
{
	CloseMenuDropdowns();
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>()) PC->SaveCurrentProjectToNewLocation();
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
		if (PC->TryLoadBuildingDataFromPathOrDatabase(FilePath))
		{
			HandleBuildingSelected(FString());
		}
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
		if (PC->SaveCurrentProject() && PC->TryLoadBuildingDataFromPathOrDatabase(PendingLoadFilePath))
		{
			HandleBuildingSelected(FString());
		}
	}
}

void UEditScreenWidget::HandleFileLoadDiscard()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		if (PC->TryLoadBuildingDataFromPathOrDatabase(PendingLoadFilePath))
		{
			HandleBuildingSelected(FString());
		}
	}
}

void UEditScreenWidget::HandleFileLoadCancel()
{
	// Nothing to do - the dialog already removed itself.
}

// ---------------------------------------------------------------------------
// Docking
// ---------------------------------------------------------------------------

UWidget* UEditScreenWidget::FindScreenWidget(FName Name) const
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
	if (NavigationPanelWidget)
	{
		if (UWidget* Found = NavigationPanelWidget->FindContentWidget(Name))
		{
			return Found;
		}
	}
	return nullptr;
}

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
	// The screen has not been painted yet (the saved layout is loaded before that). The raw viewport is in screen pixels, but the
	// layout works in UI units - pixels divided by the viewport's DPI scale - so a layout loaded against the raw size would
	// come out that much too small.
	const FVector2D Viewport = UWidgetLayoutLibrary::GetViewportSize(this);
	const float DpiScale = UWidgetLayoutLibrary::GetViewportScale(this);
	return DpiScale > KINDA_SMALL_NUMBER ? Viewport / DpiScale : Viewport;
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
	// The panels this screen has in common with the interior one (Audio, Map, ...) are where that screen left them.
	DockSharedPanels::ApplyPublished(*DockHost->GetLayout());
	bSharedPanelsTaken = true;
	DockHost->NotifyLayoutChanged();
	RefreshWindowMenuChecks();
	if (AudioPanelWidget)
	{
		AudioPanelWidget->RefreshAudioList();
	}
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
	if (bSharedPanelsTaken && DockHost)
	{
		DockSharedPanels::Publish(*DockHost->GetLayout());
	}
	RefreshWindowMenuChecks();
}

void UEditScreenWidget::RefreshWindowMenuChecks()
{
	// See UInteriorScreenWidget::RefreshWindowMenuChecks.
	if (bSharedPanelsTaken && DockHost)
	{
		DockSharedPanels::Publish(*DockHost->GetLayout());
	}
	auto Sync = [this](UCheckBox* Checkbox, FName Panel)
	{
		if (Checkbox)
		{
			Checkbox->SetIsChecked(IsDockPanelShown(Panel));
		}
	};
	Sync(WindowAudioCheckbox, ExteriorDock::Audio);
	Sync(WindowRoadsCheckbox, ExteriorDock::Map);
	Sync(WindowBuildingCheckbox, ExteriorDock::Building);
	Sync(WindowDebugCheckbox, ExteriorDock::Debug);
	if (PluginToolHost) PluginToolHost->Sync();
	Sync(WindowNavigationCheckbox, ExteriorDock::Navigation);
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
	// See UInteriorScreenWidget::SaveDockLayoutToDisk.
	if (bSharedPanelsTaken && DockHost)
	{
		DockSharedPanels::Publish(*DockHost->GetLayout());
	}
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
	if (!DockHost || !AudioPanelWidget)
	{
		return;
	}
	const float PanelWidth = DockHost->GetPanelWidth(ExteriorDock::Audio);
	if (PanelWidth <= 0.f)
	{
		return; // hidden, or not laid out yet
	}
	// Minus the panel's 14px padding on each side and room for the scroll bar.
	AudioPanelWidget->SetContentWidth(FMath::Max(80.f, PanelWidth - 48.f));
}

void UEditScreenWidget::SyncMapPanelWidth()
{
	if (!DockHost || !MapPanelWidget)
	{
		return;
	}
	const float PanelWidth = DockHost->GetPanelWidth(ExteriorDock::Map);
	if (PanelWidth <= 0.f)
	{
		return; // hidden, or not laid out yet
	}
	// Minus the panel's 14px padding on each side and room for the scroll bar.
	MapPanelWidget->SetContentWidth(FMath::Max(100.f, PanelWidth - 48.f));
}

void UEditScreenWidget::OnWindowAudioCheckChanged(bool bIsChecked)
{
	SetDockPanelHidden(ExteriorDock::Audio, !bIsChecked);
	if (bIsChecked && AudioPanelWidget)
	{
		// The file list can go stale while the panel is hidden.
		AudioPanelWidget->RefreshAudioList();
	}
}

void UEditScreenWidget::OnWindowRoadsCheckChanged(bool bIsChecked)
{
	SetDockPanelHidden(ExteriorDock::Map, !bIsChecked);
}

void UEditScreenWidget::SyncBuildingPanelWidth()
{
	if (!DockHost || !BuildingInfoPanelWidget)
	{
		return;
	}
	const float PanelWidth = DockHost->GetPanelWidth(ExteriorDock::Building);
	if (PanelWidth <= 0.f)
	{
		return; // hidden, or not laid out yet
	}
	// Minus the panel's 14px padding on each side and room for the scroll bar.
	BuildingInfoPanelWidget->SetContentWidth(FMath::Max(100.f, PanelWidth - 48.f));
}

void UEditScreenWidget::OnWindowBuildingCheckChanged(bool bIsChecked)
{
	SetDockPanelHidden(ExteriorDock::Building, !bIsChecked);
}

void UEditScreenWidget::OnWindowDebugCheckChanged(bool bIsChecked)
{
	SetDockPanelHidden(ExteriorDock::Debug, !bIsChecked);
}


void UEditScreenWidget::OnWindowNavigationCheckChanged(bool bIsChecked)
{
	SetDockPanelHidden(ExteriorDock::Navigation, !bIsChecked);
}

void UEditScreenWidget::HandleNavigationStateChanged()
{
	if (NavigationPanelWidget)
	{
		NavigationPanelWidget->RefreshDisplay();
	}
}

void UEditScreenWidget::SyncNavigationPanelWidth()
{
	if (!DockHost || !NavigationPanelWidget)
	{
		return;
	}
	const float PanelWidth = DockHost->GetPanelWidth(ExteriorDock::Navigation);
	if (PanelWidth <= 0.f)
	{
		return; // hidden, or not laid out yet
	}
	// Minus the panel's 14px padding on each side and room for the scroll bar.
	NavigationPanelWidget->SetContentWidth(FMath::Max(80.f, PanelWidth - 48.f));
}

void UEditScreenWidget::OnWindowResetLayoutClicked()
{
	CloseMenuDropdowns();
	ResetDockLayout();
}
