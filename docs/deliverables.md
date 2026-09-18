# Technical Deliverables — Ownership & Milestones

> Team **Pathfinder** · Project **InNavi**
> Owned by: **Tech Lead.** This is the `Deliverables & Ownership => M1/M2/M3` step
> of the engineering workflow. Each deliverable maps to a Jira Epic.
>
> **Status of this document:** draft.
> - `Supports (Fxx)` references are placeholders until the Feature Plan is approved.
> - Owners are role placeholders until the team assigns members.
> - Platform scope (single desktop app vs. desktop + mobile) is **pending confirmation with the professor** — see note under "Open decisions".

---

## Workflow chain

Proposal → Architecture → **Deliverables & Ownership → M1/M2/M3** → Jira → Implementation + CI/CD → GitHub/PR → Integration

## Open decisions (to confirm before locking scope)

1. **App structure & platform.** Our proposal originally split an editor (desktop)
   and a navigation app (mobile). We are proposing to **combine both into a single
   application with an Edit/Navigation mode toggle**, and to **target desktop first
   for M1**, extending to mobile in M2/M3. Pending confirmation with the professor.
2. **Feature Plan.** Feature IDs (Fxx) below are placeholders; they will be filled
   once the PM's Feature Plan is approved and F↔T mapping is agreed.

---

## Project-specific technical deliverables (our T-table)

Each deliverable is something we build and own. "How we prove it at M1" is a concrete,
demonstrable check — not "it exists".

### T01 — Model Import & Building Selection
- **What it is:** Import the school model into UE; let the user select a building at runtime.
- **Supports:** _(building selection / interior generation — F01 TBD)_
- **Owner:** Yongqi
- **Jira Epic:** `INNAVI-T01`
- **M1 target:** School model loads in UE; user can click a building and it is highlighted/selected. **Done** — real SIT Punggol campus footprints (E1–E6) load from `buildings.json`; click-to-select/highlight working.
- **M2 target:** Robust selection across all campus buildings; handle edge cases.
- **M3 target:** Polished selection UX; integrated with final flow.

### T02 — Data Input & JSON System
- **What it is:** Interface to enter/manage interior data (rooms, room types, floors, elevators, stairs, corridors); save and load as JSON.
- **Supports:** _(data entry / save-load — F02 TBD)_
- **Owner:** Split three ways —
  - **Yongqi**: backend/data model (C++ room/door/connector CRUD, validation, JSON
    save/load) and the panels/dialogs already in UMG (door/room/connector property
    panels, unsaved-changes confirm dialog).
  - **Padre**: the UI layer — turning the current keyboard-driven interior-editing
    tools (draw room/wall, select, add-door, place connector, etc. — see
    `docs/design/interior-editor-guide.md` for the full list) into a proper on-screen
    UMG toolbar. Everything Yongqi's side needs is already exposed
    `BlueprintCallable` (see `docs/design/building-generation-ui-spec.md` for the
    exterior-side integration pattern to follow for the interior tools too).
  - **Afiq**: user input → JSON conversion (the piece that turns raw UI input into the
    JSON format `BuildingGenerator`/`InteriorGenerator` read).
- **Jira Epic:** `INNAVI-T02`
- **M1 target:** User enters a set of interior data, saves it to JSON, and loads it back to reproduce the same data. **Done (Yongqi's part)** — 2D interior floor-plan editor (rooms, walls, doors with connection properties, elevators/stairs/escalators with cross-floor data, room/door naming, snapping, boundary/overlap validation); save/load round-trips through `buildings.json`. **Still keyboard-driven, not a full UMG toolbar** — that's Padre's part of T02, status not confirmed by this pass.
- **M2 target:** Import existing JSON; validation and error handling on bad input.
- **M3 target:** Polished editor UX (real UMG buttons instead of keyboard shortcuts); reusable across different buildings.

### T03 — Procedural Interior Generator
- **What it is:** Read JSON and spawn a schematic interior at runtime from a modular kit (walls, windows, doors, corridors, stairs, elevators). Elevators/doors recorded as input/output nodes.
- **Supports:** _(interior generation — F03 TBD)_
- **Owner:** Yongqi
- **Jira Epic:** `INNAVI-T03`
- **M1 target:** Given a JSON file, generate the corresponding rooms, corridors, and stairs for at least one multi-floor building. **Done** — `AInteriorGenerator` reads the JSON and spawns real 3D procedural-mesh rooms/walls/doors per floor (tested on multi-floor buildings, e.g. a 10-floor building with rooms/doors/elevators/stairs/escalators across floors). Elevators/stairs/escalators are recorded as `FInteriorNavNode`s with a `ConnectorGroupId` tying each connector's per-floor instances together, and doors carry `InputId`/`ConnectsTo` — the input/output-node format was shared with the T04 owners (ws/jh) before implementation.
- **M2 target:** More layout rules; multiple buildings; consistency guarantees for navigation.
- **M3 target:** Optional Polish/Preview pass (stretch — e.g. a rotatable 3D view of the schematic interior, on top of the existing 2D editor); performance tuning.

### T04 — Indoor Navigation System
- **What it is:** Runtime navigation over the generated interior, including cross-floor routing via elevators/stairs as abstract connectors (runtime NavMesh + NavLinks).
- **Supports:** _(navigation / routing — F04 TBD)_
- **Owner:** ws / jh
- **Jira Epic:** `INNAVI-T04`
- **M1 target:** Compute and display a route from one room to another, including at least one cross-floor path (e.g. Level 1 → elevator → Level 8 → target room). **Status unconfirmed as of 2026-09-18** — last known update was prototyping independently on test boxes; not yet confirmed whether it consumes the real `FInteriorNavNode`/room/door data from T02/T03. Needs a check-in with ws/jh.
- **M2 target:** Multiple destinations; more reliable cross-floor logic; clearer route visualization.
- **M3 target:** Polished navigation UX; (stretch) mobile navigation experience.

---

## Required of every team (graded — nothing to invent)

These are standard deliverables required of all teams. Owners TBD.

| # | Deliverable | M1 evidence | Status (2026-09-18) |
|---|-------------|-------------|---|
| R01 | GitHub repository & workflow (branches, PRs, clean structure) | Repo organized; PR-based workflow in use | Not reviewed this pass |
| R02 | Jira project & backlog (Epics → Stories/Tasks with owners) | Epics for T01–T04 with stories and assignees | Not reviewed this pass |
| R03 | Architecture v0.1 (system design + tech stack) | `docs/architecture.md` with diagram | Exists; content still broadly accurate |
| R04 | Automated build (CI) | Build runs automatically on push | Not started. See "CI/CD" note below |
| R05 | Automated checks / tests | At least basic checks run in CI | Not started (blocked on R04) |
| R06 | Reproducible dev environment | README lets a teammate clone, set up, and run | `README.md` now has real build/run steps (was "TBD") |
| R07 | Working technical proof of concept | Core risks demonstrated running (see T01–T04) | T01–T03 demonstrated; T04 (navigation) unconfirmed |
| R08 | Basic error handling | App does not crash on bad/empty input | Interior editor validates most inputs (bounds/overlap/wall-collision checks, unsaved-changes prompts) - not audited team-wide |
| R09 | Working product demonstration | 2–3 min demo: select → input → generate → navigate | Not yet assembled (navigate step depends on T04) |
| R10 | Project documentation | `docs/` kept current | Updated this pass (`architecture.md`, `deliverables.md`, root `HANDOFF.md`, new `docs/design/interior-editor-guide.md`) |
| R11 | Individual contribution evidence | Each member records a short (<5 min) contribution video | Not reviewed this pass |

**CI/CD note:** team12 has a DigiPen-provided VM intended for a self-hosted GitHub Actions
runner, but it's assessed as too underpowered (2 vCPU/4GB RAM, Linux) to compile this
UE5 Windows project — plan is to use a self-hosted runner on a teammate's own capable
Windows PC instead, triggered on push to `main`/`YQ-ui-backup`. Not yet built; needs an
owner assigned.

---

## Optional Tech Items (TI-xx) under consideration

Bonus only, never penalized. **Do not double-count** with the T-table above — an item
already contracted as a Txx cannot also be claimed as a TI.

- **TI-25 3D Asset Pipeline** — automated import/conversion of the modular kit / school model.
- **TI-02 Cross-platform Architecture / TI-47 Multi-platform Build** — if we extend to mobile in M2/M3.
- **TI-50 Mapping/Navigation** — only if navigation is *not* already contracted as T04. _(Likely N/A, since T04 covers it.)_

> Claim at most 4 TIs total, at most 2 of them SUBSTANTIAL. Decide these once the
> T-table is locked, to avoid double-counting.
