# Pathfinder — Indoor Navigation System Application

Team **Pathfinder** · Project **InNavi** · CSD3401 / UXG3400 (Fall 2026)

A standalone Unreal Engine application that adds an interior layer to the
school's 3D model: select a building (the campus itself loads from a SQL-backed
database), enter or import interior data (JSON), generate a schematic interior
in 3D, and navigate — inside a building, across floors, between buildings and
across the open ground between them. It also has an Android phone app (a test
build, reading project data and routes through a mobile data adapter).

## Repository layout

- `README.md` — this file
- `.gitignore` — UnrealEngine template, plus signing keys, packaging output and personal test files
- `.github/workflows/` — CI: `build-windows.yml` (builds the editor from a clean checkout, runs the MSVC static analysis and every
  automation test, on a self-hosted Windows runner), `lint.yml` (JSON, secrets, configuration, headers and document links; GitHub-hosted), `release-windows.yml` (a pushed tag builds the Windows
  package and publishes it as a release),
  `validate-json.yml` (every `InNavi/Data/*.json` parses), `deploy-wiki.yml` (publishes `docs/`)
- `scripts/` — `lint.py` (the repository checks of `lint.yml`), `check-analysis.ps1` and `check-test-report.ps1` (turn the analysis log and
  the test report into a pass or a fail), `package-windows.ps1`, `smoke-test-package.ps1` and `publish-release.ps1` (the release build,
  see Releases below), `install-layout-validator.mjs` (installs the plugin package)
- `InNavi/` — the Unreal Engine project (open `InNavi/InNavi.uproject`)
  - `Source/InNavi/` — all the C++. The phone app is the files starting with `Mobile`
  - `Data/` — sample data, audio, default dock layouts
- `docs/`
  - `proposal.md` — project proposal
  - `architecture.md` — system architecture v1.0: the layers, the boundaries, the tech stack, the delivery pipeline, who owns what
  - `configuration.md` — every setting (files, flags, CI), where it is set, and where the secrets are kept
  - `performance.md` — the performance budgets and the measured numbers
  - `toolchain-decision.md` — stack/platform decisions and why
  - `deliverables.md` — ownership + M1/M2/M3 targets
  - `design/`
    - `interior-editor-guide.md` — **how to use the editor** (both screens: controls, panels, doors, floor generator, 3D, navigation)
    - `ui-panels-guide.md` — how the dock panels work and how to add one
    - `mobile-app-data-interface.md` — the phone app's screens and the data interface they use
  - `mobile-real-data-guide.md` — **how to connect the phone app to real data** (read this before working on it)
  - `android-install.md` — installing the phone app's test build on an Android phone
  - `android-signing.md` — building and signing the APK
  - `test-data/` — a project file for trying doors and campus routes (three buildings, A and B next to each other, C far away; the tests use it too)
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
   and generated floors, **File > Load** `docs/test-data/entrance-test-2-with-doors.json`.

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
The reusable plugin is versioned as `1.0.0` and distributed through GitHub Packages. See its
[package README](InNavi/Plugins/InNaviLayoutValidator/README.md) for exact-version installation,
and the [M2 evidence](docs/design/layout-validator-m2-compliance.md) for the D1–D5 mapping.

## Running the tests

About 165 automation tests (`InNavi.*`), including the measurements of `InNavi.Performance` (see
[`docs/performance.md`](docs/performance.md) for the budgets and the numbers). With the editor closed:

```bash
UnrealEditor-Cmd.exe InNavi/InNavi.uproject -ExecCmds="Automation RunTests InNavi; Quit" -unattended -nullrhi -nosplash
```

(`UnrealEditor-Cmd.exe` is in `Engine/Binaries/Win64` of the Unreal install.) A part can be run on its own, e.g. `InNavi.Mobile` or
`InNavi.Navigation`. Windows CI runs **all** of them, and fails the pull request when one fails (the editor exits 0 either way, so
`scripts/check-test-report.ps1` reads the report). Before a pull request, `python scripts/lint.py` runs the repository checks of `lint.yml`
on your own machine. The report and the performance measurements of every CI run are kept with it as a downloadable artifact.

## Releases

A Windows build is published from a tag, not by hand. Tag a commit of `main` that passed CI and push the tag:

```bash
git tag m2-build-1
git push origin m2-build-1
```

`release-windows.yml` then packages the build on the Windows runner (`scripts/package-windows.ps1`: cook, stage, pak, clean, zip), starts
the package once without a window to see that it opens and closes (`scripts/smoke-test-package.ps1`), and attaches
`InNavi-<tag>.zip` to a GitHub release marked as a test build (`scripts/publish-release.ps1`). Tags look like `m<milestone>-build-<n>`.
The zip holds `InNavi.exe`, the campus database, the default layouts, `SampleProjects/` to try File > Load with, and `BUILD-INFO.txt`
(tag, commit, date). The same package can be made on your own machine with
`powershell -File scripts\package-windows.ps1 -Name InNavi-test -OutputDir C:\Temp\release` (editor closed). The Android phone app is
built and signed separately (`docs/android-signing.md`).

## Performance budget

Desktop 60 FPS or more, phone 30 FPS or more; the campus loads in under 3 s, an interior opens in under 0.5 s, a route between buildings
takes under 1 s (p95). The measured numbers, the machine they were taken on, and the one place we are above the target (a route inside
the largest, ten-floor building takes about 6 s) are in [`docs/performance.md`](docs/performance.md). The Debug panel (Window > Debug) shows
the frame rate while you use the app: the average, the 1 % low and the slowest frame of the last ten seconds.

## The phone app

An Android app (portrait) with Explore, Directory (map, search, route, walking, arrival), Saved and Profile tabs, built to the design
team's prototype. New builds use **real project data and routes** through `IMobileNavSource`; `-MobileMock` keeps the prototype available. Data selection, Android packaging and testing are described in
`docs/mobile-real-data-guide.md`. To try it on the desktop, in a phone-sized window:

```bash
UnrealEditor.exe InNavi/InNavi.uproject -game -windowed -ResX=375 -ResY=812 -MobileApp
```

To try it on a phone, install the test build from the repository's **Releases** page by following `docs/android-install.md`.

## Supported platforms

The editor and the desktop app: **Windows 11 only**, in practice — this is a UE5 project built and packaged on
Windows; the team's Linux VM has no Windows toolchain to compile it (see the CI/CD note in `docs/deliverables.md`), and macOS has
not been set up or tested. The phone app: **Android 8 or newer, 64-bit ARM** (and an x86_64 build for PC emulators); there is no iPhone version.

## Documentation (the team wiki)

The documents of `docs/` are published as the team wiki, a MkDocs site rebuilt by `deploy-wiki.yml` on every push to `main` that changes
them: **http://51.79.240.111:5000/** (the team12 VM of the course). The
wiki and this repository say the same thing: `docs/` is the one source, and the wiki is only how it is read. Start with `architecture.md`.

## Team

Who owns which component is in `docs/architecture.md` (section 8) and `docs/deliverables.md`.
