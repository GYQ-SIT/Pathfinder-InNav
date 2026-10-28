# Project file format

> Team **Pathfinder** · Project **InNavi** · Guidelines C2, C3, C5, C7 (M2 Technical Guidelines)

A **project** is one JSON file: the buildings of a campus, and for each building its outline, its floors, rooms, doors and connectors, and the
doors in its outer wall. The editor saves it (File > Save), the editor and the phone app load it, the layout validator checks it, and the route
planner works on what is loaded from it. One format serves all of them, with the same names for the same things (a building's `id`, a room's `id`,
a connector's `connectorGroupId`) in the file, in the code, on the screens and in the phone app.

The format is described three ways, and they say the same thing:

| What | Where | For |
|------|-------|-----|
| This page | `docs/design/project-file-format.md` | people |
| The schema | [`docs/schemas/project.schema.json`](../schemas/project.schema.json) (JSON Schema, draft-07) | tools: it is checked in CI and in the tests |
| The code | `ABuildingGenerator::SaveBuildingsToFile` / `LoadBuildingsFromJson`, `AInteriorGenerator` (`SerializeCurrentInterior`), `InteriorEntrance.h` | the program |

## An example, cut short

```json
{
  "version": 1,
  "scale": 10,
  "floorHeight": 300,
  "projectName": "Entrance test",
  "buildings": [
    {
      "id": "A", "name": "Building A", "offset": [0, 0], "floorCount": 3, "floorHeight": 300,
      "footprint": [[0, 0], [100, 0], [100, 100], [0, 100]],
      "interior": {
        "floors": [
          {
            "level": 1, "wallHeight": 280, "wallThickness": 20,
            "rooms": [
              {
                "id": "A-01-01", "name": "Toilet 1-01", "tags": ["generated"], "roomType": "Toilet", "access": "public", "requireKey": false, "closed": true,
                "footprint": [[96.5, 3.5], [146.3, 3.5], [146.3, 73.5], [96.5, 73.5]],
                "openings": [ { "id": "A-01-01-D01", "type": "door", "edgeIndex": 2, "offset": 204, "width": 90, "inputId": "A-01-01", "connectsTo": "corridor" } ]
              }
            ],
            "navNodes": [ { "id": "A-NAV-01-F01", "type": "elevator", "position": [16.5, 17], "connectsFloors": [1, 2, 3], "connectorGroupId": "A-NAV-01" } ]
          }
        ],
        "entrances": [
          { "id": "A-EN01", "kind": "main", "level": 1, "edge": 3, "offset": 880, "width": 240 },
          { "id": "A-EN02", "kind": "link", "level": 1, "edge": 1, "offset": 880, "width": 240, "connectsTo": "B" }
        ]
      }
    }
  ]
}
```

A complete file to open and read is `docs/test-data/entrance-test-2-with-doors.json` (three buildings, generated floors, doors between them).

## The fields

**The file**

| Field | Type | Meaning | If absent |
|-------|------|---------|-----------|
| `version` | integer | The format version that wrote the file (see "Versions" below) | 0, read as 1 |
| `scale` | number > 0 | Centimetres in one unit of the footprints and positions of the file | 1 |
| `floorHeight` | number > 0 | Height of one floor, in cm, for buildings that do not say their own | 300 |
| `projectName`, `description` | string | What the project library shows | empty |
| `buildings` | array | **Required.** The buildings | - |

**A building** (`buildings[]`)

| Field | Type | Meaning | If absent |
|-------|------|---------|-----------|
| `id` | string | **Required.** Unique in the file. How the rest of the file, the screens and the phone app name the building | - |
| `name` | string | **Required.** Shown to people | - |
| `footprint` | array of `[x, y]`, at least 3 | **Required.** The outline, in footprint units. Edge *i* runs from point *i* to point *i* + 1 (the last to the first) | the building is skipped (a warning in the log) |
| `offset` | `[x, y]` | Where the building stands among the others, in footprint units | `[0, 0]` |
| `floorCount` | integer >= 1 | How many floors | 10 |
| `floorHeight` | number > 0 | In cm | the file's |
| `interior` | object | The floors and doors of the building (below) | an empty building |

**The interior** (`interior`): `floors` (array) and `entrances` (array), both optional.

**A floor** (`floors[]`): `level` (integer >= 1, **required**; 1 is the ground floor), `wallHeight` and `wallThickness` (cm), `rooms` and `navNodes` (arrays).

**A room** (`rooms[]`)

| Field | Type | Meaning |
|-------|------|---------|
| `id` | string | **Required.** Unique in the building; the phone app and the validator use it to say which room |
| `name` | string | Shown to people |
| `tags` | array of string | What the search also finds (blank ones and repeats are dropped, ignoring case) |
| `roomType` | string | One of the types of the editor (`RoomTypes.h`); the room's category on the phone |
| `access` | `"public"` or `"private"` | Whether the route may pass through |
| `requireKey` | boolean | Whether a key is needed |
| `closed` | boolean | False for a wall that does not close into a room |
| `footprint` | array of `[x, y]` | **Required.** The room's outline, in footprint units, in the same space as the building's |
| `openings` | array | The doors and windows of the room's walls (below) |

**An opening** (`openings[]`): `id`, `type` (`"door"`, `"window"` or `"void"`), `edgeIndex` (which edge of the room's footprint), `offset` (distance along the edge to the start of
the opening, **cm**), `width` (**cm**). A window also has `height` and `sillHeight`. A door has `inputId` (what it opens from) and `connectsTo` (what it opens to: a room's id, or `"corridor"`).

**A connector** (`navNodes[]`): `id`, `type` (`"elevator"`, `"stairs"`, `"escalator"` or `"pathNode"`), `position` (`[x, y]`, footprint units), `connectsFloors` (the levels it
joins), `connectorGroupId` (the instances of one connector on its floors share it; this is what lets a route change floor), and when they are not 0 and 1, `rotationDegrees` and `sizeScale`.

**A door in the outer wall** (`entrances[]`): `id`, `kind` (`"main"`, the way out of the building, or `"link"`, a door to the building next door), `level`, `edge` (which edge of the
building's footprint), `offset` and `width` (**cm**), and for a link `connectsTo` (the other building's id). A link door is stored once; the other building shows it as a door that is
worked out, not stored.

## Units and coordinates

- **Footprints, `offset` of a building, room footprints and connector positions** are in **footprint units**. One unit is `scale` centimetres (`scale` 10: a unit is 10 cm). All of these
  are in the same space, the building's own; the building's `offset` places it among the others.
- **Everything measured along or across a wall** (`offset` and `width` of an opening or of a door in the outer wall, wall thickness and height, floor height) is in **centimetres**.
- **Levels** start at 1. **Edges** and **points** are counted from 0, in the order written.
- Text is **UTF-8**. The editor writes a byte order mark on Windows, so a reader has to accept one (the checks of the repository do).

## Versions

The file says which format wrote it (`version`, **1** now), and every build reads older files:

- A file **without** `version` was made before versions existed. It is the same format as 1 and is read as it is.
- A file with a **newer** version than the build knows is read as well as it can be, with a warning in the log; fields that build does not know are ignored.
- **Adding** an optional field does not change the version: older builds ignore it and older files simply do not have it (the defaults above apply).
- **Changing what a field means**, removing one, or making an optional one required **raises the version**, and the loader keeps reading the older one (converting it as it loads). The change
  is written down here, in the list below, in the same commit.
- Saving always writes the current version, so opening an old file and saving it is the conversion.

| Version | What it is |
|---------|-----------|
| 0 | Files saved before `version` existed. The same layout as 1; they may lack the optional fields that were added later (for example `projectName`, `description`, `entrances`, `rotationDegrees`, `sizeScale`), and the defaults above apply |
| 1 | The current format |

`InNavi.Project.FileVersion` (in `NewProjectTests.cpp`) checks that a project says its version and that files without one, or with a newer one, still open; saving and loading again gives
back what was saved.

## What happens to input that is wrong

| Input | What the program does |
|-------|-----------------------|
| Not JSON, or no `buildings` array | Nothing is loaded; an error in the log, and the action fails with "No buildings were loaded from that file - check that it matches the expected JSON format." (the Debug panel shows it as the last error, in red) |
| A building without a footprint | That building is skipped; a warning in the log; the others load |
| A field of the wrong type, or a value outside what is allowed | Not caught by the loader (it reads what it can); caught by the schema in CI for the files of the repository, and by the **layout validator** (Window > Validation) for geometry and references: doors on an edge that is not there, rooms that cannot be reached, overlapping rooms, and so on |
| A field nobody knows | Ignored by the loader; reported by the schema (it is how a misspelt field is found) |
| A phone prototype file that is not valid or does not say it is synthetic | Refused; the phone app has no prototype data, and says why in the log |

## Checking a file

```bash
python scripts/validate_schema.py docs/schemas/project.schema.json path/to/project.json
python scripts/test_schemas.py        # the schemas' own tests
```

`scripts/validate_schema.py` uses only Python's standard library. The same schema is checked from the other side too: `InNavi.Project.Schema.SavedProjectIsValid` saves a project the way the
editor does (generated floors, doors, a lift, a door in the outer wall) and checks it against `project.schema.json`, so a field added to the saver and not to the schema fails the tests, and
the other way round.

## Other formats

| File | Format | Schema |
|------|--------|--------|
| `InNavi/Data/Prototype/mobile-prototype.json` | The phone prototype's made-up data (`"format": "innavi-mobile-prototype"`, `"version": 1`, `"synthetic": true`) | [`mobile-prototype.schema.json`](../schemas/mobile-prototype.schema.json) |
| `InNavi/Data/DefaultLayouts/*.json`, `Saved/*DockLayout.json` | A screen's dock layout (`layoutVersion`; an older or newer one is not used, and the default is) | - |
| `InNavi/Data/map_extended.db` | The campus: an SQLite database (`buildings`, `building_nodes`, `roads`, `road_nodes`, `pois`) read by `LoadBuildingsFromDatabase` | the tables of the database |
