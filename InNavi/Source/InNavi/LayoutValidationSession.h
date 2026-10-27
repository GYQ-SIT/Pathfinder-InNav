#pragma once

#include "CoreMinimal.h"
#include "LayoutValidation.h"
#include "UObject/Object.h"
#include "LayoutValidationSession.generated.h"

class ABuildingGenerator;
class AInteriorGenerator;
class ANavPlayerController;

// One report per controller, shared by the exterior/interior panels. Validation never writes layout data.
UCLASS()
class INNAVI_API ULayoutValidationSession : public UObject
{
    GENERATED_BODY()
public:
    void SetContext(ABuildingGenerator* Buildings, AInteriorGenerator* Interior, const FString& Source);
    void Validate(const FString& ScopeBuildingId = FString());
    void RefreshStaleness();
    bool HasReport() const { return bHasReport; }
    bool IsStale() const { return bStale; }
    uint64 GetGeneration() const { return Generation; }
    const LayoutValidation::FReport& GetReport() const { return Report; }
    bool ShowIssue(ANavPlayerController& Controller, int32 IssueIndex);
    bool Export(const FString& Path);
    const LayoutValidation::FIssue* GetFocusedIssue() const;
    static TSharedRef<FJsonObject> Capture(const ABuildingGenerator& Buildings, const AInteriorGenerator& Interior);
private:
    UPROPERTY() ABuildingGenerator* Buildings = nullptr;
    UPROPERTY() AInteriorGenerator* Interior = nullptr;
    FString Source;
    LayoutValidation::FReport Report;
    bool bHasReport = false;
    bool bStale = false;
    int32 FocusedIssue = INDEX_NONE;
    uint64 Generation = 0;
};
