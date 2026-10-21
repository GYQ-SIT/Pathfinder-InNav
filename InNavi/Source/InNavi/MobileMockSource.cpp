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
	// Where the lift, stairs and escalator are on every level.
	const FVector2D MobileConnectorPosition(50.0, 50.0);
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
		FMobileRoutePoint Point;
		Point.BuildingId = BuildingId;
		Point.Level = Level;
		Point.Position = Position;
		Route.Points.Add(Point);
	};

	// Between two levels of a building: the lift if it is allowed, else the escalator (where there is one), else the stairs.
	auto ChangeLevel = [&](const FString& BuildingId, int32 From, int32 To2) -> bool
	{
		if (From == To2)
		{
			return true;
		}
		const FString Target = LevelLabel(*Data, BuildingId, To2);
		const int32 Levels = FMath::Abs(To2 - From);
		if (Request.Preferences.bAllowLift)
		{
			AddStep(EMobileStepKind::TakeLift, FString::Printf(TEXT("Take the lift to %s"), *Target), BuildingId, To2, 12.0 + 4.0 * Levels, MobileConnectorPosition);
		}
		else if (Request.Preferences.bAllowEscalator && Data->bHasEscalators)
		{
			AddStep(EMobileStepKind::TakeEscalator, FString::Printf(TEXT("Take the escalator to %s"), *Target), BuildingId, To2, 18.0 + 6.0 * Levels, MobileConnectorPosition);
		}
		else if (Request.Preferences.bAllowStairs)
		{
			AddStep(EMobileStepKind::TakeStairs, FString::Printf(TEXT("Take the stairs to %s"), *Target), BuildingId, To2, 20.0 + 8.0 * Levels, MobileConnectorPosition);
		}
		else
		{
			return false;
		}
		return true;
	};

	AddStep(EMobileStepKind::Straight, TEXT("Start"), FromBuilding, FromLevel, 0.0, FromPosition);
	// Between buildings: down to the street level, out, along the campus path, in.
	if (FromBuilding != To.BuildingId)
	{
		if (!ChangeLevel(FromBuilding, FromLevel, 1))
		{
			Route.Error = TEXT("There is no way between levels with the options you chose.");
			return NoRoute(Route.Error);
		}
		AddStep(EMobileStepKind::Straight, TEXT("Follow the corridor to the exit"), FromBuilding, 1, 40.0, MobileEntrance);
		AddStep(EMobileStepKind::ExitBuilding, FString::Printf(TEXT("Leave %s"), *FromBuilding), FromBuilding, 1, 5.0, MobileEntrance);
		AddStep(EMobileStepKind::Straight, FString::Printf(TEXT("Walk along the campus path to %s"), *To.BuildingName), To.BuildingId, 1, 90.0, MobileEntrance);
		AddStep(EMobileStepKind::EnterBuilding, FString::Printf(TEXT("Enter %s"), *To.BuildingName), To.BuildingId, 1, 5.0, MobileEntrance);
		FromLevel = 1;
		FromPosition = MobileEntrance;
	}
	if (!ChangeLevel(To.BuildingId, FromLevel, To.Level))
	{
		Route.Error = TEXT("There is no way between levels with the options you chose.");
		return NoRoute(Route.Error);
	}
	if (FromLevel != To.Level)
	{
		FromPosition = MobileConnectorPosition;
	}
	AddStep(EMobileStepKind::Straight, TEXT("Follow the corridor"), To.BuildingId, To.Level, FMath::Max(20.0, FVector2D::Distance(FromPosition, To.Position) * 0.8), FromPosition);
	AddStep(To.Position.X < 50.0 ? EMobileStepKind::TurnLeft : EMobileStepKind::TurnRight, FString::Printf(TEXT("Turn towards %s"), *To.Name), To.BuildingId, To.Level, 20.0, To.Position);
	AddStep(EMobileStepKind::Arrive, FString::Printf(TEXT("You have arrived: %s"), *To.Name), To.BuildingId, To.Level, 0.0, To.Position);

	// The first "Start" point only anchors the drawing; it is not a step a person is told.
	Route.Steps.RemoveAt(0);
	Route.bFound = true;
	Route.Minutes = MobileNav::MinutesForMetres(Route.Metres);
	return Route;
}
