InNavi - Milestone 1 Submission
Team Pathfinder | CSD3401 / UXG3400 | Fall 2026

================================================================================
WHAT THIS IS
================================================================================
InNavi is a standalone Unreal Engine 5.8 application that adds an indoor
navigation layer on top of a 3D model of a campus (SIT Punggol): select a
building, author or import its interior layout (rooms, walls, doors,
elevators/stairs/escalators), generate a schematic 3D interior at runtime, and
compute a route between two points inside it - including across floors.

Engine: Unreal Engine 5.8 (commercial engine track).

================================================================================
HOW TO RUN
================================================================================
1. Unzip this package.
2. Double-click InNavi.exe (in the Windows/ folder).
3. From the start screen: New starts a blank project; Open browses for an
   existing building data file (.json or .db); Quit exits the application.

No installation, no external dependencies. Everything the app needs (sample
building data, audio, default UI layouts) ships alongside the .exe under its
own Data/ folder - do not separate InNavi.exe from the folders next to it.

================================================================================
CORE FLOW / CONTROLS
================================================================================
1. Start screen: New / Open / Quit.
2. Exterior view: the campus in 3D. Click a building to select it (Enter
   Interior appears in the top menu bar once one is selected). Right-drag to
   pan, scroll to zoom, WASD + mouse-look to fly the free camera.
3. Interior editor: a 2D floor-plan view of the selected building's active
   floor.
   - Toolbar panel: draw rooms/walls, place prefab room shapes, add doors,
     place elevator/stairs/escalator connectors, select/move/resize/rotate,
     delete (Backspace).
   - Floor panel: switch/add/remove floors.
   - Properties panel: edit the selected room/door/connector's fields.
   - Navigation panel: set a start point and a destination, then Find Route -
     draws the computed path, including cross-floor transitions via
     elevators/stairs.
   - A 2D/3D toggle switches to a real 3D view of the current floor (View/Edit
     sub-modes let you keep editing from the 3D view too).
   - All panels are freely movable/resizable/dockable; Window menu toggles
     which ones are shown.
4. File > Save/New/Load and Back (top-right) all check for unsaved changes
   first.

================================================================================
FEATURES IMPLEMENTED (M1)
================================================================================
- Campus Building Selection: the real SIT Punggol campus (buildings, roads,
  POIs) loads from a SQL-backed database; click a building in 3D to
  select/highlight it.
- Custom Building Layout Editor: a full 2D floor-plan editor - draw rooms and
  walls, prefab room shapes, doors with connection properties, elevators/
  stairs/escalators with cross-floor data, room/door naming, snapping, and
  boundary/overlap validation. Save/load round-trips through JSON. A
  dockable Toolbar/Floor/Properties/Layers/Navigation/Audio panel layout, a
  top menu bar (File/Window), and a building switcher are all in place.
- Instant 3D Interior Visualization: the generator spawns a real 3D
  procedural-mesh interior per floor from the saved data (tested on
  multi-floor buildings). A 2D/3D toggle switches to a real 3D view of the
  current floor, with a View/Edit sub-mode giving the 3D view the same
  editing capability as the 2D one (select, move/resize/rotate, delete,
  draw, add door, place connector).
- Indoor Navigation System: pick a start point and a destination (any valid
  point inside the building, not just room centers) and compute a route,
  including cross-floor transitions via elevators/stairs/escalators. The
  route draws on both the 2D floor plan and the 3D preview.
- Audio: UI click sounds and a building-selection sound, adjustable/mutable
  from an Audio panel.

================================================================================
KNOWN ISSUES / LIMITATIONS (M1)
================================================================================
- Buildings loaded from a database (.db) get a fixed placeholder floor count
  (5) - the source data has no floor information of its own.
- Non-campus buildings/roads/POIs (loaded from the sample database) are
  simplified, non-interactive placeholders - only the real campus buildings
  have an editable interior.
- Mobile navigation and AR are out of scope for M1 (desktop-only prototype).
- Automated CI build for this UE5 project is not yet set up (tracked, not
  blocking this milestone).

================================================================================
TEAM
================================================================================
Team Pathfinder
- Yongqi Ge (Tech Lead)
- Ang Kai Ming (PM)
- Padre Lim
- Mohamad Afiq
- Ong Wei Song
- Zhou Jiahao
- Yeo Zhi Ming

================================================================================
CREDITS / THIRD-PARTY ASSETS
================================================================================
No third-party assets used - all icons, audio, and other assets were made
in-house by the team.

================================================================================
SOURCE / DEVELOPMENT SETUP
================================================================================
Full source, build instructions, and design docs are in the accompanying
GitHub repository (see M1 rubrics for the link) - see that repo's own
README.md and docs/ folder for engine version, Visual Studio setup, and the
full interior-editor control reference.
