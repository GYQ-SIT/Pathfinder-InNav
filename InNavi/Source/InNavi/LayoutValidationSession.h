#pragma once

#include "CoreMinimal.h"
#include "LayoutValidation.h"
#include "Containers/Ticker.h"
#include "UObject/Object.h"
#include "LayoutValidationSession.generated.h"

class ABuildingGenerator;
class AInteriorGenerator;
class ANavPlayerController;
class FNavigationLayoutCheck;

// One report per controller, shared by the exterior/interior panels. Validation never writes layout data.
UCLASS()
class INNAVI_API ULayoutValidationSession : public UObject
{
    GENERATED_BODY()
public:
    void SetContext(ABuildingGenerator* Buildings, AInteriorGenerator* Interior, const FString& Source);
    // All at once: the editor waits until the report is ready.
    void Validate(const FString& ScopeBuildingId = FString());
    // The same check spread over frames, so the editor stays usable and the panel can show progress. The session steps itself
    // from the core ticker; there is no report until it has finished (IsRunning is false and HasReport is true again).
    void StartValidate(const FString& ScopeBuildingId = FString());
    // Works for about BudgetSeconds; true once finished. The ticker calls this; tests may call it directly.
    bool StepValidate(double BudgetSeconds);
    // Stops a run that has started. There is no report afterwards.
    void CancelValidate();
    bool IsRunning() const { return bRunning; }
    // 0 to 1, and what is being done now, while running.
    float GetProgress() const;
    FString GetProgressText() const;
    // How many times the route checks have been started: they are the slow part, and viewing an issue must not start them again.
    uint32 GetRouteCheckRuns() const { return RouteCheckRuns; }
    void RefreshStaleness();
    bool HasReport() const { return bHasReport; }
    bool IsStale() const { return bStale; }
    uint64 GetGeneration() const { return Generation; }
    const LayoutValidation::FReport& GetReport() const { return Report; }
    bool ShowIssue(ANavPlayerController& Controller, int32 IssueIndex);
    bool Export(const FString& Path);
    const LayoutValidation::FIssue* GetFocusedIssue() const;
    static TSharedRef<FJsonObject> Capture(const ABuildingGenerator& Buildings, const AInteriorGenerator& Interior);
    virtual void BeginDestroy() override;
private:
    void BeginValidate(const FString& ScopeBuildingId);
    void FinishValidate();
    // After an issue has been viewed: the layout is now written a little differently (see the definition); brings the report up to date.
    void RefreshAfterViewing();
    // What the route checks found in the last run, and whether the data checks had found errors then.
    TArray<LayoutValidation::FIssue> RouteIssues;
    bool bDataHadErrors = false;
    uint32 RouteCheckRuns = 0;
    int32 DataIssueCount = 0;
    void StopTicker();
    bool TickValidate(float DeltaTime);
    TSharedPtr<FNavigationLayoutCheck> Check;
    FString RunScope;
    bool bRunning = false;
    FTSTicker::FDelegateHandle TickHandle;
    UPROPERTY() ABuildingGenerator* Buildings = nullptr;
    UPROPERTY() AInteriorGenerator* Interior = nullptr;
    FString Source;
    LayoutValidation::FReport Report;
    bool bHasReport = false;
    bool bStale = false;
    int32 FocusedIssue = INDEX_NONE;
    uint64 Generation = 0;
};
