# UI Panels — How They Work and How to Add One

> State as of 2026-10-28. Applies to both editor screens: the
> interior editor (`UInteriorScreenWidget`) and the exterior building-selection screen
> (`UEditScreenWidget`).

## The one rule

**Every panel is a dock panel.** A panel is anything the user could reasonably want to
move, resize, hide or park next to something else: tool palettes, lists, property
editors, filters, audio, debug output. All of them are registered with the screen's dock
host, so they all share the same behaviour for free:

- tabs, splits, docking to the screen edges, floating windows,
- the **Window** menu check box that shows or hides them,
- **Window > Reset Layout**,
- the saved arrangement (`Saved/InteriorDockLayout.json`, `Saved/ExteriorDockLayout.json`).

A panel must **never** be added straight onto a screen's root canvas.

## What is *not* a panel (chrome)

A few things stay fixed on purpose. They are called *chrome* and are the only things
besides the dock host allowed on a root canvas:

| Name on the canvas | What it is |
|---|---|
| `Background`, `FloorPlanWidget` | the 2D plan and the backdrop behind it (the central canvas) |
| `Pnl_MenuBar`, `Pnl_BuildingSwitcherBar` | the fixed bars along the top |
| `Pnl_FileDropdown`, `Pnl_WindowDropdown`, `Pnl_CameraDropdown`, `MenuClickCatcher` | the menus that open from those bars, and the layer that closes them on a click elsewhere |
| `Txt_InspectionHint` | temporary instructions and the hidden/active floor status for Isolate & Rotate; stays with the central viewport while tool panels move |
| `Img_InspectionPivot` | projects the building's bottom anchor into the viewport so it remains visible through the active upper floor; follows the model instead of a dock panel |
| `Chrome_*` | decoration, such as divider lines |
| `DockHost` | the dock system itself |

The guard tests (`InNavi.UI.Guard.*`, [UiGuardTests.cpp](../../InNavi/Source/InNavi/UiGuardTests.cpp))
fail if anything else appears on a root canvas. If that happens, make your widget a dock
panel. Only add a name to the whitelist if the element truly must stay fixed, and say why in
the comment above the list.

## How the pieces fit

```
UInteriorScreenWidget / UEditScreenWidget      the screen
 └─ UDockHostWidget  (DockHost)                wraps SDockHost, owns the FDockLayout
     └─ panel content, one per registered id   what you write
```

- `FDockLayout` ([DockLayoutModel.h](../../InNavi/Source/InNavi/DockLayoutModel.h)) is the pure
  model: tab groups, splits, edge areas, floating windows. It knows nothing about UMG and
  serialises to JSON.
- `SDockHost` / `UDockHostWidget` ([DockHostWidget.h](../../InNavi/Source/InNavi/DockHostWidget.h),
  [DockHostUMG.h](../../InNavi/Source/InNavi/DockHostUMG.h)) draw the layout and handle dragging.
  A panel's content is parented by the dock host, **not** by the screen's widget tree.
- `InteriorDock::` and `ExteriorDock::` ([InteriorDockLayout.h](../../InNavi/Source/InNavi/InteriorDockLayout.h),
  [ExteriorDockLayout.h](../../InNavi/Source/InNavi/ExteriorDockLayout.h)) list each screen's
  panel ids, their default arrangement, and load/save the layout file.

Because the dock host places the content, the screen's own lookups (`GetWidgetFromName`)
do not find controls inside a panel. Use `FindScreenWidget(Name)` on either screen: it also
searches the shared panels' own widget trees. Never save a dock layout against a guessed
canvas size — extents are stored as fractions of the real size.

## Shared panels

Audio, Map, Debug and Navigation exist on both screens. Each is written once, as a subclass of
`UDockPanelContentWidget` ([DockPanelContent.h](../../InNavi/Source/InNavi/DockPanelContent.h)):

| Panel | Class | Screen-specific part |
|---|---|---|
| Audio | `UAudioPanelWidget` | none; the screen passes the width with `SetContentWidth` |
| Map | `UMapPanelWidget` | `SetShowBuildingFilters(true)` on the exterior screen turns the Buildings section on |
| Debug | `UDebugPanelWidget` | `SetInfoProvider(lambda)` supplies the screen's own lines; the panel adds FPS and the last failed action |
| Navigation | `UNavigationDockPanelWidget` | holds no state (that is `UNavigationPanelWidget`, `ANavPlayerController::GetNavigationPanel()`); the screen calls `RefreshDisplay()` on every `OnStateChanged` and `RefreshRoomDropdowns()` when the places could have changed, passes the width with `SetContentWidth`, and tells the state whether it is outdoors with `SetOutdoorMode` (exterior screen: true) |

If a panel will appear on only one screen, it can still be written the same way — a
`UDockPanelContentWidget` subclass — and that is the recommended form for new panels.

## Adding a new panel

1. **Write the content.** Subclass `UDockPanelContentWidget`, build the tree in
   `RebuildWidget()` (only when `WidgetTree->RootWidget` is still empty), and use the base
   helpers: `GetController()` for the player controller, `ApplyStandardFont`, `AddCheckRow`,
   and `PlayButtonClickSound` for every button. Give every control a stable name
   (`Btn_…`, `Chk_…`, `Txt_…`, `Cbx_…`) so tests can find it. Make the root a transparent
   `UBorder` with 14 px padding; the dock frame paints the background.
2. **Name it.** Add an id next to the others in `InteriorDock` and/or `ExteriorDock`:
   declare and define the `FName`, add it to `AllPanels()`, and give it a side in
   `DefaultSideFor`. Add it to `BuildDefaultLayout` (and mark it hidden by default there if it
   should start hidden: place it, then `HidePanel` it so it remembers its spot).
   The interior screen also keeps its own `EDockPanelId` enum: add the entry there and a case in
   `GetDockPanelName` so the two stay in step.
3. **Create and register it** in the screen's `RebuildWidget()`:
   ```cpp
   MyPanelWidget = CreateWidget<UMyPanelWidget>(this, UMyPanelWidget::StaticClass());
   MyPanelWidget->SetOwnerScreen(this);
   DockHost->RegisterPanel(ExteriorDock::My, FText::FromName(ExteriorDock::My), MyPanelWidget);
   ```
   Keep the pointer in a `UPROPERTY()` member. Do not call `AddChildToCanvas`.
4. **Add the Window menu row.** One check box in the Window dropdown, whose handler calls the
   screen's `SetDockPanelHidden` (exterior) or `SetDockPanelUserHidden` (interior), and a line
   in `RefreshWindowMenuChecks` so the tick follows the real state (a tab's X or a re-dock
   changes it behind the menu's back).
5. **Follow the width.** The dock group can be any width, so size the panel's wide controls
   from it: `DockHost->GetPanelWidth(Id)` minus 48 (14 px padding on each side plus the scroll
   bar), passed to the panel's `SetContentWidth`, from the screen's tick. Labels and button
   text use `SetAutoWrapText(true)` so they wrap instead of running out of the panel.
6. **Test it.** Add a test next to `InNavi.UI.Panels.*` in
   [UiPanelTests.cpp](../../InNavi/Source/InNavi/UiPanelTests.cpp) that creates the screen(s),
   finds your controls through `FindScreenWidget` and checks the behaviour. Extend the layout
   tests if you changed the default arrangement. Run the whole suite:
   `UnrealEditor-Cmd.exe InNavi.uproject -ExecCmds="Automation RunTests InNavi; Quit" -unattended -nullrhi -nosplash`.
7. **Document it** in [interior-editor-guide.md](interior-editor-guide.md) (what the panel
   does and where it starts).

Things that do not need doing: dragging, resizing, tabs, floating, saving and restoring, and
the saved-layout migration for older files — `PlaceMissingPanels` puts a panel an old file
does not mention at its default spot.

## Pitfalls

- A panel placed on the canvas (even "temporarily") fails the guard tests.
- A file-scope `using namespace` in a `.cpp` leaks into other files in a unity build; wrap
  test code in its own namespace.
- Rebuilds fail with "Unable to build while Live Coding is active" while the editor is open.
- Saving a dock layout from a screen that was never laid out (a headless test) writes it against a made-up canvas size, which
  once turned a floating window into one 50 times the screen. Each screen's `SaveDockLayoutToDisk` therefore saves only against a
  real (or last known) canvas size, and loading caps a window at the canvas size. Check that `Saved/*DockLayout.json` is
  unchanged after running the tests.
- A Debug-style panel that reads the controller must tolerate running before the screen has a
  controller (the base class `GetController()` falls back to the owning screen's player).

## Control styling

Every control takes its look from `StartScreenStyle.h` (namespace `StartStyle`), never from the
engine default grey. Use `PrimaryButton` / `SecondaryButton` / `StatusButton(colour)` for
buttons, `MenuButton` (menu-bar text buttons) and `MenuRowButton` (boxed rows inside a menu's
dropdown), `MenuPanel` for the dropdown itself, `Field` for text boxes, `DarkCombo`,
`DarkCheckBox` and `DarkSlider`. After `SetStyle`, call `SetBackgroundColor(White)` so the
button's own tint does not darken the style. The dock chrome (tabs, panel body, splitters,
floating frames) takes its colours from the constants at the top of `DockHostWidget.cpp`.
