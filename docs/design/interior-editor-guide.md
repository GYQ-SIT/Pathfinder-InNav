# Interior Editor — How to Use It

> Current state as of 2026-10-28. The editor now has a real UMG UI
> (menu bar, dockable panels, buttons) - every action below also still has a keyboard
> shortcut, and both do the same thing under the hood.

## Getting to the interior editor

**Window > Validation** opens the advisory layout checker on either screen. Choose a scope and
press **Validate layout**; select a finding to show the affected object, then fix it with the normal
tools and validate again. Editing and saving remain available. See [Layout validator](layout-validator.md).

1. Launch the project, pick **New project** (blank), **Open project** (the project library, below), or **Quit**, from
   the start screen.
   **New project** asks for a **Project name** (required) and an optional **Description** first. Create writes a blank
   `<project name>.json` into the `InNavi/Data` folder (characters Windows does not allow in a file name are replaced;
   an existing project is never overwritten - you are asked for another name) and opens the editor on it, so **Ctrl+S**
   saves straight to that file. The name and description are kept in the file (`projectName`, `description`). Every saved file also carries a
   `version` (the format number, currently 1); a file without one is an older file and opens as before, so a later change of
   format, such as saving into a database, can tell old files from new ones.
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

- **File** — **New** and **Load** prompt about unsaved changes. **Save As** saves to a path you choose, including when the original file changed on disk.
- **Window** — **Audio** (see Audio panel below), **Map** (see Map panel below),
  **Building**, **Debug** and **Navigation** (see Navigation panel below), plus **Reset Layout**.
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

This screen's five panels — **Map** (shown by default, docked on the left), **Building**
(shown by default, docked on the right), **Navigation** (hidden by default; when shown it is a
tab beside Building), **Audio** and **Debug** (hidden by default, tabs of
one group along the bottom) — use the same docking
system as the interior editor (see **Dockable panels** below: tabs, splits, screen edges,
floating windows, Reset Layout). Its arrangement is saved separately, in
`Saved/ExteriorDockLayout.json`, when you save or leave the screen. The **Debug** panel
shows the selected building, camera speed, unsaved-changes flag, FPS and frame time, and a
**Last Error** line for the most recent failed action (yellow: rejected, nothing lost; red:
data may have been lost) - File > New/Load failures used to be silent on this screen.

> Audio, Map, Debug and Navigation are the same widgets on this screen and in the interior editor; how panels are built and added is in [UI Panels Guide](ui-panels-guide.md).

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

**Road filters**:

- **Hide All** - hides every road at once, and visually unchecks the six category
  boxes below to match. Unchecking it again shows everything (categories reset to
  checked).
- **Primary** / **Secondary** / **Residential/Tertiary** / **Cycleway** /
  **Footway/Path** / **Other** - one checkbox per road category (bucketed from OSM's
  own finer `highway=` tags); toggling one shows/hides just that category's roads.
- **Campus Only** - a region/distance filter (not a category): hides every road *and*
  POI farther than a padded radius from the loaded campus buildings' own extent.
  Checked by default.

**The ground follows the filters.** The grey ground under the campus is not a fixed size: it is made as big as what the Map panel shows - the campus
buildings that pass the building filters, the other buildings when Non-Campus is checked, the roads of the checked categories (and inside Campus
Only's range) and the places - plus a margin (5 percent of the longer side, at least 10 m), and never smaller than 40 m each way. So a whole
city loaded from a database all stands on ground, and filtering down to a few buildings or hiding the roads shrinks it to fit. It updates a
moment after each change and after every load. If nothing is shown it stays as it was. It does not throw shadows.

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
- **File** — **New** starts a blank project, **Generate floor** fills the current floor,
  and **Load** opens the OS file picker for an existing `buildings.json` or `.db`.
  **Save As** writes the current project to a path you choose. New and Load check for unsaved
  changes first and prompt to save/discard/cancel, exactly like leaving to the
  exterior does.
- **Window** — a checklist of the eight dockable panels (Toolbar/Floor/Properties/
  Layers/Navigation/Audio/Map/Debug, see below). Unchecking one hides it; re-checking
  puts it back where it was. Navigation, Audio, Map and Debug start hidden; the other four
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
below), only here, not on the exterior screen; a **Colours** button (turns the room colours on and off: in the 2D plan each room is filled with its Room type colour, in 3D its floor is; on by default, not saved); and, only while in 3D preview, a **View**/**Edit** toggle (amber for View,
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
  `Delete` to delete.
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
labels over dark fields, and every kind ends with a red **Delete ...** button (the same as `Delete`) and **Save changes**.

- **Room / wall**: **ROOM NAME**, then **X POSITION**, **Y POSITION**, **WIDTH (m)** and **LENGTH (m)** - the room's bounding box. Position is read-only; width and length apply on Enter, leaving the field, or **Save changes** - and (rooms only) **ROOM TYPE** and **SEARCH TAGS**.
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

### Doors in the outer wall

The Add door tool also works on the building's **outer wall**: click the outline where there is no room wall, and a door 2.4 m wide
goes into the outer wall, on the floor being edited. Where it goes decides which of two kinds it is:

- **Main door** - the way out of the building. **Ground floor only**, and only where **no other building's wall is next to this
  one**. Upstairs, or beside a neighbour, a main door is refused.
- **Link door** - a door to the building next door. **Any floor** (that building must have it), but only **wholly where the two outer
  walls are next to each other** (within about 1.5 m). A door half on the shared wall and half off it, or beside two buildings, is
  refused. The neighbour shows the same door, on the same floor, at the matching spot on its own wall - it is worked out from this
  door, not stored twice, so opening or deleting it from either building changes both.

Nothing can be made out of the wrong place: the reason for a refusal shows in the Debug panel. Two doors cannot overlap on the same
floor and wall. A door is a gap in the 3D outer wall on its floor (on both buildings for a link door). On the 2D plan a main door is a
green mark on the outline and a link door a blue one; in Select mode click one (it turns yellow), and the Properties panel says
**Main door** or **Link door**, its floor and where it leads (for a link door the building, and when it was opened from that building).
Delete removes it. Doors are saved in the building's `interior.entrances` (an optional list, so older files load as
before) and covered by undo. **Route-finding uses them**: a link door joins two buildings' floors, and a main door joins a building to the outside, so a route can go from one building to another, over a link door or out through the main doors and back in (see the Navigation panel).


### Generate floor (File > Generate floor)

Fills the **floor you are on** with a believable layout in one click, so there is something to test with: a **core** (two lifts and a
stair) against the outer wall, **a toilet** next to it (one per core, three on a floor at most), and a **ring of rooms** along the outer wall. What is left in the middle is
the corridor (it is not drawn).

- Open the building's **main / link doors on that floor first**. One core is made beside each door (one at the end of the longest wall
  if there are none). If a core would not fit this close to another one, that one serves the door.
- The floor must be **empty** (no rooms or walls), otherwise it is refused and the Debug panel says so.
- If the building already has lifts or stairs they are used as they are and extended to this floor; no new core is made.
- Rooms get random sizes of a believable range, have a door onto the corridor, are named "Type floor-number" and tagged `generated`.
  Rooms of one kind sit together (a run of three to seven rooms is one family - teaching, offices or study - mostly of one kind),
  a few are left out and corners are sometimes open. A floor never gets more than 24 rooms (a big
  building gets bigger rooms instead), because route-finding slows down with many walls.
- Some rooms (roughly one in four of the bigger ones) have a corner cut away on the corridor side, so they are L-shaped; the door is on the
  longer front. A convex corner that is not square (a building with slanted walls) gets a corner room of its own, a parallelogram with two
  sides along the two walls. A room whose door would open into a pocket too narrow to walk out of (between two rooms, or in the tip of an
  acute corner) is left out, so every room can be reached. Open areas (the big open spaces of the real plans) are not made yet. Every room goes through the same
  checks as one drawn by hand. The whole floor is **one undo step**. Click it again after undoing to get a different layout.

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

Editing a layout clears its displayed route and cancels any search in progress. This includes live room and door drags,
walls, connectors, floors, building changes and undo/redo. Press **Find Route** again after the edit. Room-based places are
resolved by their saved IDs before replanning. Moved rooms get updated positions. If a place is unavailable, the editor asks you to replace or remove it before planning again.
Changing the viewed floor/building or saving does not itself clear a route.

Desktop and real-data mobile directions use the same instruction generator: straight stretches are grouped, with walking
distances, left/right turns, reversals, doors, building entries/exits and floor changes. One uninterrupted lift or stair ride
is one instruction. The route line keeps its original geometry; door crossings are retained as landmarks for guidance.

Where the route is set and gone through. The same panel is on the interior editor and on the exterior screen (Window >
Navigation; it starts hidden on both). It has two views, the **Plan** and **Go** tabs at the top. **Plan** is where the route is
set; **Go** is where it is gone through once it is found (it opens by itself when a route has just been found, and **Edit Route**
goes back to Plan).

**One list of places.** A route is a list of places in the order they are visited. The **first is the Start**, the **last is the
Destination**, any between are **stops**. A route needs only **two places**: a Start and one stop is a route to that stop, and no
Destination has to be set. Each place is a row with a letter or number (A and B at the ends, 1, 2 ... between, in the colour of
its leg on the map), a dropdown, and, on the ends, a **+** button to pick the place by clicking the map (green for the Start, red for
the Destination while it waits for the click). Click a row's number to choose it, then **Up** / **Down** move it along the list
(an empty end takes a place that moves onto it) and **Remove** empties an end or removes a stop. **+ Add stop** opens an empty
row after the Destination: pick a place from its dropdown, or press its **+** and click places on the map, one after another
(Esc or **Done** ends it). What is picked becomes the new Destination and the one before it the last stop; with no Destination set
it just becomes the Destination. Up to 8 stops. **Reverse** turns the list round. **Clear Route** empties the list and the route.

**Inside a building.** The dropdowns list the open building's rooms across every floor, written as "Room name (Floor N)" (no id; the
id can still be searched). Picking a room marks its centre; or press the **+** of the Start or Destination and click a point on
the floor plan (or on the floor in the 3D view) instead. Each dropdown contains its own search field, filtering that row by room name,
id, tag or room type (every word must match, ignoring case); a place you have already picked stays shown even when the search
hides it.

**Between buildings.** The places can be in different buildings: pick the Start, switch building with the building
switcher, pick the next place. A place in another building is kept, and its dropdown shows it as its first entry, "Building · Room (Floor N)".
**Find Route** enables once two places are set and finds the way over the link doors, or out of one building and into the other
through their main doors (see **Doors in the outer wall**). On a large campus the search takes a while, so it runs a little each
frame: a progress bar shows how far it is, the editor stays usable, and **Cancel Search** stops it. Changing a place, clearing, or editing the interior also stops it. A trip within one building is searched without the outside
first (the rest of the campus is not needed for it), and the whole campus only when going out and in again could be shorter.

**Step by step.** Once a route is found it is gone through in the Go view one step at a time, as on the phone: the panel shows "Step 3 of 13"
and that step, with **Previous** and **Next Step** below it (the last step reads **Arrived**). Everything follows the step. Inside, the
building and floor of the step are opened (another building too, as the building switcher opens it, and 3D stays on if it was on);
on the exterior screen the camera goes to the spot on the campus. The screen follows the step too: a step outside is shown on the
exterior screen (the campus), a step inside a building on the interior screen, so a route that goes from a room out to the campus
and into another building changes screens as it goes. What was edited in a building stays when the screen changes (it is still
unsaved), and the route and the step are kept. On the map and in 3D the stretch of the route the step covers is
drawn in full, the rest of the route is faint, and a white ring marks where the person is. **Show All Steps** lists every step
instead (nothing is singled out), and **Go Step by Step** goes back.

**Stops.** A stop is any place of the list between the ends, picked like the others (from a dropdown, in any building, or a building's
main door on the exterior screen; or by clicking indoors, the blue outside area on Floor 1, or the ground on the exterior screen).
**Find Route** plans the journey leg by leg (from each place to the next): the legs are drawn in different colours on the map and
in 3D, each stop is numbered, and the directions say where each stop is reached. If a leg cannot be planned, the message says
which one. A place where the route already is (the one before it in the list) is refused. Changing the list ends a search that
is running and clears the route.

**Outside (the exterior screen).** No building is open, so the dropdowns list **each building's main door**, written
"Building · Main door" (the point is a metre outside the door), and the search box narrows them by building name. To pick a spot
instead (it shows as **Custom Point (External)**, or **Custom Point (Floor N)** indoors), press the **+** of a place and click the **ground** next to a building (inside its outside area). A click
on a building, inside one, or too far from every building is refused, and the status line says why. The points and the route are
**kept when you open or leave a building**, and the route is drawn on the exterior 3D map with Start and Destination markers.

**Directions.** A found route is written as numbered steps, one per line - "Step 1: Start at ...", the doors, lifts or stairs to
take (with building names, not ids), and "Step N: **Arrive at ...**" - the last step in green. In Go the route is shown as a line of its places above the directions. Before a route is found the places are in Plan. **Clear Route** resets the list and the route. **Hide Route** / **Show
Route** hides or restores the route lines, direction arrows, markers and written steps without clearing the route.

### Audio panel

Hidden by default (Window > Audio to show it). Lists every wav file under
`InNavi/Data/Audio/` with its own **Play** button. Playback accepts WAV files up to 64 MiB. Click to start a file,
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
on again - there they start shown. It is the same panel as on the exterior screen (see its description under **Exterior edit
screen** above): **Choose a building** (search and the list), the filters (floor count, Campus / Non-campus, Reset) and the road
filters. The list and the filters are shared with the exterior screen, so what is set on one is there on the other, and a screen
opened from the other does not reset them. Here the chosen building in the list is the one that is open, and **clicking a building
opens its interior** (asking what to do with unsaved changes first, like the building switcher at the top). Buildings the filters
hide are not in the list.

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

### Isolate & Rotate

In **3D**, click **Isolate** in the group **Isolate | Orbit | Grab** (in the bar under the menus, between
**Colours** and **View**) to look at the model on its own and move and turn it, the way the SIT Campus
Wayfinder's map is handled. The big **bottom platform** the building stands on is hidden, and
so are the surrounding ground and the other buildings. **Every floor is kept, the first one too**:
the first floor stands on a floor slab of its own (a slab at its base, like the floors above it have), made
while the mode is on, since the bottom platform is what it used to stand on. Only the floor being viewed
is shown. The shell, rooms, connectors and route graphics turn together.

- **Left-drag** moves (pans) the model across the screen; it follows the cursor. It cannot be taken
  further than a little way from where it started.
- **Right-drag** turns the model about its own centre (an orange marker shows it; at first it is the centre
  of the first floor): left and right spin it, up and down tip it. The tip is **limited**: the model can be
  seen from above (tipped toward you by up to 60 degrees, away from you by up to 15), never from underneath
  or upside down.
- **Orbit | Grab** chooses how the turn follows the mouse, and one of them is always on. **Grab** (the
  default) makes the model follow the mouse like something held in the hand: drag right and its near side
  goes right. **Orbit** is the other way round, as if the camera were moved around the model. They can be
  used once **Isolate** is on.
- **Mouse wheel** zooms. **Camera > Reset** puts the model back as it was when the mode was entered (not
  moved, not turned) and re-frames it.
- Choose another floor normally; the position and the turn stay.
- Click **Isolate** again, or press **Esc**, to restore the previous
  camera, floor and View/Edit state. Your previous scene filters are preserved.

A building with a single floor can be looked at in this way too.

Editing and navigation picking are disabled during inspection. Return to the normal
view to edit. Floor data and saved coordinates are preserved; rotating the model
does not count as a project change. Switching to 2D, changing buildings or leaving
the editor also ends inspection and restores the model.

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
- **Resize**/**Rotate**/**Delete** — same `=`/`-`, hold `9`/`0`, and Delete
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

- No dedicated tool for a plain path node (only elevator/stairs/escalator have a
  placement tool right now).
- No "3D Preview" floating thumbnail overlaid on the 2D view (the mockup's own stretch
  goal, separate from the full-screen 2D/3D toggle that's already built).
- Outside, a point can only be picked within a building's **outside area** (the building's outline grown by a quarter of its
  size, at least 4 m), by a main door or a ground click; there is no free choice of any spot on the campus yet, and the route
  across the open ground is the planner's straight-line path around the buildings (no roads or paths).
- The 3D route visualization is plain persistent debug lines/spheres, not a proper mesh
  - a route redraw (e.g. picking a new Start point) also clears any in-progress
  draw-room/wall point markers in 3D if the two happen to overlap in time.

## Editor sizing and input

Use **Delete** to remove a selected object; Backspace is for editing text. The free-fly camera descends with **C** and ascends with **Space** or **E**. Holding Ctrl suppresses camera movement while using editor shortcuts.

The Toolbar has a **Grow / shrink step (m)** control. With a connector selected, it becomes **Scale step (%)**. The room step defaults to **0.5 m** (previously 20 footprint units), with a **0.5 m minimum room dimension or wall span**. Room **WIDTH (m)** and **LENGTH (m)** apply on Enter, leaving the field, or **Save changes**. Straight walls keep their zero dimension and allow their length to be edited. In 2D Select mode, drag a blue handle to resize with the opposite side fixed; straight walls have two endpoint handles. An orange outline previews the size, and the mesh is rebuilt once on release. Each drag is one undo step and respects the building outline, other objects, connectors and doors.

For rectangle, triangle and circle prefabs, click for the default size or drag between opposite corners for a custom bounding box. The orange outline previews the footprint; release to place it, or press Esc to cancel.

An invalid size stays in the field after Enter so it can be corrected. Leaving the field restores the actual dimensions after reporting the error. **Save changes** still applies the name, tags and type when the size is invalid. Editing one dimension preserves the exact size of the other, even when its displayed value is rounded.

Drawing validates each new segment immediately. Overlap or geometry constraints clear the current drawing and show the reason in **Debug > Last Error**. A stray click outside the building is ignored and keeps the current points. Walls may form a simple loop by returning to their start; crossing, retracing and other non-adjacent touches are rejected. **Audio > Error Sound** independently controls error feedback using the existing `UI_Off-Route.wav`; it is included in **Mute All** and follows the controller between screens.

Search inside each Start, Destination or stop dropdown by name, ID or tag. An empty result leaves the field usable so the query can be changed. Queries are retained when the options refresh. Adding a stop appends a new destination; when there are more than two places, every row after Start is numbered **1, 2, ...** in visit order, including the final destination.
