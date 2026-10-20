#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

// One project file found in the projects folder (InNavi/Data), as the project library shows it.
struct INNAVI_API FProjectLibraryEntry
{
	FString FilePath;      // absolute
	FString FileName;      // with the extension
	FString DisplayName;   // the project's own name, else the file name without its extension
	FString Description;
	int32 BuildingCount = 0;
	FDateTime Modified;    // last time the file was written
	bool bIsDatabase = false; // a .db map database, not a .json project
	bool bIsSample = false;   // a file that ships with the project (tests and defaults refer to it by name)
};

// Finding, copying and renaming the project files that live in the projects folder. Pure file work, no UI - the project
// library page and the New project flow both use it.
namespace ProjectLibrary
{
	// The file a project with this name is stored in: the name with the characters Windows does not allow in a file name
	// replaced, plus ".json", inside Directory. Empty if nothing usable is left of the name (only dots, underscores, ...).
	INNAVI_API FString FilePathForName(const FString& Directory, const FString& Name);

	// Everything in Directory that can be loaded as a project, newest first: .json files that hold a "buildings" list (other
	// JSON, such as layout files, is left out) and .db map databases.
	INNAVI_API TArray<FProjectLibraryEntry> Scan(const FString& Directory);

	// Copies a .json project to "<name> copy" (or "... copy 2", ... if that is taken), giving the copy that name. Never
	// overwrites. A database cannot be duplicated here.
	INNAVI_API bool Duplicate(const FProjectLibraryEntry& Entry, const FString& Directory, FString& OutNewPath, FString& OutError);

	// Takes a project out of the library without destroying it: the file is moved into a "Deleted" folder inside Directory (a
	// name already there gets a time stamp), so it can be put back by hand. Refuses the shipped sample files.
	INNAVI_API bool SoftDelete(const FProjectLibraryEntry& Entry, const FString& Directory, FString& OutDeletedPath, FString& OutError);

	// Gives a .json project a new name and moves its file to match. Refuses an empty or unusable name, a name some other
	// project already has, and the shipped sample files and databases.
	INNAVI_API bool Rename(const FProjectLibraryEntry& Entry, const FString& NewName, const FString& Directory, FString& OutNewPath, FString& OutError);
}
