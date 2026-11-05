#include "ProjectLibraryWidget.h"

#include "BrandTopBarWidget.h"
#include "EditScreenWidget.h"
#include "NavPlayerController.h"
#include "NewProjectDialogWidget.h"
#include "ProjectCardWidget.h"
#include "StartScreenStyle.h"
#include "StartScreenWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"

TSharedRef<SWidget> UProjectLibraryWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		using namespace StartStyle;
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LibraryCanvas"));
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Canvas;

		// Opaque, so the 3D scene does not show through.
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
		Background->SetBrushColor(Rgb(17, 30, 45));
		UCanvasPanelSlot* BackgroundSlot = Canvas->AddChildToCanvas(Background);
		BackgroundSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackgroundSlot->SetOffsets(FMargin(0.f));

		// The page scrolls under the top bar; its content is at most 1580 design pixels wide and centred.
		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("LibraryScroll"));
		UCanvasPanelSlot* ScrollSlot = Canvas->AddChildToCanvas(Scroll);
		ScrollSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		ScrollSlot->SetOffsets(FMargin(0.f, UBrandTopBarWidget::DesignHeight(), 0.f, 0.f));

		USizeBox* Limit = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ContentLimit"));
		Limit->SetWidthOverride(1200.f * DesignToUi); // until the first tick knows the window width
		ContentLimit = Limit;
		UScrollBoxSlot* LimitSlot = Cast<UScrollBoxSlot>(Scroll->AddChild(Limit));
		LimitSlot->SetHorizontalAlignment(HAlign_Center);

		UBorder* Page = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Page"));
		Page->SetBrushColor(FLinearColor::Transparent);
		Page->SetPadding(FMargin(28.f * DesignToUi, 38.f * DesignToUi, 28.f * DesignToUi, 48.f * DesignToUi));
		Limit->SetContent(Page);
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PageColumn"));
		Page->SetContent(Column);

		// ---- Header: kicker, title and help on the left, Back and New project on the right. ----
		UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Header"));
		UVerticalBox* Titles = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Titles"));
		Titles->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_Kicker"), TEXT("PROJECT LIBRARY"), 11.f, Rgb(116, 181, 255), true, 180));
		Titles->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_LibraryTitle"), TEXT("Your projects"), 36.f, Rgb(241, 246, 255), true, -20))
			->SetPadding(FMargin(0.f, 8.f * DesignToUi, 0.f, 0.f));
		Titles->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_LibraryHelp"), TEXT("Select a project to open it in the editor."), 13.f, Rgb(176, 196, 220)))
			->SetPadding(FMargin(0.f, 6.f * DesignToUi, 0.f, 0.f));
		UHorizontalBoxSlot* TitlesSlot = Header->AddChildToHorizontalBox(Titles);
		TitlesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		auto AddHeaderButton = [this, Header](const TCHAR* Name, const TCHAR* Label, const FButtonStyle& Style, float Width, float LeftPadding) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetStyle(Style);
			Button->SetBackgroundColor(FLinearColor::White);
			UTextBlock* Caption = StartStyle::Text(WidgetTree, FString(Name) + TEXT("Label"), Label, 12.f, FLinearColor::White, true);
			Caption->SetJustification(ETextJustify::Center);
			Button->SetContent(Caption);
			USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			Size->SetMinDesiredWidth(Width * StartStyle::DesignToUi);
			Size->SetHeightOverride(39.f * StartStyle::DesignToUi);
			Size->SetContent(Button);
			UHorizontalBoxSlot* Slot = Header->AddChildToHorizontalBox(Size);
			Slot->SetVerticalAlignment(VAlign_Bottom);
			Slot->SetPadding(FMargin(LeftPadding * StartStyle::DesignToUi, 0.f, 0.f, 0.f));
			return Button;
		};
		// New project first, Back at the far right.
		NewButton = AddHeaderButton(TEXT("Btn_LibraryNew"), TEXT("+  New project"), PrimaryButton(), 126.f, 0.f);
		BackButton = AddHeaderButton(TEXT("Btn_LibraryBack"), TEXT("Back"), SecondaryButton(), 82.f, 8.f);
		Column->AddChildToVerticalBox(Header);

		// Failures from opening a file (the controller reports them; nothing else on this screen would show them).
		MessageText = Text(WidgetTree, TEXT("Txt_LibraryMessage"), TEXT(""), 12.f, Rgb(255, 140, 130));
		MessageText->SetAutoWrapText(true);
		MessageText->SetVisibility(ESlateVisibility::Collapsed);
		Column->AddChildToVerticalBox(MessageText)->SetPadding(FMargin(0.f, 14.f * DesignToUi, 0.f, 0.f));

		// ---- Search and count ----
		UHorizontalBox* SearchRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SearchRow"));
		SearchBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_LibrarySearch"));
		SearchBox->SetWidgetStyle(Field(12.f));
		SearchBox->SetHintText(FText::FromString(TEXT("Search projects by name...")));
		SearchBox->OnTextChanged.AddUniqueDynamic(this, &UProjectLibraryWidget::HandleSearchChanged);
		USizeBox* SearchSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SearchSize"));
		SearchSize->SetWidthOverride(320.f * DesignToUi);
		SearchSize->SetContent(SearchBox);
		SearchRow->AddChildToHorizontalBox(SearchSize);
		UHorizontalBoxSlot* SpacerSlot = SearchRow->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("SearchSpacer")));
		SpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		CountText = Text(WidgetTree, TEXT("Txt_LibraryCount"), TEXT(""), 12.f, Rgb(150, 170, 196));
		SearchRow->AddChildToHorizontalBox(CountText)->SetVerticalAlignment(VAlign_Center);
		Column->AddChildToVerticalBox(SearchRow)->SetPadding(FMargin(0.f, 26.f * DesignToUi, 0.f, 20.f * DesignToUi));

		// ---- Cards ----
		EmptyText = Text(WidgetTree, TEXT("Txt_LibraryEmpty"), TEXT(""), 14.f, Rgb(150, 170, 196));
		EmptyText->SetAutoWrapText(true);
		EmptyText->SetVisibility(ESlateVisibility::Collapsed);
		Column->AddChildToVerticalBox(EmptyText)->SetPadding(FMargin(0.f, 24.f * DesignToUi, 0.f, 0.f));

		Grid = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("CardGrid"));
		Grid->SetInnerSlotPadding(FVector2D(19.f * DesignToUi, 19.f * DesignToUi));
		Column->AddChildToVerticalBox(Grid);

		// The same top bar as the start screen, over the page.
		UBrandTopBarWidget* TopBar = CreateWidget<UBrandTopBarWidget>(this, UBrandTopBarWidget::StaticClass());
		UCanvasPanelSlot* TopBarSlot = Canvas->AddChildToCanvas(TopBar);
		TopBarSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
		TopBarSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, UBrandTopBarWidget::DesignHeight()));
	}
	return Super::RebuildWidget();
}

void UProjectLibraryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	for (UButton* Button : { BackButton, NewButton })
	{
		if (Button)
		{
			Button->OnClicked.AddUniqueDynamic(this, &UProjectLibraryWidget::PlayButtonClickSound);
		}
	}
	if (BackButton)
	{
		BackButton->OnClicked.AddUniqueDynamic(this, &UProjectLibraryWidget::HandleBackClicked);
	}
	if (NewButton)
	{
		NewButton->OnClicked.AddUniqueDynamic(this, &UProjectLibraryWidget::HandleNewClicked);
	}
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->OnActionFailed.AddUniqueDynamic(this, &UProjectLibraryWidget::HandleActionFailed);
	}
	Refresh();
}

void UProjectLibraryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// The content takes the window's width minus a margin on each side, but never more than 1580 design pixels. (A size
	// box that is only capped would shrink to its content, which is one card wide here.)
	const float WindowWidth = MyGeometry.GetLocalSize().X;
	if (ContentLimit && WindowWidth > 0.f)
	{
		ContentLimit->SetWidthOverride(FMath::Min(1580.f * StartStyle::DesignToUi, WindowWidth));
	}
}

void UProjectLibraryWidget::NativeDestruct()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->OnActionFailed.RemoveDynamic(this, &UProjectLibraryWidget::HandleActionFailed);
	}
	Super::NativeDestruct();
}

void UProjectLibraryWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
	}
}

void UProjectLibraryWidget::Refresh()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	Entries = PC ? ProjectLibrary::Scan(PC->GetProjectsDirectory()) : TArray<FProjectLibraryEntry>();
	RebuildCards();
}

void UProjectLibraryWidget::RebuildCards()
{
	if (!Grid)
	{
		return;
	}
	Grid->ClearChildren();
	Cards.Reset();

	const FString Needle = SearchBox ? SearchBox->GetText().ToString().TrimStartAndEnd() : FString();
	for (const FProjectLibraryEntry& Entry : Entries)
	{
		if (!Needle.IsEmpty() && !Entry.DisplayName.Contains(Needle, ESearchCase::IgnoreCase) && !Entry.FileName.Contains(Needle, ESearchCase::IgnoreCase))
		{
			continue;
		}
		UProjectCardWidget* Card = CreateWidget<UProjectCardWidget>(this, UProjectCardWidget::StaticClass());
		if (!Card)
		{
			continue;
		}
		Card->SetEntry(Entry);
		Card->OnOpen = [this](const FProjectLibraryEntry& Clicked) { OpenProject(Clicked); };
		Card->OnDuplicate = [this](const FProjectLibraryEntry& Clicked) { DuplicateProject(Clicked); };
		Card->OnRename = [this](const FProjectLibraryEntry& Clicked) { RenameProject(Clicked); };
		Card->OnDelete = [this](const FProjectLibraryEntry& Clicked) { DeleteProject(Clicked); };
		Cards.Add(Card);
		Grid->AddChildToWrapBox(Card);
	}

	if (CountText)
	{
		const int32 Total = Entries.Num();
		CountText->SetText(FText::FromString(Needle.IsEmpty() ? FString::Printf(TEXT("%d project%s"), Total, Total == 1 ? TEXT("") : TEXT("s"))
			: FString::Printf(TEXT("%d of %d projects"), Cards.Num(), Total)));
	}
	if (EmptyText)
	{
		if (Cards.IsEmpty())
		{
			EmptyText->SetText(FText::FromString(Entries.IsEmpty() ? TEXT("There are no projects in the Data folder yet. Create one with New project.") : TEXT("No project matches your search.")));
			EmptyText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			EmptyText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UProjectLibraryWidget::HandleSearchChanged(const FText& Text)
{
	RebuildCards();
}

void UProjectLibraryWidget::SetSearchForTests(const FString& Text)
{
	if (SearchBox)
	{
		SearchBox->SetText(FText::FromString(Text));
	}
	RebuildCards();
}

FText UProjectLibraryWidget::GetCountTextForTests() const
{
	return CountText ? CountText->GetText() : FText::GetEmpty();
}

FText UProjectLibraryWidget::GetMessageTextForTests() const
{
	return MessageText && MessageText->GetVisibility() != ESlateVisibility::Collapsed ? MessageText->GetText() : FText::GetEmpty();
}

void UProjectLibraryWidget::ShowMessage(const FString& Message)
{
	if (!MessageText)
	{
		return;
	}
	MessageText->SetText(FText::FromString(Message));
	MessageText->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}

void UProjectLibraryWidget::HandleActionFailed(FString Reason, EActionFailureSeverity Severity)
{
	ShowMessage(Reason);
}

void UProjectLibraryWidget::GoToEditScreen()
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

void UProjectLibraryWidget::OpenProject(const FProjectLibraryEntry& Entry)
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}
	ShowMessage(FString());
	if (PC->TryLoadBuildingDataFromPathOrDatabase(Entry.FilePath))
	{
		GoToEditScreen();
	}
}

void UProjectLibraryWidget::DuplicateProject(const FProjectLibraryEntry& Entry)
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}
	ShowMessage(FString());
	FString NewPath, Error;
	if (!ProjectLibrary::Duplicate(Entry, PC->GetProjectsDirectory(), NewPath, Error))
	{
		ShowMessage(Error);
		return;
	}
	Refresh();
}

void UProjectLibraryWidget::RenameProject(const FProjectLibraryEntry& Entry)
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}
	ShowMessage(FString());
	TWeakObjectPtr<UProjectLibraryWidget> WeakThis(this);
	const FString Directory = PC->GetProjectsDirectory();
	UNewProjectDialogWidget::OpenRename(PC, Entry.DisplayName, [WeakThis, Entry, Directory](const FString& NewName) -> FString
	{
		FString NewPath, Error;
		if (!ProjectLibrary::Rename(Entry, NewName, Directory, NewPath, Error))
		{
			return Error;
		}
		if (UProjectLibraryWidget* Library = WeakThis.Get())
		{
			Library->Refresh();
		}
		return FString();
	});
}

void UProjectLibraryWidget::DeleteProject(const FProjectLibraryEntry& Entry)
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	if (!PC)
	{
		return;
	}
	ShowMessage(FString());
	TWeakObjectPtr<UProjectLibraryWidget> WeakThis(this);
	const FString Directory = PC->GetProjectsDirectory();
	UNewProjectDialogWidget::OpenConfirm(PC, TEXT("Delete project?"),
		FString::Printf(TEXT("\"%s\" (%s) is taken out of the library. The file is moved to the Deleted folder inside Data, so you can still put it back by hand."),
			*Entry.DisplayName, *Entry.FileName),
		TEXT("Delete"),
		[WeakThis, Entry, Directory]() -> FString
		{
			FString DeletedPath, Error;
			if (!ProjectLibrary::SoftDelete(Entry, Directory, DeletedPath, Error))
			{
				return Error;
			}
			if (UProjectLibraryWidget* Library = WeakThis.Get())
			{
				Library->Refresh();
			}
			return FString();
		});
}

void UProjectLibraryWidget::HandleNewClicked()
{
	ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>();
	TWeakObjectPtr<UProjectLibraryWidget> WeakThis(this);
	UNewProjectDialogWidget::OpenNewProject(PC, [WeakThis]()
	{
		if (UProjectLibraryWidget* Library = WeakThis.Get())
		{
			Library->GoToEditScreen();
		}
	});
}

void UProjectLibraryWidget::HandleBackClicked()
{
	GoBack();
}

void UProjectLibraryWidget::GoBack()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}
	if (UStartScreenWidget* Start = CreateWidget<UStartScreenWidget>(PC, UStartScreenWidget::StaticClass()))
	{
		Start->AddToViewport();
		RemoveFromParent();
	}
}
