# Mobile app - what the screens need from the app's data

The mobile app (portrait only, built first as screens on mock data, wired to the real planner afterwards) follows the design team's
prototype: <https://innavappprototype.onrender.com/>. Every screen talks to **one data interface** (`IMobileNavSource`), never to
`AInteriorGenerator`, `NavigationPanelWidget` or the route planner directly. `FMobileMockSource` answers it with the prototype's fake
data; `FMobileRealSource` (later) answers it from the real code. Swapping one for the other must not touch a screen.

## The prototype's screens

Bottom tabs: Explore, Directory, Saved, Profile. A full-screen map (Overview, 2D | 3D, level picker, locate button) with a draggable
bottom sheet whose content follows one flow:

1. **Explore** - site cards (name, subtitle, distance, levels / buildings).
2. **Where to?** - search box, filters, places grouped by "building . level".
3. **Place** - name, code (E2-01-101), building . level, photo, category, status, Save place, Use as starting point, Get directions.
4. **Your route** - From (My location or a place; building and level), To, minutes, metres, accessibility (Escalator / Lift / Stairs /
   Use lift only), numbered steps ("Follow the corridor", "Turn towards X", "You have arrived").
5. **Navigating** - "Step 2 of 3", metres remaining, next instruction, Exit.
6. **Arrived** - Find another place, Save this place, View destination.

## The interface (what a screen may ask for)

```cpp
// MobileNavSource.h - plain structs, no UObjects
struct FMobileSite     { FString Id, Name, Subtitle; int32 BuildingCount, LevelCount; FString DistanceLabel; };
struct FMobileLevel    { int32 Level; FString Label; };                       // Label "L1", "L2M"
struct FMobileBuilding { FString Id, Name; TArray<FMobileLevel> Levels; };
struct FMobilePlace    { FString Id, Name, Code, SiteId, BuildingId, BuildingName; int32 Level; FString LevelLabel, Category, RoomType;
                         TArray<FString> Tags; FVector2D Position; bool bAvailable; FString PhotoPath, Description; };
struct FMobilePlaceFilter { TArray<FString> Categories; bool bAvailableOnly; FString BuildingId; int32 Level; };
struct FMobileRoutePreferences { bool bAllowLift, bAllowStairs, bAllowEscalator; };   // "use lift only" = LiftOnly()
struct FMobileRouteRequest { FString FromPlaceId; /* empty = my location */ FString ToPlaceId; FMobileRoutePreferences Preferences; };
struct FMobileStep     { EMobileStepKind Kind; FString Text, BuildingId; int32 Level; FString LevelLabel; double Metres; FVector2D Position; };
struct FMobileRoute    { bool bFound; FString Error; double Metres; int32 Minutes; TArray<FMobileStep> Steps; TArray<FMobileRoutePoint> Points; };

class IMobileNavSource
{
    virtual TArray<FMobileSite>     ListSites() = 0;
    virtual TArray<FMobileBuilding> ListBuildings(const FString& SiteId) = 0;
    virtual TArray<FString>         ListCategories(const FString& SiteId) = 0;
    virtual TArray<FMobilePlace>    SearchPlaces(const FString& SiteId, const FString& Query, const FMobilePlaceFilter& Filter) = 0;
    virtual bool                    FindPlace(const FString& PlaceId, FMobilePlace& OutPlace) = 0;
    virtual FMobileRoute            FindRoute(const FMobileRouteRequest& Request) = 0;
};
```

`FMobileMockSource` (`MobileMockSource.h/.cpp`) answers with the prototype's data: the Paya Lebar Mall, the Pathfinder Centre and the SIT Punggol
campus (E2, E6 with its mezzanine L2M, W2), four places on every level, the mall's closed restaurant, and routes made up from them (lift,
escalator or stairs by the person's preferences; out of one building and into another). `MobileNavSourceTests.cpp` has a contract check
(`CheckContract`) that any source must pass - the real one runs the same check.

Saved places are the app's own state (a small file in `Saved/`), not part of the source. The bottom sheet's flow (search -> place ->
route -> navigating -> arrived) is a state machine of its own; the widgets only send events and show the state.

## What the real code can give today

| Needed | Real source | Status |
|---|---|---|
| Site name, description | `ProjectLibrary::Scan` -> `FProjectLibraryEntry` (`DisplayName`, `Description`) | real |
| Building count | buildings in the project file | real |
| Distance label ("2.4 km") | - | mock / drop |
| Building id, name, floor count | `APolyBuilding`, `AInteriorGenerator` floors | real |
| Level label "L1", "L2M" | floors are integers only | **new**: a floor display name in the project file (default "L" + number) |
| Place id, name, tags, type, floor, centre | `AInteriorRoom` / `FNavigationRoomOption` (`RoomId`, `RoomName`, `Tags`, `RoomType`, `Floor`, `Centroid`) | real |
| Place code "E2-01-101" | room ids of the example data look like it; generated rooms are named "Classroom 3-04" | partly: show the id as is, or derive |
| Category (Learning, Facilities) | Room type (12 fixed values) | **new**: a mapping table Room type -> category |
| Search | `NavigationSearch::Matches`, `SearchDestinations` | real, **but only for the one building that is loaded** |
| Places across every building of a campus | the interior of one building is loaded at a time (it spawns the room actors) | **new**: a light index read from the project file without spawning actors |
| Route, distance | `FNavigationRoutePlanner::FindRoute` (points with floor and connector); metres = length x scale | real |
| Minutes | - | **new**: metres / walking speed (about 1.2 m/s) |
| Steps with floor changes | `BuildRouteDetails` ("Take Elevator from Floor 1 to Floor 2") | real, text only |
| Steps "turn left / follow the corridor / arrived" | - | **new**: turn-by-turn from the route polyline (pure function, testable) |
| Accessibility (lift only, stairs only) | the planner does not take a preference | **new**: a connector filter in the planner |
| Route across buildings, to the outside | main / link doors are saved; the planner does not use them (teammate) | later |
| Start = "My location" | - | **mock first**: pick on the map or an entrance; GPS later |
| Photo, "Available" status | - | mock or leave out |
| Saved places, Profile | - | the app's own state / out of scope for now |

## Order of work

1. This interface and the mock source (the prototype's data: E2 / E6 / W2, levels L1 / L2 / L2M / L3, places, a fake route).
2. The phone screen skeleton at 375 x 812: bottom tabs, map area, draggable bottom sheet, the sheet's state machine.
3. The sheet's five contents, one at a time, on mock data.
4. The real source, one answer at a time, starting with search and route in a single building.
5. Android packaging, tried early on a real device once the skeleton exists.

## The phone screen skeleton (step 2)

`UMobileAppWidget` (`MobileAppWidget.h/.cpp`) is the phone screen: a full-screen map area (a placeholder for now), the top bar (site name,
Overview, 2D | 3D), the level and locate buttons, a draggable bottom sheet with one placeholder page for each state of the flow, and the four
tabs (Explore, Directory, Saved, Profile). The flow and the sheet's heights are plain logic in `MobileSheetFlow.h/.cpp` (`FMobileSheetFlow`,
`MobileSheet::`), tested by themselves. Portrait only.

The layout is always 375 design pixels wide: the whole screen is scaled to the window's width, so a bigger or smaller phone looks the same,
only taller or shorter. A window 375 pixels wide shows design pixels one to one.

Trying it on the desktop (phone-sized window):

```
UnrealEditor.exe InNavi.uproject -game -windowed -ResX=375 -ResY=812 -MobileApp
```

Add `-MobileTab=Directory` to open the map tab, and `-MobileState=Place|Route|Navigating|Arrived` to open the sheet at that step. In a running
game the console command `MobileApp` does the same. Tests: `InNavi.Mobile.*` (the flow, the sheet heights, and the screen on a small, an
ordinary, a tall and a scaled-up window, with a status bar and a gesture bar, and dragging the handle).
