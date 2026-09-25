#include "StartScreenWidget.h"

#include "NavPlayerController.h"
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
	static ConstructorHelpers::FClassFinder<UUserWidget> EditScreenFinder(TEXT("/Game/WBP_EditScreen"));
	if (EditScreenFinder.Succeeded())
	{
		EditScreenClass = EditScreenFinder.Class;
	}
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
	}

	return Super::RebuildWidget();
}

void UStartScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (NewButton)
	{
		NewButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::OnNewClicked);
	}
	if (OpenButton)
	{
		OpenButton->OnClicked.AddUniqueDynamic(this, &UStartScreenWidget::OnOpenClicked);
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

	// LoadBuildingDataFromPath already broadcasts OnActionFailed itself if the file doesn't parse.
	const int32 Count = PC->LoadBuildingDataFromPath(FilePath);
	if (Count > 0)
	{
		GoToEditScreen();
	}
}

void UStartScreenWidget::GoToEditScreen()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC || !EditScreenClass)
	{
		UE_LOG(LogTemp, Error, TEXT("StartScreenWidget: cannot open the edit screen - %s"),
			EditScreenClass ? TEXT("no owning player") : TEXT("WBP_EditScreen class not found"));
		return;
	}

	if (UUserWidget* EditScreen = CreateWidget<UUserWidget>(PC, EditScreenClass))
	{
		EditScreen->AddToViewport();
		RemoveFromParent();
	}
}
