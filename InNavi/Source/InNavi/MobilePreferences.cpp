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
	return Apply(InTheme, bLargerText);
}

bool FMobilePreferences::SetLargerText(bool bInLargerText)
{
	return Apply(Theme, bInLargerText);
}

bool FMobilePreferences::Reset()
{
	return Apply(EMobileTheme::Light, false);
}

bool FMobilePreferences::Apply(EMobileTheme InTheme, bool bInLargerText)
{
	Swap(Theme, InTheme);
	Swap(bLargerText, bInLargerText);
	if (Path.IsEmpty() || Save()) return true;
	Swap(Theme, InTheme);
	Swap(bLargerText, bInLargerText);
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
	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	return FJsonSerializer::Serialize(Root, Writer) && ProjectFile::WriteText(Path, Text);
}

FString FMobilePreferences::DefaultFile()
{
	return FPaths::ProjectSavedDir() / TEXT("MobilePreferences.json");
}
