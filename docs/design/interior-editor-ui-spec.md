# Interior Editor — UI Integration Spec

> Written for Padre. This lists the C++ interface the UMG UI needs to call, what each
> tool needs to collect as input, and how to handle each function's return value. The
> code lives in `ANavPlayerController` (`InNavi/Source/InNavi/NavPlayerController.h`,
> `Category = "Interior"`) — everything below is already `BlueprintCallable`/
> `BlueprintPure`, you can drag the nodes straight into UMG.
>
> Get a reference to `NavPlayerController` via **Get Owning Player** on your UMG widget
> and cast it (not "Get Player Controller" — it doesn't reliably show up in 5.8's
> context-sensitive search). Every function below is called on that one reference — you
> never need a separate `InteriorGenerator` reference.

---

## 0. Before any of this: getting into the interior editor

`GetSelectedBuildingId()` (Building category) returns the currently-selected building's
id, or empty. Feed it into `LoadInteriorForBuilding(BuildingId)` — this is what an
"Enter Interior" button calls; it returns an `int32` (how many floors loaded, 0 on
failure). From then on, every function below acts on whichever floor is "active"
(`GetActiveFloor()` / `SetActiveFloor(Level)`).

All of the tools below work on a **2D top-down floor plan canvas** (`UInteriorFloorPlanWidget`
in C++, `WBP_InteriorScreen` in UMG) — there is no 3D raycast for interior editing, unlike
building placement. If Padre's canvas is a plain UMG widget (not a 3D viewport), clicks
need to be converted from screen space to the floor plan's own 2D "footprint" coordinate
space before being passed to any function below that takes an `FVector2D` — ping me if you
need the conversion helper exposed as its own `BlueprintCallable` function; right now it
only exists on the C++ side of `UInteriorFloorPlanWidget`.

---

## 1. Modes overview

Only **one** of the modes below (draw room, draw wall, prefab 1/2/3, add-door, connector
4/5/6, select) can be active at a time — turning one on turns any other off automatically
(handled in C++, nothing the UI needs to enforce). Snapping (§7) is a standing setting,
not a mode — it stays on/off across every mode switch.

| Mode | Toggle function | State getter | Changed event |
|---|---|---|---|
| Draw room | `ToggleDrawRoomMode()` | `IsInDrawRoomMode()` | `OnDrawRoomModeChanged(bool)` |
| Draw wall | `ToggleDrawWallMode()` | `IsInDrawWallMode()` | `OnDrawWallModeChanged(bool)` |
| Prefab rectangle | `TogglePrefabRectangleMode()` | `IsInPrefabRoomMode()` (shared by all 3 shapes) | *(none — see note below)* |
| Prefab triangle | `TogglePrefabTriangleMode()` | ″ | ″ |
| Prefab circle | `TogglePrefabCircleMode()` | ″ | ″ |
| Add door | `ToggleAddDoorMode()` | `IsInAddDoorMode()` | *(none)* |
| Elevator | `ToggleElevatorMode()` | `IsInConnectorMode()` (shared by all 3 types) | *(none)* |
| Stairs | `ToggleStairsMode()` | ″ | ″ |
| Escalator | `ToggleEscalatorMode()` | ″ | ″ |
| Select | `ToggleSelectMode()` | `IsInSelectMode()` | `OnSelectModeChanged(bool)` |

For the modes with no changed-event yet (prefab/add-door/connector), poll the state
getter right after calling the toggle function to update button highlighting — these
don't currently broadcast, since nothing needed live updates from elsewhere yet. Let me
know if you need events added for these (same pattern as the others, cheap to add).

---

## 2. Draw room mode (custom-shape room)

User clicks points to trace a room outline, same click-to-trace mechanic as exterior
building draw mode.

| UI element | Call / bind | Notes |
|---|---|---|
| Toggle button | `ToggleDrawRoomMode()` | Fails to turn on if no building interior is currently loaded |
| Toggle state indicator | Bind `OnDrawRoomModeChanged(bool)` | |
| Point count display | Bind `OnRoomPointCountChanged(int32)` | Broadcasts after every point added, and resets to 0 on finish/cancel |
| Canvas click (while in this mode) | `AddRoomPointAtFootprintPosition(FVector2D FootprintPoint)` | Call this yourself from the canvas's click handler — draw-room mode has no raycast, so nothing adds points except this call. `FootprintPoint` is the click converted to floor-plan footprint space (see §0) |
| Live preview polyline | `GetPendingRoomFootprint()` | Returns the points clicked so far, for the canvas to draw as a live outline while the user is still clicking |
| Room id field (optional) | Pass as `RoomId` to `FinishRoom` | Leave empty to auto-generate (`PeekNextRoomId()` lets you preview what it'll be, e.g. for a placeholder in the text field) |
| Room name field | Pass as `RoomName` to `FinishRoom` | |
| "Public" checkbox | Pass as `bIsPublic` to `FinishRoom` | Defaults to `true` |
| "Requires key" checkbox | Pass as `bRequiresKey` to `FinishRoom` | Defaults to `false` |
| "Finish" button | `FinishRoom(RoomId, RoomName, bIsPublic, bRequiresKey)` | Returns the new `AInteriorRoom*`, or `nullptr` on failure (e.g. self-intersecting outline, not enough points, out of bounds, overlaps another room — see the Output Log for the specific reason for now) |
| "Cancel" button | `CancelRoom()` | Clears the points drawn so far |

`Enter`/`Esc` keys still do Finish/Cancel in parallel — both stay usable.

---

## 3. Draw wall mode (standalone open wall, no room)

Same click-to-trace mechanic as draw room mode, but produces an open wall/partition line
instead of a closed room (can't take a door). Shares the same point-count event as draw
room mode (`OnRoomPointCountChanged`) and the same live-preview getter
(`GetPendingRoomFootprint()`), since both go through the same point-collection state.

| UI element | Call / bind | Notes |
|---|---|---|
| Toggle button | `ToggleDrawWallMode()` | |
| Toggle state indicator | Bind `OnDrawWallModeChanged(bool)` | |
| Canvas click (while in this mode) | `AddRoomPointAtFootprintPosition(FVector2D FootprintPoint)` | Same call as draw room mode — it's shared plumbing |
| Point count / live preview | `OnRoomPointCountChanged` / `GetPendingRoomFootprint()` | Same as draw room mode |
| Wall id field (optional) | Pass as `WallId` to `FinishWall` | Same auto-generation convention as room ids — a wall shares the room id namespace, there's no separate id scheme |
| "Finish" button | `FinishWall(WallId)` | Returns the new `AInteriorRoom*`, or `nullptr` on failure |
| "Cancel" button | `CancelWall()` | |

---

## 4. Prefab room placement (rectangle / triangle / circle)

Arm a shape, then a canvas click places one at a fixed default size, centered on the
click — no separate "finish" step.

| UI element | Call / bind | Notes |
|---|---|---|
| Rectangle button | `TogglePrefabRectangleMode()` | Pressing the same button again, or any other mode's button, disarms it |
| Triangle button | `TogglePrefabTriangleMode()` | ″ |
| Circle button | `TogglePrefabCircleMode()` | ″ (a "circle" is really a many-sided regular polygon under the hood) |
| Any armed-state indicator | `IsInPrefabRoomMode()` | No changed-event yet — poll this after each toggle call to update button highlighting |
| Canvas click (while armed) | `PlacePrefabRoomAt(FVector2D FootprintCenter)` | Returns the new `AInteriorRoom*`, or `nullptr` if nothing is currently armed. No door — that's a separate tool (§5). No name/public/key fields at placement time; if you want those set immediately, select the new room afterwards and reuse §6's rename flow (a full properties form for prefab rooms doesn't exist yet — ping me if you want one before the room is placed instead of after) |

---

## 5. Add-door mode

Arm this, then click a wall edge on the canvas to cut a fixed-size door into it,
centered on the click (clamped so it doesn't poke past either end of the edge).

| UI element | Call / bind | Notes |
|---|---|---|
| Toggle button | `ToggleAddDoorMode()` | |
| Toggle state indicator | `IsInAddDoorMode()` | No changed-event yet — poll after toggling |
| Canvas click (while in this mode) | `AddDoorToWall(const FString& RoomId, int32 EdgeIndex, float OffsetWorldUnits)` | You (the canvas click handler) need to first figure out which room and which edge of that room's footprint was clicked, and the offset along that edge — this is geometry the canvas already has to do to render the wall edges in the first place. Returns the new door's id (`FString`), or empty on failure |

`InputId` is set automatically to the clicking room — the door's `ConnectsTo` ("what's on
the other side") starts blank and is filled in afterwards via the door properties panel
(§6.2), not at add-door time. The new door is auto-selected immediately after placement
(same as prefab rooms), so it can be deleted right away if it's wrong, or so the door
properties panel can open straight away for filling in `ConnectsTo`.

---

## 6. Select mode

Click a room, wall, door marker, or connector marker to select it. Selecting is the
prerequisite for delete/rename/move/resize/properties-panel — it doesn't do anything by
itself.

| UI element | Call / bind | Notes |
|---|---|---|
| Toggle button | `ToggleSelectMode()` | |
| Toggle state indicator | Bind `OnSelectModeChanged(bool)` | |
| Canvas click (while in select mode) | `SelectRoom(RoomId)` **or** `SelectDoor(RoomId, DoorId)` **or** `SelectConnector(GroupId)` | Call whichever matches what the click landed on (you determine this from hit-testing the canvas's own rendered geometry). Pass an empty `RoomId` to `SelectRoom` to deselect (clicking empty space). Only one of a room, a door, or a connector is ever selected at once — selecting one clears the other two automatically |
| — | Bind `OnSelectedRoomChanged(FString RoomId)` / `OnSelectedDoorChanged(FString RoomId, FString DoorId)` / `OnSelectedConnectorChanged(FString GroupId)` | Bind these to know when to show/hide/refresh the matching properties panel, instead of polling every tick |
| Delete button (or `Backspace`) | `DeleteSelection()` | Deletes whichever is currently selected (door takes priority over room; a connector deletes every floor's node for that group — see §6.3). No-op (`false`) if nothing is selected |
| Drag a selected room | `MoveRoomTo(RoomId, const TArray<FVector2D>& NewFootprint)` while dragging, then `CommitRoomMove(RoomId, const TArray<FVector2D>& FallbackFootprint)` once on release | `MoveRoomTo`: call continuously during the drag with the whole footprint translated by the drag delta (not just a position — compute the new footprint yourself each move). No `OnActionFailed` on failure, since it's called every frame of a drag and a stray failure mid-drag shouldn't spam the UI. `CommitRoomMove`: call once when the drag ends, passing the *original* (pre-drag) footprint as `FallbackFootprint` — if the room ended up overlapping something, it's silently reverted to that fallback (this one does broadcast `OnActionFailed`, since it only fires once per drag) |
| Drag a selected door | `MoveDoor(RoomId, DoorId, float NewOffsetWorldUnits)` | Slides the door along its own edge. Call continuously during the drag, same "no failure spam" reasoning as `MoveRoomTo` |
| Resize (`=`/`-` keys currently — test-only UX) | `ResizeRoom(RoomId, float NewWidth, float NewDepth)` | Rescales keeping the room centered where it is. Not wired into UMG yet on my side either — this is the general-purpose function to build a real resize UI against later, not urgent |

### 6.1 Room properties panel

Shown when `OnSelectedRoomChanged` fires with a non-empty `RoomId`.

| UI element | Call / bind | Notes |
|---|---|---|
| Panel title, populated on open | `IsSelectedRoomClosedLoop()` | Returns `true` for an actual closed room, `false` for an open standalone wall (draw-wall mode) - rooms and walls share this same panel/selection, so use this to show "Room property" vs "Wall property" |
| Name field, populated on open | `GetSelectedRoomName()` | Empty if nothing is selected |
| "Apply" button | `SetSelectedRoomName(const FString& NewName)` | Returns `false` if nothing is selected, **or if that name is already used by another room/wall in the building** (names must be unique building-wide, since the door ConnectsTo dropdown resolves a room by its current name - see §6.2). Bind `OnActionFailed` to show the rejection reason |

### 6.2 Door properties panel

Shown when `OnSelectedDoorChanged` fires with non-empty ids.

| UI element | Call / bind | Notes |
|---|---|---|
| "Belongs to room" label (read-only) | `GetSelectedDoorRoomId()` **or** read `InputId` off `GetSelectedDoorOpening` (see below) | Just for display/context — not user-editable |
| Full door data, populated on open | `GetSelectedDoorOpening(FInteriorOpening& OutOpening)` | Returns `false` (leaves `OutOpening` untouched) if no door is selected. Break the returned `FInteriorOpening` struct to read `Id`, `Type`, `EdgeIndex`, `Offset`, `Width`, `InputId`, `ConnectsTo` — you only actually need `InputId` (context label) and `ConnectsTo` (dropdown's current value) for this panel |
| "Connects to" dropdown, options | `GetConnectableTargetIds()` | Returns `"corridor"` followed by every other closed room's **current name** (not id) on the door's floor, excluding the door's own room and excluding open walls. Populate the dropdown options with this list every time the panel opens (room names can change) |
| "Connects to" dropdown, initial selection | `ResolveConnectsToDisplayName(const FString& ConnectsToId)` | Call this on the `ConnectsTo` value read from `GetSelectedDoorOpening` **before** setting the dropdown's selected option — the dropdown's options are names, but the stored value is an id/`"corridor"`, so this converts stored→displayed. Don't set the dropdown's selection directly from the raw `ConnectsTo` string |
| "Apply" button | `SetSelectedDoorConnectsTo(const FString& NewConnectsToLabel)` | Pass the dropdown's currently-selected **label** (name or `"corridor"`), not an id — this function resolves the name back to the room's stable id internally. Returns `false` if no door is selected, or if the label doesn't match any known room name (and isn't `"corridor"`) |

### 6.3 Connector properties panel (elevator/stairs/escalator)

Shown when `OnSelectedConnectorChanged` fires with a non-empty `GroupId`.

| UI element | Call / bind | Notes |
|---|---|---|
| Type label (read-only) | `GetSelectedConnectorType()` | Returns an `EInteriorNavNodeType` (`Elevator`/`Stairs`/`Escalator`; `PathNode` only if nothing is actually selected — never a real value for a placed connector) |
| Floor checklist, populated on open | `GetSelectedConnectorFloors()` | Returns `TArray<int32>` — the floors this connector currently reaches. Check the matching boxes. For sizing the checklist to the building's actual floor count, see `GetLoadedBuildingFloorCount()` below (added 2026-09-20) |
| Floor checklist, hide non-existent floors | `GetLoadedBuildingFloorCount()` | Returns the loaded building's actual floor count (`int32`, 0 if nothing loaded). My own connector panel now checks `FloorCount >= N` per checkbox and sets that checkbox's `Visibility` (Visible/Collapsed) accordingly, instead of always showing a fixed set — worth copying the same pattern for any other floor-scoped UI you build |
| "Apply" button | `SetSelectedConnectorFloors(const TArray<int32>& NewFloors)` | Build the array from whichever boxes are checked. All-or-nothing: if any newly-checked floor is invalid (doesn't exist, position blocked by a wall, outside the building), the **whole call fails** and nothing changes — it won't partially apply. Returns `false` on failure or if nothing is selected. **Every checkbox's exec-flow branch must reconverge onto the same "add to array, then continue" path regardless of checked/unchecked** — see `HANDOFF.md`'s Collaboration Notes if you hit a "nothing happens on Apply" bug, this exact pattern bit me once already |
| Delete button (or `Backspace`, via `DeleteSelection()`) | `DeleteSelectedConnector()` | Removes every floor's node for this connector at once (it's one logical connector, even though it's stored as several per-floor nodes internally). Returns `false` if nothing is selected |

---

## 7. Connector placement (elevator / stairs / escalator)

Arm a type, then a canvas click places a connector there on its default floor set — no
separate "finish" step, same pattern as prefab rooms.

| UI element | Call / bind | Notes |
|---|---|---|
| Elevator button | `ToggleElevatorMode()` | Pressing the same button again, or any other mode's button, disarms it |
| Stairs button | `ToggleStairsMode()` | ″ |
| Escalator button | `ToggleEscalatorMode()` | ″ |
| Any armed-state indicator | `IsInConnectorMode()` | No changed-event yet — poll after each toggle call |
| Canvas click (while armed) | `PlaceConnectorAt(FVector2D FootprintPosition)` | Returns the new connector's shared `ConnectorGroupId` (`FString`), empty on failure or if nothing is armed. Default floor set: an elevator defaults to **every floor in the building**; stairs/escalator default to **the floor above and below the currently active floor** (clamped to valid range). Change which floors it reaches afterwards via the connector properties panel (§6.3), not at placement time |

---

## 8. Snapping

Not a mode — a standing on/off setting that applies during draw-room point placement and
room dragging, regardless of which mode/tool is active.

| UI element | Call / bind | Notes |
|---|---|---|
| Snap on/off toggle | `ToggleSnapEnabled()` | |
| Toggle state, populate on open | `IsSnapEnabled() ` | Defaults to `true`. No changed-event — poll after toggling |

When on, placing a room point or dragging a room snaps to nearby vertices/edges of other
rooms, walls, and the building's own exterior wall, within a small tolerance — entirely
handled in C++ (`AInteriorGenerator::FindSnapOffset`), nothing extra the UI needs to do
besides the toggle itself.

---

## 9. Floor management

| UI element | Call / bind | Notes |
|---|---|---|
| Floor indicator | `GetActiveFloor()` | |
| Floor up/down | `SetActiveFloor(int32 Level)` | |
| "Add floor" button | `AddFloor()` | Returns the new floor count |
| "Remove floor" button | `RemoveFloor(int32 Level)` | Typically pass `GetActiveFloor()` — removing the floor you're currently viewing, not always the top one, matches the existing keyboard UX (`X` key) |

---

## 10. Do not build against these (test-only scaffolding)

None currently active on the interior side. The `=`/`-` resize keys
(`GrowSelectedRoom`/`ShrinkSelectedRoom` in C++) are test-only in spirit but the
underlying `ResizeRoom(RoomId, NewWidth, NewDepth)` they call is the real, permanent
function — build against `ResizeRoom` directly rather than trying to mirror the
+/-20-units behavior.

---

## 11. Still open

1. Prefab placement, add-door mode, and connector placement don't broadcast a
   mode-changed event yet (only draw room/wall and select mode do) — button highlighting
   needs to poll the `Is...Mode()` getter right after calling the toggle. Let me know if
   you want events added; it's the same pattern as the existing ones.
2. ~~Connector properties panel has no "how many floors does this building actually
   have" call wired up~~ — **fixed 2026-09-20**, see `GetLoadedBuildingFloorCount()`
   in §6.3 above.
3. No dedicated placement tool for a plain `PathNode` (only Elevator/Stairs/Escalator
   have one) — not needed for M1, flagging in case T04 (navigation) ends up wanting one.
4. Prefab rooms have no name/public/key fields at placement time (only draw-room mode's
   `FinishRoom` collects those) — reuse the room properties panel (§6.1) to rename after
   placing, or ping me if you want a full form before placement instead.
5. Door/room/connector failure reasons mostly only go to the Output Log right now —
   `OnActionFailed(FString Reason)` (Category = "Feedback") does cover some of these
   (room drag revert, connector floor-edit rejection) but not every failure path yet
   (e.g. `FinishRoom` returning `nullptr`). Bind `OnActionFailed` once for the ones
   that are covered; let me know which specific failures you need surfaced next.
