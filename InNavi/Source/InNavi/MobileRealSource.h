#pragma once

#include "CoreMinimal.h"
#include "MobileNavSource.h"

class ABuildingGenerator;
class AInteriorGenerator;
class UWorld;

// Read-only, cached project adapter. Each routing world is private: browsing another site never
// opens an editor building, changes its selection, or writes project data.
class INNAVI_API FMobileRealSource final : public IMobileNavSource
{
public:
	// A directory of JSON/SQLite projects, or one project file. Saved/MobileProjects can override shipped files by name.
	explicit FMobileRealSource(const FString& ProjectPath, const FString& OverridesDirectory = FString());
	FMobileRealSource(ABuildingGenerator* Generator, AInteriorGenerator* Interior, const FString& ProjectFile = FString());
	virtual ~FMobileRealSource() override;
	static TSharedRef<IMobileNavSource> FromWorld(UWorld* World);
	// Whether going on from Incoming to Outgoing (directions in footprint units) is a left turn as the person sees it on the map.
	static bool IsLeftTurnOnMap(const FVector2D& Incoming, const FVector2D& Outgoing);

	virtual TArray<FMobileSite> ListSites() override;
	virtual TArray<FMobileBuilding> ListBuildings(const FString& SiteId) override;
	virtual TArray<FString> ListCategories(const FString& SiteId) override;
	virtual TArray<FMobilePlace> SearchPlaces(const FString& SiteId, const FString& Query, const FMobilePlaceFilter& Filter) override;
	virtual bool FindPlace(const FString& PlaceId, FMobilePlace& OutPlace) override;
	virtual FMobileRoute FindRoute(const FMobileRouteRequest& Request) override;
	virtual bool GetLevelMap(const FString& BuildingId, int32 Level, FMobileLevelMap& OutMap) override;
	virtual FMobileCampusMap GetCampusMap(const FString& SiteId) override;

private:
	struct FProject;
	TArray<TSharedPtr<FProject>> Projects;
	void AddProject(const FString& Path);
	void IndexProject(FProject& Project);
	FProject* ProjectForSite(const FString& Id) const;
	FProject* ProjectForBuilding(const FString& Id) const;
};
