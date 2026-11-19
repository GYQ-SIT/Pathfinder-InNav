# Mobile app - what the screens need from the app's data

The mobile app (portrait only, built first as screens on mock data, wired to the real planner afterwards) follows the design team's
prototype: <https://innavappprototype.onrender.com/>. Every screen talks to **one data interface** (`IMobileNavSource`), never to
`AInteriorGenerator`, `NavigationPanelWidget` or the route planner directly. `FMobileMockSource` answers it with the prototype's fake
data when `-MobileMock` is specified. `FMobileRealSource` is the default and reads cached project snapshots and the campus planner. Pages use the same interface for either source. See [the real-data guide](../mobile-real-data-guide.md) for data selection, packaging and verification.

## The prototype's screens

Bottom tabs: Explore, Directory, Saved, Profile. A full-screen map (Overview, 2D | 3D, level picker, locate button) with a draggable
bottom sheet whose content follows one flow:

1. **Explore** - site cards (name, subtitle, distance, levels / buildings).
2. **Directory** - the site's (or a building's) places grouped by "building . level", with filters; the search is on the map.
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
struct FMobileRouteRequest { FString FromPlaceId; /* empty = my location */ FString ToPlaceId; FMobileRoutePreferences Preferences;
                            FString MyLocationBuildingId; int32 MyLocationLevel; /* where my location is */ };
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
    virtual bool                    GetLevelMap(const FString& BuildingId, int32 Level, FMobileLevelMap& OutMap) = 0;   // a floor plan
    virtual FMobileCampusMap        GetCampusMap(const FString& SiteId) = 0;                                       // the site from above
};
```

`FMobileMockSource` (`MobileMockSource.h/.cpp`) answers with the prototype's data: the Paya Lebar Mall, the Pathfinder Centre and the SIT Punggol
campus (E2, E6 with its mezzanine L2M, W2), four places on every level, the mall's closed restaurant, and routes made up from them (lift,
escalator or stairs by the person's preferences; out of one building and into another). **The data is a file, not code**:
`InNavi/Data/Prototype/mobile-prototype.json` (format `innavi-mobile-prototype`, version 1). The file says `"synthetic": true` and carries the
note the screens show ("All maps, places and positioning are simulated"); a file that does not say it is made up, or that does not read, is
refused: the source is then empty, with the reason in the log and in `GetLoadError()`, and nothing crashes. To change what the prototype
shows, edit the file; the class only turns it into places, maps and walks. `MobileNavSourceTests.cpp` has a contract check
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
| Level label "L1", "L2M" | real floors use `L<number>` | custom floor labels remain future schema work |
| Place id, name, tags, type, floor, centre | `AInteriorRoom` / `FNavigationRoomOption` (`RoomId`, `RoomName`, `Tags`, `RoomType`, `Floor`, `Centroid`) | real |
| Place code "E2-01-101" | a room's id is an internal one, not a door number | **none**: the code is empty for real data and the screens leave it out (the id is still searched); the mock data keeps the prototype's codes |
| Category (Learning, Facilities) | `FMobileRealSource` maps the 12 room types to categories | built |
| Search | `NavigationSearch::MatchesWords` over the cached mobile place index | built across the project's buildings |
| Places across every building of a campus | `FMobileRealSource` indexes stored room JSON without opening editor interiors | built |
| Route, distance | `FNavigationRoutePlanner::FindCampusRoute`; world-space polyline centimetres / 100 | real |
| Minutes | `MobileNav::MinutesForMetres` at 1.2 m/s | built |
| Walking, turns, doors and floor changes | `NavigationInstructions::Build`, shared with desktop; adjacent straight segments and uninterrupted connector rides are grouped | built in the real adapter |
| Arrival | destination-specific final `FMobileStep` | built in the real adapter |
| Accessibility (lift only, stairs only) | `FNavigationRoutePreferences` filters connectors before pathfinding | built |
| Route across buildings, to the outside | `FindCampusRoute` uses main/link doors and outdoor geometry; overview draws points with building offsets | built |
| Start = "My location" | manual building/level choice, at an authored entrance or node | built; automatic positioning remains future work |
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

## The search page (step 3, first page)

`UMobileSearchPageWidget` is the first page of the sheet, the directory of the site (the prototype's second iteration, "V2 - Map search"; it was "Where to?", with a search box and chips).
It asks an `IMobileNavSource` (the mock for now) and never anything else: the **title** (the name of the building the map shows - `SetContextBuilding`, called whenever the map changes - or of the one the Building filter says; the site's name while the map shows the whole site), a
**filters button** at the right (36 x 36, soft grey with a grey 18 px icon; blue while open; a small blue count of the filters that are on), and the places grouped by "building . level" - those of that building (every place of the site on the overview), with
no Browse all button. The map's building is not a filter: it is not counted, and Clear leaves it. Finding a place by its name is the full-screen search of the map (below), so the page has no search box and no back arrow. The button opens three drop-downs in a
row (`UMobileSelectWidget` in its compact form, `SetCompact`: the caption over a short box, 12 px text, a small arrow, and a black edge while its list is open, as the prototype's browser drop-down has): **Category** (All and the site's categories), **Building** (All buildings and each; a campus only) and **Level** (All levels and each level of the site); only
one is open at a time. Each filter is one choice (`SetCategory`, `SetBuilding`, `SetLevel`, `ClearFilters`; `FMobilePlaceFilter`). A new site starts again with no filter. A row shows a map pin,
the name, "code . category" and, at the right, "building . level"; tapping a place hands it to the screen, which moves the sheet on. A row is a `UMobileTapTarget`, which carries its own
payload (a place id) to whoever made it. More launch options for looking at it: `-MobileFilters`, `-MobileSnap=Collapsed|Half|Full`.

## The place page (step 3, second page)

The place page has a circled cross at the top right (it goes back) instead of a back arrow; the category is plain words beside the Available pill; **Save place** has a heart before it and **Get directions** a pin (`MobileIcons::IconLabelButton`, `CloseButton`). No page of the phone app draws the engine's dark edge shadow when a list is scrolled (`MobileStyle::NoScrollShadow`), as the prototype has none.

`UMobilePlacePageWidget` shows one place: the back arrow, the name with its code, building and level under it, a photo box ("No photo" until a place
has one), the category and an Available / Unavailable pill, the description, Save place and Use as starting point, and Get directions pinned at the
bottom of the sheet whatever its height (the rest scrolls). It keeps no state of its own: `UMobileAppWidget` keeps the saved places and the starting
point (empty = my location) and hands the page what to show, so the buttons read "Saved" and "Starting point" when they are on. The page tells the screen
about Back, Save, Use as starting point and Get directions through callbacks. `-MobileState=Place` shows it on the first place of the site.

## The route page (step 3, third page)

`UMobileRoutePageWidget` is "Your route", as in the prototype's second iteration ("V2 - Map search"): a title at the left and a close button (a circle with a cross) at the right,
and then, in the scrolling part:

- **The stops card** (soft grey, radius 20, rows 56 tall, a thin line joining the badges): the **start** (a blue badge with the locate icon; the drop-down **From** - My location or any place - and,
  for My location, two small white boxes for the **building** and the **level** the person is on), then one row for each **destination**: a badge (a blue number for a stop on the way, a red
  pin for the last), a drop-down with the place's name over its code, building and level, a cross to take it off (when there is more than one) and a **handle** to move it; and **Add stop**
  (a blue plus badge and the words) which turns into a drop-down of the places that are not in the trip yet (and a cross to cancel).
- With several destinations, a line over the numbers: "3 destinations . 9 min . 120 m total".
- **The numbers**: "7 min . 150 m" (the sum of the legs) and a grey line, "via Escalator, Lift, Stairs" ("First destination . via ..." with several, "via Wheelchair accessible" for the lift alone).
  With no route (or while it is being found) the reason is shown instead, in red, and Start navigation is off.
- **The ways between levels**: three pills, Escalator, Lift and Stairs (on = blue edge and soft blue); each is a switch.
- **Route options** and **Route steps** (each a line with its title and a plus or minus, closed at first): Route options holds **Wheelchair accessible** (the lift alone); Route steps the numbered steps
  of the first leg, with the building and level of each.
- **Start navigation**, pinned at the bottom (the navigate icon and the words).

The drop-downs are `UMobileSelectWidget`, drawn by the screen itself (the engine's own combo box opens a pop-up menu outside the phone screen's scale, so its text would not match the rest); the
page uses three of its looks: **Bare** (the name and a small arrow, no box, inside a card), **Chip** (a small white box with the text only: the building and the level) and, in the directory,
**Compact**. A drop-down has a short label for what it shows once chosen (a place's name) and a longer one for its list (the name, the building and the level). Only one is open at a time.

The page shows the `FMobileRouteRequest` (from, to, the stops after it, where My location is - `MyLocationBuildingId` and `MyLocationLevel` - and the preferences) and the route it is given, and reports
every change as a new request; `UMobileAppWidget` keeps the start (also set from the place page's Use as starting point), the stops, where My location is (the first building's first level to begin with)
and the preferences, finds the route again (`RefreshRoute`) whenever the sheet arrives at the route page or something changes, and a destination chosen on the route page is the place the place page
shows too. `-MobileState=Route` shows the page, `-MobileOpen=From|To|Building|Level` opens a drop-down.

### Several destinations

The destinations are the selected place and `StopPlaceIds` after it. The planner (`IMobileNavSource`) still plans one leg from a start to a place, so the screen plans **a leg for each destination**, one after the
other (`PlanLeg`, `FinishLeg`; a source that plans in the background does one leg at a time): the first from the start, each next one from the destination before it. `Legs` holds the routes planned so far and
`CurrentRoute` is the first while the route page shows (and the failing one, with its reason, if a leg has no route: the trip then cannot start). The page's changes are plain edits of the request:
`AddStop`, `RemoveDestination` (never the last), `MoveDestination` and `ChooseDestination`. A handle (`UMobileReorderHandle`, mouse and touch, only the pointer that pressed on it is followed) reports how far
it was dragged in design pixels; a row is `StopRowHeight` (56) tall, so dragging by two rows moves the stop two places, and no further than the list; a drag of less than 4 pixels is a tap.

**Add stop from the place page:** the screen remembers the destinations of the trip the route page last showed (`TripDraft`). The map's search stays on the route page, so a place can be found while a trip is
open: the sheet goes to that place, and its bottom button says **Add stop** (`CanAddStopFromPlace`: a trip is open and the place is not in it) instead of Get directions; pressing it puts the place at the end of the
trip and goes back to the route page. Closing the route page, finding another place after arriving, and opening another site end the trip; Get directions with no trip open starts a new one.

**Walking** goes through the legs: the page says "Destination 1 of 3"; on the last instruction of a leg that is not the last it says "Stop reached: <place>" and the button "Continue to <next place>"; pressing it starts
the next leg at its first instruction; the last instruction of the last leg opens the arrival page, with the last destination. Exit goes back to the route page at the first leg, with the stops. **The map** shows the whole trip
on the route page (every leg's line) with a numbered blue circle where each stop on the way is (`SetStops`; the last destination is the route's red end), and only the leg being walked while walking.

Differences from the prototype: the start row has no handle (the start does not move, only the destinations do); the three ways between levels are text pills (the design team has no icons for escalator and lift yet);
Wheelchair accessible is the lift alone (the planner has no other accessibility setting); there is no "sheltered" option.

## The walking and arrival pages (step 3, last two pages)

`UMobileNavigatePageWidget` is the sheet while walking, kept low so the map has room, as the prototype draws it: with several destinations "Destination 1 of 3" in small grey words at the top;
then where to ("To Study lounge . E2 . L1", 14 bold) with **Exit** at the right (blue words, no box); a **dark card** (the prototype's, in any theme) with the arrow of the step in blue
(straight on for most steps, a turn left or right, a check for the arrival: the design team's Straight, TurnLeft, TurnRight and Success) and the instruction in white; a thin **track** with
what is walked of it in blue (the step the person is on, of all of them); "Step 2 of 3" at the left and the metres still to walk at the right (that step's distance and every later one's);
and **Next instruction** (blue). On the last step, which is the arrival, the button says "I have arrived"; on the last step of a leg that is not the last it says "Continue to <next place>" and the card
"Stop reached: <place>". On a real phone the step would follow the person; here the screen (`UMobileAppWidget::AdvanceNavigation`) moves on when the button is pressed, and pressing it on the arrival
opens the arrival page. Exit goes back to the route; setting off again starts at the first step. The tab bar is not shown while walking (nor on the place and route pages); it is back on the arrival page.

`UMobileArrivedPageWidget` is "You have arrived" (centred, with the place with its code and level under it, and a circled cross at the right that ends the trip like Find another
place): Find another place (to the search), Save this place (a heart and dark words on white; the screen keeps the saved places, the same ones the place page shows), and View destination
(to the place page). No page is a placeholder any more: the sheet
has five real pages, one for each state of the flow. `-MobileState=Navigating|Arrived` shows them.

## The map (step 3, the map area)

`UMobileMapWidget` fills the screen behind the sheet and draws what the source gives it: `GetLevelMap` (one level of a building: its outline, its rooms -
each a place or not - and its lifts, stairs and escalators) or `GetCampusMap` (the Overview: the buildings, and features such as the pond). Rooms are coloured
by what they are (Learning blue, Facilities green, Dining yellow, closed ones darker) and named; an L-shaped room is fine (the polygons are ear-clipped).

**Names on the map are not all drawn** (`MobileMapLabels.h`). The labels are looked at in order of importance - the chosen place, a building the route goes through, then the biggest rooms or buildings - and one that would cover a label already drawn, or a lift's or stair's letter, is left out. A room whose longest side is under 14 design pixels gets no name until the map is zoomed in, and a name off the screen is not drawn. Labels keep their size, so zooming in spreads them apart and more of them appear. Names have a soft outline in the page's colour so they stay readable over a room's colour or edge. This keeps a long, thin building with many rooms, or an overview of 29 buildings, readable.
It also draws, on the level it shows: the route (a line, a white-ringed blue dot at the start, a red dot at the destination), the chosen place (blue outline), and
the person (a blue dot with a halo). 3D is the same plan tilted (turned a quarter, squashed, standing on a slab). The plan is fitted into the part of the screen
the top bar and the sheet (at its half height, so it does not jump while the sheet is dragged) do not cover. A tap on a room opens its place (while searching or
looking at a place); on the Overview a tap on a building opens it.

What it shows follows the sheet (`UMobileAppWidget::RefreshMap`): the place's level for a place and the arrival, the start of the route for the route, the level
of the step the person is at while walking (so it follows them up a lift and out of a building). The person's own choices last until the sheet moves on:
the **level button** ("L1" and a small arrow; opens a list of the building's levels, top one first, upward from the button; "Map" on the Overview), **Zoom out** (to the site view,
a campus only; it says **Back to building** while the site view is shown), **Locate** (back to My location's building and level) and **2D | 3D**. The title of the map (the building and
level, "E2 . L1", or the site's name on the overview) is a white label at the bottom left, level with the toolbar. The dot is the step's position while walking, otherwise My location (the door of the
building the person said they are in; the real source uses an authored entrance or navigation node and hides the dot if none is mapped). This is a manual location choice; there is no automatic indoor positioning.

In the mock, rooms are drawn around the places' positions and the route is a path along the corridors (to the crossing, along to the room's door, in), with
`FMobileStep::Position` the place where the instruction applies and `FMobileRoute::Points` the line to draw. Options for looking at it: `-MobileOverview`, `-Mobile3D`,
`-MobileLevelList`, `-MobileMapLevel=2`.

## Use as starting point

On the place page, **Use as starting point** makes that place the start of the walk (the route's From) and moves the sheet on to **Where to?** to choose the destination. The
search page shows "Starting from <place> . <building> . <level>" under its title with a **Clear** button that goes back to My location, and the map puts the person's dot on
that place. Choosing a place there and pressing Get directions finds the route from the start to it. Pressing the button on a place that is already the start takes the start
back and stays on the place page.

## The Explore tab (site cards)

`UMobileExplorePageWidget` is the first tab: "Explore", a search field (a tap opens the full-screen search of the sites, below), and a card for each site from `ListSites()`: a picture (a placeholder drawing of buildings
until there are pictures), the name, what it is, the distance (`DistanceLabel`), and how many buildings (a campus) or levels (one building). When the source's `GetDataNote()` is not
empty (the mock's is "All maps, places and positioning are simulated.") a "Mock locations" pill sits by "Sites" and the note closes the page. A tap on a card calls
`UMobileAppWidget::OpenSite`, which starts everything again for that site: the sheet at Where to?, no chosen place, no start, no route, every way between levels on, My location in
the site's first building on its first level, the search, route choices and map taken from the site, the top bar showing its short name (`FMobileSite::ShortName`, else its name), and
the map tab shown. Opening another site in the middle of a walk does the same. An unknown site is ignored.

## The full-screen search (the prototype's second iteration)

The design team changed the prototype ("V2 - Map search", next to "V1 - Before playtests" in its header): the search is no longer a box in the sheet but a field on the
map, and a tap on it opens a search over the whole screen. This is how it is built (`UMobileSearchOverlayWidget`); the other changes of V2 (the Directory filters, the route
page, several stops) follow in other steps.

**The two search fields** (`MobileSearchField.h`) are white pills with a magnifier and a hint, 46 tall: on the **map** ("Search buildings or places", 12 below the header, 12 from
each side; the top row with Zoom out and 2D | 3D now comes under it, `TopRowTop` 76) and on the **Explore** tab under "Explore" ("Search sites"). They are buttons, not
text boxes: the typing happens in the overlay. The map's field is on the map tab only, and not while walking. The sheet open to its top now stops under the top row
(`TopRowGap`) and does not cover the field, as in the prototype.

**The overlay** covers the screen, the header and the tabs too, on the page's colour: the field (soft pill, blue edge, a magnifier, the text, a close button) and under it a list
that scrolls. It has two modes:

- **Places** (the map): with nothing typed, **Recent searches** - the buildings and places picked before (newest first, at most 8; ones of another site, or gone, are left
  out), each with a clock, and **Clear history**; with none, "No recent searches yet. Search for a building or place." With words typed, **Search results**: the buildings of
  the site whose name or id has every word, then the places (`SearchPlaces`); "No matching buildings or places." when there is none. A row is a circle with the icon, the name over
  "Building" or "E2-01-101 . E2 . L1", and an arrow. At most 50 rows are made (a campus has thousands of places): past that a line says to type more. A tap on a building shows it
  on the map and the list of its places (the sheet goes back to the search page if it was on a place or a route); a tap on a place opens its page, leaving a route if there was one.
- **Sites** (Explore): with nothing typed, the words typed before (Enter keeps them; at most 8, any case counts once) and **Clear history**; "No recent searches yet." With words
  typed, the sites whose name or subtitle has every word; "No sites found. Try another name." A tap on a site opens it (`OpenSite`); a tap on a recent search types it again.

**What is kept** (`FMobileSearchHistory`, `Saved/MobileSearchHistory.json`, `{ "version": 1, "chosen": ["building:E2", "place:E2-01-101"], "queries": ["punggol"] }`) works like the
saved places: written on every change, a missing or damaged file is empty lists, no file in tests. **Reset saved places & preferences** (Profile) clears it too. Tests:
`InNavi.Mobile.SearchHistory.*` (the lists and the file) and `InNavi.Mobile.SearchOverlay.*` (both modes, and the screen opening it and following what it picked).

## Fonts

`MobileStyle::ApplyFont` sets the typeface to Bold or to Regular, always: the engine's text block is Bold by default, so a text that was only "not bold" used to come out bold, and the pages looked heavier than the prototype (which uses 400 to 600). Regular is what the prototype's 400 and 500 are (the engine's Roboto has no Medium). Use it for any phone text; do not rely on the default.

## Icons

The phone app's icons are the design team's line icons (mostly Lucide, ISC licence; Lift and Escalator are the designers' own drawings): `InNavi/Content/ImageAssets/Mobile/Source/<Name>.svg` are the sources.
The `Mobile_<Name>.png` are the design team's own export, white on transparent, 128 px (their App check list page > Export selected > PNG 128 x 128, Colour "Custom hex" #FFFFFF, Fill and Outline "Default";
tick "Include original SVGs" to get the sources too; the export names a file `<Icon>_128x128_customFFFFFF.png`, rename it `Mobile_<Name>.png`). `scripts/render-mobile-icons.py` is the older way of drawing them from the SVGs
(softer edges); it now serves only the icons that are not in the export, and running it over all of them would overwrite the export. The texture `/Game/ImageAssets/Mobile/Mobile_<Name>` (imported from that PNG, with Compression "Uncompressed (RGBA8)"
and Mip Gen "NoMipmaps") is what the app loads (`MobileIcons.h`) and tints to the colour it needs, so one icon serves the light and the night theme. The folder is always cooked
(`DirectoriesToAlwaysCook` of `/Game/ImageAssets`). While a texture is not imported the icon is drawn from shapes (or a character), so a screen always has something to show and
the tests do not need the textures. So far the search fields, the overlay and its rows (Search, Recent, MoveForward, ClearSearch), the tab bar (NavExplore, NavDirectory, NavSaved, NavProfile), the level button and locate (LevelDropdown,
Recenter), the filters button (Filter) and the pin of a place row (Location) use them; the step page uses Straight, TurnLeft, TurnRight, Success, and Lift and Stairs for the steps that change level (an escalator step uses `Escalator`; until its texture is imported it shows the straight arrow); the other icons are made and wait for the steps that replace the placeholders. `CompassColour` keeps its own colours (it is not tinted, and is exported with the colour left at Default). `Escalator` is the one that is not exported at 128 px: the design team's drawing has fixed-width lines that turn thin when it is drawn small, so it is exported at 1024 x 1024, cropped to its drawing and put on a square 128 px picture with a 4 px margin, like the lift and the stairs.

## The Saved tab

`UMobileSavedPageWidget` is the Saved tab: the places the person saved, newest first, grouped under the name of their site (sites in the order they first come up), each row with a
placeholder icon, the name, "code . building . level", and a **Remove** button. The title is "Saved places" with "Your shortcuts for the next visit." under it. With nothing saved the page is centred: a blue heart, "No saved places yet", "Open a destination and select Save place." and an **Explore places** button that opens the Explore tab. The page shows ids it is given and
asks the source for what they are (`FindPlace`); an id the source no longer knows is left out.

The list itself is the app's own state, not the site's data: `FMobileSavedPlaces` (ids only, newest first, no duplicates). **Save place** on the place page and **Save this place** on the
arrival page both toggle it, so the two pages and the tab always agree. It is kept in `Saved/MobileSavedPlaces.json` (`{ "version": 1, "saved": [ids] }`), written on every change and read when
the app starts; a missing or damaged file is an empty list, and without a file (as in tests) nothing is written. A tap on a row opens that place on the map tab: if it belongs to another site
that site is opened first (`OpenSite`), then the sheet shows the place page with the place on the map. **Remove** takes the place off the list and the place and arrival pages update.
Real data only needs `FindPlace` to answer for every site's places. For looking at the tab without a file: `-MobileTab=Saved -MobileSave=E2-01-101,PLM-01-102`.

## The Profile tab

`UMobileProfilePageWidget` is the last tab, as in the prototype: "Make it yours" and "Preferences stay on this device.", a Visitor card (there is no account), **Appearance** (a pill for
"InNav light" and one for "Night theme", and a **Larger text** box), **About InNav**, a **Reset saved places & preferences** button, and the source's `GetDataNote()` at the end. A tap on
any control says "Appearance updated" (or "Saved places and preferences were reset") under the button, as the prototype's toast does.

The choices are the app's own state, like the saved places: `FMobilePreferences` (theme, larger text), kept in `Saved/MobilePreferences.json`
(`{ "version": 1, "theme": "light" | "night", "largerText": bool }`), written on every change and read when the app starts; a missing or damaged file is the defaults. Reset takes every
saved place off (the Saved tab, place page and arrival page follow) and puts the preferences back to light and normal text.

### The night theme and larger text

Both are applied. `MobileStyle::ApplyAppearance(theme, largerText)` sets the colours the pages are built with (`Blue`, `Ink`, `Grey`, `Line`, `White` for surfaces, `PageBg` behind them,
`OnBlue` for text on a Blue fill, and the map's own `MapColours`) and `TextScale`. The night palette is the prototype's (`#101827` page, `#192437` surface, `#243247` panel, `#edf3fc` text,
`#b2c1d6` muted, `#78adff` primary, `#0f172a` on primary); the map's night colours are ours, as the prototype keeps its map as it is. Larger text is 1.125 times every text size (the
prototype raises its base size from 16 to 18 px).

Pages read the colours when they are built, so a change builds them again: `UMobileAppWidget::RebuildForAppearance()` gives the screen a new widget tree and puts it back as it was (the tab,
the site, the place, the route and the step, the map's level and 2D or 3D, the saved places, the line the Profile tab was showing). The Profile tab asks for it while one of its own buttons
is being tapped, so it runs a moment later from a ticker. The look is read from `Saved/MobilePreferences.json` before the screen is first built, so the app opens in it. The colours are
process-wide variables, so a test that changes them puts them back (`FAppearanceGuard`). Not scaled by larger text: the labels drawn on the map. Not themed yet: the placeholder pictures
(the buildings on the Explore cards).

## Map zoom and pan

The map has a view, as in the prototype: zoom from **0.7 to 2** (1 = the plan fits the uncovered part of the screen), and a pan. In the second iteration of the prototype the map has no tools
(no More button, no Move switch, no zoom buttons): dragging with one pointer moves the view from the start, two fingers pinch-zoom (about their middle, which also moves the view), and the mouse
wheel zooms about the pointer (`UMobileAppWidget::ZoomMap` is a step of 0.2 for the code and the tests). A tap that does not move still picks a room or a building. The plan cannot be pushed out of sight: 80 pixels of it (or all of it, if smaller) stay in the
uncovered part. The view starts again at the fit for another level or building, the overview, the 2D / 3D switch, **Locate**, and opening a site; a refresh of the same level keeps it. Hit
testing uses the same transform as drawing, so a room taps where it is drawn at any zoom. Only the pointer that pressed on the map is followed: an Android emulator reports a click as a
touch and, with it, the mouse cursor's moves, which are ignored (the same rule as for the sheet's handle). Labels on the map keep their size when zoomed (and more of them appear, see above).

## Layout as the prototype (measured at 390 x 844)

The phone screen follows the prototype's layout, in design pixels (375 wide):

- **Header**: 44 tall (plus the status bar) at the top of every tab, white, with a 24 px logo and "InNav" (16 bold). The Explore page no longer has a brand row of its own.
- **Tab bar**: 60 tall (plus the gesture bar), labels 12 bold.
- **Sheet**: rests at **26 / 44 / 88 percent** of the space between the header and the tab bar (`MobileSheet::SnapHeight`); a handle zone 28 tall; a tap on the handle opens it all the way, or from
  all the way back to 44 (`NextSnap`). From 80 percent the plan and the toolbar are hidden (the top row stays). The plan fits between the top row and the toolbar, above the sheet **as it is
  now**, so it grows and shrinks as the sheet is dragged.
- **Search field** (12 below the header, 12 from each side, 46 tall; a pill with a magnifier and "Search buildings or places"; on the map tab, not while walking).
- **Top row** (76 below the header, 42 tall, under the search field, buttons at the right): **Zoom out** (40 tall, 12 bold; only for a campus; says "Back to building" while the overview is shown), and the
  2D / 3D switch (buttons 36 x 34, 12 bold, in a bordered box with 3 px padding and 8 px radius).
- **Map label** (left 12, 12 above the sheet, level with the toolbar): a white pill, radius 16, padding 10 x 8, with the building and level (14 bold).
- **Toolbar** (right 12, 12 above the sheet): one bar, 42 tall, 3 px padding, radius 14, with the level (62 x 36, "L1" 14 and a small arrow) and locate (36 x 36, a 20 px icon), no gap between them. The levels open upward from the level button.
  Both use the design team's icons (LevelDropdown, Recenter); a drawn target replaces locate until its texture is imported.
- **Tab bar** (hidden on the map tab while the sheet is on a place, the route, the walk or the arrival, as in the prototype: the sheet then goes down to the bottom of the screen; `TabsShown`, `TabAreaHeight`): the design team's icons (NavExplore, NavDirectory, NavSaved, NavProfile), 24 px, blue for the open tab and grey for the others; squares until the textures are imported.

`UMobileAppWidget::ControlsHeight` is the toolbar's 42; the pop-ups are not counted. The sheet open to its top stops `TopRowGap` (18) under the top row. Test: `InNavi.Mobile.LayoutLikePrototype`.
