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
	FString CoverPath;        // absolute path of the project's cover picture (a PNG in Data/Covers); empty when it has none
};

// Finding, copying and renaming the project files that live in the projects folder. Pure file work, no UI - the project
// library page and the New project flow both use it.
namespace ProjectLibrary
{
	// The cover picture of a project card is kept as a PNG of this size (twice what the card shows, so it stays sharp), cut from the middle of the
	// picture the person chose.
	constexpr int32 CoverWidth = 760;
	constexpr int32 CoverHeight = 390;
	// A picture bigger than this is refused (it is read into memory whole), and so is one with a side over MaxCoverSide.
	constexpr int64 MaxCoverFileBytes = 32ll * 1024 * 1024;
	constexpr int32 MaxCoverSide = 16384;

	// Where the cover of the project in FileName is kept: Directory/Covers/<FileName>.png (the file name with its extension, so SIT.json and SIT.db
	// do not share one). The cover is a file of its own next to the project: the project file is not touched, so a map database or a sample can have
	// one too, and a project's cover is not part of what the editor, the validator or the phone read.
	INNAVI_API FString CoverPathFor(const FString& Directory, const FString& FileName);

	// Makes PicturePath (a PNG, JPEG or BMP) the project's cover: cut from the middle to the card's proportions, scaled to CoverWidth x CoverHeight and
	// saved as the project's cover PNG (replacing the old one). The picture that was chosen is left as it is.
	INNAVI_API bool SetCover(const FProjectLibraryEntry& Entry, const FString& PicturePath, const FString& Directory, FString& OutError);

	// Takes the cover away: the card shows its drawing again. True when the project has none (nothing to do) or it was removed.
	INNAVI_API bool RemoveCover(const FProjectLibraryEntry& Entry, const FString& Directory, FString& OutError);

	// The file a project with this name is stored in: the name with the characters Windows does not allow in a file name
	// replaced, plus ".json", inside Directory. Empty if nothing usable is left of the name (only dots, underscores, ...).
	INNAVI_API FString FilePathForName(const FString& Directory, const FString& Name);

	// Everything in Directory that can be loaded as a project, newest first: .json files that hold a "buildings" list (other
	// JSON, such as layout files, is left out) and .db map databases.
	INNAVI_API TArray<FProjectLibraryEntry> Scan(const FString& Directory);

	// Copies a .json project to "<name> copy" (or "... copy 2", ... if that is taken), giving the copy that name (and its cover, if it has one). Never
	// overwrites. A database cannot be duplicated here.
	INNAVI_API bool Duplicate(const FProjectLibraryEntry& Entry, const FString& Directory, FString& OutNewPath, FString& OutError);

	// Takes a project out of the library without destroying it: the file is moved into a "Deleted" folder inside Directory (a
	// name already there gets a time stamp), so it can be put back by hand. Refuses the shipped sample files.
	INNAVI_API bool SoftDelete(const FProjectLibraryEntry& Entry, const FString& Directory, FString& OutDeletedPath, FString& OutError);

	// Gives a .json project a new name and moves its file to match. Refuses an empty or unusable name, a name some other
	// project already has, and the shipped sample files and databases.
	INNAVI_API bool Rename(const FProjectLibraryEntry& Entry, const FString& NewName, const FString& Directory, FString& OutNewPath, FString& OutError);
}
