#pragma once
#include "Commandlets/Commandlet.h"
#include "LayoutValidationExampleCommandlet.generated.h"

UCLASS()
class ULayoutValidationExampleCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    ULayoutValidationExampleCommandlet();
    virtual int32 Main(const FString& Params) override;
};
