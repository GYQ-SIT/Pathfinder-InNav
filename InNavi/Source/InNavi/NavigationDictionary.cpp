#include "NavigationDictionary.h"
#include "InteriorGenerator.h"
#include "InteriorRoom.h"
#include "NavigationSearchUtils.h"
#include "NavigationSystem.h"
#include "EngineUtils.h"

void FNavigationDictionary::Clear()
{
    Entries.Reset();
}

void FNavigationDictionary::SetEntries(const TArray<FNavigationDestinationEntry>& NewEntries)
{
    Entries = NewEntries;
    Entries.Sort([](const FNavigationDestinationEntry& A, const FNavigationDestinationEntry& B)
    {
        if (A.FloorLevel != B.FloorLevel) return A.FloorLevel < B.FloorLevel;
        return A.Name.Compare(B.Name, ESearchCase::IgnoreCase) < 0;
    });
}

void FNavigationDictionary::Rebuild(const AInteriorGenerator* Interior)
{
    Clear();
    if (!IsValid(Interior) || !Interior->GetWorld() || Interior->GetLoadedBuildingId().IsEmpty()) return;

    // InteriorGenerator does not expose its list of floor numbers. Enumerate
    // room actors and verify ownership via GetRoomById to exclude other rooms.
    TArray<FNavigationDestinationEntry> Found;
    for (TActorIterator<AInteriorRoom> It(Interior->GetWorld()); It; ++It)
    {
        const AInteriorRoom* Room = *It;
        if (!IsValid(Room) || !Room->IsClosedLoop() || Room->RoomId.IsEmpty()) continue;
        if (Interior->GetRoomById(Room->RoomId) != Room) continue;

        FNavigationDestinationEntry Entry;
        Entry.RoomId = Room->RoomId;
        Entry.Name = Room->RoomName.IsEmpty() ? Room->RoomId : Room->RoomName;
        Entry.Category = Room->RoomType;
        Entry.Aliases = NavigationSearch::NormalizeTags(Room->RoomTags);
        Entry.FloorLevel = Room->FloorLevel;
        Found.Add(MoveTemp(Entry));
    }
    SetEntries(Found);
}

TArray<FNavigationDestinationEntry> FNavigationDictionary::Search(const FString& Query, int32 FloorFilter) const
{
    TArray<FNavigationDestinationEntry> Results;
    TArray<FString> Words;
    Query.ParseIntoArrayWS(Words);

    for (const FNavigationDestinationEntry& Entry : Entries)
    {
        if (FloorFilter > 0 && Entry.FloorLevel != FloorFilter) continue;
        FString Searchable = Entry.Name + TEXT(" ") + Entry.Category + TEXT(" ") + Entry.RoomId;
        for (const FString& Alias : Entry.Aliases) Searchable += TEXT(" ") + Alias;
        if (NavigationSearch::MatchesWords(Words, Searchable)) Results.Add(Entry);
    }
    return Results;
}

bool FNavigationDictionary::FindByRoomId(const FString& RoomId, FNavigationDestinationEntry& OutEntry) const
{
    for (const FNavigationDestinationEntry& Entry : Entries)
    {
        if (Entry.RoomId == RoomId)
        {
            OutEntry = Entry;
            return true;
        }
    }
    return false;
}

bool FNavigationDictionary::ResolveDirectGraphNode(const FString& RoomId,
    const FInNaviNavigationSystem& Navigation, FString& OutNodeId) const
{
    OutNodeId.Empty();
    FNavigationDestinationEntry Entry;
    if (!FindByRoomId(RoomId, Entry)) return false;
    FNavigationNode Node;
    if (!Navigation.GetNode(RoomId, Node) || !Node.bWalkable) return false;
    OutNodeId = Node.Id;
    return true;
}
