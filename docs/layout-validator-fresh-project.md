# T11 — Layout validator in a fresh UE project

**Verified:** 19 November 2026 · Windows, UE 5.8.2 · plugin source **1.2.0**, main commit `3ceab94`.

**Verdict: the reusable JSON/geometry validator works in a fresh project without SQL or InNavi's data files. The complete InNavi validation panel and route checks do not come with it.** No plugin source changes were needed.

## What was tried

Created a new minimal C++ project, `T11ValidatorProbe`, outside the repository and OneDrive. Copied only the plugin's source/descriptor and package metadata into `Plugins/InNaviLayoutValidator`; enabled it, disabled default engine plugins and explicitly disabled `SQLiteCore`/`SQLiteSupport`. No InNavi actors, game module, assets or `Data/` folder were copied. A small editor commandlet called `ValidateFile` and `ExportReport`.

Both **Win64 Development Editor and Game targets compiled and linked**. The Game target was not cooked, packaged or launched.

| Input / check | Observed result |
|---|---|
| Independent layout with new building/room IDs | Exit **0**, no findings |
| Same layout with a door offset beyond its wall | Exit **1**, `OPENING_BOUNDS` |
| Unrelated JSON object | Exit **1**, `DATA_TYPE` |
| Copy of our `Data/buildings.json` | Exit **0**; two `CONNECTOR_ENCLOSED` warnings, three `INTERIOR_UNAUTHORED` information findings |
| Direct copy of `map_extended.db` | Exit **1**, `FILE_READ`; SQLite ingestion is not implemented |
| Doorless lift/stair/escalator; window-only and void-only rooms (five cases) | Exit **0**, `CONNECTOR_ENCLOSED` **Warning** in every case |
| Connector in a room with a door / outside the room (two controls) | Exit **0**, no findings |

All **12** runs reported **no SQL or InNavi game module loaded**, no product `Data/` directory, and successful provider registration. Input hashes stayed unchanged. Every report had `navigationChecked=false`: enclosure warnings are geometry heuristics, not route-reachability checks.

## Dependencies and missing integration

- **Required:** UE modules `Core`, `CoreUObject`, `Json` and private `GeometryCore`, plus a compatible UE C++ toolchain. The commandlet bridge additionally uses `Engine`; this is not a validator dependency.
- **Data contract:** a caller supplies layout JSON with buildings, footprints/interiors, IDs and the expected units/fields. Specific campus files are optional; an unrelated project's actor model needs conversion. A SQLite source needs an importer before validation.
- **Missing UI:** [`DescribeTool()`](https://github.com/GYQ-SIT/Pathfinder-InNav/blob/3ceab9457ec6079f6b24d526cf33d19c97a49139/InNavi/Plugins/InNaviLayoutValidator/Source/InNaviLayoutValidator/Private/InNaviLayoutValidatorModule.cpp#L7-L14) constructs `FDescriptor` and assigns only `Id`/`Title`; it never assigns [`WidgetClassPath`](https://github.com/GYQ-SIT/Pathfinder-InNav/blob/3ceab9457ec6079f6b24d526cf33d19c97a49139/InNavi/Plugins/InNaviLayoutValidator/Source/InNaviLayoutValidator/Public/LayoutToolRegistry.h#L10-L16), leaving its `FString` default-empty. Run metadata confirms this. A consumer supplies panel/host, actor-to-JSON and selection/focus adapters.
- **Missing route checks:** physical reachability needs a navigation planner/graph adapter; a clean data report does not prove rooms are reachable.
- **Distribution gap:** this test copied main's source **1.2.0**. Its npm pin remains **1.0.0**; registry installation/authentication and other platforms/UE versions were not tested.

## Evidence / repeat

The [verification record](measurements/layout-validator-fresh-project-2026-11-19.json) includes source, dependencies, results and hashes. The [downloadable raw evidence ZIP](measurements/layout-validator-fresh-project-2026-11-19-raw.zip) contains unmodified build/run logs, reports, inputs, the exact fresh-project/plugin source, a SHA-256 manifest and `reproduce.py`. Extract outside a synced folder and run it with `--engine <UE_5.8 root>`. This source-based reproduction does not use the older npm pin.
