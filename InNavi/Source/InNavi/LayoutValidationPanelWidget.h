#pragma once

#include "CoreMinimal.h"
#include "DockPanelContent.h"
#include "Components/Button.h"
#include "LayoutValidationPanelWidget.generated.h"

class UComboBoxString;
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
class INNAVI_API ULayoutValidationPanelWidget : public UDockPanelContentWidget
{
    GENERATED_BODY()
public:
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
    UFUNCTION() void OnExportClicked();
    UFUNCTION() void OnFilterChanged(FString Value, ESelectInfo::Type Type);
    UFUNCTION() void OnScopeChanged(FString Value, ESelectInfo::Type Type);
    UFUNCTION() UWidget* GenerateComboItem(FString Value);
    ULayoutValidationSession* Session() const;
    void RebuildIssues();
    FString ScopeBuildingId() const;
    UButton* AddButton(class UVerticalBox* Stack, const TCHAR* Name, const TCHAR* Label);
    UTextBlock* AddText(class UPanelWidget* Parent, const FString& Text, FName Name);
    UPROPERTY() UComboBoxString* Scope = nullptr;
    UPROPERTY() UComboBoxString* Filter = nullptr;
    UPROPERTY() UTextBlock* Status = nullptr;
    UPROPERTY() UTextBlock* Feedback = nullptr;
    UPROPERTY() UScrollBox* Issues = nullptr;
    UPROPERTY() UButton* ValidateButton = nullptr;
    UPROPERTY() UButton* ExportButton = nullptr;
    UPROPERTY() USizeBox* BodySize = nullptr;
    float ContentWidth = 260.f;
    float PollElapsed = 0.f;
    bool bExterior = false;
    bool bDisplayedStale = false;
    uint64 DisplayedGeneration = MAX_uint64;
};
