#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

namespace ProjectFile
{
	class FMutationLock
	{
	public:
		bool IsValid() const
		{
#if PLATFORM_DESKTOP
			return Lock.IsValid();
#else
			return true;
#endif
		}
	private:
#if PLATFORM_DESKTOP
		FSystemWideCriticalSection Lock{TEXT("InNavi.ProjectSave"), FTimespan::FromSeconds(2)};
#endif
	};

	bool MoveWithoutReplacement(const FString& Source, const FString& Destination);
	// Write and close a sibling temporary file before publishing it at the destination.
	bool WriteText(const FString& Path, const FString& Text, bool bReplace = true);
}
