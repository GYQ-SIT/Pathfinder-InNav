#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BuildingJsonWriter.generated.h"

// Stores the information for ONE building
USTRUCT(BlueprintType)
struct FBuildingData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Id;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float OriginX = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float OriginY = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Width = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Depth = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 FloorCount = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float FloorHeight = 100.0f;
};


UCLASS()
class INNAVI_API UBuildingJsonWriter : public UObject
{
    GENERATED_BODY()

public:

    // SAVE
    UFUNCTION(BlueprintCallable, Category = "Building JSON")
    static bool SaveBuildingsToJson(
        const TArray<FBuildingData>& Buildings
    );

    // LOAD
    UFUNCTION(BlueprintCallable, Category = "Building JSON")
    static bool LoadBuildingsFromJson(
        TArray<FBuildingData>& Buildings
    );
};