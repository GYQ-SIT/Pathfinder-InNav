#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DesktopRenderingSubsystem.generated.h"

class UGameViewportClient;
class UUserWidget;

UCLASS()
class INNAVI_API UDesktopRenderingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	bool IsAvailable() const { return bAvailable; }
	bool IsLightweight() const { return bLightweight; }
	void SetLightweight(bool bEnabled, bool bPersist = true);
	// Full-screen opaque library pages can suspend the world without unloading project data.
	void SetOpaqueScreen(UUserWidget* Screen, bool bVisible);

private:
	friend class FDesktopRenderingPresetTest;
	friend class FDesktopRenderingScreenTest;
	void RestoreWorldRendering();
	void UpdateOpaqueScreen(UUserWidget* Screen, bool bVisible, UGameViewportClient* Viewport);
	TMap<FString, int32> OriginalSettings;
	TSet<TWeakObjectPtr<UUserWidget>> OpaqueScreens;
	TWeakObjectPtr<UGameViewportClient> PausedViewport;
	bool bPreviousWorldRenderingDisabled = false;
	bool bAvailable = false;
	bool bLightweight = false;
};
