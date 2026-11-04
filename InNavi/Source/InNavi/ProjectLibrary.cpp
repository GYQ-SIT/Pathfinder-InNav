#include "ProjectLibrary.h"
#include "ProjectFile.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SQLiteDatabase.h"

namespace
{
	// Files the project ships with: tests and the editor's defaults refer to them by name, so they are not renamed.
	bool IsSampleFileName(const FString& FileName)
	{
		return FileName.Equals(TEXT("buildings.json"), ESearchCase::IgnoreCase) || FileName.Equals(TEXT("map_extended.db"), ESearchCase::IgnoreCase);
	}

	bool ReadProjectJson(const FString& FilePath, TSharedPtr<FJsonObject>& OutRoot)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FilePath))
		{
			return false;
		}
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), OutRoot) && OutRoot.IsValid();
	}

	bool WriteProjectJson(const FString& FilePath, const TSharedRef<FJsonObject>& Root, bool bReplace)
	{
		FString Text;
		if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text)))
		{
			return false;
		}
		return ProjectFile::WriteText(FilePath, Text, bReplace);
	}

	// How many buildings a database holds: the campus ones (tagged "university"), which are the ones the editor opens.
	int32 CountDatabaseBuildings(const FString& FilePath, bool& bOutReadable)
	{
		bOutReadable = false;
		FSQLiteDatabase Database;
		if (!Database.Open(*FilePath, ESQLiteDatabaseOpenMode::ReadOnly))
		{
			return 0;
		}
		int64 Count = 0;
		// A COUNT always answers with one row when the query ran; a file that is not a map database never does.
		bool bGotRow = false;
		Database.Execute(TEXT("SELECT COUNT(*) AS n FROM buildings WHERE building_type = 'university'"),
			[&Count, &bGotRow](const FSQLitePreparedStatement& Statement) -> ESQLitePreparedStatementExecuteRowResult
			{
				bGotRow = Statement.GetColumnValueByName(TEXT("n"), Count);
				return ESQLitePreparedStatementExecuteRowResult::Continue;
			});
		Database.Close();
		bOutReadable = bGotRow;
		return static_cast<int32>(Count);
	}

	// The longest name a project file gets; a longer name is cut (see ProjectLibrary::FilePathForName).
	constexpr int32 MaxFileNameLength = 80;

	// A name for a copy that no file has yet, or an empty string when there is none. The name is cut to fit a file name, so it is the
	// base that gives way to the suffix: cut after the suffix, every copy of a long name would end up with the same file name.
	FString UnusedCopyName(const FString& Directory, const FString& BaseName)
	{
		for (int32 Attempt = 1; Attempt < 1000; ++Attempt)
		{
			const FString Suffix = Attempt == 1 ? FString(TEXT(" copy")) : FString::Printf(TEXT(" copy %d"), Attempt);
			const FString Candidate = BaseName.Left(FMath::Max(1, MaxFileNameLength - Suffix.Len())).TrimEnd() + Suffix;
			const FString Path = ProjectLibrary::FilePathForName(Directory, Candidate);
			if (Path.IsEmpty() || !IFileManager::Get().FileExists(*Path))
			{
				return Candidate;
			}
		}
		return FString();
	}
}

FString ProjectLibrary::FilePathForName(const FString& Directory, const FString& Name)
{
	FString FileName = FPaths::MakeValidFileName(Name.TrimStartAndEnd(), TEXT('_'));
	FileName.TrimStartAndEndInline();
	// A name that is only dots or underscores (what is left of "???" or "..") would make a file nobody can see.
	FString Letters = FileName;
	Letters.ReplaceInline(TEXT("_"), TEXT(""));
	Letters.ReplaceInline(TEXT("."), TEXT(""));
	Letters.TrimStartAndEndInline();
	if (Letters.IsEmpty())
	{
		return FString();
	}
	if (FileName.Len() > MaxFileNameLength)
	{
		FileName.LeftInline(MaxFileNameLength);
		FileName.TrimEndInline();
	}
	return Directory / (FileName + TEXT(".json"));
}

TArray<FProjectLibraryEntry> ProjectLibrary::Scan(const FString& Directory)
{
	TArray<FProjectLibraryEntry> Entries;
	TArray<FString> Names;
	IFileManager::Get().FindFiles(Names, *(Directory / TEXT("*.json")), true, false);
	TArray<FString> Databases;
	IFileManager::Get().FindFiles(Databases, *(Directory / TEXT("*.db")), true, false);
	Names.Append(Databases);

	for (const FString& Name : Names)
	{
		FProjectLibraryEntry Entry;
		Entry.FileName = Name;
		Entry.FilePath = Directory / Name;
		Entry.bIsSample = IsSampleFileName(Name);
		Entry.bIsDatabase = FPaths::GetExtension(Name).Equals(TEXT("db"), ESearchCase::IgnoreCase);
		Entry.Modified = IFileManager::Get().GetTimeStamp(*Entry.FilePath);
		Entry.DisplayName = FPaths::GetBaseFilename(Name);

		if (Entry.bIsDatabase)
		{
			bool bReadable = false;
			Entry.BuildingCount = CountDatabaseBuildings(Entry.FilePath, bReadable);
			if (!bReadable)
			{
				continue; // not a map database
			}
		}
		else
		{
			TSharedPtr<FJsonObject> Root;
			const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
			if (!ReadProjectJson(Entry.FilePath, Root) || !Root->TryGetArrayField(TEXT("buildings"), Buildings))
			{
				continue; // some other JSON file (a saved layout, settings, ...)
			}
			Entry.BuildingCount = Buildings ? Buildings->Num() : 0;
			FString ProjectName;
			if (Root->TryGetStringField(TEXT("projectName"), ProjectName) && !ProjectName.TrimStartAndEnd().IsEmpty())
			{
				Entry.DisplayName = ProjectName;
			}
			Root->TryGetStringField(TEXT("description"), Entry.Description);
		}
		Entries.Add(MoveTemp(Entry));
	}

	Entries.Sort([](const FProjectLibraryEntry& A, const FProjectLibraryEntry& B)
	{
		return A.Modified != B.Modified ? A.Modified > B.Modified : A.DisplayName < B.DisplayName;
	});
	return Entries;
}

bool ProjectLibrary::Duplicate(const FProjectLibraryEntry& Entry, const FString& Directory, FString& OutNewPath, FString& OutError)
{
	OutNewPath.Reset();
	OutError.Reset();
	if (Entry.bIsDatabase)
	{
		OutError = TEXT("A map database cannot be duplicated.");
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!ReadProjectJson(Entry.FilePath, Root))
	{
		OutError = TEXT("The project file could not be read.");
		return false;
	}
	const FString CopyName = UnusedCopyName(Directory, Entry.DisplayName);
	if (CopyName.IsEmpty())
	{
		OutError = TEXT("A name for the copy could not be made.");
		return false;
	}
	const FString CopyPath = FilePathForName(Directory, CopyName);
	if (CopyPath.IsEmpty())
	{
		OutError = TEXT("A name for the copy could not be made.");
		return false;
	}
	Root->SetStringField(TEXT("projectName"), CopyName);
	if (!WriteProjectJson(CopyPath, Root.ToSharedRef(), false))
	{
		OutError = TEXT("The copy could not be written - check that the Data folder can be written to.");
		return false;
	}
	OutNewPath = CopyPath;
	return true;
}

bool ProjectLibrary::Rename(const FProjectLibraryEntry& Entry, const FString& NewName, const FString& Directory, FString& OutNewPath, FString& OutError)
{
	OutNewPath.Reset();
	OutError.Reset();
	if (Entry.bIsDatabase || Entry.bIsSample)
	{
		OutError = TEXT("The sample files that come with the project cannot be renamed. Duplicate it and rename the copy.");
		return false;
	}
	const FString Name = NewName.TrimStartAndEnd();
	if (Name.IsEmpty())
	{
		OutError = TEXT("Enter a project name.");
		return false;
	}
	const FString NewPath = FilePathForName(Directory, Name);
	if (NewPath.IsEmpty())
	{
		OutError = TEXT("The name needs at least one letter or number.");
		return false;
	}
	const ProjectFile::FMutationLock Lock;
	if (!Lock.IsValid())
	{
		OutError = TEXT("Another editor is changing a project. Try again.");
		return false;
	}
	const bool bSameFile = FPaths::GetCleanFilename(NewPath).Equals(Entry.FileName, ESearchCase::IgnoreCase);
	if (!bSameFile && IFileManager::Get().FileExists(*NewPath))
	{
		OutError = FString::Printf(TEXT("A project named \"%s\" already exists. Choose another name."), *FPaths::GetBaseFilename(NewPath));
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!ReadProjectJson(Entry.FilePath, Root))
	{
		OutError = TEXT("The project file could not be read.");
		return false;
	}
	Root->SetStringField(TEXT("projectName"), Name);
	if (!WriteProjectJson(bSameFile ? Entry.FilePath : NewPath, Root.ToSharedRef(), bSameFile))
	{
		OutError = TEXT("The project file could not be written - check that the Data folder can be written to.");
		return false;
	}
	if (!bSameFile && !IFileManager::Get().Delete(*Entry.FilePath, true, false, true))
	{
		OutError = IFileManager::Get().Delete(*NewPath, true, false, true)
			? TEXT("The original project could not be removed. The rename was rolled back.")
			: TEXT("The original project could not be removed and the renamed copy could not be cleaned up. Both files may be present.");
		return false;
	}
	OutNewPath = bSameFile ? Entry.FilePath : NewPath;
	return true;
}

bool ProjectLibrary::SoftDelete(const FProjectLibraryEntry& Entry, const FString& Directory, FString& OutDeletedPath, FString& OutError)
{
	OutDeletedPath.Reset();
	OutError.Reset();
	if (Entry.bIsSample)
	{
		OutError = TEXT("The sample files that come with the project cannot be deleted.");
		return false;
	}
	const ProjectFile::FMutationLock Lock;
	if (!Lock.IsValid())
	{
		OutError = TEXT("Another editor is changing a project. Try again.");
		return false;
	}
	if (!IFileManager::Get().FileExists(*Entry.FilePath))
	{
		OutError = TEXT("The project file is already gone.");
		return false;
	}
	const FString DeletedFolder = Directory / TEXT("Deleted");
	IFileManager::Get().MakeDirectory(*DeletedFolder, true);

	FString Target = DeletedFolder / Entry.FileName;
	for (int32 Attempt = 0; IFileManager::Get().FileExists(*Target) && Attempt < 1000; ++Attempt)
	{
		const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
		const FString Suffix = Attempt == 0 ? Stamp : FString::Printf(TEXT("%s-%d"), *Stamp, Attempt + 1);
		Target = DeletedFolder / FString::Printf(TEXT("%s (deleted %s).%s"), *FPaths::GetBaseFilename(Entry.FileName), *Suffix, *FPaths::GetExtension(Entry.FileName));
	}
	if (!ProjectFile::MoveWithoutReplacement(Entry.FilePath, Target))
	{
		OutError = TEXT("The project file could not be moved - it may be open in another program.");
		return false;
	}
	OutDeletedPath = Target;
	return true;
}
