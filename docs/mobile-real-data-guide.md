# The phone app's real project data

The Android app and desktop `-MobileApp` preview use **`FMobileRealSource` by default**. Buildings, rooms, maps and routes come from project data through `IMobileNavSource`. The prototype remains available with `-MobileMock`. Existing APK release assets need rebuilding to include this change.

See [the design notes](design/mobile-app-data-interface.md) for the screen contract.

## Data selection

- Normally the source scans `InNavi/Data/`: each JSON project or SQLite map database is an Explore site. JSON projects precede footprint-only databases.
- `Saved/MobileProjects/` supplies local projects. A file with the same name replaces a shipped project, preserving place IDs. Restart after changing files: the source caches a detached snapshot.
- When opened from an editor session with a known project file, the phone view uses that project's current applied data, including unsaved edits. It does not switch editor buildings.
- `-MobileProject="<project.json>"` selects one file explicitly and takes precedence over the editor session.
- Missing data does not select the mock. An empty catalogue displays an explanatory message.

`Data/buildings.json` contains the authored exterior and room data. `Data/map_extended.db` supplies campus footprints through the existing database loader; it has no authored rooms. Empty interiors remain empty. The adapter does not generate rooms, doors or routes to fill gaps in a project.

The shipped room with code `E2-01-02` has no openings. Expect it to be unreachable; this is a finding about the authored data, not a reason to substitute a simulated route.

## Desktop test

Close Unreal before building. From the repository root:

```powershell
$engine = 'C:/Program Files/Epic Games/UE_5.8/Engine'
$project = (Resolve-Path 'InNavi/InNavi.uproject').Path
$data = (Resolve-Path 'docs/test-data/entrance-test-2-with-doors.json').Path
& "$engine/Build/BatchFiles/Build.bat" InNaviEditor Win64 Development "-Project=$project" -WaitMutex
& "$engine/Binaries/Win64/UnrealEditor.exe" $project -game -windowed -ResX=375 -ResY=812 -MobileApp "-MobileProject=$data"
```

This fixture has buildings A, B and C, rooms, connectors and main/link doors. It is a test layout, not a measured campus survey. Without `-MobileProject`, the app reads the shipped library. Add `-MobileMock` for the original prototype.

1. Open a site. Check its buildings, levels, room names/codes and floor plans. The initial level is the first with places, or the first building/level when there are none.
2. Search by name, code, building, type or tag. Every query word must match. Try categories and building/level choices.
3. Select a room, choose **Use as starting point**, then another room and **Get directions**. Test one floor, different floors and different buildings.
4. Disable lifts, stairs or escalators. The planner must use permitted connectors or explain that no route exists. **Use lift only** cannot use stairs.
5. Advance navigation. Outdoor steps switch to the campus overview; entering a building returns to its plan. Check the destination and arrival text.
6. Try a sealed room or missing door: no invented route should appear. Invalid layout data must be corrected in the layout editor.
7. Save a place and restart. Appearance changes should preserve the real source, chosen site and route.

## Implementation

All files are in `InNavi/Source/InNavi/`.

| Concern | File |
| --- | --- |
| Source selection/injection | `MobileAppWidget.cpp`; `SetSource` accepts a provider before the widget is built |
| Catalogue, cached index, maps and route adaptation | `MobileRealSource.h/.cpp` |
| Data interface | `MobileNavSource.h` |
| Pathfinding and connector filtering | `NavigationRoutePlanner.h/.cpp` |
| Shared source contract | `MobileNavSourceContract.h`; used by mock and real tests |
| Real source/integration tests | `MobileRealSourceTests.cpp` |

Site identity uses the project filename. Building IDs include the site; place IDs also include building, floor and raw room ID. This prevents collisions in Saved and routing. The displayed code remains the raw room ID. Content updates and modification times do not change IDs; renaming a project file creates a new site identity.

Closed rooms are drawn; corridors/storage remain geometry without searchable places. Other rooms become places, with blank names falling back to their IDs. Search and map calls use cached values and never open editor interiors or spawn room actors.

Categories: Classroom/Lecture hall/Lab/Study area -> Learning; Office/Meeting room -> Workspaces; Canteen -> Dining; Toilet/Lift lobby/Corridor/Storage -> Facilities; Other -> Other. Floors default to `L<number>`. Photos and distance-from-user labels remain empty; availability defaults to true because there is no live occupancy data.

Level polygons are counter-clockwise in footprint units. Campus polygons and route points add `BuildingOffset`. Room destination candidates are triangulated and checked against actual planner wall clearance, including in concave rooms.

## Routes and location

`FindCampusRoute` supplies paths through authored doors, main/link entrances, outdoor geometry and floor connectors. Preferences remove prohibited connectors before pathfinding. Routes use disposable worlds built from the indexed snapshot: no editor selection, undo state or project file is changed. Data validation rejects invalid layouts before routing actors are created.

Steps describe corridor travel, turns, the actual lift/stairs/escalator, exits/entries and arrival. Metres are the world-space polyline length in centimetres divided by 100, including vertical travel and building offsets. Graph search penalties are not displayed as distance. Step distances sum to the route distance; minutes use `MobileNav::MinutesForMetres` at 1.2 m/s.

**My location is still a manual building/level choice**, not GPS or indoor tracking. Its start is an authored main entrance inset into the building or a mapped navigation node. Without either, the app asks for a starting room and hides the location dot. Walking still advances with **Next instruction**.

## Android packaging and verification

`Config/Android/AndroidGame.ini` stages `Data/` as UFS data inside the APK/pak, replacing Android's loose-file entry. JSON/audio use Unreal file APIs; SQLiteCore also supplies an Unreal VFS. No absolute desktop data path is compiled into the adapter. Local overrides use the writable Saved directory.

Build a fresh APK using [android-signing.md](android-signing.md) and install it using [android-install.md](android-install.md). Older APK assets retain their older code and mock data. On a device, verify the packaged Explore sites, room search and a known connected route while offline. Packaging, launch and pathfinding performance need verification on the rebuilt device APK.

Run automation with the editor closed:

```powershell
& "$engine/Binaries/Win64/UnrealEditor-Cmd.exe" $project -unattended -nullrhi -nosplash -nosound '-ExecCmds=Automation RunTests InNavi.Mobile+InNavi.Navigation+InNavi.LayoutValidation,Automation Quit' "-ReportExportPath=$PWD/InNavi/Saved/MobileRealTests"
```

`InNavi.Mobile.Source.Real*` covers the shared contract, scoped/stable IDs, search/maps, snapshot isolation, preferences, failed routes, campus paths, screen flow, source persistence and Android staging configuration. Prototype screen tests explicitly inject their mock fixtures. Windows CI also runs the mobile suite.

Local verification on 2026-10-27: Win64 Editor and standalone game builds passed; all 137 InNavi automation tests passed (70 without warnings, 67 with warnings, zero failures). A Windows `.pak` probe loaded and routed a two-room project that had no loose JSON file, confirming packaged-file reads. After installing the Android engine components and toolchain, the Android ARM64 Development build, ASTC cook, signed packaging and archive also passed. The APK's version 1.1/code 2, ARM64 libraries, separate signing certificate and embedded project data were verified for `mobile-test-2`. Physical-device verification is still outstanding.
