# InNavi Layout Validator 1.0.0

A reusable Unreal Engine 5.8 Runtime plugin for advisory validation of indoor-navigation layout JSON. It does not modify input, force fixes or block saving. The package contains source, not platform binaries. UnrealBuildTool compiles it for the consuming target.

## Install the pinned package

The source package is `@gyq-sit/innavi-layout-validator` on GitHub Packages. The product pins **exactly `1.0.0`** in its root `package.json`; `package-lock.json` pins the resolved archive and integrity hash. The checked-in Unreal plugin is an offline source mirror, verified against the installed package in CI.

Use Node.js 22+ and a GitHub token with `read:packages` and access to this private repository/package. Set `NODE_AUTH_TOKEN` in your shell or CI secret store; never put a token in `.npmrc` or Git. GitHub Actions uses the repository's `GITHUB_TOKEN` with `packages: read`.

```powershell
npm ci --ignore-scripts
npm run install:plugins
node scripts/install-layout-validator.mjs --check
```

Run installation before opening Unreal. The installer replaces package-owned source/docs and preserves local Binaries/Intermediate. To check local changes without replacing them, run only `--check`. Commit intended package changes and release a new version before updating the consumer pin; installation replaces local edits in those package-owned paths.

In another Unreal product, place the installed plugin in `<Project>/Plugins/InNaviLayoutValidator`, enable it in `.uproject`, and add `InNaviLayoutValidator` to the consuming module's dependencies. No game-module dependency is needed by this plugin. See [the working example](Examples/README.md).

## Call the API

```cpp
#include "LayoutValidation.h"
auto Report = LayoutValidation::ValidateFile(InputPath);
if (!LayoutValidation::ExportReport(Report, OutputPath)) { /* handle output failure */ }
const bool bInvalid = Report.HasErrors();
```

`ValidateProject` accepts a detached `FJsonObject`. Rules check structure, identities, polygons, openings, connectors and entrance references. `FReport` holds stable rule codes, severity, object paths/IDs, suggestions and a fingerprint. Read-only validation alone does not calculate physical route reachability: InNavi's `LayoutValidationSession` adds diagnostics using its existing navigation planner. Export to a different path from the input.

## Extend the editors with another plugin tool

1. A new Unreal plugin's Runtime module depends on `InNaviLayoutValidator`, `CoreUObject`, `Engine` and `UMG` when it supplies UI.
2. Implement `LayoutTools::IProvider::DescribeTool()` with a unique stable `Id`, localized `Title`, supported screens, default side/visibility and a native widget class path. IDs must not collide with built-in editor panels. Keep descriptor identity and capabilities stable while registered.
3. Register the provider during `StartupModule` using `LayoutTools::FRegistry::Get().Register(*this)`; check its returned value outside `check()` so registration also runs in Shipping. Unregister in `ShutdownModule`. Registration and host access run on the game thread. Load the module before constructing screens; enabling/disabling modules requires reopening the editor screen/application.
4. Supply a concrete `UUserWidget` implementing `ILayoutToolPanel`. Initialize owner/context in `InitializeToolPanel`, and handle resizing in `SetToolPanelWidth`. `FocusCompleted` is for tools that explicitly focus an interior object. Tools that do not navigate can ignore it.

Both product screens enumerate the registry. Their Window menus, widget creation, docking defaults, lookup, resizing and layout restoration automatically include the new tool; no screen or dock-layout switch needs editing. Widget classes can be provided directly in descriptors, or overridden per product in `DefaultGame.ini`:

```ini
[InNavi.ToolPanels]
Validation=/Script/InNavi.LayoutValidationPanelWidget
```

The validator registers the first real tool through this interface. Its product adapter reads live actors and implements plan focus. Generic data validation remains reusable without this UI. Native classes are linked into their modules; Blueprint adapter assets, if used by extensions, must be explicitly included in cooking. Invalid adapters are logged and their menu entries disabled.

## Release and ownership

The plugin has the same team, consumer and release schedule as InNavi, so source remains in the product repository under D2's single-product exception. The standalone example is a demonstration, not a separately maintained product. If independent ownership, release scheduling or another real product is introduced, move the package to its own repository and preserve its public API/version history.

Update the descriptor's `VersionName`, integer `Version`, npm version and CHANGELOG together. Validate the editor, game target and standalone example before tagging `innavi-layout-validator-v<version>`. `.github/workflows/publish-layout-validator.yml` publishes that immutable version to GitHub Packages and produces a consumer lockfile artifact. Update the product's exact dependency and lockfile for each release. Consumers use `npm ci`, not an unpinned Git branch.

Product usage, validation limits and headless routing checks are documented in `docs/design/layout-validator.md` in the repository.
