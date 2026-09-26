# Interior Editor — How to Use It

> Current state as of 2026-09-26, branch `YQ_CUI`. The editor now has a real UMG UI
> (menu bar, dockable panels, buttons) - every action below also still has a keyboard
> shortcut, and both do the same thing under the hood.

## Getting to the interior editor

1. Launch the project, pick **New** (blank) or **Open** (load an existing
   `buildings.json`) from the start screen.
2. Click a building in the 3D exterior view to select it, then **Enter Interior**.
3. You're now in the 2D floor-plan editor — a flat top-down view of the building's
   currently active floor, not a 3D camera trick. Right-click-drag to pan, mouse wheel
   to zoom.
4. The exit-door icon at the right end of the top menu bar returns to the exterior view
   (will prompt to save if you have unsaved changes).

## Top menu bar

Pinned across the very top of the screen.

- **File** — **New** clears everything and starts a blank project; **Load** opens the
  OS file picker for an existing `buildings.json`. Both check for unsaved changes first
  and prompt to save/discard/cancel, exactly like leaving to the exterior does.
- **Window** — a checklist of the four dockable panels (Toolbar/Floor/Properties/
  Layers, see below). Unchecking one hides it; re-checking shows it again.
- The exit-door icon on the right — same as described above.

## Building switcher bar

The bar directly under the menu bar. A dropdown lists every building in the currently
loaded project — pick one to jump straight into its interior without going back to the
exterior first. The pencil icon next to it lets you rename the currently-loaded
building in place.

## Dockable panels

Four panels — **Toolbar**, **Floor**, **Properties**, **Layers** — can each be:

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
- No 3D preview/walkthrough mode — this is a 2D top-down authoring view only.
