#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavigationTestActor.generated.h"

UCLASS()
class INNAVI_API ANavigationTestActor : public AActor
{
    GENERATED_BODY()

public:
    ANavigationTestActor();

protected:
    virtual void BeginPlay() override;

private:
    void RunNavigationTest();
};