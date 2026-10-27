#pragma once

#include "CoreMinimal.h"
#include "LayoutToolRegistry.h"
#include "DockLayoutModel.h"
#include "UObject/Object.h"
#include "PluginToolHost.generated.h"

class UCheckBox;
class UDockHostWidget;
class UUserWidget;
class UWidget;

UCLASS()
class INNAVI_API UPluginToolMenuBinding : public UObject
{
    GENERATED_BODY()
public:
    FName PanelId;
    EDockSide Side = EDockSide::Bottom;
    UPROPERTY() UCheckBox* Checkbox = nullptr;
    TWeakObjectPtr<UDockHostWidget> Dock;
    UFUNCTION() void Toggle(bool bChecked);
    void Sync();
};

// Both product screens use the same discovery, menu, creation, lookup and sizing path.
UCLASS()
class INNAVI_API UPluginToolHost : public UObject
{
    GENERATED_BODY()
public:
    void AddMenu(LayoutTools::EScreen Screen, TFunction<UCheckBox*(const FString&, const FString&)> AddRow);
    void CreatePanels(UUserWidget* Screen, UDockHostWidget* Dock, LayoutTools::EScreen Kind, TFunction<void()> FocusCompleted);
    void Sync();
    UWidget* FindControl(FName Name) const;
    UUserWidget* FindPanel(FName Id) const;
private:
    UPROPERTY() TArray<UPluginToolMenuBinding*> Bindings;
    UPROPERTY() TMap<FName, UUserWidget*> Panels;
    TWeakObjectPtr<UDockHostWidget> DockHost;
};

namespace PluginTools
{
    EDockSide DockSide(LayoutTools::ESide Side);
    void AppendPanels(TArray<FName>& Panels, LayoutTools::EScreen Screen);
    void PlaceDefaults(FDockLayout& Layout, LayoutTools::EScreen Screen);
    bool HiddenByDefault(FName Id, LayoutTools::EScreen Screen);
    EDockSide DefaultSide(FName Id, LayoutTools::EScreen Screen, EDockSide Fallback);
}
