#if WITH_DEV_AUTOMATION_TESTS

#include "GroundFit.h"
#include "UiTestFixture.h"
#include "PolyBuilding.h"
#include "RoadSegmentActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

// The ground under the campus follows what the Map panel's filters show.

namespace GroundTests
{
	FBox2D Box(double X0, double Y0, double X1, double Y1) { return FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)); }

	bool Contains(const FBox2D& Outer, const FBox2D& Inner)
	{
		return Outer.Min.X <= Inner.Min.X && Outer.Min.Y <= Inner.Min.Y && Outer.Max.X >= Inner.Max.X && Outer.Max.Y >= Inner.Max.Y;
	}

	FBox2D Flat(const FBox& Box) { return FBox2D(FVector2D(Box.Min.X, Box.Min.Y), FVector2D(Box.Max.X, Box.Max.Y)); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundFitTest, "InNavi.Ground.Fit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGroundFitTest::RunTest(const FString& Parameters)
{
	using namespace GroundTests;
	FBox2D Rect;
	TestFalse(TEXT("nothing shown, no ground to fit"), GroundFit::Fit({}, 4000.0, 0.05, 1000.0, Rect));
	TestFalse(TEXT("an empty box counts for nothing"), GroundFit::Fit({ FBox2D(ForceInit) }, 4000.0, 0.05, 1000.0, Rect));

	// Everything shown is covered, with a margin: 5 percent of the longer side when that is more than the least.
	TestTrue(TEXT("a campus is fitted"), GroundFit::Fit({ Box(0, 0, 100000, 40000), Box(-20000, 10000, 5000, 60000) }, 4000.0, 0.05, 1000.0, Rect));
	TestTrue(TEXT("the union, grown by 5 percent of its longer side (6000)"), Rect.Min.Equals(FVector2D(-26000, -6000)) && Rect.Max.Equals(FVector2D(106000, 66000)));

	// A small area gets the least margin, and then at least the least size, evenly about its middle.
	TestTrue(TEXT("a small building is fitted"), GroundFit::Fit({ Box(1000, 1000, 1500, 1400) }, 4000.0, 0.05, 1000.0, Rect));
	TestTrue(TEXT("at least 4000 each way"), Rect.GetSize().X >= 4000.0 - 1e-6 && Rect.GetSize().Y >= 4000.0 - 1e-6);
	TestTrue(TEXT("around the building's middle"), Rect.GetCenter().Equals(FVector2D(1250, 1200), 1e-6));
	TestTrue(TEXT("and it contains the building with the margin"), Contains(Rect, Box(0, 0, 2500, 2400)));
	return true;
}

// A road knows the ground it covers and whether the filters show it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundRoadTest, "InNavi.Ground.RoadBoundsAndFilters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGroundRoadTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ARoadSegmentActor* Road = World->SpawnActor<ARoadSegmentActor>();
	Road->BuildRoad({ FVector2D(0, 0), FVector2D(1000, 0), FVector2D(1000, 500) }, 400.f, 0.f, FLinearColor::Gray);
	const FBox2D Bounds = Road->GetBounds2D();
	TestTrue(TEXT("the box is the points, grown by half the width"), Bounds.Min.Equals(FVector2D(-200, -200)) && Bounds.Max.Equals(FVector2D(1200, 700)));
	TestTrue(TEXT("shown at first"), Road->IsShownByFilters());
	Road->SetCategoryVisible(false);
	TestFalse(TEXT("its category switched off"), Road->IsShownByFilters());
	Road->SetCategoryVisible(true);
	Road->SetWithinCampusRange(false);
	TestFalse(TEXT("outside the campus range"), Road->IsShownByFilters());
	Road->SetWithinCampusRange(true);
	TestTrue(TEXT("and back"), Road->IsShownByFilters());
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundFollowsFiltersTest, "InNavi.Ground.FollowsFilters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGroundFollowsFiltersTest::RunTest(const FString& Parameters)
{
	using namespace GroundTests;
	FNavigationSearchFixture Fixture;
	Fixture.Interior->ClearLoadedInterior();
	Fixture.Generator->ClearAllBuildings();
	// Buildings A and B stand 1 m apart and C is far from both (docs/test-data).
	if (!TestEqual(TEXT("the test project loads"), Fixture.Generator->LoadBuildingsFromFile(FPaths::ProjectDir() / TEXT("../docs/test-data/entrance-test-2-with-doors.json")), 3)) return false;
	APolyBuilding* A = Fixture.Generator->FindBuildingById(TEXT("A"));
	APolyBuilding* C = Fixture.Generator->FindBuildingById(TEXT("C"));
	if (!TestTrue(TEXT("buildings A and C exist"), A && C)) return false;
	const FBox2D BoxA = Flat(A->GetComponentsBoundingBox(true)), BoxC = Flat(C->GetComponentsBoundingBox(true));

	// The level's floor: the template map's floor mesh on a small floor.
	UStaticMesh* FloorMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/MapTemplates/SM_Template_Map_Floor.SM_Template_Map_Floor"));
	if (!TestNotNull(TEXT("the template floor mesh is there"), FloorMesh)) return false;
	AStaticMeshActor* Floor = Fixture.World->SpawnActor<AStaticMeshActor>();
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Floor->GetStaticMeshComponent()->SetStaticMesh(FloorMesh);
	Floor->SetActorScale3D(FVector(2, 2, 1));
	auto Ground = [Floor]() { Floor->GetStaticMeshComponent()->UpdateBounds(); return Flat(Floor->GetStaticMeshComponent()->Bounds.GetBox()); };

	// Everything shown stands on the ground.
	Fixture.Generator->UpdateGroundToVisible();
	const FBox2D All = Ground();
	TestTrue(TEXT("the ground covers every building"), Contains(All, BoxA) && Contains(All, BoxC));
	TestTrue(TEXT("with a margin of at least 10 m beyond the furthest"), All.Min.X <= BoxA.Min.X - 1000.0 + 1.0 && All.Max.X >= BoxC.Max.X + 1000.0 - 1.0);

	// Showing only C: the ground is C's and no more, so it shrinks and A is no longer on it.
	Fixture.Generator->SetBuildingFilter(TEXT("Building C"), 0, 0);
	Fixture.Generator->UpdateGroundToVisible();
	const FBox2D OnlyC = Ground();
	TestTrue(TEXT("the ground covers C"), Contains(OnlyC, BoxC));
	TestFalse(TEXT("and no longer A, which is not shown"), Contains(OnlyC, BoxA));
	TestTrue(TEXT("so it is smaller"), OnlyC.GetSize().X < All.GetSize().X);

	// Showing all again grows it back.
	Fixture.Generator->SetBuildingFilter(FString(), 0, 0);
	Fixture.Generator->UpdateGroundToVisible();
	const FBox2D Again = Ground();
	TestTrue(TEXT("all shown again, the ground covers both"), Contains(Again, BoxA) && Contains(Again, BoxC));
	TestTrue(TEXT("as before"), Again.Min.Equals(All.Min, 1.0) && Again.Max.Equals(All.Max, 1.0));

	// Nothing shown: nothing to stand on, so the ground is left as it is.
	Fixture.Generator->SetCampusBuildingsVisible(false);
	Fixture.Generator->UpdateGroundToVisible();
	TestTrue(TEXT("with nothing shown the ground stays"), Ground().Min.Equals(Again.Min, 1.0) && Ground().Max.Equals(Again.Max, 1.0));
	Fixture.Generator->SetCampusBuildingsVisible(true);

	// The floor stays at its height and does not cast shadows (a very large one overloads the shadow maps).
	TestFalse(TEXT("the floor casts no shadow"), Floor->GetStaticMeshComponent()->CastShadow);
	return true;
}

#endif
