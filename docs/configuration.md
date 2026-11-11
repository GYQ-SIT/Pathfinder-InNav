# Configuration

> Team **Pathfinder** · Project **InNavi** · Guidelines B5, E5 (M2 Technical Guidelines)

Everything that can differ from one machine or one run to the next is listed here, with where it is set. **Nothing below is written in the
source code**: `scripts/lint.py` fails the build when a URL, an address, a key or a password is found in `InNavi/Source/`.

## Why there is no `.env.example`

InNavi is a desktop application and a phone app that read their data in one process. There is no server, no database login, no port and no API
key in the product, so there is nothing for an `.env` file to hold, and none is used (`.env` is ignored by git all the same, and `lint.py`
rejects one that is committed). What can be set is below. The only secrets the team has are the **Android signing key** and the **tokens of the
CI**, and both are kept outside the repository.

## The application's own settings

| Setting | Where it is set | Default | What it does |
|---------|-----------------|---------|--------------|
| Data folder | `InNavi/Data/` (next to the exe in a package) | - | The campus database `map_extended.db`, the click sounds in `Audio/`, the default dock layouts in `DefaultLayouts/`, and the phone prototype's made-up data in `Prototype/mobile-prototype.json` (it says `"synthetic": true`) |
| Project files | the project library lists the `.json` projects in `InNavi/Data/` (a deleted one is moved to `Data/Deleted/`); File > Load opens any file | - | The versioned JSON of a project (`version` field; see `BuildingGenerator`) |
| Saved layouts and settings | `InNavi/Saved/` (never committed) | the files in `Data/DefaultLayouts/` | Where each screen's dock layout, the editor's settings and the phone app's local projects (`Saved/MobileProjects/`) are kept |
| Dock panels added by a plugin | `[InNavi.ToolPanels]` in `InNavi/Config/DefaultGame.ini` | `Validation=/Script/InNavi.LayoutValidationPanelWidget` | Which widget class shows a tool's panel, found through the plugin's tool registry |
| What is cooked and packaged | `[/Script/UnrealEd.ProjectPackagingSettings]` in `DefaultGame.ini` | `/Game/ImageAssets` always cooked, `../Data` staged | So the icons and the data folder are in a package |
| UI scale | `UIScaleRule=Custom` and `CustomScalingRuleClass` in `InNavi/Config/DefaultEngine.ini`; `InNaviDPIScalingRule` | Desktop: `0.77 * min(width / 1920, height / 1080)` | Fits the desktop UI to the window, including live resizing, ultrawide and portrait windows. Keeps the existing look at 1920x1080 and doubles it at 4K. Android and `-MobileApp` retain 0.77 and their own phone layout. |
| Input | `InNavi/Config/DefaultInput.ini` | the click action and the keys | The mouse and keyboard mappings |

## Command-line flags (the phone app and its test runs)

| Flag | What it does |
|------|--------------|
| `-MobileApp` | starts the phone app's screens instead of the editor (portrait; use with `-ResX=375 -ResY=812`) |
| `-MobileMock` | the phone app uses the prototype's made-up data instead of the project data |
| `-MobileProject=<file>` | the phone app reads this project file (takes precedence over the editor's session and the shipped projects) |
| `-MobileTab=`, `-MobileState=`, `-MobileQuery=`, `-MobileOpen=`, `-MobileSave=`, `-MobileBrowse`, `-MobileFilters`, `-MobileOverview`, `-MobileMapLevel=`, `-MobileLevelList`, `-Mobile3D`, `-MobileSnap=` | start a phone screen in a given state (used by the screenshots and the tests) |

The phone app is described in `mobile-real-data-guide.md`.

## Build and delivery (CI/CD)

| Setting | Where it is set | What it is |
|---------|-----------------|------------|
| `GITHUB_TOKEN` | provided by GitHub to every workflow | read and write access to this repository's releases and packages, for that run only |
| `NODE_AUTH_TOKEN` | the Windows build workflows set it to `secrets.GITHUB_TOKEN` | lets `npm ci` read the private package `@gyq-sit/innavi-layout-validator` |
| `UE_ROOT` | environment of the machine that packages (optional) | the Unreal Engine 5.8 folder; `scripts/package-windows.ps1` defaults to `C:\Program Files\Epic Games\UE_5.8` |
| `WIKI_DIR` | `.github/workflows/deploy-wiki.yml` | where on the team12 VM the wiki is published (`/var/www/innavi-wiki`), served by nginx on port 5000 (the VM allows only 22 and 5000-5050) |
| Runner labels | `runs-on:` of each workflow | `[self-hosted, windows]` (main-branch build, analysis, tests, release), `[self-hosted, linux, ovh]` (wiki deploy), `ubuntu-latest` (PR lint and JSON checks, plugin publish) |
| Plugin version | `package.json` (published pin) and `InNaviLayoutValidator.uplugin` (checked-in source) | The installer requires matching versions. Windows CI allows newer source before publication. |

The wiki deploy runner needs write access to the parent of the web root so it can stage a sibling directory. The Linux filesystem must support atomic directory exchange via `renameat2`. The running wiki stays in place if staging or exchange fails.

If branch protection requires the Windows workflow on pull requests, update the required checks to match the hosted PR workflows or provide an isolated Unreal runner for PR builds.

## Secrets, and where they are

| Secret | Where it is kept | Never |
|--------|------------------|-------|
| Android signing key and its passwords | the tech lead's `Documents/InNavSigning/`, backed up in a password manager (`android-signing.md`) | in the repository, in chat, in tickets, in the wiki. `*.jks`, `*.keystore`, `*.p12` are ignored by git and `lint.py` fails on one |
| `GITHUB_TOKEN` | created by GitHub for each run | in a log or a file |
| Login of the team12 VM | the team's shared sheet from the course | in the repository |

The repository is **private**. Do not make it public, and do not copy a key into a file in it: if one is ever committed, treat it as
public, replace it, and rewrite the history.
