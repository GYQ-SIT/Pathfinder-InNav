#if WITH_DEV_AUTOMATION_TESTS

#include "InteriorRoom.h"
#include "InteriorScreenWidget.h"
#include "PolyBuilding.h"
#include "UiTestFixture.h"

#include "Components/TextBlock.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

// Doors in the building's outer wall: a main door (ground floor, away from every neighbour) and link doors (any floor, wholly where
// two buildings' outer walls are next to each other, shown on both). The example project's E2 has E3's wall about a metre from its east
// end, E6's wall about a metre from part of its south side, and nothing next to its west side.

namespace EntranceTests
{
	const FVector2D WestWall(95, 104);
	const FVector2D EastWall(587, 103);
	const FVector2D SouthWallBesideE6(480, 162.4);
	const FVector2D SouthWallAwayFromE6(300, 162.0);
	const FVector2D SouthWallHalfBesideE6(445, 163.0);

	// Where an opening's middle is, in the building's own footprint space.
	FVector2D Middle(const APolyBuilding& Building, const FInteriorEntrance& Entrance, float Scale)
	{
		const TArray<FVector2D>& Outline = Building.GetFootprint();
		const FVector2D A = Outline[Entrance.EdgeIndex];
		const FVector2D B = Outline[(Entrance.EdgeIndex + 1) % Outline.Num()];
		return A + (B - A).GetSafeNormal() * ((Entrance.Offset + Entrance.Width * 0.5f) / Scale);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEntranceRulesTest, "InNavi.Interior.Entrance.Rules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEntranceRulesTest::RunTest(const FString& Parameters)
{
	using namespace EntranceTests;
	FNavigationSearchFixture Fixture;
	AInteriorGenerator* Interior = Fixture.Interior;
	FString Error;
	const float Scale = Fixture.Generator->GetScale();
	APolyBuilding* E2 = Fixture.Generator->FindBuildingById(TEXT("E2"));
	if (!TestNotNull(TEXT("E2 exists"), E2)) return false;

	// The way out: ground floor, nothing next door.
	Interior->SetActiveFloor(1);
	const FString Main = Interior->AddEntranceAtPoint(WestWall, 15.f, Error);
	TestFalse(TEXT("a main door can be added away from every neighbour"), Main.IsEmpty());
	if (const FInteriorEntrance* Door = Interior->FindEntranceById(Main))
	{
		TestTrue(TEXT("and it is a main door"), Door->Kind == EInteriorEntranceKind::Main);
		TestEqual(TEXT("on the ground floor"), Door->Level, 1);
		TestTrue(TEXT("leading nowhere in particular"), Door->ConnectsTo.IsEmpty());
	}
	TestTrue(TEXT("not on top of another door"), Interior->AddEntranceAtPoint(WestWall, 15.f, Error).IsEmpty() && Error.Contains(TEXT("already")));
	TestTrue(TEXT("not away from the wall"), Interior->AddEntranceAtPoint(FVector2D(300, 100), 15.f, Error).IsEmpty());
	Interior->SetActiveFloor(3);
	TestTrue(TEXT("a main door upstairs is refused"), Interior->AddEntranceAtPoint(WestWall, 15.f, Error).IsEmpty() && Error.Contains(TEXT("ground floor")));
	Interior->SetActiveFloor(1);
	TestTrue(TEXT("the south wall away from E6 takes a main door too"), !Interior->AddEntranceAtPoint(SouthWallAwayFromE6, 15.f, Error).IsEmpty());

	// Where the wall is next to a neighbour's: only a link door, on any floor.
	const FString OnGround = Interior->AddEntranceAtPoint(EastWall, 15.f, Error);
	TestFalse(TEXT("next to E3 a door can be added on the ground floor"), OnGround.IsEmpty());
	if (const FInteriorEntrance* Door = Interior->FindEntranceById(OnGround))
	{
		TestTrue(TEXT("but it is a link door, never a main door"), Door->Kind == EInteriorEntranceKind::Link && Door->ConnectsTo == TEXT("E3"));
	}
	Interior->SetActiveFloor(3);
	const FString Upstairs = Interior->AddEntranceAtPoint(EastWall, 15.f, Error);
	TestFalse(TEXT("the same wall takes a link door on another floor"), Upstairs.IsEmpty());
	if (const FInteriorEntrance* Door = Interior->FindEntranceById(Upstairs))
	{
		TestEqual(TEXT("on that floor"), Door->Level, 3);
		TestEqual(TEXT("to E3"), Door->ConnectsTo, FString(TEXT("E3")));
	}
	const FString Half = Interior->AddEntranceAtPoint(SouthWallHalfBesideE6, 15.f, Error);
	TestTrue(*FString::Printf(TEXT("a door half beside a neighbour and half away is refused (made: %s, error: %s)"), *Half, *Error), Half.IsEmpty() && Error.Contains(TEXT("wholly")));
	Interior->SetActiveFloor(2);
	const FString ToE6 = Interior->AddEntranceAtPoint(SouthWallBesideE6, 15.f, Error);
	TestFalse(TEXT("another neighbour's wall gets its own link door"), ToE6.IsEmpty());
	if (const FInteriorEntrance* Door = Interior->FindEntranceById(ToE6))
	{
		TestEqual(TEXT("that leads to E6"), Door->ConnectsTo, FString(TEXT("E6")));
	}

	// The neighbour shows the same door at the matching spot, on the same floor.
	const TArray<FInteriorEntrance> OnE3 = Fixture.Generator->GetMirroredEntrances(TEXT("E3"));
	TestEqual(TEXT("E3 shows E2's two link doors to it"), OnE3.Num(), 2);
	APolyBuilding* E3 = Fixture.Generator->FindBuildingById(TEXT("E3"));
	for (const FInteriorEntrance& Mirror : OnE3)
	{
		TestTrue(TEXT("each is marked as the other building's"), Mirror.bMirrored && Mirror.ConnectsTo == TEXT("E2") && Mirror.SourceBuildingId == TEXT("E2"));
		const FInteriorEntrance* Original = Interior->FindEntranceById(Mirror.Id);
		if (TestNotNull(TEXT("with the same id as E2's door"), Original) && E3)
		{
			TestEqual(TEXT("on the same floor"), Mirror.Level, Original->Level);
			// E3's copy sits right opposite: its middle is close to E2's middle (shared footprint space).
			const FVector2D Here = Middle(*E2, *Original, Scale) + E2->BuildingOffset;
			const FVector2D There = Middle(*E3, Mirror, Scale) + E3->BuildingOffset;
			TestTrue(TEXT("and opposite it"), FVector2D::Distance(Here, There) < 40.0);
		}
	}
	TestEqual(TEXT("E6 shows the one to it"), Fixture.Generator->GetMirroredEntrances(TEXT("E6")).Num(), 1);
	TestEqual(TEXT("E1 shows none"), Fixture.Generator->GetMirroredEntrances(TEXT("E1")).Num(), 0);

	// A door in the outer wall is a gap in it, on its own floor, on both buildings.
	auto CheckGap = [&](APolyBuilding& Building, const FInteriorEntrance& Door, const TCHAR* What)
	{
		const FVector2D Centre = (Middle(Building, Door, Scale) + Building.BuildingOffset) * Scale;
		const FVector2D EdgeStart = (Building.GetFootprint()[Door.EdgeIndex] + Building.BuildingOffset) * Scale;
		const FVector2D Direction = (Building.GetFootprint()[(Door.EdgeIndex + 1) % Building.GetFootprint().Num()] - Building.GetFootprint()[Door.EdgeIndex]).GetSafeNormal();
		const FVector2D Beside = EdgeStart + Direction * Door.Offset;
		const float Base = (Door.Level - 1) * Building.GetFloorHeight();
		bool bWallInGap = false, bWallBeside = false;
		for (const FVector& Vertex : Building.GetWallVerticesForTests(Door.Level))
		{
			if (Vertex.Z < Base + 200.f && FVector2D::Distance(FVector2D(Vertex.X, Vertex.Y), Centre) < Door.Width * 0.5f - 15.f) bWallInGap = true;
			if (Vertex.Z < Base + 200.f && FVector2D::Distance(FVector2D(Vertex.X, Vertex.Y), Beside) < 25.f) bWallBeside = true;
		}
		TestFalse(*FString::Printf(TEXT("%s: the wall has a gap where the door is"), What), bWallInGap);
		TestTrue(*FString::Printf(TEXT("%s: and wall beside it"), What), bWallBeside);
	};
	for (const FInteriorEntrance& Door : Interior->GetEntrances())
	{
		CheckGap(*E2, Door, TEXT("E2"));
	}
	if (E3)
	{
		for (const FInteriorEntrance& Mirror : OnE3)
		{
			CheckGap(*E3, Mirror, TEXT("E3 (mirrored)"));
		}
	}

	// A door with no matching spot on the neighbour is refused; deleting one door takes its copy away too.
	TestTrue(TEXT("a link door can be deleted"), Interior->RemoveEntrance(Upstairs));
	TestEqual(TEXT("and its copy on E3 goes"), Fixture.Generator->GetMirroredEntrances(TEXT("E3")).Num(), 1);
	TestTrue(TEXT("it can be undone"), Interior->Undo());
	TestEqual(TEXT("which brings the copy back"), Fixture.Generator->GetMirroredEntrances(TEXT("E3")).Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEntranceBothSidesTest, "InNavi.Interior.Entrance.BothSides", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEntranceBothSidesTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	FString Error;

	// A link door made on E2 is there when E3 is opened, can be deleted from E3's side, and survives a save and a load.
	Fixture.Interior->SetActiveFloor(2);
	const FString Id = Fixture.Interior->AddEntranceAtPoint(EntranceTests::EastWall, 15.f, Error);
	TestFalse(TEXT("a link door is added on E2"), Id.IsEmpty());
	const FString SavePath = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("Entrances-") + FGuid::NewGuid().ToString() + TEXT(".json"));
	Fixture.Interior->SyncInteriorToBuildingGenerator();
	TestTrue(TEXT("the project saves"), Fixture.Generator->SaveBuildingsToFile(SavePath));
	Fixture.Interior->ClearLoadedInterior();
	Fixture.Generator->ClearAllBuildings();
	TestTrue(TEXT("and loads"), Fixture.Generator->LoadBuildingsFromFile(SavePath) > 0);

	Fixture.Interior->LoadInteriorForBuilding(TEXT("E3"));
	TestEqual(TEXT("opening E3 shows the door from E2"), Fixture.Interior->GetAllEntrances().Num(), 1);
	TestEqual(TEXT("E3 owns none itself"), Fixture.Interior->GetEntrances().Num(), 0);
	if (const FInteriorEntrance* Shown = Fixture.Interior->FindEntranceById(Id))
	{
		TestTrue(TEXT("it is shown as a link door to E2"), Shown->bMirrored && Shown->Kind == EInteriorEntranceKind::Link && Shown->ConnectsTo == TEXT("E2"));
		TestEqual(TEXT("on the same floor"), Shown->Level, 2);
	}
	TestTrue(TEXT("deleting it from E3's side"), Fixture.Interior->RemoveEntrance(Id));
	TestTrue(TEXT("the owner's stored copy is removed"), Fixture.Generator->GetStoredEntrances(TEXT("E2")).IsEmpty());
	TestTrue(TEXT("undo can restore an edit made from the mirrored side"), Fixture.Interior->Undo());
	TestNotNull(TEXT("undo restores the mirror"), Fixture.Interior->FindEntranceById(Id));
	TestEqual(TEXT("undo restores the owner's copy"), Fixture.Generator->GetStoredEntrances(TEXT("E2")).Num(), 1);
	TestTrue(TEXT("redo can reapply the deletion"), Fixture.Interior->Redo());
	TestNull(TEXT("redo removes the mirror"), Fixture.Interior->FindEntranceById(Id));
	Fixture.Interior->ClearLoadedInterior();
	Fixture.Generator->RestoreBuildingBaselines();
	Fixture.Interior->LoadInteriorForBuilding(TEXT("E3"));
	TestNotNull(TEXT("discard restores the mirror"), Fixture.Interior->FindEntranceById(Id));
	TestTrue(TEXT("the restored entrance can be removed again"), Fixture.Interior->RemoveEntrance(Id));
	Fixture.Interior->LoadInteriorForBuilding(TEXT("E2"));
	TestTrue(TEXT("removes it from E2 as well"), Fixture.Interior->GetEntrances().IsEmpty());
	IFileManager::Get().Delete(*SavePath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEntrancePropertiesTest, "InNavi.Interior.Entrance.Properties", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEntrancePropertiesTest::RunTest(const FString& Parameters)
{
	FNavigationSearchFixture Fixture;
	ANavPlayerController* Controller = Fixture.MakeController();
	UInteriorScreenWidget* Screen = NewObject<UInteriorScreenWidget>();
	Screen->Initialize(); Screen->TakeWidget(); Screen->SetOwningPlayer(Controller);
	FScriptDelegate Selection;
	Selection.BindUFunction(Screen, TEXT("HandleSelectedEntranceChanged"));
	Controller->OnSelectedEntranceChanged.AddUnique(Selection);
	UTextBlock* Title = Cast<UTextBlock>(Screen->FindScreenWidget(TEXT("Txt_PropertiesTitle")));
	UTextBlock* Text = Cast<UTextBlock>(Screen->FindScreenWidget(TEXT("Txt_DoorRoom")));
	if (!TestNotNull(TEXT("the panel has a title"), Title) || !TestNotNull(TEXT("and a line for the door"), Text)) return false;

	FString Error;
	Fixture.Interior->SetActiveFloor(1);
	const FString Main = Fixture.Interior->AddEntranceAtPoint(EntranceTests::WestWall, 15.f, Error);
	Controller->SelectEntrance(Main);
	TestEqual(TEXT("a main door says so"), Title->GetText().ToString(), FString(TEXT("Main door")));
	TestTrue(TEXT("and that it leads outside"), Text->GetText().ToString().Contains(TEXT("outside")));

	Fixture.Interior->SetActiveFloor(3);
	const FString Link = Fixture.Interior->AddEntranceAtPoint(EntranceTests::EastWall, 15.f, Error);
	Controller->SelectEntrance(Link);
	TestEqual(TEXT("a link door says so"), Title->GetText().ToString(), FString(TEXT("Link door")));
	TestTrue(TEXT("and which building it leads to, and on which floor"), Text->GetText().ToString().Contains(Controller->GetBuildingDisplayName(TEXT("E3"))) && Text->GetText().ToString().Contains(TEXT("Floor 3")));

	// The building is shown by its name, so renaming it shows there; the id is only a fallback.
	Fixture.Generator->FindBuildingById(TEXT("E3"))->BuildingName = TEXT("Auditorium North");
	Controller->SelectEntrance(FString());
	Controller->SelectEntrance(Link);
	TestTrue(TEXT("a renamed building shows its new name"), Text->GetText().ToString().Contains(TEXT("Auditorium North")) && !Text->GetText().ToString().Contains(TEXT("E3")));

	// Delete removes the selected door, like any other selection.
	TestTrue(TEXT("Delete removes the selected door"), Controller->DeleteSelection());
	TestTrue(TEXT("and it is gone"), Fixture.Interior->FindEntranceById(Link) == nullptr);
	Controller->OnSelectedEntranceChanged.Remove(Selection);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
