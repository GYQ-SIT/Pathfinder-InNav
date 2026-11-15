// Fill out your copyright notice in the Description page of Project Settings.

#include "GeoProjection.h"

double GeoProjection::MetersPerDegreeLon(double Lat)
{
	return MetersPerDegreeLat * FMath::Cos(FMath::DegreesToRadians(Lat));
}

FVector2D GeoProjection::ToLocalMeters(double Lat, double Lon, double RefLat, double RefLon)
{
	// Doubles all the way: a longitude kept in a float is only good to 0.4 m, so only the small offset from the reference point may become one.
	const double North = (Lat - RefLat) * MetersPerDegreeLat;
	const double East = (Lon - RefLon) * MetersPerDegreeLon(RefLat);
	return FVector2D(North, East);
}
