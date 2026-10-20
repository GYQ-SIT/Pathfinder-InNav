#include "NewProjectDialogWidget.h"

#include "NavPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/SlateTypes.h"

// Colours and sizes come from the design team's page. Its sizes are browser pixels; the project's UI scale is 0.77, so
// they are divided by it, and font sizes (points, drawn at 96 DPI) are 3/4 of a pixel size.
namespace
{
	constexpr float DialogUiScale = 1.f / 0.77f;
	constexpr float DialogPixelsToFont = 0.75f;

	FLinearColor DialogRgb(uint8 R, uint8 G, uint8 B, uint8 A = 255)
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor(R, G, B));
		Color.A = A / 255.f;
		return Color;
	}

	FSlateBrush DialogRounded(const FLinearColor& Fill, const FLinearColor& Outline, float Radius, float OutlineWidth)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		Brush.OutlineSettings.Color = FSlateColor(Outline);
		Brush.OutlineSettings.Width = OutlineWidth;
		return Brush;
	}

	const FLinearColor DialogAmber = DialogRgb(217, 119, 6);
	const FLinearColor DialogFieldBorder = DialogRgb(54, 70, 92);
	const FLinearColor DialogFieldFill = DialogRgb(15, 26, 42);

	FEditableTextBoxStyle DialogFieldStyle(float PixelSize)
	{
		FEditableTextBoxStyle Style;
		const float Radius = 5.f * DialogUiScale;
		Style.BackgroundImageNormal = DialogRounded(DialogFieldFill, DialogFieldBorder, Radius, 1.f);
		Style.BackgroundImageHovered = DialogRounded(DialogFieldFill, DialogRgb(74, 90, 113), Radius, 1.f);
		Style.BackgroundImageFocused = DialogRounded(DialogFieldFill, DialogAmber, Radius, 1.f);
		Style.BackgroundImageReadOnly = DialogRounded(DialogFieldFill, DialogFieldBorder, Radius, 1.f);
		Style.Padding = FMargin(10.f * DialogUiScale, 9.f * DialogUiScale);
		Style.ForegroundColor = FSlateColor(DialogRgb(244, 247, 252));
		Style.FocusedForegroundColor = FSlateColor(DialogRgb(244, 247, 252));
		Style.TextStyle.Font.Size = FMath::RoundToInt(PixelSize * DialogUiScale * DialogPixelsToFont);
		Style.TextStyle.ColorAndOpacity = FSlateColor(DialogRgb(244, 247, 252));
		return Style;
	}

	FButtonStyle DialogButtonStyle(const FLinearColor& Fill, const FLinearColor& Hover, const FLinearColor& Pressed, const FLinearColor& Outline)
	{
		FButtonStyle Style;
		const float Radius = 6.f * DialogUiScale;
		Style.Normal = DialogRounded(Fill, Outline, Radius, 1.f);
		Style.Hovered = DialogRounded(Hover, Outline, Radius, 1.f);
		Style.Pressed = DialogRounded(Pressed, Outline, Radius, 1.f);
		Style.Disabled = DialogRounded(Fill * 0.5f, Outline, Radius, 1.f);
		Style.NormalPadding = FMargin(0.f);
		Style.PressedPadding = FMargin(0.f);
		return Style;
	}
}

TSharedRef<SWidget> UNewProjectDialogWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto MakeText = [this](const TCHAR* Name, const TCHAR* Value, float PixelSize, const FLinearColor& Color, bool bBold, int32 Spacing = 0) -> UTextBlock*
		{
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
			Text->SetText(FText::FromString(FString(Value)));
			Text->SetColorAndOpacity(FSlateColor(Color));
			FSlateFontInfo Font = Text->GetFont();
			Font.Size = FMath::RoundToInt(PixelSize * DialogUiScale * DialogPixelsToFont);
			Font.LetterSpacing = Spacing;
			if (bBold)
			{
				Font.TypefaceFontName = TEXT("Bold");
			}
			Text->SetFont(Font);
			return Text;
		};

		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("DialogRoot"));
		WidgetTree->RootWidget = Root;

		// Dims everything behind the dialog (and keeps clicks from reaching it).
		UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
		Backdrop->SetBrushColor(DialogRgb(5, 11, 21, 186));
		UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop);
		BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
		BackdropSlot->SetVerticalAlignment(VAlign_Fill);

		USizeBox* PanelSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelSize"));
		PanelSize->SetWidthOverride(490.f * DialogUiScale);
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
		Panel->SetBrush(DialogRounded(DialogRgb(27, 40, 62), DialogRgb(54, 70, 92), 12.f * DialogUiScale, 1.f));
		Panel->SetBrushColor(FLinearColor::White);
		PanelSize->SetContent(Panel);
		UOverlaySlot* PanelSlot = Root->AddChildToOverlay(PanelSize);
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
		Panel->SetContent(Column);

		// Header: title and one line of help.
		UVerticalBox* Header = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Header"));
		Header->AddChildToVerticalBox(MakeText(TEXT("Txt_DialogTitle"), TEXT("Create a new project"), 23.f, DialogRgb(241, 246, 255), true));
		UTextBlock* Help = MakeText(TEXT("Txt_DialogHelp"), TEXT("Name your project. A blank project file is created in the Data folder."), 13.f, DialogRgb(176, 192, 210), false);
		Help->SetAutoWrapText(true);
		Header->AddChildToVerticalBox(Help)->SetPadding(FMargin(0.f, 6.f * DialogUiScale, 0.f, 0.f));
		UBorder* HeaderPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HeaderPad"));
		HeaderPad->SetBrushColor(FLinearColor::Transparent);
		HeaderPad->SetPadding(FMargin(24.f * DialogUiScale, 22.f * DialogUiScale, 24.f * DialogUiScale, 16.f * DialogUiScale));
		HeaderPad->SetContent(Header);
		Column->AddChildToVerticalBox(HeaderPad);

		UBorder* Divider = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Divider"));
		Divider->SetBrushColor(DialogRgb(45, 60, 82));
		USizeBox* DividerSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DividerSize"));
		DividerSize->SetHeightOverride(1.f);
		DividerSize->SetContent(Divider);
		Column->AddChildToVerticalBox(DividerSize);

		// Body: the two fields.
		UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Body"));
		const FLinearColor LabelColor = DialogRgb(165, 182, 204);
		Body->AddChildToVerticalBox(MakeText(TEXT("Txt_NameLabel"), TEXT("Project name *"), 11.f, LabelColor, true, 30));

		NameBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Txt_ProjectName"));
		NameBox->SetWidgetStyle(DialogFieldStyle(13.f));
		NameBox->SetHintText(FText::FromString(TEXT("e.g. Campus Navigation")));
		NameBox->OnTextChanged.AddUniqueDynamic(this, &UNewProjectDialogWidget::HandleNameChanged);
		NameBox->OnTextCommitted.AddUniqueDynamic(this, &UNewProjectDialogWidget::HandleNameCommitted);
		Body->AddChildToVerticalBox(NameBox)->SetPadding(FMargin(0.f, 6.f * DialogUiScale, 0.f, 0.f));

		ErrorText = MakeText(TEXT("Txt_NameError"), TEXT(""), 12.f, DialogRgb(255, 140, 130), false);
		ErrorText->SetAutoWrapText(true);
		ErrorText->SetVisibility(ESlateVisibility::Collapsed);
		Body->AddChildToVerticalBox(ErrorText)->SetPadding(FMargin(0.f, 6.f * DialogUiScale, 0.f, 0.f));

		Body->AddChildToVerticalBox(MakeText(TEXT("Txt_DescriptionLabel"), TEXT("Description"), 11.f, LabelColor, true, 30))
			->SetPadding(FMargin(0.f, 16.f * DialogUiScale, 0.f, 0.f));
		DescriptionBox = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>(UMultiLineEditableTextBox::StaticClass(), TEXT("Txt_ProjectDescription"));
		DescriptionBox->WidgetStyle = DialogFieldStyle(13.f);
		DescriptionBox->SetHintText(FText::FromString(TEXT("Purpose or short description")));
		USizeBox* DescriptionSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DescriptionSize"));
		DescriptionSize->SetHeightOverride(76.f * DialogUiScale);
		DescriptionSize->SetContent(DescriptionBox);
		Body->AddChildToVerticalBox(DescriptionSize)->SetPadding(FMargin(0.f, 6.f * DialogUiScale, 0.f, 0.f));

		UBorder* BodyPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BodyPad"));
		BodyPad->SetBrushColor(FLinearColor::Transparent);
		BodyPad->SetPadding(FMargin(24.f * DialogUiScale, 18.f * DialogUiScale, 24.f * DialogUiScale, 10.f * DialogUiScale));
		BodyPad->SetContent(Body);
		Column->AddChildToVerticalBox(BodyPad);

		// Footer: Cancel and Create, right-aligned.
		UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Footer"));
		USpacer* FooterSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("FooterSpacer"));
		Footer->AddChildToHorizontalBox(FooterSpacer)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		auto AddFooterButton = [this, Footer, &MakeText](const TCHAR* Name, const TCHAR* Label, const FButtonStyle& Style, float Width, bool bFirst) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			Button->SetStyle(Style);
			Button->SetBackgroundColor(FLinearColor::White);
			UTextBlock* Text = MakeText(*(FString(Name) + TEXT("Label")), Label, 13.f, DialogRgb(255, 255, 255), true);
			Text->SetJustification(ETextJustify::Center);
			Button->SetContent(Text);
			USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Size")));
			Size->SetMinDesiredWidth(Width * DialogUiScale);
			Size->SetHeightOverride(34.f * DialogUiScale);
			Size->SetContent(Button);
			Footer->AddChildToHorizontalBox(Size)->SetPadding(FMargin(bFirst ? 0.f : 8.f * DialogUiScale, 0.f, 0.f, 0.f));
			return Button;
		};
		CancelButton = AddFooterButton(TEXT("Btn_CancelProject"), TEXT("Cancel"),
			DialogButtonStyle(FLinearColor::Transparent, DialogRgb(36, 50, 74), DialogRgb(30, 43, 66), DialogRgb(74, 90, 113)), 74.f, true);
		CreateButton = AddFooterButton(TEXT("Btn_CreateProject"), TEXT("Create project"),
			DialogButtonStyle(DialogAmber, DialogRgb(238, 138, 24), DialogRgb(190, 102, 4), DialogAmber), 110.f, false);

		UBorder* FooterPad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FooterPad"));
		FooterPad->SetBrushColor(FLinearColor::Transparent);
		FooterPad->SetPadding(FMargin(24.f * DialogUiScale, 14.f * DialogUiScale, 24.f * DialogUiScale, 20.f * DialogUiScale));
		FooterPad->SetContent(Footer);
		Column->AddChildToVerticalBox(FooterPad);
	}
	return Super::RebuildWidget();
}

void UNewProjectDialogWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CancelButton)
	{
		CancelButton->OnClicked.AddUniqueDynamic(this, &UNewProjectDialogWidget::PlayButtonClickSound);
		CancelButton->OnClicked.AddUniqueDynamic(this, &UNewProjectDialogWidget::HandleCancelClicked);
	}
	if (CreateButton)
	{
		CreateButton->OnClicked.AddUniqueDynamic(this, &UNewProjectDialogWidget::PlayButtonClickSound);
		CreateButton->OnClicked.AddUniqueDynamic(this, &UNewProjectDialogWidget::HandleCreateClicked);
	}
}

void UNewProjectDialogWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bFocusedName && NameBox)
	{
		// Typing can start straight away.
		NameBox->SetKeyboardFocus();
		bFocusedName = true;
	}
}

FReply UNewProjectDialogWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Cancel();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void UNewProjectDialogWidget::PlayButtonClickSound()
{
	if (ANavPlayerController* PC = GetOwningPlayer<ANavPlayerController>())
	{
		PC->PlayButtonClickSound();
	}
}

void UNewProjectDialogWidget::HandleCreateClicked()
{
	Submit();
}

void UNewProjectDialogWidget::HandleCancelClicked()
{
	Cancel();
}

void UNewProjectDialogWidget::HandleNameChanged(const FText& Text)
{
	// The message was about the name that was there; it no longer applies once it is edited.
	if (ErrorText)
	{
		ErrorText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UNewProjectDialogWidget::HandleNameCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		Submit();
	}
}

void UNewProjectDialogWidget::ShowError(const FString& Message)
{
	if (ErrorText)
	{
		ErrorText->SetText(FText::FromString(Message));
		ErrorText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void UNewProjectDialogWidget::Submit()
{
	if (!OnCreate || !NameBox)
	{
		return;
	}
	const FString Description = DescriptionBox ? DescriptionBox->GetText().ToString() : FString();
	const FString Error = OnCreate(NameBox->GetText().ToString(), Description);
	if (Error.IsEmpty())
	{
		RemoveFromParent();
	}
	else
	{
		ShowError(Error);
		NameBox->SetKeyboardFocus();
	}
}

void UNewProjectDialogWidget::Cancel()
{
	if (OnCancelled)
	{
		OnCancelled();
	}
	RemoveFromParent();
}

FText UNewProjectDialogWidget::GetErrorTextForTests() const
{
	return ErrorText ? ErrorText->GetText() : FText::GetEmpty();
}

bool UNewProjectDialogWidget::IsErrorShownForTests() const
{
	return ErrorText && ErrorText->GetVisibility() != ESlateVisibility::Collapsed;
}
