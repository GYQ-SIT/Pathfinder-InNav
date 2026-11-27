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
	// Crop: how much of the picture is cut away from each side, as a fraction (0 to 0.95) of the whole picture's width (left, right) or height (top, bottom).
	// The picture keeps its place and size (Center and Width are those of the whole picture); only the cut-away part is not drawn. The file is never changed.
	float CropLeft = 0.f;
	float CropTop = 0.f;
	float CropRight = 0.f;
	float CropBottom = 0.f;

	bool HasCrop() const { return CropLeft > 0.f || CropTop > 0.f || CropRight > 0.f || CropBottom > 0.f; }

	bool operator==(const FInteriorReferenceImage& Other) const
	{
		return Path == Other.Path && Center.Equals(Other.Center, 1e-6) && FMath::IsNearlyEqual(Width, Other.Width, 1e-6f)
			&& FMath::IsNearlyEqual(Opacity, Other.Opacity, 1e-6f) && bVisible == Other.bVisible
			&& FMath::IsNearlyEqual(CropLeft, Other.CropLeft, 1e-6f) && FMath::IsNearlyEqual(CropTop, Other.CropTop, 1e-6f)
			&& FMath::IsNearlyEqual(CropRight, Other.CropRight, 1e-6f) && FMath::IsNearlyEqual(CropBottom, Other.CropBottom, 1e-6f);
	}
};

namespace InteriorReferenceImage
{
	constexpr float MinOpacity = 0.05f;
	// At least this much of the picture, in each direction, is always left after a crop.
	constexpr float MinVisibleFraction = 0.05f;
	// Larger files are refused: a picture is read into memory whole.
	constexpr int64 MaxFileBytes = 32ll * 1024 * 1024;

	// The image as it is kept: path trimmed, opacity within range. False when it cannot be kept (no path, a width that is not a positive number, a centre that is not a number).
	inline bool Normalize(FInteriorReferenceImage& Image)
	{
		Image.Path = Image.Path.TrimStartAndEnd();
		Image.Opacity = FMath::Clamp(Image.Opacity, MinOpacity, 1.f);
		const auto Fraction = [](float Value, float Room) { return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, FMath::Max(Room, 0.f)) : 0.f; };
		Image.CropLeft = Fraction(Image.CropLeft, 1.f - MinVisibleFraction);
		Image.CropRight = Fraction(Image.CropRight, 1.f - MinVisibleFraction - Image.CropLeft);
		Image.CropTop = Fraction(Image.CropTop, 1.f - MinVisibleFraction);
		Image.CropBottom = Fraction(Image.CropBottom, 1.f - MinVisibleFraction - Image.CropTop);
		return !Image.Path.IsEmpty() && FMath::IsFinite(Image.Width) && Image.Width > 0.f && Image.Width < 1.0e7f
			&& FMath::IsFinite(Image.Center.X) && FMath::IsFinite(Image.Center.Y);
	}
}
