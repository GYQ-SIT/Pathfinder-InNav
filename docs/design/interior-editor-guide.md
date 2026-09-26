# Interior Editor — How to Use It

> Current state as of 2026-09-26, branch `YQ_CUI`. The editor now has a real UMG UI
> (menu bar, dockable panels, buttons) - every action below also still has a keyboard
> shortcut, and both do the same thing under the hood.

## Getting to the interior editor

1. Launch the project, pick **New** (blank) or **Open** (load an existing
   `buildings.json`) from the start screen.
2. Click a building in the 3D exterior view to select it, then **Enter Interior**.
   Clicking empty space, or clicking the already-selected building again, deselects it.
3. You're now in the 2D floor-plan editor — a flat top-down view of the building's
   currently active floor, not a 3D camera trick. Right-click-drag to pan, mouse wheel
   to zoom. A separate **3D preview** is available too — see its own section below.
4. **Back**, at the right end of the top menu bar, returns to the exterior view (will
   prompt to save if you have unsaved changes).

## Top menu bar

Pinned across the very top of the screen.

- **File** — **New** clears everything and starts a blank project; **Load** opens the
  OS file picker for an existing `buildings.json`. Both check for unsaved changes first
  and prompt to save/discard/cancel, exactly like leaving to the exterior does.
- **Window** — a checklist of the five dockable panels (Toolbar/Floor/Properties/
  Layers/Navigation, see below). Unchecking one hides it; re-checking shows it again.
- **Back**, on the right — same as described above.

## Building switcher bar

The bar directly under the menu bar. A dropdown lists every building in the currently
loaded project — pick one to jump straight into its interior without going back to the
exterior first (switching buildings while in 3D preview exits back to 2D first). The
pencil icon next to it lets you rename the currently-loaded building in place.

Further right on the same row: **2D**/**3D** toggles the preview mode (see below);
**Reset** and the **-**/**+** move-speed buttons only appear while in 3D preview.

## Dockable panels

Five panels — **Toolbar**, **Floor**, **Properties**, **Layers**, **Navigation** — can
each be:

- **Moved**: drag by the panel's own background (not one of its buttons).
- **Resized**: drag either its left or right edge.
- **Docked together**: drop one near another's edge and they snap into a shared column,
  filling the screen height end to end; drag the thin splitter bar between two docked
  panels to adjust how much height each gets.
- **Hidden/shown**: via the Window menu (above), or Properties auto-hides its own
  content but stays visible as an empty placeholder when nothing's selected.

Panel positions/sizes/docking are saved automatically (one global layout, not per
project) whenever you save the project or leave the interior editor, and restored the
next time you open it.

### Toolbar panel

Grouped into four sections:

- **Default** — select tool (also `Q`): click a room (anywhere in its body, not just
  the outline), a door marker, or a connector marker to select it; drag to move it;
  `Backspace` to delete.
- **Draw** — draw a custom room (`P`, click points then `Enter`, `Esc` cancels), draw a
  standalone wall (`L`, same mechanic), or place a prefab rectangle/triangle/circle
  room (`1`/`2`/`3`) centered on the next click.
- **Navigate** — add-door mode (`K`, click a wall edge to cut a fixed-size door into
  it), or place a stairs/elevator/escalator connector (`4`/`5`/`6`) at the next click;
  an elevator defaults to every floor in the building, stairs/escalator default to the
  floor above and below.
- **Gizmos** — grow/shrink the selected room (same as `=`/`-`), and hold to rotate it
  counter-clockwise/clockwise around its own center (same as holding `9`/`0`).

Clicking inside the building's outline but on top of a wall, outside the outline, or
overlapping another room will fail, with the reason logged to the Output Log.

### Floor panel

Lists every floor in the building; click one to switch to it (same as `[`/`]` to cycle
up/down). **+ Add floor** / **- Remove floor** buttons do the same as `F`/`X`.

### Properties panel

Always visible. Shows an empty "Properties" placeholder when nothing's selected;
selecting a room, door, or connector fills it in:

- **Room**: a field to rename it.
- **Door**: which room it belongs to (read-only), and a dropdown to pick what it
  connects to on the other side (another room, by name, or "corridor").
- **Connector**: its type (read-only), and a checklist of floors it should reach —
  check/uncheck floors and hit Apply. Only shows checkboxes for floors that actually
  exist on the loaded building.

### Layers panel

An Outliner-style view of everything on the current floor, with a search box at the
top to filter by name. Five categories — **Boundary**, **Rooms**, **Walls**, **Path
Nodes**, **I/O Nodes** — each expandable (arrow) to list its individual items. The eye
icon on a category or an individual item toggles its visibility on the floor plan;
toggling a category affects everything in it at once.

### Navigation panel

Pick Start/Destination Room from the dropdowns (populated from the current floor's
rooms, refreshed live as rooms are added/renamed/deleted) and it marks the room's
centroid on the floor plan, or click **Set Start**/**Set Destination** and then click a
point directly on the floor plan instead - either way the button turns green once set.
**Find Route** enables once both are set on the same building and computes/draws the
route (including cross-floor connector instructions where relevant); **Clear Route**
resets both endpoints.

## 3D preview mode

The **2D**/**3D** toggle (building switcher bar, see above) switches from the 2D floor
plan to a free-fly 3D view of the current floor's real generated geometry — current
building only (others hidden), current floor's own exterior wall + floor slab only
(other floors hidden, so upper/lower floors don't block the view). View-only: no
room/wall/door editing happens in 3D, and the dock panels stay up and usable
(Floor/Navigation in particular) so you can switch floors or plan a route while
previewing.

Camera controls:

- **WASD** + **mouse-look** — free-fly, same as the pawn's own default movement.
- **Mouse wheel** — zoom (dollies along the current view direction).
- **Reset** — snaps back to the building's own default framed view, for when free-fly
  wanders somewhere awkward.
- **-** / **+** — adjust WASD move speed.

Switching floors while in 3D re-frames the camera to the default view of the new
floor. Switching buildings (via the building switcher) exits back to 2D first.

## Snapping

On by default — placing a point (`P`) or dragging a room snaps to nearby
vertices/edges of other rooms, walls, and the building's own exterior wall, within a
small tolerance. Press `N` to toggle it off/on.

## Saving

`Ctrl+S` saves the current project (and the current panel layout). If there's no file
yet (a New session), it opens the OS save dialog first. Leaving the interior back to
the exterior, or exiting to the start screen, will prompt you to save/discard/cancel if
you have unsaved changes.

## Known gaps / not built yet

- Grow/Shrink is still a fixed 20-unit nudge, not a typed Length/Width or drag-to-resize
  interaction.
- No dedicated tool for a plain path node (only elevator/stairs/escalator have a
  placement tool right now).
- 3D preview is view-only - no room/wall/door editing in 3D yet, and no "3D Preview"
  floating thumbnail overlaid on the 2D view.
