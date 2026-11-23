# The phone app's real project data

The Android app and desktop `-MobileApp` preview use **`FMobileRealSource` by default**. Buildings, rooms, maps and routes come from project data through `IMobileNavSource`. The prototype remains available with `-MobileMock`. Existing APK release assets need rebuilding to include this change.

See [the design notes](design/mobile-app-data-interface.md) for the screen contract.

## Data selection

- Normally the source scans `InNavi/Data/`: each JSON project or SQLite map database is an Explore site. JSON projects precede footprint-only databases.
- The Explore card of a site shows the project's cover picture when `InNavi/Data/Covers/<file name>.png` exists (chosen in the editor's project library, see [interior-editor-guide.md](design/interior-editor-guide.md)); the middle of the picture is shown in the card's proportions, with rounded corners. A project without a cover keeps the placeholder drawing. `Data` is packaged into the APK whole, so the covers go with it.
- `Saved/MobileProjects/` supplies local projects. A file with the same name replaces a shipped project, preserving place IDs. Restart after changing files: the source caches a detached snapshot.
- When opened from an editor session with a known project file, the phone view uses that project's current applied data, including unsaved edits. It does not switch editor buildings.
- `-MobileProject="<project.json>"` selects one file explicitly and takes precedence over the editor session.
- Missing data does not select the mock. An empty catalogue displays an explanatory message.
- Authored floors above a legacy shell count remain visible and routable. When rooms share a mobile place ID, the validator flags the conflict and the app uses distinct floor-and-room IDs. A saved place that can no longer be resolved stays visible as unavailable and can be removed individually.

`Data/E2_Filled.json` contains the authored exterior and room data (the campus with the real ten-floor E2/3). `Data/map_extended.db` supplies campus footprints through the existing database loader; it has no authored rooms. Empty interiors remain empty. The adapter does not generate rooms, doors or routes to fill gaps in a project.

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

1. Open a site. Check its buildings, levels, room names (the room id is not shown; it can still be searched) and floor plans. The initial level is the first with places, or the first building/level when there are none.
2. Search by name, room id, building, type or tag. Every query word must match. Try categories and building/level choices.
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

## Phone GPS

On Android, InNavi requests foreground location permission and reads a fix about once a second while a georeferenced campus map is open. The phone map projects each fix with `GeoProjection` using the same geographic origin as the campus database. The blue dot updates on the campus overview, and Locate centers that view on the latest fix. On the plan of a building the dot is drawn too when the fix is inside the building or within 8 m of its wall (indoor fixes are rarely better): a pale halo around a smaller dot, and Locate then stays on that plan and centers on the dot instead of leaving for the campus. GPS does not know the floor, so the dot is drawn on whichever level is shown. `InNavi.Mobile.GpsIndoor` covers it. The geographic origin comes from the campus database, or from the `geoOrigin` field of a project file saved from it (see `design/project-file-format.md`); a project without one shows no GPS dot. If there is no fix or the project has no geographic origin, Locate uses the existing manual location. The location source can be replaced with fake coordinates in the editor. Run `InNavi.Mobile.Gps` for the phone-free test.

**My location without a GPS.** Without a GPS fix (no permission, no signal, a computer) My location is a guess: the door of the building the person said they are in (the first one), drawn as a dot there and used as the start of a route. With a fix the guess is not drawn: the GPS dot is where the person is, and when they are not at the campus there is no dot on the map. The same goes for a route's start: with a fix, "My location" is the building the device is in (`GetGpsBuildingId`: inside it or within 8 m of its wall); when the device is in none, the From drop-down says "Choose a starting point", the page says "You are not at the campus. Choose where you start." and no route is planned from My location until a place is chosen (`InNavi.Mobile.GpsRouteStart`).

**The GPS dot is a layer of its own.** As in a map app, the position marker is not part of the map's picture: `UMobileGpsMarkerWidget` is a widget above the map (same size, touches go through it) that draws the dot, its halo on the plan of a building and the cone, from where the map says (`UMobileMapWidget::GetGpsMarkerView`, which works out the place on the screen from the map's own view, turned or tilted). The map draws the plan, the route and My location, and nothing of the GPS.

**The direction the device faces.** The GPS dot has a cone that points the way the phone faces, from the phone's compass (its rotation vector sensor, read by `InNavi_UPL.xml` and `FAndroidMobileLocationSource::ReadHeading`, smoothed, magnetic north): lying flat it is where the top edge points, held up it is where the back faces. The map of a real project has north at the top of the screen (the plans have north on X and east on Y, as the editor draws them with X up; the source says so with `IsPlanNorthOnX`; the prototype's plans, drawn in screen units, are shown as they are), so a heading of 0 points up and 90 points right. A twist of two fingers still turns the map, and the compass button turns it back to north.

**Locate follows the heading.** Locate centres the map on the GPS dot and starts following the direction the device faces, as a map app does while a person walks: the map turns so that the way the person faces is up, the dot stays in the middle and its cone points up. A drag of the map, a twist or a move of two fingers ends it, and so does the compass button (which turns the map back to north); Locate starts it again. It is not used in the tilted 3D view, and a device without a compass keeps the map as it is. `InNavi.Mobile.GpsIndoor` covers it. A device without the sensor shows no cone.

**Testing the GPS away from the campus.** In a build that is not Shipping, `-GpsFake=<latitude>,<longitude>` on the command line replaces the device's GPS with that position (`InNavi.Mobile.GpsFakeParse`). On Android, put it in `UECommandLine.txt` and push it before starting the app, for example `InNavi -GpsFake=1.4141,103.9125` (inside building E2/3 of `Data/SIT.json`; add `-GpsFakeHeading=90` for a fixed direction, without it the phone's own compass is used) in `/sdcard/Android/data/com.teampathfinder.innavi/files/UnrealGame/InNavi/UECommandLine.txt`; delete the file afterwards to get the real GPS back. Open the SIT site and the dot is drawn at that position.

**Building for Android.** UE 5.8.2's own `LocationServicesAndroidImpl` plugin does not compile: its Java code calls `activityContext.runOnUiThread(...)`, but `activityContext` is a `ContextWrapper` in this engine version. `InNavi/Plugins/LocationServicesAndroidImpl` is a copy of the plugin with that one call changed to `runOnUiThread(...)`; the project's copy is used instead of the engine's. Remove the copy when a later engine version fixes the plugin.
