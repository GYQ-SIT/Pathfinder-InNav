#include "Modules/ModuleManager.h"
#include "LayoutToolRegistry.h"

class FInNaviLayoutValidatorModule : public IModuleInterface, public LayoutTools::IProvider
{
public:
    virtual LayoutTools::FDescriptor DescribeTool() const override
    {
        LayoutTools::FDescriptor Tool;
        Tool.Id = TEXT("Validation");
        Tool.Title = NSLOCTEXT("InNaviLayoutValidator", "Title", "Validation");
        // The consuming product supplies its actor/UI adapter through configuration.
        return Tool;
    }
    virtual void StartupModule() override { const bool bRegistered = LayoutTools::FRegistry::Get().Register(*this); check(bRegistered); }
    virtual void ShutdownModule() override { LayoutTools::FRegistry::Get().Unregister(*this); }
};
IMPLEMENT_MODULE(FInNaviLayoutValidatorModule, InNaviLayoutValidator)
