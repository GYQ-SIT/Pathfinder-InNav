#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "InNaviValidateLayoutCommandlet.generated.h"

// Headless entry point for the same rules used by the runtime panel. Input files are never saved.
UCLASS()
class UInNaviValidateLayoutCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UInNaviValidateLayoutCommandlet();
    virtual int32 Main(const FString& Params) override;
};
