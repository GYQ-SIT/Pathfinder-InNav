#include "MobileRealSource.h"

#include "Algo/Reverse.h"

#include "BuildingGenerator.h"
#include "InteriorGenerator.h"
#include "LayoutValidation.h"
#include "LayoutValidationSession.h"
#include "NavigationRoutePlanner.h"
#include "NavigationSearchUtils.h"
#include "NavPlayerController.h"
#include "PolygonGeometryUtils.h"
#include "ProjectLibrary.h"
#include "RoomTypes.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace MobileRealData
{
	// No actor or world is retained by a plain C++ source held by a UMG widget. In particular,
	// widget garbage collection cannot leave a rooted routing world behind.
	struct FRouteWorld
	{
		UWorld* World = nullptr;
		ABuildingGenerator* Generator = nullptr;
		AInteriorGenerator* Interior = nullptr;
		FRouteWorld()
		{
			if (!GEngine) return;
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!World) return;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Generator = World->SpawnActor<ABuildingGenerator>();
			Interior = World->SpawnActor<AInteriorGenerator>();
			if (Interior) Interior->DispatchBeginPlay();
		}
		~FRouteWorld()
		{
			if (World) { World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); }
		}
		bool IsReady() const { return Generator && Interior; }
	};

	FString String(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, const FString& Fallback = FString())
	{
		FString Result; return Object && Object->TryGetStringField(Key, Result) ? Result : Fallback;
	}
	double Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Fallback)
	{
		double Result; return Object && Object->TryGetNumberField(Key, Result) && FMath::IsFinite(Result) ? Result : Fallback;
	}
	const TArray<TSharedPtr<FJsonValue>>& Array(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
	{
		static const TArray<TSharedPtr<FJsonValue>> Empty;
		const TArray<TSharedPtr<FJsonValue>>* Result = nullptr;
		return Object && Object->TryGetArrayField(Key, Result) ? *Result : Empty;
	}
	TSharedPtr<FJsonObject> Object(const TSharedPtr<FJsonValue>& Value)
	{
		return Value && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	}
	TSharedPtr<FJsonObject> Child(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Result = nullptr;
		return Parent && Parent->TryGetObjectField(Key, Result) ? *Result : nullptr;
	}
	bool XY(const TSharedPtr<FJsonValue>& Value, FVector2D& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
		double X, Y;
		if (!Value || !Value->TryGetArray(Pair) || Pair->Num() != 2 || !(*Pair)[0]->TryGetNumber(X)
			|| !(*Pair)[1]->TryGetNumber(Y) || !FMath::IsFinite(X) || !FMath::IsFinite(Y)) return false;
		Out = {X, Y}; return true;
	}
	double Area(const TArray<FVector2D>& Points)
	{
		double Sum = 0;
		for (int32 I = 0; I < Points.Num(); ++I) Sum += FVector2D::CrossProduct(Points[I], Points[(I + 1) % Points.Num()]);
		return Sum * 0.5;
	}
	TArray<FVector2D> Polygon(const TSharedPtr<FJsonObject>& Json)
	{
		TArray<FVector2D> Points;
		for (const auto& Value : Array(Json, TEXT("footprint")))
		{
			FVector2D P; if (!XY(Value, P)) return {}; Points.Add(P);
		}
		if (Points.Num() > 1 && Points[0].Equals(Points.Last())) Points.Pop();
		if (Area(Points) < 0) Algo::Reverse(Points);
		return Points;
	}
	FVector2D InteriorPoint(const TArray<FVector2D>& Points)
	{
		TArray<int32> Triangles; TriangulatePolygon(Points, Triangles);
		double Best = -1; FVector2D Result = FVector2D::ZeroVector;
		for (int32 I = 0; I + 2 < Triangles.Num(); I += 3)
		{
			const auto A = Points[Triangles[I]], B = Points[Triangles[I + 1]], C = Points[Triangles[I + 2]];
			const double Size = FMath::Abs(FVector2D::CrossProduct(B - A, C - A));
			if (Size > Best) { Best = Size; Result = (A + B + C) / 3.0; }
		}
		return Result;
	}
	FString Escape(const FString& Id)
	{
		return Id.Replace(TEXT("%"), TEXT("%25")).Replace(TEXT("/"), TEXT("%2F"));
	}
	FString Category(const FString& Type)
	{
		if (Type == TEXT("Canteen")) return TEXT("Dining");
		if (Type == TEXT("Classroom") || Type == TEXT("Lecture hall") || Type == TEXT("Lab") || Type == TEXT("Study area")) return TEXT("Learning");
		if (Type == TEXT("Office") || Type == TEXT("Meeting room")) return TEXT("Workspaces");
		return Type == TEXT("Other") ? TEXT("Other") : TEXT("Facilities");
	}
	FMobileRoute Failure(const FString& Message) { FMobileRoute Result; Result.Error = Message; return Result; }
}

struct FMobileRealSource::FProject
{
	FMobileSite Site;
	TSharedPtr<FJsonObject> Json;
	TArray<FMobileBuilding> Buildings;
	TArray<FMobilePlace> Places;
	TArray<FString> Categories;
	TMap<FString, FString> SearchText;
	TMap<FString, FString> RawBuildingIds;
	TMap<FString, TMap<int32, FMobileLevelMap>> Maps;
	TMap<FString, TArray<FVector2D>> RoomShapes;
	FMobileCampusMap Campus;
};

FMobileRealSource::~FMobileRealSource() = default;

FMobileRealSource::FMobileRealSource(const FString& ProjectPath, const FString& OverridesDirectory)
{
	if (IFileManager::Get().DirectoryExists(*ProjectPath))
	{
		auto Entries = ProjectLibrary::Scan(ProjectPath);
		// Prefer the authored JSON to a footprint-only database, independently of file modification time.
		Entries.Sort([](const auto& A, const auto& B)
		{
			if (A.bIsDatabase != B.bIsDatabase) return !A.bIsDatabase;
			return A.FileName < B.FileName;
		});
		for (const auto& Entry : Entries) AddProject(Entry.FilePath);
	}
	else AddProject(ProjectPath);
	if (!OverridesDirectory.IsEmpty()) for (const auto& Entry : ProjectLibrary::Scan(OverridesDirectory)) AddProject(Entry.FilePath);
}

FMobileRealSource::FMobileRealSource(ABuildingGenerator* Generator, AInteriorGenerator* Interior, const FString& ProjectFile)
{
	if (!IsValid(Generator) || !IsValid(Interior)) return;
	auto Project = MakeShared<FProject>();
	Project->Site.Id = TEXT("project/") + MobileRealData::Escape(ProjectFile.IsEmpty() ? TEXT("current") : FPaths::GetCleanFilename(ProjectFile).ToLower());
	Project->Json = ULayoutValidationSession::Capture(*Generator, *Interior);
	Project->Site.Name = ProjectFile.IsEmpty() ? TEXT("Current project") : FPaths::GetBaseFilename(ProjectFile);
	IndexProject(*Project); Projects.Add(Project);
}

TSharedRef<IMobileNavSource> FMobileRealSource::FromWorld(UWorld* World)
{
	FString ExplicitProject;
	if (FParse::Value(FCommandLine::Get(), TEXT("MobileProject="), ExplicitProject))
		return MakeShared<FMobileRealSource>(FPaths::ConvertRelativePathToFull(ExplicitProject));
	if (World)
	{
		ABuildingGenerator* Generator = nullptr; AInteriorGenerator* Interior = nullptr;
		if (TActorIterator<ABuildingGenerator> It(World); It) Generator = *It;
		if (TActorIterator<AInteriorGenerator> It(World); It) Interior = *It;
		if (Generator && Interior && !Generator->GetAllBuildings().IsEmpty())
		{
			FString Path;
			if (auto* Controller = Cast<ANavPlayerController>(World->GetFirstPlayerController())) Path = Controller->GetCurrentProjectFilePath();
			if (!Path.IsEmpty()) return MakeShared<FMobileRealSource>(Generator, Interior, Path);
		}
	}
	return MakeShared<FMobileRealSource>(FPaths::ProjectDir() / TEXT("Data"), FPaths::ProjectSavedDir() / TEXT("MobileProjects"));
}

void FMobileRealSource::AddProject(const FString& Path)
{
	using namespace MobileRealData;
	auto Project = MakeShared<FProject>();
	Project->Site.Id = TEXT("project/") + Escape(FPaths::GetCleanFilename(Path).ToLower());
	Project->Site.Name = FPaths::GetBaseFilename(Path);
	if (FPaths::GetExtension(Path).Equals(TEXT("db"), ESearchCase::IgnoreCase))
	{
		FRouteWorld Context;
		if (!Context.IsReady() || Context.Generator->LoadBuildingsFromDatabase(Path) == 0) return;
		Project->Json = ULayoutValidationSession::Capture(*Context.Generator, *Context.Interior);
	}
	else
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Project->Json)
			|| !Project->Json || !Project->Json->HasTypedField<EJson::Array>(TEXT("buildings")))
		{
			UE_LOG(LogTemp, Warning, TEXT("Mobile navigation could not read project %s"), *Path); return;
		}
	}
	IndexProject(*Project);
	const int32 Existing = Projects.IndexOfByPredicate([&](const auto& P) { return P->Site.Id == Project->Site.Id; });
	if (Existing == INDEX_NONE) Projects.Add(Project); else Projects[Existing] = Project;
}

void FMobileRealSource::IndexProject(FProject& Project)
{
	using namespace MobileRealData;
	Project.Site.Name = String(Project.Json, TEXT("projectName"), Project.Site.Name);
	if (Project.Site.Name.IsEmpty()) Project.Site.Name = TEXT("Unnamed project");
	Project.Site.Subtitle = String(Project.Json, TEXT("description"));
	Project.Site.BuildingCount = 0; Project.Site.LevelCount = 0;
	const double Scale = Number(Project.Json, TEXT("scale"), 1);
	for (const auto& Value : Array(Project.Json, TEXT("buildings")))
	{
		const auto Json = Object(Value);
		const FString RawId = String(Json, TEXT("id"));
		const FString BuildingId = Project.Site.Id + TEXT("/building/") + Escape(RawId);
		const auto Outline = Polygon(Json);
		if (RawId.IsEmpty() || Outline.Num() < 3 || Project.RawBuildingIds.Contains(BuildingId)) continue;
		FMobileBuilding Building; Building.Id = BuildingId; Building.Name = String(Json, TEXT("name"), RawId);
		if (Building.Name.IsEmpty()) Building.Name = RawId;
		const int32 Count = FMath::Clamp(static_cast<int32>(Number(Json, TEXT("floorCount"), 10)), 1, 1000);
		const auto Interior = Child(Json, TEXT("interior"));
		auto& Maps = Project.Maps.Add(BuildingId);
		for (int32 Level = 1; Level <= Count; ++Level)
		{
			FMobileLevel Label; Label.Level = Level; Label.Label = FString::Printf(TEXT("L%d"), Level); Building.Levels.Add(Label);
			FMobileLevelMap Map; Map.BuildingId = BuildingId; Map.Level = Level; Map.Outline = Outline; Map.bHasEntrance = false;
			TSharedPtr<FJsonObject> Floor;
			for (const auto& F : Array(Interior, TEXT("floors"))) if (Number(Object(F), TEXT("level"), 1) == Level) { Floor = Object(F); break; }
			for (const auto& RoomValue : Array(Floor, TEXT("rooms")))
			{
				const auto Room = Object(RoomValue);
				bool bClosed = true; if (Room) Room->TryGetBoolField(TEXT("closed"), bClosed);
				const auto Shape = Polygon(Room); const FString RoomId = String(Room, TEXT("id"));
				if (!bClosed || Shape.Num() < 3 || FMath::Abs(Area(Shape)) < KINDA_SMALL_NUMBER || RoomId.IsEmpty()) continue;
				const FString PlaceId = BuildingId + FString::Printf(TEXT("/level/%d/room/"), Level) + Escape(RoomId);
				if (Project.RoomShapes.Contains(PlaceId)) continue;
				Project.RoomShapes.Add(PlaceId, Shape);
				FMobileMapRoom MapRoom; MapRoom.Polygon = Shape; MapRoom.Name = String(Room, TEXT("name"), RoomId);
				if (MapRoom.Name.IsEmpty()) MapRoom.Name = RoomId;
				const FString Type = RoomTypes::Normalize(String(Room, TEXT("roomType")));
				MapRoom.Category = Category(Type);
				if (Type != TEXT("Corridor") && Type != TEXT("Storage"))
				{
					MapRoom.PlaceId = PlaceId;
					FMobilePlace Place; Place.Id = PlaceId; Place.Code = RoomId; Place.Name = MapRoom.Name;
					Place.SiteId = Project.Site.Id; Place.BuildingId = BuildingId; Place.BuildingName = Building.Name;
					Place.Level = Level; Place.LevelLabel = Label.Label; Place.RoomType = Type; Place.Category = MapRoom.Category;
					Place.Position = InteriorPoint(Shape); Place.Description = String(Room, TEXT("description"));
					for (const auto& Tag : Array(Room, TEXT("tags"))) { FString Text; if (Tag && Tag->TryGetString(Text)) Place.Tags.Add(Text); }
					Project.SearchText.Add(PlaceId, (Place.Name + TEXT(" ") + RoomId + TEXT(" ") + RawId + TEXT(" ") + Building.Name
						+ TEXT(" ") + Label.Label + TEXT(" ") + Type + TEXT(" ") + Place.Category + TEXT(" ") + FString::Join(Place.Tags, TEXT(" "))).ToLower());
					Project.Categories.AddUnique(Place.Category); Project.Places.Add(MoveTemp(Place));
				}
				Map.Rooms.Add(MoveTemp(MapRoom));
			}
			for (const auto& NodeValue : Array(Floor, TEXT("navNodes")))
			{
				const auto Node = Object(NodeValue); const FString Type = String(Node, TEXT("type"));
				FVector2D Position = FVector2D::ZeroVector; if (!Node || !XY(Node->TryGetField(TEXT("position")), Position)) continue;
				if (!Map.bHasEntrance) { Map.Entrance = Position; Map.bHasEntrance = true; }
				if (Type != TEXT("elevator") && Type != TEXT("stairs") && Type != TEXT("escalator")) continue;
				FMobileMapConnector Connector; Connector.Position = Position;
				Connector.Kind = Type == TEXT("elevator") ? EMobileConnectorKind::Lift : Type == TEXT("stairs") ? EMobileConnectorKind::Stairs : EMobileConnectorKind::Escalator;
				Map.Connectors.Add(Connector);
			}
			// Choose a point just inside an authored main door, with the planner's wall clearance.
			// Use the original vertex order: entrance edge indices refer to that order, not the normalized map polygon.
			if (Level == 1 && Scale > KINDA_SMALL_NUMBER)
			{
				TArray<FVector2D> RawOutline;
				for (const auto& P : Array(Json, TEXT("footprint"))) { FVector2D Point; if (XY(P, Point)) RawOutline.Add(Point); }
				for (const auto& DoorValue : Array(Interior, TEXT("entrances")))
				{
					const auto Door = Object(DoorValue);
					if (String(Door, TEXT("kind")) != TEXT("main") || Number(Door, TEXT("level"), 1) != 1) continue;
					const int32 Edge = static_cast<int32>(Number(Door, TEXT("edge"), -1));
					if (!RawOutline.IsValidIndex(Edge)) continue;
					const auto A = RawOutline[Edge], B = RawOutline[(Edge + 1) % RawOutline.Num()];
					const double Offset = Number(Door, TEXT("offset"), 0), Width = Number(Door, TEXT("width"), 240);
					if (Offset < 0 || Width <= 0 || (Offset + Width) / Scale > FVector2D::Distance(A, B)) continue;
					const auto Direction = (B - A).GetSafeNormal();
					const auto Inward = FVector2D(-Direction.Y, Direction.X) * (Area(RawOutline) < 0 ? -1.0 : 1.0);
					Map.Entrance = A + Direction * ((Offset + Width * 0.5) / Scale)
						+ Inward * ((Number(Floor, TEXT("wallThickness"), 20) * 0.5 + 30) / Scale);
					Map.bHasEntrance = true; break;
				}
			}
			Maps.Add(Level, MoveTemp(Map));
		}
		Project.RawBuildingIds.Add(BuildingId, RawId);
		FMobileCampusBuilding Campus; Campus.BuildingId = BuildingId; Campus.Name = Building.Name;
		XY(Json->TryGetField(TEXT("offset")), Campus.Offset); Campus.Outline = Outline;
		for (auto& Point : Campus.Outline) Point += Campus.Offset;
		Project.Campus.Buildings.Add(MoveTemp(Campus));
		Project.Site.LevelCount = FMath::Max(Project.Site.LevelCount, Count);
		Project.Buildings.Add(MoveTemp(Building));
	}
	Project.Site.BuildingCount = Project.Buildings.Num(); Project.Categories.Sort();
}

FMobileRealSource::FProject* FMobileRealSource::ProjectForSite(const FString& Id) const
{
	for (const auto& Project : Projects) if (Project->Site.Id == Id) return Project.Get(); return nullptr;
}
FMobileRealSource::FProject* FMobileRealSource::ProjectForBuilding(const FString& Id) const
{
	for (const auto& Project : Projects) if (Project->RawBuildingIds.Contains(Id)) return Project.Get(); return nullptr;
}
TArray<FMobileSite> FMobileRealSource::ListSites()
{
	TArray<FMobileSite> Result; for (const auto& Project : Projects) Result.Add(Project->Site); return Result;
}
TArray<FMobileBuilding> FMobileRealSource::ListBuildings(const FString& SiteId)
{
	const auto* Project = ProjectForSite(SiteId); return Project ? Project->Buildings : TArray<FMobileBuilding>();
}
TArray<FString> FMobileRealSource::ListCategories(const FString& SiteId)
{
	const auto* Project = ProjectForSite(SiteId); return Project ? Project->Categories : TArray<FString>();
}
TArray<FMobilePlace> FMobileRealSource::SearchPlaces(const FString& SiteId, const FString& Query, const FMobilePlaceFilter& Filter)
{
	TArray<FMobilePlace> Result; const auto* Project = ProjectForSite(SiteId); if (!Project) return Result;
	TArray<FString> Words; Query.ToLower().ParseIntoArrayWS(Words);
	for (const auto& Place : Project->Places)
	{
		if ((!Filter.BuildingId.IsEmpty() && Place.BuildingId != Filter.BuildingId) || (Filter.Level != INDEX_NONE && Filter.Level != Place.Level)
			|| (!Filter.Categories.IsEmpty() && !Filter.Categories.Contains(Place.Category)) || (Filter.bAvailableOnly && !Place.bAvailable)) continue;
		if (NavigationSearch::MatchesWords(Words, Project->SearchText.FindChecked(Place.Id))) Result.Add(Place);
	}
	return Result;
}
bool FMobileRealSource::FindPlace(const FString& PlaceId, FMobilePlace& OutPlace)
{
	for (const auto& Project : Projects) if (const auto* Place = Project->Places.FindByPredicate([&](const auto& P) { return P.Id == PlaceId; }))
	{ OutPlace = *Place; return true; }
	OutPlace = {}; return false;
}
bool FMobileRealSource::GetLevelMap(const FString& BuildingId, int32 Level, FMobileLevelMap& OutMap)
{
	const auto* Project = ProjectForBuilding(BuildingId);
	if (Project) if (const auto* Map = Project->Maps.FindChecked(BuildingId).Find(Level)) { OutMap = *Map; return true; }
	OutMap = {}; return false;
}
FMobileCampusMap FMobileRealSource::GetCampusMap(const FString& SiteId)
{
	const auto* Project = ProjectForSite(SiteId); return Project ? Project->Campus : FMobileCampusMap();
}

FMobileRoute FMobileRealSource::FindRoute(const FMobileRouteRequest& Request)
{
	using namespace MobileRealData;
	FMobilePlace Destination, Start;
	if (!FindPlace(Request.ToPlaceId, Destination)) return Failure(TEXT("Choose a destination that exists in this project."));
	if (!Request.FromPlaceId.IsEmpty())
	{
		if (!FindPlace(Request.FromPlaceId, Start)) return Failure(TEXT("The starting place is no longer in the project."));
		if (Start.SiteId != Destination.SiteId) return Failure(TEXT("Choose a start and destination in the same project."));
		if (Start.Id == Destination.Id) return Failure(TEXT("You are already at this destination."));
	}
	auto& Project = *ProjectForSite(Destination.SiteId);
	if (LayoutValidation::ValidateProject(Project.Json).HasErrors()) return Failure(TEXT("This project contains invalid layout data. Correct it in the layout editor before finding routes."));
	FRouteWorld Context;
	if (!Context.IsReady() || Context.Generator->LoadBuildingsFromJson(Project.Json) != Project.Buildings.Num())
		return Failure(TEXT("The project could not be prepared for navigation."));
	FNavigationRoutePoint To; To.BuildingId = Project.RawBuildingIds.FindChecked(Destination.BuildingId); To.Floor = Destination.Level;
	if (!FNavigationRoutePlanner::FindRoomDestination(*Context.Generator, *Context.Interior, To.BuildingId, To.Floor, Project.RoomShapes.FindChecked(Destination.Id), To.Position))
		return Failure(TEXT("No destination with enough wall clearance could be found inside this room."));
	FNavigationRoutePoint From;
	if (!Start.Id.IsEmpty())
	{
		From.BuildingId = Project.RawBuildingIds.FindChecked(Start.BuildingId); From.Floor = Start.Level;
		if (!FNavigationRoutePlanner::FindRoomDestination(*Context.Generator, *Context.Interior, From.BuildingId, From.Floor, Project.RoomShapes.FindChecked(Start.Id), From.Position))
			return Failure(TEXT("No starting point with enough wall clearance could be found inside this room."));
	}
	else
	{
		FString BuildingId = Request.MyLocationBuildingId;
		if (!Project.RawBuildingIds.Contains(BuildingId)) BuildingId = Destination.BuildingId;
		const auto& Building = *Project.Buildings.FindByPredicate([&](const auto& B) { return B.Id == BuildingId; });
		int32 Level = Request.MyLocationLevel;
		if (!Project.Maps.FindChecked(BuildingId).Contains(Level)) Level = Building.Levels[0].Level;
		const auto& Map = Project.Maps.FindChecked(BuildingId).FindChecked(Level);
		if (!Map.bHasEntrance) return Failure(TEXT("This level has no mapped entrance or navigation node. Choose a room as your starting point."));
		From.BuildingId = Project.RawBuildingIds.FindChecked(BuildingId); From.Floor = Level; From.Position = Map.Entrance;
	}
	FNavigationRoutePreferences Preferences; Preferences.bAllowLift = Request.Preferences.bAllowLift;
	Preferences.bAllowStairs = Request.Preferences.bAllowStairs; Preferences.bAllowEscalator = Request.Preferences.bAllowEscalator;
	FNavigationRouteResult Planned;
	if (!FNavigationRoutePlanner::FindCampusRoute(*Context.Generator, *Context.Interior, From, To, Planned, Preferences))
		return Failure(Planned.Error.IsEmpty() ? TEXT("No route is available with these lift, stairs and escalator choices.") : Planned.Error);
	if (Planned.Points.Num() < 2) return Failure(TEXT("The planner returned no walkable route."));
	FMobileRoute Result; Result.bFound = true;
	auto MobileId = [&](const FString& Raw) { return Project.Site.Id + TEXT("/building/") + Escape(Raw); };
	auto BuildingName = [&](const FString& Raw)
	{
		const auto* Building = Project.Buildings.FindByPredicate([&](const auto& B) { return B.Id == MobileId(Raw); });
		return Building ? Building->Name : Raw;
	};
	for (const auto& Point : Planned.Points)
	{
		FMobileRoutePoint P; P.BuildingId = MobileId(Point.BuildingId); P.Level = Point.Floor; P.Position = Point.Position; Result.Points.Add(P);
	}
	for (int32 I = 0; I + 1 < Planned.Points.Num(); ++I)
	{
		const auto& A = Planned.Points[I]; const auto& B = Planned.Points[I + 1];
		FMobileStep Step; Step.BuildingId = MobileId(A.BuildingId); Step.Level = A.Floor; Step.Position = A.Position;
		Step.LevelLabel = A.Floor == 0 ? TEXT("Outside") : FString::Printf(TEXT("L%d"), A.Floor);
		Step.Metres = FVector::Distance(A.WorldPosition, B.WorldPosition) / 100.0; Result.Metres += Step.Metres;
		if (A.Floor > 0 && B.Floor == 0) { Step.Kind = EMobileStepKind::ExitBuilding; Step.Text = TEXT("Leave ") + BuildingName(A.BuildingId); }
		else if (A.Floor == 0 && B.Floor > 0) { Step.Kind = EMobileStepKind::EnterBuilding; Step.Text = TEXT("Enter ") + BuildingName(B.BuildingId); }
		else if (A.BuildingId != B.BuildingId && A.Floor > 0 && B.Floor > 0)
		{
			Step.Kind = EMobileStepKind::ExitBuilding; Step.Text = TEXT("Leave ") + BuildingName(A.BuildingId) + TEXT(" through the link door");
			Result.Steps.Add(Step); Step.Metres = 0; Step.Kind = EMobileStepKind::EnterBuilding;
			Step.BuildingId = MobileId(B.BuildingId); Step.Level = B.Floor; Step.Position = B.Position;
			Step.LevelLabel = FString::Printf(TEXT("L%d"), B.Floor); Step.Text = TEXT("Enter ") + BuildingName(B.BuildingId);
		}
		else if (A.Floor != B.Floor)
		{
			const bool bLift = A.ConnectorType == EInteriorNavNodeType::Elevator, bStairs = A.ConnectorType == EInteriorNavNodeType::Stairs;
			Step.Kind = bLift ? EMobileStepKind::TakeLift : bStairs ? EMobileStepKind::TakeStairs : EMobileStepKind::TakeEscalator;
			Step.Text = FString::Printf(TEXT("Take the %s to L%d"), bLift ? TEXT("lift") : bStairs ? TEXT("stairs") : TEXT("escalator"), B.Floor);
		}
		else
		{
			Step.Text = A.Floor == 0 ? TEXT("Follow the outdoor path") : TEXT("Continue along the corridor");
			if (I > 0)
			{
				const auto& Before = Planned.Points[I - 1];
				const FVector2D Incoming = FVector2D(A.WorldPosition - Before.WorldPosition).GetSafeNormal();
				const FVector2D Outgoing = FVector2D(B.WorldPosition - A.WorldPosition).GetSafeNormal();
				if (Before.Floor == A.Floor && !Incoming.IsNearlyZero() && !Outgoing.IsNearlyZero()
					&& FVector2D::DotProduct(Incoming, Outgoing) < 0.90)
				{
					const bool bLeft = FVector2D::CrossProduct(Incoming, Outgoing) > 0;
					Step.Kind = bLeft ? EMobileStepKind::TurnLeft : EMobileStepKind::TurnRight;
					Step.Text = bLeft ? TEXT("Turn left and continue") : TEXT("Turn right and continue");
				}
			}
		}
		if (Step.Metres > KINDA_SMALL_NUMBER || Step.Kind != EMobileStepKind::Straight) Result.Steps.Add(MoveTemp(Step));
	}
	FMobileStep Arrival; Arrival.Kind = EMobileStepKind::Arrive; Arrival.Text = TEXT("You have arrived: ") + Destination.Name;
	Arrival.BuildingId = Destination.BuildingId; Arrival.Level = Destination.Level; Arrival.LevelLabel = Destination.LevelLabel; Arrival.Position = To.Position;
	Result.Steps.Add(MoveTemp(Arrival)); Result.Minutes = MobileNav::MinutesForMetres(Result.Metres); return Result;
}
