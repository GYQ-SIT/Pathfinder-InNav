#include "LayoutToolRegistry.h"

namespace LayoutTools
{
    FRegistry& FRegistry::Get() { static FRegistry Registry; return Registry; }
    bool FRegistry::Register(IProvider& Provider)
    {
        check(IsInGameThread());
        const FDescriptor Tool = Provider.DescribeTool();
        if (Tool.Id.IsNone() || Tool.Title.IsEmpty() || (!Tool.bInterior && !Tool.bExterior) || Providers.Contains(Tool.Id)) return false;
        Providers.Add(Tool.Id, &Provider);
        return true;
    }
    void FRegistry::Unregister(IProvider& Provider)
    {
        check(IsInGameThread());
        for (auto It = Providers.CreateIterator(); It; ++It)
            if (It.Value() == &Provider) It.RemoveCurrent();
    }
    TArray<FDescriptor> FRegistry::Tools(EScreen Screen) const
    {
        check(IsInGameThread());
        TArray<FDescriptor> Result;
        for (const auto& Entry : Providers)
        {
            FDescriptor Tool = Entry.Value->DescribeTool();
            if (Tool.Supports(Screen)) Result.Add(MoveTemp(Tool));
        }
        Result.Sort([](const FDescriptor& A, const FDescriptor& B) { return A.Id.LexicalLess(B.Id); });
        return Result;
    }
}
