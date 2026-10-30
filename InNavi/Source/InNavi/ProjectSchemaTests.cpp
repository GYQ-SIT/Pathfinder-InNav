#if WITH_DEV_AUTOMATION_TESTS

#include "UiTestFixture.h"
#include "InteriorGenerator.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

// The editor writes project files, and docs/schemas/project.schema.json says what a project file is. These tests are what keeps the two
// the same: a project the editor has just saved has to be valid against the schema (the CI checks the files of the repository against it
// as well, with scripts/validate_schema.py). The checker here understands the keywords the schema uses and no others.
namespace ProjectSchemaTests
{
	TSharedPtr<FJsonObject> LoadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *Path)) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		return Root;
	}

	FString SchemaPath() { return FPaths::ProjectDir() / TEXT("../docs/schemas/project.schema.json"); }

	const FJsonObject* Resolve(const FJsonObject& Root, const FString& Reference)
	{
		// "#/definitions/name"
		TArray<FString> Parts;
		Reference.RightChop(2).ParseIntoArray(Parts, TEXT("/"));
		const FJsonObject* Node = &Root;
		for (const FString& Part : Parts)
		{
			const TSharedPtr<FJsonObject>* Next = nullptr;
			if (!Node->TryGetObjectField(Part, Next)) return nullptr;
			Node = Next->Get();
		}
		return Node;
	}

	bool IsType(const TSharedPtr<FJsonValue>& Value, const FString& Type)
	{
		if (Type == TEXT("object")) return Value->Type == EJson::Object;
		if (Type == TEXT("array")) return Value->Type == EJson::Array;
		if (Type == TEXT("string")) return Value->Type == EJson::String;
		if (Type == TEXT("boolean")) return Value->Type == EJson::Boolean;
		if (Type == TEXT("number")) return Value->Type == EJson::Number;
		if (Type == TEXT("integer")) return Value->Type == EJson::Number && FMath::IsNearlyEqual(Value->AsNumber(), FMath::RoundToDouble(Value->AsNumber()));
		return false;
	}

	void Check(const TSharedPtr<FJsonValue>& Value, const FJsonObject& Schema, const FJsonObject& Root, const FString& Path, TArray<FString>& Errors)
	{
		FString Reference;
		if (Schema.TryGetStringField(TEXT("$ref"), Reference))
		{
			if (const FJsonObject* Target = Resolve(Root, Reference)) Check(Value, *Target, Root, Path, Errors);
			else Errors.Add(Path + TEXT(": the schema refers to ") + Reference + TEXT(", which is not there"));
			return;
		}
		FString Type;
		if (Schema.TryGetStringField(TEXT("type"), Type) && !IsType(Value, Type))
		{
			Errors.Add(FString::Printf(TEXT("%s: expected %s"), *Path, *Type));
			return;
		}
		const TArray<TSharedPtr<FJsonValue>>* Allowed = nullptr;
		if (Schema.TryGetArrayField(TEXT("enum"), Allowed) && Value->Type == EJson::String)
		{
			const bool bFound = Allowed->ContainsByPredicate([&Value](const TSharedPtr<FJsonValue>& Option) { return Option->AsString() == Value->AsString(); });
			if (!bFound) Errors.Add(FString::Printf(TEXT("%s: \"%s\" is not one of the values the schema allows"), *Path, *Value->AsString()));
		}
		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
			double MinItems = 0, MaxItems = MAX_int32;
			Schema.TryGetNumberField(TEXT("minItems"), MinItems);
			Schema.TryGetNumberField(TEXT("maxItems"), MaxItems);
			if (Items.Num() < MinItems || Items.Num() > MaxItems) Errors.Add(FString::Printf(TEXT("%s: %d items"), *Path, Items.Num()));
			const TSharedPtr<FJsonObject>* ItemSchema = nullptr;
			if (Schema.TryGetObjectField(TEXT("items"), ItemSchema))
			{
				for (int32 Index = 0; Index < Items.Num(); ++Index) Check(Items[Index], **ItemSchema, Root, FString::Printf(TEXT("%s[%d]"), *Path, Index), Errors);
			}
		}
		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* Required = nullptr;
			if (Schema.TryGetArrayField(TEXT("required"), Required))
			{
				for (const TSharedPtr<FJsonValue>& Name : *Required)
				{
					if (!Object->HasField(Name->AsString())) Errors.Add(FString::Printf(TEXT("%s: the field \"%s\" is missing"), *Path, *Name->AsString()));
				}
			}
			const TSharedPtr<FJsonObject>* Properties = nullptr;
			Schema.TryGetObjectField(TEXT("properties"), Properties);
			bool bNoExtras = false;
			Schema.TryGetBoolField(TEXT("additionalProperties"), bNoExtras);
			const bool bExtrasForbidden = Schema.HasTypedField<EJson::Boolean>(TEXT("additionalProperties")) && !bNoExtras;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
			{
				const TSharedPtr<FJsonObject>* FieldSchema = nullptr;
				if (Properties && (*Properties)->TryGetObjectField(Field.Key, FieldSchema)) Check(Field.Value, **FieldSchema, Root, Path + TEXT(".") + Field.Key, Errors);
				else if (bExtrasForbidden) Errors.Add(FString::Printf(TEXT("%s: the field \"%s\" is not in the schema"), *Path, *Field.Key));
			}
		}
	}

	TArray<FString> Errors(const TSharedPtr<FJsonObject>& Project)
	{
		TArray<FString> Result;
		const TSharedPtr<FJsonObject> Schema = LoadJson(SchemaPath());
		if (!Schema.IsValid() || !Project.IsValid())
		{
			Result.Add(TEXT("the schema or the project could not be read"));
			return Result;
		}
		Check(MakeShared<FJsonValueObject>(Project), *Schema, *Schema, TEXT("$"), Result);
		return Result;
	}
}

// What the editor saves is what the schema says: a project with generated floors, rooms with doors, lifts, and doors in the outer wall.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSchemaSavedTest, "InNavi.Project.Schema.SavedProjectIsValid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectSchemaSavedTest::RunTest(const FString& Parameters)
{
	using namespace ProjectSchemaTests;
	FNavigationSearchFixture Fixture;
	const double Units = 100.0 / Fixture.Generator->GetScale();
	const TArray<FVector2D> Outline = { FVector2D(0, 0), FVector2D(60 * Units, 0), FVector2D(60 * Units, 30 * Units), FVector2D(0, 30 * Units) };
	Fixture.Generator->SpawnBuildingFromData(TEXT("SCHEMA"), TEXT("Schema test"), Outline, 2, 300.f, FVector2D(9000.0, 9000.0));
	Fixture.Interior->LoadInteriorForBuilding(TEXT("SCHEMA"));
	if (Fixture.Interior->GetActiveFloor() < 1) Fixture.Interior->AddFloor();
	FString Error;
	Fixture.Interior->SetActiveFloor(1);
	// A door in the outer wall, on the bottom edge: the point is where the wall is, in the building's footprint space.
	FString Entrance = Fixture.Interior->AddEntranceAtPoint(FVector2D(30 * Units, 0.0), 15.f, Error);
	if (Entrance.IsEmpty()) Entrance = Fixture.Interior->AddEntranceAtPoint(FVector2D(9000.0 + 30 * Units, 9000.0), 15.f, Error);
	if (!TestFalse(*FString::Printf(TEXT("a door in the outer wall is added (%s)"), *Error), Entrance.IsEmpty())) return false;
	for (int32 Level = 1; Level <= 2; ++Level)
	{
		Fixture.Interior->SetActiveFloor(Level);
		FFloorGenerationResult Generated;
		if (!TestTrue(*FString::Printf(TEXT("floor %d generates"), Level), Fixture.Interior->GenerateFloor(Level, Generated))) return false;
	}
	Fixture.Generator->SetInteriorDataForBuilding(TEXT("SCHEMA"), Fixture.Interior->CaptureInteriorSnapshot());

	const FString Path = FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / (TEXT("schema-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
	if (!TestTrue(TEXT("the project is saved"), Fixture.Generator->SaveBuildingsToFile(Path))) return false;
	const TSharedPtr<FJsonObject> Saved = LoadJson(Path);
	IFileManager::Get().Delete(*Path);
	if (!TestTrue(TEXT("and read back"), Saved.IsValid())) return false;

	const TArray<FString> Found = Errors(Saved);
	for (const FString& Problem : Found) AddError(TEXT("the saved project does not match docs/schemas/project.schema.json: ") + Problem);
	TestTrue(TEXT("the saved project says its version"), Saved->HasTypedField<EJson::Number>(TEXT("version")));
	// It has what is worth checking: rooms with doors, a lift, and a door in the outer wall.
	bool bRoom = false, bDoor = false, bNode = false, bEntrance = false;
	for (const TSharedPtr<FJsonValue>& BuildingValue : Saved->GetArrayField(TEXT("buildings")))
	{
		const TSharedPtr<FJsonObject>* Interior = nullptr;
		if (!BuildingValue->AsObject()->TryGetObjectField(TEXT("interior"), Interior)) continue;
		const TArray<TSharedPtr<FJsonValue>>* Entrances = nullptr;
		bEntrance |= (*Interior)->TryGetArrayField(TEXT("entrances"), Entrances) && !Entrances->IsEmpty();
		for (const TSharedPtr<FJsonValue>& Floor : (*Interior)->GetArrayField(TEXT("floors")))
		{
			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			bNode |= Floor->AsObject()->TryGetArrayField(TEXT("navNodes"), Nodes) && !Nodes->IsEmpty();
			for (const TSharedPtr<FJsonValue>& Room : Floor->AsObject()->GetArrayField(TEXT("rooms")))
			{
				bRoom = true;
				const TArray<TSharedPtr<FJsonValue>>* Openings = nullptr;
				bDoor |= Room->AsObject()->TryGetArrayField(TEXT("openings"), Openings) && !Openings->IsEmpty();
			}
		}
	}
	TestTrue(TEXT("the saved project has rooms"), bRoom);
	TestTrue(TEXT("with doors"), bDoor);
	TestTrue(TEXT("a connector"), bNode);
	TestTrue(TEXT("and a door in the outer wall"), bEntrance);
	return true;
}

// The checker itself: the mistakes the schema is there to catch are caught here as well, with where they are.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSchemaMistakesTest, "InNavi.Project.Schema.MistakesAreReported", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectSchemaMistakesTest::RunTest(const FString& Parameters)
{
	using namespace ProjectSchemaTests;
	const FString ProjectPath = FPaths::ProjectDir() / TEXT("../docs/test-data/entrance-test-2-with-doors.json");
	TestEqual(TEXT("the test project is valid"), Errors(LoadJson(ProjectPath)).Num(), 0);

	auto Mistake = [&](const TCHAR* What, TFunctionRef<void(const TSharedPtr<FJsonObject>&)> Make, const TCHAR* Expected)
	{
		const TSharedPtr<FJsonObject> Project = LoadJson(ProjectPath);
		Make(Project);
		const TArray<FString> Found = Errors(Project);
		TestTrue(*FString::Printf(TEXT("%s is reported (%s)"), What, *FString::Join(Found, TEXT("; "))), Found.ContainsByPredicate([Expected](const FString& Error) { return Error.Contains(Expected); }));
	};
	auto FirstBuilding = [](const TSharedPtr<FJsonObject>& Project) { return Project->GetArrayField(TEXT("buildings"))[0]->AsObject(); };
	auto FirstRoom = [&](const TSharedPtr<FJsonObject>& Project) { return FirstBuilding(Project)->GetObjectField(TEXT("interior"))->GetArrayField(TEXT("floors"))[0]->AsObject()->GetArrayField(TEXT("rooms"))[0]->AsObject(); };

	Mistake(TEXT("a project without buildings"), [](const TSharedPtr<FJsonObject>& P) { P->RemoveField(TEXT("buildings")); }, TEXT("\"buildings\" is missing"));
	Mistake(TEXT("a building without a footprint"), [&](const TSharedPtr<FJsonObject>& P) { FirstBuilding(P)->RemoveField(TEXT("footprint")); }, TEXT("\"footprint\" is missing"));
	Mistake(TEXT("a field the schema does not know"), [&](const TSharedPtr<FJsonObject>& P) { FirstBuilding(P)->SetStringField(TEXT("colour"), TEXT("red")); }, TEXT("\"colour\" is not in the schema"));
	Mistake(TEXT("a room that is neither public nor private"), [&](const TSharedPtr<FJsonObject>& P) { FirstRoom(P)->SetStringField(TEXT("access"), TEXT("staff")); }, TEXT("\"staff\" is not one of"));
	Mistake(TEXT("an opening of a kind that does not exist"), [&](const TSharedPtr<FJsonObject>& P) { FirstRoom(P)->GetArrayField(TEXT("openings"))[0]->AsObject()->SetStringField(TEXT("type"), TEXT("gate")); }, TEXT("\"gate\" is not one of"));
	Mistake(TEXT("a number where a string is wanted"), [&](const TSharedPtr<FJsonObject>& P) { FirstBuilding(P)->SetNumberField(TEXT("id"), 7); }, TEXT("expected string"));
	return true;
}

#endif
