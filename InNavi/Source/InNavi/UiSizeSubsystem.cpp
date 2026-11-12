// Fill out your copyright notice in the Description page of Project Settings.

#include "UiSizeSubsystem.h"
#include "UiSize.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* const SettingsSection = TEXT("InNavi.Display");
	const TCHAR* const SettingsKey = TEXT("UiSizePercent");

	// Ctrl + (or the numpad +), Ctrl -, Ctrl 0: the size changes from anywhere in the program, on every screen. The keys are taken only with Ctrl
	// held, so the plain = - 0 keys keep their uses (grow and shrink a room, rotate).
	class FUiSizeKeys : public IInputProcessor
	{
	public:
		explicit FUiSizeKeys(UUiSizeSubsystem* InOwner) : Owner(InOwner) {}

		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			UUiSizeSubsystem* Subsystem = Owner.Get();
			if (!Subsystem || !InKeyEvent.IsControlDown() || InKeyEvent.IsAltDown())
			{
				return false;
			}
			const FKey Key = InKeyEvent.GetKey();
			if (Key == EKeys::Equals || Key == EKeys::Add)
			{
				if (!InKeyEvent.IsRepeat()) { Subsystem->Larger(); }
				return true;
			}
			if (Key == EKeys::Hyphen || Key == EKeys::Subtract)
			{
				if (!InKeyEvent.IsRepeat()) { Subsystem->Smaller(); }
				return true;
			}
			if (Key == EKeys::Zero || Key == EKeys::NumPadZero)
			{
				if (!InKeyEvent.IsRepeat()) { Subsystem->Reset(); }
				return true;
			}
			return false;
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("InNaviUiSizeKeys"); }

	private:
		TWeakObjectPtr<UUiSizeSubsystem> Owner;
	};
}

bool UUiSizeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if PLATFORM_WINDOWS
	return Super::ShouldCreateSubsystem(Outer) && !IsRunningCommandlet() && !IsRunningDedicatedServer();
#else
	(void)Outer;
	return false;
#endif
}

void UUiSizeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if PLATFORM_WINDOWS
	OriginalApplicationScale = GetDefault<UUserInterfaceSettings>()->ApplicationScale;

	int32 Saved = UiSize::DefaultPercent;
	GConfig->GetInt(SettingsSection, SettingsKey, Saved, GGameUserSettingsIni);
	SetPercent(Saved, /*bSave=*/false);

	if (FSlateApplication::IsInitialized())
	{
		KeyProcessor = MakeShared<FUiSizeKeys>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(KeyProcessor);
	}
#endif
}

void UUiSizeSubsystem::Deinitialize()
{
#if PLATFORM_WINDOWS
	if (KeyProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(KeyProcessor);
	}
	KeyProcessor.Reset();
	GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale = OriginalApplicationScale;
#endif
	Super::Deinitialize();
}

UUiSizeSubsystem* UUiSizeSubsystem::Get(const UObject* WorldContext)
{
	if (!GEngine)
	{
		return nullptr;
	}
	const UWorld* World = WorldContext ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (UGameInstance* Instance = World ? World->GetGameInstance() : nullptr)
	{
		return Instance->GetSubsystem<UUiSizeSubsystem>();
	}
	// A widget that is still being built may not know its world yet: take the running game (not the editor's own world).
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.OwningGameInstance)
		{
			return Context.OwningGameInstance->GetSubsystem<UUiSizeSubsystem>();
		}
	}
	return nullptr;
}

void UUiSizeSubsystem::SetPercent(int32 NewPercent, bool bSave)
{
	Percent = UiSize::Clamp(NewPercent);
	ApplyToSettings();
	UE_LOG(LogTemp, Display, TEXT("[UiSize] %d%%"), Percent);
	if (bSave && GConfig)
	{
		GConfig->SetInt(SettingsSection, SettingsKey, Percent, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UUiSizeSubsystem::Larger()
{
	SetPercent(UiSize::Step(Percent, +1), bSaveEnabled);
}

void UUiSizeSubsystem::Smaller()
{
	SetPercent(UiSize::Step(Percent, -1), bSaveEnabled);
}

void UUiSizeSubsystem::Reset()
{
	SetPercent(UiSize::DefaultPercent, bSaveEnabled);
}

void UUiSizeSubsystem::ApplyToSettings() const
{
	GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale = UiSize::ToApplicationScale(Percent);
}
