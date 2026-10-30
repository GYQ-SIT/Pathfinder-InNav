#if WITH_DEV_AUTOMATION_TESTS

#include "MobileMapLabels.h"
#include "MobileNavSource.h"
#include "MobileRealSource.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

// Which labels the map draws: the important ones first, none over another, none over something that has to stay clear.

namespace MobileMapLabelTests
{
	MobileMapLabels::FCandidate Label(double X, double Y, double W, double H, double Priority)
	{
		MobileMapLabels::FCandidate Candidate;
		Candidate.Box = FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
		Candidate.Priority = Priority;
		return Candidate;
	}
	const FBox2D Screen(FVector2D(0, 0), FVector2D(400, 400));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapLabelChoiceTest, "InNavi.Mobile.MapLabels.Choice", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapLabelChoiceTest::RunTest(const FString& Parameters)
{
	using namespace MobileMapLabelTests;
	// Two that overlap: the more important one is drawn, the other is left out, whichever is listed first.
	{
		const TArray<bool> A = MobileMapLabels::Choose({ Label(10, 10, 60, 14, 1.0), Label(40, 12, 60, 14, 5.0) }, {}, Screen);
		TestTrue(TEXT("the more important label is drawn"), !A[0] && A[1]);
		const TArray<bool> B = MobileMapLabels::Choose({ Label(40, 12, 60, 14, 5.0), Label(10, 10, 60, 14, 1.0) }, {}, Screen);
		TestTrue(TEXT("in either order"), B[0] && !B[1]);
	}
	// Equal importance: the one listed first, so the picture does not change from one frame to the next.
	{
		const TArray<bool> Chosen = MobileMapLabels::Choose({ Label(10, 10, 60, 14, 2.0), Label(30, 10, 60, 14, 2.0) }, {}, Screen);
		TestTrue(TEXT("a tie goes to the first"), Chosen[0] && !Chosen[1]);
	}
	// Labels that do not meet are all drawn, and a small gap is kept between neighbours.
	{
		const TArray<bool> Apart = MobileMapLabels::Choose({ Label(10, 10, 60, 14, 1.0), Label(100, 10, 60, 14, 1.0), Label(10, 60, 60, 14, 1.0) }, {}, Screen);
		TestTrue(TEXT("labels that are apart are all drawn"), Apart[0] && Apart[1] && Apart[2]);
		const TArray<bool> Touching = MobileMapLabels::Choose({ Label(10, 10, 60, 14, 2.0), Label(71, 10, 60, 14, 1.0) }, {}, Screen);
		TestFalse(TEXT("one only a pixel away is too close"), Touching[1]);
	}
	// Chosen labels block the less important ones even when those do not meet the first, only another that was chosen.
	{
		const TArray<bool> Chain = MobileMapLabels::Choose({ Label(10, 10, 60, 14, 3.0), Label(60, 10, 60, 14, 2.0), Label(110, 10, 60, 14, 1.0) }, {}, Screen);
		TestTrue(TEXT("the second is left out, so the third is free"), Chain[0] && !Chain[1] && Chain[2]);
	}
	// Something that must stay clear (a lift's circle) keeps labels off, however important.
	{
		const TArray<bool> Chosen = MobileMapLabels::Choose({ Label(10, 10, 60, 14, 100.0) }, { FBox2D(FVector2D(30, 5), FVector2D(48, 23)) }, Screen);
		TestFalse(TEXT("an obstacle wins"), Chosen[0]);
	}
	// Only what can be seen: a label wholly off the screen is not chosen, one partly on it is.
	{
		const TArray<bool> Chosen = MobileMapLabels::Choose({ Label(500, 10, 60, 14, 1.0), Label(-30, 10, 60, 14, 1.0) }, {}, Screen);
		TestTrue(TEXT("off the screen is left out, partly on it is drawn"), !Chosen[0] && Chosen[1]);
	}
	TestEqual(TEXT("nothing in, nothing out"), MobileMapLabels::Choose({}, {}, Screen).Num(), 0);
	return true;
}

// Zooming in spreads the labels apart: the same labels, farther from each other, are more of them drawn.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileMapLabelZoomTest, "InNavi.Mobile.MapLabels.ZoomShowsMore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobileMapLabelZoomTest::RunTest(const FString& Parameters)
{
	using namespace MobileMapLabelTests;
	auto CountAt = [](double Spread)
	{
		TArray<MobileMapLabels::FCandidate> Row;
		for (int32 I = 0; I < 10; ++I) Row.Add(Label(20 + I * Spread, 100, 60, 14, 1.0));
		int32 Drawn = 0;
		for (const bool bChosen : MobileMapLabels::Choose(Row, {}, FBox2D(FVector2D(0, 0), FVector2D(2000, 400)))) Drawn += bChosen ? 1 : 0;
		return Drawn;
	};
	const int32 Far = CountAt(20.0), Near = CountAt(40.0), Closer = CountAt(80.0);
	TestTrue(TEXT("farther apart, more labels are drawn"), Far < Near && Near < Closer);
	TestEqual(TEXT("far enough apart, all of them"), Closer, 10);
	return true;
}

// The code is shown only when the place has one.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobilePlaceCodeTest, "InNavi.Mobile.MapLabels.PlaceCodeOptional", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMobilePlaceCodeTest::RunTest(const FString& Parameters)
{
	FMobilePlace Place;
	TestEqual(TEXT("no code: just the rest"), Place.WithCode(TEXT("Learning")), FString(TEXT("Learning")));
	Place.Code = TEXT("E2-01-101");
	TestEqual(TEXT("a code goes first"), Place.WithCode(TEXT("Learning")), FString(TEXT("E2-01-101 · Learning")));

	// Real data has no door numbers: a room's internal id is not shown, but it can still be searched for.
	FMobileRealSource Source(FPaths::ProjectDir() / TEXT("../docs/test-data/entrance-test-2-with-doors.json"));
	const TArray<FMobileSite> Sites = Source.ListSites();
	if (!TestFalse(TEXT("the test project is read"), Sites.IsEmpty())) return false;
	const TArray<FMobilePlace> Places = Source.SearchPlaces(Sites[0].Id, FString(), FMobilePlaceFilter());
	if (!TestFalse(TEXT("it has places"), Places.IsEmpty())) return false;
	bool bAnyCode = false;
	for (const FMobilePlace& P : Places) bAnyCode |= !P.Code.IsEmpty();
	TestFalse(TEXT("no place shows an internal id as its code"), bAnyCode);
	FString RoomId = Places[0].Id;
	RoomId = RoomId.Mid(RoomId.Find(TEXT("/room/")) + 6);
	TestFalse(TEXT("the room has an id"), RoomId.IsEmpty());
	TestTrue(TEXT("searching the id still finds the place"), Source.SearchPlaces(Sites[0].Id, RoomId, FMobilePlaceFilter()).ContainsByPredicate([&Places](const FMobilePlace& P) { return P.Id == Places[0].Id; }));
	return true;
}

#endif
