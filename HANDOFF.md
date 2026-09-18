# InNavi — Handoff / Continuation Notes

Written 2026-09-17 so a fresh Claude Code session (e.g. on another machine) can pick up
this work without the original conversation history. If you're Claude reading this at the
start of a new session: read this whole file first, then check `git log --oneline -20` on
`YQ-ui-backup` to see exactly what's landed, before touching any code.

## Who / what / where

- **User**: Yongqi (GYQ-SIT), Tech Lead of team "Pathfinder", DigiPen Singapore course
  CSD3401/UXG3400. New-ish to C++/Unreal, first time building UMG UI — walk them through
  UMG changes click-by-click; they build the UI themselves, you do all C++.
- **Project**: InNavi — standalone Unreal Engine 5.8 indoor navigation/building simulator.
  Flow: see campus in 3D → select a building → enter its interior (2D floor-plan editor) →
  eventually: navigate inside via a room/door/connector graph.
- **Repo**: `https://github.com/GYQ-SIT/Pathfinder-InNav` (private). Local clone at
  `C:\Users\admin\Documents\GitHub\Pathfinder-InNav`. UE project under `InNavi/`.
- **Branch**: `YQ-ui-backup` — this is where ALL work happens. It is pushed to
  `origin/YQ-ui-backup` (the user pushes it themselves so they can switch devices;
  this used to be a local-only branch but that rule no longer applies). Never push to
  `main` without being explicitly asked.
- **Commits**: only commit when the user explicitly says so ("可以commit" / "测试通过，可以commit").
  **Do not add any Claude/AI attribution line to commit messages** — the user asked for
  this explicitly early in the project.
- **Rebuild discipline**: any C++ header change (new UFUNCTION, new member, new class) needs
  a full Rebuild (close nothing, just rebuild via Visual Studio / the IDE — Live Coding is
  fragile on header diffs in this project). A change confined to a `.cpp` function body
  (no header touched) only needs Live Coding. Always tell the user which one is needed.
- **Workflow loop**: you edit code → tell user Rebuild vs Live Coding → they rebuild/test in
  the Unreal Editor → they report "测试通过" or a bug (often with a screenshot) → you fix or
  move on → commit only when asked.

## Architecture recap

- `BuildingGenerator` — spawns `APolyBuilding` actors from `Data/buildings.json`
  (exterior shells, ear-clipping triangulation for concave footprints).
- `InteriorGenerator` (`AInteriorGenerator`) — owns all interior data for whichever building
  is currently loaded: rooms/walls (`RoomsByFloor: TMap<int32, FInteriorRoomList>`), doors
  (openings), nav nodes. Loads/saves interior JSON nested under each building's `"interior"`
  key in the same `buildings.json` (so a building's interior travels with its exterior file).
- `AInteriorRoom` — one actor per room OR wall. `bClosedLoop` (`IsClosedLoop()`) distinguishes
  a genuine closed room (has an interior, can take a door, defines walkable floor area) from
  an open wall segment (no interior, can't take a door). Procedural mesh, `CachedFootprint`/
  `CachedOpenings` are the source of truth; `RebuildMesh()` regenerates from scratch each time.
- `ANavPlayerController` — the hub for everything: mode state (build/select/draw-room/
  draw-wall/add-door/prefab-shape, all mutually exclusive), selection state (room+door),
  camera framing, all the Blueprint-callable actions UMG calls into.
- `UInteriorFloorPlanWidget` (`InteriorFloorPlanWidget.h/.cpp`) — the actual 2D floor-plan
  canvas. Custom `NativePaint` draws the building outline + every room/wall's edges (with
  door gaps/markers) as flat 2D lines — **no 3D camera involved**, this is a real top-down
  vector view, not a tilted-camera trick (that was an earlier, abandoned approach — see
  git log around "pivot to 2D canvas" commits if curious why). Handles all mouse input for
  the canvas: click-to-add-point, click-to-select, drag-to-move (door and room), pan/zoom.
  Self-discovers `InteriorGenerator`/`BuildingGenerator`/`NavPlayerController` in
  `NativeConstruct` via `TActorIterator` — no Blueprint wiring needed for those references.
- UMG screens: `WBP_StartScreen`, `WBP_EditScreen` (exterior/building-select), `WBP_InteriorScreen`
  (hosts the floor-plan canvas + `Pnl_DoorProperties` + `Pnl_RoomProperties` panels),
  `WBP_ConfirmDialog` (unsaved-changes prompt). All in `InNavi/Content/`, flat, no subfolders.

## What's built and working (as of commit `65ccd95`)

Keyboard-driven for now (bound in `SetupInputComponent`, not yet wired to on-screen UMG
buttons except where noted) — this is intentional, a later pass will replace keys with
proper UI buttons:

- **B** — exterior build mode (draw a new building's footprint).
- **`[` / `]`** — cycle interior floor up/down. **F** / **X** — add/remove floor.
- **P** — draw a custom-shaped closed room (click points, Enter to close/finish).
- **L** — draw an open wall (same click-point mechanic, never closes, can't take a door).
- **Q** — persistent Select mode (not one-shot): click a room (edge OR now anywhere inside
  its body) or a door marker to select it; **Backspace** deletes the selection; drag a
  selected room or door to move it.
- **1 / 2 / 3** — arm placing a prefab room (rectangle/triangle/circle) at the next click.
- **K** — add-door mode: click a wall edge to cut a fixed-size (90×210) door into it.
- **`=` / `-`** — grow/shrink the selected room (test-only keys, bounding-box rescale).
- **Ctrl+S** — save current project to its JSON file.

Data/behavior:
- **Doors**: `FInteriorOpening.InputId` is auto-set at creation to the door's own room
  (read-only, never user-edited — it's the room the door "anchors" to, per an explicit
  design discussion with the user). `.ConnectsTo` is user-editable via a dropdown in
  `Pnl_DoorProperties` — populated from `ANavPlayerController::GetConnectableTargetIds()`
  ("corridor" + every other closed room on the door's floor, excluding the door's own room).
  Doors render as a fluorescent-blue marker line (red if selected) cut into the wall; a wall
  edge can hold more than one door (fixed after initially only rendering the first).
  Independently selectable/deletable/draggable-along-its-wall from the room it's cut into.
- **Rooms**: can be renamed (`RoomName`, distinct from the auto-generated `RoomId` which
  follows `{Building}-{Floor:02d}-{index:02d}`, e.g. `E1-01-03`) via `Pnl_RoomProperties`.
  Selectable by clicking anywhere in the room's body now, not just its outline. Draggable
  (whole-room translate) in Select mode.
- **Geometry guards** (`InteriorGenerator.cpp`, static helpers near the top of the file):
  - A room/wall can never be placed, resized, or dragged to extend outside the building's
    own outline — checked via `IsFootprintFullyInsidePolygon` (every vertex, not just a
    center point — an earlier prefab-placement bug only checked the center).
  - Rooms/walls can't overlap each other. Overlap is **allowed transiently while dragging**
    (checked once on mouse-up via `CommitRoomMove`, reverting to the pre-drag position if it
    landed on something) but **rejected outright at creation time** (`CreateRoomFromFootprint`).
  - `DoFootprintsOverlap` had two rounds of real bugs worth knowing about if you touch it:
    (1) `SegmentsIntersect`'s strict cross-product-sign test can misfire when two edges
    share an endpoint — which is the *normal* result of snapping two shapes' corners
    together — because one of its four cross products lands exactly on the ">0" boundary.
    Fixed by detecting a shared endpoint up front and skipping the crossing test for that
    edge pair. (2) point-in-polygon containment is unreliable for a point sitting exactly
    on (or a float-rounding hair from) the boundary rather than clearly inside/outside.
    Fixed via `IsPointClearlyInsidePolygon`, which treats anything within a tiny dead zone
    of an edge as "boundary, not inside" before ever asking the ray-cast. If you see a
    mysterious false-positive/negative overlap report again, suspect this class of bug
    first — it's inherent to cross-product/ray-cast tests at exact boundaries, not a typo.
  - **Deliberately no general "near miss" distance tolerance** in the overlap check — that
    was tried and reverted because it directly conflicts with snapping (which intentionally
    makes two vertices coincide exactly, distance 0, which any positive tolerance would
    then flag as overlapping).
- **Snapping** (`AInteriorGenerator::FindSnapOffset`): vertex/edge snapping (not just
  corners — snaps to the closest point anywhere along another room/wall's edges, corners
  included as a special case) for P-mode's next click and for dragging a room. Tolerance
  is 8 footprint units (was 20, felt like it snapped from too far away — tune in
  `InteriorFloorPlanWidget.cpp`/`NavPlayerController.cpp`, search `SnapToleranceFootprintUnits`).
  Does **not** currently snap to the building's own exterior-wall vertices (only to other
  rooms/walls) — see Pending below.
- **Camera**: New/Open now frames a top-down overview of the whole building cluster
  (`FocusCameraOnAllBuildings`) instead of leaving the pawn at ground level. Entering a
  building's interior still uses the older tilted-top-down `PositionCameraForFloor` —
  though note the 2D canvas is what's actually authoritative for interaction now; the 3D
  camera view during interior editing is largely vestigial/cosmetic at this point.

## Pending / explicitly deferred (user asked to be reminded)

1. **A settings toggle to turn snapping on/off** — currently always on, no way to disable it.
2. **Snap to the building's own exterior wall** — `FindSnapOffset` only considers other
   rooms/walls right now, not the building's own outline vertices (useful for placing a
   room flush against a corner of the building itself).

Other known-open items from earlier in the project (not touched this session, still true
as far as I know):
- Nav node (Path Node / Elevator / Stairs) placement tool — discussed conceptually, not
  implemented.
- Door `ConnectsTo`/`InputId` aren't consumed by anything yet — ws/jh's navigation work
  (NavMesh/NavLinks) hasn't started reading this data as far as this session knows.
- Everything listed above under "Keyboard-driven for now" still needs real UMG buttons
  eventually (Kaiming/PM's original paper-prototype mockup shows proper toolbar buttons,
  a "Create Room" popup with Name/Access/RequireKey, Layers panel, etc. — see the earlier
  conversation / any saved copy of `editor paper prototype.pdf` if you need the reference).
- CI/automated build (rubric requirement) not set up.

## Collaboration notes

- Go one step at a time; the user tests after each change before moving on. Don't bundle
  multiple unrelated changes into one big change they haven't asked for.
- When guiding UMG changes, be extremely click-by-click explicit (Designer tab vs Graph
  tab, exact node names to search for, which pin to drag from) — this user had never
  touched UMG before this project. Watch for these recurring UE5.8-specific gotchas that
  already bit us:
  - The plain text-input widget is called **"Text Box"** in the palette now, not "Editable
    Text Box" (that name doesn't exist anymore in 5.8's search).
  - `SetText (Text Box)` vs `SetText (Text)` vs `Set Hint Text (Text Box)` are three
    different, similarly-named functions in the search results — easy to grab the wrong
    one. Hint Text sets placeholder text, not real content.
  - `UFUNCTION(BlueprintCallable)` on a C++ function marked `const` gets silently treated
    as a pure node (no exec pins) by the Blueprint compiler — bit us twice. If a Blueprint
    node you expected to need sequencing has no white exec triangles, check whether the
    backing C++ function is `const` and remove it if it shouldn't be pure.
  - "Get Player Controller" (plain, index-based) doesn't reliably surface in the 5.8
    context search — use **"Get Owning Player"** instead when you're already inside a
    Widget Blueprint's graph (no index needed, returns the widget's own owning controller).
  - When wiring `Bind Event to On X Changed`, always drag off its own red "Event" pin to
    "Add Custom Event" (auto-generates a matching signature) rather than hand-building one
    — and double check you didn't accidentally leave a duplicate/dead `Bind Event to...`
    node sitting in the execution chain with nothing wired to its Event pin (happened once).
- Give complete, coherent changes rather than partial diffs described in prose.
- Warn before anything risky: deleting classes, `.Build.cs` edits, anything needing a
  Visual-Studio-project regeneration.

## If picking this up fresh

1. `git log --oneline -20` on `YQ-ui-backup` to confirm what's actually landed (this file
   might drift out of date; the commit log is ground truth).
2. Ask the user what they want to work on next — the two Pending items above are the most
   recently stated priorities, but confirm before assuming.
3. Don't re-litigate settled design decisions documented above (vertex-only snapping, no
   overlap tolerance, InputId being auto/read-only, etc.) without a reason — they were
   each the result of a real back-and-forth with the user.
