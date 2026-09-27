#include "EditScreenWidget.h"

#include "AudioFileRowWidget.h"
#include "ConfirmDialogWidget.h"
#include "InteriorScreenWidget.h"
#include "NavPlayerController.h"
#include "StartScreenWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	// Same look as UInteriorScreenWidget's own menu bar/dropdown/dock-panel chrome (DockPanelColor,
	// MenuBarHeight) - kept as separate local constants here rather than shared, since this screen
	// has no dock system of its own to also key off them.
	const FLinearColor MenuBarColor(0.03f, 0.03f, 0.03f, 0.88f);
	constexpr float MenuBarHeight = 32.f;
	constexpr float MenuButtonWidth = 50.f;
	constexpr float MenuBarLeftMargin = 10.f;
	constexpr float AudioPanelWidth = 220.f;
	constexpr float AudioFileListHeight = 150.f;
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
		MenuBarBackground->SetBrushColor(MenuBarColor);
		MenuBarBackground->SetPadding(FMargin(MenuBarLeftMargin, 0.f, 0.f, 0.f));
		UCanvasPanelSlot* MenuBarSlot = Canvas->AddChildToCanvas(MenuBarBackground);
		MenuBarSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
		MenuBarSlot->SetAlignment(FVector2D(0.f, 0.f));
		MenuBarSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, MenuBarHeight));
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
		FileMenuButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnFileMenuClicked);
		WindowMenuButton = AddMenuButton(TEXT("Btn_WindowMenu"), TEXT("Window"));
		WindowMenuButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowMenuClicked);

		// Eats all remaining row width so Enter Interior/Back land flush against the bar's right edge.
		USpacer* MenuBarSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
		if (UHorizontalBoxSlot* SpacerSlot = Cast<UHorizontalBoxSlot>(MenuBarRow->AddChildToHorizontalBox(MenuBarSpacer)))
		{
			SpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

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
			SizeBox->SetHeightOverride(28.f); // shorter than MenuBarHeight so it visibly sits inside the bar
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

		EnterInteriorButton = AddBarButton(TEXT("Btn_EnterInterior"), TEXT("Enter Interior"), 120.f);
		EnterInteriorButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnEnterInteriorClicked);
		EnterInteriorButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::PlayButtonClickSound);
		// Hidden until a building is actually selected (see HandleBuildingSelected) - matches
		// WBP_EditScreen's original behavior.
		EnterInteriorButton->SetVisibility(ESlateVisibility::Collapsed);

		BackButton = AddBarButton(TEXT("Btn_Back"), TEXT("Back"), 60.f);
		BackButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnBackClicked);
		BackButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::PlayButtonClickSound);

		// --- Dropdowns (File, Window) + the click-catcher that closes them on an outside click ---
		auto ConstructDropdown = [this, Canvas](const TCHAR* Name, float PositionX) -> UBorder*
		{
			UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(FString(Name) + TEXT("Stack")));
			UBorder* Dropdown = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Name));
			Dropdown->SetBrushColor(MenuBarColor);
			Dropdown->SetPadding(FMargin(4.f));
			Dropdown->SetContent(Stack);
			Dropdown->SetVisibility(ESlateVisibility::Collapsed);
			UCanvasPanelSlot* DropdownSlot = Canvas->AddChildToCanvas(Dropdown);
			DropdownSlot->SetAnchors(FAnchors(0.f, 0.f));
			DropdownSlot->SetAlignment(FVector2D(0.f, 0.f));
			DropdownSlot->SetPosition(FVector2D(PositionX, MenuBarHeight + 4.f));
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
		FileNewButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnFileNewClicked);
		UButton* FileLoadButton = AddDropdownButtonRow(FileDropdownStack, TEXT("Btn_FileLoad"), TEXT("Load"));
		FileLoadButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnFileLoadClicked);

		WindowMenuDropdown = ConstructDropdown(TEXT("Pnl_WindowDropdown"), MenuBarLeftMargin + MenuButtonWidth + 10.f);
		UVerticalBox* WindowDropdownStack = Cast<UVerticalBox>(WindowMenuDropdown->GetContent());
		WindowAudioCheckbox = AddDropdownCheckRow(WindowDropdownStack, TEXT("Chk_WindowAudio"), TEXT("Audio"));
		// Unlike UInteriorScreenWidget's own dock-panel checkboxes (all default checked, since those
		// panels are visible from the start), this is the only Window entry on this screen and starts
		// unchecked - the Audio panel starts hidden, matching the interior editor's own Audio panel.
		WindowAudioCheckbox->SetIsChecked(false);
		WindowAudioCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnWindowAudioCheckChanged);

		MenuClickCatcher = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuClickCatcher"));
		MenuClickCatcher->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		MenuClickCatcher->SetVisibility(ESlateVisibility::Collapsed);
		MenuClickCatcher->OnMouseButtonDownEvent.BindDynamic(this, &UEditScreenWidget::OnMenuClickCatcherMouseButtonDown);
		UCanvasPanelSlot* MenuClickCatcherSlot = Canvas->AddChildToCanvas(MenuClickCatcher);
		MenuClickCatcherSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		MenuClickCatcherSlot->SetOffsets(FMargin(0.f));
		MenuClickCatcherSlot->SetZOrder(5); // above nothing else on this screen, below menu bar/dropdowns (6-7)

		// --- Audio panel (Window > Audio) - same Play/Stop All/mute-click-sound/file-list layout as
		// UInteriorScreenWidget's own Audio panel, reusing the same backend and UAudioFileRowWidget
		// class, just fixed in place - this screen has no dock/drag/resize system to plug into.
		AudioPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_Audio"));
		AudioPanel->SetBrushColor(MenuBarColor);
		AudioPanel->SetPadding(FMargin(10.f));
		AudioPanel->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* AudioPanelSlot = Canvas->AddChildToCanvas(AudioPanel);
		AudioPanelSlot->SetAnchors(FAnchors(0.f, 0.f));
		AudioPanelSlot->SetAlignment(FVector2D(0.f, 0.f));
		AudioPanelSlot->SetPosition(FVector2D(MenuBarLeftMargin, MenuBarHeight + 10.f));
		AudioPanelSlot->SetAutoSize(true);
		AudioPanelSlot->SetZOrder(6);

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
		AudioStopAllButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::OnAudioStopAllClicked);
		AudioStopAllButton->OnClicked.AddUniqueDynamic(this, &UEditScreenWidget::PlayButtonClickSound);
		UTextBlock* AudioStopAllText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_AudioStopAll"));
		AudioStopAllText->SetText(FText::FromString(TEXT("Stop All")));
		AudioStopAllText->SetJustification(ETextJustify::Center);
		AudioStopAllText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(AudioStopAllText);
		USizeBox* AudioStopAllSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size_AudioStopAll"));
		AudioStopAllSizeBox->SetWidthOverride(AudioPanelWidth);
		AudioStopAllSizeBox->SetMinDesiredHeight(32.f);
		AudioStopAllSizeBox->SetContent(AudioStopAllText);
		AudioStopAllButton->SetContent(AudioStopAllSizeBox);
		UVerticalBoxSlot* AudioStopAllSlot = AudioStack->AddChildToVerticalBox(AudioStopAllButton);
		AudioStopAllSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		// Lets the user mute every button's click sound at once (see ANavPlayerController::
		// SetButtonClickSoundEnabled) without touching the Audio files below - own row, not a
		// UAudioFileRowWidget, since it's a setting rather than a playable file.
		UHorizontalBox* AudioClickSoundRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("AudioClickSoundRow"));
		AudioClickSoundCheckbox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("Chk_AudioClickSound"));
		AudioClickSoundCheckbox->SetIsChecked(true);
		AudioClickSoundCheckbox->OnCheckStateChanged.AddUniqueDynamic(this, &UEditScreenWidget::OnAudioClickSoundCheckChanged);
		UHorizontalBoxSlot* AudioClickSoundCheckSlot = AudioClickSoundRow->AddChildToHorizontalBox(AudioClickSoundCheckbox);
		AudioClickSoundCheckSlot->SetVerticalAlignment(VAlign_Center);
		UTextBlock* AudioClickSoundText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_AudioClickSound"));
		AudioClickSoundText->SetText(FText::FromString(TEXT("Button Click Sound")));
		AudioClickSoundText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ApplyStandardFont(AudioClickSoundText);
		UHorizontalBoxSlot* AudioClickSoundTextSlot = AudioClickSoundRow->AddChildToHorizontalBox(AudioClickSoundText);
		AudioClickSoundTextSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
		AudioClickSoundTextSlot->SetVerticalAlignment(VAlign_Center);
		UVerticalBoxSlot* AudioClickSoundRowSlot = AudioStack->AddChildToVerticalBox(AudioClickSoundRow);
		AudioClickSoundRowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		// Rows themselves are built by RefreshAudioList (one UAudioFileRowWidget per file under
		// Data/Audio/), same reasoning as UInteriorScreenWidget's own AudioFileListBox.
		AudioFileListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("AudioFileListBox"));
		USizeBox* AudioFileListSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("AudioFileListSizeBox"));
		AudioFileListSizeBox->SetHeightOverride(AudioFileListHeight);
		AudioFileListSizeBox->SetWidthOverride(AudioPanelWidth);
		AudioFileListSizeBox->SetContent(AudioFileListBox);
		AudioStack->AddChildToVerticalBox(AudioFileListSizeBox);
	}

	return Super::RebuildWidget();
}

void UEditScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->OnBuildingSelected.AddUniqueDynamic(this, &UEditScreenWidget::HandleBuildingSelected);

		// bButtonClickSoundEnabled lives on the PC, not this widget, so it survives this screen being
		// torn down and rebuilt (e.g. Back to the start screen and in again) - the checkbox was
		// hardcoded to SetIsChecked(true) at construction (RebuildWidget has no reliable PC access yet),
		// which showed "on" here even after the user had turned it off, despite the real setting
		// correctly staying off. Sync it to the actual state now that PC is available.
		if (AudioClickSoundCheckbox)
		{
			AudioClickSoundCheckbox->SetIsChecked(PC->IsButtonClickSoundEnabled());
		}
	}

	RefreshAudioList();
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
	}

	Super::NativeDestruct();
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
		PC->LoadBuildingDataFromPath(FilePath);
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
		PC->LoadBuildingDataFromPath(PendingLoadFilePath);
	}
	HandleBuildingSelected(FString());
}

void UEditScreenWidget::HandleFileLoadDiscard()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->DiscardUnsavedChanges();
		PC->LoadBuildingDataFromPath(PendingLoadFilePath);
	}
	HandleBuildingSelected(FString());
}

void UEditScreenWidget::HandleFileLoadCancel()
{
	// Nothing to do - the dialog already removed itself.
}

void UEditScreenWidget::OnWindowAudioCheckChanged(bool bIsChecked)
{
	if (AudioPanel)
	{
		AudioPanel->SetVisibility(bIsChecked ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (bIsChecked)
	{
		RefreshAudioList();
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
		Row->SetWidth(AudioPanelWidth);
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

void UEditScreenWidget::HandleAudioRowPlayClicked(FString FileName)
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayAudioFile(FileName);
		RefreshAudioList();
	}
}
