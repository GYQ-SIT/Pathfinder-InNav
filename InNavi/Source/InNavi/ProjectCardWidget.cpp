#include "ProjectCardWidget.h"

#include "NavPlayerController.h"
#include "StartScreenStyle.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	// "2026/10/20", in the player's own time zone.
	FString CardDate(const FDateTime& Utc)
	{
		const FDateTime Local = Utc + (FDateTime::Now() - FDateTime::UtcNow());
		return Local.ToString(TEXT("%Y/%m/%d"));
	}
}

TSharedRef<SWidget> UProjectCardWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		using namespace StartStyle;
		const FString Base = TEXT("Card");

		USizeBox* Root = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardRoot"));
		Root->SetWidthOverride(378.f * DesignToUi);
		WidgetTree->RootWidget = Root;

		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardPanel"));
		Panel->SetBrush(Rounded(Rgb(27, 43, 66), Rgb(44, 60, 84), 10.f));
		Panel->SetBrushColor(FLinearColor::White);
		Root->SetContent(Panel);

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CardColumn"));
		Panel->SetContent(Column);

		// ---- Picture area: the same placeholder for every project, with the number of buildings in the corner. ----
		USizeBox* PictureSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PictureSize"));
		PictureSize->SetHeightOverride(195.f * DesignToUi);
		UOverlay* Picture = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Picture"));
		PictureSize->SetContent(Picture);
		Column->AddChildToVerticalBox(PictureSize);

		UBorder* PictureFill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PictureFill"));
		PictureFill->SetBrush(Rounded(Rgb(33, 52, 79), Rgb(33, 52, 79, 0), 10.f, 10.f, 0.f, 0.f, 0.f));
		PictureFill->SetBrushColor(FLinearColor::White);
		UOverlaySlot* FillSlot = Picture->AddChildToOverlay(PictureFill);
		FillSlot->SetHorizontalAlignment(HAlign_Fill);
		FillSlot->SetVerticalAlignment(VAlign_Fill);

		// A small building drawn from bars: a tall outlined block with floor lines, and a lower one beside it.
		USizeBox* IconSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PictureIconSize"));
		IconSize->SetWidthOverride(110.f * DesignToUi);
		IconSize->SetHeightOverride(96.f * DesignToUi);
		UCanvasPanel* Icon = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("PictureIcon"));
		IconSize->SetContent(Icon);
		const FLinearColor IconColor = Rgb(104, 138, 186);
		auto AddBlock = [this, Icon, &IconColor](const TCHAR* Name, float X, float Y, float Width, float Height, int32 Floors)
		{
			UBorder* Block = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(Name));
			Block->SetBrush(Rounded(FLinearColor::Transparent, IconColor, 3.f, 2.f));
			Block->SetBrushColor(FLinearColor::White);
			UCanvasPanelSlot* BlockSlot = Icon->AddChildToCanvas(Block);
			BlockSlot->SetPosition(FVector2D(X, Y) * DesignToUi);
			BlockSlot->SetSize(FVector2D(Width, Height) * DesignToUi);
			for (int32 Floor = 1; Floor < Floors; ++Floor)
			{
				UBorder* Line = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *FString::Printf(TEXT("%s_Floor%d"), Name, Floor));
				Line->SetBrushColor(IconColor);
				UCanvasPanelSlot* LineSlot = Icon->AddChildToCanvas(Line);
				LineSlot->SetPosition(FVector2D(X, Y + Height * Floor / Floors) * DesignToUi);
				LineSlot->SetSize(FVector2D(Width, 2.f) * DesignToUi);
			}
		};
		AddBlock(TEXT("PictureTower"), 8.f, 0.f, 58.f, 96.f, 5);
		AddBlock(TEXT("PictureWing"), 72.f, 40.f, 36.f, 56.f, 3);
		UOverlaySlot* IconSlot = Picture->AddChildToOverlay(IconSize);
		IconSlot->SetHorizontalAlignment(HAlign_Center);
		IconSlot->SetVerticalAlignment(VAlign_Center);

		const int32 Count = Entry.BuildingCount;
		const FString BadgeText = Count <= 0 ? TEXT("NO BUILDINGS") : (Count == 1 ? TEXT("1 BUILDING") : FString::Printf(TEXT("%d BUILDINGS"), Count));
		UBorder* Badge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Badge"));
		Badge->SetBrush(Rounded(Rgb(13, 22, 36, 224), Rgb(58, 76, 102), 6.f));
		Badge->SetBrushColor(FLinearColor::White);
		Badge->SetPadding(FMargin(9.f * DesignToUi, 6.f * DesignToUi));
		Badge->SetContent(Text(WidgetTree, TEXT("Txt_Badge"), BadgeText, 10.f, Rgb(241, 246, 255), true, 20));
		UOverlaySlot* BadgeSlot = Picture->AddChildToOverlay(Badge);
		BadgeSlot->SetHorizontalAlignment(HAlign_Right);
		BadgeSlot->SetVerticalAlignment(VAlign_Top);
		BadgeSlot->SetPadding(FMargin(0.f, 12.f * DesignToUi, 12.f * DesignToUi, 0.f));

		// ---- Body ----
		UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CardBody"));
		UBorder* BodyPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardBodyPad"));
		BodyPad->SetBrushColor(FLinearColor::Transparent);
		BodyPad->SetPadding(FMargin(20.f * DesignToUi, 18.f * DesignToUi, 20.f * DesignToUi, 20.f * DesignToUi));
		BodyPad->SetContent(Body);
		Column->AddChildToVerticalBox(BodyPad);

		// Name and a small state tag: amber when the project has no buildings yet, green when it has.
		UHorizontalBox* NameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("NameRow"));
		UTextBlock* Name = Text(WidgetTree, TEXT("Txt_ProjectName"), Entry.DisplayName, 20.f, Rgb(241, 246, 255), true);
		Name->SetAutoWrapText(true);
		UHorizontalBoxSlot* NameSlot = NameRow->AddChildToHorizontalBox(Name);
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NameSlot->SetVerticalAlignment(VAlign_Center);
		const bool bEmpty = Count <= 0;
		UBorder* Tag = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StateTag"));
		Tag->SetBrush(Rounded(bEmpty ? Rgb(84, 58, 18) : Rgb(22, 74, 66), FLinearColor::Transparent, 11.f, 0.f));
		Tag->SetBrushColor(FLinearColor::White);
		Tag->SetPadding(FMargin(9.f * DesignToUi, 4.f * DesignToUi));
		Tag->SetContent(Text(WidgetTree, TEXT("Txt_StateTag"), bEmpty ? TEXT("Empty") : TEXT("✓ Has buildings"), 11.f, bEmpty ? Rgb(255, 190, 90) : Rgb(84, 222, 160), false));
		UHorizontalBoxSlot* TagSlot = NameRow->AddChildToHorizontalBox(Tag);
		TagSlot->SetVerticalAlignment(VAlign_Top);
		TagSlot->SetPadding(FMargin(10.f * DesignToUi, 2.f * DesignToUi, 0.f, 0.f));
		Body->AddChildToVerticalBox(NameRow);

		UTextBlock* FileName = Text(WidgetTree, TEXT("Txt_FileName"), Entry.FileName, 11.f, Rgb(128, 148, 174));
		FileName->SetAutoWrapText(true);
		Body->AddChildToVerticalBox(FileName)->SetPadding(FMargin(0.f, 4.f * DesignToUi, 0.f, 0.f));

		// Description, with room for two lines so every card is the same height.
		const bool bHasDescription = !Entry.Description.TrimStartAndEnd().IsEmpty();
		UTextBlock* Description = Text(WidgetTree, TEXT("Txt_Description"), bHasDescription ? Entry.Description : TEXT("No description"), 13.f,
			bHasDescription ? Rgb(190, 208, 228) : Rgb(112, 130, 154));
		Description->SetAutoWrapText(true);
		USizeBox* DescriptionSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DescriptionSize"));
		DescriptionSize->SetMinDesiredHeight(44.f * DesignToUi);
		DescriptionSize->SetContent(Description);
		Body->AddChildToVerticalBox(DescriptionSize)->SetPadding(FMargin(0.f, 12.f * DesignToUi, 0.f, 0.f));

		Body->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_Modified"), CardDate(Entry.Modified), 12.f, Rgb(140, 160, 186)))
			->SetPadding(FMargin(0.f, 14.f * DesignToUi, 0.f, 0.f));

		UBorder* Divider = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardDivider"));
		Divider->SetBrushColor(Rgb(46, 62, 86));
		USizeBox* DividerSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardDividerSize"));
		DividerSize->SetHeightOverride(1.f);
		DividerSize->SetContent(Divider);
		Body->AddChildToVerticalBox(DividerSize)->SetPadding(FMargin(0.f, 16.f * DesignToUi, 0.f, 16.f * DesignToUi));

		// Open (wide), Duplicate, Rename.
		UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CardButtons"));
		auto AddButton = [this, Buttons](const TCHAR* ButtonName, const TCHAR* Label, const FButtonStyle& Style, bool bFill, float MinWidth, float LeftPadding) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(ButtonName));
			Button->SetStyle(Style);
			Button->SetBackgroundColor(FLinearColor::White);
			UTextBlock* Caption = Text(WidgetTree, FString(ButtonName) + TEXT("Label"), Label, 12.f, FLinearColor::White, true);
			Caption->SetJustification(ETextJustify::Center);
			Button->SetContent(Caption);
			USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(ButtonName) + TEXT("Size")));
			Size->SetMinDesiredWidth(MinWidth * DesignToUi);
			Size->SetHeightOverride(39.f * DesignToUi);
			Size->SetContent(Button);
			UHorizontalBoxSlot* Slot = Buttons->AddChildToHorizontalBox(Size);
			Slot->SetPadding(FMargin(LeftPadding * DesignToUi, 0.f, 0.f, 0.f));
			if (bFill)
			{
				Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
			return Button;
		};
		OpenButton = AddButton(TEXT("Btn_OpenProject"), TEXT("Open project →"), PrimaryButton(), true, 120.f, 0.f);
		DuplicateButton = AddButton(TEXT("Btn_DuplicateProject"), TEXT("Duplicate"), SecondaryButton(), false, 74.f, 8.f);
		RenameButton = AddButton(TEXT("Btn_RenameProject"), TEXT("Rename"), SecondaryButton(), false, 66.f, 8.f);

		// Delete: a square button with a trash can drawn from bars, red when hovered.
		DeleteButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_DeleteProject"));
		DeleteButton->SetStyle(Button(Rgb(27, 41, 63), Rgb(96, 34, 42), Rgb(72, 24, 32), Rgb(61, 78, 104)));
		DeleteButton->SetBackgroundColor(FLinearColor::White);
		USizeBox* TrashSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TrashSize"));
		TrashSize->SetWidthOverride(18.f * DesignToUi);
		TrashSize->SetHeightOverride(20.f * DesignToUi);
		UCanvasPanel* Trash = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Trash"));
		const FLinearColor TrashColor = Rgb(203, 218, 236);
		auto AddTrashPart = [this, Trash](const TCHAR* PartName, float X, float Y, float Width, float Height, const FSlateBrush* Brush, const FLinearColor& Color)
		{
			UBorder* Part = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(PartName));
			if (Brush)
			{
				Part->SetBrush(*Brush);
				Part->SetBrushColor(FLinearColor::White);
			}
			else
			{
				Part->SetBrushColor(Color);
			}
			UCanvasPanelSlot* PartSlot = Trash->AddChildToCanvas(Part);
			PartSlot->SetPosition(FVector2D(X, Y) * DesignToUi);
			PartSlot->SetSize(FVector2D(Width, Height) * DesignToUi);
		};
		const FSlateBrush CanBrush = Rounded(FLinearColor::Transparent, TrashColor, 2.f, 2.f);
		AddTrashPart(TEXT("TrashHandle"), 6.f, 0.f, 6.f, 3.f, nullptr, TrashColor);
		AddTrashPart(TEXT("TrashLid"), 0.f, 3.f, 18.f, 2.f, nullptr, TrashColor);
		AddTrashPart(TEXT("TrashCan"), 2.f, 6.f, 14.f, 14.f, &CanBrush, TrashColor);
		AddTrashPart(TEXT("TrashLineLeft"), 6.f, 9.f, 2.f, 8.f, nullptr, TrashColor);
		AddTrashPart(TEXT("TrashLineRight"), 10.f, 9.f, 2.f, 8.f, nullptr, TrashColor);
		TrashSize->SetContent(Trash);
		DeleteButton->SetContent(TrashSize);
		USizeBox* DeleteBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Btn_DeleteProjectSize"));
		DeleteBox->SetWidthOverride(39.f * DesignToUi);
		DeleteBox->SetHeightOverride(39.f * DesignToUi);
		DeleteBox->SetContent(DeleteButton);
		UHorizontalBoxSlot* DeleteSlot = Buttons->AddChildToHorizontalBox(DeleteBox);
		DeleteSlot->SetPadding(FMargin(8.f * DesignToUi, 0.f, 0.f, 0.f));
		DeleteButton->SetToolTipText(FText::FromString(TEXT("Delete")));
		Body->AddChildToVerticalBox(Buttons);

		// What cannot be done to this file is greyed out, with the reason as its tooltip.
		if (Entry.bIsDatabase)
		{
			DuplicateButton->SetIsEnabled(false);
			DuplicateButton->SetToolTipText(FText::FromString(TEXT("A map database cannot be duplicated.")));
		}
		if (Entry.bIsSample)
		{
			DeleteButton->SetIsEnabled(false);
			DeleteButton->SetToolTipText(FText::FromString(TEXT("The sample files that come with the project cannot be deleted.")));
		}
		if (Entry.bIsDatabase || Entry.bIsSample)
		{
			RenameButton->SetIsEnabled(false);
			RenameButton->SetToolTipText(FText::FromString(TEXT("The sample files that come with the project cannot be renamed. Duplicate it and rename the copy.")));
		}
	}
	return Super::RebuildWidget();
}

void UProjectCardWidget::NativeConstruct()
{
	Super::NativeConstruct();
	for (UButton* Button : { OpenButton, DuplicateButton, RenameButton, DeleteButton })
	{
		if (Button)
		{
			Button->OnClicked.AddUniqueDynamic(this, &UProjectCardWidget::PlayButtonClickSound);
		}
	}
	if (OpenButton)
	{
		OpenButton->OnClicked.AddUniqueDynamic(this, &UProjectCardWidget::HandleOpenClicked);
	}
	if (DuplicateButton)
	{
		DuplicateButton->OnClicked.AddUniqueDynamic(this, &UProjectCardWidget::HandleDuplicateClicked);
	}
	if (RenameButton)
	{
		RenameButton->OnClicked.AddUniqueDynamic(this, &UProjectCardWidget::HandleRenameClicked);
	}
	if (DeleteButton)
	{
		DeleteButton->OnClicked.AddUniqueDynamic(this, &UProjectCardWidget::HandleDeleteClicked);
	}
}

void UProjectCardWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
	}
}

void UProjectCardWidget::HandleOpenClicked()
{
	if (OnOpen)
	{
		OnOpen(Entry);
	}
}

void UProjectCardWidget::HandleDuplicateClicked()
{
	if (OnDuplicate)
	{
		OnDuplicate(Entry);
	}
}

void UProjectCardWidget::HandleRenameClicked()
{
	if (OnRename)
	{
		OnRename(Entry);
	}
}

void UProjectCardWidget::HandleDeleteClicked()
{
	if (OnDelete)
	{
		OnDelete(Entry);
	}
}
