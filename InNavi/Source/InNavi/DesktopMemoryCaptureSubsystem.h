#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "HistorySnapshotText.h"
#include "DesktopMemoryCaptureSubsystem.generated.h"

class ANavPlayerController;
class ABuildingGenerator;

// Explicit unattended diagnostic, available in packaged Shipping builds without enabling the console.
UCLASS()
class UDesktopMemoryCaptureSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool IsTickable() const override { return bEnabled && !IsTemplate(); }
	virtual void Tick(float DeltaTime) override;
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UDesktopMemoryCaptureSubsystem, STATGROUP_Tickables); }
private:
	void Finish(const FString& Error = FString());
	bool PrepareHistory();
	TWeakObjectPtr<ANavPlayerController> Controller;
	TWeakObjectPtr<ABuildingGenerator> Generator;
	TSharedPtr<class FJsonObject> Data;
	TArray<TSharedPtr<class FJsonValue>> Samples;
	TArray<FString> PlainHistory;
	TArray<FHistorySnapshotText> CompressedHistory;
	FString Output, Project, BuildingId, HistoryMode;
	int32 HistoryCount = 500;
	bool bEnabled = false, bStarted = false, bAdvance = false, bScreenshots = false;
	int32 Phase = 0, PhaseSamples = 0;
	double NextAction = 0;
};
