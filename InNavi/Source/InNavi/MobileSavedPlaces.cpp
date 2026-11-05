#include "MobileSavedPlaces.h"
#include "ProjectFile.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool FMobileSavedPlaces::Toggle(const FString& PlaceId, bool* OutPersisted)
{
	const bool bPersisted = Contains(PlaceId) ? Remove(PlaceId) : Add(PlaceId);
	if (OutPersisted) *OutPersisted = bPersisted;
	return Contains(PlaceId);
}

bool FMobileSavedPlaces::Add(const FString& PlaceId)
{
	if (PlaceId.IsEmpty()) return false;
	auto NewOrder = Order;
	NewOrder.Remove(PlaceId);
	NewOrder.Insert(PlaceId, 0);
	return Commit(MoveTemp(NewOrder));
}

bool FMobileSavedPlaces::Remove(const FString& PlaceId)
{
	auto NewOrder = Order;
	return NewOrder.Remove(PlaceId) != 0 && Commit(MoveTemp(NewOrder));
}

bool FMobileSavedPlaces::Clear()
{
	return Order.IsEmpty() || Commit({});
}

bool FMobileSavedPlaces::Commit(TArray<FString> NewOrder)
{
	Swap(Order, NewOrder);
	if (Path.IsEmpty() || Save()) return true;
	Swap(Order, NewOrder);
	return false;
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
	return FJsonSerializer::Serialize(Root, Writer) && ProjectFile::WriteText(Path, Text);
}

FString FMobileSavedPlaces::DefaultFile()
{
	return FPaths::ProjectSavedDir() / TEXT("MobileSavedPlaces.json");
}
