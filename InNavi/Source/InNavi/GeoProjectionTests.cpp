// Fill out your copyright notice in the Description page of Project Settings.

#if WITH_DEV_AUTOMATION_TESTS

#include "GeoProjection.h"
#include "Misc/AutomationTest.h"

// The campus must not be a mirror image of the real map (the loop road was on the wrong side of the highway), and the phone's GPS will use the same projection.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoProjectionAxesTest, "InNavi.Geo.NorthIsXAndEastIsY", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGeoProjectionAxesTest::RunTest(const FString& Parameters)
{
	constexpr double RefLat = 1.4130;
	constexpr double RefLon = 103.9100;

	// The reference point is the origin.
	const FVector2D Origin = GeoProjection::ToLocalMeters(RefLat, RefLon, RefLat, RefLon);
	TestTrue(TEXT("the reference point is the origin"), Origin.IsNearlyZero(0.0001));

	// 100 m to the north is +X and nothing else; 100 m to the east is +Y and nothing else.
	const FVector2D North = GeoProjection::ToLocalMeters(RefLat + 100.0 / GeoProjection::MetersPerDegreeLat, RefLon, RefLat, RefLon);
	TestEqual(TEXT("north is +X"), North.X, 100.0, 0.001);
	TestEqual(TEXT("north does not move Y"), North.Y, 0.0, 0.001);
	const FVector2D East = GeoProjection::ToLocalMeters(RefLat, RefLon + 100.0 / GeoProjection::MetersPerDegreeLon(RefLat), RefLat, RefLon);
	TestEqual(TEXT("east is +Y"), East.Y, 100.0, 0.001);
	TestEqual(TEXT("east does not move X"), East.X, 0.0, 0.001);

	// South and west are the other way.
	const FVector2D SouthWest = GeoProjection::ToLocalMeters(RefLat - 50.0 / GeoProjection::MetersPerDegreeLat, RefLon - 50.0 / GeoProjection::MetersPerDegreeLon(RefLat), RefLat, RefLon);
	TestTrue(TEXT("south is -X"), SouthWest.X < -49.9);
	TestTrue(TEXT("west is -Y"), SouthWest.Y < -49.9);

	// The map is only the right way round if turning from north to east is a turn to the right seen from above (Unreal's left-handed X, Y, Z-up): the cross
	// product of the two directions is positive. East on X and north on Y (the old projection) gave a negative one: a mirror.
	TestTrue(TEXT("north to east is a turn the same way as X to Y"), (North.X * East.Y - North.Y * East.X) > 0.0);

	// A degree of longitude is shorter than one of latitude away from the equator, by the cosine of the latitude.
	TestEqual(TEXT("a degree of longitude at 1.413 degrees north"), GeoProjection::MetersPerDegreeLon(RefLat) / GeoProjection::MetersPerDegreeLat, FMath::Cos(FMath::DegreesToRadians(RefLat)), 1e-9);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
