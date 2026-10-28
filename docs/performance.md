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
| Route between buildings (over link doors and the ground) | 3 buildings, 30 routes, all found | 41 ms | 42 ms | < 1 s | met |
| Phone data: open a project | 3 buildings, 42 places | 1 ms | 1 ms | | |
| Phone data: search places | 42 places | under 1 ms | under 1 ms | < 100 ms | met |
| Phone data: a route | 40 routes, all found | 52 ms | 53 ms | < 1 s | met |
| Route inside one building: the first (it builds the graph of the building) | 10 floors, 225 rooms | 6.0 s | | < 15 s with progress | met, see below |
| Route inside one building: the next 11 (the graph is kept) | the same, 11 routes | 20 ms | 27 ms | < 1 s | met |
| Validating one building: the first run, then again | the same building | 6.2 s, then 0.16 s | | < 30 s with progress | met |

The frame figures, which need a window on a screen, are read from the **Debug** panel (Window > Debug, on both screens), which shows the
average, the 1 % low and the slowest frame over the last ten seconds ("Last 10 s: avg 61.2 | 1% low 48.0 | min 31.0 FPS").

| Frame rate to read off at the demo | Budget | Measured |
|------------------------------------|--------|----------|
| Exterior screen, the whole campus loaded (29 + 407 buildings) | 60 FPS | to be read off the Debug panel on the demo machine |
| Interior editor, 2D plan | 60 FPS | to be read off the Debug panel |
| Interior editor, 3D preview of a ten-floor building | 60 FPS | to be read off the Debug panel |
| Phone app, on a Samsung Galaxy S24+ | 30 FPS | steady 60 FPS with the made-up data (2026-10-23); to be repeated with real project data |

## Where we are under the target

**The first route in the largest building takes about six seconds; the next ones take about 20 ms.** The cause was measured (a timing of
the three steps of a route in that building: 4,391 vertices, joining them in pairs 6.1 s, the search over them 2 to 12 ms), and it was
fixed where it could be: the part of the graph that does not depend on where the route starts and ends (the vertices of the buildings and which
of them can see each other) is kept, keyed by everything it is made from (geometry, doors, connectors, entrances, the preferences for lifts
and stairs, the scale), and made again whenever any of that changes. A route then only joins its start and its destination to it. The
validator uses the same graph: validating the building again takes 0.16 s instead of 6.2 s. The tests are `InNavi.Navigation.GraphCache.*`
(the graph is found when nothing changed, made again when a room is removed, shared by the search that works at once and the one the panel runs
a little each frame, and different for other preferences) and `InNavi.Performance.InteriorRoutes`.

What is left is the **first** route in a building, or the first after a change: it still has to build the graph, about six seconds in the
largest building the editor makes. That is hidden by spreading the work over frames (the progress bar and Cancel button of the Navigation and
Validation panels), so the editor keeps its frame rate, but the user waits for it once. Most buildings are far smaller than this one (the
routes between three buildings take 41 ms), so it is the worst case of the editor, not the usual one. If it has to be faster, the way is to
join the vertices faster (the visibility tests between a room's corners), which is a change of the planner itself and is not planned for M2.

**Not measured: several users at once.** The desktop application and the phone app are single-user and share no state, so there is no
concurrent load to measure, and no server that could deadlock. The stability that matters here is that the app does not crash on bad
data: the 160+ automation tests include invalid files, empty projects and unreachable places, and they are run in CI (see below).

## Reproduce

With the Unreal editor closed:

```bash
UnrealEditor-Cmd.exe InNavi/InNavi.uproject -ExecCmds="Automation RunTests InNavi.Performance; Quit" -unattended -nullrhi -nosplash -ReportExportPath=Saved/ValidationTests
```

The measurements are in `InNavi/Saved/Performance/*.json`. The tests fail when a loose guard limit is broken (campus load 10 s, interior
5 s, a route in the largest building 15 s, a route between buildings 5 s, validation 30 s), so a large slowdown is caught on the pull
request that causes it.
