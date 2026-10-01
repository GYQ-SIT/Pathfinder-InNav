# Pathfinder — Indoor Navigation System Application

Team **Pathfinder** · Project **InNavi** · CSD3401 / UXG3400 (Fall 2026)

A standalone Unreal Engine application that adds an interior layer to the
school's 3D model: select a building (the campus itself loads from a SQL-backed
database), enter or import interior data (JSON), generate a schematic interior
in 3D, and navigate inside — including across floors.

## Repository layout

- `README.md` — this file
- `.gitignore` — UnrealEngine template
- `InNavi/` — the Unreal Engine project (open `InNavi/InNavi.uproject`)
- `docs/`
  - `proposal.md` — project proposal
  - `design/`
    - `product-design.md` — product design document
    - `interior-editor-ui-spec.md` — interior editing tools, C++ interface for UI (written for Padre)
    - `interior-editor-guide.md` — **how to use the interior editor** (controls/workflow, current as of the `YQ-ui-backup` branch)
    - `ui-ux/` — wireframes, user flows, mockups
  - `architecture.md` — system architecture + tech stack
  - `toolchain-decision.md` — stack/platform decisions and why
  - `deliverables.md` — ownership + M1/M2/M3 targets
  - `planning/` — Feature Plan + Technical Deliverable Plan

## Prerequisites

- Unreal Engine 5.8
- Visual Studio 2022 with the "Game development with C++" workload
- Git

## Getting started

​```bash
git clone https://github.com/GYQ-SIT/Pathfinder-InNav.git
cd Pathfinder-InNav
​```

1. Right-click `InNavi/InNavi.uproject` → **Generate Visual Studio project files** (first
   time only, or after pulling a change that adds/removes a C++ class).
2. Open the generated `.sln` in Visual Studio and build (or just double-click the
   `.uproject` to open directly in the Unreal Editor, which will offer to build for you).
3. Hit **Play** in the Editor. Sample data lives under `InNavi/Data/`: the campus/
   exterior layer loads from `map_extended.db` (SQL-backed), and interior building
   layouts save/load as JSON (e.g. `buildings.json`).

The interior editor has a full on-screen UI — a dockable Toolbar/Floor/Properties/
Layers/Navigation/Audio panel layout, a top menu bar (File, Window), and a 2D/3D
preview toggle with its own View/Edit sub-mode — see
`docs/design/interior-editor-guide.md` for the full control list and workflow.

## Supported platforms

Windows 11 only, in practice — this is a UE5 project built and packaged on
Windows; the team's Linux CI runner has no Windows toolchain to compile it
(see the CI/CD note in `docs/deliverables.md`), and macOS has not been set up
or tested.

## Team

See `docs/design/product-design.md` for the team roster and roles.
