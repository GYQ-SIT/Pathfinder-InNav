# InNavi — Handoff / Continuation Notes

Written 2026-09-17, updated 2026-09-18, so a fresh Claude Code session (e.g. on another
machine) can pick up this work without the original conversation history. If you're
Claude reading this at the start of a new session: read this whole file first, then
check `git log --oneline -20` on `YQ-ui-backup` to see exactly what's landed, before
touching any code.

## Who / what / where

- **User**: Yongqi (GYQ-SIT), Tech Lead of team "Pathfinder", DigiPen Singapore course
  CSD3401/UXG3400. New-ish to C++/Unreal, first time building UMG UI — walk them through
  UMG changes click-by-click; they build the UI themselves, you do all C++.
- **Project**: InNavi — standalone Unreal Engine 5.8 indoor navigation/building simulator.
  Flow: see campus in 3D → select a building → enter its interior (2D floor-plan editor) →
  eventually: navigate inside via a room/door/connector graph.
- **Repo**: `https://github.com/GYQ-SIT/Pathfinder-InNav` (private). Local clone at
  `C:\Users\admin\Documents\GitHub\Pathfinder-InNav`. UE project under `InNavi/`.
- **Branch**: `YQ-ui-backup` is where all work happens; pushed to `origin/YQ-ui-backup`
  freely. As of 2026-09-18, `main` was fast-forward merged to match `YQ-ui-backup`
  (they were on a clean linear history, no divergence) — still ask before pushing to
  `main` again in the future, this was a one-off explicit sync, not a standing rule
  change.
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
  (openings), nav nodes (`NavNodes: TArray<FInteriorNavNode>`). Loads/saves interior JSON
  nested under each building's `"interior"` key in the same `buildings.json` (so a
  building's interior travels with its exterior file).
- `AInteriorRoom` — one actor per room OR wall. `bClosedLoop` (`IsClosedLoop()`) distinguishes
  a genuine closed room (has an interior, can take a door, defines walkable floor area) from
  an open wall segment (no interior, can't take a door). Procedural mesh, `CachedFootprint`/
  `CachedOpenings` are the source of truth; `RebuildMesh()` regenerates from scratch each time.
- `ANavPlayerController` — the hub for everything: mode state (build/select/draw-room/
  draw-wall/add-door/prefab-shape/connector-placement, all mutually exclusive via
  `Exit*Mode()` helpers), selection state (room/door/connector, mutually exclusive),
  camera framing, all the Blueprint-callable actions UMG calls into.
- `UInteriorFloorPlanWidget` (`InteriorFloorPlanWidget.h/.cpp`) — the actual 2D floor-plan
  canvas. Custom `NativePaint` draws the building outline, every room/wall's edges (with
  door gaps/markers), and connector markers (color-coded boxes) as flat 2D lines — **no 3D
  camera involved**, this is a real top-down vector view, not a tilted-camera trick (that
  was an earlier, abandoned approach). Handles all mouse input for the canvas:
  click-to-add-point, click-to-select, drag-to-move (door and room), pan/zoom. Self-discovers
  `InteriorGenerator`/`BuildingGenerator`/`NavPlayerController` in `NativeConstruct` via
  `TActorIterator` — no Blueprint wiring needed for those references.
- UMG screens (`InNavi/Content/`, flat, no subfolders): `WBP_StartScreen`, `WBP_EditScreen`
  (exterior/building-select), `WBP_InteriorScreen` (hosts the floor-plan canvas +
  `Pnl_DoorProperties` + `Pnl_RoomProperties` + `Pnl_ConnectorProperties` panels, and the
  unsaved-changes confirm-dialog flow on its Back button), `WBP_ConfirmDialog`
  (reusable Save/Discard/Cancel prompt — also used by `WBP_EditScreen`'s own back button).

## What's built and working (as of commit `5c3c584`, `main` and `YQ-ui-backup` in sync)

Keyboard-driven for now (bound in `SetupInputComponent`, not yet wired to on-screen UMG
buttons except where noted) — this is intentional, a later pass will replace keys with
proper UI buttons. Full user-facing control list: `docs/design/interior-editor-guide.md`.

- **B** — exterior build mode. **`[`/`]`** — cycle floor. **F**/**X** — add/remove floor.
- **P** — draw a custom closed room. **L** — draw an open wall (no door, no interior).
- **1/2/3** — place a prefab room (rectangle/triangle/circle).
- **Q** — persistent Select mode: click a room (anywhere in its body)/door marker/connector
  marker to select it; **Backspace** deletes; drag a selected room or door to move it.
- **K** — add-door mode: click a wall edge to cut a fixed-size door into it.
- **4/5/6** — place a connector (elevator/stairs/escalator) at the next click.
- **N** — toggle vertex/edge snapping on/off (on by default).
- **`=`/`-`** — grow/shrink the selected room (test-only keys).
- **Ctrl+S** — save current project to its JSON file.

Data/behavior:
- **Doors**: `FInteriorOpening.InputId` auto-set at creation to the door's own room
  (read-only). `.ConnectsTo` is user-editable via a **dropdown** in `Pnl_DoorProperties` —
  shown/picked by room **name**, not id (`GetConnectableTargetIds` returns names;
  `SetSelectedDoorConnectsTo` resolves the picked name back to the room's stable id before
  storing, so a later rename doesn't break the connection; `ResolveConnectsToDisplayName`
  does the reverse lookup to pre-select the dropdown). "corridor" is always the first
  option. Doors render as a fluorescent-blue marker (red if selected); a wall edge can hold
  more than one door. Independently selectable/deletable/draggable along its wall.
- **Rooms**: renameable (`RoomName`, distinct from the auto id `{Building}-{Floor:02d}-
  {index:02d}`) via `Pnl_RoomProperties`. Selectable anywhere in its body. Draggable.
- **Elevator/Stairs/Escalator connectors** (`FInteriorNavNode`, `EInteriorNavNodeType` enum
  - data format agreed with the navigation team, ws/jh, before implementing): placing one
  creates a separate node per floor it connects to, all sharing a fresh `ConnectorGroupId`
  (`AInteriorGenerator::PlaceConnectorNode`) - elevator defaults to every floor in the
  building, stairs/escalator to just the adjacent floor(s). **All-or-nothing validation**
  per floor (`CanPlaceConnectorOnFloor`: floor must exist, position inside the building
  outline, not inside a wall's solid thickness - being inside a room's open interior is
  fine) - if any requested floor fails, nothing is placed anywhere. Rendered as a
  color-coded placeholder box (blue/purple/green by type, red if selected) on the active
  floor only - swapping in real icons later only touches the `NativePaint` rendering, not
  selection/data. Select/delete like rooms/doors (delete removes the node on every floor).
  `Pnl_ConnectorProperties` shows the type (read-only) and 20 fixed floor checkboxes
  (`Chk_Floor1`..`Chk_Floor20`) to edit which floors it reaches
  (`AInteriorGenerator::SetConnectorFloors` diffs old vs new floor list: validates+creates
  newly-added floors, deletes dropped floors, syncs `ConnectsFloors` on what's kept) -
  **known gap**: the panel doesn't hide checkboxes beyond the building's actual floor
  count, ticking a nonexistent floor just fails on Apply with a clear error.
- **Unsaved-changes confirmation**: both `WBP_EditScreen`'s back-to-start button and
  `WBP_InteriorScreen`'s back-to-exterior button check `HasUnsavedChanges()` and, if true,
  show `WBP_ConfirmDialog` (Save/Discard/Cancel) before leaving, reusing the same
  `Bind Event to On Save/Discard/Cancel` pattern in both places.
- **Geometry guards** (`InteriorGenerator.cpp`, static helpers near the top of the file):
  - A room/wall/connector can never be placed, resized, or dragged to extend outside the
    building's own outline (`IsFootprintFullyInsidePolygon`/`IsPointInPolygon` - every
    vertex, not just a center point).
  - Rooms/walls can't overlap each other. Overlap is **allowed transiently while dragging**
    (checked once on mouse-up via `CommitRoomMove`, reverting to the pre-drag position if
    it landed on something) but **rejected outright at creation time**.
  - `DoFootprintsOverlap` had two rounds of real bugs worth knowing about if you touch it:
    (1) `SegmentsIntersect`'s strict cross-product-sign test can misfire when two edges
    share an endpoint - the *normal* result of snapping two shapes' corners together -
    because one of its four cross products lands exactly on the ">0" boundary. Fixed by
    detecting a shared endpoint up front and skipping the crossing test for that pair.
    (2) point-in-polygon containment is unreliable for a point sitting exactly on (or a
    float-rounding hair from) the boundary. Fixed via `IsPointClearlyInsidePolygon`,
    which treats anything within a tiny dead zone of an edge as "boundary, not inside"
    before ever asking the ray-cast. If a mysterious false-positive/negative overlap
    report shows up again, suspect this class of bug first.
  - **Deliberately no general "near miss" distance tolerance** in the overlap check - it
    directly conflicts with snapping (which intentionally makes two vertices coincide
    exactly, distance 0, which any positive tolerance would flag as overlapping).
- **Snapping** (`AInteriorGenerator::FindSnapOffset`): vertex/edge snapping (closest point
  on any edge, not just corners) against other rooms/walls **and the building's own
  exterior outline**, for P-mode's next click and for dragging a room. Tolerance 8
  footprint units (`SnapToleranceFootprintUnits`). Toggleable with **N**
  (`bSnapEnabled`/`ToggleSnapEnabled`) - a standing setting, not a mode.
- **Camera**: New/Open frames a top-down overview of the whole building cluster
  (`FocusCameraOnAllBuildings`). Entering a building's interior uses the older tilted-
  top-down `PositionCameraForFloor` - the 2D canvas is what's actually authoritative for
  interaction now, the 3D camera view during interior editing is largely vestigial.
- **Docs**: `README.md` has real build/run steps; `docs/design/interior-editor-guide.md`
  is the up-to-date control/workflow reference; `docs/deliverables.md` has current
  owner/status info for T01-T04 (T04/navigation status is **unconfirmed**, flagged for a
  check-in with ws/jh, not verified by this session).

## Pending / explicitly deferred

1. Connector properties panel's 20 checkboxes aren't scoped to the building's actual
   floor count (see above).
2. No placement tool for a plain `PathNode` (only Elevator/Stairs/Escalator have one).
   **Decided 2026-09-20: deferred to M2, not needed for M1.** `PathNode` would mark a
   navigable destination that isn't inside any room (e.g. a reception desk in an open
   lobby) - useful only if ws/jh's navigation UI lets users pick a destination that
   isn't a room. T04's M1 target (`docs/deliverables.md`) is explicitly room-to-room
   routing only, so this isn't blocking M1 either way. A (non-urgent) question was sent
   to ws/jh to confirm they don't need it for M1 either - revisit if they say otherwise.
3. Everything under "Keyboard-driven for now" still needs real UMG buttons eventually.
4. **CI/CD (rubric requirement R04/R05) - partially done as of 2026-09-20.** The team12
   DigiPen-provided OVH VM (51.79.240.111, 2 vCPU/4GB RAM, Ubuntu 24.04, shared `team12`
   SSH login - see `FullStack_OVH_team12.pdf`, key-based auth now set up so no password
   is needed) runs a registered self-hosted GitHub Actions runner (`team12-ovh-vm`,
   installed as a systemd service). `.github/workflows/validate-json.yml` validates every
   `InNavi/Data/*.json` file on push to `main`/`YQ-ui-backup` and is confirmed working
   (see `docs/deliverables.md` R04/R05). **Still missing**: the actual UE5 Windows
   build/package step - the VM can't do this (no Windows toolchain, not enough RAM even
   if it had one) - still needs a self-hosted runner on a teammate's own Windows PC,
   not yet set up, no owner assigned. **Flag if the deadline is getting close and this
   still hasn't moved.**
5. **M1 scope discussion, not yet confirmed with Kaiming/PM**: working assumption is that
   in-app pathfinding stays 2D (a computed route drawn on the existing floor-plan canvas)
   for M1, with a real 3D walkthrough/preview mode deferred to M2/M3. Don't treat as
   settled without checking - see `docs/deliverables.md` T03 M3 target and the session
   notes it was discussed in.
6. Whether ws/jh's navigation work (T04) is actually consuming the real room/door/connector
   data this session built, vs. still on isolated test boxes, is **unknown** - worth
   checking early if you're continuing this project, since it's the biggest open risk to
   an end-to-end M1 demo.

## Collaboration notes

- Go one step at a time; the user tests after each change before moving on. Don't bundle
  multiple unrelated changes into one big change they haven't asked for.
- When guiding UMG changes, be extremely click-by-click explicit (Designer tab vs Graph
  tab, exact node names to search for, which pin to drag from) — this user had never
  touched UMG before this project. Watch for these recurring UE5.8-specific gotchas that
  already bit us more than once:
  - The plain text-input widget is called **"Text Box"** in the palette now, not "Editable
    Text Box" (that name doesn't exist anymore in 5.8's search).
  - `SetText (Text Box)` vs `SetText (Text)` vs `Set Hint Text (Text Box)` are three
    different, similarly-named functions in search results — easy to grab the wrong one.
    Hint Text sets placeholder text, not real content.
  - `UFUNCTION(BlueprintCallable)` on a C++ function marked `const` gets silently treated
    as a pure node (no exec pins) by the Blueprint compiler. If a node you expected to
    need sequencing has no white exec triangles, check whether the backing C++ function
    is `const` and remove it if it shouldn't be pure.
  - "Get Player Controller" (plain, index-based) doesn't reliably surface in the 5.8
    context search — use **"Get Owning Player"** instead inside a Widget Blueprint's graph.
  - When wiring `Bind Event to On X Changed`, drag off its own red "Event" pin to
    "Add Custom Event" (auto-generates a matching signature) rather than hand-building
    one — and double-check there's no duplicate/dead `Bind Event to...` node left in the
    execution chain with nothing wired to its Event pin.
  - A `Set` node for an **Array**-typed variable needs an explicit value wired in (even an
    empty `Make Array` with its default pin removed) — leaving it unconnected is a hard
    compile error ("array inputs must have an input wired into them"), unlike scalar
    types which accept a typed-in default.
  - For "do this for each of N checkboxes/widgets" UI logic (e.g. the connector floor
    panel's 20 checkboxes), a `Branch` per item that both (a) does something on True and
    (b) needs to reach the *same* next step on False requires wiring **both** the
    post-True-work output and the Branch's False output into that next step's exec input
    — a single exec pin can have multiple incoming wires, that's normal. Forgetting the
    False-path wire on **any** item (including the *last* one — it still needs to lead
    somewhere) silently breaks the whole chain whenever that box happens to be unchecked.
    Also: when copy-pasting near-identical blocks like this, explicitly double check each
    copy's literal values (e.g. the floor number in a `Contains`/`Add` node) actually got
    changed — a forgotten rename after copy/paste (all 20 checking the same number) was a
    real bug here, and it manifests as "looks right sometimes, wrong other times" rather
    than an obvious crash.
- Give complete, coherent changes rather than partial diffs described in prose.
- Warn before anything risky: deleting classes, `.Build.cs` edits, anything needing a
  Visual-Studio-project regeneration.

## If picking this up fresh

1. `git log --oneline -20` on `YQ-ui-backup` to confirm what's actually landed (this file
   might drift out of date; the commit log is ground truth).
2. Ask the user what they want to work on next — the Pending items above are the most
   recently known priorities, but confirm before assuming, especially the CI/CD and
   navigation-team check-in items, since those depend on real-world timing/conversations
   this file can't track.
3. Don't re-litigate settled design decisions documented above (vertex-only-but-anywhere-
   on-edge snapping, no overlap tolerance, InputId being auto/read-only, all-or-nothing
   connector validation, ConnectsTo shown by name not id, etc.) without a reason — they
   were each the result of a real back-and-forth with the user.
