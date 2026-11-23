# UI gap list: the design team's prototypes against the app

Task T5 (PAT-173). The UX team made two interactive prototypes: one for the phone app and one for the editor (see
[the prototypes](../prototype/README.md)). This page lists what the app does like the prototype, where it differs, and what it
does not have yet. It was written on 2026-11-23.

**How it was compared.** Each prototype was opened at its address and its screens, controls and flows were read: the four tabs of the
phone prototype (version **V2, "Map search"**; V1 is not used), the place page, the route page and the walking page, and every page of the
editor prototype (Projects, Editor, Floors, Rooms, Destinations, Navigation, Validation, Runtime preview). The phone app was built to V2
screen by screen (PR #22) and rechecked for this list. **Not compared:** spacing, fonts, exact colours and animations at the pixel level.
The prototypes show simulated data ("All maps, places and positioning are simulated"); the app uses real project files, so a difference that
comes only from the data is not a gap.

Status: **Done** (the same), **Differs** (there, but not the same), **Missing** (not in the app).

## Phone app

| Area | Prototype (V2) | App | Status |
|------|----------------|-----|--------|
| Tabs | Explore, Directory, Saved, Profile | The same four | Done |
| Explore: sites | Search sites, a card per site (picture, name, subtitle), a note that the data is simulated | The same, from the project files; the card shows the project's cover picture, or a drawing when it has none; the data note shows for simulated data | Done |
| Explore: card details | Distance to the site (2.4 km) and the number of levels | The number of buildings | Differs. The distance could be worked out from the GPS now; levels belong to a building, not a site |
| Directory: map | 2D / 3D, a level choice, Re-centre, search with filters | 2D / 3D, a level drop-down, Locate (centres on the GPS dot, or My location), the search overlay and filters | Done |
| Map legend | A "Start / Destination" legend | The start and end of a route are marked, with no legend | Differs (small) |
| Route on the map | The walked part orange, the part still to walk blue | One colour (blue) for the whole route | **Missing** |
| Place page | Get directions, Save place, Use as starting point | The same | Done |
| Saved place | A saved place is marked with a filled heart | An outline heart only (no filled red heart texture) | **Missing** |
| Route page | My location / From, several destinations with Add stop, reorder and remove, Lift / Escalator / Stairs choices, route steps, Start navigation | The same (PR #22) | Done |
| Route options | Pills with an icon each (Lift, Escalator, Stairs) | The pills, with no icons | Differs (small) |
| Walking page | The destination, the instruction, "Step n of N", the distance left, **Next instruction**, **Show my current level**, an **Up next** list of the steps after this one, Exit | The destination, the instruction, "Step n of N", the distance left, Next instruction, Exit (with a question before it ends the walk) | **Missing**: the Up next list and Show my current level |
| Several destinations | "Destination i of N", Stop reached / Continue | The same | Done |
| Arrival page | Arrived, close | The same | Done |
| Saved tab | Empty state "No saved places yet" with Explore places | The same, and the places once saved | Done |
| Profile tab | Visitor, Appearance (InNav light, Night theme, Larger text), About, Reset saved places and preferences | The same | Done |
| Room types | The design team's twelve room-type icons (a coloured circle with a white symbol) | Not in the app: rooms show their names; all list rows have the same pin | **Missing** |
| Phone preview | "Phone view", drag to resize | Not applicable: it is the phone app | Not applicable |

## Editor

The prototype is a web page with a project dashboard, one editing page with a tool strip, and management pages (Floors, Rooms,
Destinations, Navigation), a Validation page and a Runtime preview. The app has two screens (the campus, the interior) and dock panels
(see the [interior editor guide](interior-editor-guide.md) and the [UI panels guide](ui-panels-guide.md)), so the pages map to panels, not to pages.

| Area | Prototype | App | Status |
|------|-----------|-----|--------|
| Projects | A library of buildings: New project, Open project, Import JSON, "Jump back in" (recent projects); a card with floors, rooms and nodes counts, a Configured mark, Load, Duplicate, Rename | The project library: a card per project (cover picture, name, buildings, description, date), New project, Open, Duplicate, Rename, Delete (to `Data/Deleted/`) | Differs. No "Jump back in" on the start screen; no floors / rooms / nodes counts or Configured mark on a card |
| Import | Import JSON, **Import model** | File > Load for a project or a map database; a reference picture can go under a plan. No import of a 3D model | **Missing**: Import model (needs a decision: is it wanted?) |
| Top bar | Undo, Redo, Save, Validate, Preview, Export JSON | Save, Ctrl+Z / redo, the menu bar (File, Window), Validation (Window), 3D preview; the project is saved as JSON | Done (as a menu bar, not buttons) |
| Floors | A page of floors with their elevation, Add floor | The Floor panel: a card per floor (name, level, height), Add floor, basements, Duplicate to floors | Done |
| Rooms | A page listing every room, with Add room | The Layers panel lists the rooms of the floor (search finds a room on any floor); the Properties panel edits one | Differs. No page of all rooms of the building |
| Destinations and POIs | A page of destinations (entrances, offices) | A room is a destination; the main doors are the entrances; the places of interest were dropped | Differs |
| Navigation graph | A page of graph nodes, with connect on the canvas | The planner builds the graph from rooms, doors and connectors; the Layers panel has path nodes; the Navigation panel plans and shows a route | Differs by design: the graph is not drawn by hand |
| Tools | Select, Draw room, Door, Path node, Destination, Lift, Stairs, Escalator, Connect nodes, Pan | The toolbar, in five groups: Select; Draw & place (a room by its corners, a wall, a rectangle, triangle or circle room); Doors & links (a door, stairs, lift, escalator); Utility points; Transform. The plan pans with a right-drag | Differs. No tool for a plain path node or Connect nodes (see "Known gaps" in the editor guide) |
| 2D / 3D | 2D, 3D, **Stack** (the floors spread out) | 2D and 3D, Isolate and Rotate; the route overview pulls the floors of a route apart | Differs. No Stack view of the whole building |
| Validation | A page with the counts of errors, warnings and passed, and the findings | The Validation panel: the same counts, a scope and severity filter, a finding shows the object on the plan or in 3D, the report is exported as JSON | Done (beyond: the checks are many more) |
| Runtime preview | A visitor view: pick two endpoints, a route on the plan | The Navigation panel: Plan and Go, a route on the plan and in 3D, a whole-route view, steps | Done (beyond) |
| Properties | X and Y of the selected object, its type | The Properties panel: position, size, name, type, doors | Done |

## What the app has that the prototypes do not

The phone app: the real project files and routes; a GPS dot on the campus and on a building's plan, with the direction the phone faces; the
map follows that direction; north at the top; routes across buildings and floors. The editor: the exterior screen (roads, buildings, the map
around the campus), generated floors, the layout validator with a headless file check, the 3D route overview.

## Gaps to close, in the order I suggest

1. Walking page: the **Up next** list and **Show my current level**.
2. Route on the map: **walked orange, to walk blue**.
3. **Filled red heart** for a saved place.
4. The **room-type icons** (the twelve) on the map and in the lists.
5. Editor: counts and a Configured mark on a project card, and **Jump back in** on the start screen.
6. Decide, with the design team: **Import model**, the **Stack** view, a plain **path node** tool, a page of all rooms.
7. Small: the map legend, icons on the route-option pills, distance on an Explore card.
