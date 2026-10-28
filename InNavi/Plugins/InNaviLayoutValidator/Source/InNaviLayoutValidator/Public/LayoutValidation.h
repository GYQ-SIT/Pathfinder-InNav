#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace LayoutValidation
{
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
    INNAVILAYOUTVALIDATOR_API FReport ValidateFile(const FString& Path);
    INNAVILAYOUTVALIDATOR_API FString Fingerprint(const TSharedPtr<FJsonObject>& Project);
    INNAVILAYOUTVALIDATOR_API bool ExportReport(const FReport& Report, const FString& Path);
}
