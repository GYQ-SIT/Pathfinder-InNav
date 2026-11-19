#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "InteriorScreenWidget.h"
#include "NavPlayerController.h"
#include "NavigationRoutePlanner.h"
#include "PolyBuilding.h"
#include "UiTestFixture.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

// PAT-205: an open floor has no outer wall; on the ground floor the whole outline is a way in.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOpenFloorTest, "InNavi.Interior.OpenFloor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOpenFloorTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F;
	F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
	APolyBuilding* Building = F.Generator->CreateRectangleBuilding(TEXT("OPN"), TEXT("OPN"), {500,500}, 1000, 1000, 0, 2, 300);
	if (!TestNotNull(TEXT("a building"), Building)) return false;
	F.Interior->LoadInteriorForBuilding(TEXT("OPN"));
	FString Error;
	F.Interior->CreateRoomFromFootprint({{400,400},{600,400},{600,600},{400,600}}, TEXT("Hall"), TEXT("Hall"), true, false, false, Error);
	const FBox2D Walls(Building->GetFootprint());
	const FVector2D Inside = Walls.Min + FVector2D(100, 100); // clear of the hall, which has no door
	const FVector2D Outside(Walls.Min.X - 20.0, Walls.GetCenter().Y);
	const auto PointOn = [](const FString& Id, int32 Floor, FVector2D Position)
	{
		FNavigationRoutePoint Point; Point.BuildingId = Id; Point.Floor = Floor; Point.Position = Position; return Point;
	};
	const auto CanRoute = [&]()
	{
		FNavigationRouteResult Route;
		return FNavigationRoutePlanner::FindCampusRoute(*F.Generator, *F.Interior, PointOn(TEXT("OPN"), 0, Outside), PointOn(TEXT("OPN"), 1, Inside), Route);
	};

	// A closed building with no main door cannot be entered.
	TestFalse(TEXT("floors are closed by default"), F.Interior->IsFloorOpen(1) || Building->IsLevelOpen(1));
	TestFalse(TEXT("no way in without a door"), CanRoute());

	// Open ground floor: the wall ring goes, and the outline is a way in.
	TestTrue(TEXT("the ground floor is made open"), F.Interior->SetFloorOpen(1, true));
	TestTrue(TEXT("it is open"), F.Interior->IsFloorOpen(1));
	TestTrue(TEXT("and the building shows it (no outer wall)"), Building->IsLevelOpen(1) && !Building->IsLevelOpen(2));
	TestFalse(TEXT("setting it again changes nothing"), F.Interior->SetFloorOpen(1, true));
	TestFalse(TEXT("a floor that is not there cannot be opened"), F.Interior->SetFloorOpen(9, true));
	TestTrue(TEXT("a route now enters from outside, anywhere along the outline"), CanRoute());

	// Saved with the floor (only when open), and what is saved opens the building again.
	TSharedPtr<FJsonObject> Saved = F.Interior->CaptureInteriorSnapshot();
	const TArray<TSharedPtr<FJsonValue>>* Floors = nullptr;
	if (!TestTrue(TEXT("the snapshot has floors"), Saved.IsValid() && Saved->TryGetArrayField(TEXT("floors"), Floors))) return false;
	bool bFirstOpen = false, bSecondHasField = false;
	for (const TSharedPtr<FJsonValue>& Value : *Floors)
	{
		const TSharedPtr<FJsonObject> Floor = Value->AsObject();
		double Level = 0; Floor->TryGetNumberField(TEXT("level"), Level);
		if (static_cast<int32>(Level) == 1) Floor->TryGetBoolField(TEXT("open"), bFirstOpen);
		else bSecondHasField = Floor->HasField(TEXT("open"));
	}
	TestTrue(TEXT("floor 1 is saved as open"), bFirstOpen);
	TestFalse(TEXT("a closed floor has no field, so older files read the same"), bSecondHasField);
	F.Interior->ClearLoadedInterior();
	Building->SetOpenLevels({});
	F.Generator->SetInteriorDataForBuilding(TEXT("OPN"), Saved);
	TestTrue(TEXT("stored data alone opens the building's floor, loaded in the editor or not"), Building->IsLevelOpen(1));
	F.Interior->LoadInteriorForBuilding(TEXT("OPN"));
	TestTrue(TEXT("and it comes back open when the interior is opened"), F.Interior->IsFloorOpen(1));

	// Undo and the Floor panel's switch.
	TestTrue(TEXT("closing it again"), F.Interior->SetFloorOpen(1, false));
	TestFalse(TEXT("the wall is back"), Building->IsLevelOpen(1));
	TestFalse(TEXT("and the way in is gone"), CanRoute());
	TestTrue(TEXT("Undo opens it again"), F.Interior->Undo() && F.Interior->IsFloorOpen(1) && Building->IsLevelOpen(1));

	ANavPlayerController* PC = F.MakeController();
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(PC);
	auto* Box = Cast<UCheckBox>(Screen->FindScreenWidget(TEXT("Chk_OpenFloor")));
	if (!TestNotNull(TEXT("the Floor panel has the switch"), Box)) return false;
	Box->SetIsChecked(true); Box->OnCheckStateChanged.Broadcast(true);
	TestTrue(TEXT("ticking it opens the floor being viewed"), F.Interior->IsFloorOpen(1) && PC->IsActiveFloorOpen());
	Box->SetIsChecked(false); Box->OnCheckStateChanged.Broadcast(false);
	TestFalse(TEXT("unticking closes it"), F.Interior->IsFloorOpen(1));
	return true;
}

// PAT-203: floors can have a name of their own, and the Floor panel has "+ Basement" and "+ Special" next to "+ Add".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorNamesTest, "InNavi.Interior.FloorNames", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFloorNamesTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture F;
	F.Interior->ClearLoadedInterior(); F.Generator->ClearAllBuildings();
	F.Generator->CreateRectangleBuilding(TEXT("NAM"), TEXT("NAM"), {500,500}, 1000, 1000, 0, 2, 300);
	F.Interior->LoadInteriorForBuilding(TEXT("NAM"));
	ANavPlayerController* PC = F.MakeController();

	// A name is a label next to the level: trimmed, cut at 40 characters, saved with the floor only when there is one, and one Undo step.
	TestTrue(TEXT("floor 2 is named"), F.Interior->SetFloorName(2, TEXT("  Mezzanine  ")));
	TestEqual(TEXT("trimmed"), F.Interior->GetFloorName(2), FString(TEXT("Mezzanine")));
	TestFalse(TEXT("the same name changes nothing"), F.Interior->SetFloorName(2, TEXT("Mezzanine")));
	TestFalse(TEXT("a floor that is not there has no name to set"), F.Interior->SetFloorName(7, TEXT("x")));
	TestTrue(TEXT("a very long name"), F.Interior->SetFloorName(1, FString::ChrN(60, TEXT('x'))));
	TestEqual(TEXT("is cut at 40"), F.Interior->GetFloorName(1).Len(), 40);
	TSharedPtr<FJsonObject> Saved = F.Interior->CaptureInteriorSnapshot();
	const TArray<TSharedPtr<FJsonValue>>* Floors = nullptr;
	if (!TestTrue(TEXT("floors saved"), Saved.IsValid() && Saved->TryGetArrayField(TEXT("floors"), Floors))) return false;
	FString SavedName;
	for (const TSharedPtr<FJsonValue>& Value : *Floors)
	{
		double Level = 0; Value->AsObject()->TryGetNumberField(TEXT("level"), Level);
		if (static_cast<int32>(Level) == 2) Value->AsObject()->TryGetStringField(TEXT("name"), SavedName);
	}
	TestEqual(TEXT("the name is in the file"), SavedName, FString(TEXT("Mezzanine")));
	F.Generator->SetInteriorDataForBuilding(TEXT("NAM"), Saved);
	F.Interior->LoadInteriorForBuilding(TEXT("NAM"));
	TestEqual(TEXT("and comes back"), F.Interior->GetFloorName(2), FString(TEXT("Mezzanine")));
	TestTrue(TEXT("Undo takes a name back"), F.Interior->SetFloorName(2, TEXT("Roof")) && F.Interior->Undo() && F.Interior->GetFloorName(2) == TEXT("Mezzanine"));

	// The Floor panel: the name box, "+ Basement" and "+ Special".
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>(); Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(PC);
	auto* NameBox = Cast<UEditableTextBox>(Screen->FindScreenWidget(TEXT("Edt_FloorName")));
	auto* Basement = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_AddBasement")));
	auto* Special = Cast<UButton>(Screen->FindScreenWidget(TEXT("Btn_AddSpecialFloor")));
	if (!TestNotNull(TEXT("name box"), NameBox) || !TestNotNull(TEXT("+ Basement"), Basement) || !TestNotNull(TEXT("+ Special"), Special)) return false;
	PC->SetActiveFloor(1);
	NameBox->SetText(FText::FromString(TEXT("Lobby level"))); NameBox->OnTextCommitted.Broadcast(NameBox->GetText(), ETextCommit::OnEnter);
	TestEqual(TEXT("the name box names the floor being viewed"), F.Interior->GetFloorName(1), FString(TEXT("Lobby level")));

	Basement->OnClicked.Broadcast();
	TestEqual(TEXT("+ Basement adds B1 under the ground floor"), F.Interior->GetFloorLevels(), TArray<int32>({ -1, 1, 2 }));
	Basement->OnClicked.Broadcast();
	TestEqual(TEXT("and then B2"), F.Interior->GetFloorLevels(), TArray<int32>({ -2, -1, 1, 2 }));
	Special->OnClicked.Broadcast();
	TestEqual(TEXT("+ Special adds a floor on top"), F.Interior->GetFloorLevels().Last(), 3);
	TestEqual(TEXT("named so it can be changed"), F.Interior->GetFloorName(3), FString(TEXT("Special floor")));
	TestTrue(TEXT("the floors above keep their names"), F.Interior->GetFloorName(2) == TEXT("Mezzanine") && F.Interior->GetFloorName(1) == TEXT("Lobby level"));
	return true;
}

#endif
