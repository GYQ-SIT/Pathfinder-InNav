#include "MobileMockSource.h"

#include "NavigationSearchUtils.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

FString FMobilePlace::GroupLabel() const
{
	return BuildingName + TEXT(" · ") + LevelLabel;
}

int32 MobileNav::MinutesForMetres(double Metres)
{
	return FMath::Max(1, FMath::CeilToInt(Metres / (WalkingMetresPerSecond * 60.0)));
}

namespace
{
	FMobileRoute NoRoute(const FString& Error)
	{
		FMobileRoute Route;
		Route.Error = Error;
		return Route;
	}

	// A point written as [x, y].
	bool ReadPoint(const TSharedPtr<FJsonValue>& Value, FVector2D& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Pair) || Pair->Num() != 2)
		{
			return false;
		}
		double X = 0.0, Y = 0.0;
		if (!(*Pair)[0]->TryGetNumber(X) || !(*Pair)[1]->TryGetNumber(Y))
		{
			return false;
		}
		Out = FVector2D(X, Y);
		return true;
	}

	// An outline written as [[x, y], ...].
	bool ReadOutline(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, TArray<FVector2D>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(Field, Points) || Points->Num() < 3)
		{
			return false;
		}
		Out.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *Points)
		{
			FVector2D Point;
			if (!ReadPoint(Value, Point))
			{
				return false;
			}
			Out.Add(Point);
		}
		return true;
	}

	bool ReadPointField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FVector2D& Out)
	{
		return Object.IsValid() && Object->HasField(Field) && ReadPoint(Object->TryGetField(Field), Out);
	}
}

FString FMobileMockSource::DefaultDataFile()
{
	return FPaths::ProjectDir() / TEXT("Data/Prototype/mobile-prototype.json");
}

FMobileMockSource::FMobileMockSource()
{
	Load(DefaultDataFile());
}

FMobileMockSource::FMobileMockSource(const FString& DataFile)
{
	Load(DataFile);
}

void FMobileMockSource::Load(const FString& DataFile)
{
	auto Fail = [this, &DataFile](const FString& Why)
	{
		Sites.Reset();
		LoadError = FString::Printf(TEXT("The phone prototype data %s could not be used: %s"), *DataFile, *Why);
		UE_LOG(LogTemp, Warning, TEXT("%s"), *LoadError);
	};

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *DataFile))
	{
		Fail(TEXT("the file is missing."));
		return;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		Fail(TEXT("it is not valid JSON."));
		return;
	}
	FString Format;
	int32 Version = 0;
	if (!Root->TryGetStringField(TEXT("format"), Format) || Format != TEXT("innavi-mobile-prototype") || !Root->TryGetNumberField(TEXT("version"), Version) || Version != 1)
	{
		Fail(TEXT("it is not a version 1 file of the format \"innavi-mobile-prototype\"."));
		return;
	}
	// A prototype's data is made up, and says so; a file that does not say it is refused rather than shown as if it were real.
	bool bSynthetic = false;
	if (!Root->TryGetBoolField(TEXT("synthetic"), bSynthetic) || !bSynthetic)
	{
		Fail(TEXT("it does not say that it is synthetic data (\"synthetic\": true)."));
		return;
	}
	Root->TryGetStringField(TEXT("dataNote"), DataNote);
	FString PlaceDescription;
	Root->TryGetStringField(TEXT("placeDescription"), PlaceDescription);

	const TSharedPtr<FJsonObject>* Plan = nullptr;
	if (!Root->TryGetObjectField(TEXT("plan"), Plan)
		|| !ReadPointField(*Plan, TEXT("entrance"), Entrance) || !ReadPointField(*Plan, TEXT("crossing"), Crossing)
		|| !ReadOutline(*Plan, TEXT("levelOutline"), LevelOutline) || !(*Plan)->TryGetNumberField(TEXT("roomHalfSize"), RoomHalfSize))
	{
		Fail(TEXT("\"plan\" is missing or incomplete."));
		return;
	}
	const TSharedPtr<FJsonObject>* Connectors = nullptr;
	if (!(*Plan)->TryGetObjectField(TEXT("connectors"), Connectors)
		|| !ReadPointField(*Connectors, TEXT("lift"), LiftAt) || !ReadPointField(*Connectors, TEXT("stairs"), StairsAt) || !ReadPointField(*Connectors, TEXT("escalator"), EscalatorAt))
	{
		Fail(TEXT("\"plan.connectors\" needs lift, stairs and escalator."));
		return;
	}

	// The four places of every level, by set (a campus and a mall have their own).
	TMap<FString, TArray<FPlaceTemplate>> PlaceSets;
	const TSharedPtr<FJsonObject>* Sets = nullptr;
	if (!Root->TryGetObjectField(TEXT("placeSets"), Sets))
	{
		Fail(TEXT("\"placeSets\" is missing."));
		return;
	}
	// The keys of a JSON object are not FString in this engine version: a loop that names TPair<FString, ...> copies each pair, and clang (the Android build) refuses that.
	for (const auto& Set : (*Sets)->Values)
	{
		const FString SetName(*Set.Key);
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Set.Value->TryGetArray(Entries))
		{
			Fail(FString::Printf(TEXT("place set \"%s\" is not a list."), *SetName));
			return;
		}
		for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			FPlaceTemplate Template;
			if (!EntryValue->TryGetObject(Entry) || !(*Entry)->TryGetStringField(TEXT("name"), Template.Name) || !(*Entry)->TryGetNumberField(TEXT("number"), Template.Number)
				|| !(*Entry)->TryGetStringField(TEXT("category"), Template.Category) || !(*Entry)->TryGetStringField(TEXT("roomType"), Template.RoomType)
				|| !ReadPointField(*Entry, TEXT("position"), Template.Position))
			{
				Fail(FString::Printf(TEXT("a place of the set \"%s\" needs name, number, category, roomType and position."), *SetName));
				return;
			}
			const TArray<TSharedPtr<FJsonValue>>* Closed = nullptr;
			if ((*Entry)->TryGetArrayField(TEXT("unavailableOnLevels"), Closed))
			{
				for (const TSharedPtr<FJsonValue>& Level : *Closed)
				{
					int32 LevelNumber = 0;
					if (Level->TryGetNumber(LevelNumber)) Template.UnavailableOnLevels.Add(LevelNumber);
				}
			}
			PlaceSets.FindOrAdd(SetName).Add(Template);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* SiteValues = nullptr;
	if (!Root->TryGetArrayField(TEXT("sites"), SiteValues) || SiteValues->IsEmpty())
	{
		Fail(TEXT("\"sites\" is missing or empty."));
		return;
	}
	for (const TSharedPtr<FJsonValue>& SiteValue : *SiteValues)
	{
		const TSharedPtr<FJsonObject>* SiteObject = nullptr;
		FSiteData Data;
		FString SetName, CodePattern;
		const TArray<TSharedPtr<FJsonValue>>* BuildingValues = nullptr;
		if (!SiteValue->TryGetObject(SiteObject) || !(*SiteObject)->TryGetStringField(TEXT("id"), Data.Site.Id) || !(*SiteObject)->TryGetStringField(TEXT("name"), Data.Site.Name)
			|| !(*SiteObject)->TryGetStringField(TEXT("placeSet"), SetName) || !(*SiteObject)->TryGetStringField(TEXT("codePattern"), CodePattern)
			|| !(*SiteObject)->TryGetArrayField(TEXT("buildings"), BuildingValues) || BuildingValues->IsEmpty())
		{
			Fail(TEXT("a site needs id, name, placeSet, codePattern and buildings."));
			return;
		}
		const TArray<FPlaceTemplate>* Templates = PlaceSets.Find(SetName);
		if (!Templates)
		{
			Fail(FString::Printf(TEXT("site \"%s\" uses the place set \"%s\", which is not there."), *Data.Site.Id, *SetName));
			return;
		}
		(*SiteObject)->TryGetStringField(TEXT("shortName"), Data.Site.ShortName);
		(*SiteObject)->TryGetStringField(TEXT("subtitle"), Data.Site.Subtitle);
		(*SiteObject)->TryGetStringField(TEXT("distanceLabel"), Data.Site.DistanceLabel);
		(*SiteObject)->TryGetBoolField(TEXT("hasEscalators"), Data.bHasEscalators);

		for (const TSharedPtr<FJsonValue>& BuildingValue : *BuildingValues)
		{
			const TSharedPtr<FJsonObject>* BuildingObject = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* LevelValues = nullptr;
			FMobileBuilding Building;
			if (!BuildingValue->TryGetObject(BuildingObject) || !(*BuildingObject)->TryGetStringField(TEXT("id"), Building.Id) || !(*BuildingObject)->TryGetStringField(TEXT("name"), Building.Name)
				|| !(*BuildingObject)->TryGetArrayField(TEXT("levels"), LevelValues) || LevelValues->IsEmpty())
			{
				Fail(TEXT("a building needs id, name and levels."));
				return;
			}
			for (const TSharedPtr<FJsonValue>& LevelValue : *LevelValues)
			{
				const TSharedPtr<FJsonObject>* LevelObject = nullptr;
				FMobileLevel Level;
				if (!LevelValue->TryGetObject(LevelObject) || !(*LevelObject)->TryGetNumberField(TEXT("level"), Level.Level) || !(*LevelObject)->TryGetStringField(TEXT("label"), Level.Label))
				{
					Fail(FString::Printf(TEXT("a level of building \"%s\" needs level and label."), *Building.Id));
					return;
				}
				Building.Levels.Add(Level);
			}
			Data.Buildings.Add(Building);
			Data.Site.LevelCount = FMath::Max(Data.Site.LevelCount, Building.Levels.Num());
		}
		Data.Site.BuildingCount = Data.Buildings.Num();

		for (const FMobileBuilding& Building : Data.Buildings)
		{
			for (const FMobileLevel& Level : Building.Levels)
			{
				// "L2M" is level 2's mezzanine: its code says 2M, the others say the level number.
				const FString LevelCode = Level.Label.StartsWith(TEXT("L")) ? Level.Label.Mid(1) : Level.Label;
				const FString Padded = LevelCode.Len() < 2 && LevelCode.IsNumeric() ? TEXT("0") + LevelCode : LevelCode;
				for (const FPlaceTemplate& Template : *Templates)
				{
					FMobilePlace Place;
					Place.Name = Template.Name;
					Place.SiteId = Data.Site.Id;
					Place.BuildingId = Building.Id;
					Place.BuildingName = Building.Name;
					Place.Level = Level.Level;
					Place.LevelLabel = Level.Label;
					Place.Category = Template.Category;
					Place.RoomType = Template.RoomType;
					Place.Position = Template.Position;
					// A mall prints "#01-101"; a campus "E2-01-101": the site says how.
					Place.Code = CodePattern.Replace(TEXT("{building}"), *Building.Id).Replace(TEXT("{level}"), *Padded).Replace(TEXT("{number}"), *FString::FromInt(Template.Number));
					Place.Id = Building.Id + TEXT("-") + Padded + TEXT("-") + FString::FromInt(Template.Number);
					Place.bAvailable = !Template.UnavailableOnLevels.Contains(Level.Level);
					Place.Description = PlaceDescription;
					Data.Places.Add(Place);
				}
			}
		}

		// The map of the site from above: an outline for each building (and the features between them).
		const TSharedPtr<FJsonObject>* MapObject = nullptr;
		if ((*SiteObject)->TryGetObjectField(TEXT("campusMap"), MapObject))
		{
			const TArray<TSharedPtr<FJsonValue>>* MapBuildings = nullptr;
			if ((*MapObject)->TryGetArrayField(TEXT("buildings"), MapBuildings))
			{
				for (const TSharedPtr<FJsonValue>& MapBuildingValue : *MapBuildings)
				{
					const TSharedPtr<FJsonObject>* MapBuilding = nullptr;
					FMobileCampusBuilding Outline;
					if (!MapBuildingValue->TryGetObject(MapBuilding) || !(*MapBuilding)->TryGetStringField(TEXT("buildingId"), Outline.BuildingId) || !ReadOutline(*MapBuilding, TEXT("outline"), Outline.Outline))
					{
						Fail(FString::Printf(TEXT("a building outline of the map of site \"%s\" needs buildingId and outline."), *Data.Site.Id));
						return;
					}
					const FMobileBuilding* Known = Data.Buildings.FindByPredicate([&Outline](const FMobileBuilding& Candidate) { return Candidate.Id == Outline.BuildingId; });
					Outline.Name = Known ? Known->Name : Outline.BuildingId;
					Data.CampusMap.Buildings.Add(Outline);
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Features = nullptr;
			if ((*MapObject)->TryGetArrayField(TEXT("features"), Features))
			{
				for (const TSharedPtr<FJsonValue>& FeatureValue : *Features)
				{
					const TSharedPtr<FJsonObject>* Feature = nullptr;
					FMobileCampusFeature Parsed;
					if (!FeatureValue->TryGetObject(Feature) || !(*Feature)->TryGetStringField(TEXT("name"), Parsed.Name) || !ReadOutline(*Feature, TEXT("outline"), Parsed.Outline))
					{
						Fail(FString::Printf(TEXT("a feature of the map of site \"%s\" needs name and outline."), *Data.Site.Id));
						return;
					}
					Data.CampusMap.Features.Add(Parsed);
				}
			}
		}
		Sites.Add(MoveTemp(Data));
	}
}

const FMobileMockSource::FSiteData* FMobileMockSource::FindSite(const FString& SiteId) const
{
	return Sites.FindByPredicate([&SiteId](const FSiteData& Data) { return Data.Site.Id == SiteId; });
}

const FMobileMockSource::FSiteData* FMobileMockSource::FindSiteOfPlace(const FString& PlaceId) const
{
	return Sites.FindByPredicate([&PlaceId](const FSiteData& Data) { return Data.Places.ContainsByPredicate([&PlaceId](const FMobilePlace& Place) { return Place.Id == PlaceId; }); });
}

FString FMobileMockSource::LevelLabel(const FSiteData& Data, const FString& BuildingId, int32 Level) const
{
	for (const FMobileBuilding& Building : Data.Buildings)
	{
		if (Building.Id == BuildingId)
		{
			for (const FMobileLevel& Candidate : Building.Levels)
			{
				if (Candidate.Level == Level)
				{
					return Candidate.Label;
				}
			}
		}
	}
	return FString::Printf(TEXT("L%d"), Level);
}

TArray<FMobileSite> FMobileMockSource::ListSites()
{
	TArray<FMobileSite> Result;
	for (const FSiteData& Data : Sites)
	{
		Result.Add(Data.Site);
	}
	return Result;
}

TArray<FMobileBuilding> FMobileMockSource::ListBuildings(const FString& SiteId)
{
	const FSiteData* Data = FindSite(SiteId);
	return Data ? Data->Buildings : TArray<FMobileBuilding>();
}

TArray<FString> FMobileMockSource::ListCategories(const FString& SiteId)
{
	TArray<FString> Result;
	if (const FSiteData* Data = FindSite(SiteId))
	{
		for (const FMobilePlace& Place : Data->Places)
		{
			Result.AddUnique(Place.Category);
		}
	}
	return Result;
}

TArray<FMobilePlace> FMobileMockSource::SearchPlaces(const FString& SiteId, const FString& Query, const FMobilePlaceFilter& Filter)
{
	TArray<FMobilePlace> Result;
	const FSiteData* Data = FindSite(SiteId);
	if (!Data)
	{
		return Result;
	}
	const TArray<FString> Words = [&Query]()
	{
		TArray<FString> Parsed;
		Query.ParseIntoArrayWS(Parsed);
		return Parsed;
	}();
	for (const FMobilePlace& Place : Data->Places)
	{
		if ((!Filter.Categories.IsEmpty() && !Filter.Categories.Contains(Place.Category))
			|| (Filter.bAvailableOnly && !Place.bAvailable)
			|| (!Filter.BuildingId.IsEmpty() && Place.BuildingId != Filter.BuildingId)
			|| (Filter.Level != INDEX_NONE && Place.Level != Filter.Level))
		{
			continue;
		}
		const FString Searchable = FString::Join(TArray<FString>{ Place.Name, Place.Code, Place.Category, Place.BuildingName, Place.LevelLabel, FString::Join(Place.Tags, TEXT(" ")) }, TEXT(" "));
		if (NavigationSearch::MatchesWords(Words, Searchable))
		{
			Result.Add(Place);
		}
	}
	return Result;
}

bool FMobileMockSource::FindPlace(const FString& PlaceId, FMobilePlace& OutPlace)
{
	if (const FSiteData* Data = FindSiteOfPlace(PlaceId))
	{
		OutPlace = *Data->Places.FindByPredicate([&PlaceId](const FMobilePlace& Place) { return Place.Id == PlaceId; });
		return true;
	}
	return false;
}

FMobileRoute FMobileMockSource::FindRoute(const FMobileRouteRequest& Request)
{
	FMobileRoute Route;
	FMobilePlace To;
	if (!FindPlace(Request.ToPlaceId, To))
	{
		Route.Error = TEXT("That place cannot be found.");
		return Route;
	}
	const FSiteData* Data = FindSite(To.SiteId);

	// Where the walk starts: a place, or "my location" - at the door of the destination's building, on level 1.
	FString FromBuilding = To.BuildingId;
	int32 FromLevel = 1;
	FVector2D FromPosition = Entrance;
	if (Request.FromPlaceId.IsEmpty() && !Request.MyLocationBuildingId.IsEmpty())
	{
		// "My location" in a building and level the person said, if the site has them.
		for (const FMobileBuilding& Building : Data->Buildings)
		{
			if (Building.Id == Request.MyLocationBuildingId)
			{
				FromBuilding = Building.Id;
				FromLevel = Building.Levels.ContainsByPredicate([&Request](const FMobileLevel& L) { return L.Level == Request.MyLocationLevel; }) ? Request.MyLocationLevel : (Building.Levels.IsEmpty() ? 1 : Building.Levels[0].Level);
			}
		}
	}
	if (!Request.FromPlaceId.IsEmpty())
	{
		FMobilePlace From;
		if (!FindPlace(Request.FromPlaceId, From))
		{
			Route.Error = TEXT("The starting place cannot be found.");
			return Route;
		}
		if (From.SiteId != To.SiteId)
		{
			Route.Error = TEXT("These places are on different sites.");
			return Route;
		}
		if (From.Id == To.Id)
		{
			Route.Error = TEXT("You are already there.");
			return Route;
		}
		FromBuilding = From.BuildingId;
		FromLevel = From.Level;
		FromPosition = From.Position;
	}

	// The walk is built as a person goes: where they are, a step for each instruction (given where it applies, with the metres of the leg that
	// follows it), and the points of the line the map draws.
	FString CurBuilding = FromBuilding;
	int32 CurLevel = FromLevel;
	FVector2D CurPosition = FromPosition;
	auto Mark = [&](const FString& BuildingId, int32 Level, const FVector2D& Position)
	{
		FMobileRoutePoint Point;
		Point.BuildingId = BuildingId;
		Point.Level = Level;
		Point.Position = Position;
		Route.Points.Add(Point);
	};
	auto AddStep = [&](EMobileStepKind Kind, const FString& Text, const FString& BuildingId, int32 Level, double Metres, const FVector2D& Position)
	{
		FMobileStep Step;
		Step.Kind = Kind;
		Step.Text = Text;
		Step.BuildingId = BuildingId;
		if (const FMobileBuilding* Building = Data->Buildings.FindByPredicate([&BuildingId](const FMobileBuilding& B) { return B.Id == BuildingId; }))
		{
			Step.BuildingName = Building->Name;
		}
		Step.Level = Level;
		Step.LevelLabel = LevelLabel(*Data, BuildingId, Level);
		Step.Metres = Metres;
		Step.Position = Position;
		Route.Steps.Add(Step);
		Route.Metres += Metres;
	};
	// A straight leg across the plan (a metre is 0.8 of a plan unit, as the prototype's 100 m is a walk of about 125).
	auto Leg = [](const FVector2D& A, const FVector2D& B) { return FMath::Max(6.0, FVector2D::Distance(A, B) * 0.8); };
	auto Walk = [&](EMobileStepKind Kind, const FString& Text, const FVector2D& Target)
	{
		AddStep(Kind, Text, CurBuilding, CurLevel, Leg(CurPosition, Target), CurPosition);
		Mark(CurBuilding, CurLevel, Target);
		CurPosition = Target;
	};
	auto ConnectorAt = [this](EMobileStepKind Kind) { return Kind == EMobileStepKind::TakeLift ? LiftAt : (Kind == EMobileStepKind::TakeStairs ? StairsAt : EscalatorAt); };

	// Between two levels of the building the person is in: the lift if it is allowed, else the escalator (where there is one), else the stairs.
	auto ChangeLevel = [&](int32 To2) -> bool
	{
		if (CurLevel == To2)
		{
			return true;
		}
		EMobileStepKind Kind;
		const TCHAR* Word;
		double Ride;
		const int32 Levels = FMath::Abs(To2 - CurLevel);
		if (Request.Preferences.bAllowLift)
		{
			Kind = EMobileStepKind::TakeLift; Word = TEXT("lift"); Ride = 12.0 + 4.0 * Levels;
		}
		else if (Request.Preferences.bAllowEscalator && Data->bHasEscalators)
		{
			Kind = EMobileStepKind::TakeEscalator; Word = TEXT("escalator"); Ride = 18.0 + 6.0 * Levels;
		}
		else if (Request.Preferences.bAllowStairs)
		{
			Kind = EMobileStepKind::TakeStairs; Word = TEXT("stairs"); Ride = 20.0 + 8.0 * Levels;
		}
		else
		{
			return false;
		}
		// Along the corridor to it, then the ride: the instruction is given at the lift, on the level the person boards it.
		Walk(EMobileStepKind::Straight, TEXT("Follow the corridor"), ConnectorAt(Kind));
		AddStep(Kind, FString::Printf(TEXT("Take the %s to %s"), Word, *LevelLabel(*Data, CurBuilding, To2)), CurBuilding, CurLevel, Ride, CurPosition);
		CurLevel = To2;
		Mark(CurBuilding, CurLevel, CurPosition);
		return true;
	};

	Mark(CurBuilding, CurLevel, CurPosition);
	// Between buildings: down to the street level, out, along the campus path, in.
	if (FromBuilding != To.BuildingId)
	{
		if (!ChangeLevel(1))
		{
			Route.Error = TEXT("There is no way between levels with the options you chose.");
			return NoRoute(Route.Error);
		}
		Walk(EMobileStepKind::Straight, TEXT("Follow the corridor to the exit"), Entrance);
		AddStep(EMobileStepKind::ExitBuilding, FString::Printf(TEXT("Leave %s"), *FromBuilding), CurBuilding, 1, 5.0, CurPosition);
		AddStep(EMobileStepKind::Straight, FString::Printf(TEXT("Walk along the campus path to %s"), *To.BuildingName), CurBuilding, 1, 90.0, CurPosition);
		CurBuilding = To.BuildingId;
		Mark(CurBuilding, 1, Entrance);
		AddStep(EMobileStepKind::EnterBuilding, FString::Printf(TEXT("Enter %s"), *To.BuildingName), CurBuilding, 1, 5.0, Entrance);
	}
	if (!ChangeLevel(To.Level))
	{
		Route.Error = TEXT("There is no way between levels with the options you chose.");
		return NoRoute(Route.Error);
	}

	// The last stretch: along the corridor to the crossing, then along it to the room's door and in.
	if (FVector2D::Distance(CurPosition, Crossing) > 4.0 || Route.Steps.IsEmpty())
	{
		Walk(EMobileStepKind::Straight, TEXT("Follow the corridor"), Crossing);
	}
	const FVector2D InCorridor(To.Position.X, 50.0);
	const FVector2D Door(To.Position.X, To.Position.Y < 50.0 ? 40.0 : 60.0);
	const double TurnLeg = Leg(Crossing, InCorridor) + Leg(InCorridor, Door) + Leg(Door, To.Position);
	AddStep(To.Position.X < 50.0 ? EMobileStepKind::TurnLeft : EMobileStepKind::TurnRight, FString::Printf(TEXT("Turn towards %s"), *To.Name), CurBuilding, CurLevel, TurnLeg, Crossing);
	Mark(CurBuilding, CurLevel, InCorridor);
	Mark(CurBuilding, CurLevel, Door);
	Mark(CurBuilding, CurLevel, To.Position);
	AddStep(EMobileStepKind::Arrive, FString::Printf(TEXT("You have arrived: %s"), *To.Name), CurBuilding, CurLevel, 0.0, To.Position);
	Route.bFound = true;
	Route.Minutes = MobileNav::MinutesForMetres(Route.Metres);
	return Route;
}

bool FMobileMockSource::GetLevelMap(const FString& BuildingId, int32 Level, FMobileLevelMap& OutMap)
{
	for (const FSiteData& Data : Sites)
	{
		for (const FMobileBuilding& Building : Data.Buildings)
		{
			if (Building.Id != BuildingId || !Building.Levels.ContainsByPredicate([Level](const FMobileLevel& L) { return L.Level == Level; }))
			{
				continue;
			}
			OutMap = FMobileLevelMap();
			OutMap.BuildingId = BuildingId;
			OutMap.Level = Level;
			OutMap.Outline = LevelOutline;
			OutMap.Entrance = Entrance;
			// A room for each place of the level, around the place's position, as in the prototype's schematic plans.
			for (const FMobilePlace& Place : Data.Places)
			{
				if (Place.BuildingId == BuildingId && Place.Level == Level)
				{
					FMobileMapRoom Room;
					Room.PlaceId = Place.Id;
					Room.Name = Place.Name;
					Room.Category = Place.Category;
					Room.bAvailable = Place.bAvailable;
					Room.Polygon = { FVector2D(Place.Position.X - RoomHalfSize, Place.Position.Y - RoomHalfSize), FVector2D(Place.Position.X + RoomHalfSize, Place.Position.Y - RoomHalfSize),
						FVector2D(Place.Position.X + RoomHalfSize, Place.Position.Y + RoomHalfSize), FVector2D(Place.Position.X - RoomHalfSize, Place.Position.Y + RoomHalfSize) };
					OutMap.Rooms.Add(Room);
				}
			}
			OutMap.Connectors.Add({ EMobileConnectorKind::Lift, LiftAt });
			OutMap.Connectors.Add({ EMobileConnectorKind::Stairs, StairsAt });
			if (Data.bHasEscalators)
			{
				OutMap.Connectors.Add({ EMobileConnectorKind::Escalator, EscalatorAt });
			}
			return true;
		}
	}
	return false;
}

FMobileCampusMap FMobileMockSource::GetCampusMap(const FString& SiteId)
{
	const FSiteData* Data = FindSite(SiteId);
	return Data ? Data->CampusMap : FMobileCampusMap();
}

FString FMobileMockSource::GetDataNote()
{
	return DataNote;
}
