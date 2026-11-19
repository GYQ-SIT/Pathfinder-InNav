# T11 — Layout validator in a fresh UE project

**Verified:** 19 November 2026 · Windows, UE 5.8.2 · plugin source **1.1.1**, commit `2cd6236`.

**Verdict: the reusable JSON/geometry validator works in a fresh project without SQL or InNavi's data files. The complete InNavi validation panel and route checks do not come with it.** No plugin source changes were needed.

## What was tried

Created a new minimal C++ project, `T11ValidatorProbe`, outside the repository and OneDrive. Copied only the plugin's source/descriptor and package metadata into `Plugins/InNaviLayoutValidator`; enabled it, disabled default engine plugins and explicitly disabled `SQLiteCore`/`SQLiteSupport`. No InNavi actors, game module, assets or `Data/` folder were copied. A small editor commandlet called `ValidateFile` and `ExportReport`.

Both **Win64 Development Editor and Game targets compiled and linked**. The Game target was not cooked, packaged or launched.

| Input / check | Observed result |
|---|---|
| Independent layout with new building/room IDs | Exit **0**, no findings |
| Same layout with a door offset beyond its wall | Exit **1**, `OPENING_BOUNDS` |
| Unrelated JSON object | Exit **1**, `DATA_TYPE` |
| Copy of our `Data/buildings.json` | Exit **0**, three `INTERIOR_UNAUTHORED` information findings |
| Direct copy of `map_extended.db` | Exit **1**, `FILE_READ`; SQLite ingestion is not implemented |

Every run reported **no SQL or InNavi game module loaded**, no product `Data/` directory, and successful validator-provider registration. All five input hashes stayed unchanged. Every report had `navigationChecked=false`.

## Dependencies and missing integration

- **Required:** UE modules `Core`, `CoreUObject`, `Json` and private `GeometryCore`, plus a compatible UE C++ toolchain. The commandlet bridge additionally uses `Engine`; this is not a validator dependency.
- **Data contract:** a caller supplies layout JSON with buildings, footprints/interiors, IDs and the expected units/fields. Specific campus files are optional; an unrelated project's actor model needs conversion. A SQLite source needs an importer before validation.
- **Missing UI:** the plugin registers a tool descriptor with an empty widget-class path. A new product must provide its own panel/host, actor-to-JSON adapter and selection/focus behavior. InNavi's dock panel is product code.
- **Missing route checks:** physical reachability needs a navigation planner/graph adapter; a clean data report does not prove rooms are reachable.
- **Distribution gap:** this test copied checkout source **1.1.1**. The repository's npm pin remains **1.0.0**; registry installation/authentication and other platforms/UE versions were not tested.

## Evidence / repeat

The [verification record](measurements/layout-validator-fresh-project-2026-11-19.json) contains host source, descriptor, dependencies, results and hashes. Raw logs/reports and the fresh-project source archive remain locally under `InNavi/Saved/T11FreshProject-20261119/`. The package's [standalone example](../InNavi/Plugins/InNaviLayoutValidator/Examples/README.md) documents the equivalent consumer setup and commandlet invocation; use the tested source version when reproducing this result.
