#pragma once

#include "CoreMinimal.h"

namespace NavigationSearch
{
	INNAVI_API TArray<FString> NormalizeTags(const TArray<FString>& Tags);
	INNAVI_API TArray<FString> ParseTags(const FString& Text);
	// The search tags a room gets by itself from its name and its type (PAT-198): the words of the name, lower case, then the type as one tag
	// ("Male Toilet L2" + Toilet -> male, toilet, l2). Words of digits only are left out, a name with an id in it (six or more digits in a row,
	// "SQL-1161271361-01-02") gives nothing, the type Other adds no tag, and a word is added once.
	INNAVI_API TArray<FString> TagsFromNameAndType(const FString& Name, const FString& Type);
	// Every whitespace-separated word must match; matching ignores case.
	INNAVI_API bool Matches(const FString& Query, const FString& SearchableText);
	// Parse once when testing the same query against many entries.
	INNAVI_API bool MatchesWords(const TArray<FString>& Words, const FString& SearchableText);
}
