# Interior Editor — How to Use It

> Current state as of 2026-09-30, branch `YQ_CUI`. The editor now has a real UMG UI
> (menu bar, dockable panels, buttons) - every action below also still has a keyboard
> shortcut, and both do the same thing under the hood.

## Getting to the interior editor

1. Launch the project, pick **New project** (blank), **Open project** (the project library, below), or **Quit**, from
   the start screen.
   **New project** asks for a **Project name** (required) and an optional **Description** first. Create writes a blank
   `<project name>.json` into the `InNavi/Data` folder (characters Windows does not allow in a file name are replaced;
   an existing project is never overwritten - you are asked for another name) and opens the editor on it, so **Ctrl+S**
   saves straight to that file. The name and description are kept in the file (`projectName`, `description`).
2. Click a building in the 3D exterior view to select it (plays a short selection
   sound) - its details appear in the exterior screen's **Building** panel, with **Open 2D floor
   editor** (see below). Clicking empty space, or clicking the already-selected building again,
   deselects it.
3. You're now in the 2D floor-plan editor — a flat top-down view of the building's
   currently active floor, not a 3D camera trick. Right-click-drag to pan, mouse wheel
   to zoom. A separate **3D preview** is available too — see its own section below.
4. **Back**, at the right end of the top menu bar, returns to the exterior view (will
   prompt to save if you have unsaved changes).

### Project library (Open project)

**Open project** shows every project in the `InNavi/Data` folder as a card, newest first - `.json` projects and `.db` map
databases; other JSON files in the folder (layouts, settings) are left out. There is no file dialog. The search box filters by
project or file name, **+ New project** starts a new one, and **Back** returns to the start screen.

Each card shows the number of buildings (and an **Empty** tag when there are none yet), the project's name, its file name, its
description and when the file was last written, with:

- **Open project** - loads it and opens the editor. A project with no buildings yet opens like any other.
- **Duplicate** - copies a `.json` project to "<name> copy" (then "copy 2", ...) with that name inside; the original is untouched.
- **Rename** - changes the project's name and its file name together; a name another project already has is refused.
- **Delete** (the trash can) - asks first, then moves the file into `Data/Deleted/` (a name already there gets a time stamp), so
  nothing is destroyed: put the file back into `Data` by hand to restore it. The keyboard focus starts on Cancel, so Enter never
  deletes.

The sample files that ship with the project (`buildings.json`, `map_extended.db`) cannot be renamed or deleted, because tests and
defaults refer to them by name; a database cannot be duplicated. A database card's building count is the number of campus buildings
(tagged `university`) in it.

## Exterior edit screen

The 3D building-cluster view (before entering any building's interior) has its own top
menu bar, same look as the interior editor's:

- **File** — **New**/**Load**, same behavior and unsaved-changes check as the interior
  editor's own File menu below.
- **Window** — **Audio** (see Audio panel below), **Map** (see Map panel below) and
  **Debug**, plus **Reset Layout**.
- **Camera** — the free-fly camera's controls, in a dropdown: **Reset**, the typed move
  speed (Enter or clicking away commits it), **+** / **-** to nudge it, and **Set Default**
  (see below). The menu stays open after +/- and closes after Reset/Set Default.
- **Save** — same as `Ctrl+S`, left of File.
- **Back** on the far right — returns to the start screen (not to be confused
  with the interior editor's own Back, which returns to *this* screen instead). The way into a
  building is the **Building** panel's buttons, below.

Camera: WASD + mouse-look to free-fly, scroll to zoom; the **Camera** menu's **-**/**+**
and speed box adjust move speed. **Reset** snaps the camera back to a framed view of every
loaded building; **Set Default** stores the camera's *current* position/angle as that reset target
instead (persisted to disk, so it survives closing and reopening the project) — for
tuning what "Reset" snaps back to once you've found an angle you like.

This screen's four panels — **Map** (shown by default, docked on the left), **Building**
(shown by default, docked on the right), **Audio** and **Debug** (hidden by default, tabs of
one group along the bottom) — use the same docking
system as the interior editor (see **Dockable panels** below: tabs, splits, screen edges,
floating windows, Reset Layout). Its arrangement is saved separately, in
`Saved/ExteriorDockLayout.json`, when you save or leave the screen. The **Debug** panel
shows the selected building, camera speed, unsaved-changes flag, FPS and frame time, and a
**Last Error** line for the most recent failed action (yellow: rejected, nothing lost; red:
data may have been lost) - File > New/Load failures used to be silent on this screen.

> Audio, Map and Debug are the same widgets on this screen and in the interior editor; how panels are built and added is in [UI Panels Guide](ui-panels-guide.md).

### Building panel (the selected building)

Hidden or shown via Window > Building. With nothing selected it asks for a selection. For the
selected building (picked in the Map panel's list or by clicking it in the scene) it shows:

- its **outline** (the footprint, drawn as a line drawing that fits the panel), its name and
  **Building ID**;
- **Floors**, **Rooms** (all floors) and **Lifts & stairs** (elevators, stairs and escalators; a
  link that reaches several floors counts once). Rooms and links come from the interior data
  saved in the project, so edits made in the interior editor show here once they are saved;
- **STATUS**: an amber **Empty** tag while the building has no rooms, a green **Has rooms** tag
  once it does;
- **Open 2D floor editor** - opens the building in the interior editor's 2D floor plan - and
  **Open 3D building view** - opens it straight in the interior editor's 3D preview.

### Map panel (roads, POIs, buildings)

Hidden or shown via Window > Map. Only appears once a database (`.db`) has been loaded
(roads/POIs/context buildings only come from that loading path - a plain
`buildings.json` project has nothing for this panel to filter).

**Exterior screen: site overview.** The panel is laid out like the design team's "Site overview" page:

- **Choose a building** - a search box (building name or id) and the list of buildings that pass the filters, one row each
  with its name and floor count, and a count ("29 / 29 editable buildings"). Clicking a row selects that building and
  frames it with the camera; the selected building's row is highlighted in blue (also when it was picked in the 3D scene).
  Buildings that do not match are hidden and cannot be clicked. The list scrolls inside the panel when it is long.
- **FILTERS** (collapsed; click the header) - **Floors**: Any, 1 floor, 2-5 or 6+. **Show**: **Campus** shows/hides the
  real, university-tagged (or adjacency-linked, see below) campus buildings - the ones you can enter and edit - and
  **Non-Campus** shows/hides every other building loaded from the database as flat gray placeholder blocks (context only,
  not editable; unchecked by default). **Reset Building Filters** shows everything again. The filters are kept when you
  enter a building and come back to the exterior.
- **ROADS** (collapsed) - the road filters below.
- A note that buildings outside the campus are context only.

**Road filters** (the whole panel in the interior editor, which has no outside buildings):

- **Hide All** - hides every road at once, and visually unchecks the six category
  boxes below to match. Unchecking it again shows everything (categories reset to
  checked).
- **Primary** / **Secondary** / **Residential/Tertiary** / **Cycleway** /
  **Footway/Path** / **Other** - one checkbox per road category (bucketed from OSM's
  own finer `highway=` tags); toggling one shows/hides just that category's roads.
- **Campus Only** - a region/distance filter (not a category): hides every road *and*
  POI farther than a padded radius from the loaded campus buildings' own extent.
  Checked by default.

**Why some non-`university`-tagged buildings are still enterable:** loading a
database doesn't just take the buildings tagged `university` - it also pulls in any
building whose footprint physically touches (or nearly touches, within a small
tolerance) one already in the campus set, repeating until nothing new joins. This
catches covered walkways/bridges and a few buildings mapped under a different OSM
type that are really part of the same connected campus structure, so they don't get
left out as inert placeholder blocks.

## Top menu bar

Pinned across the very top of the interior editor screen.

- **Save** — same as `Ctrl+S`, left of File.
- **File** — **New** clears everything and starts a blank project; **Load** opens the
  OS file picker for an existing `buildings.json` or `.db`. Both check for unsaved
  changes first and prompt to save/discard/cancel, exactly like leaving to the
  exterior does.
- **Window** — a checklist of the eight dockable panels (Toolbar/Floor/Properties/
  Layers/Navigation/Audio/Map/Debug, see below). Unchecking one hides it; re-checking
  puts it back where it was. Navigation, Audio and Debug start hidden; the other five
  start visible. **Reset Layout** puts every panel back in the team default
  arrangement.
- **Back**, on the right — same as described above.

## Building switcher bar

The bar directly under the menu bar, in the style of the design team's page, in one row with everything side by side.
A small building mark and a dropdown that lists every building in the currently loaded project - pick one to jump
straight into its interior without going back to the exterior first (switching buildings while in 3D preview exits back
to 2D first) - and the pencil next to it, which renames the currently-loaded building in place (it becomes a check mark
to confirm).

Then, in order: **2D | 3D**, one segmented control (the active mode is blue) that toggles the preview mode (see below);
**Undo** and **Redo** (the same as `Ctrl+Z`/`Ctrl+Y`); a **Snapping** button (same on/off as pressing `N` - see Snapping
below), only here, not on the exterior screen; and, only while in 3D preview, a **View**/**Edit** toggle (amber for View,
green for Edit; it is 3D's sub-mode - covered in the 3D preview section below). Drop-downs in the editor share one dark
look. Which floor you are on is shown by the Floor panel (switch with it or with `[`/`]`). The 3D camera controls are not on this bar: while
in 3D preview a **Camera** menu appears in the menu bar after **Window**. Its dropdown
holds **Reset** (snaps the view back; closes the menu), the typed move-speed box (Enter
or clicking away commits it), then **+** and **-** to nudge the speed (the menu stays open
so you can press them repeatedly). Clicking anywhere else closes it.

## UI scale

Every screen's UI is drawn at a fixed 0.77 scale, whatever the window size (set in
`Config/DefaultEngine.ini`, `UIScaleCurve`) - so the editor, the packaged build and any
resolution look the same, rather than the UI growing and shrinking with window height as
Unreal's default does. A smaller window therefore shows less of the UI, not a smaller UI.

## Dockable panels

Eight panels — **Toolbar**, **Floor**, **Properties**, **Layers**, **Navigation**,
**Audio**, **Map**, **Debug** — sit in a Visual Studio / Unreal-style docking layout. The
middle of the screen (the floor plan or the 3D view) is never part of the layout; panels
dock around it, on the left, right, top or bottom edge, and sit on top of the canvas.

- **Tabs**: every panel has a tab. Several panels in one group show as tabs; click a tab
  to switch. The **×** on a tab hides that panel (the Window menu check box clears too).
- **Re-dock by dragging a tab**: press a tab and drag. A blue preview shows where it will
  land when you let go:
  - onto the **middle** of another group (or its tab strip) → joins that group as a tab;
  - onto the **left / right / top / bottom** part of a group → splits that group, putting
    the panel beside or above/below it;
  - onto a **screen edge** → docks against that edge (a new column or row if one is
    already there);
  - along **its own tab strip** → reorders the tabs.
  Right-click while dragging cancels; letting go anywhere with no highlight does nothing.
- **Resize**: drag the thin bar between two panels, or the inner edge of a side/top/bottom
  area, to change how much room each gets.
- **Hide / show**: the tab's **×**, or the Window menu. Showing a panel again puts it back
  where it was.
- **Reset**: Window → **Reset Layout**.

Panel contents scroll when they are taller than their panel. The arrangement (which panels
are tabs of which, how the space is split, which are hidden) is saved automatically — one
global layout per machine, not per project — whenever you save the project or leave the
interior editor, and restored the next time you open it. Layouts saved by older versions
of the editor are not compatible; the first run after updating uses the team default.

- **Floating windows**: drop a tab on empty space and it becomes a floating window. Drag the thin
  strip along its top to move it, drag its outer edge or corner to resize it, and click it to bring
  it to the front. Dragging the strip of a window that holds a single panel docks it like a tab drag;
  dropping on another window's strip adds the panel as a tab of that window. Right-click while
  dragging a tab cancels the drop.

### Toolbar panel

Headed **FLOOR EDITOR / 2D floor tools**, in the style of the design team's page: one row per tool with its icon,
its name and its keyboard shortcut in a small key on the right (hover a row for the full description). The row of the
tool that is active is highlighted in blue. Four groups:

- **SELECT** - the select tool (`Q`): click a room (anywhere in its body, not just
  the outline), a door marker, or a connector marker to select it; drag to move it;
  `Backspace` to delete.
- **DRAW & PLACE** - draw a custom room (`P`, click points then `Enter`, `Esc` cancels), draw a
  standalone wall (`L`, same mechanic), or place a prefab rectangle/triangle/circle
  room (`1`/`2`/`3`) centered on the next click.
- **DOORS & LINKS** - add-door mode (`K`, click a wall edge to cut a fixed-size door into
  it), or place a stairs/elevator/escalator connector (`5`/`4`/`6`) at the next click;
  an elevator defaults to every floor in the building, stairs/escalator default to the
  floor above and below.
- **TRANSFORM** - grow/shrink the selected room (`=`/`-`), and hold to rotate it
  counter-clockwise/clockwise around its own center (hold `9`/`0`).

Clicking inside the building's outline but on top of a wall, outside the outline, or
overlapping another room will fail, with the reason logged to the Output Log.

### Floor panel

The heading shows **Floors** and how many there are, with **+ Add** on the right. Each floor is a row - "Level N"
and, on the right, how high that level is above the ground floor (for example 0 m, 3 m, 6 m, from the building's
own floor height); the active floor's row is blue. Click a row to switch to that floor (same as `[`/`]` to cycle
up/down). **+ Add** and **Delete floor** do the same as `F`/`X`; Delete floor turns red under the mouse.

### Properties panel

Headed **Properties**, with a small tag saying what is selected (ROOM, WALL, DOOR or CONNECTOR). With nothing selected
the section heading reads "Nothing selected"; selecting a room, door, or connector fills it in. Fields have small-capital
labels over dark fields, and every kind ends with a red **Delete ...** button (the same as `Backspace`) and **Save changes**.

- **Room / wall**: **ROOM NAME**, then **X POSITION**, **Y POSITION**, **WIDTH** and **HEIGHT** - the room's bounding box,
  read-only, following the room as you drag or resize it - and (rooms only) **ROOM TYPE** and **SEARCH TAGS**.
  The type is one value picked from a fixed list (Classroom, Lecture hall, Lab, Office, Study area, Meeting room,
  Canteen, Toilet, Lift lobby, Corridor, Storage, Other; the list lives in `RoomTypes.h`) and is shown in the tag at the
  top of the panel; a room with no type is **Other**. Tags are a free comma-separated list (for example
  `laboratory, teaching`) for details the type cannot say. **Save changes** saves the name, the type and the tags
  together. Both are saved with the project (`roomType` is left out of the file when it is Other, so older files load
  unchanged), work with Undo/Redo, are copied with a copied room, and make the room findable in the Layers and
  Navigation searches (the word "other" is not searchable). Blank and repeated tags (ignoring case) are dropped.
- **Door**: which room it belongs to (read-only), and **CONNECTS TO**, a dropdown to pick what it
  connects to on the other side (another room, by name, or "corridor").
- **Connector**: its type (read-only), and **FLOORS IT REACHES**, a checklist of floors -
  check/uncheck floors and hit **Save changes**. Only shows checkboxes for floors that actually
  exist on the loaded building.

### Layers panel

An Outliner-style view of everything on the current floor (the heading says which: "LEVEL 3"), with a search box
at the top to filter by name. Typing in the search box also adds a **Search results** group at the
top: every room of the building, on **any floor**, whose name, id or tags match (every word
you type must match, ignoring case). Click a result to switch to its floor and select the
room. Five categories — **Boundary**, **Rooms**, **Walls**, **Path
Nodes**, **I/O Nodes** — each expandable (arrow) to list its individual items, and showing on its right how many of that kind
this floor has. The dot on a category or an individual item (blue when shown, grey ring when hidden) toggles its
visibility on the floor plan;
toggling a category affects everything in it at once.

### Navigation panel

Pick Start/Destination Room from the dropdowns (populated from the current floor's
rooms, refreshed live as rooms are added/renamed/deleted) and it marks the room's
centroid on the floor plan, or click **Set Start**/**Set Destination** and then click a
point directly on the floor plan instead - either way the button turns green once set.
A **search box above Destination Room** filters the destination list across every floor of
the building by room name, id or tag (every word must match, ignoring case), with a line
saying how many destinations match; typing never changes the Start you already picked.
**Find Route** enables once both are set on the same building and computes/draws the
route (including cross-floor connector instructions where relevant); **Clear Route**
resets both endpoints. **Hide Route** / **Show Route** hides or restores the route lines,
direction arrows, markers and written instructions without clearing the route.

### Audio panel

Hidden by default (Window > Audio to show it). Lists every wav file under
`InNavi/Data/Audio/` with its own **Play** button — click to start it,
click again while it's playing to see it highlighted green; **Stop All** halts
everything at once. Multiple files can play together (e.g. background music plus an
effect). A **Button Click Sound** checkbox mutes/unmutes the short click sound every
button in the app plays on click (this is a separate, PC-wide setting — the checkbox
here and the identical one in the exterior edit screen's own Audio panel both read/
write the same underlying flag, so toggling it in one place is reflected in the other
next time that panel is opened). Leaving all the way back to the start screen stops any
audio still playing.

### Map panel

Hidden by default here (Window > Map shows it; it comes back above the Toolbar). The roads are
hidden by default too while you edit a building: opening the interior editor turns them off, and the
panel's check boxes start that way (Hide All checked). Going back to the exterior screen turns them
on again - there they start shown. Same road filters as the exterior screen's own Map panel (Hide All,
the six road categories, Campus Only) - see its description under **Exterior edit screen** above. No
site overview or building filters here - campus/non-campus building visibility isn't something you'd
toggle from inside a building's own interior.

## 3D preview mode

The **2D**/**3D** toggle (building switcher bar, see above) switches from the 2D floor
plan to a free-fly 3D view of the current floor's real generated geometry — current
building only (others hidden), current floor's own exterior wall + floor slab only
(other floors hidden, so upper/lower floors don't block the view). The dock panels stay
up and usable (Floor/Navigation in particular) so you can switch floors or plan a route
while previewing. A found route (and its Start/Destination markers) draws in 3D too —
same per-floor rule as 2D, only the segment(s)/marker(s) on the currently viewed floor
show.

Camera controls:

- **WASD** + **mouse-look** — free-fly, same as the pawn's own default movement.
- **Mouse wheel** — zoom (dollies along the current view direction).
- **Reset** — snaps back to the building's own default framed view, for when free-fly
  wanders somewhere awkward.
- **-** / **+** — adjust WASD move speed.

Switching floors while in 3D re-frames the camera to the default view of the new
floor. Switching buildings (via the building switcher) exits back to 2D first.

### View / Edit

3D preview opens in **View** mode (yellow button) — look around only, every editing
tool is a no-op, matching 2D being read-only from outside select mode. Click the
button to switch to **Edit** (green) for the same editing tools 2D has, driven by
clicking in the 3D view instead of the 2D canvas:

- **Select** — click a room, door, or connector; the selected room/connector tints
  light green (3D's equivalent of 2D's red outline — a selected door highlights its
  own room, same as 2D). Click empty space to deselect.
- **Move** — drag a selected room to reposition it (connectors are rotate/scale only,
  never dragged, same restriction as 2D).
- **Resize**/**Rotate**/**Delete** — same `=`/`-`, hold `9`/`0`, and Backspace
  shortcuts as 2D, now actually work in 3D Edit mode (they're inert in View mode, even
  if something was selected before switching to it).
- **Draw Room**/**Draw Wall**/prefab shapes/**Add Door**/elevator-stairs-escalator
  placement — same P/L/1-2-3/K/4-5-6 tools as 2D, clicking points in the 3D view
  instead of the 2D canvas. Each clicked point draws a temporary green marker/line so
  you can see where it landed; Enter/Esc finish or cancel same as 2D.

Switching back to View, leaving 3D preview, or switching buildings always resets back
to View mode.

## Snapping

On by default — placing a point (`P`) or dragging a room snaps to nearby
vertices/edges of other rooms, walls, and the building's own exterior wall, within a
small tolerance. Press `N` to toggle it off/on.

## Saving

`Ctrl+S` saves the current project (and the current panel layout). If there's no file
yet (a New session), it opens the OS save dialog first. Leaving the interior back to
the exterior, or exiting to the start screen, will prompt you to save/discard/cancel if
you have unsaved changes. **Discard** really does throw the edits away: every building you
opened for editing goes back to how it was at the last save (or when you first opened it) -
its floors (and so the exterior shell's height), its name and its interior - without
reloading the project, so it stays quick however large the database is.

## Audio feedback

Three kinds, all backed by wav files under `InNavi/Data/Audio/` (read and decoded by
this project's own small WAV reader, `WavFileLoader.h` — no third-party plugin; an
earlier pass vendored one for mp3 support, removed once the professor confirmed
wav-only playback is sufficient, including for the Audio panel's own arbitrary-file
playback):

- **Button click sound** (`click_button.wav`) — every button in the app plays this on
  click. Mute it from either screen's own Audio panel (see above) — it's one shared
  setting, not per-screen.
- **Building selection sound** (`select_ui.wav`) — plays only when clicking newly
  selects a building in the exterior 3D view (not on deselect, and not tied to the
  click-sound mute above).
- **Unsaved-changes dialog sound** (`quit_confirm.wav`) — plays once, the instant the
  Save/Discard/Cancel dialog itself appears (Back, File > New, File > Load, switching
  buildings — anywhere unsaved changes trigger it), not tied to any of its buttons.

Both are separate from the Audio panel's own file list, which is for playing back
arbitrary files under `Data/Audio/` on demand (background music, effects, etc.).

## Known gaps / not built yet

- Grow/Shrink is still a fixed 20-unit nudge, not a typed Length/Width or drag-to-resize
  interaction.
- No dedicated tool for a plain path node (only elevator/stairs/escalator have a
  placement tool right now).
- No "3D Preview" floating thumbnail overlaid on the 2D view (the mockup's own stretch
  goal, separate from the full-screen 2D/3D toggle that's already built).
- The 3D route visualization is plain persistent debug lines/spheres, not a proper mesh
  - a route redraw (e.g. picking a new Start point) also clears any in-progress
  draw-room/wall point markers in 3D if the two happen to overlap in time.
