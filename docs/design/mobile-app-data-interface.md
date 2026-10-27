# Mobile app - what the screens need from the app's data

The mobile app (portrait only, built first as screens on mock data, wired to the real planner afterwards) follows the design team's
prototype: <https://innavappprototype.onrender.com/>. Every screen talks to **one data interface** (`IMobileNavSource`), never to
`AInteriorGenerator`, `NavigationPanelWidget` or the route planner directly. `FMobileMockSource` answers it with the prototype's fake
data when `-MobileMock` is specified. `FMobileRealSource` is the default and reads cached project snapshots and the campus planner. Pages use the same interface for either source. See [the real-data guide](../mobile-real-data-guide.md) for data selection, packaging and verification.

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
| Level label "L1", "L2M" | real floors use `L<number>` | custom floor labels remain future schema work |
| Place id, name, tags, type, floor, centre | `AInteriorRoom` / `FNavigationRoomOption` (`RoomId`, `RoomName`, `Tags`, `RoomType`, `Floor`, `Centroid`) | real |
| Place code "E2-01-101" | room ids of the example data look like it; generated rooms are named "Classroom 3-04" | partly: show the id as is, or derive |
| Category (Learning, Facilities) | `FMobileRealSource` maps the 12 room types to categories | built |
| Search | `NavigationSearch::MatchesWords` over the cached mobile place index | built across the project's buildings |
| Places across every building of a campus | `FMobileRealSource` indexes stored room JSON without opening editor interiors | built |
| Route, distance | `FNavigationRoutePlanner::FindCampusRoute`; world-space polyline centimetres / 100 | real |
| Minutes | `MobileNav::MinutesForMetres` at 1.2 m/s | built |
| Steps with floor changes | `BuildRouteDetails` ("Take Elevator from Floor 1 to Floor 2") | real, text only |
| Steps "turn left / follow the corridor / arrived" | actual route polyline converted to `FMobileStep` | built in the real adapter |
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

`UMobileSearchPageWidget` is the "Where to?" page of the sheet. It asks an `IMobileNavSource` (the mock for now) and never anything else: a search box,
a Filters toggle (one chip for each category the site has, Available only, Clear), and the places grouped by "building . level". With nothing typed
and no filter it lists the places of the building and level the person is in (the first building's first level in the mock), with a Browse all
places button; a search or a filter lists every match. Tapping a place hands it to the screen, which moves the sheet on. The back arrow goes to the
Explore tab. A row or a chip is a `UMobileTapTarget`, which carries its own payload (a place id, a filter name) to whoever made it.

The category icons and the search box's magnifier are placeholders (a letter on a soft square); the design team's icons replace them. More launch
options for looking at it: `-MobileQuery=lounge`, `-MobileBrowse`, `-MobileFilters`, `-MobileSnap=Collapsed|Half|Full`.

## The place page (step 3, second page)

`UMobilePlacePageWidget` shows one place: the back arrow, the name with its code, building and level under it, a photo box ("No photo" until a place
has one), the category and an Available / Unavailable pill, the description, Save place and Use as starting point, and Get directions pinned at the
bottom of the sheet whatever its height (the rest scrolls). It keeps no state of its own: `UMobileAppWidget` keeps the saved places and the starting
point (empty = my location) and hands the page what to show, so the buttons read "Saved" and "Starting point" when they are on. The page tells the screen
about Back, Save, Use as starting point and Get directions through callbacks. `-MobileState=Place` shows it on the first place of the site.

## The route page (step 3, third page)

`UMobileRoutePageWidget` is "Your route", as in the prototype: **From** (My location, or any place), **Building** and **Level** (asked for when From is My
location: where the person is), **To**, the minutes and metres, the ways between levels (chips Escalator, Lift, Stairs - on is blue - and Use lift only),
the numbered steps with the building and level of each, and Start navigation pinned at the bottom. With no route it shows the reason in red instead of
the numbers and Start navigation is off.

The drop-downs are `UMobileSelectWidget`, drawn by the screen itself: a field with a small caption and the chosen text, and when it is open the list of
options right under it (in the page, scrolling when long). The engine's own combo box opens a pop-up menu outside the phone screen's scale, so its
text would not match the rest; this one scales with the screen. Only one is open at a time. The arrow is a placeholder letter until the design team's
icon is in.

The page shows the `FMobileRouteRequest` (from, to, where My location is - `MyLocationBuildingId` and `MyLocationLevel` - and the preferences) and the
route it is given, and reports every change as a new request. `UMobileAppWidget` keeps the start (also set from the place page's Use as starting point),
where My location is (the first building's first level to begin with) and the preferences, finds the route again (`RefreshRoute`) whenever the sheet
arrives at the route page or something changes, and a destination chosen on the route page is the place the place page shows too.
`-MobileState=Route` shows the page, `-MobileOpen=From|To|Building|Level` opens a drop-down.

## The walking and arrival pages (step 3, last two pages)

`UMobileNavigatePageWidget` is the sheet while walking, kept low so the map has room: where to ("To Study lounge . E2 . L1"), Exit, the instruction of
the step the person is on, "Step 2 of 3", the metres still to walk (that step's distance and every later one's), the step's building and level, and
Next instruction. On the last step, which is the arrival, the button says "I have arrived". On a real phone the step would follow the person; here
the screen (`UMobileAppWidget::AdvanceNavigation`) moves on when the button is pressed, and pressing it on the arrival opens the arrival page. Exit
goes back to the route; setting off again starts at the first step.

`UMobileArrivedPageWidget` is "You have arrived": the place with its code and level, Find another place (to the search), Save this place (the screen
keeps the saved places, the same ones the place page shows), and View destination (to the place page). No page is a placeholder any more: the sheet
has five real pages, one for each state of the flow. `-MobileState=Navigating|Arrived` shows them.

## The map (step 3, the map area)

`UMobileMapWidget` fills the screen behind the sheet and draws what the source gives it: `GetLevelMap` (one level of a building: its outline, its rooms -
each a place or not - and its lifts, stairs and escalators) or `GetCampusMap` (the Overview: the buildings, and features such as the pond). Rooms are coloured
by what they are (Learning blue, Facilities green, Dining yellow, closed ones darker) and named; an L-shaped room is fine (the polygons are ear-clipped).
It also draws, on the level it shows: the route (a line, a white-ringed blue dot at the start, a red dot at the destination), the chosen place (blue outline), and
the person (a blue dot with a halo). 3D is the same plan tilted (turned a quarter, squashed, standing on a slab). The plan is fitted into the part of the screen
the top bar and the sheet (at its half height, so it does not jump while the sheet is dragged) do not cover. A tap on a room opens its place (while searching or
looking at a place); on the Overview a tap on a building opens it.

What it shows follows the sheet (`UMobileAppWidget::RefreshMap`): the place's level for a place and the arrival, the start of the route for the route, the level
of the step the person is at while walking (so it follows them up a lift and out of a building). The person's own choices last until the sheet moves on:
the **level button** (opens a list of the building's levels, top one first, upward from the button; "Map" on the Overview), **Overview** (toggles the site view),
**Locate** (back to My location's building and level) and **2D | 3D**. The dot is the step's position while walking, otherwise My location (the door of the
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

`UMobileExplorePageWidget` is the first tab: the app's name, "Find your way indoors", and a card for each site from `ListSites()`: a picture (a placeholder drawing of buildings
until there are pictures), the name, what it is, the distance (`DistanceLabel`), and how many buildings (a campus) or levels (one building). When the source's `GetDataNote()` is not
empty (the mock's is "All maps, places and positioning are simulated.") a "Mock locations" pill sits by "Sites" and the note closes the page. A tap on a card calls
`UMobileAppWidget::OpenSite`, which starts everything again for that site: the sheet at Where to?, no chosen place, no start, no route, every way between levels on, My location in
the site's first building on its first level, the search, route choices and map taken from the site, the top bar showing its short name (`FMobileSite::ShortName`, else its name), and
the map tab shown. Opening another site in the middle of a walk does the same. An unknown site is ignored.

## The Saved tab

`UMobileSavedPageWidget` is the Saved tab: the places the person saved, newest first, grouped under the name of their site (sites in the order they first come up), each row with a
placeholder icon, the name, "code . building . level", and a **Remove** button. With nothing saved it says "No saved places yet" and how to save one. The page shows ids it is given and
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

The map has a view, as in the prototype: zoom from **0.7 to 2** (1 = the plan fits the uncovered part of the screen), and a pan. The **More** button above the sheet opens the map's tools:
**Move: Off / On** and **-** / **+** (a step of 0.2 about the middle of the uncovered part; each button turns off at its end of the range). Dragging with one pointer moves the view and two
fingers pinch-zoom (about their middle, which also moves the view) only when **Move** is on, as in the prototype; with it off a drag does nothing and is not a tap. The mouse wheel zooms
about the pointer when Move is on. A tap that does not move still picks a room or a building. The plan cannot be pushed out of sight: 80 pixels of it (or all of it, if smaller) stay in the
uncovered part. The view starts again at the fit for another level or building, the overview, the 2D / 3D switch, **Locate**, and opening a site; a refresh of the same level keeps it. Hit
testing uses the same transform as drawing, so a room taps where it is drawn at any zoom. Only the pointer that pressed on the map is followed: an Android emulator reports a click as a
touch and, with it, the mouse cursor's moves, which are ignored (the same rule as for the sheet's handle). Labels on the map keep their size when zoomed.

## Layout as the prototype (measured at 390 x 844)

The phone screen follows the prototype's layout, in design pixels (375 wide):

- **Header**: 44 tall (plus the status bar) at the top of every tab, white, with a 24 px logo and "InNav" (16 bold). The Explore page no longer has a brand row of its own.
- **Tab bar**: 60 tall (plus the gesture bar), labels 12 bold.
- **Sheet**: rests at **26 / 44 / 88 percent** of the space between the header and the tab bar (`MobileSheet::SnapHeight`); a handle zone 28 tall; a tap on the handle opens it all the way, or from
  all the way back to 44 (`NextSnap`). From 80 percent the plan and the toolbar are hidden (the top row stays). The plan fits between the top row and the toolbar, above the sheet **as it is
  now**, so it grows and shrinks as the sheet is dragged.
- **Top row** (8 below the header, 54 tall): the title as plain text (14 bold; the building and level, "E2 . L1", or the site's name on the overview), **Overview** (44 tall; only for a campus; says
  "Current" while the overview is shown), and the 2D / 3D switch (buttons 44 tall, 12 bold, in a bordered box with 8 px radius).
- **Toolbar** (right 12, 12 above the sheet): one bar, 5 px padding, 4 px gaps, with the level (68 x 44, "L1"), locate (44 x 44, a drawn target) and more (44 x 44, three dots). The levels open
  upward from the level button; the tools (Move: Off, -, +, 44 tall, 8 px gaps) open upward from the bar, right-aligned. Icons are drawn from shapes until the design team's icons are in.

`UMobileAppWidget::ControlsHeight` is the toolbar's 54; the pop-ups are not counted. Test: `InNavi.Mobile.LayoutLikePrototype`.
