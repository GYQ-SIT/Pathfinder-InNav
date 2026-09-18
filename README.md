# Pathfinder — Indoor Navigation System Application

Team **Pathfinder** · Project **InNavi** · CSD3401 / UXG3400 (Fall 2026)

A standalone Unreal Engine application that adds an interior layer to the
school's 3D model: select a building, enter or import interior data (JSON),
generate a schematic interior in 3D, and navigate inside — including across floors.

## Repository layout

- `README.md` — this file
- `.gitignore` — UnrealEngine template
- `InNavi/` — the Unreal Engine project (open `InNavi/InNavi.uproject`)
- `docs/`
  - `proposal.md` — project proposal
  - `design/`
    - `product-design.md` — product design document
    - `building-generation-ui-spec.md` — exterior building tools, C++ interface for UI
    - `interior-editor-guide.md` — **how to use the interior editor** (controls/workflow, current as of the `YQ-ui-backup` branch)
    - `ui-ux/` — wireframes, user flows, mockups
  - `architecture.md` — system architecture + tech stack
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
3. Hit **Play** in the Editor. The building/interior data it loads from lives at
   `InNavi/Data/buildings.json`.

Most of the current interior-editing functionality (rooms, doors, elevators/stairs/
escalators, snapping, save/load) is only reachable via keyboard shortcuts right now —
see `docs/design/interior-editor-guide.md` for the full control list and workflow.

## Supported platforms

Development and build should target:
- Windows 11
- Ubuntu 24.04
- macOS Tahoe

## Team

See `docs/design/product-design.md` for the team roster and roles.
