#pragma once

#include "CoreMinimal.h"

namespace DesktopProjectFileDialog
{
	// What the open dialog asks for (a save dialog always asks for a .json project).
	enum class EKind : uint8
	{
		BuildingData,   // a .json project or a .db map database
		Png,            // a PNG picture: a floor's reference image
		Picture,        // a PNG, JPEG or BMP picture: a project's cover
	};

	// False + empty Error is cancellation. Windows uses runtime system APIs, including in Shipping.
	bool Pick(const void* ParentWindow, const FString& Directory, bool bSave, FString& OutPath, FString& Error, EKind Kind = EKind::BuildingData);
}
