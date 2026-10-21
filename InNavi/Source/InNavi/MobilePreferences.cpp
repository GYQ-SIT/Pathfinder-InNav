#include "MobilePreferences.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void FMobilePreferences::SetTheme(EMobileTheme InTheme)
{
	Theme = InTheme;
	Changed();
}

void FMobilePreferences::SetLargerText(bool bInLargerText)
{
	bLargerText = bInLargerText;
	Changed();
}

void FMobilePreferences::Reset()
{
	Theme = EMobileTheme::Light;
	bLargerText = false;
	Changed();
}

void FMobilePreferences::Changed()
{
	if (!Path.IsEmpty())
	{
		Save();
	}
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
	FJsonSerializer::Serialize(Root, Writer);
	return FFileHelper::SaveStringToFile(Text, *Path);
}

FString FMobilePreferences::DefaultFile()
{
	return FPaths::ProjectSavedDir() / TEXT("MobilePreferences.json");
}
