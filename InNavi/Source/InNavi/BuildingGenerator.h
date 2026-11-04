// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BuildingSummary.h"
#include "InteriorEntrance.h"
#include "GameFramework/Actor.h"
#include "RoadSegmentActor.h"
#include "BuildingGenerator.generated.h"

class AContextBuildingActor;
class APolyBuilding;
class AStaticMeshActor;
class FJsonObject;

DECLARE_MULTICAST_DELEGATE(FOnBuildingNavigationLayoutChanged);

UCLASS()
class INNAVI_API ABuildingGenerator : public AActor
{
	GENERATED_BODY()

public:
	ABuildingGenerator();
	FOnBuildingNavigationLayoutChanged OnNavigationLayoutChanged;
	uint64 GetNavigationRevision() const { return NavigationRevision; }

	// Spawns one building directly from already-parsed data. Used both by JSON loading
	// (LoadBuildingsFromFile) and by runtime building creation (e.g. NavPlayerController's build mode).
	// Returns nullptr if Id is empty, already used, or Footprint has fewer than 3 points.
	APolyBuilding* SpawnBuildingFromData(const FString& Id, const FString& Name, const TArray<FVector2D>& Footprint, int32 FloorCount, float FloorHeight, FVector2D Offset);

	// SpawnBuildingFromData multiplies footprint+offset by this. Callers building a footprint
	// out of world-space coordinates (e.g. from a raycast) must divide by this first.
	float GetScale() const { return Scale; }

	// Plan B: builds an axis-aligned-then-rotated rectangle footprint and spawns it.
	// Position/Width/Depth are world-space units (same units you'd read off in-level), matching
	// how Plan A's click points work. This is the function Padre's UI form should call directly -
	// it does not go through JSON.
	UFUNCTION(BlueprintCallable, Category = "Building")
	APolyBuilding* CreateRectangleBuilding(const FString& Id, const FString& Name, FVector2D Position, float Width, float Depth, float RotationDegrees, int32 FloorCount, float FloorHeight);

	// Plan B: builds a regular N-sided polygon footprint (Sides=3 triangle, 5 pentagon, 6 hexagon,
	// ...) and spawns it. Position/Radius are world-space units, same convention as
	// CreateRectangleBuilding. Radius is the distance from the center to each vertex.
	UFUNCTION(BlueprintCallable, Category = "Building")
	APolyBuilding* CreateRegularPolygonBuilding(const FString& Id, const FString& Name, FVector2D Position, int32 Sides, float Radius, float RotationDegrees, int32 FloorCount, float FloorHeight);

	// Parses a buildings.json-format file (same schema as Data/buildings.json - "scale",
	// "floorHeight", "buildings": [...]) and spawns everything in it via SpawnBuildingFromData.
	// Nothing loads automatically at BeginPlay - the start screen's Open/New choice is what decides
	// whether (and which) building data gets loaded. The file's top-level "scale" overwrites Scale
	// for the whole generator (affects unit conversion for anything created afterward, including
	// Plan A/B) - already-spawned buildings keep their existing geometry either way. Importing a
	// second file with a different scale is not something the UI should offer without a warning.
	// Each building entry may carry an optional "interior" sub-object - this is stashed (not
	// interpreted) per id for InteriorGenerator to read via GetInteriorDataForBuilding, so a
	// building's interior is bound to the specific project file it came from, not just its id
	// string (two different projects can safely reuse the same building ids).
	// Returns how many buildings were successfully spawned.
	UFUNCTION(BlueprintCallable, Category = "Building")
	int32 LoadBuildingsFromFile(const FString& FilePath);
	// Same loader for a detached in-memory project (mobile navigation never writes a temporary project file).
	int32 LoadBuildingsFromJson(const TSharedPtr<FJsonObject>& RootObject);

	// Second, database-backed way to load exterior buildings - queries a SQLite file (schema:
	// buildings(building_id, building_type), building_nodes(building_id, node_id, sequence, lat,
	// lon), matching the "Map Viewer" project's own schema - see project memory) instead of parsing
	// JSON. Each building's ordered lat/lon nodes are projected to local meters around the centroid
	// of every point read (so the cluster lands near the world origin regardless of where on Earth it
	// actually is), then spawned via the same SpawnBuildingFromData every other loading path uses -
	// no interior data is involved here at all, unlike LoadBuildingsFromFile's "interior" field:
	// interiors are always hand-authored through the interior editor regardless of where a building's
	// exterior came from. Sets Scale to 100 (footprint units become real-world meters directly, since
	// this data - unlike buildings.json's hand-authored, non-geographic footprints - already IS the
	// real campus at real scale). Returns how many buildings were successfully spawned.
	UFUNCTION(BlueprintCallable, Category = "Building")
	int32 LoadBuildingsFromDatabase(const FString& DatabasePath);

	// Loads roads (as flat ribbon actors) and points of interest (as small marker actors) from the
	// SAME database LoadBuildingsFromDatabase reads buildings from - must be called AFTER a
	// successful LoadBuildingsFromDatabase(DatabasePath) using that exact same path, since it reuses
	// that call's own lat/lon reference point (see bHasDatabaseRefPoint) so roads/POIs/buildings all
	// line up in the same local-meters space instead of each projecting around their own centroid.
	// Loads every road/POI in the database, not just ones near whatever the buildings query filtered
	// to (this sample dataset covers the whole surrounding neighborhood, not just the campus) - purely
	// a first look at what's in the data, not tuned for how much clutter that turns out to be.
	// Clears any previously-loaded roads/POIs first. Returns how many actors were spawned.
	UFUNCTION(BlueprintCallable, Category = "Building")
	int32 LoadRoadsAndPOIsFromDatabase(const FString& DatabasePath);

	// Destroys every currently-spawned road/POI actor - same idea as ClearAllBuildings.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void ClearAllRoadsAndPOIs();

	// Loads every OTHER building in the database (building_type != 'university') as a simple,
	// visual-only extruded-block placeholder (AContextBuildingActor) - context for the surrounding
	// neighborhood, not a real APolyBuilding: no floors, no interior editing, not part of
	// GetAllBuildings() (so it can't be entered, switched to, or mistaken for one of the real campus
	// buildings camera-framing/bounds code already relies on GetAllBuildings() meaning "the campus").
	// Must be called AFTER LoadBuildingsFromDatabase (same path) for the same shared-reference-point
	// reason as LoadRoadsAndPOIsFromDatabase. Clears any previously-loaded context buildings first.
	// Returns how many actors were spawned.
	UFUNCTION(BlueprintCallable, Category = "Building")
	int32 LoadContextBuildingsFromDatabase(const FString& DatabasePath);

	// Destroys every currently-spawned context-building actor - same idea as ClearAllRoadsAndPOIs.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void ClearAllContextBuildings();

	// Shows/hides every REAL campus building at once (the ones in GetAllBuildings()/BuildingsById) -
	// the filter panel's "Campus" building-visibility checkbox. Also toggles collision, so a hidden
	// building can't still be clicked to select/enter (mirrors the reasoning anywhere else in this
	// codebase hides a whole building, e.g. NavPlayerController's other-buildings-hidden-in-3D-preview).
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetCampusBuildingsVisible(bool bVisible);
	void SetBuildingFilter(const FString& Query, int32 MinimumFloors, int32 MaximumFloors);
	void ApplyBuildingFilter();
	bool MatchesBuildingFilter(const APolyBuilding& Building) const;
	TArray<APolyBuilding*> GetFilteredBuildings() const;
	FString GetBuildingFilterText() const { return BuildingFilterText; }
	int32 GetBuildingFilterMinimumFloors() const { return BuildingFilterMinimumFloors; }
	int32 GetBuildingFilterMaximumFloors() const { return BuildingFilterMaximumFloors; }
	bool AreCampusBuildingsVisible() const { return bCampusBuildingsVisible; }
	uint64 GetBuildingResultsRevision() const { return BuildingResultsRevision; }
	int32 GetBuildingCount() const { return BuildingsById.Num(); }
	// Lazily rebuilt once per data/filter change; idle UI frames and selection changes reuse it.
	const TMap<FString, FString>& GetFilteredBuildingOptions() const;

	// Shows/hides every spawned context-building actor at once - the filter panel's "Non-Campus"
	// building-visibility checkbox. These are visual-only placeholders (see
	// LoadContextBuildingsFromDatabase), so no collision toggle is needed.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetNonCampusBuildingsVisible(bool bVisible);

	// What kind of building a campus building is (one value from BuildingTypes::All()). False when there is no such building. A change
	// counts as a change of the results, so the lists and the Building panel follow it, and the building is shown or hidden as the type
	// filter says.
	bool SetBuildingType(const FString& BuildingId, const FString& Type);
	FString GetBuildingType(const FString& BuildingId) const;

	// Shows/hides the buildings of one type, campus and non-campus alike - the Map panel's type check boxes. Not saved in the project.
	void SetBuildingTypeVisible(const FString& Type, bool bVisible);
	bool IsBuildingTypeVisible(const FString& Type) const;
	// Shows every type again.
	void ResetBuildingTypeFilter();

	// Shows/hides every currently-spawned road actor in Category at once - what a road-visibility
	// filter panel's checkboxes call. Takes effect immediately (no reload/re-query needed) since
	// every road actor already remembers its own category (see ARoadSegmentActor::GetCategory).
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetRoadCategoryVisible(ERoadCategory Category, bool bVisible);

	// Shows/hides every spawned road actor at once, regardless of category - the filter panel's
	// master "Hide All" checkbox. Goes through the same SetCategoryVisible flag SetRoadCategoryVisible
	// uses (not a second, competing visibility path), so it still combines correctly with the campus-
	// range filter (SetRoadsAndPOIsWithinCampusOnly).
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetAllRoadsVisible(bool bVisible);

	// Hides every spawned road/POI actor farther than Radius (world units) from Center; shows the rest
	// again (subject to their own category filter, for roads) when bEnable is false. Center/Radius are
	// computed by the caller (ANavPlayerController::SetRoadsAndPOIsWithinCampusOnly, from the loaded
	// buildings' own extent) - this function just applies them.
	UFUNCTION(BlueprintCallable, Category = "Building")
	void SetRoadsAndPOIsWithinCampusOnly(bool bEnable, const FVector2D& Center, float Radius);

	// The ground under the campus. The level has one floor (a static mesh actor using the template map's floor mesh); it is made the size of
	// what the Map panel's filters show - the campus buildings that pass the filters, the other buildings, the roads and the places - with a
	// margin, so everything seen stands on ground, and a smaller area when less is shown. It follows every change of a filter and every
	// load or clear, a moment later (once per frame at most). With nothing shown, or no such floor in the level, it is left as it is.
	void UpdateGroundToVisible();
	// Same explicit template-floor lookup as ground fitting, without moving it.
	AStaticMeshActor* FindGroundActor();
	void SetGroundFitSuspended(bool bSuspended) { bGroundFitSuspended = bSuspended; }
	bool IsGroundFitSuspended() const { return bGroundFitSuspended; }

	// The area of the world (X, Y) that what the filters show covers; false when nothing is shown.
	bool GetVisibleExtent(FBox2D& OutExtent) const;

	// Looks up an already-spawned building by id (e.g. for InteriorGenerator to find which
	// building's shell a piece of interior data belongs to). Returns nullptr if not found.
	UFUNCTION(BlueprintCallable, Category = "Building")
	APolyBuilding* FindBuildingById(const FString& Id) const;

	// Every currently-spawned building, in no particular order - e.g. for framing a camera view
	// around the whole cluster instead of just one building.
	UFUNCTION(BlueprintCallable, Category = "Building")
	TArray<APolyBuilding*> GetAllBuildings() const;

	// Every currently-spawned building's id, in no particular order - for a building-switcher
	// dropdown to list, since ids (not names) are what LoadInteriorForBuilding takes.
	UFUNCTION(BlueprintCallable, Category = "Building")
	TArray<FString> GetAllBuildingIds() const;
	FString GenerateUniqueBuildingId(const FString& BaseId) const;
	// Reads a building's current human-readable name - mirrors AInteriorGenerator::GetRoomName.
	// Returns an empty string if no building with that id exists.
	UFUNCTION(BlueprintPure, Category = "Building")
	FString GetBuildingName(const FString& BuildingId) const;

	// Renames a building. Names (not just ids) need to be unique across all buildings - a
	// building-switcher dropdown shows/picks by name, so a collision would make that ambiguous
	// (same reasoning as AInteriorGenerator::SetRoomName for rooms/walls). Returns false if no
	// building with that id exists, or if NewName is already used by a different building.
	UFUNCTION(BlueprintCallable, Category = "Building")
	bool SetBuildingName(const FString& BuildingId, const FString& NewName);

	// Resolves a building's current display name back to its stable id - what a building-switcher
	// dropdown's "apply selection" should call before feeding the result to
	// LoadInteriorForBuilding, since the dropdown's options are names, not ids (mirrors
	// NavPlayerController::SetSelectedDoorConnectsTo's name-to-id resolution for rooms). Returns
	// an empty string if no building currently has that name.
	UFUNCTION(BlueprintPure, Category = "Building")
	FString FindBuildingIdByName(const FString& Name) const;

	// Writes every currently-spawned building back out as a buildings.json-format file (the same
	// schema LoadBuildingsFromFile reads) - the "Save" side of the Open/New + Ctrl+S save flow
	// (see NavPlayerController::SaveCurrentProject). Each building's stored interior data (see
	// GetInteriorDataForBuilding) is written back out as that building's "interior" field, if any -
	// callers editing an interior should call InteriorGenerator::SyncInteriorToBuildingGenerator()
	// first so in-progress edits aren't lost. Returns true on success.
	UFUNCTION(BlueprintCallable, Category = "Building")
	bool SaveBuildingsToFile(const FString& FilePath) const;

	// The project's own name and description, written to the file's top level as "projectName" and
	// "description" and read back by LoadBuildingsFromFile. Empty for a file made before they existed.
	const FString& GetProjectName() const { return ProjectName; }

	// The format version written into every project file as "version". A file from before it existed has none and is read as
	// version 0, which is the same format as 1. Raise this when the format changes in a way an older reader would misread, and
	// convert older files in LoadBuildingsFromFile by the version it reports here.
	static constexpr int32 ProjectFileVersion = 1;
	int32 GetLoadedFileVersion() const { return LoadedFileVersion; }
	const FString& GetProjectDescription() const { return ProjectDescription; }
	void SetProjectInfo(const FString& Name, const FString& Description)
	{
		ProjectName = Name;
		ProjectDescription = Description;
	}

	// Stage the new buildings before changing the live project. BeforeReplace runs only after a successful load,
	// while the caller can still close the old interior. Returns INDEX_NONE on failure, or a count (zero for an empty JSON project).
	int32 ReplaceBuildingsFromPath(const FString& Path, bool bDatabase, TFunctionRef<void()> BeforeReplace, FString& OutProblem);

	// The raw "interior" JSON sub-object associated with a building id, as loaded from (or to be
	// written into) the combined project file - not interpreted here, that's InteriorGenerator's
	// schema. Returns nullptr if this building has no interior data yet.
	TSharedPtr<FJsonObject> GetInteriorDataForBuilding(const FString& BuildingId) const;

	// False if there is no such building.
	bool GetBuildingSummary(const FString& BuildingId, FBuildingSummary& OutSummary) const;

	// Stashes the interior data for a building id, overwriting whatever was there before (e.g.
	// InteriorGenerator syncing its current in-memory state before a project save).
	// A live-to-stored sync passes false: model edits already notified navigation, while saving/viewing is not an edit.
	void SetInteriorDataForBuilding(const FString& BuildingId, TSharedPtr<FJsonObject> InteriorData, bool bNotifyNavigation = true);

	// --- Doors in the outer wall (see InteriorEntrance.h) ---
	// Those saved with a building's interior.
	TArray<FInteriorEntrance> GetStoredEntrances(const FString& BuildingId) const;
	// Other buildings' Link doors to this one, as they show here: not stored twice, worked out from the other building's door.
	TArray<FInteriorEntrance> GetMirroredEntrances(const FString& BuildingId) const;
	// Where Source (a Link door on SourceBuildingId) shows on OnBuildingId: at the nearest point of that building's outline. False if no
	// edge there is long enough for the opening.
	bool MakeMirrorOfEntrance(const FString& SourceBuildingId, const FInteriorEntrance& Source, const FString& OnBuildingId, FInteriorEntrance& OutMirror) const;
	// Takes one saved door out of a building's interior data (also what removes the mirror of it from the building next door).
	bool RemoveStoredEntrance(const FString& BuildingId, const FString& EntranceId);
	// Cuts every building's outer wall to match its doors, its own and the mirrored ones.
	void RefreshWallOpenings();

	// --- "Discard unsaved changes" ---
	// Editing a building's interior changes things that live on the building itself (its floor count -
	// the exterior shell's height - and its name) as well as its interior data. To be able to throw those
	// edits away without reloading the whole project (a database can be huge), a small baseline is kept
	// for each building that has been opened for editing: just those three things, not the project.
	//
	// Captures the building's current state as its baseline, unless one is already kept (so entering the
	// same building twice does not overwrite what the last save/open left behind). Call right before a
	// building's interior is opened.
	void CaptureBuildingBaseline(const FString& BuildingId);

	// Re-captures every kept baseline from the current state - call after a successful save, which is
	// what the baselines are meant to represent.
	void RefreshBuildingBaselines();

	// Puts every building that has a baseline back to it (floor count, name, interior data). Buildings
	// that no longer exist are skipped. Does not touch the live InteriorGenerator - callers leave or
	// reload the interior afterwards.
	void RestoreBuildingBaselines();

	bool HasBuildingBaseline(const FString& BuildingId) const { return BuildingBaselines.Contains(BuildingId); }

	// Destroys every currently-spawned building and forgets all interior data - back to the state
	// right after BeginPlay, nothing loaded. Call this before starting a New project or before
	// LoadBuildingsFromFile, so leftover buildings from whatever was open before don't stick around
	// (LoadBuildingsFromFile only adds - it never removes what's already there on its own).
	UFUNCTION(BlueprintCallable, Category = "Building")
	void ClearAllBuildings();

private:
	void HandleBuildingFloorCountChanged(APolyBuilding* Building);
	UFUNCTION()
	void HandleBuildingDestroyed(AActor* Building);
	void RefreshBuildingFilterFor(APolyBuilding& Building) const;
	// A non-campus building is shown when the Non-Campus filter and the filter of its type both allow it.
	bool ShouldShowContextBuilding(const AContextBuildingActor& Building) const;
	void RefreshContextBuildingVisibility();

	uint64 NavigationRevision = 0;
	void NotifyNavigationLayoutChanged();
	float Scale = 1.f;
	TSet<FString> UsedBuildingIds;

	// LoadBuildingsFromDatabase's fallback FloorCount/FloorHeight - the buildings/building_nodes
	// schema has no floor data at all (it's OSM footprint+type only), unlike buildings.json's
	// per-building "floorCount"/"floorHeight" fields.
	static constexpr int32 DefaultFloorCountForDatabaseBuildings = 5;
	static constexpr float DefaultFloorHeightForDatabaseBuildings = 300.f;

	UPROPERTY()
	TMap<FString, APolyBuilding*> BuildingsById;

	TMap<FString, TSharedPtr<FJsonObject>> InteriorDataById;

	struct FBuildingBaseline
	{
		int32 FloorCount = 1;
		FString Name;
		bool bHadInterior = false;
		FString InteriorJson; // the interior data serialized, so later edits to the live object cannot reach it
	};
	TMap<FString, FBuildingBaseline> BuildingBaselines;
	bool CaptureBuildingBaselineInto(const FString& BuildingId, FBuildingBaseline& Out) const;
	FString BuildingFilterText;
	TArray<FString> BuildingFilterWords;
	int32 BuildingFilterMinimumFloors = 0;
	int32 BuildingFilterMaximumFloors = 0;
	bool bCampusBuildingsVisible = true;
	uint64 BuildingResultsRevision = 0;
	mutable uint64 CachedBuildingResultsRevision = MAX_uint64;
	mutable TMap<FString, FString> CachedBuildingOptions;
	FString ProjectName;
	FString ProjectDescription;
	int32 LoadedFileVersion = 0;

	// The lat/lon centroid LoadBuildingsFromDatabase projects around, stashed so
	// LoadRoadsAndPOIsFromDatabase can project roads/POIs into that exact same local-meters space
	// (see its own comment). bHasDatabaseRefPoint is false until a database load actually succeeds.
	bool bHasDatabaseRefPoint = false;
	double DatabaseRefLat = 0.0;
	double DatabaseRefLon = 0.0;

	// Every database building_id LoadBuildingsFromDatabase decided belongs to the campus - the
	// 'university'-tagged seed plus anything pulled in by its own adjacency pass (see that function's
	// comment). LoadContextBuildingsFromDatabase reads this to skip spawning a context placeholder for
	// a building that's already a real, interior-capable campus building.
	TSet<int64> DatabaseCampusBuildingIds;

	UPROPERTY()
	TArray<AActor*> SpawnedRoadActors;

	UPROPERTY()
	TArray<AActor*> SpawnedPOIActors;

	UPROPERTY()
	TArray<AActor*> SpawnedContextBuildingActors;

	// Whether the filter panel shows the other (non-campus) buildings: SetNonCampusBuildingsVisible.
	bool bNonCampusBuildingsVisible = true;

	// The types the filter panel hides: SetBuildingTypeVisible. Empty when every type is shown.
	TSet<FString> HiddenBuildingTypes;

	// The level's floor and how it was, found the first time the ground is updated (see UpdateGroundToVisible).
	TWeakObjectPtr<AStaticMeshActor> GroundActor;
	FTransform GroundOriginalTransform;
	bool bGroundSearched = false;
	bool bGroundFitSuspended = false;
	bool bGroundUpdateQueued = false;
	void MarkGroundDirty();
};
