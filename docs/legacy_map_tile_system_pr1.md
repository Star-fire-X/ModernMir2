# Legacy Map and Tile System PR-1 Audit

This document captures the Delphi-compatible map and tile behavior that must be
preserved before implementing the C++ migration work. It is intentionally scoped
to audit output only; it does not propose a new tile engine.

## Scope

PR-1 deliverables:

- `.map` file format notes.
- WIL/WIX tile and object resource mapping.
- Client draw order trace.
- Server movement and blocking rule table.
- Map transfer and visibility message order.
- Open questions that require further Delphi source verification.

Reviewed Delphi sources:

- `Source/Client/MapUnit.pas`
- `Source/Client/PlayScn.pas`
- `Source/Client/DrawScrn.pas`
- `Source/Client/ClMain.pas`
- `Source/Client/WIL/Path.pas`
- `Source/M2Server/Envir.pas`
- `Source/M2Server/ObjBase.pas`
- `Source/Common/Grobal2.pas`

Reviewed C++ sources:

- `shared/legacy/map_document.hpp`
- `ModernClient/src/assets/asset_manager.cpp`
- `ModernClient/src/scene/scenes.cpp`
- `ModernClient/src/shared/legacy/map_render_math.hpp`
- `ModernServer/src/world/legacy_map_environment.cpp`
- `ModernServer/src/world/map_actor.cpp`
- `ModernServer/src/world/map_actor_movement.hpp`
- `ModernServer/src/world/map_actor_visibility.hpp`
- `ModernServer/src/world/map_actor_packets.hpp`

## Delphi Client Map Pipeline

The high-level frame order in `ClMain.pas` is:

```text
Timer1Timer
→ DecodeMessagePacket
→ ProcessKeyMessages
→ ProcessActionMessages
→ DScreen.DrawScreen
→ CurrentScene.PlayScene
→ DWinMan.DirectPaint
→ DScreen.DrawScreenTop
→ DScreen.DrawHint
→ DXDraw1.Flip
```

`DrawScrn.pas` calls the current scene's `PlayScene` method. When the current
scene is the game scene, map and actor rendering happen before UI direct paint,
screen-top drawing, hint drawing, and flip.

## .map File Format

Normal client map header in `MapUnit.pas`:

```pascal
TMapHeader = packed record
  Width: word;
  Height: word;
  Title: string[20];
  UpdateDate: TDateTime;
  Reserved: array[0..18] of char;
end;
```

AntiHack client map header:

```pascal
TMapHeader_AntiHack = packed record
  Title: string[30];
  Width: word;
  CheckKey: word;
  Height: word;
  UpdateDate: TDateTime;
  Reserved: array[0..18] of char;
end;
```

The client uses the AntiHack header for `LABY01`, `LABY02`, `LABY03`,
`LABY04`, and `SNAKE`. For those maps:

- `Width = encoded_width xor CheckKey`
- `Height = encoded_height xor CheckKey`
- each cell's `BkImg`, `MidImg`, and `FrImg` are XOR decoded with `CheckKey`

The server `Envir.pas` currently reads a normal 52-byte header and does not show
an AntiHack branch in the reviewed source. That is a compatibility gap to verify
before using those maps server-side.

Cell layout is 12 bytes and column-major in the file:

```text
offset  size  field       meaning
0       2     BkImg       background tile id; bit 15 is blocking
2       2     MidImg      SmTiles id
4       2     FrImg       Objects id; bit 15 is blocking
6       1     DoorIndex   bit 7 marks door tile; low 7 bits are door group
7       1     DoorOffset  bit 7 marks open door; low 7 bits are frame offset
8       1     AniFrame    bit 7 marks blend; low 7 bits are animation frames
9       1     AniTick     animation delay factor
10      1     Area        Objects archive selector
11      1     Light       cell light value
```

Compatibility requirements:

- Read little-endian fields manually.
- Do not `reinterpret_cast` C++ structs onto Delphi records.
- Preserve raw ids even if a resource frame is missing.
- Treat missing frame lookups like Delphi `nil`: skip drawing.
- Keep static map data separate from runtime client draw state and server
  dynamic occupancy.

## Resource Mapping

Delphi resource constants are defined in `Source/Client/WIL/Path.pas`.

```text
map field       Delphi library       C++ archive             index rule
BkImg           Data/Tiles.wil        ArchiveId::tiles        (BkImg & $7FFF) - 1
MidImg          Data/SmTiles.wil      ArchiveId::sm_tiles     MidImg - 1
FrImg Area 0    Data/Objects.wil      ArchiveId::objects1     computed object frame
FrImg Area 1    Data/Objects2.wil     ArchiveId::objects2     computed object frame
FrImg Area 2    Data/Objects3.wil     ArchiveId::objects3     computed object frame
FrImg Area 3    Data/Objects4.wil     ArchiveId::objects4     computed object frame
FrImg Area 4    Data/Objects5.wil     ArchiveId::objects5     computed object frame
FrImg Area 5    Data/Objects6.wil     ArchiveId::objects6     computed object frame
FrImg Area 6    Data/Objects7.wil     ArchiveId::objects7     computed object frame
minimap         Data/mmap.wil         ArchiveId::mmap         MiniMapInfo index
dropped items   Data/DnItems.wil      ArchiveId::dn_items     Looks
```

Object frame computation from `PlayScn.pas`:

```text
fridx = FrImg & $7FFF
blend = false
ani = AniFrame
if (ani & $80) != 0:
  blend = true
  ani = ani & $7F
if ani > 0:
  fridx += (MainAniCount mod (ani + ani * AniTick)) div (1 + AniTick)
if (DoorOffset & $80) != 0 and (DoorIndex & $7F) > 0:
  fridx += DoorOffset & $7F
frame = fridx - 1
archive = Objects[Area], falling back to Objects.wil for unexpected areas
```

## Client Draw Order Trace

The map scene in `PlayScn.pas` is row-order driven, not a modern independent
layer stack.

Actual order:

```text
1. Update Map.ClientRect around Myself.Rx/Myself.Ry.
2. Map.UpdateMapPos(Myself.Rx, Myself.Ry).
3. DrawTileMap:
   3.1 Draw all BkImg tiles to MapSurface.
       Only draw when source x and y are both even.
   3.2 Draw all MidImg tiles to MapSurface.
4. Copy the visible MapSurface rectangle into ObjSurface.
5. First FrImg pass:
   draw 48x32 object frames as low/small objects.
6. Draw GroundEffectList.
7. Main row scan from viewport top to bottom + LONGHEIGHT_IMAGE:
   7.1 Draw non-48x32 FrImg objects for this row.
   7.2 If row is inside visible bottom:
       draw events for this row.
       draw dropped items for this row.
       draw actors whose draw row matches this row.
       draw FlyList effects for this row.
8. Redraw Myself, FocusCret, and MagicTarget in blend/highlight mode.
9. Draw actor-attached effects.
10. Draw EffectList.
11. Draw dropped item flash effects.
12. Apply death grayscale if Myself is dead.
13. Copy ObjSurface to the main surface.
14. Draw mini map when enabled.
15. UI direct paint, screen top, hint, flip happen after scene draw.
```

Actor row rule:

```text
actor draw row = actor.Ry - Map.BlockTop - actor.DownDrawLevel
```

The corresponding C++ concept is:

```text
render_tiles_background
→ render_tiles_mid
→ render_small_objects_48x32
→ render_ground_effects
→ for row:
    render_large_objects_for_row
    render_events_for_row
    render_ground_items_for_row
    render_actors_for_row
    render_fly_effects_for_row
→ render_actor_selection_blend_pass
→ render_actor_effects
→ render_overlay_effects
→ render_item_flash
→ render_minimap
→ UI / hint / present
```

Important risk: batching must not reorder draw calls across row boundaries.

## Coordinates and Viewport

Confirmed constants:

```text
UNITX = 48
UNITY = 32
HALFX = 24
HALFY = 16
LOGICALMAPUNIT = 40
visible left = self.Rx - 9
visible top = self.Ry - 9
visible right = self.Rx + 9
visible bottom = self.Ry + 8
```

The map is drawn as an orthogonal 48x32 grid. It is not an isometric transform.
Movement animation uses logical tile coordinates plus pixel `ShiftX/ShiftY`.

C++ `ModernClient/src/shared/legacy/map_render_math.hpp` already encodes most of
these constants and should remain the single source for viewport math.

## Client Blocking Helpers

`MapUnit.pas` client-side movement checks:

```text
CanMove(x,y):
  false if BkImg bit15 or FrImg bit15 is set.
  false if this is a door tile and DoorOffset bit7 is not set.

CanFly(x,y):
  false if FrImg bit15 is set.
  false if this is a door tile and DoorOffset bit7 is not set.

MarkCanWalk(x,y,true):
  clears FrImg bit15.

MarkCanWalk(x,y,false):
  sets FrImg bit15.
```

Client checks are predictive/display-side. Server remains authoritative.

## Server Blocking and Occupancy

`Envir.pas` loads map blocking from static cell bits:

```text
if BkImg bit15 is set: MoveAttr = wall
if FrImg bit15 is set: MoveAttr = high wall
```

Dynamic cell objects are stored in each map cell's `ObjList`.

`CanWalk(x,y,allowdup)`:

- returns false out of range.
- requires `MoveAttr = MP_CANMOVE`.
- if `allowdup = false`, rejects a cell containing an `OS_MOVINGOBJECT` where:
  - not ghost
  - hold place
  - not dead
  - not hidden
  - not supervisor mode

`MoveToMovingObject(old_x,old_y,obj,new_x,new_y,allowdup)`:

```text
-1 = target is static-blocked or out of range
 0 = occupied by another blocking moving object
 1 = movement succeeded
```

On success, the old moving object entry is removed before the new one is added.

Item placement:

- Requires static movable cell.
- Gold can merge up to `BAGGOLD`.
- Otherwise a cell accepts at most 5 item objects.
- Items do not block movement.

Door grouping:

- Server groups door tiles by low 7-bit door number and proximity within about
  10 tiles.
- Door cores track open state, lock state, key, and open time.
- Gate transfer checks `ArroundDoorOpened` before crossing.

C++ `LegacyMapEnvironment` already models most of this. Known gaps:

- shared server map decoder does not yet support AntiHack maps.
- `can_fly_line` currently differs from reviewed Delphi `CanFly` behavior and
  must be reconciled before PR-6.

## Map Transfer Order

Reviewed Delphi server flow in `ObjBase.pas`:

```text
TCreature.Walk
→ checks current cell ObjList for gate/event/door
→ if gate and doors around are open:
   EnterAnotherMap(target_env, target_x, target_y)
```

`EnterAnotherMap`:

```text
1. validate level, map quest, required quest mark, target cell, castle rule
2. save old environment and position
3. Disappear from old map
4. clear message target and visible item/event/actor tracking
5. continue target map appearance and synchronization
```

Current C++ cross-map flow in `MapActor::try_gate_transfer` and
`try_item_map_move`:

```text
queue SM_CLEAROBJECTS
queue SM_CHANGEMAP
queue character save
detach slaves
remove actor from visibility
delete moving object from old map
erase local object
enqueue cross-map spawn_player mail
```

This is close to legacy behavior, but PR-7 must verify exact packet order against
Delphi client handling of `SM_CLEAROBJECTS`, `SM_CHANGEMAP`, `SM_NEWMAP`, and
object appearance messages.

## Visibility and Message Order

Relevant packet ids in `Grobal2.pas` and C++ `legacy_types.hpp`:

```text
SM_TURN          10
SM_WALK          11
SM_RUN           13
SM_DISAPPEAR     30
SM_NEWMAP        51
SM_MAPDESCRIPTION 54
SM_ITEMSHOW      610
SM_ITEMHIDE      611
SM_OPENDOOR_OK   612
SM_CLOSEDOOR     614
SM_CLEAROBJECTS  633
SM_CHANGEMAP     634
SM_MAGICFIRE     638
SM_AREA_STATE    708
```

Relevant client commands:

```text
CM_TURN       3010
CM_WALK       3011
CM_RUN        3013
CM_HIT        3014
CM_SPELL      3017
CM_DROPITEM   1000
CM_PICKUP     1001
CM_OPENDOOR   1002
CM_CLICKNPC   1010
CM_DROPGOLD   1016
CM_WANTMINIMAP 1033
```

C++ `dispatch_login_sequence` currently sends:

```text
SM_NEWMAP
SM_LOGON
SM_USERNAME
SM_AREA_STATE
SM_MAPDESCRIPTION
SM_ABILITY
SM_SENDUSEITEMS
SM_SENDMYMAGIC
```

Visibility synchronization then sends actor and item appearances. PR-9 must
prove that legacy protocol and `client_v1` apply these messages in the same
order as the server generated them.

## Current C++ Reuse Points

Use as-is unless tests prove a difference:

- WIL/WIX frame loading and nil-frame behavior in `AssetManager`.
- `LegacyMapViewport` constants and helper functions.
- `AnimationManager::map_object_frame`.
- `LegacyMapEnvironment` dynamic object model.
- `MapActor` door core grouping.
- `MapActor` visibility sets.

Use with changes:

- `shared/legacy/map_document.hpp`: add AntiHack support and structured errors.
- `WorldScene` map draw passes: split background and mid tile trace phases.
- `LegacyMapEnvironment::can_fly_line`: align with Delphi `CanFly`.
- Client world state: add or enforce map generation for stale message discard.

Avoid:

- ECS-style map object redesign.
- Generic tile layer abstraction that hides row scan order.
- Cross-row D3D batching.
- Delta-time camera interpolation that changes `ShiftX/ShiftY` feel.

## Verification Plan for Later PRs

Minimum fixtures:

- normal `.map` header decode
- AntiHack `.map` header decode
- column-major cell ordering
- BkImg and FrImg blocking
- object frame animation
- door open/closed frame offset
- Objects area 0..6 mapping
- 48x32 object first pass
- non-48x32 row pass
- actor row ordering
- dropped item before actor
- fly effect after actor
- map transfer clear/change/new map order
- stale old-map message discard
- minimap coordinate scaling

Golden trace format should record at least:

```text
phase,row,x,y,archive,index,blend,screen_x,screen_y
```

Golden image tests are useful only after trace equality is established.

## Open Questions

These are not safe to infer without further source or runtime verification:

- Whether the Delphi server has an AntiHack map branch elsewhere.
- The exact `CanFly` stepping behavior; the reviewed source appears to use a
  10-step rounded check and must be reproduced precisely.
- Whether foreground objects affect click hit testing or only rendering.
- Full light and darkness composition, including `DrawLight`, actor light,
  spell light, and map day/night state.
- Exact ordering of `SendRefMsg`, visible actor insertion, item show/hide, and
  disappear messages during same-frame movement/death/drop events.
- Mini map NPC/group/guild point rendering rules beyond the visible actor point
  logic in `PlayScn.pas`.
- Sound and persistent effect cleanup during map switching.

## PR-1 Acceptance Criteria

PR-1 is complete when this document exists and covers:

- map file header and cell layout
- resource mapping
- client draw order trace
- server static and dynamic blocking semantics
- map transfer order
- visibility/protocol ids
- C++ reuse and known gap list
- open verification questions

