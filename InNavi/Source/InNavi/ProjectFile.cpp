#include "ProjectFile.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"

static FString ProjectWindowsPath(IFileManager& Files, const FString& Path)
{
	FString Result = Files.ConvertToAbsolutePathForExternalAppForWrite(*Path);
	FPaths::MakePlatformFilename(Result);
	if (Result.Len() >= MAX_PATH && !Result.StartsWith(TEXT("\\\\?\\")))
	{
		return Result.StartsWith(TEXT("\\\\"))
			? TEXT("\\\\?\\UNC\\") + Result.Mid(2)
			: TEXT("\\\\?\\") + Result;
	}
	return Result;
}
#endif

bool ProjectFile::WriteText(const FString& Path, const FString& Text, bool bReplace)
{
	IFileManager& Files = IFileManager::Get();
	const FString Temp = FPaths::GetPath(Path) / (TEXT(".InNavi-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp"));
	if (FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &Files, FILEWRITE_NoReplaceExisting))
	{
#if PLATFORM_WINDOWS
		const FString Source = ProjectWindowsPath(Files, Temp);
		const FString Destination = ProjectWindowsPath(Files, Path);
		// IFileManager::Move with replacement deletes the destination before trying the move.
		const DWORD Flags = MOVEFILE_WRITE_THROUGH | (bReplace ? MOVEFILE_REPLACE_EXISTING : 0);
		if (::MoveFileExW(*Source, *Destination, Flags))
#else
		if ((bReplace || !Files.FileExists(*Path)) && FPlatformFileManager::Get().GetPlatformFile().MoveFile(*Path, *Temp))
#endif
		{
			return true;
		}
	}
	Files.Delete(*Temp, false, false, true);
	return false;
}
