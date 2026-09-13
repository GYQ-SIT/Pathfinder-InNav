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
- **Supports:** _(building selection / interior generation — Fxx TBD)_
- **Owner:** _Import & Selection Dev (TBD)_
- **Jira Epic:** `INNAVI-T01`
- **M1 target:** School model loads in UE; user can click a building and it is highlighted/selected.
- **M2 target:** Robust selection across all campus buildings; handle edge cases.
- **M3 target:** Polished selection UX; integrated with final flow.

### T02 — Data Input & JSON System
- **What it is:** UMG interface to enter/manage interior data (rooms, room types, floors, elevators, stairs, corridors); save and load as JSON.
- **Supports:** _(data entry / save-load — Fxx TBD)_
- **Owner:** _Editor UI / Data Dev (TBD)_
- **Jira Epic:** `INNAVI-T02`
- **M1 target:** User enters a set of interior data, saves it to JSON, and loads it back to reproduce the same data.
- **M2 target:** Import existing JSON; validation and error handling on bad input.
- **M3 target:** Polished editor UX; reusable across different buildings.

### T03 — Procedural Interior Generator
- **What it is:** Read JSON and spawn a schematic interior at runtime from a modular kit (walls, windows, doors, corridors, stairs, elevators). Elevators/doors recorded as input/output nodes.
- **Supports:** _(interior generation — Fxx TBD)_
- **Owner:** _Generation Dev (TBD)_
- **Jira Epic:** `INNAVI-T03`
- **M1 target:** Given a JSON file, generate the corresponding rooms, corridors, and stairs for at least one multi-floor building.
- **M2 target:** More layout rules; multiple buildings; consistency guarantees for navigation.
- **M3 target:** Optional Polish/Preview pass (stretch); performance tuning.

### T04 — Indoor Navigation System
- **What it is:** Runtime navigation over the generated interior, including cross-floor routing via elevators/stairs as abstract connectors (runtime NavMesh + NavLinks).
- **Supports:** _(navigation / routing — Fxx TBD)_
- **Owner:** _Navigation Dev (TBD)_
- **Jira Epic:** `INNAVI-T04`
- **M1 target:** Compute and display a route from one room to another, including at least one cross-floor path (e.g. Level 1 → elevator → Level 8 → target room).
- **M2 target:** Multiple destinations; more reliable cross-floor logic; clearer route visualization.
- **M3 target:** Polished navigation UX; (stretch) mobile navigation experience.

---

## Required of every team (graded — nothing to invent)

These are standard deliverables required of all teams. Owners TBD.

| # | Deliverable | M1 evidence |
|---|-------------|-------------|
| R01 | GitHub repository & workflow (branches, PRs, clean structure) | Repo organized; PR-based workflow in use |
| R02 | Jira project & backlog (Epics → Stories/Tasks with owners) | Epics for T01–T04 with stories and assignees |
| R03 | Architecture v0.1 (system design + tech stack) | `docs/architecture.md` with diagram |
| R04 | Automated build (CI) | Build runs automatically on push |
| R05 | Automated checks / tests | At least basic checks run in CI |
| R06 | Reproducible dev environment | README lets a teammate clone, set up, and run |
| R07 | Working technical proof of concept | Core risks demonstrated running (see T01–T04) |
| R08 | Basic error handling | App does not crash on bad/empty input |
| R09 | Working product demonstration | 2–3 min demo: select → input → generate → navigate |
| R10 | Project documentation | `docs/` kept current |
| R11 | Individual contribution evidence | Each member records a short (<5 min) contribution video |

---

## Optional Tech Items (TI-xx) under consideration

Bonus only, never penalized. **Do not double-count** with the T-table above — an item
already contracted as a Txx cannot also be claimed as a TI.

- **TI-25 3D Asset Pipeline** — automated import/conversion of the modular kit / school model.
- **TI-02 Cross-platform Architecture / TI-47 Multi-platform Build** — if we extend to mobile in M2/M3.
- **TI-50 Mapping/Navigation** — only if navigation is *not* already contracted as T04. _(Likely N/A, since T04 covers it.)_

> Claim at most 4 TIs total, at most 2 of them SUBSTANTIAL. Decide these once the
> T-table is locked, to avoid double-counting.
