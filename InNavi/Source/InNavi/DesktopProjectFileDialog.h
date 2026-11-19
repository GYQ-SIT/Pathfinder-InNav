#pragma once

#include "CoreMinimal.h"

namespace DesktopProjectFileDialog
{
	// False + empty Error is cancellation. Windows uses runtime system APIs, including in Shipping.
	// bPng: ask for a PNG picture (a floor's reference image) instead of building data; only for opening.
	bool Pick(const void* ParentWindow, const FString& Directory, bool bSave, FString& OutPath, FString& Error, bool bPng = false);
}
