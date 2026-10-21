#include "MobileMockSource.h"

#include "NavigationSearchUtils.h"

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
	FMobileLevel MakeLevel(int32 Level, const TCHAR* Label)
	{
		FMobileLevel Result;
		Result.Level = Level;
		Result.Label = Label;
		return Result;
	}

	FMobileBuilding MakeBuilding(const TCHAR* Id, const TCHAR* Name, const TArray<FMobileLevel>& Levels)
	{
		FMobileBuilding Result;
		Result.Id = Id;
		Result.Name = Name;
		Result.Levels = Levels;
		return Result;
	}

	// The four places of every level, at the corners of its schematic map (0-100 each way), as in the prototype.
	struct FPlaceTemplate
	{
		const TCHAR* Name;
		int32 Number;
		const TCHAR* Category;
		const TCHAR* RoomType;
		double X, Y;
	};

	FMobileRoute NoRoute(const FString& Error)
	{
		FMobileRoute Route;
		Route.Error = Error;
		return Route;
	}

	// Where a person standing at the building's door starts.
	const FVector2D MobileEntrance(50.0, 95.0);
}

FMobileMockSource::FMobileMockSource()
{
	FMobileSite PayaLebar;
	PayaLebar.Id = TEXT("paya-lebar");
	PayaLebar.Name = TEXT("Paya Lebar Mall");
	PayaLebar.Subtitle = TEXT("Paya Lebar · Singapore");
	PayaLebar.BuildingCount = 1;
	PayaLebar.LevelCount = 7;
	PayaLebar.DistanceLabel = TEXT("2.4 km");
	TArray<FMobileLevel> MallLevels;
	for (int32 Level = 1; Level <= 7; ++Level)
	{
		MallLevels.Add(MakeLevel(Level, *FString::Printf(TEXT("L%d"), Level)));
	}
	AddSite(PayaLebar, true, { MakeBuilding(TEXT("PLM"), TEXT("Paya Lebar Mall"), MallLevels) }, true);

	FMobileSite Pathfinder;
	Pathfinder.Id = TEXT("pathfinder-centre");
	Pathfinder.Name = TEXT("Pathfinder Centre");
	Pathfinder.Subtitle = TEXT("Learning & community spaces");
	Pathfinder.BuildingCount = 1;
	Pathfinder.LevelCount = 3;
	Pathfinder.DistanceLabel = TEXT("3.1 km");
	AddSite(Pathfinder, false, { MakeBuilding(TEXT("PFC"), TEXT("Pathfinder Centre"), { MakeLevel(1, TEXT("L1")), MakeLevel(2, TEXT("L2")), MakeLevel(3, TEXT("L3")) }) }, false);

	FMobileSite Campus;
	Campus.Id = TEXT("sit-punggol");
	Campus.Name = TEXT("SIT Punggol Campus");
	Campus.ShortName = TEXT("SIT Campus");
	Campus.Subtitle = TEXT("Campus demo · Schematic maps");
	Campus.BuildingCount = 3;
	Campus.LevelCount = 4;
	Campus.DistanceLabel = TEXT("Demo");
	AddSite(Campus, false, {
		MakeBuilding(TEXT("E2"), TEXT("E2"), { MakeLevel(1, TEXT("L1")), MakeLevel(2, TEXT("L2")), MakeLevel(3, TEXT("L3")) }),
		MakeBuilding(TEXT("E6"), TEXT("E6"), { MakeLevel(1, TEXT("L1")), MakeLevel(2, TEXT("L2")), MakeLevel(3, TEXT("L2M")), MakeLevel(4, TEXT("L3")) }),
		MakeBuilding(TEXT("W2"), TEXT("W2"), { MakeLevel(1, TEXT("L1")), MakeLevel(2, TEXT("L2")) }) }, false);
}

void FMobileMockSource::AddSite(const FMobileSite& Site, bool bHasEscalators, const TArray<FMobileBuilding>& Buildings, bool bMall)
{
	FSiteData Data;
	Data.Site = Site;
	Data.bHasEscalators = bHasEscalators;
	Data.Buildings = Buildings;

	static const FPlaceTemplate CampusPlaces[] = {
		{ TEXT("Study lounge"), 101, TEXT("Learning"), TEXT("Study area"), 22.0, 25.0 },
		{ TEXT("Classroom"), 102, TEXT("Learning"), TEXT("Classroom"), 78.0, 25.0 },
		{ TEXT("Restrooms"), 103, TEXT("Facilities"), TEXT("Toilet"), 78.0, 75.0 },
		{ TEXT("Lift lobby"), 104, TEXT("Facilities"), TEXT("Lift lobby"), 22.0, 75.0 },
	};
	static const FPlaceTemplate MallPlaces[] = {
		{ TEXT("Pepper Lunch"), 101, TEXT("Dining"), TEXT("Canteen"), 22.0, 25.0 },
		{ TEXT("Café Terrace"), 102, TEXT("Dining"), TEXT("Canteen"), 78.0, 25.0 },
		{ TEXT("Restrooms"), 103, TEXT("Facilities"), TEXT("Toilet"), 78.0, 75.0 },
		{ TEXT("Information desk"), 104, TEXT("Facilities"), TEXT("Other"), 22.0, 75.0 },
	};
	const FPlaceTemplate* Templates = bMall ? MallPlaces : CampusPlaces;

	for (const FMobileBuilding& Building : Buildings)
	{
		for (const FMobileLevel& Level : Building.Levels)
		{
			// "L2M" is level 2's mezzanine: its code says 2M, the others say the level number.
			const FString LevelCode = Level.Label.StartsWith(TEXT("L")) ? Level.Label.Mid(1) : Level.Label;
			const FString Padded = LevelCode.Len() < 2 && LevelCode.IsNumeric() ? TEXT("0") + LevelCode : LevelCode;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const FPlaceTemplate& Template = Templates[Index];
				FMobilePlace Place;
				Place.Name = Template.Name;
				Place.SiteId = Site.Id;
				Place.BuildingId = Building.Id;
				Place.BuildingName = Building.Name;
				Place.Level = Level.Level;
				Place.LevelLabel = Level.Label;
				Place.Category = Template.Category;
				Place.RoomType = Template.RoomType;
				Place.Position = FVector2D(Template.X, Template.Y);
				// A mall prints "#01-101"; a campus "E2-01-101".
				Place.Code = bMall ? FString::Printf(TEXT("#%s-%d"), *Padded, Template.Number) : FString::Printf(TEXT("%s-%s-%d"), *Building.Id, *Padded, Template.Number);
				Place.Id = Building.Id + TEXT("-") + Padded + TEXT("-") + FString::FromInt(Template.Number);
				// The mall's second restaurant is closed on level 1 (the prototype shows it "Unavailable").
				Place.bAvailable = !(bMall && Index == 1 && Level.Level == 1);
				Place.Description = TEXT("Campus demonstration destination. Location and map are schematic.");
				Data.Places.Add(Place);
			}
		}
	}
	Sites.Add(MoveTemp(Data));
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
	FVector2D FromPosition = MobileEntrance;
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
	auto ConnectorAt = [](EMobileStepKind Kind) { return Kind == EMobileStepKind::TakeLift ? FVector2D(44.0, 50.0) : (Kind == EMobileStepKind::TakeStairs ? FVector2D(56.0, 50.0) : FVector2D(50.0, 38.0)); };

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
		Walk(EMobileStepKind::Straight, TEXT("Follow the corridor to the exit"), MobileEntrance);
		AddStep(EMobileStepKind::ExitBuilding, FString::Printf(TEXT("Leave %s"), *FromBuilding), CurBuilding, 1, 5.0, CurPosition);
		AddStep(EMobileStepKind::Straight, FString::Printf(TEXT("Walk along the campus path to %s"), *To.BuildingName), CurBuilding, 1, 90.0, CurPosition);
		CurBuilding = To.BuildingId;
		Mark(CurBuilding, 1, MobileEntrance);
		AddStep(EMobileStepKind::EnterBuilding, FString::Printf(TEXT("Enter %s"), *To.BuildingName), CurBuilding, 1, 5.0, MobileEntrance);
	}
	if (!ChangeLevel(To.Level))
	{
		Route.Error = TEXT("There is no way between levels with the options you chose.");
		return NoRoute(Route.Error);
	}

	// The last stretch: along the corridor to the crossing, then along it to the room's door and in.
	const FVector2D Crossing(50.0, 50.0);
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

namespace
{
	TArray<FVector2D> Box(double X0, double Y0, double X1, double Y1)
	{
		return { FVector2D(X0, Y0), FVector2D(X1, Y0), FVector2D(X1, Y1), FVector2D(X0, Y1) };
	}
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
			OutMap.Outline = Box(0.0, 0.0, 100.0, 100.0);
			OutMap.Entrance = MobileEntrance;
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
					Room.Polygon = Box(Place.Position.X - 15.0, Place.Position.Y - 15.0, Place.Position.X + 15.0, Place.Position.Y + 15.0);
					OutMap.Rooms.Add(Room);
				}
			}
			OutMap.Connectors.Add({ EMobileConnectorKind::Lift, FVector2D(44.0, 50.0) });
			OutMap.Connectors.Add({ EMobileConnectorKind::Stairs, FVector2D(56.0, 50.0) });
			if (Data.bHasEscalators)
			{
				OutMap.Connectors.Add({ EMobileConnectorKind::Escalator, FVector2D(50.0, 38.0) });
			}
			return true;
		}
	}
	return false;
}

FMobileCampusMap FMobileMockSource::GetCampusMap(const FString& SiteId)
{
	FMobileCampusMap Map;
	const FSiteData* Data = FindSite(SiteId);
	if (!Data)
	{
		return Map;
	}
	if (Data->Buildings.Num() == 1)
	{
		FMobileCampusBuilding Building;
		Building.BuildingId = Data->Buildings[0].Id;
		Building.Name = Data->Buildings[0].Name;
		Building.Outline = Box(20.0, 25.0, 80.0, 75.0);
		Map.Buildings.Add(Building);
		return Map;
	}
	// The campus of the prototype: E2 and E6 along the top, W2 below E2, a pond and a courtyard between.
	const TArray<TArray<FVector2D>> Outlines = { Box(6.0, 8.0, 42.0, 38.0), Box(58.0, 8.0, 94.0, 38.0), Box(6.0, 58.0, 42.0, 88.0) };
	for (int32 i = 0; i < Data->Buildings.Num() && i < Outlines.Num(); ++i)
	{
		FMobileCampusBuilding Building;
		Building.BuildingId = Data->Buildings[i].Id;
		Building.Name = Data->Buildings[i].Name;
		Building.Outline = Outlines[i];
		Map.Buildings.Add(Building);
	}
	Map.Features.Add({ TEXT("Courtyard"), Box(44.0, 12.0, 56.0, 40.0) });
	Map.Features.Add({ TEXT("Pond"), Box(58.0, 58.0, 94.0, 88.0) });
	return Map;
}

FString FMobileMockSource::GetDataNote()
{
	return TEXT("Interactive design prototype \u00B7 All maps, places and positioning are simulated.");
}
