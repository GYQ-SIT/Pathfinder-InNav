# Pathfinder — Indoor Navigation System Application

Team **Pathfinder** · Project **InNavi** · CSD3401 / UXG3400 (Fall 2026)

A standalone Unreal Engine application that adds an interior layer to the
school's 3D model: select a building (the campus itself loads from a SQL-backed
database), enter or import interior data (JSON), generate a schematic interior
in 3D, and navigate — inside a building, across floors, between buildings and
across the open ground between them. It also has an Android phone app (a test
build, on made-up data for now).

## Repository layout

- `README.md` — this file
- `Readme.txt` — the readme that went into the Milestone 1 submission zip (kept as it was handed in)
- `.gitignore` — UnrealEngine template, plus signing keys, packaging output and personal test files
- `.github/workflows/` — CI: `build-windows.yml` (compiles the editor on a self-hosted Windows runner),
  `validate-json.yml` (every `InNavi/Data/*.json` parses), `deploy-wiki.yml` (publishes `docs/`)
- `InNavi/` — the Unreal Engine project (open `InNavi/InNavi.uproject`)
  - `Source/InNavi/` — all the C++. The phone app is the files starting with `Mobile`
  - `Data/` — sample data, audio, default dock layouts
- `docs/`
  - `proposal.md` — project proposal
  - `architecture.md` — system architecture + tech stack
  - `toolchain-decision.md` — stack/platform decisions and why
  - `deliverables.md` — ownership + M1/M2/M3 targets
  - `design/`
    - `product-design.md` — product design document
    - `interior-editor-guide.md` — **how to use the editor** (both screens: controls, panels, doors, floor generator, 3D, navigation)
    - `ui-panels-guide.md` — how the dock panels work and how to add one
    - `interior-editor-ui-spec.md` — the original C++ interface spec for the editor's UI (written for Padre)
    - `mobile-app-data-interface.md` — the phone app's screens and the data interface they use
    - `ui-ux/` — wireframes, user flows, mockups
  - `mobile-real-data-guide.md` — **how to connect the phone app to real data** (read this before working on it)
  - `android-install.md` — installing the phone app's test build on an Android phone
  - `android-signing.md` — building and signing the APK
  - `test-data/` — project files for trying doors and campus routes (three buildings, A and B next to each other, C far away)
  - `planning/` — Feature Plan + Technical Deliverable Plan

## Prerequisites

- Unreal Engine 5.8
- Visual Studio 2022 with the "Game development with C++" workload
- Git
- For the phone app only: the Android SDK/NDK that Unreal's Turnkey installs (see `docs/android-signing.md`)

## Getting started

```bash
git clone https://github.com/GYQ-SIT/Pathfinder-InNav.git
cd Pathfinder-InNav
```

1. Right-click `InNavi/InNavi.uproject` → **Generate Visual Studio project files** (first
   time only, or after pulling a change that adds/removes a C++ class).
2. Open the generated `.sln` in Visual Studio and build (or just double-click the
   `.uproject` to open directly in the Unreal Editor, which will offer to build for you).
   Close the editor before building from outside it: Unreal cannot build while it is open.
3. Hit **Play** in the Editor. Sample data lives under `InNavi/Data/`: the campus/
   exterior layer loads from `map_extended.db` (SQL-backed), and interior building
   layouts save/load as JSON (e.g. `buildings.json`). For several buildings with doors
   and generated floors, **File > Load** one of the files in `docs/test-data/`.

The editor has a full on-screen UI on both its screens (the campus and a building's
interior) — a dockable layout of Toolbar / Floor / Properties / Layers / Navigation /
Audio / Map / Debug panels, a top menu bar (File, Window), a 2D/3D preview toggle with its
own View/Edit sub-mode, and a Navigation panel that picks Start and Destination (a room, a
building's main door, or a click on the floor plan or the ground) and finds the route.
See `docs/design/interior-editor-guide.md` for the full control list and workflow.

**Window > Validation** opens the read-only layout validator on either screen. It checks
data, geometry, doors, connectors and room reachability, with clickable findings and JSON
report export. Findings never block editing or saving. See
[`docs/design/layout-validator.md`](docs/design/layout-validator.md) for usage and headless file validation.

## Running the tests

About 120 automation tests (`InNavi.*`). With the editor closed:

```bash
UnrealEditor-Cmd.exe InNavi/InNavi.uproject -ExecCmds="Automation RunTests InNavi; Quit" -unattended -nullrhi -nosplash
```

(`UnrealEditor-Cmd.exe` is in `Engine/Binaries/Win64` of the Unreal install.) A part can be run on its own, e.g. `InNavi.Mobile` or
`InNavi.Navigation`. Windows CI runs the layout validator, navigation, docking and UI guard tests.

## The phone app

An Android app (portrait) with Explore, Directory (map, search, route, walking, arrival), Saved and Profile tabs, built to the design
team's prototype. It runs on **made-up data** behind one interface; connecting the real data is described in
`docs/mobile-real-data-guide.md`. To try it on the desktop, in a phone-sized window:

```bash
UnrealEditor.exe InNavi/InNavi.uproject -game -windowed -ResX=375 -ResY=812 -MobileApp
```

To try it on a phone, install the test build from the repository's **Releases** page by following `docs/android-install.md`.

## Supported platforms

The editor and the desktop app: **Windows 11 only**, in practice — this is a UE5 project built and packaged on
Windows; the team's Linux VM has no Windows toolchain to compile it (see the CI/CD note in `docs/deliverables.md`), and macOS has
not been set up or tested. The phone app: **Android 8 or newer, 64-bit ARM** (and an x86_64 build for PC emulators); there is no iPhone version.

## Team

See `docs/design/product-design.md` for the team roster and roles.
