#include "BuildingJsonWriter.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"


bool UBuildingJsonWriter::SaveBuildingsToJson(
    const TArray<FBuildingData>& Buildings)
{
    // Create the main/root JSON object
    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();

    // This will contain all of our buildings
    TArray<TSharedPtr<FJsonValue>> BuildingsArray;


    // Go through every building we received
    for (const FBuildingData& Building : Buildings)
    {
        TSharedPtr<FJsonObject> BuildingObject = MakeShared<FJsonObject>();

        BuildingObject->SetStringField(TEXT("id"), Building.Id);
        BuildingObject->SetStringField(TEXT("name"), Building.Name);

        BuildingObject->SetNumberField(TEXT("originX"), Building.OriginX);
        BuildingObject->SetNumberField(TEXT("originY"), Building.OriginY);

        BuildingObject->SetNumberField(TEXT("width"), Building.Width);
        BuildingObject->SetNumberField(TEXT("depth"), Building.Depth);

        BuildingObject->SetNumberField(TEXT("floorCount"), Building.FloorCount);
        BuildingObject->SetNumberField(TEXT("floorHeight"), Building.FloorHeight);

        BuildingsArray.Add(
            MakeShared<FJsonValueObject>(BuildingObject)
        );
    }


    // Create:
    // "buildings": [...]
    RootObject->SetArrayField(TEXT("buildings"), BuildingsArray);


    // Convert the JSON object into text
    FString OutputString;

    TSharedRef<TJsonWriter<>> Writer =
        TJsonWriterFactory<>::Create(&OutputString);

    if (!FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to serialize buildings JSON"));
        return false;
    }


    // Save to:
    // <Project Folder>/Data/buildings.json
    FString FilePath =
        FPaths::ProjectDir() / TEXT("Data") / TEXT("buildings.json");


    if (!FFileHelper::SaveStringToFile(OutputString, *FilePath))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("Failed to save buildings JSON: %s"),
            *FilePath
        );

        return false;
    }


    UE_LOG(
        LogTemp,
        Warning,
        TEXT("Saved %d buildings to: %s"),
        Buildings.Num(),
        *FilePath
    );

    return true;
}
bool UBuildingJsonWriter::LoadBuildingsFromJson(
    TArray<FBuildingData>& Buildings)
{
    // Location of the JSON file
    const FString FilePath =
        FPaths::ProjectDir() / TEXT("Data") / TEXT("buildings.json");

    // Read JSON file into a string
    FString JsonString;

    if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("Failed to load buildings JSON: %s"),
            *FilePath
        );

        return false;
    }

    // Convert JSON text into a JSON object
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, RootObject) ||
        !RootObject.IsValid())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("Failed to parse buildings JSON")
        );

        return false;
    }

    // Find the "buildings" array
    const TArray<TSharedPtr<FJsonValue>>* BuildingsArray;

    if (!RootObject->TryGetArrayField(
        TEXT("buildings"),
        BuildingsArray))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("JSON does not contain a buildings array")
        );

        return false;
    }

    // Remove anything currently inside the output array
    Buildings.Empty();

    // Convert each JSON building back into FBuildingData
    for (const TSharedPtr<FJsonValue>& BuildingValue : *BuildingsArray)
    {
        const TSharedPtr<FJsonObject> BuildingObject =
            BuildingValue->AsObject();

        if (!BuildingObject.IsValid())
        {
            continue;
        }

        FBuildingData Building;

        Building.Id =
            BuildingObject->GetStringField(TEXT("id"));

        Building.Name =
            BuildingObject->GetStringField(TEXT("name"));

        Building.OriginX =
            BuildingObject->GetNumberField(TEXT("originX"));

        Building.OriginY =
            BuildingObject->GetNumberField(TEXT("originY"));

        Building.Width =
            BuildingObject->GetNumberField(TEXT("width"));

        Building.Depth =
            BuildingObject->GetNumberField(TEXT("depth"));

        Building.FloorCount =
            BuildingObject->GetIntegerField(TEXT("floorCount"));

        Building.FloorHeight =
            BuildingObject->GetNumberField(TEXT("floorHeight"));

        Buildings.Add(Building);
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("Loaded %d buildings from: %s"),
        Buildings.Num(),
        *FilePath
    );

    return true;
}
bool UBuildingJsonWriter::SaveInteriorNavNodesToJson(
    const TArray<FInteriorNavNode>& NavNodes)
{
    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();

    TArray<TSharedPtr<FJsonValue>> NavNodesArray;

    for (const FInteriorNavNode& Node : NavNodes)
    {
        TSharedPtr<FJsonObject> NodeObject = MakeShared<FJsonObject>();

        NodeObject->SetStringField(TEXT("id"), Node.Id);
        FString TypeString;

        switch (Node.Type)
        {
        case EInteriorNavNodeType::PathNode:
            TypeString = TEXT("pathNode");
            break;

        case EInteriorNavNodeType::Elevator:
            TypeString = TEXT("elevator");
            break;

        case EInteriorNavNodeType::Stairs:
            TypeString = TEXT("stairs");
            break;

        case EInteriorNavNodeType::Escalator:
            TypeString = TEXT("escalator");
            break;

        default:
            TypeString = TEXT("pathNode");
            break;
        }

        NodeObject->SetStringField(TEXT("type"), TypeString);
        NodeObject->SetNumberField(TEXT("level"), Node.Level);

        // Position
        TSharedPtr<FJsonObject> PositionObject = MakeShared<FJsonObject>();

        PositionObject->SetNumberField(
            TEXT("x"),
            Node.Position.X
        );

        PositionObject->SetNumberField(
            TEXT("y"),
            Node.Position.Y
        );

        NodeObject->SetObjectField(
            TEXT("position"),
            PositionObject
        );

        // Connected floors
        TArray<TSharedPtr<FJsonValue>> FloorsArray;

        for (int32 Floor : Node.ConnectsFloors)
        {
            FloorsArray.Add(
                MakeShared<FJsonValueNumber>(Floor)
            );
        }

        NodeObject->SetArrayField(
            TEXT("connectsFloors"),
            FloorsArray
        );

        NodeObject->SetStringField(
            TEXT("connectorGroupId"),
            Node.ConnectorGroupId
        );

        NavNodesArray.Add(
            MakeShared<FJsonValueObject>(NodeObject)
        );
    }

    RootObject->SetArrayField(
        TEXT("interiorNavNodes"),
        NavNodesArray
    );

    FString OutputString;

    TSharedRef<TJsonWriter<>> Writer =
        TJsonWriterFactory<>::Create(&OutputString);

    if (!FJsonSerializer::Serialize(
        RootObject.ToSharedRef(),
        Writer))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("Failed to serialize interior nav nodes")
        );

        return false;
    }

    const FString FilePath =
        FPaths::ProjectDir()
        / TEXT("Data")
        / TEXT("interior_nav_nodes.json");

    if (!FFileHelper::SaveStringToFile(
        OutputString,
        *FilePath))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("Failed to save interior nav nodes: %s"),
            *FilePath
        );

        return false;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("Saved %d interior nav nodes to: %s"),
        NavNodes.Num(),
        *FilePath
    );

    return true;
}