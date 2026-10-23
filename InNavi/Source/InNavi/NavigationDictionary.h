#pragma once

#include "CoreMinimal.h"

class AInteriorGenerator;
class FInNaviNavigationSystem;

// Searchable metadata for one real, closed room in the loaded building.
struct FNavigationDestinationEntry
{
    FString RoomId;
    FString Name;
    FString Category;
    TArray<FString> Aliases;
    int32 FloorLevel = 1;
};

// Plain C++ service: no UObject ownership or changes to existing project files.
class INNAVI_API FNavigationDictionary
{
public:
    // Rebuild after loading a building or editing rooms (including undo/redo).
    void Rebuild(const AInteriorGenerator* Interior);
    void Clear();

    // Also useful for independent unit tests.
    void SetEntries(const TArray<FNavigationDestinationEntry>& NewEntries);

    // Empty query returns all entries. FloorFilter <= 0 means all floors.
    TArray<FNavigationDestinationEntry> Search(const FString& Query, int32 FloorFilter = 0) const;
    bool FindByRoomId(const FString& RoomId, FNavigationDestinationEntry& OutEntry) const;
    const TArray<FNavigationDestinationEntry>& GetAll() const { return Entries; }

    // Only succeeds if the graph has a walkable node whose ID equals RoomId.
    // A separate room-to-node mapping is required if graph IDs differ.
    bool ResolveDirectGraphNode(const FString& RoomId,
        const FInNaviNavigationSystem& Navigation, FString& OutNodeId) const;

private:
    TArray<FNavigationDestinationEntry> Entries;
};
