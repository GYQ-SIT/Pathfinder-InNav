# Task for PL: smooth walls and roads (T8) - bring it into `main` with your own pull request

Yongqi built and tested the fix on the branch **`YQ_smooth_walls_roads`** (two commits, `f23f806` and `2061c21`, on top of `main` at `ccb87d8`).
**Your job is to take that work, understand it, and submit it as your own pull request.** You may copy the files straight from the branch;
you do not have to rewrite it. What matters is that you can explain every part of it and that the PR goes through review.

## Paste this to your AI assistant

> I am working on the repository `GYQ-SIT/Pathfinder-InNav` (Unreal Engine 5.8, C++). A teammate's branch `origin/YQ_smooth_walls_roads` already
> contains a finished change that makes the building walls and the roads smooth. Read `git diff origin/main...origin/YQ_smooth_walls_roads`
> and the document `docs/planning/pl-handoff-smooth-walls-roads.md` on that branch (`git show origin/YQ_smooth_walls_roads:docs/planning/pl-handoff-smooth-walls-roads.md`).
> Help me (1) create my own branch from the latest `origin/main`, (2) bring the code changes of that branch into it (copy the files or cherry-pick
> the two commits, not the handoff document), (3) build, run the tests and the checks listed in the document, (4) explain to me, file by file,
> what the code does and why, until I can explain it myself, and (5) prepare a pull request description. Do not open or merge the pull request
> for me, and do not add co-author or "generated with" lines to commits. Keep the repository's CRLF line endings.

## What was wrong (what you can see in the editor)

1. **Walls looked like separate pieces.** Every wall edge was drawn as its own box. At a corner two boxes overlap and leave a notch outside,
   and `CalculateTangentsForMesh` averages the normals of vertices that sit in the same place, so each box was lit differently: two shades on one
   wall, visible seams on a curved front.
2. **Room walls looked hollow from above.** They used the translucent material `M_BuildingTransparent`. A translucent material writes no depth,
   so its faces are painted in mesh order and a nearer wall can be painted over by a farther one.
3. **Roads were thin boxes**, one per segment: gaps outside every bend, a cap inside every joint, overlapping boxes whose shadows fought each other
   into a checked pattern, and roads of different kinds in the same plane fighting for the same pixels.

## What the fix does

| Part | File | Idea |
|---|---|---|
| Wall ring | `PolygonGeometryUtils.h/.cpp`: `ComputeWallCorners`, `AppendWallSpan`, `AppendWallEdges`, `FShadedMesh` | A wall is one continuous ribbon. Two edges meet at one **mitred** corner point (no notch), no cap is drawn where the wall carries on, and the normals are chosen by us: smooth round a gentle bend of short edges, crisp at a real corner. |
| Door and window gaps | same, `FWallPiece`, `WallReachesVertex` | A piece of wall is joined to its neighbour only if the neighbour's wall also reaches that corner over the same height; otherwise it ends square **with a cap** (no open slit). |
| Users of the wall ring | `PolyBuilding.cpp` (`BuildWallSection`), `ContextBuildingActor.cpp`, `InteriorRoom.cpp` (`RebuildMesh`) | They build the pieces of every edge, then call `AppendWallEdges`. |
| Room wall material | `InteriorRoom.cpp` | Opaque `BasicShapeMaterial` instead of the translucent one (its opacity was always 1, so nothing is lost). |
| Roads | `RoadSegmentActor.cpp`, `PolygonGeometryUtils.cpp`: `SmoothPolyline`, `AppendFlatRibbon` | A road is flat paint: one ribbon along the whole line, bends rounded first, mitred corners, a half disc at each open end so roads that meet close up, two-sided, **no shadow**. |
| Road layers | `BuildingGenerator.cpp` (`SpawnRoad`) | Each road kind sits 0.6 higher than the one before (primary lowest, footway highest), so crossing roads never share a plane. |
| Tests | `SmoothGeometryTests.cpp` | 11 tests `InNavi.Geometry.*`. |

### The one rule you must know: the facing of a triangle

The engine takes the facing of triangle (P0, P1, P2) as `(P1 - P2) ^ (P0 - P2)`. `AddFacingTriangle` in `PolygonGeometryUtils.cpp` orders every
triangle by that rule so that each face points along the normal we store. Do not hand-order triangle indices; a face with the wrong order is
drawn inside out and lit the wrong way round. (`FacesAgreeWithNormals` in the tests checks this.)

## Steps

1. **Start from the latest `main`, on a new branch.** Do not reuse `building_colour_PL` or `UI_PL`; they are far behind `main`.
   ```bash
   git fetch
   git checkout -b PL_smooth_walls_roads origin/main
   ```
2. **Bring the code in.** Either copy these files from the branch
   ```bash
   git checkout origin/YQ_smooth_walls_roads -- \
     InNavi/Source/InNavi/PolygonGeometryUtils.h InNavi/Source/InNavi/PolygonGeometryUtils.cpp \
     InNavi/Source/InNavi/PolyBuilding.cpp InNavi/Source/InNavi/ContextBuildingActor.cpp InNavi/Source/InNavi/InteriorRoom.cpp \
     InNavi/Source/InNavi/RoadSegmentActor.h InNavi/Source/InNavi/RoadSegmentActor.cpp InNavi/Source/InNavi/BuildingGenerator.cpp \
     InNavi/Source/InNavi/SmoothGeometryTests.cpp
   ```
   or `git cherry-pick f23f806 2061c21`. Then check `git diff origin/main --stat`:
   it must show exactly those 9 files and nothing else (no `Data/buildings.json`, no `Saved/`, no personal files).
3. **Read it** (see "Be able to explain" below). Read the diff of every file, not just the summary.
4. **Build and test** (editor closed; paths as in `README.md`):
   ```bash
   Build.bat InNaviEditor Win64 Development -Project="<repo>\InNavi\InNavi.uproject" -WaitMutex
   UnrealEditor-Cmd.exe InNavi/InNavi.uproject -ExecCmds="Automation RunTests InNavi; Quit" -unattended -nullrhi -nosplash
   python scripts/lint.py
   ```
   Expected: 272 tests pass (11 of them `InNavi.Geometry.*`), lint 0 problems. The static analysis command (`README.md`, "static analysis")
   must still report 0 findings in the product code. Pull requests only get lint on GitHub, so these run on your machine.
5. **Look at it in the editor** and write what you saw in the PR: the roads (flat, rounded bends, no checked pattern), a curved building front
   (one smooth surface), a room corner and the top of a room wall from above (solid, no notch), a door beside a corner, and a room that shares a
   wall with another one when you select it (the shared wall must not flicker). Check the frame rate in Window > Debug in the biggest building
   (10 floors, 225 rooms): room walls now cast shadows. Screenshots belong in the PR.
6. **Commit** with the Jira key if there is one (`PAT-123 Short summary`), in English, no co-author lines. Push the branch.
7. **Open the pull request** (see `docs/pull-request-guide.md` once it is on `main`, or the steps in `README.md` "Working agreement"):
   fill in the template, tick only what you really did, and choose a reviewer **who is not you**. Pick someone who does not know this code yet;
   Yongqi can be a second reviewer. Answer every review comment. You merge after the approval, then delete the branch.

## Be able to explain (the professor asks this kind of question)

- Why was a wall made of boxes wrong, and what does a mitre do at a corner?
- Why do we set the normals ourselves instead of calling `CalculateTangentsForMesh`?
- What is the difference between the smooth and the crisp corner, and why does a long wall not get smoothed?
- Why is a road flat and shadowless, and why does each road kind get its own height?
- What goes wrong with a translucent material on a wall, and why is the opaque one safe here?
- What happens when a door starts at a corner, and why does that piece need a cap?
- How do the tests show the change works? (Open `SmoothGeometryTests.cpp` and walk through one test.)

## Known limits (written down, not bugs to fix in this PR)

- Corners sharper than about 60 degrees are limited by the mitre cap, so the wall is a little thinner at the tip (a 45 degree corner about 23 %).
- A door right at a corner leaves a tiny triangular notch (about 10 cm) in the lintel above it.
- Very short edges beside a concave corner can make the inner mitre points cross; this is cosmetic.
- `InteriorConnectorMarker.cpp` still uses the old `AppendWallBox`; it was not part of this task.

## Practical notes

- Source files here use **CRLF** line endings. Keep them: after editing, `git diff --stat` should show only the lines you meant to change.
- Close the Unreal editor before building or running tests from the command line.
- The Android build is not part of CI. If you can, check the new code compiles for Android too (`docs/android-signing.md` has the packaging
  command); clang treats unused variables and a few more things as errors.
