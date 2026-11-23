#include "ProjectLibrary.h"
#include "ProjectFile.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
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
		return FileName.Equals(TEXT("E2_Filled.json"), ESearchCase::IgnoreCase) || FileName.Equals(TEXT("map_extended.db"), ESearchCase::IgnoreCase);
	}

	// Moves or copies a project's cover along with the project. A cover that cannot be moved is left where it is: the project itself matters more.
	void CarryCover(const FString& Directory, const FString& FromFileName, const FString& ToPath, bool bMove)
	{
		const FString From = ProjectLibrary::CoverPathFor(Directory, FromFileName);
		if (!IFileManager::Get().FileExists(*From))
		{
			return;
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ToPath), true);
		if (bMove)
		{
			IFileManager::Get().Move(*ToPath, *From, true, true, false, true);
		}
		else
		{
			IFileManager::Get().Copy(*ToPath, *From, true, true);
		}
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

FString ProjectLibrary::CoverPathFor(const FString& Directory, const FString& FileName)
{
	return Directory / TEXT("Covers") / (FileName + TEXT(".png"));
}

bool ProjectLibrary::SetCover(const FProjectLibraryEntry& Entry, const FString& PicturePath, const FString& Directory, FString& OutError)
{
	OutError.Reset();
	const FString Picture = PicturePath.TrimStartAndEnd();
	const FString Extension = FPaths::GetExtension(Picture).ToLower();
	if (Extension != TEXT("png") && Extension != TEXT("jpg") && Extension != TEXT("jpeg") && Extension != TEXT("bmp"))
	{
		OutError = TEXT("A cover has to be a PNG, JPEG or BMP picture.");
		return false;
	}
	const int64 Size = IFileManager::Get().FileSize(*Picture);
	if (Size < 0)
	{
		OutError = TEXT("The picture could not be found.");
		return false;
	}
	if (Size > MaxCoverFileBytes)
	{
		OutError = TEXT("The picture is too big (the limit is 32 MB).");
		return false;
	}
	FImage Source;
	if (!FImageUtils::LoadImage(*Picture, Source) || Source.SizeX <= 0 || Source.SizeY <= 0)
	{
		OutError = TEXT("The picture could not be read.");
		return false;
	}
	if (Source.SizeX > MaxCoverSide || Source.SizeY > MaxCoverSide)
	{
		OutError = TEXT("The picture is too large (the limit is 16384 pixels on a side).");
		return false;
	}
	Source.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	const TArrayView64<FColor> Pixels = Source.AsBGRA8();

	// The part of the picture the card shows: its middle, in the card's proportions.
	const double Aspect = static_cast<double>(CoverWidth) / CoverHeight;
	double CropW = Source.SizeX, CropH = Source.SizeY;
	if (CropW / CropH > Aspect) CropW = CropH * Aspect; else CropH = CropW / Aspect;
	const double CropX = (Source.SizeX - CropW) * 0.5, CropY = (Source.SizeY - CropH) * 0.5;

	// Scaled down by averaging the pixels each new pixel covers (smooth), up by blending the four nearest (not blocky).
	TArray<FColor> Cover;
	Cover.SetNumUninitialized(CoverWidth * CoverHeight);
	const double StepX = CropW / CoverWidth, StepY = CropH / CoverHeight;
	auto At = [&](int32 X, int32 Y) -> const FColor& { return Pixels[static_cast<int64>(FMath::Clamp(Y, 0, Source.SizeY - 1)) * Source.SizeX + FMath::Clamp(X, 0, Source.SizeX - 1)]; };
	for (int32 Y = 0; Y < CoverHeight; ++Y)
	{
		for (int32 X = 0; X < CoverWidth; ++X)
		{
			const double X0 = CropX + X * StepX, Y0 = CropY + Y * StepY;
			double R = 0, G = 0, B = 0, A = 0;
			if (StepX >= 1.0 && StepY >= 1.0)
			{
				const int32 IX0 = FMath::FloorToInt(X0), IX1 = FMath::Max(IX0 + 1, FMath::CeilToInt(X0 + StepX));
				const int32 IY0 = FMath::FloorToInt(Y0), IY1 = FMath::Max(IY0 + 1, FMath::CeilToInt(Y0 + StepY));
				int32 Count = 0;
				for (int32 SY = IY0; SY < IY1; ++SY)
				{
					for (int32 SX = IX0; SX < IX1; ++SX)
					{
						const FColor& P = At(SX, SY);
						R += P.R; G += P.G; B += P.B; A += P.A;
						++Count;
					}
				}
				R /= Count; G /= Count; B /= Count; A /= Count;
			}
			else
			{
				const double CX = X0 + StepX * 0.5 - 0.5, CY = Y0 + StepY * 0.5 - 0.5;
				const int32 IX = FMath::FloorToInt(CX), IY = FMath::FloorToInt(CY);
				const double FX = CX - IX, FY = CY - IY;
				const FColor& P00 = At(IX, IY); const FColor& P10 = At(IX + 1, IY); const FColor& P01 = At(IX, IY + 1); const FColor& P11 = At(IX + 1, IY + 1);
				auto Mix = [&](uint8 A00, uint8 A10, uint8 A01, uint8 A11) { return (A00 * (1 - FX) + A10 * FX) * (1 - FY) + (A01 * (1 - FX) + A11 * FX) * FY; };
				R = Mix(P00.R, P10.R, P01.R, P11.R); G = Mix(P00.G, P10.G, P01.G, P11.G); B = Mix(P00.B, P10.B, P01.B, P11.B); A = Mix(P00.A, P10.A, P01.A, P11.A);
			}
			Cover[Y * CoverWidth + X] = FColor(static_cast<uint8>(FMath::Clamp(R + 0.5, 0.0, 255.0)), static_cast<uint8>(FMath::Clamp(G + 0.5, 0.0, 255.0)),
				static_cast<uint8>(FMath::Clamp(B + 0.5, 0.0, 255.0)), static_cast<uint8>(FMath::Clamp(A + 0.5, 0.0, 255.0)));
		}
	}

	const FString Target = CoverPathFor(Directory, Entry.FileName);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Target), true);
	// Written beside the old cover first, so a failure leaves the old one in place.
	const FString Temp = FPaths::GetPath(Target) / (FPaths::GetCleanFilename(Entry.FileName) + TEXT(".new.png"));
	if (!FImageUtils::SaveImageByExtension(*Temp, FImageView(Cover.GetData(), CoverWidth, CoverHeight)))
	{
		IFileManager::Get().Delete(*Temp, false, true, true);
		OutError = TEXT("The cover could not be saved - check that the Data folder can be written to.");
		return false;
	}
	if (!IFileManager::Get().Move(*Target, *Temp, true, true, false, true))
	{
		IFileManager::Get().Delete(*Temp, false, true, true);
		OutError = TEXT("The cover could not be saved - check that the Data folder can be written to.");
		return false;
	}
	return true;
}

bool ProjectLibrary::RemoveCover(const FProjectLibraryEntry& Entry, const FString& Directory, FString& OutError)
{
	OutError.Reset();
	const FString Target = CoverPathFor(Directory, Entry.FileName);
	if (!IFileManager::Get().FileExists(*Target))
	{
		return true;
	}
	if (!IFileManager::Get().Delete(*Target, true, true, true))
	{
		OutError = TEXT("The cover could not be removed - it may be open in another program.");
		return false;
	}
	return true;
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
		const FString Cover = CoverPathFor(Directory, Name);
		if (IFileManager::Get().FileExists(*Cover))
		{
			Entry.CoverPath = Cover;
		}

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
	CarryCover(Directory, Entry.FileName, CoverPathFor(Directory, FPaths::GetCleanFilename(CopyPath)), false);
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
	if (!bSameFile)
	{
		CarryCover(Directory, Entry.FileName, CoverPathFor(Directory, FPaths::GetCleanFilename(NewPath)), true);
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
	// The cover goes into the Deleted folder with it, under the name the project got there.
	CarryCover(Directory, Entry.FileName, DeletedFolder / (FPaths::GetCleanFilename(Target) + TEXT(".png")), true);
	OutDeletedPath = Target;
	return true;
}
