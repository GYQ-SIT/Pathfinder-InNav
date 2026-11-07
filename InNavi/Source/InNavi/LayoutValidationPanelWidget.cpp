#include "LayoutValidationPanelWidget.h"

#include "LayoutValidationSession.h"
#include "NavPlayerController.h"
#include "StartScreenStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Misc/Paths.h"

void ULayoutValidationIssueButton::SetIssue(ULayoutValidationPanelWidget* Panel, int32 Index)
{
    OwnerPanel = Panel; IssueIndex = Index;
    OnClicked.AddUniqueDynamic(this, &ULayoutValidationIssueButton::HandleClicked);
}
void ULayoutValidationIssueButton::HandleClicked() { if (OwnerPanel.IsValid()) OwnerPanel->ShowIssue(IssueIndex); }

UTextBlock* ULayoutValidationPanelWidget::AddText(UPanelWidget* Parent, const FString& Text, FName Name)
{
    auto* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
    Label->SetText(FText::FromString(Text)); Label->SetAutoWrapText(true); Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    ApplyStandardFont(Label); Parent->AddChild(Label); return Label;
}
UButton* ULayoutValidationPanelWidget::AddButton(UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Text)
{
    auto* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
    Button->SetStyle(StartStyle::SecondaryButton()); Button->SetBackgroundColor(FLinearColor::White);
    Button->OnClicked.AddUniqueDynamic(this, &ULayoutValidationPanelWidget::PlayButtonClickSound);
    auto* Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetMinDesiredHeight(34.f); Button->SetContent(Size);
    auto* Label = AddText(Size, Text, FName(FString(Name) + TEXT("Label"))); Label->SetJustification(ETextJustify::Center);
    Stack->AddChildToVerticalBox(Button)->SetPadding(FMargin(0, 0, 0, 6)); return Button;
}
TSharedRef<SWidget> ULayoutValidationPanelWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        auto* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Pnl_Validation"));
        Root->SetBrushColor(FLinearColor::Transparent); Root->SetPadding(FMargin(14.f)); WidgetTree->RootWidget = Root;
        BodySize = WidgetTree->ConstructWidget<USizeBox>(); BodySize->SetWidthOverride(ContentWidth); Root->SetContent(BodySize);
        auto* Stack = WidgetTree->ConstructWidget<UVerticalBox>(); BodySize->SetContent(Stack);
        AddText(Stack, TEXT("Checks applied layout edits. Issues are advisory; editing and saving remain available."), TEXT("Txt_ValidationHelp"));
        Scope = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Cbx_ValidationScope"));
        StartStyle::DarkCombo(Scope); Scope->OnGenerateWidgetEvent.BindDynamic(this, &ULayoutValidationPanelWidget::GenerateComboItem);
        Scope->AddOption(TEXT("All buildings")); Scope->AddOption(TEXT("Current building")); Scope->SetSelectedIndex(0);
        Stack->AddChildToVerticalBox(Scope)->SetPadding(FMargin(0, 6, 0, 6));
        Scope->OnSelectionChanged.AddUniqueDynamic(this, &ULayoutValidationPanelWidget::OnScopeChanged);
        ValidateButton = AddButton(Stack, TEXT("Btn_ValidateLayout"), TEXT("Validate layout"));
        ValidateButton->SetStyle(StartStyle::PrimaryButton()); ValidateButton->OnClicked.AddUniqueDynamic(this, &ULayoutValidationPanelWidget::OnValidateClicked);
        Progress = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("Bar_ValidationProgress"));
        Progress->SetFillColorAndOpacity(FLinearColor(.3f, .85f, .4f)); Progress->SetPercent(0.f); Progress->SetVisibility(ESlateVisibility::Collapsed);
        Stack->AddChildToVerticalBox(Progress)->SetPadding(FMargin(0, 0, 0, 6));
        CancelButton = AddButton(Stack, TEXT("Btn_CancelValidation"), TEXT("Cancel"));
        CancelButton->SetVisibility(ESlateVisibility::Collapsed); CancelButton->OnClicked.AddUniqueDynamic(this, &ULayoutValidationPanelWidget::OnCancelClicked);
        Status = AddText(Stack, TEXT("Choose a scope, then validate."), TEXT("Txt_ValidationStatus"));
        Filter = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Cbx_ValidationSeverity"));
        StartStyle::DarkCombo(Filter); Filter->OnGenerateWidgetEvent.BindDynamic(this, &ULayoutValidationPanelWidget::GenerateComboItem);
        for (const TCHAR* Value : { TEXT("All issues"), TEXT("Errors"), TEXT("Warnings"), TEXT("Information") }) Filter->AddOption(Value);
        Filter->SetSelectedIndex(0); Filter->OnSelectionChanged.AddUniqueDynamic(this, &ULayoutValidationPanelWidget::OnFilterChanged);
        Stack->AddChildToVerticalBox(Filter)->SetPadding(FMargin(0, 6, 0, 6));
        Issues = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("List_ValidationIssues"));
        auto* ListSlot = Stack->AddChildToVerticalBox(Issues); ListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ExportButton = AddButton(Stack, TEXT("Btn_ExportValidation"), TEXT("Export report"));
        ExportButton->OnClicked.AddUniqueDynamic(this, &ULayoutValidationPanelWidget::OnExportClicked);
        Feedback = AddText(Stack, FString(), TEXT("Txt_ValidationFeedback"));
    }
    auto Result = Super::RebuildWidget(); RefreshDisplay(); return Result;
}
UWidget* ULayoutValidationPanelWidget::GenerateComboItem(FString Value)
{
    auto* Text = WidgetTree->ConstructWidget<UTextBlock>(); Text->SetText(FText::FromString(Value)); ApplyStandardFont(Text); Text->SetAutoWrapText(true);
    // Slate keeps only its own widget, not this one: keep it alive (the newest 400 are far more than are ever on screen).
    RetainedComboItems.Add(Text); if (RetainedComboItems.Num() > 400) RetainedComboItems.RemoveAt(0, RetainedComboItems.Num() - 400);
    return Text;
}
ULayoutValidationSession* ULayoutValidationPanelWidget::Session() const
{
    auto* PC = GetController(); return PC ? PC->GetLayoutValidationSession() : nullptr;
}
FString ULayoutValidationPanelWidget::ScopeBuildingId() const
{
    auto* PC = GetController();
    if (!PC || !Scope || Scope->GetSelectedIndex() != 1) return FString();
    return bExterior ? PC->GetSelectedBuildingId() : PC->GetLoadedBuildingId();
}
void ULayoutValidationPanelWidget::SetContentWidth(float Width)
{
    if (FMath::IsNearlyEqual(Width, ContentWidth, 0.5f)) return;
    ContentWidth = Width; if (BodySize) BodySize->SetWidthOverride(Width);
}
void ULayoutValidationPanelWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    PollElapsed += DeltaTime;
    // Validation is stepped by the session itself; this only shows how far it is. Fingerprinting is throttled and stops as soon as the report becomes stale.
    const auto* State = Session();
    const bool bBusy = State && State->IsRunning();
    if (bBusy || bWasBusy || PollElapsed >= 1.f) { PollElapsed = 0.f; bWasBusy = bBusy; RefreshDisplay(); }
}
void ULayoutValidationPanelWidget::RefreshDisplay()
{
    auto* State = Session();
    const bool bBusy = State && State->IsRunning();
    if (ValidateButton) ValidateButton->SetIsEnabled(State && !bBusy && (!Scope || Scope->GetSelectedIndex() == 0 || !ScopeBuildingId().IsEmpty()));
    if (CancelButton) CancelButton->SetVisibility(bBusy ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (Progress)
    {
        Progress->SetVisibility(bBusy ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        if (bBusy) Progress->SetPercent(State->GetProgress());
    }
    if (!State || !Status || !Issues) return;
    if (bBusy)
    {
        // The old issues belong to the old report; clear them while the new one is made.
        if (DisplayedGeneration != State->GetGeneration()) { DisplayedGeneration = State->GetGeneration(); RebuildIssues(); }
        if (ExportButton) ExportButton->SetIsEnabled(false);
        Status->SetText(FText::FromString(FString::Printf(TEXT("Validating... %d%%\n%s"), FMath::RoundToInt(State->GetProgress() * 100.f), *State->GetProgressText())));
        Status->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        return;
    }
    State->RefreshStaleness();
    const bool bStale = State->IsStale();
    if (ExportButton) ExportButton->SetIsEnabled(State->HasReport() && !bStale);
    if (State->HasReport())
    {
        const auto& Report = State->GetReport();
        const FString ScopeName = Report.ScopeBuildingId.IsEmpty() ? TEXT("All buildings") : Report.ScopeBuildingId;
        Status->SetText(FText::FromString(FString::Printf(TEXT("%s\n%d errors / %d warnings / %d information\n%d rooms checked, %d skipped\n%s"),
            *ScopeName, Report.Count(LayoutValidation::ESeverity::Error), Report.Count(LayoutValidation::ESeverity::Warning), Report.Count(LayoutValidation::ESeverity::Info),
            Report.RoomsChecked, Report.RoomsSkipped, bStale ? TEXT("Layout changed - validate again.") : Report.bNavigationChecked ? TEXT("Data and entrance reachability checked.") : TEXT("Data checked; navigation not checked."))));
        Status->SetColorAndOpacity(FSlateColor(bStale ? FLinearColor::Yellow : FLinearColor::White));
    }
    if (DisplayedGeneration != State->GetGeneration() || bDisplayedStale != bStale)
    {
        DisplayedGeneration = State->GetGeneration(); bDisplayedStale = bStale; RebuildIssues();
    }
}
void ULayoutValidationPanelWidget::RebuildIssues()
{
    if (!Issues) return;
    Issues->ClearChildren();
    auto* State = Session(); if (!State || !State->HasReport()) return;
    const auto& Report = State->GetReport();
    const int32 Selected = Filter ? Filter->GetSelectedIndex() : 0;
    for (int32 I = 0; I < Report.Issues.Num(); ++I)
    {
        const auto& Issue = Report.Issues[I];
        if (Selected > 0 && static_cast<int32>(Issue.Severity) != Selected - 1) continue;
        auto* Button = WidgetTree->ConstructWidget<ULayoutValidationIssueButton>(ULayoutValidationIssueButton::StaticClass());
        Button->SetStyle(StartStyle::SecondaryButton()); Button->SetBackgroundColor(FLinearColor::White); Button->SetIssue(this, I);
        auto* Box = WidgetTree->ConstructWidget<UVerticalBox>(); Button->SetContent(Box);
        const FString Location = Issue.BuildingId.IsEmpty() ? TEXT("Project") : Issue.BuildingId + (Issue.Floor > 0 ? FString::Printf(TEXT(" / L%d"), Issue.Floor) : FString());
        auto* Title = AddText(Box, FString(LayoutValidation::SeverityName(Issue.Severity)) + TEXT(" - ") + Location + TEXT(" ") + Issue.ObjectId, NAME_None);
        Title->SetColorAndOpacity(FSlateColor(Issue.Severity == LayoutValidation::ESeverity::Error ? FLinearColor(1.f, .4f, .35f) : Issue.Severity == LayoutValidation::ESeverity::Warning ? FLinearColor::Yellow : FLinearColor(.6f, .8f, 1.f)));
        AddText(Box, Issue.Message, NAME_None); AddText(Box, Issue.Suggestion, NAME_None);
        if (!Issue.BuildingId.IsEmpty()) AddText(Box, TEXT("Show on plan"), NAME_None);
        Button->SetIsEnabled(!State->IsStale() && !Issue.BuildingId.IsEmpty());
        Issues->AddChild(Button);
    }
    if (Issues->GetChildrenCount() == 0) AddText(Issues, TEXT("No issues match this filter."), NAME_None);
}
void ULayoutValidationPanelWidget::OnValidateClicked()
{
    auto* State = Session(); if (!State) return;
    if (Scope && Scope->GetSelectedIndex() == 1 && ScopeBuildingId().IsEmpty()) return;
    State->StartValidate(ScopeBuildingId()); if (Feedback) Feedback->SetText(FText::GetEmpty()); RefreshDisplay();
}
void ULayoutValidationPanelWidget::OnCancelClicked()
{
    auto* State = Session(); if (!State) return;
    State->CancelValidate();
    if (Status) Status->SetText(FText::FromString(TEXT("Validation cancelled.")));
    if (Feedback) Feedback->SetText(FText::GetEmpty());
    RefreshDisplay();
}
void ULayoutValidationPanelWidget::OnFilterChanged(FString Value, ESelectInfo::Type Type) { RebuildIssues(); }
void ULayoutValidationPanelWidget::OnScopeChanged(FString Value, ESelectInfo::Type Type) { RefreshDisplay(); }
void ULayoutValidationPanelWidget::ShowIssue(int32 Index)
{
    PlayButtonClickSound(); auto* State = Session(); auto* PC = GetController();
    if (State && PC && State->ShowIssue(*PC, Index)) { if (OnIssueShown) OnIssueShown(); }
    else if (Feedback) Feedback->SetText(FText::FromString(TEXT("The issue could not be shown. Validate again if the layout changed.")));
    RefreshDisplay();
}
void ULayoutValidationPanelWidget::OnExportClicked()
{
    auto* State = Session(); if (!State) return;
    const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Validation") / (TEXT("Layout-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json")));
    const bool bSaved = State->Export(Path);
    if (Feedback) Feedback->SetText(FText::FromString(bSaved ? TEXT("Report saved: ") + Path : TEXT("Report export failed. Validate again if the layout changed.")));
    RefreshDisplay();
}
