# Testing InNavi

InNavi's Unreal automation tests live in [`InNavi/Source/InNavi/*Tests.cpp`](../InNavi/Source/InNavi) and are registered under `InNavi.*`. The name in `IMPLEMENT_SIMPLE_AUTOMATION_TEST` is the name shown in Unreal and the exported report. Many tests use [`UiTestFixture.h`](../InNavi/Source/InNavi/UiTestFixture.h) to open a small [E1, E2 and E6 fixture](test-data/e2-test-fixture.json). Campus performance and persistence tests also use `InNavi/Data/map_extended.db`. The real first-route benchmark uses `InNavi/Data/E2_Filled.json` when available.

## Run on Windows

Use an Unreal Engine 5.8 installation with the `InNaviEditor` target built for **Development Editor | Win64**. Open PowerShell in the repository root. If the editor target is missing or older than the branch's source, close any open InNavi editor session and rebuild it first.

```powershell
$ueRoot = 'D:\EPIC\Epic Games\UE_5.8' # Use your UE 5.8 installation folder.
$project = Join-Path $PWD.Path 'InNavi\InNavi.uproject'
& "$ueRoot\Engine\Build\BatchFiles\Build.bat" InNaviEditor Win64 Development "-Project=$project" -WaitMutex -DisableAdaptiveUnity
if ($LASTEXITCODE -ne 0) { throw "InNaviEditor build failed with exit code $LASTEXITCODE" }
```

Run the full editor suite and check the exported results. Set `$ueRoot` and `$project` as above even when the editor target is already built.

```powershell
$testFilter = 'InNavi'
$minimumTests = 150
$runId = [guid]::NewGuid().ToString('N')
$report = Join-Path $PWD.Path "InNavi\Saved\AutomationReport-$runId"
& "$ueRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project `
    -unattended -nullrhi -nosplash -nosound `
    "-ExecCmds=Automation RunTests $testFilter; Quit" "-ReportExportPath=$report"
$editorExit = $LASTEXITCODE
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-test-report.ps1 -Report $report -MinimumTests $minimumTests
$reportExit = $LASTEXITCODE
Write-Host "Report folder: $report"
if ($editorExit -ne 0 -or $reportExit -ne 0) {
    throw "Automation failed (editor $editorExit, report check $reportExit)"
}
```

To rerun a group, change `$testFilter` to `InNavi.Navigation`, `InNavi.Mobile` or another prefix below, and `$minimumTests` to `1`. Use the full test name for a single case. Each run exports `index.json` to a fresh folder. The [report checker](../scripts/check-test-report.ps1) prints the totals and the failed test names and errors, and fails on a missing report, incomplete tests or too few results. Startup and test messages are in `InNavi/Saved/Logs/InNavi.log`.

For a visual run, open the built project and choose **Tools > Test Automation**. Select the local editor session in Session Frontend, then select `InNavi` or a group in the **Automation** tab and click **Start Tests**. Select a result to show its events and assertion messages. The project's editor suite covers C++ data and UI behavior. Device gestures and rendered performance still need the corresponding [Android test build](android-install.md) or [rendered checks](configuration.md). [Unreal Automation documentation](https://dev.epicgames.com/documentation/unreal-engine/run-automation-tests-in-unreal-engine)

## What each group covers

All prefixes below start with `InNavi.`.

| Prefix | What it checks |
| --- | --- |
| `Project` | Create, close, load and save projects, versioned JSON, database round trips, project library and recovery from invalid files or failed writes. |
| `Building`, `Interior`, `Levels` | Building types and previews, floor creation and generation, room boundaries and doors, connectors, entrances and floor transitions. |
| `Geometry`, `Ground`, `Geo` | Mesh and surface behavior, overlapping surfaces, ground behavior and map coordinate projection. |
| `EditorUX`, `Dock`, `UI`, `UiScale`, `UiSize`, `Display` | Editing tools, actions and feedback, panel docking and persistence, input protection, UI scaling and display fitting. |
| `Desktop`, `DesktopRendering` | Desktop resize behavior, rendering settings and memory-related behavior. The client-only render transitions case needs a rendered game session. |
| `Navigation` | Indoor, outdoor and multi-building routing, route steps, entrance selection, stops and graph caching or invalidation. |
| `Mobile`, `Audio` | Phone screens and navigation, real and mock data, map interaction, search, saved places and audio. The separate `Audio` test covers WAV loading. |
| `LayoutValidation` | Invalid data, geometry, openings, references and room reachability, plus validator plugin and UI integration. |
| `Performance` | Repeatable timings for campus load, routes, phone data and validation, plus memory and frame measurements. See [budgets and measurement procedures](performance.md). |

## Stress test

Run `InNavi.Performance.InteriorRoutes` for the large indoor-routing workload. It generates a 175 × 64 m building with ten floors, opens the interior five times, then tries up to 12 room pairs. The first route builds the graph and the rest exercise reuse. Explain the first-route time, the cached p95 and whether most room pairs produced routes. The test asserts a first route under 45 seconds, a cached p95 under 1.5 seconds and a graph cache hit for each subsequent route. The [performance budgets](performance.md) describe the product targets. Results are saved to `InNavi/Saved/Performance/interior-routes.json`. [Stress-test source](../InNavi/Source/InNavi/PerformanceTests.cpp)

## Ten cases to explain in the live run

| Test | What to show or explain |
| --- | --- |
| `InNavi.Project.Database.SaveKeepsEverything` | Open the campus database, save a JSON project and reopen it. Campus and context building types and roads survive the round trip. [Source](../InNavi/Source/InNavi/DatabaseProjectTests.cpp) |
| `InNavi.Project.Robustness.BadFilesKeepWhatIsOpen` | Feed the loader invalid JSON and database files. The original interior, save path and unsaved floor remain intact. [Source](../InNavi/Source/InNavi/RobustnessTests.cpp) |
| `InNavi.Interior.Floor.Reachable` | Generate seeded layouts in three nonrectangular footprints and check routes from the first room to the other generated rooms. [Source](../InNavi/Source/InNavi/FloorLayoutTests.cpp) |
| `InNavi.LayoutValidation.Reachability` | The validator can reach both rooms via doors and a lift. Sealing one room reports its ID and floor. [Source](../InNavi/Source/InNavi/LayoutValidationTests.cpp) |
| `InNavi.Navigation.Outdoor.MainDoors` | Main doors are selectable outdoor points. Selecting two doors produces an outdoor route. [Source](../InNavi/Source/InNavi/NavigationOutdoorTests.cpp) |
| `InNavi.Navigation.GraphCache.ReusedUntilTheBuildingChanges` | The first route builds the graph, later routes reuse it, and a room deletion forces a rebuild. [Source](../InNavi/Source/InNavi/NavigationGraphCacheTests.cpp) |
| `InNavi.Mobile.MapFlow` | Change floors, use Locate, switch between campus overview and a building, and verify which controls are available. [Source](../InNavi/Source/InNavi/MobileMapTests.cpp) |
| `InNavi.Mobile.RouteFlow` | Start directions from My location. A destination upstairs adds a lift, the same floor removes it, and another building adds exit and entry steps. [Source](../InNavi/Source/InNavi/MobileRoutePageTests.cpp) |
| `InNavi.Desktop.Responsive.FloatingPanels` | A floating panel stays in usable viewport bounds as the window shrinks. The larger window restores its prior placement and size. [Source](../InNavi/Source/InNavi/DesktopResponsiveTests.cpp) |
| `InNavi.Performance.InteriorRoutes` | Compare the first route with cached routes under the ten-floor workload and check the timing and cache assertions above. [Source](../InNavi/Source/InNavi/PerformanceTests.cpp) |

### Team meeting

1. Before the meeting, build the branch and rehearse the command-line run once. Check that the fixture, campus database and exported report are present.
2. During screen sharing, show the branch and run the full `InNavi` suite. While it runs, use the table to explain what each case sets up and what failure it would catch.
3. Show the report-check summary and open the stress-test measurement. For a failed case, show the first assertion error and rerun that exact test name.

## What CI runs

- [Windows CI](../.github/workflows/build-windows.yml) builds `InNaviEditor`, runs MSVC static analysis, runs the full `InNavi` editor suite on `main` and checks for at least 150 tests. Its `test-report-and-performance` artifact contains the automation report and measurements.
- [Pull request lint](../.github/workflows/lint.yml) runs the repository checks and schema tests on a GitHub-hosted runner. [JSON validation](../.github/workflows/validate-json.yml) also runs for relevant data changes. Run the Windows automation suite locally on feature branches before opening a PR.
