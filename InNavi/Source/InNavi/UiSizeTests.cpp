#if WITH_DEV_AUTOMATION_TESTS

#include "UiSize.h"
#include "UiSizeSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/UserInterfaceSettings.h"
#include "Misc/AutomationTest.h"

// The user's UI size: the sizes the menu and the keys step through, and how a size reaches the engine's UI scale.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiSizeStepsTest, "InNavi.UiSize.Steps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiSizeStepsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("100 is the default"), UiSize::DefaultPercent, 100);
	TestTrue(TEXT("the default is one of the steps"), UiSize::Steps().Contains(UiSize::DefaultPercent));
	TestEqual(TEXT("the steps run from the smallest to the largest size"), UiSize::Steps()[0], UiSize::MinPercent);
	TestEqual(TEXT("to"), UiSize::Steps().Last(), UiSize::MaxPercent);

	TestEqual(TEXT("one step up from the default"), UiSize::Step(100, +1), 110);
	TestEqual(TEXT("and down"), UiSize::Step(100, -1), 90);
	TestEqual(TEXT("up from 125"), UiSize::Step(125, +1), 150);
	TestEqual(TEXT("down from 125"), UiSize::Step(125, -1), 110);

	// The ends of the range hold.
	TestEqual(TEXT("no larger than the largest"), UiSize::Step(UiSize::MaxPercent, +1), UiSize::MaxPercent);
	TestEqual(TEXT("no smaller than the smallest"), UiSize::Step(UiSize::MinPercent, -1), UiSize::MinPercent);

	// A hand-edited size between two steps goes to the nearest step in the direction asked; one out of range is brought into range first.
	TestEqual(TEXT("between steps, up"), UiSize::Step(117, +1), 125);
	TestEqual(TEXT("between steps, down"), UiSize::Step(117, -1), 110);
	TestEqual(TEXT("too large"), UiSize::Clamp(900), UiSize::MaxPercent);
	TestEqual(TEXT("too small"), UiSize::Clamp(-5), UiSize::MinPercent);
	TestEqual(TEXT("stepping from out of range starts at the end"), UiSize::Step(900, -1), 175);

	// Stepping up from the smallest reaches every step once, and the largest is reached.
	int32 Size = UiSize::MinPercent;
	int32 Count = 1;
	while (Size < UiSize::MaxPercent && Count < 50)
	{
		const int32 Next = UiSize::Step(Size, +1);
		TestTrue(TEXT("every step goes up"), Next > Size);
		Size = Next;
		++Count;
	}
	TestEqual(TEXT("all the steps are visited"), Count, UiSize::Steps().Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiSizeAppliedTest, "InNavi.UiSize.ScalesTheWholeUi", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUiSizeAppliedTest::RunTest(const FString& Parameters)
{
	UUserInterfaceSettings* Settings = GetMutableDefault<UUserInterfaceSettings>();
	const float Original = Settings->ApplicationScale;
	Settings->ApplicationScale = 1.f;
	const float Base = Settings->GetDPIScaleBasedOnSize(FIntPoint(1920, 1080));

	TestEqual(TEXT("a size of 100 is the factor 1"), UiSize::ToApplicationScale(100), 1.f, 0.0001f);
	TestEqual(TEXT("150 is one and a half"), UiSize::ToApplicationScale(150), 1.5f, 0.0001f);
	TestEqual(TEXT("an out-of-range size is brought into range"), UiSize::ToApplicationScale(1000), 2.f, 0.0001f);

	// The subsystem puts the size on top of the curve; no save, so the user's own setting is not touched.
	UGameInstance* Instance = NewObject<UGameInstance>(GetTransientPackage());
	UUiSizeSubsystem* Subsystem = NewObject<UUiSizeSubsystem>(Instance);
	Subsystem->SetPercent(125, /*bSave=*/false);
	TestEqual(TEXT("the UI scale is the curve times the size"), Settings->GetDPIScaleBasedOnSize(FIntPoint(1920, 1080)), Base * 1.25f, 0.0001f);
	TestEqual(TEXT("at any resolution"), Settings->GetDPIScaleBasedOnSize(FIntPoint(3840, 2160)), 1.8f * 1.25f, 0.001f);

	Subsystem->SetPercent(UiSize::DefaultPercent, false);
	TestEqual(TEXT("back to the default"), Settings->GetDPIScaleBasedOnSize(FIntPoint(1920, 1080)), Base, 0.0001f);

	Settings->ApplicationScale = Original;
	return true;
}

#endif
