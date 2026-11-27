#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "InteriorFloorPlanWidget.h"
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

#include <limits>

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
	FScriptDelegate Content;
	Content.BindUFunction(Screen, TEXT("HandleInteriorContentChanged"));
	PC->OnInteriorContentChanged.AddUnique(Content);
	auto* Import = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_ImportReference")));
	auto* Remove = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_RemoveReference")));
	auto* Shown = Cast<UCheckBox>(Screen->FindScreenWidget(TEXT("Chk_ReferenceShown")));
	auto* Lock = Cast<UCheckBox>(Screen->FindScreenWidget(TEXT("Chk_ReferenceLock")));
	auto* Scale = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Edt_ReferenceScale")));
	auto* X = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Edt_ReferenceX")));
	auto* Y = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Edt_ReferenceY")));
	auto* Opacity = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Edt_ReferenceOpacity")));
	auto* CropLeft = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Edt_ReferenceCropLeft")));
	auto* CropRight = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Edt_ReferenceCropRight")));
	auto* CropTop = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Edt_ReferenceCropTop")));
	auto* CropBottom = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Edt_ReferenceCropBottom")));
	if (!TestNotNull(TEXT("the four Crop boxes"), CropLeft) || !TestNotNull(TEXT("Crop right"), CropRight) || !TestNotNull(TEXT("Crop top"), CropTop) || !TestNotNull(TEXT("Crop bottom"), CropBottom)) return false;
	if (!TestNotNull(TEXT("Import PNG"), Import) || !TestNotNull(TEXT("Remove"), Remove) || !TestNotNull(TEXT("Show image"), Shown) || !TestNotNull(TEXT("Lock image"), Lock)
		|| !TestNotNull(TEXT("Scale"), Scale) || !TestNotNull(TEXT("Position X"), X) || !TestNotNull(TEXT("Position Y"), Y) || !TestNotNull(TEXT("Opacity"), Opacity)) return false;
	TestTrue(TEXT("the image is locked at first"), Lock->IsChecked() && !PC->IsReferenceImageUnlocked());

	TestTrue(TEXT("the file is imported by the controller (the button opens the system dialog)"), PC->ImportReferenceImage(WriteTinyPng(TEXT("ref_panel.png"))));
	const float UnitCm = PC->GetFootprintUnitCm();
	FVector2D FrameCentre; double FrameWidth = 0.0;
	if (!TestTrue(TEXT("the building's frame"), PC->GetActiveReferenceFrame(FrameCentre, FrameWidth))) return false;
	// Imported, the picture is the building's own width and in its middle: 100 per cent, position (0, 0).
	TestEqual(TEXT("the scale starts at 100 per cent"), Scale->GetText().ToString(), FString(TEXT("100.00")));
	TestTrue(TEXT("and the position at 0, 0"), X->GetText().ToString() == TEXT("0.00") && Y->GetText().ToString() == TEXT("0.00"));
	Scale->SetText(FText::FromString(TEXT("50"))); X->SetText(FText::FromString(TEXT("12.5"))); Y->SetText(FText::FromString(TEXT("-3"))); Opacity->SetText(FText::FromString(TEXT("80")));
	Scale->OnTextCommitted.Broadcast(Scale->GetText(), ETextCommit::OnEnter);
	FInteriorReferenceImage Image;
	PC->GetActiveReferenceImage(Image);
	TestTrue(TEXT("the scale is a per cent of the building's width"), FMath::IsNearlyEqual(Image.Width, static_cast<float>(FrameWidth * 0.5), 0.01f));
	TestTrue(TEXT("the position is in metres from the middle of the building"), FMath::IsNearlyEqual(static_cast<float>(Image.Center.X), static_cast<float>(FrameCentre.X + 1250.f / UnitCm), 0.01f)
		&& FMath::IsNearlyEqual(static_cast<float>(Image.Center.Y), static_cast<float>(FrameCentre.Y - 300.f / UnitCm), 0.01f));
	TestTrue(TEXT("and the opacity in per cent"), FMath::IsNearlyEqual(Image.Opacity, 0.8f, 0.001f));
	Scale->SetText(FText::FromString(TEXT("abc")));
	Scale->OnTextCommitted.Broadcast(Scale->GetText(), ETextCommit::OnEnter);
	FInteriorReferenceImage After;
	PC->GetActiveReferenceImage(After);
	TestTrue(TEXT("text that is no number changes nothing"), After == Image);
	// Crop: per cent of the whole picture cut away from each side; none to start with, and a value that is too big is brought back so a bit is left.
	TestTrue(TEXT("no crop at first"), CropLeft->GetText().ToString() == TEXT("0") && CropBottom->GetText().ToString() == TEXT("0"));
	CropLeft->SetText(FText::FromString(TEXT("10"))); CropTop->SetText(FText::FromString(TEXT("20"))); CropRight->SetText(FText::FromString(TEXT("95")));
	CropLeft->OnTextCommitted.Broadcast(CropLeft->GetText(), ETextCommit::OnEnter);
	PC->GetActiveReferenceImage(After);
	TestTrue(TEXT("the crop is kept as fractions"), FMath::IsNearlyEqual(After.CropLeft, 0.1f, 1e-4f) && FMath::IsNearlyEqual(After.CropTop, 0.2f, 1e-4f));
	TestTrue(TEXT("one side cannot take the last of the picture"), After.CropLeft + After.CropRight <= 0.95f + 1e-4f);
	TestEqual(TEXT("and the box shows what was kept"), CropRight->GetText().ToString(), FString::SanitizeFloat(FMath::RoundToDouble(After.CropRight * 1000.0) / 10.0, 0));
	for (UEditableTextBox* Box : { CropLeft, CropTop, CropRight, CropBottom }) { Box->SetText(FText::FromString(TEXT("0"))); }
	CropLeft->OnTextCommitted.Broadcast(CropLeft->GetText(), ETextCommit::OnEnter);
	PC->GetActiveReferenceImage(After);
	TestFalse(TEXT("zeros take the crop off"), After.HasCrop());
	// Lock image: unticked, the controller has the picture unlocked; on another floor it is locked again.
	Lock->SetIsChecked(false); Lock->OnCheckStateChanged.Broadcast(false);
	TestTrue(TEXT("unticking Lock image unlocks it"), PC->IsReferenceImageUnlocked());
	PC->SetActiveFloor(2); PC->SetActiveFloor(1);
	TestFalse(TEXT("it is locked again when the floor is left"), PC->IsReferenceImageUnlocked());
	Shown->SetIsChecked(false); Shown->OnCheckStateChanged.Broadcast(false);
	PC->GetActiveReferenceImage(After);
	TestFalse(TEXT("the tick hides the image"), After.bVisible);
	Remove->OnClicked.Broadcast();
	TestFalse(TEXT("Remove takes it off the floor"), PC->GetActiveReferenceImage(After));
	return true;
}

// Undo and Redo of a picture's size or place: the boxes show what is there at once, the rooms are not rebuilt (that made them lag), and a drag with the mouse is
// one step.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReferenceImageUndoTest, "InNavi.Interior.ReferenceImageUndo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReferenceImageUndoTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F;
	F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
	F.Generator->CreateRectangleBuilding(TEXT("REU"), TEXT("REU"), {500,500}, 1000, 1000, 0, 2, 300);
	F.Interior->LoadInteriorForBuilding(TEXT("REU"));
	ANavPlayerController* PC = F.MakeController();
	PC->SetActiveFloor(1);
	FString Error;
	AInteriorRoom* Room = F.Interior->CreateRoomFromFootprint({ {400, 400}, {600, 400}, {600, 600}, {400, 600} }, TEXT("R1"), TEXT("R1"), true, false, false, Error);
	if (!TestNotNull(TEXT("a room"), Room)) return false;
	if (!TestTrue(TEXT("a picture"), PC->ImportReferenceImage(WriteTinyPng(TEXT("ref_undo.png"))))) return false;
	FInteriorReferenceImage Start;
	PC->GetActiveReferenceImage(Start);

	// A change of size is one step; Undo puts the old size back without making the rooms again, and Redo the new one.
	FInteriorReferenceImage Bigger = Start;
	Bigger.Width = Start.Width * 1.5f;
	TestTrue(TEXT("bigger"), PC->SetActiveReferenceImage(Bigger));
	TestTrue(TEXT("Undo"), PC->UndoInteriorEdit());
	FInteriorReferenceImage Now;
	PC->GetActiveReferenceImage(Now);
	TestTrue(TEXT("the old size is back"), Now == Start);
	TestTrue(TEXT("the room is the same actor: nothing was rebuilt"), F.Interior->GetRoomById(TEXT("R1")) == Room);
	TestTrue(TEXT("Redo"), PC->RedoInteriorEdit());
	PC->GetActiveReferenceImage(Now);
	TestTrue(TEXT("the new size is back"), Now == Bigger);
	TestTrue(TEXT("the room is still the same actor"), F.Interior->GetRoomById(TEXT("R1")) == Room);

	// The boxes of the panel follow an Undo and a Redo at once.
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(PC);
	FScriptDelegate Content;
	Content.BindUFunction(Screen, TEXT("HandleInteriorContentChanged"));
	PC->OnInteriorContentChanged.AddUnique(Content);
	auto* Scale = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Edt_ReferenceScale")));
	if (!TestNotNull(TEXT("the Scale box"), Scale)) return false;
	PC->OnInteriorContentChanged.Broadcast();
	TestEqual(TEXT("the box says 150"), Scale->GetText().ToString(), FString(TEXT("150.00")));
	PC->UndoInteriorEdit();
	TestEqual(TEXT("after Undo it says 100 at once"), Scale->GetText().ToString(), FString(TEXT("100.00")));
	PC->RedoInteriorEdit();
	TestEqual(TEXT("after Redo, 150 again"), Scale->GetText().ToString(), FString(TEXT("150.00")));

	// A drag with the mouse is many previews and one step.
	FInteriorReferenceImage Moved = Bigger;
	for (int32 Step = 1; Step <= 20; ++Step)
	{
		Moved.Center.X = Bigger.Center.X + Step;
		PC->PreviewActiveReferenceImage(Moved);
	}
	PC->CommitActiveReferenceImage();
	PC->GetActiveReferenceImage(Now);
	TestTrue(TEXT("the picture is where the drag ended"), Now == Moved);
	TestTrue(TEXT("one Undo takes the whole drag back"), PC->UndoInteriorEdit());
	PC->GetActiveReferenceImage(Now);
	TestTrue(TEXT("to where it began"), Now == Bigger);
	return true;
}

// Crop of the reference image: the data (clamped, saved, back after a reload, one Undo step) and the drag of an edge handle.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReferenceImageCropTest, "InNavi.Interior.ReferenceImageCrop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReferenceImageCropTest::RunTest(const FString& Parameters)
{
	// Normalize keeps a bit of the picture in each direction and drops what is no number.
	FInteriorReferenceImage Raw;
	Raw.Path = TEXT("a.png"); Raw.Width = 10.f;
	Raw.CropLeft = 0.7f; Raw.CropRight = 0.7f; Raw.CropTop = -1.f; Raw.CropBottom = std::numeric_limits<float>::quiet_NaN();
	TestTrue(TEXT("kept"), InteriorReferenceImage::Normalize(Raw));
	TestTrue(TEXT("left and right leave at least the minimum"), Raw.CropLeft + Raw.CropRight <= 1.f - InteriorReferenceImage::MinVisibleFraction + 1e-4f && Raw.CropRight > 0.f);
	TestTrue(TEXT("a negative or no-number crop is none"), Raw.CropTop == 0.f && Raw.CropBottom == 0.f);

	// What is left after a crop is a box inside the whole picture, in its own axes.
	FInteriorReferenceImage Picture;
	Picture.Path = TEXT("a.png"); Picture.Center = FVector2D(100.0, 50.0); Picture.Width = 40.f;
	const float Aspect = 2.f;	// 40 wide, 80 high: X from 80 to 120, Y from 10 to 90
	FVector2D Min, Max;
	UInteriorFloorPlanWidget::GetReferenceVisibleBounds(Picture, Aspect, Min, Max);
	TestTrue(TEXT("without a crop it is the whole picture"), Min.Equals(FVector2D(80.0, 10.0), 1e-6) && Max.Equals(FVector2D(120.0, 90.0), 1e-6));
	Picture.CropLeft = 0.25f; Picture.CropTop = 0.5f; Picture.CropRight = 0.f; Picture.CropBottom = 0.125f;
	UInteriorFloorPlanWidget::GetReferenceVisibleBounds(Picture, Aspect, Min, Max);
	TestTrue(TEXT("with a crop it is the part that is left"), Min.Equals(FVector2D(90.0, 50.0), 1e-6) && Max.Equals(FVector2D(120.0, 80.0), 1e-6));

	// Dragging an edge handle: that side goes to where the pointer is; the others stay; a bit is always left.
	Picture = FInteriorReferenceImage();
	Picture.Path = TEXT("a.png"); Picture.Center = FVector2D(100.0, 50.0); Picture.Width = 40.f;
	FInteriorReferenceImage Dragged = UInteriorFloorPlanWidget::CropReferenceImageEdge(Picture, Aspect, 0, FVector2D(90.0, 0.0));
	TestTrue(TEXT("the left edge at a quarter of the width"), FMath::IsNearlyEqual(Dragged.CropLeft, 0.25f, 1e-4f) && Dragged.CropTop == 0.f && Dragged.CropRight == 0.f);
	Dragged = UInteriorFloorPlanWidget::CropReferenceImageEdge(Dragged, Aspect, 2, FVector2D(110.0, 0.0));
	TestTrue(TEXT("the right edge at three quarters"), FMath::IsNearlyEqual(Dragged.CropRight, 0.25f, 1e-4f) && FMath::IsNearlyEqual(Dragged.CropLeft, 0.25f, 1e-4f));
	Dragged = UInteriorFloorPlanWidget::CropReferenceImageEdge(Dragged, Aspect, 1, FVector2D(100.0, 50.0));
	TestTrue(TEXT("the top edge in the middle of the height"), FMath::IsNearlyEqual(Dragged.CropTop, 0.5f, 1e-4f));
	Dragged = UInteriorFloorPlanWidget::CropReferenceImageEdge(Dragged, Aspect, 3, FVector2D(100.0, 0.0));
	TestTrue(TEXT("dragged far past the other side it stops with a bit left"), Dragged.CropBottom + Dragged.CropTop <= 1.f - InteriorReferenceImage::MinVisibleFraction + 1e-4f);
	Dragged = UInteriorFloorPlanWidget::CropReferenceImageEdge(Dragged, Aspect, 0, FVector2D(0.0, 0.0));
	TestEqual(TEXT("dragged outwards past the picture the crop is none"), Dragged.CropLeft, 0.f);

	// Saved with the floor and back after a reload; one Undo step; a cropped picture is not rebuilt with the rooms.
	FNavigationSearchFixture F;
	F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
	F.Generator->CreateRectangleBuilding(TEXT("REC"), TEXT("REC"), {500,500}, 1000, 1000, 0, 2, 300);
	F.Interior->LoadInteriorForBuilding(TEXT("REC"));
	ANavPlayerController* PC = F.MakeController();
	PC->SetActiveFloor(1);
	if (!TestTrue(TEXT("a picture"), PC->ImportReferenceImage(WriteTinyPng(TEXT("ref_crop.png"))))) return false;
	FInteriorReferenceImage Start;
	PC->GetActiveReferenceImage(Start);
	FInteriorReferenceImage Cropped = Start;
	Cropped.CropLeft = 0.2f; Cropped.CropBottom = 0.3f;
	TestTrue(TEXT("cropped"), PC->SetActiveReferenceImage(Cropped));
	FInteriorReferenceImage Now;
	PC->GetActiveReferenceImage(Now);
	TestTrue(TEXT("the crop is kept"), Now == Cropped && Now.HasCrop());
	TSharedPtr<FJsonObject> Saved = F.Interior->CaptureInteriorSnapshot();
	F.Generator->SetInteriorDataForBuilding(TEXT("REC"), Saved);
	F.Interior->LoadInteriorForBuilding(TEXT("REC"));
	const FInteriorReferenceImage* Back = F.Interior->GetReferenceImage(1);
	TestTrue(TEXT("and comes back after a reload"), Back && *Back == Cropped);
	// A reload starts the history again: take the crop off and put it on once more, then one Undo takes it off.
	TestTrue(TEXT("uncropped"), PC->SetActiveReferenceImage(Start));
	TestTrue(TEXT("cropped again"), PC->SetActiveReferenceImage(Cropped));
	TestTrue(TEXT("Undo takes the crop off in one step"), PC->UndoInteriorEdit());
	PC->GetActiveReferenceImage(Now);
	TestTrue(TEXT("to the picture as it was"), Now == Start && !Now.HasCrop());
	return true;
}

#endif
