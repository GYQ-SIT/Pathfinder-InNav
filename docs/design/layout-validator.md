# Layout validator

The **InNavi Layout Validator** plugin checks project data and navigation without changing the layout. All findings are advisory in the application: saving and editing remain available. There are no automatic fixes.

## Using the panel

1. Open a project and choose **Window > Validation**, on either the exterior or interior screen. The panel starts hidden and uses the usual docking, resizing, floating and saved-layout controls.
2. Choose **Whole campus** or **Current building**, then **Validate layout**. On the exterior screen, Current building means the selected building; inside, it means the open building. Apply pending Properties fields before validating.
3. Review the counts and use the severity filter. Issues are sorted by severity, building and floor. Selecting **Show on plan** opens the relevant building/floor and selects the affected room, opening, entrance or connector. Navigation issues also display a yellow location ring.
4. Correct issues with the existing editing tools and validate again. Changes to geometry, names, doors, floors or connectors (including undo/redo) mark the report stale. Stale reports cannot navigate to old objects or be exported.
5. **Export report** writes a uniquely named JSON report under `InNavi/Saved/Validation/`. The report includes rule codes, object locations, suggestions, the checked snapshot fingerprint and navigation coverage.

Validation reads live, applied, unsaved edits in the open building and stored interior data in other buildings. It does not open every building, sync interior data, save project files, clear selections, or add undo steps. Explicitly choosing Show on plan uses the normal building-switch path, which preserves unsaved edits in memory.

If opening a building materialises defaults from an older file, Show on plan refreshes the report for the resulting live layout. A view change alone therefore does not leave the report incorrectly marked as an author edit.

## Checks and interpretation

- Data types, coordinate pairs, scale, dimensions and unique identifiers in their existing scopes.
- Closed polygon degeneracy/self-intersection and containment inside the building. Two-point open wall paths are supported. Suspicious room overlaps are warnings because intentional overlap is allowed.
- Opening edge, width and offset bounds in **world centimetres**; overlapping openings; stale door metadata. `corridor` and `outside` are supported special values. Door metadata warnings are distinct from geometry reachability.
- Floor levels and cross-floor connector instances, grouping, placement and reciprocal served-floor lists. Legacy connectors without group IDs or served-floor lists retain the planner's fallback behaviour. Single-floor connectors are allowed.
- Main entrance placement and link references. Link entrances are stored once; their mirrors are derived. Navigation diagnostics check the same cross-building doorway edges as normal routing, including distance and floor elevation.
- Room reachability from at least one campus exterior entry, using one valid representative destination per closed room. Candidate points come from triangulation and must meet the planner's wall clearance. A room with no suitable candidate is **skipped with a warning**, rather than labelled unreachable.

Data checks run before navigation checks. Data/geometry errors skip navigation for that run, preventing misleading cascades. Whole campus scope reveals errors in dependencies outside the selected building. Unauthored interiors are informational. With no usable exterior entry points, entrance reachability is explicitly not checked.

The panel checks the **current loaded layout**. To diagnose malformed or duplicate raw file entries before loading can skip them, use the file commandlet below. Validation is a model consistency check, not physical accessibility or fire-safety certification. A reachable representative point does not prove every point of a room is reachable.

## Headless file validation

After building `InNaviEditor`, run this in PowerShell from the repository root:

```powershell
$project = (Resolve-Path 'InNavi/InNavi.uproject').Path
$inputFile = (Resolve-Path 'docs/test-data/entrance-test-2-with-doors.json').Path
$reportFile = Join-Path (Split-Path $project) 'Saved/Validation/campus-report.json'
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' $project -run=InNaviValidateLayout "-Input=$inputFile" "-Report=$reportFile" -unattended -nullrhi -nosplash
```

Input JSON is checked before loading. If structural checks pass, routing runs in a disposable world. The input is never saved. Exit status is **0** for no errors, **1** for validation errors, and **2** for invocation/output failures. Add `-FailOnWarnings` to make warnings fail CI as well. The commandlet rejects a report path equal to its input path.

## Implementation

- `InNavi/Plugins/InNaviLayoutValidator/`: versioned Runtime module with JSON rules, report types and a public tool registry. It depends on Core/CoreUObject/Json, without dependencies on game actors or UnrealEd.
- `LayoutValidationSession`: controller-owned report shared by both screens; detached snapshots, scope, stale detection, focus and report export.
- `LayoutValidationPanelWidget`: shared dock content. Its one-second UI poll only checks snapshot fingerprints; it never reruns validation. Fingerprinting stops after a report becomes stale.
- `NavigationRoutePlanner`: ordinary routing and validation share `BuildCampusGraph`. Validation builds the graph once per run and traverses it from exterior entries. Geometry and graph generation run synchronously on demand on the game thread.
- `InNaviValidationTools`: Editor-only commandlet module. It is excluded from packaged games; the plugin and panel use Runtime code.

The plugin is enabled and linked by the project. `LayoutTools::IProvider` and `FRegistry` expose tools; `ILayoutToolPanel` defines their UI adapter contract. `PluginToolHost` discovers tools in both screens and handles their menus, widgets, sizing and dock defaults. The validator uses this registration path. New plugin tools provide their own descriptors and adapter classes without editing either screen or dock-layout switches. InNavi selects its validation adapter through `[InNavi.ToolPanels]` in `DefaultGame.ini`. Module registration happens at startup; reopen screens/application after enabling an extension.

The package-local [README](../../InNavi/Plugins/InNaviLayoutValidator/README.md), [CHANGELOG](../../InNavi/Plugins/InNaviLayoutValidator/CHANGELOG.md) and [standalone example](../../InNavi/Plugins/InNaviLayoutValidator/Examples/README.md) cover installation, API use, extension and release procedures. The root npm manifest pins the source package to `1.0.0`; CI installs from GitHub Packages and verifies that the offline source mirror matches. npm distributes the source archive; UnrealBuildTool compiles the native Unreal plugin.

See [M2 compliance evidence](layout-validator-m2-compliance.md) for the D1–D5 mapping.

## Verification

Run `InNavi.LayoutValidation`, `InNavi.Navigation`, `InNavi.UI.Guard` and `InNavi.Dock` automation tests. The Windows build workflow runs these checks; the lightweight Linux JSON-syntax workflow remains available independently.
