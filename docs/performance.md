# Performance: budgets and measurements

> Team **Pathfinder** · Project **InNavi** · Guidelines A3, A4, H3 (M2 Technical Guidelines)
>
> Measured on 2026-10-28, Unreal Engine 5.8, Windows 11, Intel Core i5-14600KF (14 cores), 32 GB RAM, on the data in this repository.

InNavi is a desktop application and a phone app that read their data in-process; there is **no backend and no network call**, so the
usual "API response time" is the time of the calls the screens make into the data and route code (the phone app's `IMobileNavSource`
and the route planner). Load times are the times those calls take with the real data at its real size.

## Budgets

| What | Budget | Why |
|------|--------|-----|
| Desktop editor and app, frames per second | **60 FPS or more** (average over 10 s), none of it a frozen frame | A view that does not wait for a backend (guideline H3) |
| Phone app, frames per second | **30 FPS or more** | The phone is the slower device; it draws one screen of a map |
| Opening the campus from the database | **under 3 s** | The first thing the user waits for |
| Opening a building's interior | **under 0.5 s** | Done every time a building is switched to |
| A search of the places | **under 100 ms** | It runs on every key press |
| A route between buildings, or on the phone (p95) | **under 1 s** | The user asks and waits |
| A route inside the largest building | the first, which builds the graph of the building: **under 15 s, shown as progress**; the next ones (p95): **under 1 s** | See "Where we are under the target" |
| Validating a building | **under 30 s, shown as progress** | The check is advisory and runs a little each frame |

A route, a search and the validator never block a frame: the route and the validator are spread over frames and show a progress bar and a
Cancel button.

## What was measured

All of these are automation tests (`InNavi.Performance.*`), so the numbers can be produced again by anyone and are checked in CI against
loose guard limits. Every test writes what it measured to `InNavi/Saved/Performance/<name>.json`, with the machine it ran on.

| Measured | Size of the data | p50 | p95 | Budget | Result |
|----------|------------------|-----|-----|--------|--------|
| Campus from the database: 29 campus buildings | `map_extended.db` | 0.49 s | 0.50 s | | |
| ... its 1,824 roads and points of interest | | 0.41 s | 0.42 s | | |
| ... its 407 context buildings | | 0.86 s | 0.87 s | | |
| **Whole campus, loaded** | 29 + 407 buildings, 1,824 roads and POIs | | **1.8 s** | < 3 s | met |
| Opening an interior | 10 floors, 225 rooms (a 175 x 64 m building) | 0.12 s | 0.12 s | < 0.5 s | met |
| Generating the ten floors (once, when authored) | same | 4.2 s | | | |
| Route between buildings (over link doors and the ground) | 3 buildings, 30 routes, all found | 1.6 ms | 1.9 ms | < 1 s | met |
| Phone data: open a project | 3 buildings, 42 places | 1 ms | 1 ms | | |
| Phone data: search places | 42 places | under 1 ms | under 1 ms | < 100 ms | met |
| Phone data: a route | 40 routes, all found | 12 ms | 13 ms | < 1 s | met |
| Route inside one building: the first (it builds the graph of the building) | 10 floors, 225 rooms | 0.9 s | | < 15 s with progress | met, see below |
| Route inside one building: the next 11 (the graph is kept) | the same, 11 routes | 20 ms | 27 ms | < 1 s | met |
| Validating one building: the first run, then again | the same building | 1.0 s, then 0.17 s | | < 30 s with progress | met |

The frame figures, which need a window on a screen, are read from the **Debug** panel (Window > Debug, on both screens), which shows the
average, the 1 % low and the slowest frame over the last ten seconds ("Last 10 s: avg 61.2 | 1% low 48.0 | min 31.0 FPS").

| Frame rate to read off at the demo | Budget | Measured |
|------------------------------------|--------|----------|
| Exterior screen, the whole campus loaded (29 + 407 buildings) | 60 FPS | **average 115.9, 1 % low 86.5, minimum 84.6 FPS**. Met. Read after 5 s, not the full 10 s |
| Interior editor, 2D plan | 60 FPS | **average 126.3, 1 % low 76.1, minimum 37.0 FPS**. Average met. The low minimum is a short hitch while the rooms were generated with the one-click generator (noted by the person who took the reading, not measured separately) |
| Interior editor, 3D preview (a five-floor building) | 60 FPS | **average 126.6, 1 % low 53.1, minimum 17.1 FPS**. Average met. The 1 % low and the minimum (17.1 FPS, 58 ms) come from a short hitch while the rooms were generated with the one-click generator (noted by the person who took the reading, not measured separately) |
| Interior editor, 3D preview of a 15-floor building (floor 10 shown) | 60 FPS | **average 131.2, 1 % low 103.5, minimum 84.1 FPS**. Met, with nothing being generated |
| Phone app, on a Samsung Galaxy S24+, real project data | 30 FPS | **60 FPS (the app's cap) in every scenario, no frame over 33 ms except one**. 2026-10-31, Development build, USB, with the real `buildings.json` and `map_extended.db` (sites of 6 and 29 buildings). Scenarios, each read from the app's own `[MobilePerf]` line (every 2 s, about 120 frames): sites list scrolling, switching the four tabs, the places sheet dragged up and down, the 2D plan and the 3D plan dragged, a route to a room, navigation stepped through, idle. The slowest frames were 20 to 33 ms. The one frame over 33 ms (121 ms) was the first route being worked out right after the tap. Made-up data, 2026-10-23: also steady 60 FPS |

The desktop readings are from the Debug panel on 2026-10-28, on the machine above with an RTX 4060, with the interior open and the mouse
moving. The budget is met on the average in every screen. The dips in the first two interior readings are one-off hitches of the generator; the reading of the 15-floor building, taken without generating anything, shows the steady figure.

## Where we are under the target

**The first route in the largest building takes about one second; the next ones take about 20 ms.** The cause was measured (a timing of
the three steps of a route in that building: 4,391 vertices, joining them in pairs 6.1 s, the search over them 2 to 12 ms), and it was
fixed in three steps. The first is that the part of the graph that does not depend on where the route starts and ends (the vertices of the buildings and which
of them can see each other) is kept, keyed by everything it is made from (geometry, doors, connectors, entrances, the preferences for lifts
and stairs, the scale), and made again whenever any of that changes. A route then only joins its start and its destination to it. The
validator uses the same graph: validating the building again takes 0.17 s instead of 6.2 s. The second (2026-10-31) is that the graph is
the same for every leg of a trip: it is made from the buildings that have doors in their outer wall or an interior, wherever a leg starts and
ends. Before, a leg that ended in another building made a graph of its own, so a trip through three buildings built it twice (about 25 s on
the CI laptop in a project of 3,952 vertices, two builds of 12 to 13 s each); now it is built once, and the same trip takes about 4 s. The
third is that the vertices are joined on several threads, each worked out on its own and put into the graph in the order of the vertices, so
the graph is the same as before (the same vertices and edges in the project that was timed). The first route in the largest building came down
from 6.0 s to 0.9 s and validating it the first time from 6.2 s to 1.0 s (the desktop, 14 cores); joining the 3,991 vertices of a real project
from 5.9 s to 1.2 s on the desktop, and the 3,952 vertices of the same project on the CI laptop from 7.0 s to 2.2 s. The route search writes
what it measured to the log, one `[RouteTiming]` line for each leg (the time it waited, the time it worked, the vertices and edges of the
graph, whether it was found kept or joined now), so a slow route can be read off without a profiler. The tests are `InNavi.Navigation.GraphCache.*`
(the graph is found when nothing changed, made again when a room is removed, shared by the search that works at once and the one the panel runs
a little each frame, and different for other preferences) and `InNavi.Performance.InteriorRoutes`.

What is left is the **first** route in a building, or the first after a change: it still has to build the graph. That is hidden by
spreading the work over frames (the progress bar and Cancel button of the Navigation and Validation panels), so the editor keeps its frame
rate, but the user waits for it once, and a little longer than the work takes, because a slice of the panel's search is only 8 ms of a frame
(on the CI laptop a build of 2.2 s of work took about twice that). Most buildings are far smaller than the largest one (the routes between three
buildings take about 2 ms), so it is the worst case of the editor, not the usual one.

**The joining still grows with the square of the number of vertices**, and the threads only lower its factor: a project with three times the
vertices of the one above would take about nine times as long to join. If it has to be faster than this, the way is to join a vertex only to
those of its own room and the doors of that room, and to keep a graph for every floor, so that a change of one floor builds only that floor.
That changes how the planner makes its graph and every route would have to be checked again, so it is not planned for M2. Running the search
on a worker thread would take away the wait beyond the work, and is left for later.

**Not measured: several users at once.** The desktop application and the phone app are single-user and share no state, so there is no
concurrent load to measure, and no server that could deadlock. The stability that matters here is that the app does not crash on bad
data: the 160+ automation tests include invalid files, empty projects and unreachable places, and they are run in CI (see below).

## Reproduce

With the Unreal editor closed:

```bash
UnrealEditor-Cmd.exe InNavi/InNavi.uproject -ExecCmds="Automation RunTests InNavi.Performance; Quit" -unattended -nullrhi -nosplash -ReportExportPath=Saved/ValidationTests
```

The measurements are in `InNavi/Saved/Performance/*.json`. The tests fail when a loose guard limit is broken (campus load 10 s, interior
5 s, the first route in the largest building 45 s, a route between buildings 5 s, validating a building 45 s and a project 30 s), so a
large slowdown is caught on the pull request that causes it. The limits are not the budgets above: they are set for the slowest machine the
tests run on (see "The machine that runs the CI is slower" below).

## Desktop memory usage

`InNavi.Performance.MemoryUsage` measures process RAM (resident working set) and committed memory, in bytes, at these checkpoints:
after garbage collection, with the real campus loaded, with a generated ten-floor interior, after the first route builds its graph,
after eleven more routes reuse it, and after destroying the workload, clearing the graph cache and collecting garbage again.
Each checkpoint includes its signed change from baseline. The JSON also records machine details, the engine version, whether the
editor and `-nullrhi` were used, and the largest sampled values. Log messages use MiB (1,048,576 bytes).

Run it in a fresh process for a comparable baseline, with the editor closed:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' InNavi/InNavi.uproject '-ExecCmds=Automation RunTests InNavi.Performance.MemoryUsage; Quit' -unattended -nullrhi -nosplash -nosound -ReportExportPath=Saved/MemoryTests
```

The measurement is `InNavi/Saved/Performance/memory-usage.json`; the automation result is also checked in the normal Windows CI run.
There is no assumed RAM budget: by default the test checks that its campus and route workloads succeeded, counters are available, and
the report was saved. Add `-MemoryMaxGrowthMiB=512` to fail if the largest **sampled committed-memory increase over baseline** exceeds
512 MiB. Choose a limit appropriate to the machine and build. Cleanup memory need not return to baseline because Unreal's allocators
and asset caches can keep pages. That difference alone does not establish a leak.

This automation measures a desktop **data workload inside Unreal Editor**, including its test harness. With `-nullrhi` it omits renderer
resources. It is not the packaged app's footprint, and none of these counters measures dedicated GPU memory. Reported OS peaks cover
the process's entire lifetime, including startup and any earlier tests; checkpoint maxima can miss temporary allocations between samples.

For the actual rendered desktop app, open a packaged build and sample its running process:

```powershell
Get-Process InNavi* | Select-Object Id, ProcessName
powershell -ExecutionPolicy Bypass -File scripts/measure-memory.ps1 -ProcessId 1234 -DurationSeconds 60 -Scenario 'Campus idle'
```

Replace `1234` with the running game's PID. If the package exposes both a small bootstrap `InNavi.exe` and an
`InNavi-Win64-Shipping.exe` (or Development) process, select the latter. You can also sample an `UnrealEditor.exe -game` instance;
label that result as a development run. Allow startup to settle, then use separate runs for campus idle, interior editing and route
calculation so the scenario is clear. Interact with the app during sampling as needed.

The sampler writes every sample and min/mean/max values for the working set and private committed bytes to
`InNavi/Saved/Performance/desktop-process-memory.json`. Use `-OutputPath` to retain separate reports,
`-IntervalMilliseconds` to change the default 500 ms interval, and `-MaxPrivateMiB` for an optional absolute private-memory limit.
It fails on premature process exit or an exceeded limit, saves partial measurements if any exist, and leaves the target process running.
Working set includes shared resident pages. OS lifetime peaks can predate the sampling window; sampled maxima can miss brief spikes.

Local verification on 2026-11-11 (Windows, Unreal 5.8 Development; machine details in the automation JSON):

| Run | Resident RAM / working set | Committed / private memory |
|-----|----------------------------|----------------------------|
| Fresh memory automation, baseline after GC, `-nullrhi` | 2,729.38 MiB | 2,541.30 MiB |
| Same automation, largest checkpoint value | 2,765.64 MiB | 2,558.23 MiB (+16.93 MiB over baseline) |
| Desktop Development start screen, rendering enabled, 1280x720, 30 samples over 15 s | sampled maximum 2,100.23 MiB | sampled maximum 5,360.41 MiB |

These are measurements of the development processes on this machine, not packaged Shipping memory requirements. The full 265-test
automation suite and the fresh memory-only run passed. The process sampler was also checked for normal sampling, an exceeded limit,
a missing PID and premature process exit.

### Campus interior previews

Campus X-ray room actors and meshes are created only when the existing camera-distance or per-building transparency rules reveal
them. Choosing a single floor creates only that floor's preview; All floors creates any remaining floors. Opaque or filtered-out
buildings keep their interiors as project data until revealed. Multiple buildings can still show interiors at the same time.

Created previews stay cached for quick floor switching and repeat visits. The existing refresh paths invalidate the cache after
interior editing, discard and project replacement, so the next reveal uses current data. New/Close destroys the previews.
This avoids allocations for unrevealed interiors; it does not impose a memory limit on previously visited previews. First reveal
still pays the mesh-construction cost. The `InNavi.Building.SeeThrough.*` automation tests check allocation on demand, cache reuse,
visibility and the edit/save lifecycle. The earlier desktop tables and Insights capture predate this optimization.

### Preview RAM comparison — 2026-11-12

Compared the original eager preview algorithms with the optimized lazy algorithms on **Windows 11, Ryzen 7 7700, 16 GiB RAM,
RTX 4070**, using **UnrealEditor.exe -game, Development, D3D12 SM6, 1280x720, capped at 30 FPS**. Rendering was enabled.
The capture ran **01:03–01:17 SGT**. This is a controlled rendered-app workload with the native exterior screen, not a packaged
Shipping measurement. Both builds used the same harness and data; the baseline restored the two original preview functions
verbatim from `8e5bb6e` while retaining the common cache-struct fields and the unused optimized helper.

There were **14 fresh app processes and 560 memory samples**: three eager/lazy pairs for the shipped six-building, nine-room
authored project; three pairs for a generated ten-floor, 225-room building; and one pair for the footprint-only campus database
(29 editable buildings, 407 context buildings, 1,359 road actors, no authored preview rooms). Pilot runs were excluded.
The generated project was saved once, then the identical file was loaded in every run; generation and its undo history were
outside the measured process. Execution order was reversed for the second pair of each repeated workload.

For each process and state, take the median of ten samples and subtract that process's empty-app median. The table reports
**eager growth minus lazy growth**, then the median and range across paired runs. **Positive means lower measured growth in
the lazy build; negative means higher growth.** Resident memory is the Windows working set, including shared resident pages.
Private committed memory is a separate counter; neither measures dedicated GPU memory. Values below are **MiB**.

| Workload / state | Cached preview rooms, eager → lazy | Pairs | Resident difference, median [range] | Private committed difference, median |
|------------------|------------------------------------|-------|------------------------------------|--------------------------------------|
| Shipped authored project, opaque | 9 → 0 | 3 | -3.56 [-13.03, +0.80] | -15.22 |
| Shipped authored project, E2 floor 1 revealed | 9 → 3 | 3 | -2.29 [-14.44, +4.62] | -10.73 |
| Generated 225-room building, opaque | 225 → 0 | 3 | +13.13 [+12.05, +16.91] | +25.24 |
| Generated building, floor 1 revealed | 225 → 22 | 3 | +9.36 [+2.74, +11.10] | +17.76 |
| Footprint-only campus, opaque control | 0 → 0 | 1 | +10.11 [single pair] | +3.71 |

**Interpretation:** the large generated case showed modest lower memory growth, but these results do **not establish a general
or precisely attributable RAM saving**. The no-room control differed by 10.11 MiB while opaque and up to 22.27 MiB in later
states despite having no preview room actors in either build. The small authored project did not show a consistent resident-RAM
reduction, and its private committed growth was higher in the lazy build. After revealing all floors of the generated building,
both builds retained 225 previews, yet a 9.34 MiB median resident difference remained. Allocation history, renderer/allocator
caches and OS/run variation contribute to the observed differences; the control is a different workload with only one pair,
so its difference cannot simply be subtracted as a noise correction.

The confirmed improvement is avoiding unused preview actors and meshes until reveal. Previously visited previews remain cached.
These short snapshots can miss transient peaks and do not measure a leak or establish a RAM budget. A packaged build with longer
steady-state windows and more control repetitions would be needed before claiming a reliable end-user RAM reduction.
The [measurement record](measurements/desktop-campus-preview-ram-2026-11-12.json) includes all 560 samples, per-run summaries,
paired differences, source/input/DLL hashes and limitations. Raw reports, comparison sources, DLLs, runner and logs are retained
locally in `InNavi/Saved/CampusPreviewMemory-20261112/` (git-ignored). The optimized source and DLL were restored after comparison.

### Reproduce the preview comparison

`InNavi.Performance.CampusPreviewMemory` is an opt-in capture of the rendered desktop app, not the headless data fixture.
It opens the native exterior screen in the actual game world, clears the project, and samples an empty-app baseline. It then loads
the chosen project with camera-distance see-through disabled, reveals floor 1 of one building, and reveals all floors of that
building. Each phase settles for five seconds, then takes ten process-memory samples with a requested 500 ms interval while
rendering continues. There is one explicit GC before the empty baseline; normal engine GC continues afterward.

Run it alone in a fresh `UnrealEditor.exe -game` Development process, with rendering enabled:

```powershell
$engine = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64'
$project = Join-Path $PWD 'InNavi/InNavi.uproject'
$data = Join-Path $PWD 'InNavi/Data/buildings.json'
$output = Join-Path $PWD 'InNavi/Saved/Performance/preview-memory-lazy.json'
& "$engine\UnrealEditor.exe" $project -game -d3d12 -windowed -ResX=1280 -ResY=720 -ForceRes -unattended -nosplash -nosound -NoVSync `
  "-PreviewMemoryProject=$data" -PreviewMemoryBuilding=E2 -PreviewMemoryVariant=lazy "-PreviewMemoryOutput=$output" `
  '-ExecCmds=t.MaxFPS 30,Automation RunTests InNavi.Performance.CampusPreviewMemory; Quit'
```

`PreviewMemoryVariant` labels and checks the compiled implementation; it does **not** switch implementations. For the baseline
comparison, compile the original `BuildCampusInteriorPreview` and `UpdateBuildingTransparency` functions from `8e5bb6e`, keeping
the same measurement harness and room-count helper, and use `-PreviewMemoryVariant=eager`. Restore the optimized functions for
the lazy capture. Keep all other build/runtime settings and project data identical, and alternate execution order across repeats.
The baseline retains the same cache-struct fields and the unused optimized helper, isolating the two original preview algorithms.

For a generated large interior, first create one input file and reuse it for both variants:

```powershell
$generated = Join-Path $PWD 'InNavi/Saved/Performance/preview-memory-ten-floor.json'
& "$engine\UnrealEditor-Cmd.exe" $project -unattended -nullrhi -nosplash -nosound "-PreviewMemoryPrepare=$generated" `
  '-ExecCmds=Automation RunTests InNavi.Performance.CampusPreviewMemory; Quit'
```

Then use that file as `PreviewMemoryProject` and `-PreviewMemoryBuilding=MEMORY` in the rendered capture command.
Without these opt-in parameters, the test reports instructions and does not run the capture during the normal suite.

### Unreal Insights desktop capture — 2026-11-11

Captured the rendered desktop application at approximately **23:02–23:04 SGT**, using **Unreal Insights 5.8.2 Memory Insights**.
The application was `UnrealEditor.exe -game`, **Development**, at **1280x720**, with D3D12 SM6 rendering enabled (no `-nullrhi`).
The machine was Windows with an **AMD Ryzen 7 7700**, **16 GiB installed RAM** (15.15 GiB visible to Windows).
This run includes the editor executable's runtime and development facilities; it is not a packaged Shipping measurement.

Tracing was enabled from process startup with `-trace=default,memory -tracefile="<capture-directory>/desktop.utrace"`.
After the start screen settled, the in-game console command `Trace.Bookmark StartScreenIdle` marked the first checkpoint.
The project library then opened `map_extended.db`; after its **29 editable buildings, 407 context buildings and 1,359 road actors**
loaded, `Trace.Bookmark CampusIdle` marked the second checkpoint. The application was closed normally to finish the trace.

Unreal Insights analyzed the completed trace and exported both checkpoints using its **live allocations** query (`aAf`):

```text
MemoryInsights.ExportAllocs -Rule=aAf -BookmarkA=StartScreenIdle -Output="<capture-directory>/start-screen-live.csv" -Columns=Size,Tag
MemoryInsights.ExportAllocs -Rule=aAf -BookmarkA=CampusIdle -Output="<capture-directory>/campus-live.csv" -Columns=Size,Tag
```

These commands were supplied in a response file through `-ExecOnAnalysisCompleteCmd="@=<response-file>"`, with
`-OpenTraceFile="<capture-directory>/desktop.utrace" -AutoQuit`. Neither export had a result-count limit. The CSV row counts
were checked against the successful export counts in the Insights analysis log, and each CSV's integer `Size` column was summed.

| Desktop checkpoint | Time in trace | Exported live allocations | Sum of exported allocation sizes |
|--------------------|---------------|---------------------------|----------------------------------|
| Start screen idle | 75.27 s | 4,239,453 | 18,900,054,698 bytes / **18,024.50 MiB** |
| Full campus loaded, exterior editor idle | 141.43 s | 4,386,518 | 18,976,041,854 bytes / **18,096.96 MiB** |

**Metric and quality limits:** these are **provisional, unfiltered traced-allocation totals**, across both System memory and Video
memory roots. They include virtual/reserved allocations and heap bookkeeping. They are **not resident RAM, private committed memory,
or a physical GPU-memory reading**; a sum exceeding the machine's installed RAM does not mean that much physical RAM was in use.
For example, the start-screen export contains 4,113,093,848 bytes tagged `LiveCoding` and 3,271,925,760 bytes tagged `ProgramSize`.
The analysis reported **49 heap-event errors and 9 free-event errors**, with recovery of missing events; each checkpoint export
contains **35 zero-size rows**. Both exports completed successfully, but these totals should not be treated as a validated memory
requirement or a leak verdict. The earlier process-counter table measures a different metric and remains separate.

The [measurement record](measurements/desktop-memory-insights-2026-11-11.json) contains exact totals, counts, conditions, trace/CSV
SHA-256 hashes and quality notes. The raw 3,481,748,451-byte trace, CSV exports, response file and analysis log are retained locally
under `InNavi/Saved/Performance/MemoryInsights-20261111/` (git-ignored). This capture covers the start screen and exterior campus;
interior editing and routing were not included in this Insights run.

## Phone memory usage — 2026-11-11

Measured the installed **mobile-test-3 release, version 1.4 (version code 5)** on a **Google Pixel 9 Pro XL**,
using `adb shell dumpsys meminfo -d <pid>` over USB. The installed APK's SHA-256 matched the published release:
`6c101c94eee5bb13cb355acf8436b62ac1eb2abf884dac0b718dfe65873e4b54`.
This is the Android ARM64 **Development**, ASTC, distribution-signed build from commit
`52eb8f69b273f7ba8b87fa57673226be7eac3862`, rather than a Shipping build.

The phone reported **Android 17 / API 37**, Tensor G4, a **1344x2992** physical screen at density **480**, and
**15,957,128 KiB** of OS-visible RAM (15.22 GiB). It was connected by USB and charging.
The capture ran from **23:48:54 to 23:56:54 SGT** on 2026-11-11.

There was one force-stop and fresh launch before the first Explore reading, followed by the states below in order, without
restarting the app, clearing its data or forcing garbage collection. Each state has **five foreground samples**, with a requested
five-second pause between samples; ADB commands add time, so each window took approximately 23–24 seconds.
The main activity process (PID 22406) and a detached, same-name, same-UID companion (PID 22446) stayed alive throughout.
Both were captured sequentially for each sample, giving **35 samples and 70 process reports**. The companion's role was not
independently identified.

All table values are **MiB** (Android's reported kB divided by 1024). **Combined PSS** sums `TOTAL PSS` for both processes.
[Android's dumpsys documentation](https://developer.android.com/tools/dumpsys) defines PSS as accounting for each process's share
of shared pages. RSS counts shared pages in full, so only the **main process's RSS** is reported here; it is not added to PSS.
Graphics rows and swap PSS are also not added to `TOTAL PSS`.

| Foreground state, in capture order | Samples | Combined PSS median | Combined PSS maximum | Main RSS median |
|------------------------------------|---------|---------------------|----------------------|-----------------|
| Explore after fresh launch | 5 | 1,192.53 | 1,192.58 | 1,332.59 |
| Authored project, Block E2 L1, 2D directory | 5 | 1,191.85 | 1,192.95 | 1,332.46 |
| Same floor, 3D directory | 5 | 1,161.02 | 1,161.30 | 1,301.49 |
| Route result, My location to E2-01-01 | 5 | 1,001.31 | 1,035.34 | 1,141.76 |
| Active guidance, step 1 of 3 on the same 16 m route | 5 | 1,002.65 | 1,002.70 | 1,143.10 |
| Campus overview, 29 building footprints | 5 | 1,166.30 | 1,166.52 | 1,306.75 |
| Explore after using maps and navigation | 5 | 1,164.64 | 1,164.69 | 1,305.07 |

The **largest sampled combined PSS was 1,192.95 MiB (about 1.17 GiB)**. The largest sampled main-process RSS was
**1,333.56 MiB**, a separate metric. Combined swap PSS medians were approximately **0.06 MiB**.
These are snapshots from one phone and one sequential session, not a continuous startup/route-calculation peak or an endurance
test for leaks. The route was a short same-floor route; cross-floor and campus routes were not sampled. The initial 2D-directory
window overlapped read-only installed-APK hash verification, which may affect file caches. Shared-page accounting, graphics,
caches and OS activity can also change the readings, so differences between states do not establish their individual memory costs.

To repeat the capture, identify the current activity process and any same-package companion before sampling; PIDs change each run:

```text
adb shell am force-stop com.teampathfinder.innavi
adb shell am start -W -n com.teampathfinder.innavi/com.epicgames.unreal.GameActivity
adb shell ps -A -o UID,PID,PPID,NAME
adb shell dumpsys meminfo -d <main-pid>
adb shell dumpsys meminfo -d <companion-pid>
```

Keep the app in the foreground, open each state above and let it settle, then capture both processes five times with a five-second
pause between pairs of reports. The [measurement record](measurements/mobile-memory-dumpsys-2026-11-11.json) contains all
per-sample counters, summaries, build/device details and raw-report SHA-256 hashes. Raw reports, the capture script and screenshots
are retained locally in `InNavi/Saved/MobileMemory-20261111/` (git-ignored). These PSS readings measure a different quantity from
the desktop Insights allocation totals above and should not be compared directly.

## The machine that runs the CI is slower

The same tests on the laptop that runs the CI (the build workflow, 2026-10-30) gave: the whole campus loaded 3.8 s (p95), generating the ten
floors 9.9 s, opening the interior 0.21 s, **the first route in the largest building 21.8 s** (the next ones 37 ms, p95 55 ms), validating
the largest building 22.2 s the first time and 0.45 s after. Everything is two to three and a half times slower than on the desktop of the
table above, and the part that builds the graph of a building the most. The first route is therefore **above the 15 s budget on that
machine**: it is shown as progress and spread over frames (see "Where we are under the target"), so the editor stays responsive, but the
user waits about twenty seconds once for the largest building there. The campus load, opening an interior and the next routes meet their budgets
on both machines (the frame rates were only read on the desktop). The laptop figures of this section are from before the vertices were joined
on several threads (2026-10-31, see above); the graph of a real project there went from 7.0 s to 2.2 s, and the CI will write the new figures
of the largest building on its next run.
