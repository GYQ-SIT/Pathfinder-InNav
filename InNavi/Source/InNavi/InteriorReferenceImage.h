#pragma once

#include "CoreMinimal.h"

// A reference image under the 2D drawing of one floor (PAT-201): a PNG, such as a scanned floor plan, to trace over. It is only drawn in the 2D plan; it
// never takes part in routing, validation or the 3D view. The file stays where it is (the project keeps its path), so a project moved to another
// computer shows no image until the file is put back.
struct FInteriorReferenceImage
{
	// Full path of the PNG.
	FString Path;
	// Where the middle of the picture lies, in footprint units (the space of the building's outline).
	FVector2D Center = FVector2D::ZeroVector;
	// How wide the picture is, in footprint units; its height follows the picture's own proportions.
	float Width = 0.f;
	// 0.05 (almost clear) to 1 (solid).
	float Opacity = 0.5f;
	bool bVisible = true;
	bool bLocked = true;
	FBox2D Crop = FBox2D(FVector2D::ZeroVector, FVector2D(1.0, 1.0));

	bool operator==(const FInteriorReferenceImage& Other) const
	{
		return Path == Other.Path && Center.Equals(Other.Center, 1e-6) && FMath::IsNearlyEqual(Width, Other.Width, 1e-6f)
			&& FMath::IsNearlyEqual(Opacity, Other.Opacity, 1e-6f) && bVisible == Other.bVisible
			&& bLocked == Other.bLocked && Crop.Min.Equals(Other.Crop.Min, 1e-6) && Crop.Max.Equals(Other.Crop.Max, 1e-6);
	}
};

namespace InteriorReferenceImage
{
	constexpr float MinOpacity = 0.05f;
	constexpr double MinCropFraction = 0.01;
	// Larger files are refused: a picture is read into memory whole.
	constexpr int64 MaxFileBytes = 32ll * 1024 * 1024;

	// The image as it is kept: path trimmed, opacity within range. False when it cannot be kept (no path, a width that is not a positive number, a centre that is not a number).
	inline bool Normalize(FInteriorReferenceImage& Image)
	{
		Image.Path = Image.Path.TrimStartAndEnd();
		Image.Opacity = FMath::Clamp(Image.Opacity, MinOpacity, 1.f);
		if (Image.Crop.Min.ContainsNaN() || Image.Crop.Max.ContainsNaN()
			|| Image.Crop.Min.X < 0.0 || Image.Crop.Min.Y < 0.0 || Image.Crop.Max.X > 1.0 || Image.Crop.Max.Y > 1.0
			|| Image.Crop.GetSize().X < MinCropFraction || Image.Crop.GetSize().Y < MinCropFraction)
		{
			Image.Crop = FBox2D(FVector2D::ZeroVector, FVector2D(1.0, 1.0));
		}
		return !Image.Path.IsEmpty() && FMath::IsFinite(Image.Width) && Image.Width > 0.f && Image.Width < 1.0e7f
			&& FMath::IsFinite(Image.Center.X) && FMath::IsFinite(Image.Center.Y);
	}
}
