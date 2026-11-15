#pragma once

#include "CoreMinimal.h"

// What the person searched for lately, for the "Recent searches" of the full-screen search (see UMobileSearchOverlayWidget), newest first, at
// most MaxEntries of each kind, and a small file to keep them in. Two lists, as in the prototype:
//  - Chosen: the buildings and places picked from the map's search, as keys ("building:E2", "place:E2-01-101"), so a tap on one opens it again;
//  - Queries: the words typed into the Explore tab's site search, so a tap on one types it again.
// The app's own state, not the site's data. It works without a file (then nothing is kept), so tests and the first run touch no files.
class INNAVI_API FMobileSearchHistory
{
public:
	static constexpr int32 MaxEntries = 8;
	// Longest query that is remembered (longer is cut).
	static constexpr int32 MaxQueryLength = 80;

	static FString BuildingKey(const FString& BuildingId) { return TEXT("building:") + BuildingId; }
	static FString PlaceKey(const FString& PlaceId) { return TEXT("place:") + PlaceId; }
	// Splits a key into its kind ("building" or "place") and id; false for anything else.
	static bool ParseKey(const FString& Key, FString& OutKind, FString& OutId);

	// Newest first.
	const TArray<FString>& Chosen() const { return ChosenKeys; }
	const TArray<FString>& Queries() const { return QueryList; }

	// Puts a key at the top (once), and drops the oldest past MaxEntries. False when it could not be written (the list is as it was).
	bool AddChosen(const FString& Key);
	// Puts words at the top (once, however they are cased), trimmed and cut to MaxQueryLength; nothing for empty words.
	bool AddQuery(const FString& Query);
	bool ClearChosen();
	bool ClearQueries();
	// Both lists.
	bool ClearAll();

	// Keeps the lists in this file from now on and reads what is in it (a missing or damaged file is empty lists). Every change is written.
	void UseFile(const FString& InPath);
	bool Load();
	bool Save() const;
	const FString& GetFile() const { return Path; }

	// Where the app keeps it.
	static FString DefaultFile();

private:
	bool Commit(TArray<FString> NewChosen, TArray<FString> NewQueries);
	static FString CleanQuery(const FString& Query);

	TArray<FString> ChosenKeys;
	TArray<FString> QueryList;
	FString Path;
};
