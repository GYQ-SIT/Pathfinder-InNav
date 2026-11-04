#pragma once

#include "CoreMinimal.h"

namespace ProjectFile
{
	// Write and close a sibling temporary file before publishing it at the destination.
	bool WriteText(const FString& Path, const FString& Text, bool bReplace = true);
}
