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

TArray<FString> NavigationSearch::TagsFromNameAndType(const FString& Name, const FString& Type)
{
	// Words made of letters and digits; a word of digits only ("101", "01") says little and the name itself is searched anyway, so it is not a tag.
	TArray<FString> Words;
	FString Word;
	int32 DigitRun = 0;
	bool bLooksLikeAnId = false;
	auto Flush = [&]()
	{
		bool bAllDigits = !Word.IsEmpty();
		for (const TCHAR Character : Word) bAllDigits &= FChar::IsDigit(Character);
		if (!Word.IsEmpty() && !bAllDigits) Words.Add(Word.ToLower());
		Word.Reset();
	};
	for (const TCHAR Character : Name)
	{
		DigitRun = FChar::IsDigit(Character) ? DigitRun + 1 : 0;
		// A long run of digits is a database or import id (SQL-1161271361-01-02), not a name a person gave: nothing of the name is used then.
		bLooksLikeAnId |= DigitRun >= 6;
		if (FChar::IsAlnum(Character)) Word.AppendChar(Character); else Flush();
	}
	Flush();
	if (bLooksLikeAnId) Words.Reset();
	const FString TrimmedType = Type.TrimStartAndEnd();
	if (!TrimmedType.IsEmpty() && !TrimmedType.Equals(TEXT("Other"), ESearchCase::IgnoreCase)) Words.Add(TrimmedType.ToLower());
	return NormalizeTags(Words);
}

bool NavigationSearch::Matches(const FString& Query, const FString& SearchableText)
{
	TArray<FString> Words;
	Query.ParseIntoArrayWS(Words);
	return MatchesWords(Words, SearchableText);
}

bool NavigationSearch::MatchesWords(const TArray<FString>& Words, const FString& SearchableText)
{
	for (const FString& Word : Words)
	{
		if (!SearchableText.Contains(Word, ESearchCase::IgnoreCase)) return false;
	}
	return true;
}
