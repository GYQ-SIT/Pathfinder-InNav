#pragma once

#include "CoreMinimal.h"

namespace NavigationSearch
{
	INNAVI_API TArray<FString> NormalizeTags(const TArray<FString>& Tags);
	INNAVI_API TArray<FString> ParseTags(const FString& Text);
	// Every whitespace-separated word must match; matching ignores case.
	INNAVI_API bool Matches(const FString& Query, const FString& SearchableText);
	// Parse once when testing the same query against many entries.
	INNAVI_API bool MatchesWords(const TArray<FString>& Words, const FString& SearchableText);
}
