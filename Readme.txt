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
Yongqi (Tech Lead) - exterior/interior 3D features, 2D interior editor, and
  SQL-backed exterior data loading.
[Kaiming / Padre / Afiq / ws / jh / zhiming - fill in each person's actual M1
  contribution here before submitting]

================================================================================
SOURCE / DEVELOPMENT SETUP
================================================================================
Full source, build instructions, and design docs are in the accompanying
GitHub repository (see M1 rubrics for the link) - see that repo's own
README.md and docs/ folder for engine version, Visual Studio setup, and the
full interior-editor control reference.
