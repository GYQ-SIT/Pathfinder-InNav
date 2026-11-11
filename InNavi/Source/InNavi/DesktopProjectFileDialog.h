#pragma once

#include "CoreMinimal.h"

namespace DesktopProjectFileDialog
{
	// False + empty Error is cancellation. Windows uses runtime system APIs, including in Shipping.
	bool Pick(const void* ParentWindow, const FString& Directory, bool bSave, FString& OutPath, FString& Error);
}
