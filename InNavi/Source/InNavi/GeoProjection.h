// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

// How a point of the map database (a latitude and a longitude) becomes a point of the world, in meters around a reference point. One place, so that
// the buildings, the context buildings and the roads of the campus can never disagree, and so that the GPS of the phone can use the same one.
//
// Unreal's world is left-handed: X points forward, Y to the right, Z up. Seen from above with X up the screen, Y is to the right of it. The
// map is only the right way round when NORTH is X and EAST is Y: then the editor's top view (X up) is a map with north up and east to the right,
// and a compass heading is an Unreal yaw (yaw 0 looks along +X, to the north). The loader used to put east on X and north on Y, which swaps
// the two axes and so draws the whole campus as a mirror image of the real map (the loop road on the wrong side of the highway).
namespace GeoProjection
{
	// The length of a degree of latitude, in meters (the equirectangular approximation validated against the campus data).
	constexpr double MetersPerDegreeLat = 111320.0;

	// The length of a degree of longitude, in meters, at a latitude.
	double MetersPerDegreeLon(double Lat);

	// The point (Lat, Lon) in meters from the reference point (RefLat, RefLon): X = meters to the north, Y = meters to the east.
	FVector2D ToLocalMeters(double Lat, double Lon, double RefLat, double RefLon);
}
