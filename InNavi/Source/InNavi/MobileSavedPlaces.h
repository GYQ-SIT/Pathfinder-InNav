#pragma once

#include "CoreMinimal.h"

// The places the person saved (the Saved tab), newest first, and a small file to keep them in so they are there the next time the app opens.
// The app's own state, not the site's data. It works without a file (then nothing is kept), so tests and the first run touch no files.
class INNAVI_API FMobileSavedPlaces
{
public:
	bool Contains(const FString& PlaceId) const { return Order.Contains(PlaceId); }
	// Saves a place (at the top of the list) or takes it back; returns whether it is saved now.
	bool Toggle(const FString& PlaceId);
	void Add(const FString& PlaceId);
	bool Remove(const FString& PlaceId);
	// Takes every place off.
	void Clear();
	// Newest first.
	const TArray<FString>& Ids() const { return Order; }
	int32 Num() const { return Order.Num(); }

	// Keeps the list in this file from now on and reads what is in it (a missing or damaged file is an empty list). Every change is written.
	void UseFile(const FString& InPath);
	bool Load();
	bool Save() const;
	const FString& GetFile() const { return Path; }

	// Where the app keeps it.
	static FString DefaultFile();

private:
	void Changed();

	TArray<FString> Order;
	FString Path;
};
