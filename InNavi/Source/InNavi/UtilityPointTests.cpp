#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorScreenWidget.h"
#include "NavPlayerController.h"
#include "PolyBuilding.h"
#include "UiTestFixture.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

// PAT-193: utility points - an AED, a bicycle rack, a drop-off point - placed on a floor, moved, renamed, retyped, deleted, saved with the floor.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUtilityPointsTest, "InNavi.Interior.UtilityPoints", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUtilityPointsTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F;
	F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
	APolyBuilding* Building = F.Generator->CreateRectangleBuilding(TEXT("UTL"), TEXT("UTL"), {500,500}, 1000, 1000, 0, 2, 300);
	if (!TestNotNull(TEXT("a building"), Building)) return false;
	F.Interior->LoadInteriorForBuilding(TEXT("UTL"));
	const FBox2D Walls(Building->GetFootprint());
	const FVector2D Inside = Walls.GetCenter();
	const FVector2D Outside(Walls.Min.X - 200.0, Walls.GetCenter().Y);

	// Placing: inside the building, on a floor that exists, with a known type; an unknown type is "other".
	FString Why;
	const FString Aed = F.Interior->AddUtility(1, InteriorUtilityTypes::Aed, TEXT("  AED by the lifts  "), Inside, &Why);
	TestFalse(TEXT("an AED is placed"), Aed.IsEmpty());
	const FInteriorUtility* Placed = F.Interior->FindUtility(Aed);
	if (!TestNotNull(TEXT("and can be found"), Placed)) return false;
	TestEqual(TEXT("with a trimmed name"), Placed->Name, FString(TEXT("AED by the lifts")));
	TestTrue(TEXT("on the floor asked for"), Placed->Level == 1 && Placed->Position.Equals(Inside));
	TestTrue(TEXT("outside the building is refused, with a reason"), F.Interior->AddUtility(1, InteriorUtilityTypes::Aed, TEXT("x"), Outside, &Why).IsEmpty() && !Why.IsEmpty());
	TestTrue(TEXT("a floor that is not there is refused"), F.Interior->AddUtility(9, InteriorUtilityTypes::Aed, TEXT("x"), Inside).IsEmpty());
	const FString Rack = F.Interior->AddUtility(2, TEXT("something new"), FString(), Inside + FVector2D(50, 0));
	TestEqual(TEXT("an unknown type reads as other"), F.Interior->FindUtility(Rack) ? F.Interior->FindUtility(Rack)->Type : FString(), InteriorUtilityTypes::Other);
	TestEqual(TEXT("two points, told apart"), F.Interior->GetUtilities().Num(), 2);
	TestNotEqual(TEXT("by id"), Aed, Rack);

	// Details: name and type together, one Undo step; nothing changed changes nothing.
	TestTrue(TEXT("renamed and retyped"), F.Interior->SetUtilityDetails(Rack, TEXT("Bike parking"), InteriorUtilityTypes::Bicycle));
	TestTrue(TEXT("shown that way"), F.Interior->FindUtility(Rack)->Name == TEXT("Bike parking") && F.Interior->FindUtility(Rack)->Type == InteriorUtilityTypes::Bicycle);
	TestFalse(TEXT("the same again changes nothing"), F.Interior->SetUtilityDetails(Rack, TEXT("Bike parking"), InteriorUtilityTypes::Bicycle));
	TestTrue(TEXT("a very long name is cut"), F.Interior->SetUtilityDetails(Rack, FString::ChrN(100, TEXT('x')), InteriorUtilityTypes::Bicycle) && F.Interior->FindUtility(Rack)->Name.Len() == InteriorUtilityTypes::MaxNameLength);
	TestTrue(TEXT("Undo takes the change back"), F.Interior->Undo() && F.Interior->FindUtility(Rack)->Name == TEXT("Bike parking"));

	// Moving: inside only; no history of its own (a drag records one step when it ends).
	TestTrue(TEXT("moved"), F.Interior->MoveUtility(Aed, Inside + FVector2D(100, 100)));
	TestFalse(TEXT("moving out of the building is refused"), F.Interior->MoveUtility(Aed, Outside));
	TestTrue(TEXT("and it stayed"), F.Interior->FindUtility(Aed)->Position.Equals(Inside + FVector2D(100, 100)));
	F.Interior->RecordHistorySnapshot();

	// Saved with the floor, only on floors that have some, and read back the same.
	TSharedPtr<FJsonObject> Saved = F.Interior->CaptureInteriorSnapshot();
	const TArray<TSharedPtr<FJsonValue>>* Floors = nullptr;
	if (!TestTrue(TEXT("floors saved"), Saved.IsValid() && Saved->TryGetArrayField(TEXT("floors"), Floors))) return false;
	int32 SavedOnFirst = 0, SavedOnSecond = 0;
	for (const TSharedPtr<FJsonValue>& Value : *Floors)
	{
		double Level = 0; Value->AsObject()->TryGetNumberField(TEXT("level"), Level);
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		const int32 Count = Value->AsObject()->TryGetArrayField(TEXT("utilities"), List) ? List->Num() : 0;
		(static_cast<int32>(Level) == 1 ? SavedOnFirst : SavedOnSecond) = Count;
	}
	TestTrue(TEXT("one point on each floor in the file"), SavedOnFirst == 1 && SavedOnSecond == 1);
	F.Generator->SetInteriorDataForBuilding(TEXT("UTL"), Saved);
	F.Interior->LoadInteriorForBuilding(TEXT("UTL"));
	TestEqual(TEXT("both come back"), F.Interior->GetUtilities().Num(), 2);
	const FInteriorUtility* Back = F.Interior->FindUtility(Aed);
	TestTrue(TEXT("with their type, name and place"), Back && Back->Type == InteriorUtilityTypes::Aed && Back->Name == TEXT("AED by the lifts") && Back->Level == 1 && Back->Position.Equals(Inside + FVector2D(100, 100)));

	// Delete, and Undo brings it back; taking a floor away takes its points.
	TestTrue(TEXT("deleted"), F.Interior->RemoveUtility(Aed) && !F.Interior->FindUtility(Aed));
	TestFalse(TEXT("deleting again does nothing"), F.Interior->RemoveUtility(Aed));
	TestTrue(TEXT("Undo brings it back"), F.Interior->Undo() && F.Interior->FindUtility(Aed));
	TestTrue(TEXT("a floor taken away takes its points"), F.Interior->SetLevelRange(1, 1) && F.Interior->GetUtilities().Num() == 1 && !F.Interior->FindUtility(Rack));
	return true;
}

// The controller and the Properties panel: arming a type, placing with a click, selecting (exclusive with the other selections), editing, deleting.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUtilityPointsUiTest, "InNavi.EditorUX.UtilityPoints", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUtilityPointsUiTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F;
	F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
	APolyBuilding* Building = F.Generator->CreateRectangleBuilding(TEXT("UTU"), TEXT("UTU"), {500,500}, 1000, 1000, 0, 2, 300);
	if (!TestNotNull(TEXT("a building"), Building)) return false;
	F.Interior->LoadInteriorForBuilding(TEXT("UTU"));
	FString Error;
	F.Interior->CreateRoomFromFootprint({{100,100},{200,100},{200,200},{100,200}}, TEXT("Lab"), TEXT("Lab"), true, false, false, Error);
	ANavPlayerController* PC = F.MakeController();
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(PC);
	const FVector2D Inside = FBox2D(Building->GetFootprint()).GetCenter();
	PC->SetActiveFloor(1);

	auto* AedButton = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_UtilityAed")));
	auto* OtherButton = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_UtilityOther")));
	auto* NameBox = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Txt_UtilityName")));
	auto* TypeBox = Cast<UComboBoxString>(Screen->FindScreenWidget(TEXT("Cmb_UtilityType")));
	auto* Save = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_ApplyUtility")));
	if (!TestNotNull(TEXT("the toolbar has an AED button"), AedButton) || !TestNotNull(TEXT("and an Other button"), OtherButton)
		|| !TestNotNull(TEXT("the Properties panel has a name box"), NameBox) || !TestNotNull(TEXT("a type box"), TypeBox) || !TestNotNull(TEXT("and Save changes"), Save)) return false;

	// Arming: one type at a time, the same button again turns it off, and another mode takes over.
	AedButton->OnClicked.Broadcast();
	TestTrue(TEXT("the AED button arms placing AEDs"), PC->IsInUtilityMode() && PC->GetActiveUtilityType() == InteriorUtilityTypes::Aed);
	OtherButton->OnClicked.Broadcast();
	TestEqual(TEXT("another type replaces it"), PC->GetActiveUtilityType(), InteriorUtilityTypes::Other);
	OtherButton->OnClicked.Broadcast();
	TestFalse(TEXT("and the same button again turns placing off"), PC->IsInUtilityMode());
	AedButton->OnClicked.Broadcast();
	PC->ToggleSelectMode();
	TestFalse(TEXT("select mode ends placing"), PC->IsInUtilityMode());
	AedButton->OnClicked.Broadcast();
	PC->ToggleElevatorMode();
	TestFalse(TEXT("a connector mode ends placing"), PC->IsInUtilityMode());
	AedButton->OnClicked.Broadcast();
	TestFalse(TEXT("placing and a connector mode are never both on"), PC->IsInConnectorMode());

	// Placing with a click: on the floor being viewed; a click outside is refused with a message.
	const FString Id = PC->PlaceUtilityAt(Inside);
	TestFalse(TEXT("a point is placed"), Id.IsEmpty());
	TestTrue(TEXT("on the floor being viewed"), F.Interior->FindUtility(Id) && F.Interior->FindUtility(Id)->Level == 1);
	TestTrue(TEXT("outside the building gives nothing"), PC->PlaceUtilityAt(FVector2D(-5000, -5000)).IsEmpty());
	PC->ExitUtilityMode();

	// Selecting is exclusive with the room / connector selections, and shows in the panel.
	PC->SelectRoom(TEXT("Lab"));
	PC->SelectUtility(Id);
	TestTrue(TEXT("the point is selected and the room is not"), PC->GetSelectedUtilityId() == Id && PC->GetSelectedRoomId().IsEmpty());
	TestEqual(TEXT("the name box shows its name"), NameBox->GetText().ToString(), FString());
	PC->SelectRoom(TEXT("Lab"));
	TestTrue(TEXT("selecting a room deselects the point"), PC->GetSelectedUtilityId().IsEmpty() && PC->GetSelectedRoomId() == TEXT("Lab"));
	PC->SelectUtility(Id);

	// Save changes: name and type from the panel.
	NameBox->SetText(FText::FromString(TEXT("Defibrillator, ground floor")));
	TypeBox->SetSelectedOption(InteriorUtilityTypes::Label(InteriorUtilityTypes::DropOff));
	Save->OnClicked.Broadcast();
	TestTrue(TEXT("the panel's name and type are saved"), F.Interior->FindUtility(Id)->Name == TEXT("Defibrillator, ground floor") && F.Interior->FindUtility(Id)->Type == InteriorUtilityTypes::DropOff);

	// Moving the selected one, and the Delete key / button path.
	TestTrue(TEXT("moves"), PC->MoveSelectedUtilityTo(Inside + FVector2D(60, 0)));
	TestFalse(TEXT("but not out of the building"), PC->MoveSelectedUtilityTo(FVector2D(-5000, 0)));
	TestTrue(TEXT("Delete removes the selected point"), PC->DeleteSelection() && !F.Interior->FindUtility(Id) && PC->GetSelectedUtilityId().IsEmpty());
	TestTrue(TEXT("Undo brings it back, deselected"), PC->UndoInteriorEdit() && F.Interior->FindUtility(Id) && PC->GetSelectedUtilityId().IsEmpty());
	return true;
}

#endif
