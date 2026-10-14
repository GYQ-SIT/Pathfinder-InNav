#include "NavigationSearchUtils.h"

TArray<FString> NavigationSearch::NormalizeTags(const TArray<FString>& Tags)
{
	TArray<FString> Result;
	TSet<FString> Seen;
	for (const FString& Entry : Tags)
	{
		TArray<FString> Parts;
		Entry.ParseIntoArray(Parts, TEXT(","), true);
		for (FString& Tag : Parts)
		{
			Tag.TrimStartAndEndInline();
			if (!Tag.IsEmpty() && !Seen.Contains(Tag.ToLower()))
			{
				Seen.Add(Tag.ToLower());
				Result.Add(Tag);
			}
		}
	}
	return Result;
}

TArray<FString> NavigationSearch::ParseTags(const FString& Text)
{
	return NormalizeTags({ Text });
}

bool NavigationSearch::Matches(const FString& Query, const FString& SearchableText)
{
	TArray<FString> Words;
	Query.ParseIntoArrayWS(Words);
	for (const FString& Word : Words)
	{
		if (!SearchableText.Contains(Word, ESearchCase::IgnoreCase)) return false;
	}
	return true;
}
