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
| A route inside the largest building (p95) | **under 15 s, and shown as progress** | See "Where we are under the target" |
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
| Route inside one building | 10 floors, 225 rooms, 12 routes (9 across floors) | 6.3 s | 6.3 s | < 15 s with progress | met, see below |
| Validating one building | the same building | 6.4 s | 6.4 s | < 30 s with progress | met |

The frame figures, which need a window on a screen, are read from the **Debug** panel (Window > Debug, on both screens), which shows the
average, the 1 % low and the slowest frame over the last ten seconds ("Last 10 s: avg 61.2 | 1% low 48.0 | min 31.0 FPS").

| Frame rate to read off at the demo | Budget | Measured |
|------------------------------------|--------|----------|
| Exterior screen, the whole campus loaded (29 + 407 buildings) | 60 FPS | to be read off the Debug panel on the demo machine |
| Interior editor, 2D plan | 60 FPS | to be read off the Debug panel |
| Interior editor, 3D preview of a ten-floor building | 60 FPS | to be read off the Debug panel |
| Phone app, on a Samsung Galaxy S24+ | 30 FPS | steady 60 FPS with the made-up data (2026-10-23); to be repeated with real project data |

## Where we are under the target

**A route inside the largest building takes about six seconds.** This is the one number that is far from where we want it. Every route
builds the visibility graph of the building from its walls, doors and room corners (all ten floors) before it searches, and the validator,
which builds that graph once for the building, takes the same six seconds, so the graph is where the time goes. (That is read from the two
measurements; it is to be confirmed with a profile before it is changed.) In use it is hidden by spreading the work over frames (the progress
bar and Cancel button of the Navigation and Validation panels), so the editor stays at its frame rate, but the user still waits.

Plan: keep the graph of a building until the building changes, so that only the first route in a building pays for it and the next ones,
and the validator, take a fraction of that. Most buildings are far smaller than this one (the routes between three buildings take 41 ms),
so this is the worst case of the editor, not the usual one.

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
