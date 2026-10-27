# Layout validator: M2 compliance evidence

Source: `TechGuidelines_Parminder.xlsx`, sheet **M2 Guidelines**, rows **21–25** (D1–D5), and **M2 Technical Deliverables**, row **17** (Packages/plugins). These are assessment criteria; this document records implementation evidence.

| Criterion | Evidence |
| --- | --- |
| D1 — Must: reusable component packaged and installed by the product | Standard `InNaviLayoutValidator.uplugin` Runtime module, enabled in `InNavi.uproject` and linked by `InNavi.Build.cs`. CI installs the exact source package into `InNavi/Plugins` before Unreal builds. Generic JSON validation has no dependency on the product's actors or UnrealEd. |
| D2 — Should: own repository for independent ownership, release schedule or multiple products | Same team, product and release schedule; source stays in the main repository under the stated single-product exception. The standalone example demonstrates reuse and is not a second product. Move to a separate repository if those conditions change. |
| D3 — Must: semantic version, CHANGELOG, install/use README and working example | Descriptor and npm package use `1.0.0`; package-local README/CHANGELOG and a standalone Unreal consumer with valid/invalid example JSON. The commandlet validates and exports reports without loading InNavi game code. |
| D4 — Should: interface/registry plus a feature supplied as a plugin | `LayoutToolRegistry.h`: public `IProvider`, `FDescriptor` and lifecycle-aware `FRegistry`. `LayoutToolPanel.h`: UI adapter interface. Validator module registers its own tool. Shared `PluginToolHost` discovers menus/panels/docking in both editors; automation adds an additional provider without changing either editor. |
| D5 — Should: registry publication and exact-version consumer pin | GitHub Packages npm source distribution `@gyq-sit/innavi-layout-validator`, release workflow `publish-layout-validator.yml`, exact dependency `1.0.0`, integrity lockfile, installer and CI package read/install/verification. Publication must succeed before this row can be considered fulfilled; a workflow file alone is insufficient. |

The package's UI remains advisory: users review findings and fix issues with existing editing tools. Validation does not force repairs or prevent saving. CI/headless commands can return a failing exit status for invalid test input.

## Reproduce

1. Authenticate to the private package using a token with package read access, run `npm ci --ignore-scripts`, then `npm run install:plugins` and `node scripts/install-layout-validator.mjs --check`.
2. Build `InNaviEditor` and `InNavi` for Win64 Development using Unreal 5.8.
3. Run automation with `-ExecCmds="Automation RunTests InNavi,Automation Quit"`; reports appear in the specified `-ReportExportPath`. `InNavi.LayoutValidation.PluginDiscovery` exercises duplicate-ID rejection, lifecycle cleanup, both editor menus/panels and layout persistence.
4. Follow the package's `Examples/README.md` to build the standalone consumer and run its valid and invalid samples. Expected exit statuses are 0 and 1, respectively.

Verified locally on 2026-10-27: Win64 Editor and standalone game builds; all 129 InNavi automation tests passed (69 success, 60 success with existing fixture warnings, zero failed). The standalone consumer built successfully; its valid/invalid samples exited 0/1 and both input hashes stayed unchanged.
