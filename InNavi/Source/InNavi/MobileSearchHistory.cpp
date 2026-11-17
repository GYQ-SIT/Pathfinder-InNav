#include "MobileSearchHistory.h"
#include "ProjectFile.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool FMobileSearchHistory::ParseKey(const FString& Key, FString& OutKind, FString& OutId)
{
	int32 Colon = INDEX_NONE;
	if (!Key.FindChar(TEXT(':'), Colon) || Colon <= 0 || Colon + 1 >= Key.Len())
	{
		return false;
	}
	const FString Kind = Key.Left(Colon);
	if (Kind != TEXT("building") && Kind != TEXT("place"))
	{
		return false;
	}
	OutKind = Kind;
	OutId = Key.Mid(Colon + 1);
	return true;
}

FString FMobileSearchHistory::CleanQuery(const FString& Query)
{
	FString Clean = Query.TrimStartAndEnd();
	if (Clean.Len() > MaxQueryLength)
	{
		Clean = Clean.Left(MaxQueryLength).TrimEnd();
	}
	return Clean;
}

bool FMobileSearchHistory::AddChosen(const FString& Key)
{
	FString Kind, Id;
	if (!ParseKey(Key, Kind, Id))
	{
		return false;
	}
	auto NewChosen = ChosenKeys;
	NewChosen.Remove(Key);
	NewChosen.Insert(Key, 0);
	if (NewChosen.Num() > MaxEntries)
	{
		NewChosen.SetNum(MaxEntries);
	}
	return Commit(MoveTemp(NewChosen), QueryList);
}

bool FMobileSearchHistory::AddQuery(const FString& Query)
{
	const FString Clean = CleanQuery(Query);
	if (Clean.IsEmpty())
	{
		return false;
	}
	auto NewQueries = QueryList;
	NewQueries.RemoveAll([&Clean](const FString& Old) { return Old.Equals(Clean, ESearchCase::IgnoreCase); });
	NewQueries.Insert(Clean, 0);
	if (NewQueries.Num() > MaxEntries)
	{
		NewQueries.SetNum(MaxEntries);
	}
	return Commit(ChosenKeys, MoveTemp(NewQueries));
}

bool FMobileSearchHistory::ClearChosen()
{
	return ChosenKeys.IsEmpty() || Commit({}, QueryList);
}

bool FMobileSearchHistory::ClearQueries()
{
	return QueryList.IsEmpty() || Commit(ChosenKeys, {});
}

bool FMobileSearchHistory::ClearAll()
{
	return (ChosenKeys.IsEmpty() && QueryList.IsEmpty()) || Commit({}, {});
}

bool FMobileSearchHistory::Commit(TArray<FString> NewChosen, TArray<FString> NewQueries)
{
	Swap(ChosenKeys, NewChosen);
	Swap(QueryList, NewQueries);
	if (Path.IsEmpty() || Save())
	{
		return true;
	}
	// Not written: the lists are as they were.
	Swap(ChosenKeys, NewChosen);
	Swap(QueryList, NewQueries);
	return false;
}

void FMobileSearchHistory::UseFile(const FString& InPath)
{
	Path = InPath;
	Load();
}

bool FMobileSearchHistory::Load()
{
	ChosenKeys.Reset();
	QueryList.Reset();
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
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (Root->TryGetArrayField(TEXT("chosen"), Values) && Values)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Key, Kind, Id;
			if (Value.IsValid() && Value->TryGetString(Key) && ParseKey(Key, Kind, Id) && !ChosenKeys.Contains(Key) && ChosenKeys.Num() < MaxEntries)
			{
				ChosenKeys.Add(Key);
			}
		}
	}
	if (Root->TryGetArrayField(TEXT("queries"), Values) && Values)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Query;
			if (Value.IsValid() && Value->TryGetString(Query))
			{
				const FString Clean = CleanQuery(Query);
				if (!Clean.IsEmpty() && !QueryList.ContainsByPredicate([&Clean](const FString& Old) { return Old.Equals(Clean, ESearchCase::IgnoreCase); }) && QueryList.Num() < MaxEntries)
				{
					QueryList.Add(Clean);
				}
			}
		}
	}
	return true;
}

bool FMobileSearchHistory::Save() const
{
	if (Path.IsEmpty())
	{
		return false;
	}
	auto ToArray = [](const TArray<FString>& Items)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Item : Items)
		{
			Values.Add(MakeShared<FJsonValueString>(Item));
		}
		return Values;
	};
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetArrayField(TEXT("chosen"), ToArray(ChosenKeys));
	Root->SetArrayField(TEXT("queries"), ToArray(QueryList));
	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	return FJsonSerializer::Serialize(Root, Writer) && ProjectFile::WriteText(Path, Text);
}

FString FMobileSearchHistory::DefaultFile()
{
	return FPaths::ProjectSavedDir() / TEXT("MobileSearchHistory.json");
}
