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
	// Turns the model about its own centre: Yaw about the vertical axis, Tilt about the axis across the view (SetTiltRange), kept between
	// the limits so that the model can be looked at from above and never from underneath or upside down.
	void RotateBy(float YawDegrees, float TiltDegrees);
	void SetTiltRange(const FVector& Axis, float MinDegrees, float MaxDegrees);
	float GetTilt() const { return Tilt; }
	// Slides the model (and the point it turns about) by a distance, never further than MaxDistance from where it started.
	void TranslateBy(const FVector& Delta, float MaxDistance);
	// The model as it was entered: not moved, not turned.
	void ResetRotation();
	// Where the model's centre started (the centre of the lowest floor), and what the camera looks at: it does not follow a pan.
	FVector GetDefaultPivot() const { return DefaultPivot; }
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
	void DrawGuide();
	void AttachModel();
	void DetachModel();
	TArray<FModelState> ModelStates;
	UPROPERTY()
	ULineBatchComponent* PivotGuide = nullptr;
	TArray<FSceneState> SceneStates;
	TWeakObjectPtr<APolyBuilding> ViewedBuilding;
	TWeakObjectPtr<ABuildingGenerator> ViewedGenerator;
	bool bGroundWasSuspended = false;
	FVector DefaultPivot = FVector::ZeroVector;
	FVector TiltAxis = FVector::RightVector;
	float TiltMin = -15.f, TiltMax = 60.f;
	// The building as it was, to say how far the model has moved from it (GetDisplayTransform).
	FTransform OriginalBuildingWorld = FTransform::Identity;
	float GuideSize = 25.f;
	int32 OriginalVisibleLevel = 0;
	int32 LowestFloor = 1;
	float Yaw = 0.f, Tilt = 0.f, SweepRadius = 100.f;
};
