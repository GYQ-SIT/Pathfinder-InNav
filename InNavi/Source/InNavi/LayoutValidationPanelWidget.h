#pragma once

#include "CoreMinimal.h"
#include "DockPanelContent.h"
#include "Components/Button.h"
#include "LayoutToolPanel.h"
#include "LayoutValidationPanelWidget.generated.h"

class UComboBoxString;
class UHorizontalBox;
class UProgressBar;
class UScrollBox;
class USizeBox;
class UTextBlock;
class ULayoutValidationSession;
class ULayoutValidationPanelWidget;

UCLASS()
class INNAVI_API ULayoutValidationIssueButton : public UButton
{
    GENERATED_BODY()
public:
    void SetIssue(ULayoutValidationPanelWidget* Panel, int32 Index);
private:
    UFUNCTION() void HandleClicked();
    TWeakObjectPtr<ULayoutValidationPanelWidget> OwnerPanel;
    int32 IssueIndex = INDEX_NONE;
};

UCLASS()
class INNAVI_API ULayoutValidationPanelWidget : public UDockPanelContentWidget, public ILayoutToolPanel
{
    GENERATED_BODY()
public:
    virtual void InitializeToolPanel(UUserWidget* Screen, bool bExteriorMode, TFunction<void()> FocusCompleted) override
    {
        SetOwnerScreen(Screen); SetExteriorMode(bExteriorMode); OnIssueShown = MoveTemp(FocusCompleted);
    }
    virtual void SetToolPanelWidth(float Width) override { SetContentWidth(Width); }
    void SetContentWidth(float Width);
    void SetExteriorMode(bool bValue) { bExterior = bValue; }
    void RefreshDisplay();
    void ShowIssue(int32 Index);
    // Called after the controller has focused the object. The screen handles its own chrome/view transition.
    TFunction<void()> OnIssueShown;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
private:
    UFUNCTION() void OnValidateClicked();
    UFUNCTION() void OnCancelClicked();
    UFUNCTION() void OnExportClicked();
    UFUNCTION() void OnFilterChanged(FString Value, ESelectInfo::Type Type);
    UFUNCTION() void OnScopeChanged(FString Value, ESelectInfo::Type Type);
    UFUNCTION() UWidget* GenerateComboItem(FString Value);
    ULayoutValidationSession* Session() const;
    void RebuildIssues();
    FString ScopeBuildingId() const;
    UButton* AddButton(class UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label, const TCHAR* IconAsset = nullptr);
    UTextBlock* AddText(class UPanelWidget* Parent, const FString& Text, FName Name);
    UPROPERTY() UComboBoxString* Scope = nullptr;
    UPROPERTY() UComboBoxString* Filter = nullptr;
    UPROPERTY() UTextBlock* Status = nullptr;
    // The counts of the report as icons with numbers: errors, warnings, and the rooms checked (the design team's Errors / Warnings / Passed counts).
    UPROPERTY() UHorizontalBox* CountsRow = nullptr;
    UPROPERTY() UTextBlock* ErrorCountText = nullptr;
    UPROPERTY() UTextBlock* WarningCountText = nullptr;
    UPROPERTY() UTextBlock* CheckedCountText = nullptr;
    UPROPERTY() UTextBlock* Feedback = nullptr;
    UPROPERTY() UScrollBox* Issues = nullptr;
    UPROPERTY() UButton* ValidateButton = nullptr;
    UPROPERTY() UButton* CancelButton = nullptr;
    UPROPERTY() UProgressBar* Progress = nullptr;
    UPROPERTY() UButton* ExportButton = nullptr;
    UPROPERTY() USizeBox* BodySize = nullptr;
    // The items the two combo boxes generate are held only by Slate, which does not keep a UObject alive: without this the garbage collector frees them while they are still on screen.
    UPROPERTY() TArray<TObjectPtr<UWidget>> RetainedComboItems;
    float ContentWidth = 260.f;
    float PollElapsed = 0.f;
    bool bExterior = false;
    bool bDisplayedStale = false;
    bool bWasBusy = false;
    uint64 DisplayedGeneration = MAX_uint64;
};
