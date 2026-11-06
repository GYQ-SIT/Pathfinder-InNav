#include "NavCameraPawn.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"

void ANavCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	// Keep the engine's mouse/gamepad controls, replacing only the vertical axis that includes Ctrl.
	PlayerInputComponent->AxisBindings.RemoveAll([](const FInputAxisBinding& Binding)
	{
		return Binding.AxisName == TEXT("DefaultPawn_MoveUp");
	});
	static bool bAddedMappings = false;
	if (!bAddedMappings)
	{
		bAddedMappings = true;
		for (const FInputAxisKeyMapping& Mapping : {
			FInputAxisKeyMapping(TEXT("InNaviCamera_MoveUp"), EKeys::C, -1.f),
			FInputAxisKeyMapping(TEXT("InNaviCamera_MoveUp"), EKeys::SpaceBar, 1.f),
			FInputAxisKeyMapping(TEXT("InNaviCamera_MoveUp"), EKeys::E, 1.f),
			FInputAxisKeyMapping(TEXT("InNaviCamera_MoveUp"), EKeys::Gamepad_LeftThumbstick, 1.f),
			FInputAxisKeyMapping(TEXT("InNaviCamera_MoveUp"), EKeys::Gamepad_RightThumbstick, -1.f),
			FInputAxisKeyMapping(TEXT("InNaviCamera_MoveUp"), EKeys::Gamepad_FaceButton_Bottom, 1.f),
			FInputAxisKeyMapping(TEXT("InNaviCamera_MoveUp"), EKeys::Gamepad_LeftTriggerAxis, -0.5f),
			FInputAxisKeyMapping(TEXT("InNaviCamera_MoveUp"), EKeys::Gamepad_RightTriggerAxis, 0.5f) })
		{
			UPlayerInput::AddEngineDefinedAxisMapping(Mapping);
		}
	}
	PlayerInputComponent->BindAxis(TEXT("InNaviCamera_MoveUp"), this, &ANavCameraPawn::MoveUp_World);
}

bool ANavCameraPawn::IsShortcutHeld() const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	return PC && (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl));
}

void ANavCameraPawn::MoveForward(float Val)
{
	if (!IsShortcutHeld()) Super::MoveForward(Val);
}

void ANavCameraPawn::MoveRight(float Val)
{
	if (!IsShortcutHeld()) Super::MoveRight(Val);
}

void ANavCameraPawn::MoveUp_World(float Val)
{
	if (!IsShortcutHeld()) Super::MoveUp_World(Val);
}
