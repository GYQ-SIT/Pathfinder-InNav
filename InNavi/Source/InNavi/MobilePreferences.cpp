#include "MobilePreferences.h"
#include "ProjectFile.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool FMobilePreferences::SetTheme(EMobileTheme InTheme)
{
	return Apply(InTheme, bLargerText, Volume);
}

bool FMobilePreferences::SetLargerText(bool bInLargerText)
{
	return Apply(Theme, bInLargerText, Volume);
}

bool FMobilePreferences::SetVolume(float InVolume)
{
	return Apply(Theme, bLargerText, InVolume);
}

bool FMobilePreferences::Reset()
{
	return Apply(EMobileTheme::Light, false, 1.f);
}

bool FMobilePreferences::Apply(EMobileTheme InTheme, bool bInLargerText, float InVolume)
{
	InVolume = FMath::IsFinite(InVolume) ? FMath::Clamp(InVolume, 0.f, 1.f) : 1.f;
	Swap(Theme, InTheme);
	Swap(bLargerText, bInLargerText);
	Swap(Volume, InVolume);
	if (Path.IsEmpty() || Save()) return true;
	Swap(Theme, InTheme);
	Swap(bLargerText, bInLargerText);
	Swap(Volume, InVolume);
	return false;
}

void FMobilePreferences::UseFile(const FString& InPath)
{
	Path = InPath;
	Load();
}

bool FMobilePreferences::Load()
{
	Theme = EMobileTheme::Light;
	bLargerText = false;
	Volume = 1.f;
	FString Text;
	if (Path.IsEmpty() || !FFileHelper::LoadFileToString(Text, *Path))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		return false;
	}
	FString ThemeName;
	if (Root->TryGetStringField(TEXT("theme"), ThemeName) && ThemeName == TEXT("night"))
	{
		Theme = EMobileTheme::Night;
	}
	Root->TryGetBoolField(TEXT("largerText"), bLargerText);
	// A file from before the volume has none: full volume. A value that is not a number from 0 to 1 is full volume too.
	double StoredVolume = 1.0;
	if (Root->TryGetNumberField(TEXT("volume"), StoredVolume) && FMath::IsFinite(StoredVolume))
	{
		Volume = static_cast<float>(FMath::Clamp(StoredVolume, 0.0, 1.0));
	}
	return true;
}

bool FMobilePreferences::Save() const
{
	if (Path.IsEmpty())
	{
		return false;
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetStringField(TEXT("theme"), Theme == EMobileTheme::Night ? TEXT("night") : TEXT("light"));
	Root->SetBoolField(TEXT("largerText"), bLargerText);
	Root->SetNumberField(TEXT("volume"), Volume);
	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	return FJsonSerializer::Serialize(Root, Writer) && ProjectFile::WriteText(Path, Text);
}

FString FMobilePreferences::DefaultFile()
{
	return FPaths::ProjectSavedDir() / TEXT("MobilePreferences.json");
}
