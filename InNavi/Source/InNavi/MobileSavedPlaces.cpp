#include "MobileSavedPlaces.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool FMobileSavedPlaces::Toggle(const FString& PlaceId)
{
	if (Remove(PlaceId))
	{
		return false;
	}
	Add(PlaceId);
	return true;
}

void FMobileSavedPlaces::Add(const FString& PlaceId)
{
	if (PlaceId.IsEmpty())
	{
		return;
	}
	Order.Remove(PlaceId);
	Order.Insert(PlaceId, 0);
	Changed();
}

bool FMobileSavedPlaces::Remove(const FString& PlaceId)
{
	if (Order.Remove(PlaceId) == 0)
	{
		return false;
	}
	Changed();
	return true;
}

void FMobileSavedPlaces::Clear()
{
	if (!Order.IsEmpty())
	{
		Order.Reset();
		Changed();
	}
}

void FMobileSavedPlaces::Changed()
{
	if (!Path.IsEmpty())
	{
		Save();
	}
}

void FMobileSavedPlaces::UseFile(const FString& InPath)
{
	Path = InPath;
	Load();
}

bool FMobileSavedPlaces::Load()
{
	Order.Reset();
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
	const TArray<TSharedPtr<FJsonValue>>* Saved = nullptr;
	if (Root->TryGetArrayField(TEXT("saved"), Saved) && Saved)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Saved)
		{
			FString Id;
			if (Value.IsValid() && Value->TryGetString(Id) && !Id.IsEmpty() && !Order.Contains(Id))
			{
				Order.Add(Id);
			}
		}
	}
	return true;
}

bool FMobileSavedPlaces::Save() const
{
	if (Path.IsEmpty())
	{
		return false;
	}
	TArray<TSharedPtr<FJsonValue>> Saved;
	for (const FString& Id : Order)
	{
		Saved.Add(MakeShared<FJsonValueString>(Id));
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetArrayField(TEXT("saved"), Saved);
	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(Root, Writer);
	return FFileHelper::SaveStringToFile(Text, *Path);
}

FString FMobileSavedPlaces::DefaultFile()
{
	return FPaths::ProjectSavedDir() / TEXT("MobileSavedPlaces.json");
}
