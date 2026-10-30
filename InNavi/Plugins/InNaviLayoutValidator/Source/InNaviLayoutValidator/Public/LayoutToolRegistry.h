#pragma once

#include "CoreMinimal.h"

namespace LayoutTools
{
    enum class EScreen : uint8 { Interior, Exterior };
    enum class ESide : uint8 { Left, Right, Bottom };

    // Native, versioned tool contract. Register during module startup, before screens are built.
    struct INNAVILAYOUTVALIDATOR_API FDescriptor
    {
        FName Id;
        FText Title;
        FString WidgetClassPath;
        bool bInterior = true;
        bool bExterior = true;
        bool bHiddenByDefault = true;
        ESide DefaultSide = ESide::Bottom;
        bool Supports(EScreen Screen) const { return Screen == EScreen::Interior ? bInterior : bExterior; }
    };

    class INNAVILAYOUTVALIDATOR_API IProvider
    {
    public:
        virtual ~IProvider() = default;
        virtual FDescriptor DescribeTool() const = 0;
    };

    // Game-thread only. A duplicate ID is rejected; only its original provider can unregister it.
    class INNAVILAYOUTVALIDATOR_API FRegistry
    {
    public:
        static FRegistry& Get();
        bool Register(IProvider& Provider);
        void Unregister(IProvider& Provider);
        TArray<FDescriptor> Tools(EScreen Screen) const;
    private:
        TMap<FName, IProvider*> Providers;
    };
}
