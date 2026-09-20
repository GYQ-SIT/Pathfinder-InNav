# Interior Editor — How to Use It

> Current state as of 2026-09-18, branch `YQ-ui-backup`. Everything below is
> keyboard-driven for now (real UMG buttons/toolbar are a later pass — this is the
> functional layer underneath them). If you just want to click around and see what's
> been built, this is the doc to read.

## Getting to the interior editor

1. Launch the project, pick **New** (blank) or **Open** (load an existing
   `buildings.json`) from the start screen.
2. Click a building in the 3D exterior view to select it, then **Enter Interior**.
3. You're now in the 2D floor-plan editor — a flat top-down view of the building's
   currently active floor, not a 3D camera trick. Right-click-drag to pan, mouse wheel
   to zoom.
4. **Back** button returns to the exterior view (will prompt to save if you have
   unsaved changes).

## Floors

| Key | Action |
|---|---|
| `[` / `]` | Cycle active floor down / up |
| `F` | Add a floor |
| `X` | Remove the active floor |

## Drawing / placing things

Only one of these can be active at a time — turning one on turns any other off.

| Key | Mode |
|---|---|
| `P` | Draw a custom room (click points, `Enter` to close the shape, `Esc` to cancel) |
| `L` | Draw a standalone wall (same click mechanic, stays open — not a room, can't take a door) |
| `1` / `2` / `3` | Place a prefab room (rectangle / triangle / circle) centered on the next click |
| `K` | Add-door mode — click a wall edge to cut a fixed-size door into it |
| `4` / `5` / `6` | Place a connector (elevator / stairs / escalator) at the next click — an elevator defaults to every floor in the building, stairs/escalator default to the floor above and below |

Clicking inside the building's outline but on top of a wall, outside the outline, or
overlapping another room will fail, with the reason logged to the Output Log.

## Select mode

| Key | Action |
|---|---|
| `Q` | Toggle select mode |
| *(in select mode)* click | Select a room (anywhere in its body, not just the outline), a door marker, or a connector marker |
| drag | Move the selected room, or slide the selected door along its wall |
| `Backspace` | Delete whatever's selected |
| `=` / `-` | Grow / shrink the selected room (test-only, not the final resize UX) |

Selecting a **door** or a **connector** pops up a small properties panel:

- **Door panel**: shows which room it belongs to (read-only), and a dropdown to pick
  what it connects to on the other side (another room, by name, or "corridor").
- **Connector panel**: shows its type (read-only), and a checklist of floors it should
  reach — check/uncheck floors and hit Apply. Only shows checkboxes for floors that
  actually exist on the loaded building (fixed 2026-09-20).

Selecting a **room** pops up a panel to rename it.

## Snapping

On by default — placing a point (`P`) or dragging a room snaps to nearby
vertices/edges of other rooms, walls, and the building's own exterior wall, within a
small tolerance. Press `N` to toggle it off/on.

## Saving

`Ctrl+S` saves the current project. If there's no file yet (a New session), it opens
the OS save dialog first. Leaving the interior back to the exterior, or exiting to the
start screen, will prompt you to save/discard/cancel if you have unsaved changes.

## Known gaps / not built yet

- No real UMG toolbar — everything above is a keyboard shortcut.
- No dedicated tool for a plain path node (only elevator/stairs/escalator have a
  placement tool right now).
- No 3D preview/walkthrough mode — this is a 2D top-down authoring view only.
