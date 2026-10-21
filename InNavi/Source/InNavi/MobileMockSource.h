#pragma once

#include "CoreMinimal.h"
#include "MobileNavSource.h"

// The design team's prototype as data: three sites (Paya Lebar Mall, Pathfinder Centre, the SIT Punggol campus with buildings E2, E6 and W2),
// their levels (E6 has a mezzanine, L2M), four places on each level, and routes made up from them. Nothing here reads the real project;
// it exists so the screens can be built and looked at before they are wired to the real code, and it stays as the source the tests use.
// Delete it (and nothing else) when the app has a real source it can run without.
class INNAVI_API FMobileMockSource : public IMobileNavSource
{
public:
	FMobileMockSource();

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
	};

	void AddSite(const FMobileSite& Site, bool bHasEscalators, const TArray<FMobileBuilding>& Buildings, bool bMall);
	const FSiteData* FindSite(const FString& SiteId) const;
	const FSiteData* FindSiteOfPlace(const FString& PlaceId) const;
	FString LevelLabel(const FSiteData& Data, const FString& BuildingId, int32 Level) const;

	TArray<FSiteData> Sites;
};
