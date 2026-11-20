#include "ProjectFile.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
// The Windows headers leave macros behind (LoadImage is LoadImageW): the build joins .cpp files into one, and the macro would rename the engine's
// FImageUtils::LoadImage (declared in ImageUtils.h) in every file after this one.
#ifdef LoadImage
#undef LoadImage
#endif

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
#elif PLATFORM_DESKTOP
#include <unistd.h>
#endif

bool ProjectFile::MoveWithoutReplacement(const FString& Source, const FString& Destination)
{
	IFileManager& Files = IFileManager::Get();
#if PLATFORM_WINDOWS
	return ::MoveFileExW(*ProjectWindowsPath(Files, Source), *ProjectWindowsPath(Files, Destination), MOVEFILE_WRITE_THROUGH) != 0;
#elif PLATFORM_DESKTOP
	const FString From = Files.ConvertToAbsolutePathForExternalAppForWrite(*Source);
	const FString To = Files.ConvertToAbsolutePathForExternalAppForWrite(*Destination);
	if (::link(TCHAR_TO_UTF8(*From), TCHAR_TO_UTF8(*To)) != 0) return false;
	if (::unlink(TCHAR_TO_UTF8(*From)) == 0) return true;
	::unlink(TCHAR_TO_UTF8(*To));
	return false;
#else
	return Files.Move(*Destination, *Source, false);
#endif
}

bool ProjectFile::WriteText(const FString& Path, const FString& Text, bool bReplace)
{
	IFileManager& Files = IFileManager::Get();
	const FString Temp = FPaths::GetPath(Path) / (TEXT(".InNavi-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp"));
	if (FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &Files, FILEWRITE_NoReplaceExisting))
	{
		if (!bReplace && MoveWithoutReplacement(Temp, Path)) return true;
#if PLATFORM_WINDOWS
		const FString Source = ProjectWindowsPath(Files, Temp);
		const FString Destination = ProjectWindowsPath(Files, Path);
		// IFileManager::Move with replacement deletes the destination before trying the move.
		if (bReplace && ::MoveFileExW(*Source, *Destination, MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING))
#else
		if (bReplace && FPlatformFileManager::Get().GetPlatformFile().MoveFile(*Path, *Temp))
#endif
		{
			return true;
		}
	}
	Files.Delete(*Temp, false, false, true);
	return false;
}
