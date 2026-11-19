#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorScreenWidget.h"
#include "NavPlayerController.h"
#include "PolyBuilding.h"
#include "UiTestFixture.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// PAT-201: a PNG under the 2D drawing of a floor.

namespace
{
	// A 1 x 1 PNG: the tests only need a file that is one, since nothing is painted here.
	FString WriteTinyPng(const FString& Name)
	{
		static const uint8 Bytes[] = { 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,
			0x1F,0x15,0xC4,0x89,0x00,0x00,0x00,0x0A,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0x00,0x01,0x00,0x00,0x05,0x00,0x01,0x0D,0x0A,0x2D,0xB4,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82 };
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / Name);
		FFileHelper::SaveArrayToFile(TArrayView<const uint8>(Bytes, UE_ARRAY_COUNT(Bytes)), *Path);
		return Path;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReferenceImageTest, "InNavi.Interior.ReferenceImage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReferenceImageTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F;
	F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
	APolyBuilding* Building = F.Generator->CreateRectangleBuilding(TEXT("REF"), TEXT("REF"), {500,500}, 1000, 1000, 0, 2, 300);
	if (!TestNotNull(TEXT("a building"), Building)) return false;
	F.Interior->LoadInteriorForBuilding(TEXT("REF"));
	ANavPlayerController* PC = F.MakeController();
	PC->SetActiveFloor(1);
	const FString Png = WriteTinyPng(TEXT("ref_plan.png"));
	const FString NotPng = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("ref_plan.txt"));
	FFileHelper::SaveStringToFile(TEXT("not a picture"), *NotPng);

	// Importing: only a PNG that is there; framed on the building.
	TestFalse(TEXT("a text file is refused"), PC->ImportReferenceImage(NotPng));
	TestFalse(TEXT("a PNG that is not there is refused"), PC->ImportReferenceImage(FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("nope.png")));
	FInteriorReferenceImage Image;
	TestFalse(TEXT("nothing was added"), PC->GetActiveReferenceImage(Image));
	TestTrue(TEXT("a PNG is imported"), PC->ImportReferenceImage(Png));
	if (!TestTrue(TEXT("and the floor has it"), PC->GetActiveReferenceImage(Image))) return false;
	const FBox2D Bounds(Building->GetFootprint());
	TestTrue(TEXT("centred on the building"), Image.Center.Equals(Bounds.GetCenter(), 0.01));
	TestTrue(TEXT("as wide as the building"), FMath::IsNearlyEqual(Image.Width, static_cast<float>(Bounds.GetSize().X), 0.01f));
	TestTrue(TEXT("half clear, shown"), FMath::IsNearlyEqual(Image.Opacity, 0.5f) && Image.bVisible);
	TestFalse(TEXT("another floor has none"), F.Interior->GetReferenceImage(2) != nullptr);

	// Editing: position, width, opacity (kept within range), hidden. A width that is not a positive number is refused.
	Image.Center += FVector2D(30, -20); Image.Width = 400.f; Image.Opacity = 3.f; Image.bVisible = false;
	TestTrue(TEXT("changed"), PC->SetActiveReferenceImage(Image));
	FInteriorReferenceImage Kept;
	PC->GetActiveReferenceImage(Kept);
	TestTrue(TEXT("opacity is kept within range"), FMath::IsNearlyEqual(Kept.Opacity, 1.f) && !Kept.bVisible && FMath::IsNearlyEqual(Kept.Width, 400.f));
	Image.Width = 0.f;
	TestFalse(TEXT("a width of 0 is refused"), PC->SetActiveReferenceImage(Image));
	Image.Width = 400.f; Image.Opacity = 0.f;
	PC->SetActiveReferenceImage(Image);
	PC->GetActiveReferenceImage(Kept);
	TestTrue(TEXT("and an opacity of 0 is raised to the least"), FMath::IsNearlyEqual(Kept.Opacity, InteriorReferenceImage::MinOpacity));

	// Saved with the floor, and back after a reload.
	TSharedPtr<FJsonObject> Saved = F.Interior->CaptureInteriorSnapshot();
	const TArray<TSharedPtr<FJsonValue>>* Floors = nullptr;
	if (!TestTrue(TEXT("floors saved"), Saved.IsValid() && Saved->TryGetArrayField(TEXT("floors"), Floors))) return false;
	bool bFirstHas = false, bSecondHas = false, bHiddenWritten = false;
	for (const TSharedPtr<FJsonValue>& Value : *Floors)
	{
		double Level = 0; Value->AsObject()->TryGetNumberField(TEXT("level"), Level);
		const TSharedPtr<FJsonObject>* Object = nullptr;
		const bool bHas = Value->AsObject()->TryGetObjectField(TEXT("referenceImage"), Object);
		if (static_cast<int32>(Level) == 1)
		{
			bFirstHas = bHas;
			bool bVisible = true;
			bHiddenWritten = bHas && (*Object)->TryGetBoolField(TEXT("visible"), bVisible) && !bVisible;
		}
		else bSecondHas = bHas;
	}
	TestTrue(TEXT("floor 1 has the image in the file, hidden"), bFirstHas && bHiddenWritten);
	TestFalse(TEXT("floor 2 has no field"), bSecondHas);
	F.Generator->SetInteriorDataForBuilding(TEXT("REF"), Saved);
	F.Interior->LoadInteriorForBuilding(TEXT("REF"));
	const FInteriorReferenceImage* Back = F.Interior->GetReferenceImage(1);
	TestTrue(TEXT("it comes back as it was"), Back && *Back == Kept);

	// Undo, remove, and taking the floor away.
	TestTrue(TEXT("removed"), F.Interior->RemoveReferenceImage(1) && !F.Interior->GetReferenceImage(1));
	TestFalse(TEXT("removing again does nothing"), F.Interior->RemoveReferenceImage(1));
	TestTrue(TEXT("Undo brings it back"), F.Interior->Undo() && F.Interior->GetReferenceImage(1));
	TestTrue(TEXT("a floor taken away takes its image"), F.Interior->SetReferenceImage(2, Kept) && F.Interior->SetLevelRange(1, 1) && !F.Interior->GetReferenceImage(2) && F.Interior->GetReferenceImage(1));
	return true;
}

// The Floor panel: the fields are in metres and apply on Enter; Remove takes the image off.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReferenceImagePanelTest, "InNavi.EditorUX.ReferenceImage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReferenceImagePanelTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F;
	F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
	F.Generator->CreateRectangleBuilding(TEXT("REP"), TEXT("REP"), {500,500}, 1000, 1000, 0, 2, 300);
	F.Interior->LoadInteriorForBuilding(TEXT("REP"));
	ANavPlayerController* PC = F.MakeController();
	PC->SetActiveFloor(1);
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(PC);
	auto* Import = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_ImportReference")));
	auto* Remove = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_RemoveReference")));
	auto* Shown = Cast<UCheckBox>(Screen->FindScreenWidget(TEXT("Chk_ReferenceShown")));
	auto* Width = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Edt_ReferenceWidth")));
	auto* X = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Edt_ReferenceX")));
	auto* Opacity = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Edt_ReferenceOpacity")));
	if (!TestNotNull(TEXT("Import PNG"), Import) || !TestNotNull(TEXT("Remove"), Remove) || !TestNotNull(TEXT("Show image"), Shown)
		|| !TestNotNull(TEXT("Width"), Width) || !TestNotNull(TEXT("Centre X"), X) || !TestNotNull(TEXT("Opacity"), Opacity)) return false;

	TestTrue(TEXT("the file is imported by the controller (the button opens the system dialog)"), PC->ImportReferenceImage(WriteTinyPng(TEXT("ref_panel.png"))));
	const float UnitCm = PC->GetFootprintUnitCm();
	Width->SetText(FText::FromString(TEXT("25"))); X->SetText(FText::FromString(TEXT("12.5"))); Opacity->SetText(FText::FromString(TEXT("80")));
	Width->OnTextCommitted.Broadcast(Width->GetText(), ETextCommit::OnEnter);
	FInteriorReferenceImage Image;
	PC->GetActiveReferenceImage(Image);
	TestTrue(TEXT("the width is read in metres"), FMath::IsNearlyEqual(Image.Width, 2500.f / UnitCm, 0.01f));
	TestTrue(TEXT("so is the position"), FMath::IsNearlyEqual(static_cast<float>(Image.Center.X), 1250.f / UnitCm, 0.01f));
	TestTrue(TEXT("and the opacity in per cent"), FMath::IsNearlyEqual(Image.Opacity, 0.8f, 0.001f));
	Width->SetText(FText::FromString(TEXT("abc")));
	Width->OnTextCommitted.Broadcast(Width->GetText(), ETextCommit::OnEnter);
	FInteriorReferenceImage After;
	PC->GetActiveReferenceImage(After);
	TestTrue(TEXT("text that is no number changes nothing"), After == Image);
	Shown->SetIsChecked(false); Shown->OnCheckStateChanged.Broadcast(false);
	PC->GetActiveReferenceImage(After);
	TestFalse(TEXT("the tick hides the image"), After.bVisible);
	Remove->OnClicked.Broadcast();
	TestFalse(TEXT("Remove takes it off the floor"), PC->GetActiveReferenceImage(After));
	return true;
}

#endif
