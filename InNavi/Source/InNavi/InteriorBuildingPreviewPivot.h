#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteriorBuildingPreviewPivot.generated.h"

class APolyBuilding;
class AInteriorGenerator;
class ABuildingGenerator;
class ULineBatchComponent;

// Transient presentation hierarchy. Authored footprints and navigation coordinates never change.
UCLASS(Transient, NotBlueprintable)
class INNAVI_API AInteriorBuildingPreviewPivot : public AActor
{
	GENERATED_BODY()
public:
	AInteriorBuildingPreviewPivot();
	bool Initialize(APolyBuilding* Building, AInteriorGenerator* Interior, ABuildingGenerator* Generator);
	void RotateBy(float YawDegrees, float TiltDegrees);
	void ResetRotation();
	void Restore();
	void ApplyIsolation();
	FTransform GetDisplayTransform() const;
	float GetSweepRadius() const { return SweepRadius; }
	int32 GetLowestFloor() const { return LowestFloor; }
	bool HasBuilding() const { return ViewedBuilding.IsValid(); }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;

private:
	struct FModelState
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<USceneComponent> Parent;
		FName Socket;
		FTransform World, Relative;
		EComponentMobility::Type Mobility = EComponentMobility::Movable;
		bool bHidden = false, bCollision = false;
	};
	struct FSceneState
	{
		TWeakObjectPtr<AActor> Actor;
		bool bHidden = false, bCollision = false;
	};
	bool GroupActor(AActor* Actor);
	TArray<FModelState> ModelStates;
	UPROPERTY()
	ULineBatchComponent* PivotGuide = nullptr;
	TArray<FSceneState> SceneStates;
	TWeakObjectPtr<APolyBuilding> ViewedBuilding;
	TWeakObjectPtr<ABuildingGenerator> ViewedGenerator;
	bool bGroundWasSuspended = false;
	int32 OriginalVisibleLevel = 0;
	int32 LowestFloor = 1;
	float Yaw = 0.f, Tilt = 0.f, SweepRadius = 100.f;
};
