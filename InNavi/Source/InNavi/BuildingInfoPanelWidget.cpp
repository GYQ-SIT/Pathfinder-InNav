#include "BuildingInfoPanelWidget.h"

#include "BuildingSummary.h"
#include "FootprintPreviewWidget.h"
#include "NavPlayerController.h"
#include "StartScreenStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

TSharedRef<SWidget> UBuildingInfoPanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		using namespace StartStyle;
		// The dock frame paints the panel background; this root only adds the usual panel padding.
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_Building"));
		Root->SetBrushColor(FLinearColor::Transparent);
		Root->SetPadding(FMargin(14.f));
		WidgetTree->RootWidget = Root;

		ContentSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BuildingContentSize"));
		ContentSize->SetWidthOverride(ContentWidth);
		Root->SetContent(ContentSize);
		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BuildingStack"));
		ContentSize->SetContent(Stack);

		Stack->AddChildToVerticalBox(Text(WidgetTree, TEXT("Txt_BuildingKicker"), TEXT("SELECTED BUILDING"), 10.f, Rgb(116, 181, 255), true, 160));

		EmptyText = Text(WidgetTree, TEXT("Txt_BuildingEmpty"), TEXT("Select a building in the Map panel, or click one in the scene."), 12.f, Rgb(150, 170, 196));
		EmptyText->SetAutoWrapText(true);
		Stack->AddChildToVerticalBox(EmptyText)->SetPadding(FMargin(0.f, 12.f * DesignToUi, 0.f, 0.f));

		Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BuildingContent"));
		Content->SetVisibility(ESlateVisibility::Collapsed);
		Stack->AddChildToVerticalBox(Content)->SetPadding(FMargin(0.f, 12.f * DesignToUi, 0.f, 0.f));

		// ---- The building's outline ----
		USizeBox* PictureSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BuildingPictureSize"));
		PictureSize->SetHeightOverride(150.f * DesignToUi);
		UOverlay* Picture = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("BuildingPicture"));
		PictureSize->SetContent(Picture);
		UBorder* PictureFill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BuildingPictureFill"));
		PictureFill->SetBrush(Rounded(Rgb(28, 45, 70), Rgb(48, 68, 96), 10.f));
		PictureFill->SetBrushColor(FLinearColor::White);
		UOverlaySlot* FillSlot = Picture->AddChildToOverlay(PictureFill);
		FillSlot->SetHorizontalAlignment(HAlign_Fill);
		FillSlot->SetVerticalAlignment(VAlign_Fill);
		Outline = WidgetTree->ConstructWidget<UFootprintPreviewWidget>(UFootprintPreviewWidget::StaticClass(), TEXT("BuildingOutline"));
		UOverlaySlot* OutlineSlot = Picture->AddChildToOverlay(Outline);
		OutlineSlot->SetHorizontalAlignment(HAlign_Fill);
		OutlineSlot->SetVerticalAlignment(VAlign_Fill);
		OutlineSlot->SetPadding(FMargin(10.f * DesignToUi));
		Content->AddChildToVerticalBox(PictureSize);

		// ---- Name and id ----
		NameText = Text(WidgetTree, TEXT("Txt_BuildingName"), TEXT(""), 24.f, Rgb(241, 246, 255), true);
		NameText->SetAutoWrapText(true);
		Content->AddChildToVerticalBox(NameText)->SetPadding(FMargin(0.f, 16.f * DesignToUi, 0.f, 0.f));
		IdText = Text(WidgetTree, TEXT("Txt_BuildingId"), TEXT(""), 13.f, Rgb(160, 180, 204));
		IdText->SetAutoWrapText(true);
		Content->AddChildToVerticalBox(IdText)->SetPadding(FMargin(0.f, 4.f * DesignToUi, 0.f, 0.f));

		// ---- Numbers: floors and rooms side by side, lifts & stairs on a row of its own ----
		UBorder* Stats = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BuildingStats"));
		Stats->SetBrush(Rounded(Rgb(20, 34, 54), Rgb(44, 62, 88), 8.f));
		Stats->SetBrushColor(FLinearColor::White);
		UVerticalBox* Grid = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BuildingStatGrid"));
		Stats->SetContent(Grid);
		UHorizontalBox* TopRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BuildingStatTopRow"));
		Grid->AddChildToVerticalBox(TopRow);

		auto MakeDivider = [this](const TCHAR* Name, bool bVertical) -> USizeBox*
		{
			UBorder* Line = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(FString(Name)));
			Line->SetBrushColor(StartStyle::Rgb(44, 62, 88));
			USizeBox* LineSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			if (bVertical)
			{
				LineSize->SetWidthOverride(1.f);
			}
			else
			{
				LineSize->SetHeightOverride(1.f);
			}
			LineSize->SetContent(Line);
			return LineSize;
		};
		auto AddCell = [this](UHorizontalBox* Row, const TCHAR* Name, const TCHAR* Label, UTextBlock*& OutValue)
		{
			UVerticalBox* Cell = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(FString(Name) + TEXT("Cell")));
			Cell->AddChildToVerticalBox(StartStyle::Text(WidgetTree, FString(Name) + TEXT("Label"), Label, 11.f, StartStyle::Rgb(150, 170, 196)));
			OutValue = StartStyle::Text(WidgetTree, FString(Name) + TEXT("Value"), TEXT("0"), 22.f, StartStyle::Rgb(241, 246, 255), true);
			Cell->AddChildToVerticalBox(OutValue)->SetPadding(FMargin(0.f, 4.f * StartStyle::DesignToUi, 0.f, 0.f));
			UBorder* CellPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(FString(Name) + TEXT("Pad")));
			CellPad->SetBrushColor(FLinearColor::Transparent);
			CellPad->SetPadding(FMargin(12.f * StartStyle::DesignToUi, 11.f * StartStyle::DesignToUi));
			CellPad->SetContent(Cell);
			UHorizontalBoxSlot* CellSlot = Row->AddChildToHorizontalBox(CellPad);
			CellSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			CellSlot->SetVerticalAlignment(VAlign_Fill);
		};
		AddCell(TopRow, TEXT("Floors"), TEXT("Floors"), FloorsValue);
		TopRow->AddChildToHorizontalBox(MakeDivider(TEXT("RoomsDivider"), true))->SetVerticalAlignment(VAlign_Fill);
		AddCell(TopRow, TEXT("Rooms"), TEXT("Rooms"), RoomsValue);
		Grid->AddChildToVerticalBox(MakeDivider(TEXT("LinksDivider"), false));
		UHorizontalBox* BottomRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BuildingStatBottomRow"));
		Grid->AddChildToVerticalBox(BottomRow);
		AddCell(BottomRow, TEXT("Links"), TEXT("Lifts & stairs"), LinksValue);
		Content->AddChildToVerticalBox(Stats)->SetPadding(FMargin(0.f, 18.f * DesignToUi, 0.f, 0.f));

		// ---- Status: Empty (no rooms yet) or Has rooms ----
		UHorizontalBox* StatusRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BuildingStatusRow"));
		UHorizontalBoxSlot* StatusLabelSlot = StatusRow->AddChildToHorizontalBox(Text(WidgetTree, TEXT("Txt_BuildingStatusLabel"), TEXT("STATUS"), 10.f, Rgb(170, 190, 214), true, 120));
		StatusLabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		StatusLabelSlot->SetVerticalAlignment(VAlign_Center);
		StatusTag = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BuildingStatusTag"));
		StatusTag->SetBrush(Rounded(Rgb(84, 58, 18), FLinearColor::Transparent, 11.f, 0.f));
		StatusTag->SetBrushColor(FLinearColor::White);
		StatusTag->SetPadding(FMargin(9.f * DesignToUi, 4.f * DesignToUi));
		StatusText = Text(WidgetTree, TEXT("Txt_BuildingStatus"), TEXT("Empty"), 11.f, Rgb(255, 190, 90));
		StatusTag->SetContent(StatusText);
		StatusRow->AddChildToHorizontalBox(StatusTag);
		Content->AddChildToVerticalBox(StatusRow)->SetPadding(FMargin(0.f, 18.f * DesignToUi, 0.f, 0.f));

		// ---- The two ways in ----
		auto AddWideButton = [this](const TCHAR* Name, const TCHAR* Label, const FButtonStyle& Style) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetStyle(Style);
			Button->SetBackgroundColor(FLinearColor::White);
			UTextBlock* Caption = StartStyle::Text(WidgetTree, FString(Name) + TEXT("Label"), Label, 13.f, FLinearColor::White, true);
			Caption->SetJustification(ETextJustify::Center);
			Caption->SetAutoWrapText(true);
			UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(FString(Name) + TEXT("Pad")));
			Pad->SetBrushColor(FLinearColor::Transparent);
			Pad->SetPadding(FMargin(10.f * StartStyle::DesignToUi, 12.f * StartStyle::DesignToUi));
			Pad->SetContent(Caption);
			Button->SetContent(Pad);
			return Button;
		};
		Open2DButton = AddWideButton(TEXT("Btn_Open2DEditor"), TEXT("Open 2D floor editor →"), PrimaryButton());
		Content->AddChildToVerticalBox(Open2DButton)->SetPadding(FMargin(0.f, 16.f * DesignToUi, 0.f, 0.f));
		Open3DButton = AddWideButton(TEXT("Btn_Open3DView"), TEXT("Open 3D building view →"), SecondaryButton());
		Content->AddChildToVerticalBox(Open3DButton)->SetPadding(FMargin(0.f, 8.f * DesignToUi, 0.f, 0.f));
	}
	return Super::RebuildWidget();
}

void UBuildingInfoPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	for (UButton* Button : { Open2DButton, Open3DButton })
	{
		if (Button)
		{
			Button->OnClicked.AddUniqueDynamic(this, &UBuildingInfoPanelWidget::PlayButtonClickSound);
		}
	}
	if (Open2DButton)
	{
		Open2DButton->OnClicked.AddUniqueDynamic(this, &UBuildingInfoPanelWidget::HandleOpen2DClicked);
	}
	if (Open3DButton)
	{
		Open3DButton->OnClicked.AddUniqueDynamic(this, &UBuildingInfoPanelWidget::HandleOpen3DClicked);
	}
}

void UBuildingInfoPanelWidget::HandleOpen2DClicked()
{
	if (OnOpen2D)
	{
		OnOpen2D();
	}
}

void UBuildingInfoPanelWidget::HandleOpen3DClicked()
{
	if (OnOpen3D)
	{
		OnOpen3D();
	}
}

void UBuildingInfoPanelWidget::SetContentWidth(float Width)
{
	if (FMath::IsNearlyEqual(Width, ContentWidth, 0.5f))
	{
		return;
	}
	ContentWidth = Width;
	if (ContentSize)
	{
		ContentSize->SetWidthOverride(Width);
	}
}

void UBuildingInfoPanelWidget::RefreshIfChanged()
{
	ANavPlayerController* PC = GetController();
	if (PC && (DisplayedId != PC->GetSelectedBuildingId() || DisplayedRevision != PC->GetBuildingResultsRevision()))
	{
		Refresh();
	}
}

void UBuildingInfoPanelWidget::Refresh()
{
	ANavPlayerController* PC = GetController();
	FBuildingSummary Summary;
	bShowingBuilding = PC && PC->GetSelectedBuildingSummary(Summary);
	DisplayedId = PC ? PC->GetSelectedBuildingId() : FString();
	DisplayedRevision = PC ? PC->GetBuildingResultsRevision() : MAX_uint64;

	if (EmptyText)
	{
		EmptyText->SetVisibility(bShowingBuilding ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
	if (Content)
	{
		Content->SetVisibility(bShowingBuilding ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (!bShowingBuilding)
	{
		return;
	}

	using namespace StartStyle;
	if (Outline)
	{
		Outline->SetFootprint(Summary.Footprint);
	}
	if (NameText)
	{
		NameText->SetText(FText::FromString(Summary.Name.IsEmpty() ? Summary.Id : Summary.Name));
	}
	if (IdText)
	{
		IdText->SetText(FText::FromString(FString::Printf(TEXT("Building ID: %s"), *Summary.Id)));
	}
	if (FloorsValue)
	{
		FloorsValue->SetText(FText::AsNumber(Summary.Floors));
	}
	if (RoomsValue)
	{
		RoomsValue->SetText(FText::AsNumber(Summary.Rooms));
	}
	if (LinksValue)
	{
		LinksValue->SetText(FText::AsNumber(Summary.VerticalLinks));
	}
	const bool bEmpty = Summary.Rooms <= 0;
	if (StatusTag)
	{
		StatusTag->SetBrush(Rounded(bEmpty ? Rgb(84, 58, 18) : Rgb(22, 74, 66), FLinearColor::Transparent, 11.f, 0.f));
	}
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(bEmpty ? TEXT("Empty") : TEXT("✓ Has rooms")));
		StatusText->SetColorAndOpacity(FSlateColor(bEmpty ? Rgb(255, 190, 90) : Rgb(84, 222, 160)));
	}
}

FString UBuildingInfoPanelWidget::GetNameTextForTests() const { return NameText ? NameText->GetText().ToString() : FString(); }
FString UBuildingInfoPanelWidget::GetIdTextForTests() const { return IdText ? IdText->GetText().ToString() : FString(); }
FString UBuildingInfoPanelWidget::GetFloorsTextForTests() const { return FloorsValue ? FloorsValue->GetText().ToString() : FString(); }
FString UBuildingInfoPanelWidget::GetRoomsTextForTests() const { return RoomsValue ? RoomsValue->GetText().ToString() : FString(); }
FString UBuildingInfoPanelWidget::GetLinksTextForTests() const { return LinksValue ? LinksValue->GetText().ToString() : FString(); }
FString UBuildingInfoPanelWidget::GetStatusTextForTests() const { return StatusText ? StatusText->GetText().ToString() : FString(); }
int32 UBuildingInfoPanelWidget::GetOutlinePointCountForTests() const { return Outline ? Outline->GetPointCountForTests() : 0; }
