#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "NavCameraPawn.generated.h"

// Free-fly camera: C descends, Space/E ascends. Ctrl is reserved for editor shortcuts.
UCLASS()
class INNAVI_API ANavCameraPawn : public ADefaultPawn
{
	GENERATED_BODY()
public:
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void MoveForward(float Val) override;
	virtual void MoveRight(float Val) override;
	virtual void MoveUp_World(float Val) override;
private:
	bool IsShortcutHeld() const;
};
