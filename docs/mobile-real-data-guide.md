# Connecting the phone app to real data

For teammates who will make the phone app (the one in the Android test build) show the **real** buildings, rooms and routes instead of the made-up ones.
Read this first; the design notes behind it are in [`design/mobile-app-data-interface.md`](design/mobile-app-data-interface.md).

## In one minute

- The phone app's screens never touch the editor's classes. They ask **one interface**, `IMobileNavSource`, for everything.
- Today the answers come from `FMobileMockSource` (the design prototype's data).
- To use real data you write a **second class**, `FMobileRealSource`, that implements the same interface from the real project data and the real route planner,
  then swap it in at **one line**. **No screen needs to change.**
- A ready-made test (`CheckContract`) tells you whether your source keeps the promises the screens rely on.

## Where everything is

All of the phone app is in `InNavi/Source/InNavi/`, in files whose names start with `Mobile`.

| What | File | You will |
|---|---|---|
| **The interface** and the data types (`FMobilePlace`, `FMobileRoute`, ...) | `MobileNavSource.h` | read it; add a field only after talking to whoever owns the screens |
| The fake data source | `MobileMockSource.h/.cpp` | read it as an example of every answer; do not edit |
| **Where the source is chosen** | `MobileAppWidget.cpp`, the line `Source = MakeShared<FMobileMockSource>();` (about line 300) | change this one line |
| The check every source must pass | `MobileNavSourceTests.cpp`, `CheckContract` | call it from a test for your source |
| The screens | `MobileAppWidget`, `MobileSearchPageWidget`, `MobilePlacePageWidget`, `MobileRoutePageWidget`, `MobileNavigatePageWidget`, `MobileArrivedPageWidget`, `MobileExplorePageWidget`, `MobileMapWidget`, ... | do not touch |
| Saved places and preferences | `MobileSavedPlaces`, `MobilePreferences` | do not touch: they are the app's own state, not site data |

Put your new class in two new files, `MobileRealSource.h/.cpp`, next to the mock.

## The interface, method by method

The screens call these eight methods. For each: what they do with the answer, and what the real data must give.

| Method | The screens use it for | What to return |
|---|---|---|
| `GetDataNote()` | a small "this is simulated" line on Explore and Profile | leave it empty (the default): real data shows no note |
| `ListSites()` | the cards on the Explore tab | one `FMobileSite` per project (see "Where the real data is") |
| `ListBuildings(SiteId)` | the level picker, the Overview, which building the person is in | the site's buildings, each with its levels (`Level` = floor number, `Label` = "L1", "L2") |
| `ListCategories(SiteId)` | the filter chips | the categories the site's places have |
| `SearchPlaces(SiteId, Query, Filter)` | the "Where to?" list | places matching **every word** of the query and the filter, in building, then level, then list order; an empty query is all of them |
| `FindPlace(PlaceId, Out)` | the place page, Saved tab, route page | `false` for an id it does not know |
| `FindRoute(Request)` | the route page and the walk | an `FMobileRoute`; `bFound = false` with a human `Error` if there is none |
| `GetLevelMap(BuildingId, Level, Out)` | the floor plan | outline, rooms, lifts and stairs of that level; `false` if there is no such level |
| `GetCampusMap(SiteId)` | the Overview | every building's outline, seen from above |

### Rules the screens rely on (the contract test checks all of these)

- **Ids are unique**: site ids, building ids within a site, place ids within a site, level numbers within a building.
- A **place** has a name, a code and a category that is in `ListCategories`; its building and level exist in `ListBuildings`; `FindPlace` finds it again by its id.
- A **search** returns only the site's places; searching a place's own name finds it; a nonsense word returns nothing.
- Every level has a **plan** with an outline of at least 3 points, and **every place of that level is a room on that plan** (`FMobileMapRoom::PlaceId`). A room on the plan may have no place (a corridor, a store room).
- **Polygons are counter-clockwise** and in **the building's own footprint units** (the same numbers as the project file). The campus map is in the site's own units (a building's footprint **plus its `BuildingOffset`**).
- A **route** between two places of a site is found, has steps and at least 2 points, **its last step is `Arrive` and its text contains the destination's name**, and `Minutes == MobileNav::MinutesForMetres(Metres)`.
- A route to an id that does not exist is **not found and says why**.
- `FMobileRouteRequest::FromPlaceId` empty means **"My location"**: the person is in `MyLocationBuildingId` on `MyLocationLevel` (empty / `INDEX_NONE` = the door of the destination's building, level 1).
- `FMobileRoutePreferences` says which ways between levels the person accepts (lift, stairs, escalator); "Use lift only" is the lift alone.

## Where the real data is

You can read the project **without opening any building in the editor**. Do that: opening a building spawns its room actors and only one can be open at a time.

| You need | Take it from |
|---|---|
| The projects (sites) | `ProjectLibrary::Scan(Directory)` (`ProjectLibrary.h`): a list of `FProjectLibraryEntry` with `DisplayName`, `Description`, `BuildingCount`, `FilePath`. Or the one project the generator has loaded |
| Buildings, floors, footprint, offset, scale | `ABuildingGenerator` (`BuildingGenerator.h`): `GetAllBuildings()`, `FindBuildingById()`, `GetBuildingSummary()`, `GetScale()`, `APolyBuilding::GetFootprint()`, `BuildingOffset`, `GetFloorCount()` |
| **Rooms, doors, lifts and stairs of every building, without loading it** | `ABuildingGenerator::GetInteriorDataForBuilding(BuildingId)`: the building's `"interior"` JSON (`floors[]` with `rooms[]` and `navNodes[]`). This is what the project file holds; see `docs/test-data/*.json` for a complete example |
| Room name, type, tags, centre | the same JSON (`name`, `roomType`, `tags`, `footprint`); `RoomTypes.h` lists the 12 types; `FNavigationRoomOption` shows how the editor builds a room's label and centre |
| Doors in the outer wall (main / link) | `ABuildingGenerator::GetStoredEntrances(BuildingId)` (`InteriorEntrance.h`) |
| **A route, also between buildings and through the outside** | `FNavigationRoutePlanner::FindCampusRoute(Generator, Interior, Start, Destination, Result)` (`NavigationRoutePlanner.h`). It does **not** change which building is open. Points carry building, floor, connector and world position |
| Text for a route's steps | `UNavigationPanelWidget::BuildRouteSteps()` (the editor's numbered directions) is a good model for the wording |

The scale: a footprint unit is `GetScale()` world units; metres = world units / 100. A route's length in metres is the polyline's length in footprint units x `GetScale()` / 100.

Where do `Generator` and `Interior` come from on the phone? The player controller already finds them in the level (`ANavPlayerController`, `Generator` and `InteriorGen`). Pass them to your source's constructor.

## What does not exist yet (new work, with a suggestion for how)

| Needed by the screens | Status | Suggestion |
|---|---|---|
| Level label "L1", "L2M" | floors are plain numbers | default to `"L" + number`; a display name in the project file can come later |
| Category (Learning, Facilities, Dining...) | rooms have a **type** (12 fixed values) | a table `Room type -> category`, in one place |
| Place code (`E2-01-101`) | rooms have an id and a name | show the room id as it is |
| **Search across all buildings of a site** | the editor's search covers the open building only | build a small list of places once from `GetInteriorDataForBuilding` for every building, and search that |
| **Turn-by-turn steps** ("Follow the corridor", "Turn left", with metres) | the planner gives points, the editor gives floor changes only | a pure function from the route's points to `FMobileStep` (`Straight`, `TurnLeft`, `TurnRight`, `TakeLift`, `EnterBuilding`, `ExitBuilding`, `Arrive`); easy to unit-test |
| Preferences (lift only, no stairs) | the planner has no such option | a filter on which connectors the planner may use |
| Minutes | - | `MobileNav::MinutesForMetres(metres)` (walking speed 1.2 m/s) |
| "My location" | no positioning | first version: the person picks a building and level on the route page (already built); a position can come later |
| Photo, "Available" | not in the project data | leave empty / `true` |
| Distance label ("2.4 km") | - | leave empty |

## How to do it, in this order

1. **New files** `MobileRealSource.h/.cpp`: `class FMobileRealSource : public IMobileNavSource`. Take `ABuildingGenerator*` and `AInteriorGenerator*` in the constructor.
2. **`ListSites`, `ListBuildings`, `ListCategories`** first. Run the app: Explore shows real sites, the level picker shows real levels.
3. **`SearchPlaces`, `FindPlace`**: the "Where to?" list and the place page work.
4. **`GetLevelMap`, `GetCampusMap`**: the floor plan and Overview show real buildings.
5. **`FindRoute`**: first a route inside one building, then across buildings (`FindCampusRoute`), then the turn-by-turn steps and the preferences.
6. After each step, add to a test file (copy the pattern of `MobileMockContractTest` in `MobileNavSourceTests.cpp`):

   ```cpp
   IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRealContractTest, "InNavi.Mobile.Source.RealContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
   bool FMobileRealContractTest::RunTest(const FString& Parameters)
   {
       FNavigationSearchFixture Fixture;   // see UiTestFixture.h: a headless world with a project loaded
       // load docs/test-data/entrance-test-2-with-doors.json into Fixture.Generator, then:
       FMobileRealSource Source(Fixture.Generator, Fixture.Interior);
       MobileNavSourceTests::CheckContract(*this, Source);
       return true;
   }
   ```

   `CheckContract` lives in `namespace MobileNavSourceTests` inside `MobileNavSourceTests.cpp` and is not visible from other files; move it into a small header (`MobileNavSourceContract.h`) when you need it from a second file, or put your test in the same file.
7. **Swap it in**: in `MobileAppWidget.cpp` replace `Source = MakeShared<FMobileMockSource>();` with your source (keep the mock available, for example behind a launch option `-MobileMock`, so the screens can still be tried without a project).

## Trying it

- **Desktop, phone-sized window** (the quickest): `UnrealEditor.exe InNavi.uproject -game -windowed -ResX=375 -ResY=812 -MobileApp`. Useful extras: `-MobileTab=Directory`, `-MobileState=Route`. All options are in `design/mobile-app-data-interface.md`.
- **Tests**: `UnrealEditor-Cmd.exe InNavi.uproject -ExecCmds="Automation RunTests InNavi.Mobile; Quit" -unattended -nullrhi`. Close the editor first; Unreal cannot build while it is open.
- **Test project with several buildings, doors and generated floors**: `docs/test-data/entrance-test-2-with-doors.json` (buildings A and B 1 m apart with link doors, C far away, a main door each).
- **On a phone**: [`android-install.md`](android-install.md) to install a build, [`android-signing.md`](android-signing.md) to build one.

## Things that will bite

- **Not tried on a phone: how the project file gets there.** `DefaultGame.ini` stages `Data/` as non-UFS files, and the phone build today uses mock data only. Check that your source can open its project on Android before building much on it; if it cannot, the file may have to be packaged differently.
- **Do not store your own state in the source**: saved places and the look (night theme, larger text) are the app's, kept in `Saved/MobileSavedPlaces.json` and `Saved/MobilePreferences.json`.
- **Names in the Mobile files leak between files** (the project builds in "unity" mode). Give anything file-level in a test or a helper a distinctive name (`MobileStyle`'s names especially) or the build breaks in another file.
- **Positions are in footprint units**, not metres and not pixels. The map scales them itself.
- **Keep answers fast.** The screens call `SearchPlaces` as the person types and `GetLevelMap` when the level changes. Build any list once and cache it; do not load interiors on each call.
- **Do not use the editor's UI classes** (`UNavigationPanelWidget` is fine as a model of what the planner returns, but the phone screens must not depend on it).

## Who to ask

- The interface and the screens: Yongqi.
- The route planner, entrances and campus routing: WS.
