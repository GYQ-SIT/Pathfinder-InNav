#include "PluginToolHost.h"
#include "LayoutToolPanel.h"
#include "DockHostUMG.h"
#include "Blueprint/UserWidget.h"
#include "Components/CheckBox.h"
#include "Misc/ConfigCacheIni.h"

void UPluginToolMenuBinding::Toggle(bool bChecked)
{
    if (!Dock.IsValid() || !Dock->IsPanelRegistered(PanelId) || Dock->IsPanelShown(PanelId) == bChecked) return;
    if (bChecked) Dock->GetLayout()->ShowPanel(PanelId, Side);
    else Dock->GetLayout()->HidePanel(PanelId);
    Dock->NotifyLayoutChanged();
}
void UPluginToolMenuBinding::Sync()
{
    if (!Checkbox || !Dock.IsValid()) return;
    Checkbox->SetIsEnabled(Dock->IsPanelRegistered(PanelId));
    Checkbox->SetIsChecked(Dock->IsPanelRegistered(PanelId) && Dock->IsPanelShown(PanelId));
}
void UPluginToolHost::AddMenu(LayoutTools::EScreen Screen, TFunction<UCheckBox*(const FString&, const FString&)> AddRow)
{
    Bindings.Empty();
    for (const auto& Tool : LayoutTools::FRegistry::Get().Tools(Screen))
    {
        auto* Binding = NewObject<UPluginToolMenuBinding>(this);
        Binding->PanelId = Tool.Id;
        Binding->Side = PluginTools::DockSide(Tool.DefaultSide);
        Binding->Checkbox = AddRow(TEXT("Chk_Window") + Tool.Id.ToString(), Tool.Title.ToString());
        Binding->Checkbox->SetIsChecked(!Tool.bHiddenByDefault);
        Binding->Checkbox->OnCheckStateChanged.AddDynamic(Binding, &UPluginToolMenuBinding::Toggle);
        Bindings.Add(Binding);
    }
}
void UPluginToolHost::CreatePanels(UUserWidget* Screen, UDockHostWidget* Dock, LayoutTools::EScreen Kind, TFunction<void()> FocusCompleted)
{
    DockHost = Dock;
    Panels.Empty();
    for (auto* Binding : Bindings) Binding->Dock = Dock;
    for (const auto& Tool : LayoutTools::FRegistry::Get().Tools(Kind))
    {
        if (Dock->IsPanelRegistered(Tool.Id))
        {
            UE_LOG(LogTemp, Error, TEXT("Plugin tool ID %s collides with a built-in panel"), *Tool.Id.ToString());
            continue;
        }
        FString ClassPath = Tool.WidgetClassPath;
        GConfig->GetString(TEXT("InNavi.ToolPanels"), *Tool.Id.ToString(), ClassPath, GGameIni);
        UClass* Class = ClassPath.IsEmpty() ? nullptr : LoadClass<UUserWidget>(nullptr, *ClassPath);
        if (!Class || Class->HasAnyClassFlags(CLASS_Abstract) || !Class->ImplementsInterface(ULayoutToolPanel::StaticClass()))
        {
            UE_LOG(LogTemp, Warning, TEXT("Plugin tool %s needs a concrete UUserWidget implementing ILayoutToolPanel (%s)"), *Tool.Id.ToString(), *ClassPath);
            continue;
        }
        auto* Panel = CreateWidget<UUserWidget>(Screen, Class);
        if (!Panel) continue;
        Cast<ILayoutToolPanel>(Panel)->InitializeToolPanel(Screen, Kind == LayoutTools::EScreen::Exterior, FocusCompleted);
        Panels.Add(Tool.Id, Panel);
        Dock->RegisterPanel(Tool.Id, Tool.Title, Panel);
    }
    Sync();
}
void UPluginToolHost::Sync()
{
    for (auto* Binding : Bindings) Binding->Sync();
    if (!DockHost.IsValid()) return;
    for (const auto& Entry : Panels)
    {
        const float Width = DockHost->GetPanelWidth(Entry.Key);
        if (Width > 0.f) Cast<ILayoutToolPanel>(Entry.Value)->SetToolPanelWidth(FMath::Max(100.f, Width - 48.f));
    }
}
UWidget* UPluginToolHost::FindControl(FName Name) const
{
    for (const auto& Entry : Panels) if (UWidget* Widget = Entry.Value->GetWidgetFromName(Name)) return Widget;
    return nullptr;
}
UUserWidget* UPluginToolHost::FindPanel(FName Id) const
{
    auto* const* Panel = Panels.Find(Id);
    return Panel ? *Panel : nullptr;
}
namespace PluginTools
{
    EDockSide DockSide(LayoutTools::ESide Side)
    {
        switch (Side) { case LayoutTools::ESide::Left: return EDockSide::Left; case LayoutTools::ESide::Right: return EDockSide::Right; default: return EDockSide::Bottom; }
    }
    void AppendPanels(TArray<FName>& Panels, LayoutTools::EScreen Screen)
    {
        for (const auto& Tool : LayoutTools::FRegistry::Get().Tools(Screen)) Panels.AddUnique(Tool.Id);
    }
    bool HiddenByDefault(FName Id, LayoutTools::EScreen Screen)
    {
        for (const auto& Tool : LayoutTools::FRegistry::Get().Tools(Screen)) if (Tool.Id == Id) return Tool.bHiddenByDefault;
        return false;
    }
    EDockSide DefaultSide(FName Id, LayoutTools::EScreen Screen, EDockSide Fallback)
    {
        for (const auto& Tool : LayoutTools::FRegistry::Get().Tools(Screen)) if (Tool.Id == Id) return DockSide(Tool.DefaultSide);
        return Fallback;
    }
    void PlaceDefaults(FDockLayout& Layout, LayoutTools::EScreen Screen)
    {
        for (const auto& Tool : LayoutTools::FRegistry::Get().Tools(Screen))
        {
            Layout.DockPanelToEdge(Tool.Id, DockSide(Tool.DefaultSide));
            if (Tool.bHiddenByDefault) Layout.HidePanel(Tool.Id);
        }
    }
}
