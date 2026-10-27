#if WITH_DEV_AUTOMATION_TESTS
#include "LayoutToolRegistry.h"
#include "LayoutValidationPanelWidget.h"
#include "InteriorScreenWidget.h"
#include "EditScreenWidget.h"
#include "InteriorDockLayout.h"
#include "ExteriorDockLayout.h"
#include "DockHostUMG.h"
#include "Components/CheckBox.h"
#include "Misc/AutomationTest.h"

namespace InNaviPluginToolTests
{
    struct FExampleProvider : LayoutTools::IProvider
    {
        virtual LayoutTools::FDescriptor DescribeTool() const override
        {
            LayoutTools::FDescriptor Tool;
            Tool.Id = TEXT("ComplianceExample");
            Tool.Title = FText::FromString(TEXT("Example tool"));
            Tool.WidgetClassPath = TEXT("/Script/InNavi.LayoutValidationPanelWidget");
            return Tool;
        }
    };
    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegistryTest, "InNavi.LayoutValidation.PluginDiscovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FRegistryTest::RunTest(const FString& Parameters)
    {
        auto& Registry = LayoutTools::FRegistry::Get();
        FExampleProvider Provider, Duplicate;
        if (!TestTrue(TEXT("extension registered"), Registry.Register(Provider))) return false;
        TestFalse(TEXT("duplicate ID rejected"), Registry.Register(Duplicate));
        Registry.Unregister(Duplicate);
        TestTrue(TEXT("wrong owner cannot remove registration"), InteriorDock::KnownPanels().Contains(TEXT("ComplianceExample")));
        auto* Interior = NewObject<UInteriorScreenWidget>(); Interior->Initialize(); Interior->TakeWidget();
        auto* Exterior = NewObject<UEditScreenWidget>(); Exterior->Initialize(); Exterior->TakeWidget();
        TestTrue(TEXT("interior discovers an additional tool without screen edits"), Interior->GetDockHost()->IsPanelRegistered(TEXT("ComplianceExample")));
        TestTrue(TEXT("exterior discovers an additional tool without screen edits"), Exterior->GetDockHost()->IsPanelRegistered(TEXT("ComplianceExample")));
        auto* InteriorCheck = Cast<UCheckBox>(Interior->FindScreenWidget(TEXT("Chk_WindowComplianceExample")));
        auto* ExteriorCheck = Cast<UCheckBox>(Exterior->FindScreenWidget(TEXT("Chk_WindowComplianceExample")));
        TestNotNull(TEXT("interior extension menu"), InteriorCheck);
        TestNotNull(TEXT("exterior extension menu"), ExteriorCheck);
        FDockLayout Layout; InteriorDock::BuildDefaultLayout(Layout);
        TestFalse(TEXT("extension initially hidden"), Layout.ContainsPanel(TEXT("ComplianceExample")));
        TestTrue(TEXT("extension can be shown"), Layout.ShowPanel(TEXT("ComplianceExample"), InteriorDock::DefaultSideFor(TEXT("ComplianceExample"))));
        FDockLayout Restored; TArray<FName> Missing;
        TestTrue(TEXT("extension layout round trips"), Restored.FromJson(Layout.ToJson({1920,1080}), {1920,1080}, InteriorDock::KnownPanels(), Missing));
        TestTrue(TEXT("restored extension shown"), Restored.ContainsPanel(TEXT("ComplianceExample")));
        if (InteriorCheck)
        {
            InteriorCheck->OnCheckStateChanged.Broadcast(true);
            TestTrue(TEXT("generic menu opens panel"), Interior->GetDockHost()->IsPanelShown(TEXT("ComplianceExample")));
            InteriorCheck->OnCheckStateChanged.Broadcast(false);
            TestFalse(TEXT("generic menu closes panel"), Interior->GetDockHost()->IsPanelShown(TEXT("ComplianceExample")));
        }
        Registry.Unregister(Provider);
        TestFalse(TEXT("unregistration removes known panel"), ExteriorDock::KnownPanels().Contains(TEXT("ComplianceExample")));
        return true;
    }
}
#endif
