# Technical Deliverables — Ownership & Milestones

> Team **Pathfinder** · Project **InNavi**
> Owned by: **Tech Lead.** This is the `Deliverables & Ownership => M1/M2/M3` step
> of the engineering workflow. Each deliverable maps to a Jira Epic of the project **PAT** (<https://digipen-team-pathfinder.atlassian.net/jira/software/projects/PAT/list>);
> the epic of each is named under it below. Work is traced from a Jira issue to the pull request and the commits that carry its key (see "Working agreement" in `README.md`).
>
> **Status of this document:** confirmed for M1 — the Feature Plan is approved and
> platform scope is locked (see "Decisions" below); this is no longer a draft.

---

## Workflow chain

Proposal → Architecture → **Deliverables & Ownership → M1/M2/M3** → Jira → Implementation + CI/CD → GitHub/PR → Integration

## Decisions (locked for M1)

1. **App structure & platform.** Our proposal originally split an editor (desktop)
   and a navigation app (mobile). **Confirmed with the professor:** combine both into
   a single application with an Edit/Navigation mode toggle, desktop-first for M1,
   extending to mobile in M2/M3.
2. **Feature Plan.** Feature IDs (Fxx) below match the approved M1 Feature Plan (see
   `docs/planning/M1_Feature_Plan.xlsx`) — F01-F04, not placeholders.

---

## Project-specific technical deliverables (our T-table)

Each deliverable is something we build and own. "How we prove it at M1" is a concrete,
demonstrable check — not "it exists".

### T01 — Model Import & Building Selection
- **What it is:** Import the school model into UE; let the user select a building at runtime.
- **Supports:** F01 (Campus Building Selection)
- **Owner:** Yongqi
- **Jira Epic:** [PAT-26](https://digipen-team-pathfinder.atlassian.net/browse/PAT-26) Model Import & Building Selection
- **M1 target:** School model loads in UE; user can click a building and it is highlighted/selected. **Done** — real SIT Punggol campus footprints (E1–E6) load from `buildings.json`; click-to-select/highlight working.
- **M2 target:** Robust selection across all campus buildings; handle edge cases.
- **M3 target:** Polished selection UX; integrated with final flow.

### T02 — Data Input & JSON System
- **What it is:** Interface to enter/manage interior data (rooms, room types, floors, elevators, stairs, corridors); save and load as JSON.
- **Supports:** F02 (Custom Building Layout Editor)
- **Owner:** Split three ways —
  - **Yongqi**: backend/data model (C++ room/door/connector CRUD, validation, JSON
    save/load) and the panels/dialogs already in UMG (door/room/connector property
    panels, unsaved-changes confirm dialog).
  - **Padre**: the UI layer — turning the current keyboard-driven interior-editing
    tools (draw room/wall, select, add-door, place connector, etc. — see
    `docs/design/interior-editor-guide.md` for the full list) into a proper on-screen
    UMG toolbar. Everything Yongqi's side needs is already exposed
    `BlueprintCallable`.
  - **Afiq**: user input → JSON conversion (the piece that turns raw UI input into the
    JSON format `BuildingGenerator`/`InteriorGenerator` read).
- **Jira Epic:** [PAT-16](https://digipen-team-pathfinder.atlassian.net/browse/PAT-16) Data Input & JSON System
- **M1 target:** User enters a set of interior data, saves it to JSON, and loads it back to reproduce the same data. **Done (Yongqi's part)** — 2D interior floor-plan editor (rooms, walls, doors with connection properties, elevators/stairs/escalators with cross-floor data, room/door naming, snapping, boundary/overlap validation); save/load round-trips through `buildings.json`. **Padre's part (UMG toolbar) in progress and working as of 2026-09-26** — real on-screen buttons for draw room/wall, select mode, prefab shapes (rectangle/triangle/circle), add door, elevator/stairs/escalator modes, and grow/shrink/rotate gizmos, tested and confirmed functional; replaces the keyboard-only interim workflow. Now also has a top menu bar (File new/load with unsaved-changes checks; Window panel-visibility toggles), a building switcher, and eight panels (Toolbar/Floor/Properties/Layers/Navigation/Audio/Map/Debug, with an Outliner-style Layers panel and search) in a tabbed docking layout - drag a tab to join another group as a tab, split it beside/above/below one, or dock it to a screen edge; resize with the splitters; hide/show from the Window menu or a tab's close button; Reset Layout - whose arrangement persists across sessions. The exterior building-cluster screen now has a matching top menu bar too (File new/load, Window panel toggles + Reset Layout), and its Audio/Map/Debug panels use the same docking system with floating windows, with Enter Interior/Back relocated into it. Every button in the app plays a short click sound (mutable from either screen's own Audio panel), and selecting a building in the exterior view plays its own selection sound — see `docs/design/interior-editor-guide.md`. **Constraint from the professor:** the Audio panel's own file-playback feature only needs to support wav (confirmed 2026-09-29, relaxed from an earlier mp3 requirement) — the app now reads/decodes wav files itself (`WavFileLoader.h`), no third-party plugin needed; the previously-vendored `RuntimeAudioImporter` plugin (added for mp3 support) has been removed.
- **M2 target:** Import existing JSON; validation and error handling on bad input.
- **M3 target:** Polished editor UX (real UMG buttons instead of keyboard shortcuts — largely achieved already at M1); reusable across different buildings.

### T03 — Procedural Interior Generator
- **What it is:** Read JSON and spawn a schematic interior at runtime from a modular kit (walls, windows, doors, corridors, stairs, elevators). Elevators/doors recorded as input/output nodes.
- **Supports:** F03 (Instant 3D Interior Visualization)
- **Owner:** Mohamad Afiq (the owner of the epic in Jira, which is what counts)
- **Jira Epic:** [PAT-31](https://digipen-team-pathfinder.atlassian.net/browse/PAT-31) Procedural Interior Generator
- **M1 target:** Given a JSON file, generate the corresponding rooms, corridors, and stairs for at least one multi-floor building. **Done** — `AInteriorGenerator` reads the JSON and spawns real 3D procedural-mesh rooms/walls/doors per floor (tested on multi-floor buildings, e.g. a 10-floor building with rooms/doors/elevators/stairs/escalators across floors). Elevators/stairs/escalators are recorded as `FInteriorNavNode`s with a `ConnectorGroupId` tying each connector's per-floor instances together, and doors carry `InputId`/`ConnectsTo` — the input/output-node format was shared with the T04 owners (ws/jh) before implementation.
- **M2 target:** More layout rules; multiple buildings; consistency guarantees for navigation. **Status as of 2026-10-23:** **File > Generate floor** fills an empty floor with a believable layout (a core of two lifts, a stair and a toilet beside each door in the outer wall, and a ring of typed rooms with doors onto the corridor, L-shaped and corner rooms included, every room reachable); several buildings can be open in one project and linked by doors in their outer walls (main door / link door, saved in the project file). Sample projects with three buildings and generated floors are in `docs/test-data/`.
- **M3 target:** Optional Polish/Preview pass (stretch — e.g. a rotatable 3D view of the schematic interior, on top of the existing 2D editor); performance tuning. **Stretch goal substantially delivered already, as of 2026-09-27** — a "2D/3D" toggle in the interior editor switches from the 2D floor plan to a free-fly 3D view of the current floor's actual generated geometry (current building only, current floor's own exterior wall + floor slab, other floors/buildings hidden). WASD + mouse-look move the camera, scroll zooms, and Reset View/move-speed buttons compensate for it being unlocked rather than orbit-constrained. A **View**/**Edit** toggle within 3D preview now also gives 3D full parity with the 2D editor — select (with a light-green highlight), move/resize/rotate/delete a room, rotate/scale a connector, draw a new room/wall, add a door, and place a prefab room/connector, all via clicks in the 3D view — see `docs/design/interior-editor-guide.md`.

### T04 — Indoor Navigation System
- **What it is:** Runtime navigation over the generated interior, including cross-floor routing via elevators/stairs as abstract connectors, with our own planner (a visibility graph over the real geometry, searched with Dijkstra; see `architecture.md`), through any number of stops, one step at a time.
- **Supports:** F04 (Indoor Navigation System)
- **Owner:** ws / jh
- **Jira Epic:** [PAT-8](https://digipen-team-pathfinder.atlassian.net/browse/PAT-8) Navigation Logic
- **M1 target:** Compute and display a route from one room to another, including at least one cross-floor path (e.g. Level 1 → elevator → Level 8 → target room). **Confirmed working as of 2026-09-25** — `NavigationRoutePlanner` (merged to `main`) builds a visibility graph (walls, doors, room-corner turn points, building boundary, plus every `FInteriorNavNode`) directly from the real `AInteriorGenerator`/`APolyBuilding` data - not a test/placeholder graph - and runs Dijkstra's over it. Cross-floor routing works via matching elevator/stairs/escalator connector pairs (`ConnectorGroupId`). Start/destination aren't limited to room centers - any valid point inside the building, away from walls, works as an endpoint, so this already covers point-to-point navigation, not just room-to-room (no separate "PathNode" tool needed for that). Start/destination were picked by clicking the 2D floor plan at M1.
- **M2 target:** Multiple destinations; more reliable cross-floor logic; clearer route visualization. **Status as of 2026-10-23:** the found route (and Start/Destination markers) draws in the interior editor's 2D plan and 3D preview, and on the exterior 3D map. Routing now covers **a whole campus**: doors in a building's outer wall (main doors to the outside, link doors to the building next door, ws/jh) let a route go between buildings and across the open ground (`FindCampusRoute`). The Navigation panel is one shared dock panel on both screens (`NavigationDockPanelWidget`): Start and Destination can be picked from a dropdown of rooms (or, outside, of each building's main door) or by clicking the floor plan or the ground, one search box narrows both dropdowns by name/id/tag, the points and route are kept when a building is opened or left, and the route is written as numbered steps, the last one in green. See `docs/design/interior-editor-guide.md`.
- **M3 target:** Polished navigation UX; (stretch) mobile navigation experience. **Mobile, status as of 2026-10-23:** an Android phone app (Explore, Directory with map/search/route/walking/arrival, Saved, Profile; night theme, larger text, map zoom and pan) is built to the design team's prototype on made-up data behind one data interface, and a signed test build runs on a real phone (Samsung Galaxy S24+, steady 60 fps) and on emulators. Connecting it to real data is the next step: `docs/mobile-real-data-guide.md`.

---

## M2 epics in Jira

The work of Milestone 2 is in these epics of the Jira project **PAT** (each has stories with acceptance criteria, an owner, and the label `M2`):

| Epic | Owner (epic) | What it is |
|------|--------------|------------|
| [PAT-135](https://digipen-team-pathfinder.atlassian.net/browse/PAT-135) M2: CI/CD and release build | Yongqi | Build, static analysis and tests on every push and pull request; lint; tag-triggered release build |
| [PAT-139](https://digipen-team-pathfinder.atlassian.net/browse/PAT-139) M2: Performance measurement | Yongqi | Budgets and measured numbers, frame-rate statistics, the graph cache, the readings on the demo machine |
| [PAT-145](https://digipen-team-pathfinder.atlassian.net/browse/PAT-145) M2: Data format, schemas and documentation | Yongqi | Project file format and schemas, prototype data out of the code, architecture v1.0 |
| [PAT-149](https://digipen-team-pathfinder.atlassian.net/browse/PAT-149) M2: Layout validator plugin | Ong Wei Song | The versioned plugin package and its tool registry |
| [PAT-153](https://digipen-team-pathfinder.atlassian.net/browse/PAT-153) M2: Multi-stop routes and the navigation panel | Ong Wei Song | Routes through stops, step by step, the places list with Plan and Go |
| [PAT-157](https://digipen-team-pathfinder.atlassian.net/browse/PAT-157) M2: Isolate 3D view | Ong Wei Song | Look at a building on its own and move and turn it |
| [PAT-160](https://digipen-team-pathfinder.atlassian.net/browse/PAT-160) M2: Phone app on real data | Ong Wei Song | The real data source, the signed Android build, the frame rate on a device |


## Required of every team (graded — nothing to invent)

These are standard deliverables required of all teams. Owners TBD.

| # | Deliverable | M1 evidence | Status (2026-10-23) |
|---|-------------|-------------|---|
| R01 | GitHub repository & workflow (branches, PRs, clean structure) | Repo organized; PR-based workflow in use | Not reviewed this pass |
| R02 | Jira project & backlog (Epics → Stories/Tasks with owners) | Epics for T01–T04 with stories and assignees | Not reviewed this pass |
| R03 | Architecture (system design + tech stack) | `docs/architecture.md` with diagram | Exists; **v1.0 (2026-10-28)** adds the layers (UI, application, services, data access, stored data), the boundary between them, the delivery pipeline and the measured numbers, with an owner for each component; v0.2 (2026-10-23) brought it in line with what is built (own route planner instead of a NavMesh, the dock UI, doors between buildings, the phone app) |
| R04 | Automated build (CI) | Build runs automatically on push | Each push to `main` runs the Windows editor build, MSVC static analysis and Unreal tests on the self-hosted runner. Pull requests run lint and JSON validation on GitHub-hosted runners. The team12 VM publishes the wiki, and pushing a release tag runs packaging and the Windows smoke test. |
| R05 | Automated checks / tests | At least basic checks run in CI | The Windows workflow runs all `InNavi.*` automation tests and fails if any fail or fewer than 150 complete. It retains the report and performance measurements as artifacts. See `docs/performance.md` for budgets and measurements. |
| R06 | Reproducible dev environment | README lets a teammate clone, set up, and run | `README.md` has real build/run/test steps, how to try the phone app, and the repository layout; `docs/test-data/` has a project to try doors and campus routes with |
| R07 | Working technical proof of concept | Core risks demonstrated running (see T01–T04) | T01–T04 all demonstrated (T04 confirmed 2026-09-25) |
| R08 | Basic error handling | App does not crash on bad/empty input | Interior editor validates most inputs (bounds/overlap/wall-collision checks, unsaved-changes prompts) - not audited team-wide |
| R09 | Working product demonstration | 2–3 min demo: select → input → generate → navigate | Not yet assembled (navigate step depends on T04) |
| R10 | Project documentation | `docs/` kept current | Updated 2026-10-23 (`README.md`, `architecture.md` v0.2, `deliverables.md`, `docs/design/interior-editor-guide.md` and `ui-panels-guide.md`, and the phone app's `mobile-real-data-guide.md`, `mobile-app-data-interface.md`, `android-install.md`, `android-signing.md`). The editor guide covers the menu bar, building switcher, dockable panels, Gizmos, the 2D/3D preview toggle, its View/Edit sub-mode, 3D route visualization, the exterior screen's own menu bar, and the Audio panel/click-sound/selection-sound system) |
| R11 | Individual contribution evidence | Each member records a short (<5 min) contribution video | Not reviewed this pass |

**CI/CD note (updated 2026-10-23):** the team12 DigiPen-provided VM (2 vCPU/4GB RAM,
Ubuntu 24.04) is confirmed too underpowered/wrong-platform to compile this UE5 Windows
project (no Windows toolchain, likely not enough RAM even if it had one), so
the compile step runs on a self-hosted runner on a capable Windows PC
(`.github/workflows/build-windows.yml` builds `InNaviEditor` on pushes to `main`).
The check waits in the queue while that PC (or its runner) is off. The release workflow verifies that the milestone tag points to a commit on `main` before the Windows runner builds that exact commit. Run manual releases from `main`. Restrict who can create or move release tags and who can run jobs on the persistent Windows runner, since GitHub can load older workflow definitions from tags pointing to older commits.
The VM runs a registered self-hosted Actions runner
(`team12-ovh-vm`, installed as a systemd service) for wiki publishing from `main`.
JSON validation runs on a GitHub-hosted runner for data changes on `main` and pull requests. VM access is
via a shared `team12` SSH login (see `FullStack_OVH_team12.pdf` - team-shared per DigiPen,
not to be shared outside the team) using key-based auth.

---

## Optional Tech Items (TI-xx) under consideration

Bonus only, never penalized. **Do not double-count** with the T-table above — an item
already contracted as a Txx cannot also be claimed as a TI.

- **TI-25 3D Asset Pipeline** — automated import/conversion of the modular kit / school model.
- **TI-02 Cross-platform Architecture / TI-47 Multi-platform Build** — if we extend to mobile in M2/M3.
- **TI-50 Mapping/Navigation** — only if navigation is *not* already contracted as T04. _(Likely N/A, since T04 covers it.)_

> Claim at most 4 TIs total, at most 2 of them SUBSTANTIAL. Decide these once the
> T-table is locked, to avoid double-counting.
