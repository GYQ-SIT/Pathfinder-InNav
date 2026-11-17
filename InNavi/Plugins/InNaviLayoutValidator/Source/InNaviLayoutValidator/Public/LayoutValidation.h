#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace LayoutValidation
{
    inline constexpr int32 MaxBuildingFloors = 200;
    // How far down a building may go: level -20 is B20.
    inline constexpr int32 MaxBasementFloors = 20;

    // Floor levels. 1 is the ground floor, 2, 3 ... go up, and the basements are -1 (B1), -2 (B2) ... going down. There is no level 0 (0 is
    // "no floor" in several places), so the floors of a building are not simply first..last: use these, never "Level++" or "Level - 1".
    inline constexpr bool IsValidLevel(int32 Level) { return Level != 0 && Level >= -MaxBasementFloors && Level <= MaxBuildingFloors; }
    // How many floors a level is above the ground floor: level 1 is 0, level 2 is 1, B1 (-1) is -1. A floor's bottom is this times the floor height.
    inline constexpr int32 LevelIndex(int32 Level) { return Level > 0 ? Level - 1 : Level; }
    inline constexpr int32 LevelFromIndex(int32 Index) { return Index >= 0 ? Index + 1 : Index; }
    // The level Steps floors above Level (below for a negative Steps): LevelAfter(-1, 1) is 1, LevelAfter(1, -1) is -1.
    inline constexpr int32 LevelAfter(int32 Level, int32 Steps) { return LevelFromIndex(LevelIndex(Level) + Steps); }
    // The number of floors from First up to Last (0 when Last is below First).
    inline constexpr int32 LevelSpan(int32 First, int32 Last) { return LevelIndex(Last) >= LevelIndex(First) ? LevelIndex(Last) - LevelIndex(First) + 1 : 0; }
    // What the floor is called: "B1" for -1, "3" for 3.
    inline FString LevelLabel(int32 Level) { return Level < 0 ? FString::Printf(TEXT("B%d"), -Level) : FString::FromInt(Level); }

    enum class ESeverity : uint8 { Error, Warning, Info };
    enum class EObjectKind : uint8 { Project, Building, Floor, Room, Opening, Connector, Entrance };

    struct INNAVILAYOUTVALIDATOR_API FIssue
    {
        FString Rule;
        ESeverity Severity = ESeverity::Error;
        EObjectKind Kind = EObjectKind::Project;
        FString BuildingId;
        int32 Floor = INDEX_NONE;
        FString ObjectId;
        FString OwnerId;
        FString JsonPath;
        FString Message;
        FString Suggestion;
        FVector2D Position = FVector2D::ZeroVector;
        bool bHasPosition = false;

        FString Key() const;
    };

    struct INNAVILAYOUTVALIDATOR_API FReport
    {
        TArray<FIssue> Issues;
        FString Source;
        FString Fingerprint;
        FString ScopeBuildingId;
        bool bNavigationChecked = false;
        int32 RoomsChecked = 0;
        int32 RoomsSkipped = 0;

        int32 Count(ESeverity Severity) const;
        bool HasErrors() const { return Count(ESeverity::Error) > 0; }
        void Sort();
        void FilterToBuilding(const FString& BuildingId);
        TSharedRef<FJsonObject> ToJson() const;
    };

    INNAVILAYOUTVALIDATOR_API const TCHAR* SeverityName(ESeverity Severity);
    // These functions only read their input. File validation runs before actor loading can discard bad entries.
    INNAVILAYOUTVALIDATOR_API FReport ValidateProject(const TSharedPtr<FJsonObject>& Project);
    // Returns an empty report if cancellation is requested. The callback must stay valid for the call.
    INNAVILAYOUTVALIDATOR_API FReport ValidateProject(const TSharedPtr<FJsonObject>& Project, TFunctionRef<bool()> ShouldCancel);
    INNAVILAYOUTVALIDATOR_API FReport ValidateFile(const FString& Path);
    INNAVILAYOUTVALIDATOR_API FString Fingerprint(const TSharedPtr<FJsonObject>& Project);
    INNAVILAYOUTVALIDATOR_API bool ExportReport(const FReport& Report, const FString& Path);
}
