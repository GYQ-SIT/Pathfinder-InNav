#include "StartScreenWidget.h"

#include "EditScreenWidget.h"
#include "NavPlayerController.h"
#include "TimerManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

UStartScreenWidget::UStartScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedRef<SWidget> UStartScreenWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("StartScreenCanvas"));
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Canvas;

		// Opaque background, filling the whole screen, behind everything else - without this the
		// 3D viewport shows through.
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
		Background->SetBrushColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.f));
		UCanvasPanelSlot* BackgroundSlot = Canvas->AddChildToCanvas(Background);
		BackgroundSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackgroundSlot->SetOffsets(FMargin(0.f));

		UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Txt_Title"));
		Title->SetText(FText::FromString(TEXT("In Nav")));
		FSlateFontInfo TitleFont = Title->GetFont();
		TitleFont.Size = 32;
		Title->SetFont(TitleFont);
		UCanvasPanelSlot* TitleSlot = Canvas->AddChildToCanvas(Title);
		TitleSlot->SetAnchors(FAnchors(0.f, 0.f));
		TitleSlot->SetPosition(FVector2D(20.f, 16.f));
		TitleSlot->SetAutoSize(true);

		UVerticalBox* ButtonStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ButtonStack"));
		UCanvasPanelSlot* StackSlot = Canvas->AddChildToCanvas(ButtonStack);
		StackSlot->SetAnchors(FAnchors(0.f, 0.f));
		StackSlot->SetPosition(FVector2D(20.f, 90.f));
		StackSlot->SetAutoSize(true);

		auto AddButton = [this, ButtonStack](const TCHAR* Name, const TCHAR* Label) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));

			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString(Name) + TEXT("Label")));
			Text->SetText(FText::FromString(FString(Label)));
			Text->SetJustification(ETextJustify::Center);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));

			USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			SizeBox->SetWidthOverride(140.f);
			SizeBox->SetHeightOverride(36.f);
			SizeBox->SetContent(Text);
			Button->SetContent(SizeBox);

			UVerticalBoxSlot* Slot = ButtonStack->AddChildToVerticalBox(Button);
			Slot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
			return Button;
		};

		NewButton = AddButton(TEXT("Btn_New"), TEXT("New"));
		OpenButton = AddButton(TEXT("Btn_Open"), TEXT("Open"));
		QuitButton = AddButton(TEXT("Btn_Quit"), TEXT("Quit"));
	}

	return Super::RebuildWidget();
}

void UStartScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (NewButton)
	{
		NewButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::OnNewClicked);
		NewButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::PlayButtonClickSound);
	}
	if (OpenButton)
	{
		// PlayButtonClickSound bound BEFORE OnOpenClicked (opposite of every other button in the app) -
		// a multicast delegate fires its bound functions in bind order, and OnOpenClicked's
		// BrowseForBuildingDataFile() opens a blocking native file picker, so with the usual order the
		// click sound wouldn't play until the dialog closed and the user had already picked a file.
		OpenButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::PlayButtonClickSound);
		OpenButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::OnOpenClicked);
	}
	if (QuitButton)
	{
		QuitButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::PlayButtonClickSound);
		QuitButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::OnQuitClicked);
	}
}

void UStartScreenWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
	}
}

void UStartScreenWidget::OnNewClicked()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	PC->StartNewProject();
	GoToEditScreen();
}

void UStartScreenWidget::OnOpenClicked()
{
	// PlayButtonClickSound (bound alongside this handler) decodes the click sound asynchronously and
	// only actually starts playing once the game thread processes that decode's completion callback -
	// see ANavPlayerController::PlayButtonClickSound. That can't happen while this function is
	// blocked inside BrowseForBuildingDataFile's native OS dialog below, on the very same call stack,
	// with no tick in between - the sound would only become audible once the dialog had already
	// closed. Deferring the actual browse-and-load by a short delay instead gives that callback time
	// to run first, so the click is audible right away.
	if (UWorld* World = GetWorld())
	{
		FTimerHandle Unused;
		World->GetTimerManager().SetTimer(Unused, FTimerDelegate::CreateUObject(this, &UStartScreenWidget::BrowseAndLoadBuildingData), 0.15f, false);
	}
	else
	{
		BrowseAndLoadBuildingData();
	}
}

void UStartScreenWidget::BrowseAndLoadBuildingData()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}

	const FString FilePath = PC->BrowseForBuildingDataFile();
	if (FilePath.IsEmpty())
	{
		// User cancelled the OS dialog - stay on this screen, nothing to report.
		return;
	}

	// LoadBuildingDataFromPathOrDatabase already broadcasts OnActionFailed itself if the file doesn't parse.
	const int32 Count = PC->LoadBuildingDataFromPathOrDatabase(FilePath);
	if (Count > 0)
	{
		GoToEditScreen();
	}
}

void UStartScreenWidget::OnQuitClicked()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}
	UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, /*bIgnorePlatformRestrictions=*/false);
}

void UStartScreenWidget::GoToEditScreen()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	if (UEditScreenWidget* EditScreen = CreateWidget<UEditScreenWidget>(PC, UEditScreenWidget::StaticClass()))
	{
		EditScreen->AddToViewport();
		RemoveFromParent();
	}
}
