#pragma once

#include "CoreMinimal.h"
#include "MobileNavSource.h"

// The design team's prototype as data: three sites (Paya Lebar Mall, Pathfinder Centre, the SIT Punggol campus with buildings E2, E6 and W2),
// their levels (E6 has a mezzanine, L2M), four places on each level, and routes made up from them. Nothing here reads the real project;
// it exists so the screens can be built and looked at before they are wired to the real code, and it stays as the source the tests use.
//
// The data is not in the code: it is `InNavi/Data/Prototype/mobile-prototype.json`, a file that says it is synthetic (`"synthetic": true`)
// and carries the note the screens show ("All maps, places and positioning are simulated"). This class only turns that file into places,
// maps and walks. A file that is missing or does not read leaves the source empty (IsLoaded is false) and says why in the log.
// Delete the class and the file when the app has a real source it can run without.
class INNAVI_API FMobileMockSource : public IMobileNavSource
{
public:
	// The prototype of the project's own data folder.
	FMobileMockSource();
	// Another file of the same format (the tests use this).
	explicit FMobileMockSource(const FString& DataFile);

	static FString DefaultDataFile();
	// False when the file could not be read; the source then has no sites.
	bool IsLoaded() const { return !Sites.IsEmpty(); }
	// Why it could not be read, empty when it was.
	const FString& GetLoadError() const { return LoadError; }

	virtual FString GetDataNote() override;
	virtual TArray<FMobileSite> ListSites() override;
	virtual TArray<FMobileBuilding> ListBuildings(const FString& SiteId) override;
	virtual TArray<FString> ListCategories(const FString& SiteId) override;
	virtual TArray<FMobilePlace> SearchPlaces(const FString& SiteId, const FString& Query, const FMobilePlaceFilter& Filter) override;
	virtual bool FindPlace(const FString& PlaceId, FMobilePlace& OutPlace) override;
	virtual FMobileRoute FindRoute(const FMobileRouteRequest& Request) override;
	virtual bool GetLevelMap(const FString& BuildingId, int32 Level, FMobileLevelMap& OutMap) override;
	virtual FMobileCampusMap GetCampusMap(const FString& SiteId) override;

private:
	struct FSiteData
	{
		FMobileSite Site;
		bool bHasEscalators = false;
		TArray<FMobileBuilding> Buildings;
		TArray<FMobilePlace> Places;
		FMobileCampusMap CampusMap;
	};

	// One of the four places every level has, at the corners of its schematic map (0-100 each way), as in the prototype.
	struct FPlaceTemplate
	{
		FString Name;
		int32 Number = 0;
		FString Category;
		FString RoomType;
		FVector2D Position = FVector2D::ZeroVector;
		TArray<int32> UnavailableOnLevels;
	};

	void Load(const FString& DataFile);
	const FSiteData* FindSite(const FString& SiteId) const;
	const FSiteData* FindSiteOfPlace(const FString& PlaceId) const;
	FString LevelLabel(const FSiteData& Data, const FString& BuildingId, int32 Level) const;

	TArray<FSiteData> Sites;
	FString LoadError;
	FString DataNote;
	// The schematic plan every level shares: where a person comes in, where the corridors cross, the connectors, the outline of a level.
	FVector2D Entrance = FVector2D(50.0, 95.0);
	FVector2D Crossing = FVector2D(50.0, 50.0);
	FVector2D LiftAt = FVector2D(44.0, 50.0);
	FVector2D StairsAt = FVector2D(56.0, 50.0);
	FVector2D EscalatorAt = FVector2D(50.0, 38.0);
	TArray<FVector2D> LevelOutline;
	double RoomHalfSize = 15.0;
};
