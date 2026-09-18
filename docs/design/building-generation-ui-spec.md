# External Building Generation — UI Integration Spec

> Written for Padre. This lists the C++ interface the UMG UI needs to call, what each
> screen needs to collect, and the overall screen structure. The code lives in
> `ANavPlayerController` (`InNavi/Source/InNavi/NavPlayerController.h`) and
> `ABuildingGenerator` (`BuildingGenerator.h`), and everything below is already marked
> `BlueprintCallable` — you can drag the nodes straight into UMG.
>
> Get a reference to `NavPlayerController` via `Get Owning Player` on your UMG widget
> and cast it. Every function/event below is called on that one reference — you never
> need to separately find `BuildingGenerator`.

---

## 1. Screen structure

Two layers: **start screen** (pick where buildings come from) → **edit screen** (the tools used to place buildings).

```
Start screen
├─ [Import building data]  ─┐
└─ [Start from blank]      ─┤
                            ↓
                        Edit screen (both paths land here)
                        ├─ Status bar: current building list / "Blank canvas" + back button
                        ├─ Draw mode panel (Plan A)
                        └─ Parameter mode panel (Plan B)
```

Draw mode and parameter mode are **not options on the start screen** — they're two tools available at any time on the edit screen, regardless of which start path was taken.

---

## 2. Start screen

### 2.1 Import building data

Decided: this uses the OS's native "open file" dialog, not an in-app preset list.

| UI element | Call / bind | Notes |
|---|---|---|
| "Import" button | `ImportBuildingDataFromFile()` | Opens the native file dialog filtered to `.json` right away — no separate "browse" step needed in UMG, the OS dialog is the browse step. Returns an `int32`: how many buildings were spawned. Returns `0` if the user cancels the dialog or the file fails to parse (see the Output Log for why) |

There's no separate "confirm" step in the interface — the function only returns once the user has closed the OS dialog (picked a file or cancelled), so a single button press does the whole "pick + confirm" job. If your mockup has a two-step "choose file, then confirm" flow, you can just call this once, and use the returned count (>0) to move on to showing the loaded building list.

### 2.2 Start from blank

No function call needed — go straight to the edit screen, status bar shows "Blank canvas".

---

## 3. Edit screen — Draw mode (Plan A)

User clicks points on the ground to trace out a building outline.

| UI element | Call / bind | Notes |
|---|---|---|
| Toggle button | `ToggleBuildMode()` | Turns draw mode on/off. Once on, clicking the ground (handled entirely in C++, nothing the UI needs to do) adds a point |
| Toggle state indicator | Bind event `OnBuildModeChanged(bool)` | Don't poll — bind this once; the bool param is the new on/off state |
| Point count display ("N points added") | Bind event `OnFootprintPointCountChanged(int32)` | Broadcasts the current point count after every point added, and after cancel/finish clears it back to 0 |
| Building id / name text fields (optional) | Pass into `FinishBuilding` as `CustomId`/`CustomName` | Optional. Leave both empty (or don't add the fields at all) and it auto-numbers ("NEW_1", "NEW_2", ...) same as before |
| "Finish" button | `FinishBuilding(CustomId, CustomName)` | Needs at least 3 points to succeed; fails on a self-intersecting outline, a duplicate id, etc. (failure reason currently only goes to the Output Log — ping me if you need a failure event for an in-game toast). Both params are optional and default to empty if you don't wire the text fields above |
| "Cancel" button | `CancelBuilding()` | Clears the points drawn so far |

The `B`/`Enter`/`Esc` keyboard shortcuts still work and do the same thing as the buttons above — both stay usable.

---

## 4. Edit screen — Parameter mode (Plan B)

User fills a form, generates in one step. **No mode toggle needed** — unlike Plan A, there's nothing to turn on first; just fill the form and hit generate.

### 4.1 Common fields (needed regardless of shape)

| Field | Type | Notes |
|---|---|---|
| Building id | string | Required, must not collide with an existing building (duplicate ids fail) |
| Building name | string | |
| Position X / Y | number | Center point of the building in world space (same units as every other building in the scene) |
| Rotation degrees | number | |
| Floor count | integer | |
| Floor height | number | Fine to default this in the UI (e.g. 300) if left blank |

### 4.2 Shape dropdown

| Option | Extra fields | Function to call |
|---|---|---|
| Rectangle / square | Width, Depth | `CreateRectangleBuilding(Id, Name, Position, Width, Depth, RotationDegrees, FloorCount, FloorHeight)` |
| Regular polygon (triangle / pentagon / hexagon…) | Sides, Radius | `CreateRegularPolygonBuilding(Id, Name, Position, Sides, Radius, RotationDegrees, FloorCount, FloorHeight)` |

"Triangle / pentagon / hexagon" can be nice display labels in the UI — underneath, they're all just the `Sides` number: 3 = triangle, 5 = pentagon, 6 = hexagon, and so on. Radius is the distance from the center to a vertex, not to an edge.

Both functions return the spawned building (success) or `nullptr` (failure, e.g. duplicate id) — check the return value if you want to show an error to the user.

---

## 5. Do not build against these (my own test scaffolding — will be deleted)

`ToggleShapeTestMode()` / `IsInShapeTestMode()` / `OnShapeModeChanged`, plus the `R` key — these are a temporary stand-in I'm using to test `CreateRectangleBuilding`/`CreateRegularPolygonBuilding` before your UI exists (it reads a hardcoded test json file to simulate form input). Once the UI is wired to the two functions in 4.2, I'll delete these — **don't bind the real UI to them.**

---

## 6. Still open

1. Plan A failure reasons (duplicate id / self-intersecting outline / not enough points) currently only show up in the Output Log — let me know if you want a failure-reason event so the UI can show a proper error message.
